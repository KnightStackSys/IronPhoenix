#pragma once

#include <iosfwd>
#include <string_view>

namespace ironphoenix {

    class SearchEngine;

    namespace Tuning {

        struct SearchParameters final {
            int aspirationStartDepth = 4;
            int aspirationInitialDelta = 50;
            int seePruneMaxDepth = 4;
            int seePruneMarginPerDepth = 80;
            int historyBonusScale = 64;
            int historyBonusMax = 2048;
            int counterMoveBonus = 32'000;
            int checkMoveBonus = 10'000'000;
            int lmrMinDepth = 3;
            int lmrFullMoves = 4;
            int lmrBaseX100 = 78;
            int lmrDivisorX100 = 286;
            int lmpHistoryThreshold = 1918;
            int hmpMaxDepth = 4;
            int hmpMarginPerDepth = 1519;
            int nullMoveMinDepth = 2;
            int nullMoveDivisor = 192;
            int rfpMaxDepth = 6;
            int rfpMarginPerDepth = 141;
            int rfpMinMargin = 88;
            int rfpMaxImprovingDiscount = 78;
        };

        struct EvalParameters final {
            int pawnValue = 114;
            int knightValue = 307;
            int bishopValue = 390;
            int rookValue = 497;
            int queenValue = 1011;
            int knightMobility = 5;
            int bishopMobility = 4;
            int rookMobility = 4;
            int queenMobility = 5;
            // 100 = default pressure; 0 = disabled, for A/B tests.
            int attackingPiecesWeight = 100;
        };

        [[nodiscard]] SearchParameters& search() noexcept;
        [[nodiscard]] EvalParameters& eval() noexcept;

        void resetSearch() noexcept;
        void resetEval() noexcept;
        void resetAll() noexcept;

        [[nodiscard]] bool setSearchParameter(std::string_view name, int value) noexcept;
        [[nodiscard]] bool setEvalParameter(std::string_view name, int value) noexcept;

        // UCI integration helpers. handleSetOption returns true when the option
        // belongs to the tuning system, including recognized-but-invalid input.
        [[nodiscard]] bool handleSetOption(std::string_view args, SearchEngine& searchEngine, std::ostream& out);
        void printUciOptions(std::ostream& out);
        void printCurrent(std::ostream& out);

    } // namespace Tuning

} // namespace ironphoenix
