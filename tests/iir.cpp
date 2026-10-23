#include "ironphoenix/geometry.hpp"
#include "ironphoenix/iir.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace ironphoenix;

namespace {
int failures = 0;
void expect(bool passed, const char* name) {
    if (!passed) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
int main() {
    Geometry::init();
    Tuning::resetSearch();
    const Tuning::SearchParameters defaults{};

    struct Expected { const char* name; int Tuning::SearchParameters::*field; int value; };
    const Expected tuned[] = {
        {"SearchAspirationStartDepth", &Tuning::SearchParameters::aspirationStartDepth, 2},
        {"SearchAspirationDelta", &Tuning::SearchParameters::aspirationInitialDelta, 54},
        {"SearchSEEPruneMaxDepth", &Tuning::SearchParameters::seePruneMaxDepth, 4},
        {"SearchSEEPruneMargin", &Tuning::SearchParameters::seePruneMarginPerDepth, 87},
        {"SearchHistoryBonusScale", &Tuning::SearchParameters::historyBonusScale, 63},
        {"SearchHistoryBonusMax", &Tuning::SearchParameters::historyBonusMax, 2044},
        {"SearchCounterMoveBonus", &Tuning::SearchParameters::counterMoveBonus, 31995},
        {"SearchCheckMoveBonus", &Tuning::SearchParameters::checkMoveBonus, 9999994},
        {"SearchLMRMinDepth", &Tuning::SearchParameters::lmrMinDepth, 1},
        {"SearchLMRFullMoves", &Tuning::SearchParameters::lmrFullMoves, 6},
        {"SearchLMRBaseX100", &Tuning::SearchParameters::lmrBaseX100, 79},
        {"SearchLMRDivisorX100", &Tuning::SearchParameters::lmrDivisorX100, 300},
        {"SearchLMPHistoryThreshold", &Tuning::SearchParameters::lmpHistoryThreshold, 1927},
        {"SearchHMPMaxDepth", &Tuning::SearchParameters::hmpMaxDepth, 4},
        {"SearchHMPMarginPerDepth", &Tuning::SearchParameters::hmpMarginPerDepth, 1522},
        {"SearchNullMoveMinDepth", &Tuning::SearchParameters::nullMoveMinDepth, 3},
        {"SearchNullMoveDivisor", &Tuning::SearchParameters::nullMoveDivisor, 194},
        {"SearchRFPMaxDepth", &Tuning::SearchParameters::rfpMaxDepth, 6},
        {"SearchRFPMarginPerDepth", &Tuning::SearchParameters::rfpMarginPerDepth, 141},
        {"SearchRFPMinMargin", &Tuning::SearchParameters::rfpMinMargin, 93},
        {"SearchRFPMaxImprovingDiscount", &Tuning::SearchParameters::rfpMaxImprovingDiscount, 82},
        {"SearchIIRMinDepth", &Tuning::SearchParameters::iirMinDepth, 6},
        {"SearchIIRReduction", &Tuning::SearchParameters::iirReduction, 1},
    };
    std::ostringstream defaultsUci;
    Tuning::printUciOptions(defaultsUci);
    for (const auto& row : tuned) {
        expect(Tuning::search().*(row.field) == row.value,
               "SPSA search default matches requested value");
        expect(defaultsUci.str().find(std::string("option name ") + row.name
                    + " type spin default " + std::to_string(row.value) + " min ")
                   != std::string::npos,
               "UCI advertises current SPSA default");
    }
    expect(defaults.iirMinDepth == 6, "IIR default depth six");
    expect(defaults.iirReduction == 1, "IIR default reduction one");

    // IIR no longer restricts PV/cut/all nodes, the root, or check status.
    // Once the depth threshold is met, absence of a TT move is sufficient.
    expect(reduce(5, false) == 0, "depth below threshold");
    expect(reduce(6, false) == 1, "eligible depth six");
    expect(reduce(9, false) == 1, "deep node qualifies");
    expect(reduce(9, true) == 0, "TT move still suppresses IIR");

    expect(Tuning::setSearchParameter("SearchIIRMinDepth", 4), "set min depth");
    expect(Tuning::setSearchParameter("SearchIIRReduction", 3), "set reduction");
    expect(reduce(4, false) == 3, "configurable reduction");
    expect(reduce(4, true) == 0, "TT move suppresses tuned reduction");

    expect(Tuning::setSearchParameter("SearchIIRMinDepth", 2), "accept min depth 2");
    expect(reduce(2, false) == 1, "clamp keeps at least one depth");
    expect(Tuning::setSearchParameter("SearchIIRReduction", 0), "disable IIR");
    expect(reduce(9, false) == 0, "zero reduction disables feature");

    expect(!Tuning::setSearchParameter("SearchIIRMinDepth", 1), "reject min depth 1");
    expect(!Tuning::setSearchParameter("SearchIIRMinDepth", 33), "reject min depth 33");
    expect(!Tuning::setSearchParameter("SearchIIRReduction", -1), "reject negative reduction");
    expect(!Tuning::setSearchParameter("SearchIIRReduction", 5), "reject oversized reduction");
    expect(!Tuning::setSearchParameter("SearchIIRCutNode", 1), "obsolete cut knob rejected");
    expect(!Tuning::setSearchParameter("SearchIIRAllNode", 1), "obsolete all knob rejected");

    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream out;
    expect(Tuning::handleSetOption("name SearchIIRMinDepth value 7", *engine, out),
           "UCI setoption recognized");
    expect(Tuning::search().iirMinDepth == 7, "UCI changes IIR minimum depth");
    Tuning::printUciOptions(out);
    Tuning::printCurrent(out);
    expect(out.str().find("option name SearchIIRMinDepth type spin default 6 min 2 max 32")
           != std::string::npos, "min-depth UCI option advertised");
    expect(out.str().find("option name SearchIIRReduction type spin default 1 min 0 max 4")
           != std::string::npos, "reduction UCI option advertised");
    expect(out.str().find("option name SearchIIRCutNode type spin") == std::string::npos,
           "cut switch not advertised");
    expect(out.str().find("option name SearchIIRAllNode type spin") == std::string::npos,
           "all switch not advertised");
    expect(out.str().find("SearchIIRMinDepth=7") != std::string::npos,
           "tuneparams reports active depth");

    Tuning::resetSearch();
    expect(Tuning::search().iirMinDepth == defaults.iirMinDepth
           && Tuning::search().iirReduction == defaults.iirReduction,
           "reset restores both IIR defaults");

    if (!failures) std::cout << "Internal iterative reduction tuning: PASS\n";
    return failures ? 1 : 0;
}
