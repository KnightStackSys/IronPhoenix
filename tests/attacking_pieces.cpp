#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>

using namespace ironphoenix;

namespace {
int failures = 0;
void expect(bool yes, const char* message) {
    if (!yes) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }
Position kings() {
    Position pos;
    pos.clear();
    pos.placePiece(sq('g', 1), makePiece(RED, KING));
    pos.placePiece(sq('a', 7), makePiece(BLUE, KING));
    pos.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    pos.placePiece(sq('n', 7), makePiece(GREEN, KING));
    pos.setSideToMove(RED);
    pos.finalizeSetup();
    return pos;
}
// Compare the same position with threat weights enabled/disabled; material
// and mobility remain identical between the two evaluations.
int bonus(const Position& pos, int weight = 100) {
    const Tuning::EvalParameters saved = Tuning::eval();
    Tuning::eval().attackingPiecesWeight = 0;
    const int base = Eval::evaluate(pos);
    Tuning::eval().attackingPiecesWeight = weight;
    const int withThreats = Eval::evaluate(pos);
    Tuning::eval() = saved;
    return withThreats - base;
}
}

int main() {
    Geometry::init();
    expect(!NNUE::loaded(), "tests require HCE mode (no loaded NNUE)");
    Tuning::resetEval();
    expect(Tuning::eval().attackingPiecesWeight == 100, "pressure default 100%");

    {
        Position pos = kings();
        expect(bonus(pos) == 0, "only kings: no attacked pieces");
        pos.placePiece(sq('c', 5), makePiece(RED, KNIGHT));
        pos.placePiece(sq('d', 7), makePiece(BLUE, QUEEN));
        pos.finalizeSetup();
        expect(bonus(pos) == 28, "knight threatens enemy queen");
        expect(bonus(pos, 200) == 56, "scaling threat weight to 200%");
        expect(bonus(pos, 0) == 0, "threat evaluation can be disabled");
        pos.setSideToMove(BLUE);
        pos.finalizeSetup();
        expect(bonus(pos) == -28, "team-relative sign reversal");
    }

    {
        Position pos = kings();
        // Rook from d6 and teammate rook from g7 both attack d7.
        pos.placePiece(sq('d', 6), makePiece(RED, ROOK));
        pos.placePiece(sq('g', 7), makePiece(YELLOW, ROOK));
        pos.placePiece(sq('d', 7), makePiece(BLUE, BISHOP));
        pos.finalizeSetup();
        expect(bonus(pos) == 10, "both teammates get one bishop-threat bonus");
        pos.erasePiece(sq('d', 7));
        pos.placePiece(sq('d', 7), makePiece(YELLOW, BISHOP));
        pos.finalizeSetup();
        expect(bonus(pos) == 0, "friendly attacked pieces never receive bonus");
    }
    {
        Position pos = kings();
        pos.placePiece(sq('f', 5), makePiece(RED, PAWN));
        pos.placePiece(sq('g', 6), makePiece(BLUE, ROOK));
        pos.finalizeSetup();
        expect(bonus(pos) == 16, "pawn attacks enemy rook");
    }

    expect(Tuning::setEvalParameter("EvalAttackingPiecesWeight", 0), "weight 0 accepted");
    expect(Tuning::eval().attackingPiecesWeight == 0, "weight 0 stored");
    expect(Tuning::setEvalParameter("EvalAttackingPiecesWeight", 200), "weight 200 accepted");
    expect(Tuning::eval().attackingPiecesWeight == 200, "weight 200 stored");
    expect(!Tuning::setEvalParameter("EvalAttackingPiecesWeight", -1), "negative rejected");
    expect(!Tuning::setEvalParameter("EvalAttackingPiecesWeight", 301), "above 300 rejected");

    Tuning::resetEval();
    if (!failures)
        std::cout << "Attacking-piece evaluation tests: PASS\n";
    return failures ? 1 : 0;
}
