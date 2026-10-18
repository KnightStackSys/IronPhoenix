#include "ironphoenix/search.hpp"
#include "ironphoenix/eval.hpp"
#include "ironphoenix/movegen.hpp"
#include <iostream>
#include <memory>

using namespace ironphoenix;

namespace ironphoenix {
struct ReverseFutilityTestAccess {
    static bool safe(SearchEngine& engine, Position& pos) {
        return engine.reverseFutilitySafe(pos);
    }
    static int search(SearchEngine& engine, Position& pos, int depth, int beta,
                      int ply = 1, bool pv = false, bool allowNull = true) {
        engine.tt_.clear();
        engine.searchStack_.fill(SearchStackEntry{});
        engine.stopRequested_.store(false);
        engine.nodes_ = 0;
        engine.limits_.nodes = 2000;
        return engine.negamax(pos, depth, beta - 1, beta, ply, pv, allowNull);
    }
    static std::uint64_t nodes(const SearchEngine& engine) { return engine.nodes_; }
    static const SearchStackEntry& stack(const SearchEngine& engine) { return engine.searchStack_[1]; }
};
}

namespace {
int failures = 0;
void expect(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }
Position quiet(Color side) {
    Position pos;
    pos.clear();
    pos.placePiece(sq('g', 1), makePiece(RED, KING));
    pos.placePiece(sq('a', 7), makePiece(BLUE, KING));
    pos.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    pos.placePiece(sq('n', 7), makePiece(GREEN, KING));
    pos.placePiece(sq('e', 5), makePiece(side, KNIGHT));
    pos.setSideToMove(side);
    pos.finalizeSetup();
    return pos;
}
}

int main() {
    Geometry::init();
    auto engine = std::make_unique<SearchEngine>();
    using Access = ReverseFutilityTestAccess;
    for (unsigned c = 0; c < COLOR_NB; ++c) {
        Position pos = quiet(static_cast<Color>(c));
        const Key key = pos.key();
        expect(Access::safe(*engine, pos), "quiet position eligible for every seat");
        expect(pos.key() == key && pos.verify(), "safety probe restores board and hash");
        const int eval = Eval::evaluate(pos);
        for (int depth = 1; depth <= 4; ++depth) {
            const int beta = eval - 200 * depth;
            expect(Access::search(*engine, pos, depth, beta) == beta, "margin boundary returns adjusted score");
            expect(Access::nodes(*engine) == 1, "RFP cuts before child search");
            expect(Access::stack(*engine).staticEvalValid && Access::stack(*engine).staticEval == eval,
                   "raw team evaluation recorded on search stack");
        }
        Access::search(*engine, pos, 1, eval - 199);
        expect(Access::nodes(*engine) > 1, "insufficient margin searches children");
        Access::search(*engine, pos, 1, eval - 200, 1, true);
        expect(Access::nodes(*engine) > 1, "PV node excluded");
        Access::search(*engine, pos, 1, eval - 200, 0);
        expect(Access::nodes(*engine) > 1, "root excluded");
        Access::search(*engine, pos, 1, eval - 200, 1, false, false);
        expect(Access::nodes(*engine) > 1, "null subtree excluded");
        Access::search(*engine, pos, 1, -29000);
        expect(Access::nodes(*engine) > 1, "mate window excluded");
        Access::search(*engine, pos, 5, eval - 1000);
        expect(Access::nodes(*engine) > 1, "depth beyond four excluded");
        expect(pos.key() == key && pos.verify(), "search restores board and hash");
    }
    {
        auto pos = quiet(RED);
        pos.placePiece(sq('d', 14), makePiece(BLUE, ROOK));
        pos.finalizeSetup();
        expect(!pos.inCheck() && pos.inCheck(YELLOW), "partner-only check fixture");
        expect(!Access::safe(*engine, pos), "partner check excluded");
        const int eval = Eval::evaluate(pos);
        Access::search(*engine, pos, 1, eval - 200);
        expect(Access::nodes(*engine) > 1, "partner check cannot RFP");
    }
    {
        auto pos = quiet(RED);
        pos.placePiece(sq('d', 7), makePiece(RED, ROOK));
        pos.finalizeSetup();
        expect(!Access::safe(*engine, pos), "terminal king capture excluded");
        expect(Access::search(*engine, pos, 1, -20000) == 29998,
               "immediate enemy king capture keeps mate distance");
    }
    const Color colors[] = { RED, BLUE, YELLOW, GREEN };
    const Square promotionSources[] = { sq('g', 10), sq('j', 7), sq('g', 5), sq('e', 7) };
    for (unsigned c = 0; c < COLOR_NB; ++c) {
        auto pos = quiet(RED);
        pos.placePiece(promotionSources[c], makePiece(colors[c], PAWN));
        pos.finalizeSetup();
        expect(!Access::safe(*engine, pos), "promotion threat from any seat excluded");
    }
    {
        auto pos = quiet(RED);
        pos.erasePiece(sq('e', 5));
        pos.placePiece(sq('g', 2), makePiece(RED, ROOK));
        for (auto square : { sq('f', 1), sq('h', 1), sq('f', 2), sq('h', 2), sq('g', 3) })
            pos.placePiece(square, makePiece(YELLOW, PAWN));
        pos.finalizeSetup();
        MoveList legal;
        generateLegalMoves(pos, legal);
        expect(!pos.inCheck() && legal.size == 0, "stalemate fixture");
        expect(!Access::safe(*engine, pos), "stalemate excluded");
        expect(Access::search(*engine, pos, 1, -20000) == 0, "stalemate retains draw score");
        pos.erasePiece(sq('h', 1));
        pos.finalizeSetup();
        generateLegalMoves(pos, legal);
        expect(legal.size == 1, "forced-move fixture");
        expect(!Access::safe(*engine, pos), "only legal move excluded");
    }
    {
        auto pos = quiet(RED);
        pos.placePiece(sq('d', 1), makePiece(BLUE, ROOK));
        pos.finalizeSetup();
        expect(pos.inCheck(), "own check fixture");
        expect(!Access::safe(*engine, pos), "own check excluded");
        Access::search(*engine, pos, 1, Eval::evaluate(pos) - 200);
        expect(Access::nodes(*engine) > 1, "own check cannot RFP");
    }
    {
        auto pos = quiet(RED);
        pos.setAliveMask(7);
        expect(!Access::safe(*engine, pos), "eliminated player excluded");
        pos.setAliveMask(15);
        pos.erasePiece(sq('g', 14));
        pos.finalizeSetup();
        expect(!Access::safe(*engine, pos), "missing king excluded");
    }
    if (!failures) std::cout << "Reverse futility regression tests: PASS\n";
    return failures ? 1 : 0;
}
