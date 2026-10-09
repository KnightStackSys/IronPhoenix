#include "ironphoenix/fen.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/search.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ironphoenix {
namespace {

constexpr std::array<char, 8> DATA_MAGIC = {'I', 'P', 'D', 'A', 'T', 'A', '1', '\0'};
constexpr std::uint32_t DATA_VERSION = 1;
constexpr std::uint32_t MAX_FEATURES_PER_STREAM = 64;

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
    int randomPlies = 4;
    int skipPlies = 8;
    int sampleEvery = 3;
    int maxPlies = 320;
    int cpClamp = 4000;
    float scoreScale = 400.0f;
    std::size_t hashMB = 16;
    unsigned workers = 1;
    std::uint64_t seed = 0x49524f4e50484f45ULL;
};

struct TeacherResult final {
    bool ok = false;
    bool mate = false;
    int cp = 0;
    Move bestMove{};
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

template <typename T>
void writeValue(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void writeFeatureVector(std::ofstream& out, const std::vector<std::uint16_t>& features) {
    if (!features.empty()) {
        out.write(
            reinterpret_cast<const char*>(features.data()),
            static_cast<std::streamsize>(features.size() * sizeof(std::uint16_t)));
    }
}

class DatasetWriter final {
public:
    DatasetWriter(const Options& options)
        : options_(options), out_(options.output, std::ios::binary | std::ios::trunc) {
        if (!out_)
            return;

        out_.write(DATA_MAGIC.data(), static_cast<std::streamsize>(DATA_MAGIC.size()));
        writeValue(out_, DATA_VERSION);
        writeValue(out_, static_cast<std::uint32_t>(NNUE::FEATURE_COUNT));
        writeValue(out_, MAX_FEATURES_PER_STREAM);
        writeValue(out_, static_cast<std::uint32_t>(options.depth));
        writeValue(out_, options.scoreScale);
        writeValue(out_, options.seed);

        recordCountPos_ = out_.tellp();
        const std::uint64_t zero = 0;
        writeValue(out_, zero);
    }

    ~DatasetWriter() {
        finalize();
    }

    [[nodiscard]] bool good() const noexcept {
        return static_cast<bool>(out_);
    }

    bool write(const Record& record) {
        if (record.own.size() > MAX_FEATURES_PER_STREAM
            || record.partner.size() > MAX_FEATURES_PER_STREAM)
            return false;

        std::lock_guard lock(mutex_);
        if (!out_ || finalized_)
            return false;

        const auto ownCount = static_cast<std::uint16_t>(record.own.size());
        const auto partnerCount = static_cast<std::uint16_t>(record.partner.size());

        writeValue(out_, ownCount);
        writeValue(out_, partnerCount);
        writeValue(out_, record.target);
        writeValue(out_, record.teacherCp);
        writeValue(out_, record.gameId);
        writeValue(out_, record.ply);
        writeValue(out_, record.sideToMove);
        writeValue(out_, record.flags);
        writeFeatureVector(out_, record.own);
        writeFeatureVector(out_, record.partner);

        return static_cast<bool>(out_);
    }

    void finalize() {
        std::lock_guard lock(mutex_);
        if (finalized_ || !out_)
            return;

        const auto end = out_.tellp();
        out_.seekp(recordCountPos_);
        const std::uint64_t count = written_.load(std::memory_order_relaxed);
        writeValue(out_, count);
        out_.seekp(end);
        out_.flush();
        finalized_ = true;
    }

    std::atomic<std::uint64_t> written_{0};

private:
    const Options& options_;
    std::ofstream out_;
    std::streampos recordCountPos_{};
    std::mutex mutex_;
    bool finalized_ = false;
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

Square findCastlingRook(const Position& pos, Color c, Square king, Direction dir) noexcept {
    for (Square sq = Geometry::step(king, dir); sq != SQ_NONE; sq = Geometry::step(sq, dir)) {
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
            Direction dir = kingSideDirection(c);
            if (laneIndex == 1)
                dir = Geometry::opposite(dir);

            const Square rook = findCastlingRook(pos, c, king, dir);
            const Square rookTo = Geometry::step(king, dir);
            const Square kingTo = rookTo == SQ_NONE ? SQ_NONE : Geometry::step(rookTo, dir);

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
    std::vector<std::uint16_t>& out) {

    out.clear();
    out.reserve(MAX_FEATURES_PER_STREAM);

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

                if (feature >= NNUE::FEATURE_COUNT
                    || feature > std::numeric_limits<std::uint16_t>::max())
                    continue;

                out.push_back(static_cast<std::uint16_t>(feature));
            }
        }
    }
}

TeacherResult searchPosition(SearchEngine& search, Position& pos, int depth) {
    SearchLimits limits;
    limits.depth = depth;

    std::ostringstream output;
    search.start(pos, limits, output);

    while (search.searching())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    search.stopAndWait();

    TeacherResult result;
    std::string bestMoveText;
    std::istringstream lines(output.str());
    std::string line;

    enum class ScoreKind { None, Cp, Mate };
    ScoreKind lastScore = ScoreKind::None;
    int lastCp = 0;

    while (std::getline(lines, line)) {
        if (line.rfind("info ", 0) == 0) {
            const std::string cpToken = " score cp ";
            const std::string mateToken = " score mate ";
            const auto cpPos = line.find(cpToken);
            const auto matePos = line.find(mateToken);

            if (cpPos != std::string::npos) {
                const auto begin = cpPos + cpToken.size();
                try {
                    lastCp = std::stoi(line.substr(begin));
                    lastScore = ScoreKind::Cp;
                }
                catch (...) {
                    lastScore = ScoreKind::None;
                }
            }
            else if (matePos != std::string::npos) {
                lastScore = ScoreKind::Mate;
            }
        }
        else if (line.rfind("bestmove ", 0) == 0) {
            bestMoveText = line.substr(9);
        }
    }

    if (bestMoveText.empty() || bestMoveText == "0000")
        return result;

    Move bestMove;
    std::string error;
    if (!parseLegalMove(pos, bestMoveText, bestMove, error))
        return result;

    result.ok = true;
    result.bestMove = bestMove;
    result.mate = lastScore == ScoreKind::Mate;
    if (lastScore == ScoreKind::Cp)
        result.cp = lastCp;
    else if (lastScore == ScoreKind::None)
        result.ok = false;

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

    std::uniform_int_distribution<std::size_t> distribution(0, candidateCount - 1);
    return candidates[distribution(rng)];
}

bool reserveRecordSlot(std::atomic<std::uint64_t>& count, std::uint64_t limit) {
    std::uint64_t current = count.load(std::memory_order_relaxed);
    while (current < limit) {
        if (count.compare_exchange_weak(
                current,
                current + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed))
            return true;
    }
    return false;
}

void workerMain(
    unsigned workerId,
    const Options& options,
    DatasetWriter& writer,
    std::atomic<std::uint32_t>& nextGameId,
    std::atomic<bool>& failed) {

    SearchEngine search;
    if (!search.setHashSizeMB(options.hashMB)) {
        failed.store(true, std::memory_order_relaxed);
        return;
    }

    std::mt19937_64 rng(options.seed + 0x9E3779B97F4A7C15ULL * (workerId + 1ULL));

    Position pos;
    Fen4State fenState;
    std::string error;

    while (!failed.load(std::memory_order_relaxed)
        && writer.written_.load(std::memory_order_relaxed) < options.positions) {

        const std::uint32_t gameId = nextGameId.fetch_add(1, std::memory_order_relaxed);
        if (!resetModern(pos, fenState, error)) {
            std::cerr << "worker " << workerId << ": failed to reset start position: " << error << '\n';
            failed.store(true, std::memory_order_relaxed);
            return;
        }
        search.newGame();

        for (int ply = 0; ply < options.maxPlies; ++ply) {
            if (failed.load(std::memory_order_relaxed)
                || writer.written_.load(std::memory_order_relaxed) >= options.positions)
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
                teacher = searchPosition(search, pos, options.depth);
                if (!teacher.ok)
                    break;
                move = teacher.bestMove;

                const bool samplePly = ply >= options.skipPlies
                    && ((ply - options.skipPlies) % options.sampleEvery == 0);

                if (samplePly && !teacher.mate) {
                    const int clippedCp = std::clamp(teacher.cp, -options.cpClamp, options.cpClamp);

                    Record record;
                    record.target = static_cast<float>(clippedCp) / options.scoreScale;
                    record.teacherCp = teacher.cp;
                    record.gameId = gameId;
                    record.ply = static_cast<std::uint16_t>(std::min(ply, 65535));
                    record.sideToMove = static_cast<std::uint8_t>(pos.sideToMove());
                    if (pos.inCheck())
                        record.flags |= 1u;
                    if (pos.aliveMask() != 0xF)
                        record.flags |= 2u;

                    const Color perspective = pos.sideToMove();
                    const Color partner = static_cast<Color>(static_cast<unsigned>(perspective) ^ 2u);
                    collectFeatures(pos, perspective, pos.kingSquare(perspective), record.own);
                    collectFeatures(pos, perspective, pos.kingSquare(partner), record.partner);

                    if (record.own.size() != record.partner.size()) {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    if (reserveRecordSlot(writer.written_, options.positions)) {
                        if (!writer.write(record)) {
                            failed.store(true, std::memory_order_relaxed);
                            return;
                        }

                        const auto count = writer.written_.load(std::memory_order_relaxed);
                        if (count % 1000 == 0 || count == options.positions) {
                            std::cout << "positions " << count << '/' << options.positions
                                << " games " << (gameId + 1)
                                << " worker " << workerId << '\n';
                        }
                    }
                }
            }

            if (!move)
                break;

            StateInfo st;
            pos.makeMove(move, st);
        }
    }
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoull(std::string(text), &used);
        if (used != text.size())
            return false;
        value = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool parseInt(std::string_view text, int& value) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoi(std::string(text), &used);
        if (used != text.size())
            return false;
        value = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

void printUsage() {
    std::cout
        << "IronPhoenix PhoenixNet dataset generator\n\n"
        << "Usage:\n"
        << "  ironphoenix_dataset [options]\n\n"
        << "Options:\n"
        << "  --output <file>         Output .ipd file (default phoenix_dataset.ipd)\n"
        << "  --positions <n>         Number of samples (default 500000)\n"
        << "  --depth <n>             Teacher search depth (default 8)\n"
        << "  --workers <n>           Parallel self-play workers (default 1)\n"
        << "  --hash <mb>             Hash MB per worker (default 16)\n"
        << "  --random-plies <n>      Random opening plies per game (default 4)\n"
        << "  --skip-plies <n>        Do not save before this ply (default 8)\n"
        << "  --sample-every <n>      Save one of every N plies (default 3)\n"
        << "  --max-plies <n>         Maximum plies per game (default 320)\n"
        << "  --cp-clamp <n>          Clamp teacher CP before scaling (default 4000)\n"
        << "  --score-scale <n>       CP per one network target unit (default 400)\n"
        << "  --seed <n>              RNG seed\n"
        << "  --help                  Show this help\n";
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto needValue = [&]() -> std::string_view {
            if (i + 1 >= argc)
                return {};
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return false;
        }
        if (arg == "--output") {
            const auto value = needValue();
            if (value.empty()) return false;
            options.output = value;
        }
        else if (arg == "--positions") {
            std::uint64_t value = 0;
            if (!parseUnsigned(needValue(), value) || value == 0) return false;
            options.positions = value;
        }
        else if (arg == "--depth") {
            if (!parseInt(needValue(), options.depth) || options.depth < 1 || options.depth > 32) return false;
        }
        else if (arg == "--workers") {
            std::uint64_t value = 0;
            if (!parseUnsigned(needValue(), value) || value == 0 || value > 128) return false;
            options.workers = static_cast<unsigned>(value);
        }
        else if (arg == "--hash") {
            std::uint64_t value = 0;
            if (!parseUnsigned(needValue(), value) || value == 0 || value > 4096) return false;
            options.hashMB = static_cast<std::size_t>(value);
        }
        else if (arg == "--random-plies") {
            if (!parseInt(needValue(), options.randomPlies) || options.randomPlies < 0) return false;
        }
        else if (arg == "--skip-plies") {
            if (!parseInt(needValue(), options.skipPlies) || options.skipPlies < 0) return false;
        }
        else if (arg == "--sample-every") {
            if (!parseInt(needValue(), options.sampleEvery) || options.sampleEvery < 1) return false;
        }
        else if (arg == "--max-plies") {
            if (!parseInt(needValue(), options.maxPlies) || options.maxPlies < 1) return false;
        }
        else if (arg == "--cp-clamp") {
            if (!parseInt(needValue(), options.cpClamp) || options.cpClamp < 1 || options.cpClamp >= 29000) return false;
        }
        else if (arg == "--score-scale") {
            try {
                options.scoreScale = std::stof(std::string(needValue()));
            }
            catch (...) {
                return false;
            }
            if (!(options.scoreScale > 0.0f)) return false;
        }
        else if (arg == "--seed") {
            if (!parseUnsigned(needValue(), options.seed)) return false;
        }
        else {
            std::cerr << "unknown option: " << arg << '\n';
            return false;
        }
    }

    if (options.skipPlies < options.randomPlies)
        options.skipPlies = options.randomPlies;
    return true;
}

} // namespace
} // namespace ironphoenix

int main(int argc, char** argv) {
    using namespace ironphoenix;

    Options options;
    if (!parseOptions(argc, argv, options)) {
        if (argc <= 1)
            printUsage();
        return argc > 1 ? 1 : 0;
    }

    DatasetWriter writer(options);
    if (!writer.good()) {
        std::cerr << "unable to open output file: " << options.output << '\n';
        return 1;
    }

    std::cout << "PhoenixNet dataset generation\n"
        << "output       " << options.output << '\n'
        << "positions    " << options.positions << '\n'
        << "depth        " << options.depth << '\n'
        << "workers      " << options.workers << '\n'
        << "hash/worker  " << options.hashMB << " MB\n"
        << "random plies " << options.randomPlies << '\n'
        << "skip plies   " << options.skipPlies << '\n'
        << "sample every " << options.sampleEvery << '\n'
        << "seed         " << options.seed << "\n\n";

    std::atomic<std::uint32_t> nextGameId{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    workers.reserve(options.workers);

    for (unsigned worker = 0; worker < options.workers; ++worker) {
        workers.emplace_back(workerMain,
            worker,
            std::cref(options),
            std::ref(writer),
            std::ref(nextGameId),
            std::ref(failed));
    }

    for (auto& worker : workers)
        worker.join();

    writer.finalize();

    if (failed.load(std::memory_order_relaxed)) {
        std::cerr << "dataset generation failed\n";
        return 1;
    }

    std::cout << "done: wrote " << writer.written_.load(std::memory_order_relaxed)
        << " positions to " << options.output << '\n';
    return 0;
}
