#include "ironphoenix/search.hpp"
#include "ironphoenix/eval.hpp"
#include "ironphoenix/tuning.hpp"
#include "ironphoenix/movegen.hpp"
#include <iostream>
#include <memory>
#include <limits>

using namespace ironphoenix;

namespace ironphoenix {
struct ReverseFutilityTestAccess {
    static bool safe(SearchEngine& engine, Position& pos) {
        return engine.reverseFutilitySafe(pos);
    }
    static int search(SearchEngine& engine, Position& pos, int depth, int beta,
                      int ply = 1, bool pv = false, bool allowNull = true,
                      int ancestorEval = std::numeric_limits<int>::max()) {
        engine.tt_.clear();
        engine.searchStack_.fill(SearchStackEntry{});
        if (ply >= 2 && ancestorEval != std::numeric_limits<int>::max()) {
            auto& ancestor = engine.searchStack_[ply - 2];
            ancestor.beginNode(pos.sideToMove(), depth + 2, false);
            ancestor.recordStaticEval(ancestorEval);
        }
        engine.stopRequested_.store(false);
        engine.nodes_ = 0;
        engine.limits_.nodes = 2000;
        return engine.negamax(pos, depth, beta - 1, beta, ply, pv, allowNull);
    }
    static std::uint64_t nodes(const SearchEngine& engine) { return engine.nodes_; }
    static const SearchStackEntry& stack(const SearchEngine& engine, int ply = 1) { return engine.searchStack_[ply]; }
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

    // Verify the search options, bounds, and default-preserving reset.
    struct PruningOption {
        const char* name;
        int Tuning::SearchParameters::*member;
        int sampleValue;
        int invalidValue;
    };
    const PruningOption options[] = {
        { "SearchLMPHistoryThreshold", &Tuning::SearchParameters::lmpHistoryThreshold, 2010, -1 },
        { "SearchHMPMaxDepth", &Tuning::SearchParameters::hmpMaxDepth, 5, -1 },
        { "SearchHMPMarginPerDepth", &Tuning::SearchParameters::hmpMarginPerDepth, 1510, -1 },
        { "SearchNullMoveMinDepth", &Tuning::SearchParameters::nullMoveMinDepth, 3, 0 },
        { "SearchNullMoveDivisor", &Tuning::SearchParameters::nullMoveDivisor, 200, 0 },
        { "SearchRFPMaxDepth", &Tuning::SearchParameters::rfpMaxDepth, 7, -1 },
        { "SearchRFPMarginPerDepth", &Tuning::SearchParameters::rfpMarginPerDepth, 140, -1 },
        { "SearchRFPMinMargin", &Tuning::SearchParameters::rfpMinMargin, 99, -1 },
        { "SearchRFPMaxImprovingDiscount", &Tuning::SearchParameters::rfpMaxImprovingDiscount, 88, -1 },
    };
    // Defaults evolve as tuning results are adopted. Test against the actual
    // default constructor rather than stale historical SPSA constants.
    const Tuning::SearchParameters defaults{};
    Tuning::resetSearch();
    for (const auto& option : options) {
        expect(Tuning::search().*(option.member) == defaults.*(option.member), "pruning default retained");
        expect(Tuning::setSearchParameter(option.name, option.sampleValue), "valid pruning option accepted");
        expect(Tuning::search().*(option.member) == option.sampleValue, "pruning option takes effect");
        expect(!Tuning::setSearchParameter(option.name, option.invalidValue), "invalid pruning value rejected");
        expect(Tuning::search().*(option.member) == option.sampleValue, "invalid input leaves setting unchanged");
    }
    Tuning::resetSearch();
    for (const auto& option : options)
        expect(Tuning::search().*(option.member) == defaults.*(option.member), "pruning reset restores default");

    {
        Position pos = quiet(RED);
        const int eval = Eval::evaluate(pos);
        const int defaultMargin = defaults.rfpMarginPerDepth;
        const int beta = eval - defaultMargin;
        // The tuned default may change; probe its exact cutoff boundary.
        expect(Tuning::setSearchParameter("SearchRFPMarginPerDepth", defaultMargin + 50),
               "RFP runtime margin configurable");
        Access::search(*engine, pos, 1, beta);
        expect(Access::nodes(*engine) > 1, "larger runtime RFP margin prevents shallow cutoff");
        Tuning::resetSearch();
        expect(Access::search(*engine, pos, 1, beta) == beta, "default RFP cutoff restored");
        expect(Access::nodes(*engine) == 1, "default RFP searches no children");
    }

    // The following boundary tests deliberately exercise the original 130/90/80
    // RFP policy, independently of the tuned production defaults.
    expect(Tuning::setSearchParameter("SearchRFPMarginPerDepth", 130),
           "set RFP regression margin");
    expect(Tuning::setSearchParameter("SearchRFPMinMargin", 90),
           "set RFP regression minimum");
    expect(Tuning::setSearchParameter("SearchRFPMaxImprovingDiscount", 80),
           "set RFP regression improving discount");

    for (unsigned c = 0; c < COLOR_NB; ++c) {
        Position pos = quiet(static_cast<Color>(c));
        const Key key = pos.key();
        expect(Access::safe(*engine, pos), "quiet position eligible for every seat");
        expect(pos.key() == key && pos.verify(), "safety probe restores board and hash");
        const int eval = Eval::evaluate(pos);
        for (int depth = 1; depth <= 6; ++depth) {
            const int beta = eval - 130 * depth;
            expect(Access::search(*engine, pos, depth, beta) == beta, "aggressive margin boundary returns adjusted score");
            expect(Access::nodes(*engine) == 1, "RFP cuts before child search");
            expect(Access::stack(*engine).staticEvalValid && Access::stack(*engine).staticEval == eval,
                   "raw team evaluation recorded on search stack");
        }
        Access::search(*engine, pos, 1, eval - 129);
        expect(Access::nodes(*engine) > 1, "insufficient margin searches children");
        Access::search(*engine, pos, 1, eval - 130, 1, true);
        expect(Access::nodes(*engine) > 1, "PV node excluded");
        Access::search(*engine, pos, 1, eval - 130, 0);
        expect(Access::nodes(*engine) > 1, "root excluded");
        Access::search(*engine, pos, 1, eval - 130, 1, false, false);
        expect(Access::nodes(*engine) > 1, "null subtree excluded");
        Access::search(*engine, pos, 1, -29000);
        expect(Access::nodes(*engine) > 1, "mate window excluded");
        Access::search(*engine, pos, 7, eval - 1000);
        expect(Access::nodes(*engine) > 1, "depth beyond six excluded");
        // At ply 3, +180 cp against a same-team ancestor discounts
        // depth-3's 390 cp base margin by 60 cp (to 330 cp).
        const int improvingBeta = eval - 330;
        expect(Access::search(*engine, pos, 3, improvingBeta, 3, false, true, eval - 180)
               == improvingBeta, "improving team can prune at reduced margin");
        expect(Access::nodes(*engine) == 1, "improving team RFP searches no children");
        expect(Access::stack(*engine, 3).improving
               && Access::stack(*engine, 3).improvingRate == 180,
               "improving rate populated from same-team ancestor");

        // When the trend is flat or negative, preserve the base margin.
        Access::search(*engine, pos, 3, improvingBeta, 3, false, true, eval + 100);
        expect(Access::nodes(*engine) > 1, "worsening trend receives no discount");
        expect(!Access::stack(*engine, 3).improving, "worsening trend is not improving");

        // Cap the discount at 80 cp, leaving a nonzero margin.
        const int cappedBeta = eval - 310;
        expect(Access::search(*engine, pos, 3, cappedBeta, 3, false, true, eval - 1000)
               == cappedBeta, "improving discount is bounded");
        expect(Access::nodes(*engine) == 1, "bounded improvement still enables RFP");

        expect(pos.key() == key && pos.verify(), "search restores board and hash");
    }
    {
        auto pos = quiet(RED);
        pos.placePiece(sq('d', 14), makePiece(BLUE, ROOK));
        pos.finalizeSetup();
        expect(!pos.inCheck() && pos.inCheck(YELLOW), "partner-only check fixture");
        expect(!Access::safe(*engine, pos), "partner check excluded");
        const int eval = Eval::evaluate(pos);
        Access::search(*engine, pos, 1, eval - 130);
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
        Access::search(*engine, pos, 1, Eval::evaluate(pos) - 130);
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
    Tuning::resetSearch();
    if (!failures) std::cout << "Reverse futility regression tests: PASS\n";
    return failures ? 1 : 0;
}
