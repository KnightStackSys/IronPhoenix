#pragma once

#include "history.hpp"
#include "tt.hpp"

namespace ironphoenix {

// Per-ply metadata for search and qsearch. The parent writes history before
// recursing, so beginNode() deliberately preserves that incoming context.
struct SearchStackEntry final {
    HistoryContext history{};
    Move currentMove{};
    Move bestMove{};
    Move ttMove{};
    Color sideToMove = RED;
    TTBound ttBound = TTBound::None;
    int depth = 0;
    int ttDepth = -1;
    int moveCount = 0;
    int staticEval = 0;
    int improvingRate = 0; // Centipawn delta vs a recent evaluated teammate node.
    bool pvNode = false;
    bool ttPv = false;
    bool ttHit = false;
    bool inCheck = false;
    bool staticEvalValid = false;
    bool improving = false;

    void beginNode(Color mover, int searchDepth, bool isPv) noexcept {
        currentMove = Move{};
        bestMove = Move{};
        ttMove = Move{};
        sideToMove = mover;
        ttBound = TTBound::None;
        depth = searchDepth;
        ttDepth = -1;
        moveCount = 0;
        staticEval = 0;
        improvingRate = 0;
        pvNode = isPv;
        ttPv = isPv;
        ttHit = false;
        inCheck = false;
        staticEvalValid = false;
        improving = false;
    }

    void recordTT(const TTProbe& probe, bool hit) noexcept {
        ttHit = hit;
        ttMove = hit ? probe.move : Move{};
        ttDepth = hit ? probe.depth : -1;
        ttBound = hit ? probe.bound : TTBound::None;
        ttPv = pvNode || (hit && probe.pvNode);
    }

    void recordStaticEval(int score, const SearchStackEntry* earlier = nullptr) noexcept {
        staticEval = score;
        staticEvalValid = true;
        improvingRate = 0;
        improving = false;
        // Scores are team-relative and cannot be compared across enemy turns.
        if (earlier && earlier->staticEvalValid
            && teamOf(earlier->sideToMove) == teamOf(sideToMove)) {
            improvingRate = score - earlier->staticEval;
            improving = improvingRate > 0;
        }
    }
};

} // namespace ironphoenix
