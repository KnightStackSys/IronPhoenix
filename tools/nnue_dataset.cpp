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

class DatasetWriter final {
public:
    explicit DatasetWriter(const Options& options)
        : out_(options.output, std::ios::binary | std::ios::trunc) {
        if (!out_)
            return;

        out_.write(DATA_MAGIC.data(), static_cast<std::streamsize>(DATA_MAGIC.size()));
        writeValue(out_, DATA_VERSION);
        writeValue(out_, static_cast<std::uint32_t>(NNUE::FEATURE_COUNT));
        writeValue(out_, MAX_FEATURES_PER_STREAM);
        writeValue(out_, static_cast<std::uint32_t>(options.depth));
        writeValue(out_, options.scoreScale);
        writeValue(out_, options.seed);

        countPosition_ = out_.tellp();
        const std::uint64_t zero = 0;
        writeValue(out_, zero);
    }

    ~DatasetWriter() {
        finalize();
    }

    [[nodiscard]] bool good() const noexcept {
        return static_cast<bool>(out_);
    }

    [[nodiscard]] std::uint64_t count() const noexcept {
        return reserved_.load(std::memory_order_relaxed);
    }

    bool tryWrite(const Record& record, std::uint64_t limit) {
        if (record.own.size() > MAX_FEATURES_PER_STREAM
            || record.partner.size() > MAX_FEATURES_PER_STREAM)
            return false;

        std::uint64_t current = reserved_.load(std::memory_order_relaxed);
        while (current < limit) {
            if (reserved_.compare_exchange_weak(
                    current,
                    current + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
                break;
        }
        if (current >= limit)
            return true;

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

        if (!record.own.empty()) {
            out_.write(
                reinterpret_cast<const char*>(record.own.data()),
                static_cast<std::streamsize>(record.own.size() * sizeof(std::uint16_t)));
        }
        if (!record.partner.empty()) {
            out_.write(
                reinterpret_cast<const char*>(record.partner.data()),
                static_cast<std::streamsize>(record.partner.size() * sizeof(std::uint16_t)));
        }

        return static_cast<bool>(out_);
    }

    void finalize() {
        std::lock_guard lock(mutex_);
        if (finalized_ || !out_)
            return;

        const auto end = out_.tellp();
        out_.seekp(countPosition_);
        const std::uint64_t recordCount = count();
        writeValue(out_, recordCount);
        out_.seekp(end);
        out_.flush();
        finalized_ = true;
    }

private:
    std::ofstream out_;
    std::streampos countPosition_{};
    std::atomic<std::uint64_t> reserved_{0};
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

TeacherResult searchPosition(SearchEngine& search, Position& pos, int depth) {
    SearchLimits limits;
    limits.depth = depth;

    std::ostringstream output;
    search.start(pos, limits, output);
    while (search.searching())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    search.stopAndWait();

    enum class ScoreKind { None, Cp, Mate };
    ScoreKind scoreKind = ScoreKind::None;
    int cp = 0;
    std::string bestMoveText;

    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("info ", 0) == 0) {
            constexpr std::string_view CP_TOKEN = " score cp ";
            constexpr std::string_view MATE_TOKEN = " score mate ";
            const auto cpPos = line.find(CP_TOKEN);
            const auto matePos = line.find(MATE_TOKEN);

            if (cpPos != std::string::npos) {
                try {
                    cp = std::stoi(line.substr(cpPos + CP_TOKEN.size()));
                    scoreKind = ScoreKind::Cp;
                }
                catch (...) {
                    scoreKind = ScoreKind::None;
                }
            }
            else if (matePos != std::string::npos) {
                scoreKind = ScoreKind::Mate;
            }
        }
        else if (line.rfind("bestmove ", 0) == 0) {
            bestMoveText = line.substr(9);
        }
    }

    TeacherResult result;
    if (bestMoveText.empty() || bestMoveText == "0000" || scoreKind == ScoreKind::None)
        return result;

    std::string error;
    if (!parseLegalMove(pos, bestMoveText, result.bestMove, error))
        return result;

    result.ok = true;
    result.mate = scoreKind == ScoreKind::Mate;
    result.cp = cp;
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
    std::atomic<std::uint32_t>& nextGameId,
    std::atomic<bool>& failed) {

    SearchEngine search;
    if (!search.setHashSizeMB(options.hashMB)) {
        std::cerr << "worker " << workerId << ": unable to allocate hash\n";
        failed.store(true, std::memory_order_relaxed);
        return;
    }

    std::mt19937_64 rng(
        options.seed + 0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(workerId) + 1ULL));

    Position pos;
    Fen4State fenState;
    std::string error;

    while (!failed.load(std::memory_order_relaxed) && writer.count() < options.positions) {
        const std::uint32_t gameId = nextGameId.fetch_add(1, std::memory_order_relaxed);

        if (!resetModern(pos, fenState, error)) {
            std::cerr << "worker " << workerId << ": start position failed: " << error << '\n';
            failed.store(true, std::memory_order_relaxed);
            return;
        }
        search.newGame();

        for (int ply = 0; ply < options.maxPlies; ++ply) {
            if (failed.load(std::memory_order_relaxed) || writer.count() >= options.positions)
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
                        std::cerr << "worker " << workerId << ": feature stream size mismatch\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    if (!writer.tryWrite(record, options.positions)) {
                        std::cerr << "worker " << workerId << ": dataset write failed\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    const auto count = writer.count();
                    if (count % 1000 == 0 || count == options.positions) {
                        std::cout << "positions " << count << '/' << options.positions
                            << " games " << (gameId + 1)
                            << " worker " << workerId << '\n';
                    }
                }
            }

            if (!move)
                break;

            // Chess.com-style Teams ends immediately when an enemy king is
            // captured. Never generate post-terminal positions for training.
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

void printUsage() {
    std::cout
        << "IronPhoenix PhoenixNet dataset generator\n\n"
        << "Usage: ironphoenix_dataset [options]\n\n"
        << "  --output <file>       Output .ipd file (default phoenix_dataset.ipd)\n"
        << "  --positions <n>       Number of samples (default 500000)\n"
        << "  --depth <n>           Teacher search depth (default 8)\n"
        << "  --workers <n>         Parallel self-play workers (default 1)\n"
        << "  --hash <mb>           Hash MB per worker (default 16)\n"
        << "  --random-plies <n>    Random opening plies (default 4)\n"
        << "  --skip-plies <n>      Earliest saved ply (default 8)\n"
        << "  --sample-every <n>    Save one of every N plies (default 3)\n"
        << "  --max-plies <n>       Maximum plies per game (default 320)\n"
        << "  --cp-clamp <n>        Teacher CP clamp (default 4000)\n"
        << "  --score-scale <n>     CP per target unit (default 400)\n"
        << "  --seed <n>            RNG seed\n"
        << "  --help                Show this help\n";
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
            if (!parseSigned(value(), options.depth) || options.depth < 1 || options.depth > 32) return false;
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
            if (!parseSigned(value(), options.cpClamp) || options.cpClamp < 1 || options.cpClamp >= 29000) return false;
        }
        else if (arg == "--score-scale") {
            try {
                options.scoreScale = std::stof(std::string(value()));
            }
            catch (...) {
                return false;
            }
            if (!(options.scoreScale > 0.0f)) return false;
        }
        else if (arg == "--seed") {
            if (!parseUnsigned(value(), options.seed)) return false;
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
    bool showedHelp = false;
    if (!parseOptions(argc, argv, options, showedHelp)) {
        if (!showedHelp)
            printUsage();
        return showedHelp ? 0 : 1;
    }

    DatasetWriter writer(options);
    if (!writer.good()) {
        std::cerr << "unable to open output file: " << options.output << '\n';
        return 1;
    }

    std::cout
        << "PhoenixNet dataset generation\n"
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

    for (unsigned workerId = 0; workerId < options.workers; ++workerId) {
        workers.emplace_back(
            workerMain,
            workerId,
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

    std::cout << "done: wrote " << writer.count()
        << " positions to " << options.output << '\n';
    return 0;
}
