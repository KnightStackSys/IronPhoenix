#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

using namespace ironphoenix;

namespace {
int failures = 0;
void expect(bool ok, const char* reason) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << reason << '\n'; }
}
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }

Position kings() {
    Position p;
    p.clear();
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}

using P = Tuning::EvalParameters;
int contribution(Position p, int P::*field, int amount) {
    const P original = Tuning::eval();
    Tuning::eval().*field = 0;
    const int without = Eval::evaluate(p);
    Tuning::eval().*field = amount;
    const int with = Eval::evaluate(p);
    Tuning::eval() = original;
    return with - without;
}

void testOptions() {
    Tuning::resetEval();
    const P defaults{};
    struct Weight { const char* name; int P::*field; int value; int max; };
    const Weight parameters[] = {
        {"EvalKingShieldPenalty", &P::kingShieldPenalty, 10, 200},
        {"EvalKingZoneAttackPenalty", &P::kingZoneAttackPenalty, 6, 200},
        {"EvalKingPawnStormPenalty", &P::kingPawnStormPenalty, 5, 200},
        {"EvalKingMateThreatPenalty", &P::kingMateThreatPenalty, 30, 1000},
        {"EvalKingCentralizationBonus", &P::kingCentralizationBonus, 4, 200},
        {"EvalKingPawnlessWingPenalty", &P::kingPawnlessWingPenalty, 14, 200},
        {"EvalKingPinPenalty", &P::kingPinPenalty, 14, 200},
        {"EvalKingXrayPenalty", &P::kingXrayPenalty, 8, 200},
        {"EvalKingCastlingRightsBonus", &P::kingCastlingRightsBonus, 8, 200},
    };
    std::ostringstream uci;
    Tuning::printUciOptions(uci);
    for (const auto& p : parameters) {
        expect(Tuning::eval().*(p.field) == p.value, "feature starts at default");
        expect(uci.str().find(std::string("option name ") + p.name
                   + " type spin default " + std::to_string(p.value)
                   + " min 0 max " + std::to_string(p.max) + "\n")
                   != std::string::npos, "UCI option format/default valid");
        expect(Tuning::setEvalParameter(p.name, p.max), "accept upper bound");
        expect(Tuning::eval().*(p.field) == p.max, "new weight stored");
        expect(!Tuning::setEvalParameter(p.name, p.max+1), "reject upper overflow");
        expect(!Tuning::setEvalParameter(p.name, -1), "reject negative weight");
    }
    Tuning::resetEval();
    for (const auto& p : parameters)
        expect(Tuning::eval().*(p.field) == defaults.*(p.field),
               "reset restores current default");
    std::ostringstream params;
    Tuning::printCurrent(params);
    expect(params.str().find("EvalKingPinPenalty=14") != std::string::npos,
           "tuneparams reports king weights");
}

void testMiddlegame() {
    auto pos = kings();
    // Only enemy non-pawn pieces enable the middlegame safety blend.
    pos.placePiece(sq('c', 9), makePiece(BLUE, QUEEN));
    pos.placePiece(sq('l', 9), makePiece(GREEN, QUEEN));
    pos.finalizeSetup();

    const int noShield = contribution(pos, &P::kingShieldPenalty, 100);
    expect(noShield < 0, "missing shield penalizes Red king");
    pos.placePiece(sq('f', 2), makePiece(RED, PAWN));
    pos.placePiece(sq('g', 2), makePiece(RED, PAWN));
    pos.placePiece(sq('h', 2), makePiece(RED, PAWN));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingShieldPenalty, 100) > noShield,
           "three forward shield pawns protect Red king");

    pos.erasePiece(sq('f', 2));
    pos.erasePiece(sq('g', 2));
    pos.erasePiece(sq('h', 2));
    pos.placePiece(sq('g', 3), makePiece(BLUE, PAWN));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingPawnStormPenalty, 100) < 0,
           "nearby enemy pawn penalizes Red king");

    pos.setSideToMove(BLUE);
    pos.finalizeSetup();
    const int blue = contribution(pos, &P::kingPawnStormPenalty, 100);
    pos.setSideToMove(RED);
    pos.finalizeSetup();
    expect(blue == -contribution(pos, &P::kingPawnStormPenalty, 100),
           "king features obey team-relative perspective");
}

void testKingZoneMatePressure() {
    auto pos = kings();
    pos.erasePiece(sq('g', 1));
    pos.placePiece(sq('d', 1), makePiece(RED, KING));
    pos.placePiece(sq('d', 6), makePiece(BLUE, ROOK));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingZoneAttackPenalty, 100) < 0,
           "enemy rook controls squares around the king");
    expect(contribution(pos, &P::kingMateThreatPenalty, 100) < 0,
           "constrained checked king receives at-a-glance danger penalty");
}

void testEndgame() {
    auto pos = kings();
    const int home = contribution(pos, &P::kingCentralizationBonus, 12);
    pos.erasePiece(sq('g', 1));
    pos.placePiece(sq('g', 7), makePiece(RED, KING));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingCentralizationBonus, 12) > home,
           "endgame king centralization rewards active king");

    pos.erasePiece(sq('g', 7));
    pos.placePiece(sq('d', 1), makePiece(RED, KING));
    pos.finalizeSetup();
    const int wing = contribution(pos, &P::kingPawnlessWingPenalty, 100);
    expect(wing < 0, "pawnless wing penalizes endgame king");
    pos.placePiece(sq('d', 2), makePiece(RED, PAWN));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingPawnlessWingPenalty, 100) > wing,
           "pawn on same wing removes penalty");
}

void testPinsAndXRays() {
    auto pos = kings();
    pos.placePiece(sq('g', 3), makePiece(RED, KNIGHT));
    pos.placePiece(sq('g', 8), makePiece(BLUE, ROOK));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingPinPenalty, 100) < 0,
           "rook pins a piece to Red king");
    pos.erasePiece(sq('g', 8));
    pos.placePiece(sq('i', 8), makePiece(BLUE, ROOK));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingPinPenalty, 100) == 0,
           "off-ray rook cannot pin");

    pos = kings();
    pos.placePiece(sq('g', 2), makePiece(RED, QUEEN));
    pos.placePiece(sq('g', 3), makePiece(RED, PAWN));
    pos.placePiece(sq('g', 8), makePiece(BLUE, ROOK));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingXrayPenalty, 100) < 0,
           "friendly pawn screens own queen from enemy rook X-ray");
    pos.erasePiece(sq('g', 3));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingXrayPenalty, 100) == 0,
           "no X-ray penalty without a screening piece");
}

void testCastlingRights() {
    auto pos = kings();
    pos.placePiece(sq('d', 1), makePiece(RED, ROOK));
    pos.placePiece(sq('k', 9), makePiece(BLUE, QUEEN));
    pos.configureCastle(RED, 0,
        CastleLane{sq('g', 1), sq('d', 1), sq('i', 1), sq('h', 1), 0});
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingCastlingRightsBonus, 100) == 0,
           "castle geometry alone does not grant rights bonus");
    pos.setCastlingRights(1);
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingCastlingRightsBonus, 100) > 0,
           "real castling right plus king and rook earns bonus");
    pos.erasePiece(sq('d', 1));
    pos.finalizeSetup();
    expect(contribution(pos, &P::kingCastlingRightsBonus, 100) == 0,
           "missing rook invalidates bonus despite stale rights flag");
}

} // namespace

int main() {
    Geometry::init();
    // Compare the same exact HCE scores across the geometric ray prefilter
    // on hand-built king, pin, and queen X-ray positions.
    auto kingTest = kings();
    kingTest.placePiece(sq('g', 3), makePiece(RED, KNIGHT));
    kingTest.placePiece(sq('g', 8), makePiece(BLUE, ROOK));
    kingTest.placePiece(sq('j', 9), makePiece(GREEN, QUEEN));
    kingTest.finalizeSetup();
    Tuning::search().kingRayPrefilter = 0;
    const int withoutRayPrefilter = Eval::evaluate(kingTest);
    Tuning::search().kingRayPrefilter = 1;
    expect(Eval::evaluate(kingTest) == withoutRayPrefilter,
           "ray prefilter preserves exact king pin/xray score");
    Tuning::resetSearch();
    expect(!NNUE::loaded(), "king evaluation features apply to HCE");
    testOptions();
    testMiddlegame();
    testKingZoneMatePressure();
    testEndgame();
    testPinsAndXRays();
    testCastlingRights();
    Tuning::resetEval();
    if (!failures) std::cout << "4PC Teams king evaluation tests: PASS\n";
    return failures ? 1 : 0;
}
