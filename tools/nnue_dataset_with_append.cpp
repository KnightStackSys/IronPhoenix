#include "ironphoenix/fen.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/search.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ironphoenix {
namespace {

constexpr std::array<char, 8> DATA_MAGIC = {'I', 'P', 'D', 'A', 'T', 'A', '1', '\0'};
constexpr std::uint32_t DATA_VERSION = 1;
constexpr std::uint32_t MAX_FEATURES_PER_STREAM = 64;
constexpr std::streamoff RECORD_COUNT_OFFSET = 36;

constexpr std::string_view MODERN_START_FEN =
    "R-0,0,0,0-1,1,1,1-1,1,1,1-0,0,0,0-0-"
    "x,x,x,yR,yN,yB,yK,yQ,yB,yN,yR,x,x,x/"
    "x,x,x,yP,yP,yP,yP,yP,yP,yP,yP,x,x,x/"
    "x,x,x,8,x,x,x/"
    "bR,bP,10,gP,gR/"
    "bN,bP,10,gP,gN/"
    "bB,bP,10,gP,gB/"
    "bQ,bP,10,gP,gK/"
    "bK,bP,10,gP,gQ/"
    "bB,bP,10,gP,gB/"
    "bN,bP,10,gP,gN/"
    "bR,bP,10,gP,gR/"
    "x,x,x,8,x,x,x/"
    "x,x,x,rP,rP,rP,rP,rP,rP,rP,rP,x,x,x/"
    "x,x,x,rR,rN,rB,rQ,rK,rB,rN,rR,x,x,x";

struct Options final {
    std::string output = "phoenix_dataset.ipd";
    std::uint64_t positions = 500000;
    int depth = 8;
    int randomPlies = 8;
    int skipPlies = 12;
    int sampleEvery = 3;
    int maxPlies = 320;
    int cpClamp = 4000;
    float scoreScale = 400.0f;
    std::size_t hashMB = 16;
    unsigned workers = 1;
    std::uint64_t seed = 0x49524f4e50484f45ULL;
    bool seedExplicit = false;
    bool append = false;
    bool dedup = true;

    // Controlled exploration: normally play MultiPV #1. With this probability,
    // sample a nearby alternative from the top MultiPV lines.
    double exploration = 0.15;
    int explorationMultiPV = 4;
    double explorationTemperature = 120.0;
    int explorationMaxLoss = 250;
};

struct DatasetHeader final {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t featureCount = 0;
    std::uint32_t maxFeatures = 0;
    std::uint32_t depth = 0;
    float scoreScale = 0.0f;
    std::uint64_t seed = 0;
    std::uint64_t recordCount = 0;
    std::streamoff dataOffset = 0;
};

struct DatasetScan final {
    DatasetHeader header{};
    std::uint32_t maxGameId = 0;
    bool hasGameId = false;
    std::streamoff dataEnd = 0;
    bool hasTrailingBytes = false;
};

struct Record final {
    std::vector<std::uint16_t> own;
    std::vector<std::uint16_t> partner;
    float target = 0.0f;
    std::int32_t teacherCp = 0;
    std::uint32_t gameId = 0;
    std::uint16_t ply = 0;
    std::uint8_t sideToMove = 0;
    std::uint8_t flags = 0;
};

struct RootCandidate final {
    Move move{};
    int cp = 0;
    bool mate = false;
    int rank = 1;
};

struct TeacherResult final {
    bool ok = false;
    bool bestMate = false;
    int bestCp = 0;
    Move move{};
    bool explored = false;
    std::vector<RootCandidate> candidates;
};

struct GenerationStats final {
    std::atomic<std::uint64_t> duplicateSamples{0};
    std::atomic<std::uint64_t> exploredMoves{0};
};

template <typename T>
bool readValue(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
bool writeValue(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

std::uint64_t mix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::uint64_t featureFingerprint(
    const std::vector<std::uint16_t>& own,
    const std::vector<std::uint16_t>& partner,
    std::uint8_t sideToMove) noexcept {

    std::uint64_t hash = mix64(static_cast<std::uint64_t>(sideToMove) + 0x50484f454e4958ULL);
    hash ^= mix64(static_cast<std::uint64_t>(own.size()) << 32);

    for (std::uint16_t feature : own)
        hash = mix64(hash ^ (static_cast<std::uint64_t>(feature) + 0x10001ULL));

    hash = mix64(hash ^ 0xA55AA55AA55AA55AULL);
    hash ^= mix64(static_cast<std::uint64_t>(partner.size()) << 33);

    for (std::uint16_t feature : partner)
        hash = mix64(hash ^ (static_cast<std::uint64_t>(feature) + 0x20003ULL));

    return hash;
}

class DatasetDeduper final {
public:
    void reserve(std::size_t count) {
        std::lock_guard lock(mutex_);
        seen_.reserve(count);
    }

    void preload(std::uint64_t fingerprint) {
        seen_.insert(fingerprint);
    }

    bool claim(std::uint64_t fingerprint) {
        std::lock_guard lock(mutex_);
        return seen_.insert(fingerprint).second;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return seen_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<std::uint64_t> seen_;
};

bool readHeader(std::istream& in, DatasetHeader& header, std::string& error) {
    if (!in.read(header.magic.data(), static_cast<std::streamsize>(header.magic.size()))) {
        error = "dataset header is truncated";
        return false;
    }
    if (header.magic != DATA_MAGIC) {
        error = "not an IronPhoenix IPDATA1 dataset";
        return false;
    }

    if (!readValue(in, header.version)
        || !readValue(in, header.featureCount)
        || !readValue(in, header.maxFeatures)
        || !readValue(in, header.depth)
        || !readValue(in, header.scoreScale)
        || !readValue(in, header.seed)
        || !readValue(in, header.recordCount)) {
        error = "dataset header is truncated";
        return false;
    }

    if (header.version != DATA_VERSION) {
        error = "unsupported dataset version " + std::to_string(header.version);
        return false;
    }
    if (header.featureCount != NNUE::FEATURE_COUNT) {
        error = "dataset feature count does not match this PhoenixNet build";
        return false;
    }
    if (header.maxFeatures != MAX_FEATURES_PER_STREAM) {
        error = "dataset maximum sparse feature count is incompatible";
        return false;
    }
    if (!std::isfinite(header.scoreScale) || header.scoreScale <= 0.0f) {
        error = "dataset score scale is invalid";
        return false;
    }

    header.dataOffset = static_cast<std::streamoff>(in.tellg());
    return true;
}

bool readFeatureVector(
    std::istream& in,
    std::uint16_t count,
    std::vector<std::uint16_t>& output) {

    output.resize(count);
    if (count == 0)
        return true;

    return static_cast<bool>(in.read(
        reinterpret_cast<char*>(output.data()),
        static_cast<std::streamsize>(count * sizeof(std::uint16_t))));
}

bool scanDataset(
    const std::filesystem::path& path,
    DatasetScan& scan,
    DatasetDeduper* deduper,
    std::string& error) {

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "unable to open dataset";
        return false;
    }

    if (!readHeader(in, scan.header, error))
        return false;

    if (deduper != nullptr)
        deduper->reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
            scan.header.recordCount * 2ULL + 1024ULL,
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))));

    std::vector<std::uint16_t> own;
    std::vector<std::uint16_t> partner;

    for (std::uint64_t i = 0; i < scan.header.recordCount; ++i) {
        std::uint16_t ownCount = 0;
        std::uint16_t partnerCount = 0;
        float target = 0.0f;
        std::int32_t teacherCp = 0;
        std::uint32_t gameId = 0;
        std::uint16_t ply = 0;
        std::uint8_t side = 0;
        std::uint8_t flags = 0;

        if (!readValue(in, ownCount)
            || !readValue(in, partnerCount)
            || !readValue(in, target)
            || !readValue(in, teacherCp)
            || !readValue(in, gameId)
            || !readValue(in, ply)
            || !readValue(in, side)
            || !readValue(in, flags)) {
            error = "dataset is truncated while reading record " + std::to_string(i);
            return false;
        }

        if (ownCount > scan.header.maxFeatures || partnerCount > scan.header.maxFeatures) {
            error = "dataset record has an invalid sparse feature count";
            return false;
        }

        if (!readFeatureVector(in, ownCount, own)
            || !readFeatureVector(in, partnerCount, partner)) {
            error = "dataset is truncated inside sparse features";
            return false;
        }

        if (deduper != nullptr)
            deduper->preload(featureFingerprint(own, partner, side));

        if (!scan.hasGameId || gameId > scan.maxGameId) {
            scan.maxGameId = gameId;
            scan.hasGameId = true;
        }
    }

    scan.dataEnd = static_cast<std::streamoff>(in.tellg());
    char extra = 0;
    scan.hasTrailingBytes = static_cast<bool>(in.read(&extra, 1));
    return true;
}

class DatasetWriter final {
public:
    DatasetWriter(
        const Options& options,
        const DatasetScan* existing,
        std::string& error)
        : targetAdded_(options.positions) {

        const std::filesystem::path path(options.output);

        if (existing != nullptr) {
            if (existing->hasGameId
                && existing->maxGameId == std::numeric_limits<std::uint32_t>::max()) {
                error = "cannot append: game id space is exhausted";
                return;
            }

            baseCount_ = existing->header.recordCount;
            gameOffset_ = existing->hasGameId ? existing->maxGameId + 1u : 0u;
            countPosition_ = RECORD_COUNT_OFFSET;

            stream_.open(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!stream_) {
                error = "unable to open existing dataset for append";
                return;
            }
            stream_.seekp(existing->dataEnd);
            if (!stream_) {
                error = "unable to seek to dataset append position";
                return;
            }
        }
        else {
            stream_.open(
                path,
                std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
            if (!stream_) {
                error = "unable to create output dataset";
                return;
            }

            stream_.write(DATA_MAGIC.data(), static_cast<std::streamsize>(DATA_MAGIC.size()));
            writeValue(stream_, DATA_VERSION);
            writeValue(stream_, static_cast<std::uint32_t>(NNUE::FEATURE_COUNT));
            writeValue(stream_, MAX_FEATURES_PER_STREAM);
            writeValue(stream_, static_cast<std::uint32_t>(options.depth));
            writeValue(stream_, options.scoreScale);
            writeValue(stream_, options.seed);
            countPosition_ = static_cast<std::streamoff>(stream_.tellp());
            const std::uint64_t zero = 0;
            writeValue(stream_, zero);

            if (!stream_) {
                error = "failed while writing dataset header";
                return;
            }
        }

        good_ = true;
    }

    ~DatasetWriter() {
        if (stream_)
            stream_.flush();
    }

    [[nodiscard]] bool good() const noexcept { return good_; }
    [[nodiscard]] std::uint64_t addedCount() const noexcept {
        return added_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t totalCount() const noexcept {
        return baseCount_ + addedCount();
    }
    [[nodiscard]] std::uint64_t targetAdded() const noexcept { return targetAdded_; }

    bool tryWrite(const Record& record) {
        if (record.own.size() > MAX_FEATURES_PER_STREAM
            || record.partner.size() > MAX_FEATURES_PER_STREAM)
            return false;

        std::uint64_t current = added_.load(std::memory_order_relaxed);
        while (current < targetAdded_) {
            if (added_.compare_exchange_weak(
                    current,
                    current + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
                break;
        }

        if (current >= targetAdded_)
            return true;

        const std::uint64_t adjustedGameId64 =
            static_cast<std::uint64_t>(gameOffset_) + record.gameId;
        if (adjustedGameId64 > std::numeric_limits<std::uint32_t>::max())
            return false;
        const auto adjustedGameId = static_cast<std::uint32_t>(adjustedGameId64);

        std::lock_guard lock(mutex_);
        if (!stream_ || committed_)
            return false;

        const auto ownCount = static_cast<std::uint16_t>(record.own.size());
        const auto partnerCount = static_cast<std::uint16_t>(record.partner.size());

        writeValue(stream_, ownCount);
        writeValue(stream_, partnerCount);
        writeValue(stream_, record.target);
        writeValue(stream_, record.teacherCp);
        writeValue(stream_, adjustedGameId);
        writeValue(stream_, record.ply);
        writeValue(stream_, record.sideToMove);
        writeValue(stream_, record.flags);

        if (!record.own.empty()) {
            stream_.write(
                reinterpret_cast<const char*>(record.own.data()),
                static_cast<std::streamsize>(record.own.size() * sizeof(std::uint16_t)));
        }
        if (!record.partner.empty()) {
            stream_.write(
                reinterpret_cast<const char*>(record.partner.data()),
                static_cast<std::streamsize>(record.partner.size() * sizeof(std::uint16_t)));
        }

        return static_cast<bool>(stream_);
    }

    bool commit(std::string& error) {
        std::lock_guard lock(mutex_);
        if (committed_)
            return true;
        if (!stream_) {
            error = "dataset stream is not writable";
            return false;
        }

        // Flush record bytes first. The authoritative count is updated last.
        // If the process dies before the count update, a future append trims
        // the uncommitted trailing bytes discovered by scanDataset().
        stream_.flush();
        if (!stream_) {
            error = "failed to flush dataset records";
            return false;
        }

        const std::uint64_t count = totalCount();
        stream_.seekp(countPosition_);
        if (!writeValue(stream_, count)) {
            error = "failed to update dataset record count";
            return false;
        }
        stream_.flush();
        if (!stream_) {
            error = "failed to commit dataset record count";
            return false;
        }

        committed_ = true;
        return true;
    }

private:
    std::fstream stream_;
    std::streamoff countPosition_ = RECORD_COUNT_OFFSET;
    std::uint64_t baseCount_ = 0;
    std::uint64_t targetAdded_ = 0;
    std::uint32_t gameOffset_ = 0;
    std::atomic<std::uint64_t> added_{0};
    std::mutex mutex_;
    bool good_ = false;
    bool committed_ = false;
};

Direction kingSideDirection(Color c) noexcept {
    switch (c) {
    case RED:    return EAST;
    case BLUE:   return SOUTH;
    case YELLOW: return WEST;
    case GREEN:  return NORTH;
    }
    return EAST;
}

Square findCastlingRook(const Position& pos, Color c, Square king, Direction direction) noexcept {
    for (Square sq = Geometry::step(king, direction);
         sq != SQ_NONE;
         sq = Geometry::step(sq, direction)) {
        if (pos.pieceAt(sq) == makePiece(c, ROOK))
            return sq;
    }
    return SQ_NONE;
}

void configureStandardCastling(Position& pos) noexcept {
    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color c = static_cast<Color>(ci);
        const Square king = pos.kingSquare(c);
        if (king == SQ_NONE)
            continue;

        for (unsigned laneIndex = 0; laneIndex < 2; ++laneIndex) {
            Direction direction = kingSideDirection(c);
            if (laneIndex == 1)
                direction = Geometry::opposite(direction);

            const Square rook = findCastlingRook(pos, c, king, direction);
            const Square rookTo = Geometry::step(king, direction);
            const Square kingTo = rookTo == SQ_NONE
                ? SQ_NONE
                : Geometry::step(rookTo, direction);

            CastleLane lane{};
            if (rook != SQ_NONE && rookTo != SQ_NONE && kingTo != SQ_NONE && kingTo != rook) {
                lane.kingFrom = king;
                lane.rookFrom = rook;
                lane.kingTo = kingTo;
                lane.rookTo = rookTo;
                lane.rightBit = static_cast<std::uint8_t>(2u * ci + laneIndex);
            }
            pos.configureCastle(c, laneIndex, lane);
        }
    }
}

bool resetModern(Position& pos, Fen4State& state, std::string& error) {
    pos.setRulesetId(1);
    if (!setFromFen4(pos, MODERN_START_FEN, state, error))
        return false;
    configureStandardCastling(pos);
    return true;
}

void collectFeatures(
    const Position& pos,
    Color perspective,
    Square anchorKing,
    std::vector<std::uint16_t>& output) {

    output.clear();
    output.reserve(MAX_FEATURES_PER_STREAM);

    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color pieceColor = static_cast<Color>(ci);
        for (unsigned pti = PAWN; pti <= KING; ++pti) {
            const PieceType pt = static_cast<PieceType>(pti);
            Bitboard pieces = pos.pieces(pieceColor, pt);

            while (pieces) {
                const Square sq = popLsb(pieces);
                const std::size_t feature = NNUE::featureIndex(
                    perspective,
                    anchorKing,
                    makePiece(pieceColor, pt),
                    sq);

                if (feature < NNUE::FEATURE_COUNT
                    && feature <= std::numeric_limits<std::uint16_t>::max()) {
                    output.push_back(static_cast<std::uint16_t>(feature));
                }
            }
        }
    }
}

bool parseInfoLine(
    Position& pos,
    const std::string& line,
    int& depth,
    RootCandidate& candidate) {

    if (line.rfind("info ", 0) != 0)
        return false;

    std::istringstream input(line);
    std::string token;
    input >> token; // info

    int parsedDepth = -1;
    int rank = 1;
    bool haveScore = false;
    bool mate = false;
    int score = 0;
    std::string pvMove;

    while (input >> token) {
        if (token == "depth") {
            input >> parsedDepth;
        }
        else if (token == "multipv") {
            input >> rank;
        }
        else if (token == "score") {
            std::string kind;
            input >> kind >> score;
            if (!input)
                return false;
            mate = kind == "mate";
            haveScore = kind == "cp" || kind == "mate";
        }
        else if (token == "pv") {
            input >> pvMove;
            break;
        }
    }

    if (parsedDepth < 0 || rank < 1 || !haveScore || pvMove.empty())
        return false;

    Move move;
    std::string error;
    if (!parseLegalMove(pos, pvMove, move, error))
        return false;

    depth = parsedDepth;
    candidate.move = move;
    candidate.rank = rank;
    candidate.mate = mate;
    candidate.cp = mate
        ? (score > 0 ? 30000 - std::abs(score) : -30000 + std::abs(score))
        : score;
    return true;
}

TeacherResult searchPosition(
    SearchEngine& search,
    Position& pos,
    const Options& options,
    std::mt19937_64& rng) {

    SearchLimits limits;
    limits.depth = options.depth;

    const int requestedMultiPV = options.exploration > 0.0
        ? std::clamp(options.explorationMultiPV, 2, SearchEngine::MAX_MULTI_PV)
        : 1;

    std::ostringstream output;
    if (requestedMultiPV > 1)
        search.startMultiPV(pos, limits, requestedMultiPV, output);
    else
        search.start(pos, limits, output);

    while (search.searching())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    search.stopAndWait();

    int bestDepth = -1;
    std::vector<RootCandidate> finalLines;
    std::string bestMoveText;

    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("bestmove ", 0) == 0) {
            bestMoveText = line.substr(9);
            continue;
        }

        int depth = -1;
        RootCandidate candidate;
        if (!parseInfoLine(pos, line, depth, candidate))
            continue;

        if (depth > bestDepth) {
            bestDepth = depth;
            finalLines.clear();
        }
        if (depth == bestDepth) {
            auto it = std::find_if(
                finalLines.begin(),
                finalLines.end(),
                [&](const RootCandidate& item) { return item.rank == candidate.rank; });
            if (it == finalLines.end())
                finalLines.push_back(candidate);
            else
                *it = candidate;
        }
    }

    std::sort(
        finalLines.begin(),
        finalLines.end(),
        [](const RootCandidate& a, const RootCandidate& b) {
            return a.rank < b.rank;
        });

    TeacherResult result;
    if (finalLines.empty())
        return result;

    const RootCandidate& best = finalLines.front();
    result.ok = true;
    result.bestMate = best.mate;
    result.bestCp = best.cp;
    result.move = best.move;
    result.candidates = finalLines;

    if (best.mate || options.exploration <= 0.0 || finalLines.size() < 2)
        return result;

    std::uniform_real_distribution<double> probability(0.0, 1.0);
    if (probability(rng) >= options.exploration)
        return result;

    std::vector<const RootCandidate*> alternatives;
    std::vector<double> weights;

    for (std::size_t i = 1; i < finalLines.size(); ++i) {
        const RootCandidate& candidate = finalLines[i];
        if (candidate.mate)
            continue;

        const int loss = best.cp - candidate.cp;
        if (loss < 0 || loss > options.explorationMaxLoss)
            continue;

        const double exponent = -static_cast<double>(loss) / options.explorationTemperature;
        alternatives.push_back(&candidate);
        weights.push_back(std::exp(exponent));
    }

    if (alternatives.empty())
        return result;

    std::discrete_distribution<std::size_t> choose(weights.begin(), weights.end());
    result.move = alternatives[choose(rng)]->move;
    result.explored = true;
    return result;
}

Move chooseRandomOpeningMove(Position& pos, std::mt19937_64& rng) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    if (legal.size == 0)
        return {};

    std::array<Move, MAX_MOVES> candidates{};
    std::size_t candidateCount = 0;
    for (Move move : legal) {
        if (!isTerminalKingCapture(pos, move))
            candidates[candidateCount++] = move;
    }

    if (candidateCount == 0)
        return legal.moves[0];

    std::uniform_int_distribution<std::size_t> pick(0, candidateCount - 1);
    return candidates[pick(rng)];
}

void workerMain(
    unsigned workerId,
    const Options& options,
    DatasetWriter& writer,
    DatasetDeduper& deduper,
    GenerationStats& stats,
    std::atomic<std::uint32_t>& nextGameId,
    std::atomic<bool>& failed) {

    SearchEngine search;
    if (!search.setHashSizeMB(options.hashMB)) {
        std::cerr << "worker " << workerId << ": unable to allocate hash\n";
        failed.store(true, std::memory_order_relaxed);
        return;
    }

    std::mt19937_64 rng(
        options.seed
        + 0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(workerId) + 1ULL));

    Position pos;
    Fen4State fenState;
    std::string error;

    const int firstSamplePly = std::max({1, options.randomPlies, options.skipPlies});

    while (!failed.load(std::memory_order_relaxed)
        && writer.addedCount() < writer.targetAdded()) {

        const std::uint32_t gameId = nextGameId.fetch_add(1, std::memory_order_relaxed);

        if (!resetModern(pos, fenState, error)) {
            std::cerr << "worker " << workerId << ": start position failed: " << error << '\n';
            failed.store(true, std::memory_order_relaxed);
            return;
        }
        search.newGame();

        for (int ply = 0; ply < options.maxPlies; ++ply) {
            if (failed.load(std::memory_order_relaxed)
                || writer.addedCount() >= writer.targetAdded())
                break;

            MoveList legal;
            generateLegalMoves(pos, legal);
            if (legal.size == 0)
                break;

            Move move;
            TeacherResult teacher;

            if (ply < options.randomPlies) {
                move = chooseRandomOpeningMove(pos, rng);
            }
            else {
                teacher = searchPosition(search, pos, options, rng);
                if (!teacher.ok)
                    break;

                move = teacher.move;
                if (teacher.explored)
                    stats.exploredMoves.fetch_add(1, std::memory_order_relaxed);

                const bool samplePly = ply >= firstSamplePly
                    && ((ply - firstSamplePly) % options.sampleEvery == 0);

                if (samplePly && !teacher.bestMate) {
                    const int clippedCp = std::clamp(
                        teacher.bestCp,
                        -options.cpClamp,
                        options.cpClamp);

                    Record record;
                    record.target = static_cast<float>(clippedCp) / options.scoreScale;
                    record.teacherCp = teacher.bestCp;
                    record.gameId = gameId;
                    record.ply = static_cast<std::uint16_t>(std::min(ply, 65535));
                    record.sideToMove = static_cast<std::uint8_t>(pos.sideToMove());

                    if (pos.inCheck())
                        record.flags |= 1u;
                    if (pos.aliveMask() != 0xF)
                        record.flags |= 2u;
                    if (teacher.explored)
                        record.flags |= 4u;

                    const Color perspective = pos.sideToMove();
                    const Color partner = static_cast<Color>(
                        static_cast<unsigned>(perspective) ^ 2u);

                    collectFeatures(
                        pos,
                        perspective,
                        pos.kingSquare(perspective),
                        record.own);
                    collectFeatures(
                        pos,
                        perspective,
                        pos.kingSquare(partner),
                        record.partner);

                    if (record.own.size() != record.partner.size()) {
                        std::cerr << "worker " << workerId
                            << ": feature stream size mismatch\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    bool unique = true;
                    if (options.dedup) {
                        const std::uint64_t fingerprint = featureFingerprint(
                            record.own,
                            record.partner,
                            record.sideToMove);
                        unique = deduper.claim(fingerprint);
                    }

                    if (!unique) {
                        stats.duplicateSamples.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (!writer.tryWrite(record)) {
                        std::cerr << "worker " << workerId << ": dataset write failed\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    else {
                        const auto count = writer.addedCount();
                        if (count != 0 && (count % 1000 == 0 || count == writer.targetAdded())) {
                            std::cout << "new positions " << count << '/' << writer.targetAdded()
                                << " total " << writer.totalCount()
                                << " duplicates " << stats.duplicateSamples.load(std::memory_order_relaxed)
                                << " explored " << stats.exploredMoves.load(std::memory_order_relaxed)
                                << " worker " << workerId << '\n';
                        }
                    }
                }
            }

            if (!move)
                break;

            const bool terminalKingCapture = isTerminalKingCapture(pos, move);
            StateInfo state;
            pos.makeMove(move, state);

            if (terminalKingCapture)
                break;
        }
    }
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
    try {
        std::size_t used = 0;
        value = std::stoull(std::string(text), &used);
        return used == text.size();
    }
    catch (...) {
        return false;
    }
}

bool parseSigned(std::string_view text, int& value) {
    try {
        std::size_t used = 0;
        value = std::stoi(std::string(text), &used);
        return used == text.size();
    }
    catch (...) {
        return false;
    }
}

bool parseDouble(std::string_view text, double& value) {
    try {
        std::size_t used = 0;
        value = std::stod(std::string(text), &used);
        return used == text.size() && std::isfinite(value);
    }
    catch (...) {
        return false;
    }
}

void printUsage() {
    std::cout
        << "IronPhoenix PhoenixNet dataset generator\n\n"
        << "Usage: ironphoenix_dataset [options]\n\n"
        << "  --output <file>              Output .ipd file\n"
        << "  --positions <n>              New samples to generate (default 500000)\n"
        << "  --append                     Safely append to an existing compatible file\n"
        << "  --depth <n>                  Teacher search depth (default 8)\n"
        << "  --workers <n>                Parallel self-play workers (default 1)\n"
        << "  --hash <mb>                  Hash MB per worker (default 16)\n"
        << "  --random-plies <n>           Random opening plies (default 8)\n"
        << "  --skip-plies <n>             Earliest sample ply (default 12)\n"
        << "  --sample-every <n>           Save one of every N plies (default 3)\n"
        << "  --max-plies <n>              Maximum game length (default 320)\n"
        << "  --cp-clamp <n>               Teacher CP clamp (default 4000)\n"
        << "  --score-scale <n>            CP per target unit (default 400)\n"
        << "  --exploration <0..1>         Chance to choose a strong non-best move (default 0.15)\n"
        << "  --exploration-multipv <n>    Root lines considered for exploration (default 4)\n"
        << "  --exploration-temperature N  Softmax temperature in CP (default 120)\n"
        << "  --exploration-max-loss <cp>  Largest alternative loss allowed (default 250)\n"
        << "  --dedup                      Enable feature-position deduplication (default)\n"
        << "  --no-dedup                   Disable deduplication\n"
        << "  --seed <n>                   RNG seed\n"
        << "  --help                       Show this help\n";
}

bool parseOptions(int argc, char** argv, Options& options, bool& showedHelp) {
    showedHelp = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc)
                return {};
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage();
            showedHelp = true;
            return false;
        }
        if (arg == "--append") {
            options.append = true;
        }
        else if (arg == "--dedup") {
            options.dedup = true;
        }
        else if (arg == "--no-dedup") {
            options.dedup = false;
        }
        else if (arg == "--output") {
            const auto text = value();
            if (text.empty()) return false;
            options.output = text;
        }
        else if (arg == "--positions") {
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value(), parsed) || parsed == 0) return false;
            options.positions = parsed;
        }
        else if (arg == "--depth") {
            if (!parseSigned(value(), options.depth)
                || options.depth < 1 || options.depth > 32) return false;
        }
        else if (arg == "--workers") {
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value(), parsed) || parsed == 0 || parsed > 128) return false;
            options.workers = static_cast<unsigned>(parsed);
        }
        else if (arg == "--hash") {
            std::uint64_t parsed = 0;
            if (!parseUnsigned(value(), parsed) || parsed == 0 || parsed > 4096) return false;
            options.hashMB = static_cast<std::size_t>(parsed);
        }
        else if (arg == "--random-plies") {
            if (!parseSigned(value(), options.randomPlies) || options.randomPlies < 0) return false;
        }
        else if (arg == "--skip-plies") {
            if (!parseSigned(value(), options.skipPlies) || options.skipPlies < 0) return false;
        }
        else if (arg == "--sample-every") {
            if (!parseSigned(value(), options.sampleEvery) || options.sampleEvery < 1) return false;
        }
        else if (arg == "--max-plies") {
            if (!parseSigned(value(), options.maxPlies) || options.maxPlies < 1) return false;
        }
        else if (arg == "--cp-clamp") {
            if (!parseSigned(value(), options.cpClamp)
                || options.cpClamp < 1 || options.cpClamp >= 29000) return false;
        }
        else if (arg == "--score-scale") {
            double parsed = 0.0;
            if (!parseDouble(value(), parsed) || parsed <= 0.0) return false;
            options.scoreScale = static_cast<float>(parsed);
        }
        else if (arg == "--exploration") {
            if (!parseDouble(value(), options.exploration)
                || options.exploration < 0.0 || options.exploration > 1.0) return false;
        }
        else if (arg == "--exploration-multipv") {
            if (!parseSigned(value(), options.explorationMultiPV)
                || options.explorationMultiPV < 2
                || options.explorationMultiPV > SearchEngine::MAX_MULTI_PV) return false;
        }
        else if (arg == "--exploration-temperature") {
            if (!parseDouble(value(), options.explorationTemperature)
                || options.explorationTemperature <= 0.0) return false;
        }
        else if (arg == "--exploration-max-loss") {
            if (!parseSigned(value(), options.explorationMaxLoss)
                || options.explorationMaxLoss < 0) return false;
        }
        else if (arg == "--seed") {
            if (!parseUnsigned(value(), options.seed)) return false;
            options.seedExplicit = true;
        }
        else {
            std::cerr << "unknown option: " << arg << '\n';
            return false;
        }
    }

    return true;
}

} // namespace
} // namespace ironphoenix

int main(int argc, char** argv) {
    using namespace ironphoenix;

    Options options;
    bool showedHelp = false;
    if (!parseOptions(argc, argv, options, showedHelp)) {
        if (!showedHelp)
            printUsage();
        return showedHelp ? 0 : 1;
    }

    const std::filesystem::path outputPath(options.output);
    DatasetDeduper deduper;
    DatasetScan existing;
    DatasetScan* existingPtr = nullptr;
    std::string error;

    if (options.append && std::filesystem::exists(outputPath)) {
        if (!scanDataset(outputPath, existing, options.dedup ? &deduper : nullptr, error)) {
            std::cerr << "cannot append: " << error << '\n';
            return 1;
        }

        if (existing.header.depth != static_cast<std::uint32_t>(options.depth)) {
            std::cerr << "cannot append: teacher depth differs (existing "
                << existing.header.depth << ", requested " << options.depth << ")\n";
            return 1;
        }
        if (std::fabs(existing.header.scoreScale - options.scoreScale) > 1e-6f) {
            std::cerr << "cannot append: score scale differs\n";
            return 1;
        }

        if (existing.hasTrailingBytes) {
            std::error_code resizeError;
            std::filesystem::resize_file(
                outputPath,
                static_cast<std::uintmax_t>(existing.dataEnd),
                resizeError);
            if (resizeError) {
                std::cerr << "cannot trim stale uncommitted bytes: "
                    << resizeError.message() << '\n';
                return 1;
            }
            std::cout << "trimmed stale uncommitted bytes from interrupted append\n";
        }

        if (!options.seedExplicit) {
            options.seed = existing.header.seed
                ^ mix64(existing.header.recordCount + 1ULL)
                ^ 0xA5A5A5A55A5A5A5AULL;
        }

        existingPtr = &existing;
        std::cout << "append mode: existing positions "
            << existing.header.recordCount << '\n';
        if (options.dedup)
            std::cout << "preloaded dedup fingerprints " << deduper.size() << '\n';
    }
    else if (options.append) {
        std::cout << "append requested but dataset does not exist; creating it\n";
    }

    DatasetWriter writer(options, existingPtr, error);
    if (!writer.good()) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout
        << "PhoenixNet dataset generation\n"
        << "output          " << options.output << '\n'
        << "new positions   " << options.positions << '\n'
        << "depth           " << options.depth << '\n'
        << "workers         " << options.workers << '\n'
        << "hash/worker     " << options.hashMB << " MB\n"
        << "random plies    " << options.randomPlies << '\n'
        << "first sample    " << std::max({1, options.randomPlies, options.skipPlies}) << '\n'
        << "sample every    " << options.sampleEvery << '\n'
        << "exploration     " << options.exploration << '\n'
        << "explore MultiPV " << options.explorationMultiPV << '\n'
        << "explore maxloss " << options.explorationMaxLoss << " cp\n"
        << "dedup           " << (options.dedup ? "on" : "off") << '\n'
        << "seed            " << options.seed << "\n\n";

    std::atomic<std::uint32_t> nextGameId{0};
    std::atomic<bool> failed{false};
    GenerationStats stats;
    std::vector<std::thread> workers;
    workers.reserve(options.workers);

    for (unsigned workerId = 0; workerId < options.workers; ++workerId) {
        workers.emplace_back(
            workerMain,
            workerId,
            std::cref(options),
            std::ref(writer),
            std::ref(deduper),
            std::ref(stats),
            std::ref(nextGameId),
            std::ref(failed));
    }

    for (auto& worker : workers)
        worker.join();

    if (failed.load(std::memory_order_relaxed)) {
        std::cerr << "dataset generation failed; record count was not committed\n";
        return 1;
    }

    if (!writer.commit(error)) {
        std::cerr << "dataset commit failed: " << error << '\n';
        return 1;
    }

    std::cout
        << "done: added " << writer.addedCount()
        << " unique positions; total " << writer.totalCount()
        << "; skipped duplicates " << stats.duplicateSamples.load(std::memory_order_relaxed)
        << "; explored moves " << stats.exploredMoves.load(std::memory_order_relaxed)
        << '\n';

    return 0;
}
