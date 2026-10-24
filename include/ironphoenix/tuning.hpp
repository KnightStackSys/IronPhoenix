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
            int fastCheckPrefilter = 1;
            int evalCacheEnabled = 1;
            int fastKingLegality = 1;
            int pawnAttackCache = 1;
            int kingRayPrefilter = 1;
        };

        struct EvalParameters final {
            int pawnValue = 118;
            int knightValue = 247;
            int bishopValue = 408;
            int rookValue = 440;
            int queenValue = 1111;
            int knightMobility = 12;
            int bishopMobility = 8;
            int rookMobility = 17;
            int queenMobility = 11;
            int attackingPiecesWeight = 120;
            int knightCheckThreatPenalty = 28;
            int knightDevelopmentBonus = 35;
            int knightEnemyPressureBonus = 43;
            int knightKingApproachBonus = 20;
            int knightOutpostBonus = 6;
            int badBishopPenalty = 1;
            int bishopPairBonus = 34;
            int bishopVsKnightBonus = 30;
            int bishopColorWeaknessPenalty = 3;
            int bishopFianchettoBonus = 31;
            int bishopReturningBonus = 1;
            int kingShieldPenalty = 6;
            int kingZoneAttackPenalty = 45;
            int kingPawnStormPenalty = 12;
            int kingMateThreatPenalty = 106;
            int kingCentralizationBonus = 2;
            int kingPawnlessWingPenalty = 13;
            int kingPinPenalty = 1;
            int kingXrayPenalty = 39;
            int kingCastlingRightsBonus = 10;
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
