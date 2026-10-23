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
int reduce(int depth, int ply, bool pv, bool cut, bool tt, bool check) {
    return IIR::reduction(depth, ply, pv, cut, tt, check, Tuning::search());
}
}

int main() {
    Geometry::init();
    Tuning::resetSearch();
    const Tuning::SearchParameters defaults{};
    expect(defaults.iirMinDepth == 6, "IIR default depth six");
    expect(defaults.iirReduction == 1, "IIR default reduction one");
    expect(defaults.iirCutNode == 0 && defaults.iirAllNode == 0,
           "IIR remains PV-only by default");

    expect(reduce(5, 1, true, false, false, false) == 0, "below minimum depth");
    expect(reduce(6, 1, true, false, false, false) == 1, "PV IIR at depth six");
    expect(reduce(9, 2, true, false, false, false) == 1, "PV deep IIR");
    expect(reduce(9, 0, true, false, false, false) == 0, "root excluded");
    expect(reduce(9, 2, true, false, true, false) == 0, "TT move excludes IIR");
    expect(reduce(9, 2, true, false, false, true) == 0, "check excludes IIR");
    expect(reduce(9, 2, false, true, false, false) == 0, "cut node off by default");
    expect(reduce(9, 2, false, false, false, false) == 0, "all node off by default");

    expect(Tuning::setSearchParameter("SearchIIRMinDepth", 4), "set minimum depth");
    expect(Tuning::setSearchParameter("SearchIIRReduction", 3), "set reduction");
    expect(Tuning::setSearchParameter("SearchIIRCutNode", 1), "enable cut node");
    expect(Tuning::setSearchParameter("SearchIIRAllNode", 1), "enable all node");
    expect(reduce(4, 2, true, false, false, false) == 3, "tuned PV reduction");
    expect(reduce(4, 2, false, true, false, false) == 3, "tuned cut reduction");
    expect(reduce(4, 2, false, false, false, false) == 3, "tuned all reduction");
    expect(reduce(2, 2, true, false, false, false) == 0, "under tuned minimum");

    expect(Tuning::setSearchParameter("SearchIIRMinDepth", 2), "low min depth accepted");
    expect(reduce(2, 1, true, false, false, false) == 1,
           "reduction clamps to keep at least one depth");
    expect(Tuning::setSearchParameter("SearchIIRReduction", 0), "disable via zero");
    expect(reduce(9, 2, true, false, false, false) == 0,
           "zero reduction disables feature");

    expect(!Tuning::setSearchParameter("SearchIIRMinDepth", 1), "reject shallow min");
    expect(!Tuning::setSearchParameter("SearchIIRMinDepth", 33), "reject excessive min");
    expect(!Tuning::setSearchParameter("SearchIIRReduction", -1), "reject negative reduction");
    expect(!Tuning::setSearchParameter("SearchIIRReduction", 5), "reject excessive reduction");
    expect(!Tuning::setSearchParameter("SearchIIRCutNode", 2), "reject invalid cut switch");
    expect(!Tuning::setSearchParameter("SearchIIRAllNode", -1), "reject invalid all switch");

    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream out;
    expect(Tuning::handleSetOption("name SearchIIRMinDepth value 7", *engine, out),
           "IIR option processed through UCI");
    expect(Tuning::search().iirMinDepth == 7, "IIR UCI option stores value");
    Tuning::printUciOptions(out);
    Tuning::printCurrent(out);
    expect(out.str().find("option name SearchIIRMinDepth type spin default 6 min 2 max 32")
           != std::string::npos, "min depth advertised");
    expect(out.str().find("option name SearchIIRReduction type spin default 1 min 0 max 4")
           != std::string::npos, "reduction advertised");
    expect(out.str().find("option name SearchIIRCutNode type spin default 0 min 0 max 1")
           != std::string::npos, "cut node switch advertised");
    expect(out.str().find("option name SearchIIRAllNode type spin default 0 min 0 max 1")
           != std::string::npos, "all node switch advertised");
    expect(out.str().find("SearchIIRMinDepth=7") != std::string::npos,
           "tuneparams includes IIR parameter");

    Tuning::resetSearch();
    expect(Tuning::search().iirMinDepth == defaults.iirMinDepth
           && Tuning::search().iirReduction == defaults.iirReduction
           && Tuning::search().iirCutNode == defaults.iirCutNode
           && Tuning::search().iirAllNode == defaults.iirAllNode,
           "reset restores all four IIR defaults");

    if (!failures) std::cout << "Internal iterative reduction tuning: PASS\n";
    return failures ? 1 : 0;
}
