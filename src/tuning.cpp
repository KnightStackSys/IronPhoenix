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
                || name == "SearchLMRDivisorX100";
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
                || name == "EvalSpaceControl"
                || name == "EvalInfiltrationKnight"
                || name == "EvalInfiltrationBishop"
                || name == "EvalInfiltrationRook"
                || name == "EvalInfiltrationQueen";
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
        else if (name == "EvalSpaceControl" && inRange(value, -100, 100))
            p.spaceControl = value;
        else if (name == "EvalInfiltrationKnight" && inRange(value, -100, 100))
            p.infiltrationKnight = value;
        else if (name == "EvalInfiltrationBishop" && inRange(value, -100, 100))
            p.infiltrationBishop = value;
        else if (name == "EvalInfiltrationRook" && inRange(value, -100, 100))
            p.infiltrationRook = value;
        else if (name == "EvalInfiltrationQueen" && inRange(value, -100, 100))
            p.infiltrationQueen = value;
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
        out << "option name SearchAspirationStartDepth type spin default 4 min 1 max 32\n"
            << "option name SearchAspirationDelta type spin default 50 min 1 max 2000\n"
            << "option name SearchSEEPruneMaxDepth type spin default 4 min 0 max 32\n"
            << "option name SearchSEEPruneMargin type spin default 80 min 0 max 2000\n"
            << "option name SearchHistoryBonusScale type spin default 64 min 0 max 2048\n"
            << "option name SearchHistoryBonusMax type spin default 2048 min 1 max 32767\n"
            << "option name SearchCounterMoveBonus type spin default 32000 min 0 max 50000000\n"
            << "option name SearchCheckMoveBonus type spin default 10000000 min 0 max 50000000\n"
            << "option name SearchLMRMinDepth type spin default 3 min 1 max 32\n"
            << "option name SearchLMRFullMoves type spin default 3 min 0 max 128\n"
            << "option name SearchLMRBaseX100 type spin default 75 min 0 max 1000\n"
            << "option name SearchLMRDivisorX100 type spin default 275 min 25 max 5000\n"
            << "option name EvalPawnValue type spin default 100 min 0 max 5000\n"
            << "option name EvalKnightValue type spin default 300 min 0 max 5000\n"
            << "option name EvalBishopValue type spin default 400 min 0 max 5000\n"
            << "option name EvalRookValue type spin default 500 min 0 max 5000\n"
            << "option name EvalQueenValue type spin default 1000 min 0 max 5000\n"
            << "option name EvalKnightMobility type spin default 4 min -100 max 100\n"
            << "option name EvalBishopMobility type spin default 4 min -100 max 100\n"
            << "option name EvalRookMobility type spin default 2 min -100 max 100\n"
            << "option name EvalQueenMobility type spin default 1 min -100 max 100\n"
            << "option name EvalSpaceControl type spin default 1 min -100 max 100\n"
            << "option name EvalInfiltrationKnight type spin default 3 min -100 max 100\n"
            << "option name EvalInfiltrationBishop type spin default 3 min -100 max 100\n"
            << "option name EvalInfiltrationRook type spin default 2 min -100 max 100\n"
            << "option name EvalInfiltrationQueen type spin default 1 min -100 max 100\n"
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
            << " SearchLMRDivisorX100=" << s.lmrDivisorX100 << '\n';

        out << "info string EvalPawnValue=" << e.pawnValue
            << " EvalKnightValue=" << e.knightValue
            << " EvalBishopValue=" << e.bishopValue
            << " EvalRookValue=" << e.rookValue
            << " EvalQueenValue=" << e.queenValue
            << " EvalKnightMobility=" << e.knightMobility
            << " EvalBishopMobility=" << e.bishopMobility
            << " EvalRookMobility=" << e.rookMobility
            << " EvalQueenMobility=" << e.queenMobility
            << " EvalSpaceControl=" << e.spaceControl
            << " EvalInfiltrationKnight=" << e.infiltrationKnight
            << " EvalInfiltrationBishop=" << e.infiltrationBishop
            << " EvalInfiltrationRook=" << e.infiltrationRook
            << " EvalInfiltrationQueen=" << e.infiltrationQueen << '\n';
    }

} // namespace ironphoenix::Tuning
