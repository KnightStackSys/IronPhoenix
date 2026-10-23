#include "ironphoenix/search.hpp"
#include "ironphoenix/movegen.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

using namespace ironphoenix;

namespace ironphoenix {
struct MateStopTestAccess {
    static bool shouldStop(SearchEngine& engine, int score, Move move,
                           bool infinite = false, bool pondering = false) {
        engine.limits_ = SearchLimits{};
        engine.limits_.infinite = infinite;
        engine.pondering_ = pondering;
        return engine.shouldStopOnWinningMate(score, move);
    }
};
} // namespace ironphoenix

namespace {
int failures = 0;

void expect(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}

Position immediateTeamMate() {
    Position pos;
    pos.clear();
    pos.placePiece(sq('g', 1), makePiece(RED, KING));
    pos.placePiece(sq('a', 7), makePiece(BLUE, KING));
    pos.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    pos.placePiece(sq('n', 7), makePiece(GREEN, KING));
    // From d7, RED can capture BLUE's king on a7 immediately.
    pos.placePiece(sq('d', 7), makePiece(RED, ROOK));
    pos.setSideToMove(RED);
    pos.finalizeSetup();
    return pos;
}

bool waitForCompletion(SearchEngine& engine) {
    for (int i = 0; i < 400; ++i) {
        if (!engine.searching())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !engine.searching();
}

void testRootMate(bool multiPV) {
    Position pos = immediateTeamMate();
    const Key original = pos.key();
    MoveList legal;
    generateLegalMoves(pos, legal);
    bool terminalAvailable = false;
    for (Move move : legal)
        if (isTerminalKingCapture(pos, move))
            terminalAvailable = true;
    expect(terminalAvailable, "fixture has legal immediate enemy king capture");

    auto engine = std::make_unique<SearchEngine>();
    SearchLimits limits;
    limits.depth = 10; // Mate in one should terminate at depth one, not ten.
    std::ostringstream output;
    if (multiPV)
        engine->startMultiPV(pos, limits, 2, output);
    else
        engine->start(pos, limits, output);

    const bool completed = waitForCompletion(*engine);
    engine->stopAndWait();
    const std::string text = output.str();
    expect(completed, multiPV ? "MultiPV returned promptly on mate" : "root search returned promptly on mate");
    expect(text.find("score mate 1") != std::string::npos, "engine reports winning mate in one");
    expect(text.find("bestmove d7a7") != std::string::npos, "engine returns king-capturing move");
    expect(text.find("info string iir reductions ") != std::string::npos,
           "search reports actual applied IIR reductions");
    expect(text.find("info depth 2 ") == std::string::npos, "no further deepening after winning mate");
    if (multiPV) {
        expect(text.find("multipv 1 ") != std::string::npos, "MultiPV top line emitted");
        expect(text.find("multipv 2 ") != std::string::npos, "MultiPV second line emitted before stop");
    }
    expect(pos.key() == original && pos.verify(), "search did not modify caller's position");
}
} // namespace

int main() {
    Geometry::init();
    auto engine = std::make_unique<SearchEngine>();
    const Move move(sq('d', 7), sq('a', 7));

    expect(MateStopTestAccess::shouldStop(*engine, 29999, move), "winning mate causes stop");
    expect(MateStopTestAccess::shouldStop(*engine, 29000, move), "valid long mate score causes stop");
    expect(!MateStopTestAccess::shouldStop(*engine, -29999, move), "losing mate keeps searching");
    expect(!MateStopTestAccess::shouldStop(*engine, 1500, move), "centipawn score keeps searching");
    expect(!MateStopTestAccess::shouldStop(*engine, 30001, move), "invalid over-mate score ignored");
    expect(!MateStopTestAccess::shouldStop(*engine, 29999, Move{}), "missing root move cannot stop");
    expect(!MateStopTestAccess::shouldStop(*engine, 29999, move, true), "go infinite waits for stop");
    expect(!MateStopTestAccess::shouldStop(*engine, 29999, move, false, true), "ponder waits for ponderhit");

    testRootMate(false);
    testRootMate(true);

    if (!failures)
        std::cout << "Mate-stop regression tests: PASS\n";
    return failures ? 1 : 0;
}
