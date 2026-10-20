#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>
#include <memory>
#include <sstream>

using namespace ironphoenix;

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}
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
// Isolate the trial heuristic from existing material/mobility/attack scores.
int penaltyDelta(Position position, int weight = 5) {
    const auto original = Tuning::eval();
    Tuning::eval().knightCheckThreatPenalty = 0;
    const int baseline = Eval::evaluate(position);
    Tuning::eval().knightCheckThreatPenalty = weight;
    const int withThreat = Eval::evaluate(position);
    Tuning::eval() = original;
    return withThreat - baseline;
}
} // namespace

int main() {
    Geometry::init();
    NNUE::unloadNetwork();
    Tuning::resetEval();
    check(Tuning::eval().knightCheckThreatPenalty == 0, "failed check-threat term defaults to off");
    for (unsigned king = 0; king < SQUARE_NB; ++king) {
        Bitboard expected{};
        Bitboard checkingSquares = Geometry::KnightAttacks[king];
        while (checkingSquares)
            expected |= Geometry::KnightAttacks[popLsb(checkingSquares)];
        check(Geometry::KnightCheckApproach[king] == expected, "approach mask equals two knight jumps");
    }
    check(penaltyDelta(kings()) == 0, "kings alone do not incur knight-check penalty");

    {
        auto p = kings();
        // Blue can play g3-e2 or g3-i2, checking the Red king at g1.
        p.placePiece(sq('g', 3), makePiece(BLUE, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == -5, "enemy knight one move from checking our king");
        check(penaltyDelta(p, 10) == -10, "UCI weight scales threat");
        check(penaltyDelta(p, 0) == 0, "zero option disables");
        p.setSideToMove(YELLOW);
        p.finalizeSetup();
        check(penaltyDelta(p) == -5, "partner to move sees identical team penalty");
        p.setSideToMove(BLUE);
        p.finalizeSetup();
        check(penaltyDelta(p) == 5, "threat rewards opposing team perspective");
        p.setSideToMove(RED);
        p.finalizeSetup();

        // The same target king should count only once despite two attackers.
        p.placePiece(sq('f', 4), makePiece(GREEN, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == -5, "several knights threatening one king count once");
    }
    {
        auto p = kings();
        // Green can hop from g12 to e13 or i13 to check Yellow at g14.
        p.placePiece(sq('g', 12), makePiece(GREEN, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == -5, "enemy knight one move away from checking partner king");
        p.placePiece(sq('g', 3), makePiece(BLUE, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == -10, "both kings threatened: penalty capped at twice weight");
    }
    {
        auto p = kings();
        p.placePiece(sq('g', 3), makePiece(BLUE, KNIGHT));
        // Both potential checking landings occupied by knight's own team.
        p.placePiece(sq('e', 2), makePiece(BLUE, PAWN));
        p.placePiece(sq('i', 2), makePiece(GREEN, PAWN));
        p.finalizeSetup();
        check(penaltyDelta(p) == 0, "friendly-occupied checking landings don't count");

        // Occupied by the checked team is capturable: still a potential check.
        p.erasePiece(sq('e', 2));
        p.placePiece(sq('e', 2), makePiece(RED, PAWN));
        p.finalizeSetup();
        check(penaltyDelta(p) == -5, "potential capture check remains a threat");
    }
    {
        auto p = kings();
        p.placePiece(sq('f', 3), makePiece(BLUE, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == 0, "already-checking knight is not a future one-move threat");
    }
    {
        auto p = kings();
        p.placePiece(sq('c', 7), makePiece(RED, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == 5, "knight one move away from checking Blue king");
        p.placePiece(sq('l', 7), makePiece(YELLOW, KNIGHT));
        p.finalizeSetup();
        check(penaltyDelta(p) == 10, "both opposing kings threatened");
    }
    {
        auto p = kings();
        p.placePiece(sq('g', 3), makePiece(BLUE, KNIGHT));
        p.setAliveMask(0xDu); // Blue eliminated.
        p.finalizeSetup();
        check(penaltyDelta(p) == 0, "eliminated opponent knights ignored");
    }
    check(Tuning::setEvalParameter("EvalKnightCheckThreatPenalty", 0), "disable through UCI tuning");
    check(Tuning::setEvalParameter("EvalKnightCheckThreatPenalty", 100), "maximum weight allowed");
    check(!Tuning::setEvalParameter("EvalKnightCheckThreatPenalty", -1), "negative rejected");
    check(!Tuning::setEvalParameter("EvalKnightCheckThreatPenalty", 101), "over maximum rejected");
    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream out;
    check(Tuning::handleSetOption("name EvalKnightCheckThreatPenalty value 7", *engine, out),
          "UCI setoption recognized");
    check(Tuning::eval().knightCheckThreatPenalty == 7, "UCI setoption updates parameter");
    Tuning::printUciOptions(out);
    Tuning::printCurrent(out);
    check(out.str().find("option name EvalKnightCheckThreatPenalty type spin default 0 min 0 max 100") != std::string::npos,
          "UCI option advertised");
    check(out.str().find("EvalKnightCheckThreatPenalty=7") != std::string::npos,
          "current tuning shows parameter");
    Tuning::resetEval();
    check(Tuning::eval().knightCheckThreatPenalty == 0, "reset restores safe default");

    if (!failures)
        std::cout << "Knights one move from check: PASS\n";
    return failures ? 1 : 0;
}
