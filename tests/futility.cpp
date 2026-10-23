#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"
#include "ironphoenix/movegen.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace ironphoenix;

namespace ironphoenix {
struct FutilityPruningTestAccess {
    static std::uint64_t nodes(SearchEngine& engine) { return engine.nodes_; }
    static bool evaluatedAt(SearchEngine& engine, int ply) {
        return engine.searchStack_[ply].staticEvalValid;
    }
    static int search(SearchEngine& engine, Position& pos, int depth,
                      int alpha, int beta, int ply = 1, bool pv = false,
                      bool allowNull = true) {
        engine.stopRequested_.store(false, std::memory_order_relaxed);
        engine.limits_ = SearchLimits{};
        engine.limits_.nodes = 500'000;
        engine.tt_.clear();
        engine.history_.clear();
        engine.searchStack_.fill(SearchStackEntry{});
        engine.nodes_ = 0;
        engine.nextTimeCheckNode_ = 0;
        engine.pvLength_.fill(0);
        return engine.negamax(pos, depth, alpha, beta, ply, pv, allowNull);
    }
};
}

namespace {
int fails = 0;
void expect(bool ok, const char* description) {
    if (!ok) { std::cerr << "FAIL: " << description << '\n'; ++fails; }
}
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }
Position quietPosition() {
    Position p;
    p.clear();
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
    p.placePiece(sq('f', 5), makePiece(RED, KNIGHT));
    p.placePiece(sq('i', 10), makePiece(YELLOW, KNIGHT));
    p.placePiece(sq('d', 6), makePiece(BLUE, KNIGHT));
    p.placePiece(sq('k', 9), makePiece(GREEN, KNIGHT));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}

std::uint64_t count(Position pos, bool pruning, bool pv = false,
                    int depth = 2, int alpha = 8000, bool allowNull = true,
                    int ply = 1, bool* parentStaticEvaluated = nullptr) {
    Tuning::search().futilityMaxDepth = pruning ? 2 : 0;
    auto engine = std::make_unique<SearchEngine>();
    engine->setHashSizeMB(1);
    FutilityPruningTestAccess::search(*engine, pos, depth, alpha, alpha + 1,
                                     ply, pv, allowNull);
    if (parentStaticEvaluated)
        *parentStaticEvaluated =
            FutilityPruningTestAccess::evaluatedAt(*engine, ply);
    return FutilityPruningTestAccess::nodes(*engine);
}

void testOptions() {
    Tuning::resetSearch();
    const Tuning::SearchParameters defaults{};
    expect(defaults.futilityMaxDepth == 2, "default depth 2");
    expect(defaults.futilityBaseMargin == 100, "default base margin 100");
    expect(defaults.futilityMarginPerDepth == 150, "default margin per ply 150");

    struct Parameter {
        const char* name;
        int Tuning::SearchParameters::*member;
        int good;
        int invalid;
    };
    const Parameter parameters[] = {
        {"SearchFutilityMaxDepth", &Tuning::SearchParameters::futilityMaxDepth, 4, 17},
        {"SearchFutilityBaseMargin", &Tuning::SearchParameters::futilityBaseMargin, 220, -1},
        {"SearchFutilityMarginPerDepth", &Tuning::SearchParameters::futilityMarginPerDepth, 250, 5001},
    };
    for (const auto& p : parameters) {
        expect(Tuning::setSearchParameter(p.name, p.good), "UCI setter accepts in-range");
        expect(Tuning::search().*(p.member) == p.good, "UCI setter updates value");
        expect(!Tuning::setSearchParameter(p.name, p.invalid), "UCI setter rejects out-of-range");
        expect(Tuning::search().*(p.member) == p.good, "invalid value leaves setting unchanged");
    }
    std::ostringstream uci;
    Tuning::printUciOptions(uci);
    expect(uci.str().find("option name SearchFutilityMaxDepth type spin default 2 min 0 max 16")
        != std::string::npos, "depth option advertised");
    expect(uci.str().find("option name SearchFutilityBaseMargin type spin default 100 min 0 max 5000")
        != std::string::npos, "base margin option advertised");
    expect(uci.str().find("option name SearchFutilityMarginPerDepth type spin default 150 min 0 max 5000")
        != std::string::npos, "per-depth option advertised");

    std::ostringstream state;
    Tuning::printCurrent(state);
    expect(state.str().find("SearchFutilityMaxDepth=4") != std::string::npos,
           "current parameter diagnostics include move-loop futility");

    Tuning::resetSearch();
    for (const auto& p : parameters)
        expect(Tuning::search().*(p.member) == defaults.*(p.member),
               "reset restores tuning default");
}

void testSearch() {
    Tuning::resetSearch();
    // Disable unrelated pruning so node differences isolate futility.
    auto& tune = Tuning::search();
    tune.rfpMaxDepth = 0;
    tune.nullMoveMinDepth = 32;
    tune.hmpMaxDepth = 0;
    tune.lmpHistoryThreshold = 0;
    tune.seePruneMaxDepth = 0;

    const Position pos = quietPosition();
    const Key before = pos.key();
    expect(!pos.inCheck(RED) && !pos.inCheck(YELLOW), "fixture not checked");

    const auto unpruned = count(pos, false);
    const auto pruned = count(pos, true);
    expect(unpruned > 1, "baseline searches more than one node");
    expect(pruned < unpruned, "move-loop futility actually avoids child searches");
    expect(pos.key() == before && pos.verify(), "caller position stays unchanged");

    // Futility requires non-PV internal nodes, not the first/only legal move.
    const auto pvBase = count(pos, false, true);
    const auto pvPruned = count(pos, true, true);
    expect(pvBase == pvPruned, "PV search excludes futility");
    const auto rootBase = count(pos, false, false, 1, 8000, true, 0);
    const auto rootPruned = count(pos, true, false, 1, 8000, true, 0);
    expect(rootBase == rootPruned, "root excluded from futility");

    // Child nodes at depth 2 may still prune below a depth-3 parent.
    // Verify the parent itself does not compute an FP static estimate.
    bool parentEvaluated = true;
    (void)count(pos, true, false, 3, 8000, true, 1, &parentEvaluated);
    expect(!parentEvaluated, "depth-3 node skips futility static estimate");

    const auto nullBase = count(pos, false, false, 2, 8000, false);
    const auto nullPruned = count(pos, true, false, 2, 8000, false);
    expect(nullBase == nullPruned, "null subtree excluded");

    auto partnerChecked = quietPosition();
    // Rook attacks YELLOW across rank 14 but cannot attack RED on g1.
    partnerChecked.placePiece(sq('e', 14), makePiece(GREEN, ROOK));
    partnerChecked.finalizeSetup();
    expect(!partnerChecked.inCheck(RED) && partnerChecked.inCheck(YELLOW),
           "partner check setup valid");
    const auto partnerBase = count(partnerChecked, false);
    const auto partnerPruned = count(partnerChecked, true);
    expect(partnerBase == partnerPruned, "partner check disables futility");

    auto checked = quietPosition();
    checked.placePiece(sq('g', 4), makePiece(BLUE, ROOK));
    checked.finalizeSetup();
    expect(checked.inCheck(RED), "own check setup valid");
    const auto checkBase = count(checked, false);
    const auto checkPruned = count(checked, true);
    expect(checkBase == checkPruned, "own check disables futility");

    Tuning::resetSearch();
}
}

int main() {
    Geometry::init();
    testOptions();
    testSearch();
    if (!fails)
        std::cout << "Move-loop futility pruning tests: PASS\n";
    return fails ? 1 : 0;
}
