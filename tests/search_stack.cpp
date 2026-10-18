#include "ironphoenix/search_stack.hpp"
#include <iostream>

using namespace ironphoenix;

namespace {
    int failures = 0;
    void expect(bool ok, const char* message) {
        if (!ok) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }
}

int main() {
    SearchStackEntry entry{};
    entry.history = HistoryContext{ Move{3, 4}, makePiece(RED, KNIGHT), RED };
    entry.beginNode(BLUE, 9, true);
    expect(entry.history.valid() && entry.history.move == Move(3, 4), "parent history preserved");
    expect(entry.depth == 9 && entry.pvNode && entry.ttPv, "PV begins with ttPv");
    expect(!entry.ttHit && !entry.staticEvalValid && entry.moveCount == 0, "initial state");

    // TTProbe is undefined on a miss. Only access its fields on a hit.
    TTProbe miss;
    entry.recordTT(miss, false);
    expect(!entry.ttHit && !entry.ttMove, "TT miss move");
    expect(entry.ttDepth == -1 && entry.ttBound == TTBound::None && entry.ttPv, "TT miss metadata");

    TTProbe probe{};
    probe.move = Move{5, 6};
    probe.depth = 7;
    probe.bound = TTBound::Lower;
    probe.pvNode = true;
    entry.beginNode(RED, 8, false);
    entry.recordTT(probe, true);
    expect(entry.ttHit && entry.ttPv && entry.ttMove == Move(5, 6), "TT PV hint");
    expect(entry.ttDepth == 7 && entry.ttBound == TTBound::Lower, "TT depth and bound");

    probe.pvNode = false;
    entry.recordTT(probe, true);
    expect(!entry.ttPv, "non-PV TT entry");
    entry.beginNode(BLUE, 4, false);
    expect(entry.history.valid() && !entry.ttHit && !entry.ttMove && !entry.ttPv, "reset node but not parent history");

    SearchStackEntry earlier{};
    earlier.beginNode(RED, 5, false);
    earlier.recordStaticEval(125);

    SearchStackEntry current{};
    current.beginNode(YELLOW, 3, false); // Same team as RED.
    current.recordStaticEval(170, &earlier);
    expect(current.improving && current.improvingRate == 45, "positive same-team improvement");
    current.recordStaticEval(100, &earlier);
    expect(!current.improving && current.improvingRate == -25, "negative same-team trend");
    current.recordStaticEval(125, &earlier);
    expect(!current.improving && current.improvingRate == 0, "equal evaluation trend");

    current.beginNode(BLUE, 2, false);
    current.recordStaticEval(-70, &earlier);
    expect(current.staticEvalValid && !current.improving && current.improvingRate == 0, "reject enemy score perspective");
    current.beginNode(RED, 6, false);
    current.recordStaticEval(150);
    expect(!current.improving && current.improvingRate == 0, "unknown previous evaluation");

    current.currentMove = Move{10, 11};
    current.bestMove = Move{10, 11};
    current.moveCount = 5;
    current.beginNode(YELLOW, 0, false);
    expect(!current.currentMove && !current.bestMove, "move fields reset");
    expect(current.moveCount == 0 && !current.staticEvalValid, "count and eval reset");

    return failures == 0 ? 0 : 1;
}
