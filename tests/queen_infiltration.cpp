#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>

using namespace ironphoenix;

namespace {
int failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}

Position queenPosition(Color queenColor, char file, int rank,
                       bool attacked = false) {
    Position pos;
    pos.clear();
    pos.placePiece(sq('g', 1), makePiece(RED, KING));
    pos.placePiece(sq('a', 7), makePiece(BLUE, KING));
    pos.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    pos.placePiece(sq('n', 7), makePiece(GREEN, KING));
    pos.placePiece(sq(file, rank), makePiece(queenColor, QUEEN));
    if (attacked)
        pos.placePiece(sq('d', 9), makePiece(BLUE, ROOK));
    pos.setSideToMove(queenColor);
    pos.finalizeSetup();
    return pos;
}

// Subtract an evaluation of the exact same position with the new term
// disabled. This isolates infiltration from material, mobility and the
// existing attacked-piece score without disabling any other features.
int bonus(const Position& pos, int weight = 2) {
    const auto saved = Tuning::eval();
    Tuning::eval().queenInfiltration = 0;
    const int baseline = Eval::evaluate(pos);
    Tuning::eval().queenInfiltration = weight;
    const int updated = Eval::evaluate(pos);
    Tuning::eval() = saved;
    return updated - baseline;
}
}

int main() {
    Geometry::init();
    expect(!NNUE::loaded(), "tests run in HCE mode");
    Tuning::resetEval();
    expect(Tuning::eval().queenInfiltration == 2, "default infiltration weight");

    const Position red = queenPosition(RED, 'd', 7);
    const Position yellow = queenPosition(YELLOW, 'k', 7);
    const Position blue = queenPosition(BLUE, 'g', 4);
    const Position green = queenPosition(GREEN, 'g', 11);
    expect(bonus(red) > 0, "RED infiltrates BLUE's camp");
    expect(bonus(yellow) > 0, "YELLOW infiltrates GREEN's camp");
    expect(bonus(blue) > 0, "BLUE infiltrates RED's camp");
    expect(bonus(green) > 0, "GREEN infiltrates YELLOW's camp");

    expect(bonus(red) == 24, "RED infiltration score accounts for depth and king distance");
    expect(bonus(red, 4) == 48, "infiltration scales with configured weight");
    expect(bonus(red, 0) == 0, "disabled infiltration adds no score");
    expect(bonus(queenPosition(RED, 'g', 4)) == 0, "centre is not infiltration");
    expect(bonus(queenPosition(RED, 'g', 11)) == 0, "allied king camp adds no bonus");
    expect(bonus(queenPosition(RED, 'g', 1)) == 0, "queen in own home camp adds no bonus");
    expect(bonus(queenPosition(RED, 'd', 7, true)) == 0, "hanging infiltrating queen is not rewarded");

    Position oppositeTurn = red;
    oppositeTurn.setSideToMove(BLUE);
    oppositeTurn.finalizeSetup();
    expect(bonus(oppositeTurn) == -24, "bonus follows team-relative score perspective");

    expect(Tuning::setEvalParameter("EvalQueenInfiltration", 0), "UCI setting permits disable");
    expect(Tuning::eval().queenInfiltration == 0, "zero stored");
    expect(Tuning::setEvalParameter("EvalQueenInfiltration", 20), "maximum weight accepted");
    expect(!Tuning::setEvalParameter("EvalQueenInfiltration", -1), "negative weight rejected");
    expect(!Tuning::setEvalParameter("EvalQueenInfiltration", 21), "weight beyond maximum rejected");
    Tuning::resetEval();
    expect(Tuning::eval().queenInfiltration == 2, "reset restores default");

    if (!failures)
        std::cout << "Queen infiltration tests: PASS\n";
    return failures ? 1 : 0;
}
