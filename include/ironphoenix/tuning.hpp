#pragma once

#include <iosfwd>
#include <string_view>

namespace ironphoenix {

    class SearchEngine;

    namespace Tuning {

        struct SearchParameters final {
            int aspirationStartDepth = 2;
            int aspirationInitialDelta = 54;
            int seePruneMaxDepth = 4;
            int seePruneMarginPerDepth = 87;
            int historyBonusScale = 63;
            int historyBonusMax = 2044;
            int counterMoveBonus = 31'995;
            int checkMoveBonus = 9'999'994;
            int lmrMinDepth = 1;
            int lmrFullMoves = 6;
            int lmrBaseX100 = 79;
            int lmrDivisorX100 = 300;
            int lmpHistoryThreshold = 1927;
            int hmpMaxDepth = 4;
            int hmpMarginPerDepth = 1522;
            int nullMoveMinDepth = 3;
            int nullMoveDivisor = 194;
            int rfpMaxDepth = 6;
            int rfpMarginPerDepth = 141;
            int rfpMinMargin = 93;
            int rfpMaxImprovingDiscount = 82;
            // Applies to every search node without a TT move at depth >= 6.
            int iirMinDepth = 6;
            int iirReduction = 1;
        };

        struct EvalParameters final {
            int pawnValue = 106;
            int knightValue = 303;
            int bishopValue = 455;
            int rookValue = 466;
            int queenValue = 1047;
            int knightMobility = 14;
            int bishopMobility = 5;
            int rookMobility = 16;
            int queenMobility = 6;
            int attackingPiecesWeight = 120;
            int knightCheckThreatPenalty = 5;
            int knightDevelopmentBonus = 17;
            int knightEnemyPressureBonus = 18;
            int knightKingApproachBonus = 5;
            int knightOutpostBonus = 2;
            int badBishopPenalty = 1;
            int bishopPairBonus = 51;
            int bishopVsKnightBonus = 36;
            int bishopColorWeaknessPenalty = 3;
            int bishopFianchettoBonus = 5;
            int bishopReturningBonus = 22;
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
