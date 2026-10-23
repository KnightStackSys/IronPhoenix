#include "ironphoenix/tuning.hpp"

#include "ironphoenix/lmr.hpp"
#include "ironphoenix/search.hpp"

#include <charconv>
#include <ostream>
#include <string_view>

namespace ironphoenix::Tuning {

    namespace {

        std::string_view trim(std::string_view text) noexcept {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            return text;
        }

        bool parseInt(std::string_view text, int& value) noexcept {
            text = trim(text);
            if (text.empty())
                return false;

            const char* first = text.data();
            const char* last = text.data() + text.size();
            const auto [ptr, ec] = std::from_chars(first, last, value);
            return ec == std::errc{} && ptr == last;
        }

        bool inRange(int value, int minimum, int maximum) noexcept {
            return value >= minimum && value <= maximum;
        }

        bool isSearchParameter(std::string_view name) noexcept {
            return name == "SearchAspirationStartDepth"
                || name == "SearchAspirationDelta"
                || name == "SearchSEEPruneMaxDepth"
                || name == "SearchSEEPruneMargin"
                || name == "SearchHistoryBonusScale"
                || name == "SearchHistoryBonusMax"
                || name == "SearchCounterMoveBonus"
                || name == "SearchCheckMoveBonus"
                || name == "SearchLMRMinDepth"
                || name == "SearchLMRFullMoves"
                || name == "SearchLMRBaseX100"
                || name == "SearchLMRDivisorX100"
                || name == "SearchLMPHistoryThreshold"
                || name == "SearchHMPMaxDepth"
                || name == "SearchHMPMarginPerDepth"
                || name == "SearchNullMoveMinDepth"
                || name == "SearchNullMoveDivisor"
                || name == "SearchRFPMaxDepth"
                || name == "SearchRFPMarginPerDepth"
                || name == "SearchRFPMinMargin"
                || name == "SearchRFPMaxImprovingDiscount"
                || name == "SearchIIRMinDepth"
                || name == "SearchIIRReduction";
        }

        bool isEvalParameter(std::string_view name) noexcept {
            return name == "EvalPawnValue"
                || name == "EvalKnightValue"
                || name == "EvalBishopValue"
                || name == "EvalRookValue"
                || name == "EvalQueenValue"
                || name == "EvalKnightMobility"
                || name == "EvalBishopMobility"
                || name == "EvalRookMobility"
                || name == "EvalQueenMobility"
                || name == "EvalAttackingPiecesWeight"
                || name == "EvalKnightCheckThreatPenalty"
                || name == "EvalKnightDevelopmentBonus"
                || name == "EvalKnightEnemyPressureBonus"
                || name == "EvalKnightKingApproachBonus"
                || name == "EvalKnightOutpostBonus"
                || name == "EvalBadBishopPenalty"
                || name == "EvalBishopPairBonus"
                || name == "EvalBishopVsKnightBonus"
                || name == "EvalBishopColorWeaknessPenalty"
                || name == "EvalBishopFianchettoBonus"
                || name == "EvalBishopReturningBonus";
        }

        bool isLmrParameter(std::string_view name) noexcept {
            return name == "SearchLMRMinDepth"
                || name == "SearchLMRFullMoves"
                || name == "SearchLMRBaseX100"
                || name == "SearchLMRDivisorX100";
        }

    }

    SearchParameters& search() noexcept {
        // Function-local statics avoid cross-translation-unit initialization
        // order problems with the LMR table's startup construction.
        static SearchParameters parameters{};
        return parameters;
    }

    EvalParameters& eval() noexcept {
        static EvalParameters parameters{};
        return parameters;
    }

    void resetSearch() noexcept {
        search() = SearchParameters{};
        rebuildLmrReductionTable();
    }

    void resetEval() noexcept {
        eval() = EvalParameters{};
    }

    void resetAll() noexcept {
        search() = SearchParameters{};
        eval() = EvalParameters{};
        rebuildLmrReductionTable();
    }

    bool setSearchParameter(std::string_view name, int value) noexcept {
        auto& p = search();
        bool changed = true;

        if (name == "SearchAspirationStartDepth" && inRange(value, 1, 32))
            p.aspirationStartDepth = value;
        else if (name == "SearchAspirationDelta" && inRange(value, 1, 2000))
            p.aspirationInitialDelta = value;
        else if (name == "SearchSEEPruneMaxDepth" && inRange(value, 0, 32))
            p.seePruneMaxDepth = value;
        else if (name == "SearchSEEPruneMargin" && inRange(value, 0, 2000))
            p.seePruneMarginPerDepth = value;
        else if (name == "SearchHistoryBonusScale" && inRange(value, 0, 2048))
            p.historyBonusScale = value;
        else if (name == "SearchHistoryBonusMax" && inRange(value, 1, 32767))
            p.historyBonusMax = value;
        else if (name == "SearchCounterMoveBonus" && inRange(value, 0, 50'000'000))
            p.counterMoveBonus = value;
        else if (name == "SearchCheckMoveBonus" && inRange(value, 0, 50'000'000))
            p.checkMoveBonus = value;
        else if (name == "SearchLMRMinDepth" && inRange(value, 1, 32))
            p.lmrMinDepth = value;
        else if (name == "SearchLMRFullMoves" && inRange(value, 0, 128))
            p.lmrFullMoves = value;
        else if (name == "SearchLMRBaseX100" && inRange(value, 0, 1000))
            p.lmrBaseX100 = value;
        else if (name == "SearchLMRDivisorX100" && inRange(value, 25, 5000))
            p.lmrDivisorX100 = value;
        else if (name == "SearchLMPHistoryThreshold" && inRange(value, 0, 20000))
            p.lmpHistoryThreshold = value;
        else if (name == "SearchHMPMaxDepth" && inRange(value, 0, 32))
            p.hmpMaxDepth = value;
        else if (name == "SearchHMPMarginPerDepth" && inRange(value, 0, 10000))
            p.hmpMarginPerDepth = value;
        else if (name == "SearchNullMoveMinDepth" && inRange(value, 1, 32))
            p.nullMoveMinDepth = value;
        else if (name == "SearchNullMoveDivisor" && inRange(value, 1, 5000))
            p.nullMoveDivisor = value;
        else if (name == "SearchRFPMaxDepth" && inRange(value, 0, 32))
            p.rfpMaxDepth = value;
        else if (name == "SearchRFPMarginPerDepth" && inRange(value, 0, 5000))
            p.rfpMarginPerDepth = value;
        else if (name == "SearchRFPMinMargin" && inRange(value, 0, 5000))
            p.rfpMinMargin = value;
        else if (name == "SearchRFPMaxImprovingDiscount" && inRange(value, 0, 5000))
            p.rfpMaxImprovingDiscount = value;
        else if (name == "SearchIIRMinDepth" && inRange(value, 2, 32))
            p.iirMinDepth = value;
        else if (name == "SearchIIRReduction" && inRange(value, 0, 4))
            p.iirReduction = value;
        else
            changed = false;

        if (changed && isLmrParameter(name))
            rebuildLmrReductionTable();

        return changed;
    }

    bool setEvalParameter(std::string_view name, int value) noexcept {
        auto& p = eval();

        if (name == "EvalPawnValue" && inRange(value, 0, 5000))
            p.pawnValue = value;
        else if (name == "EvalKnightValue" && inRange(value, 0, 5000))
            p.knightValue = value;
        else if (name == "EvalBishopValue" && inRange(value, 0, 5000))
            p.bishopValue = value;
        else if (name == "EvalRookValue" && inRange(value, 0, 5000))
            p.rookValue = value;
        else if (name == "EvalQueenValue" && inRange(value, 0, 5000))
            p.queenValue = value;
        else if (name == "EvalKnightMobility" && inRange(value, -100, 100))
            p.knightMobility = value;
        else if (name == "EvalBishopMobility" && inRange(value, -100, 100))
            p.bishopMobility = value;
        else if (name == "EvalRookMobility" && inRange(value, -100, 100))
            p.rookMobility = value;
        else if (name == "EvalQueenMobility" && inRange(value, -100, 100))
            p.queenMobility = value;
        else if (name == "EvalAttackingPiecesWeight" && inRange(value, 0, 300))
            p.attackingPiecesWeight = value;
        else if (name == "EvalKnightCheckThreatPenalty" && inRange(value, 0, 100))
            p.knightCheckThreatPenalty = value;
        else if (name == "EvalKnightDevelopmentBonus" && inRange(value, 0, 50))
            p.knightDevelopmentBonus = value;
        else if (name == "EvalKnightEnemyPressureBonus" && inRange(value, 0, 50))
            p.knightEnemyPressureBonus = value;
        else if (name == "EvalKnightKingApproachBonus" && inRange(value, 0, 50))
            p.knightKingApproachBonus = value;
        else if (name == "EvalKnightOutpostBonus" && inRange(value, 0, 50))
            p.knightOutpostBonus = value;
        else if (name == "EvalBadBishopPenalty" && inRange(value, 0, 100))
            p.badBishopPenalty = value;
        else if (name == "EvalBishopPairBonus" && inRange(value, 0, 100))
            p.bishopPairBonus = value;
        else if (name == "EvalBishopVsKnightBonus" && inRange(value, 0, 100))
            p.bishopVsKnightBonus = value;
        else if (name == "EvalBishopColorWeaknessPenalty" && inRange(value, 0, 100))
            p.bishopColorWeaknessPenalty = value;
        else if (name == "EvalBishopFianchettoBonus" && inRange(value, 0, 100))
            p.bishopFianchettoBonus = value;
        else if (name == "EvalBishopReturningBonus" && inRange(value, 0, 100))
            p.bishopReturningBonus = value;
        else
            return false;

        return true;
    }

    bool handleSetOption(std::string_view args, SearchEngine& searchEngine, std::ostream& out) {
        args = trim(args);

        if (args == "name ResetTuning") {
            resetAll();
            searchEngine.clearHash();
            out << "info string tuning parameters reset to defaults\n";
            return true;
        }

        constexpr std::string_view namePrefix = "name ";
        constexpr std::string_view valueDelimiter = " value ";

        if (!args.starts_with(namePrefix))
            return false;

        const std::size_t valuePos = args.find(valueDelimiter, namePrefix.size());
        if (valuePos == std::string_view::npos)
            return false;

        const std::string_view name = trim(args.substr(namePrefix.size(), valuePos - namePrefix.size()));
        if (!isSearchParameter(name) && !isEvalParameter(name))
            return false;

        const std::string_view text = trim(args.substr(valuePos + valueDelimiter.size()));
        int value = 0;
        if (!parseInt(text, value)) {
            out << "info string invalid tuning value for " << name << '\n';
            return true;
        }

        const bool accepted = isSearchParameter(name)
            ? setSearchParameter(name, value)
            : setEvalParameter(name, value);

        if (!accepted) {
            out << "info string tuning value out of range for " << name << ": " << value << '\n';
            return true;
        }

        searchEngine.clearHash();
        out << "info string tuning parameter " << name << " set to " << value << '\n';
        return true;
    }

    void printUciOptions(std::ostream& out) {
        const SearchParameters searchDefaults{};
        const EvalParameters defaults{};
        out << "option name SearchAspirationStartDepth type spin default " << searchDefaults.aspirationStartDepth << " min 1 max 32\n"
            << "option name SearchAspirationDelta type spin default " << searchDefaults.aspirationInitialDelta << " min 1 max 2000\n"
            << "option name SearchSEEPruneMaxDepth type spin default " << searchDefaults.seePruneMaxDepth << " min 0 max 32\n"
            << "option name SearchSEEPruneMargin type spin default " << searchDefaults.seePruneMarginPerDepth << " min 0 max 2000\n"
            << "option name SearchHistoryBonusScale type spin default " << searchDefaults.historyBonusScale << " min 0 max 2048\n"
            << "option name SearchHistoryBonusMax type spin default " << searchDefaults.historyBonusMax << " min 1 max 32767\n"
            << "option name SearchCounterMoveBonus type spin default " << searchDefaults.counterMoveBonus << " min 0 max 50000000\n"
            << "option name SearchCheckMoveBonus type spin default " << searchDefaults.checkMoveBonus << " min 0 max 50000000\n"
            << "option name SearchLMRMinDepth type spin default " << searchDefaults.lmrMinDepth << " min 1 max 32\n"
            << "option name SearchLMRFullMoves type spin default " << searchDefaults.lmrFullMoves << " min 0 max 128\n"
            << "option name SearchLMRBaseX100 type spin default " << searchDefaults.lmrBaseX100 << " min 0 max 1000\n"
            << "option name SearchLMRDivisorX100 type spin default " << searchDefaults.lmrDivisorX100 << " min 25 max 5000\n"
            << "option name SearchLMPHistoryThreshold type spin default " << searchDefaults.lmpHistoryThreshold << " min 0 max 20000\n"
            << "option name SearchHMPMaxDepth type spin default " << searchDefaults.hmpMaxDepth << " min 0 max 32\n"
            << "option name SearchHMPMarginPerDepth type spin default " << searchDefaults.hmpMarginPerDepth << " min 0 max 10000\n"
            << "option name SearchNullMoveMinDepth type spin default " << searchDefaults.nullMoveMinDepth << " min 1 max 32\n"
            << "option name SearchNullMoveDivisor type spin default " << searchDefaults.nullMoveDivisor << " min 1 max 5000\n"
            << "option name SearchRFPMaxDepth type spin default " << searchDefaults.rfpMaxDepth << " min 0 max 32\n"
            << "option name SearchRFPMarginPerDepth type spin default " << searchDefaults.rfpMarginPerDepth << " min 0 max 5000\n"
            << "option name SearchRFPMinMargin type spin default " << searchDefaults.rfpMinMargin << " min 0 max 5000\n"
            << "option name SearchRFPMaxImprovingDiscount type spin default " << searchDefaults.rfpMaxImprovingDiscount << " min 0 max 5000\n"
            << "option name SearchIIRMinDepth type spin default " << searchDefaults.iirMinDepth << " min 2 max 32\n"
            << "option name SearchIIRReduction type spin default " << searchDefaults.iirReduction << " min 0 max 4\n"
            << "option name EvalPawnValue type spin default " << defaults.pawnValue << " min 0 max 5000\n"
            << "option name EvalKnightValue type spin default " << defaults.knightValue << " min 0 max 5000\n"
            << "option name EvalBishopValue type spin default " << defaults.bishopValue << " min 0 max 5000\n"
            << "option name EvalRookValue type spin default " << defaults.rookValue << " min 0 max 5000\n"
            << "option name EvalQueenValue type spin default " << defaults.queenValue << " min 0 max 5000\n"
            << "option name EvalKnightMobility type spin default " << defaults.knightMobility << " min -100 max 100\n"
            << "option name EvalBishopMobility type spin default " << defaults.bishopMobility << " min -100 max 100\n"
            << "option name EvalRookMobility type spin default " << defaults.rookMobility << " min -100 max 100\n"
            << "option name EvalQueenMobility type spin default " << defaults.queenMobility << " min -100 max 100\n"
            << "option name EvalAttackingPiecesWeight type spin default " << defaults.attackingPiecesWeight << " min 0 max 300\n"
            << "option name EvalKnightCheckThreatPenalty type spin default " << defaults.knightCheckThreatPenalty << " min 0 max 100\n"
            << "option name EvalKnightDevelopmentBonus type spin default " << defaults.knightDevelopmentBonus << " min 0 max 50\n"
            << "option name EvalKnightEnemyPressureBonus type spin default " << defaults.knightEnemyPressureBonus << " min 0 max 50\n"
            << "option name EvalKnightKingApproachBonus type spin default " << defaults.knightKingApproachBonus << " min 0 max 50\n"
            << "option name EvalKnightOutpostBonus type spin default " << defaults.knightOutpostBonus << " min 0 max 50\n"
            << "option name EvalBadBishopPenalty type spin default " << defaults.badBishopPenalty << " min 0 max 100\n"
            << "option name EvalBishopPairBonus type spin default " << defaults.bishopPairBonus << " min 0 max 100\n"
            << "option name EvalBishopVsKnightBonus type spin default " << defaults.bishopVsKnightBonus << " min 0 max 100\n"
            << "option name EvalBishopColorWeaknessPenalty type spin default " << defaults.bishopColorWeaknessPenalty << " min 0 max 100\n"
            << "option name EvalBishopFianchettoBonus type spin default " << defaults.bishopFianchettoBonus << " min 0 max 100\n"
            << "option name EvalBishopReturningBonus type spin default " << defaults.bishopReturningBonus << " min 0 max 100\n"
            << "option name ResetTuning type button\n";
    }

    void printCurrent(std::ostream& out) {
        const auto& s = search();
        const auto& e = eval();

        out << "info string SearchAspirationStartDepth=" << s.aspirationStartDepth
            << " SearchAspirationDelta=" << s.aspirationInitialDelta
            << " SearchSEEPruneMaxDepth=" << s.seePruneMaxDepth
            << " SearchSEEPruneMargin=" << s.seePruneMarginPerDepth
            << " SearchHistoryBonusScale=" << s.historyBonusScale
            << " SearchHistoryBonusMax=" << s.historyBonusMax
            << " SearchCounterMoveBonus=" << s.counterMoveBonus
            << " SearchCheckMoveBonus=" << s.checkMoveBonus
            << " SearchLMRMinDepth=" << s.lmrMinDepth
            << " SearchLMRFullMoves=" << s.lmrFullMoves
            << " SearchLMRBaseX100=" << s.lmrBaseX100
            << " SearchLMRDivisorX100=" << s.lmrDivisorX100
            << " SearchLMPHistoryThreshold=" << s.lmpHistoryThreshold
            << " SearchHMPMaxDepth=" << s.hmpMaxDepth
            << " SearchHMPMarginPerDepth=" << s.hmpMarginPerDepth
            << " SearchNullMoveMinDepth=" << s.nullMoveMinDepth
            << " SearchNullMoveDivisor=" << s.nullMoveDivisor
            << " SearchRFPMaxDepth=" << s.rfpMaxDepth
            << " SearchRFPMarginPerDepth=" << s.rfpMarginPerDepth
            << " SearchRFPMinMargin=" << s.rfpMinMargin
            << " SearchRFPMaxImprovingDiscount=" << s.rfpMaxImprovingDiscount
            << " SearchIIRMinDepth=" << s.iirMinDepth
            << " SearchIIRReduction=" << s.iirReduction << '\n';

        out << "info string EvalPawnValue=" << e.pawnValue
            << " EvalKnightValue=" << e.knightValue
            << " EvalBishopValue=" << e.bishopValue
            << " EvalRookValue=" << e.rookValue
            << " EvalQueenValue=" << e.queenValue
            << " EvalKnightMobility=" << e.knightMobility
            << " EvalBishopMobility=" << e.bishopMobility
            << " EvalRookMobility=" << e.rookMobility
            << " EvalQueenMobility=" << e.queenMobility
            << " EvalAttackingPiecesWeight=" << e.attackingPiecesWeight
            << " EvalKnightCheckThreatPenalty=" << e.knightCheckThreatPenalty
            << " EvalKnightDevelopmentBonus=" << e.knightDevelopmentBonus
            << " EvalKnightEnemyPressureBonus=" << e.knightEnemyPressureBonus
            << " EvalKnightKingApproachBonus=" << e.knightKingApproachBonus
            << " EvalKnightOutpostBonus=" << e.knightOutpostBonus
            << " EvalBadBishopPenalty=" << e.badBishopPenalty
            << " EvalBishopPairBonus=" << e.bishopPairBonus
            << " EvalBishopVsKnightBonus=" << e.bishopVsKnightBonus
            << " EvalBishopColorWeaknessPenalty=" << e.bishopColorWeaknessPenalty
            << " EvalBishopFianchettoBonus=" << e.bishopFianchettoBonus
            << " EvalBishopReturningBonus=" << e.bishopReturningBonus << '\n';
    }

} // namespace ironphoenix::Tuning
