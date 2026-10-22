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

Square sq(char f, int rank) { return Geometry::square(f - 'a', rank - 1); }
void expect(bool ok, const char* label) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}
Position kings(Color side = RED) {
    Position p;
    p.clear();
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
    p.setSideToMove(side);
    p.finalizeSetup();
    return p;
}
int outpostDelta(Position p, int bonus = 6) {
    const auto saved = Tuning::eval();
    Tuning::eval().knightOutpostBonus = 0;
    const int baseline = Eval::evaluate(p);
    Tuning::eval().knightOutpostBonus = bonus;
    const int withOutpost = Eval::evaluate(p);
    Tuning::eval() = saved;
    return withOutpost - baseline;
}
} // namespace

int main() {
    Geometry::init();
    NNUE::unloadNetwork();
    Tuning::resetEval();
    expect(Tuning::eval().knightOutpostBonus == 6, "outpost default 6");
    expect(Tuning::eval().knightCheckThreatPenalty == 0, "failed check-threat feature disabled");
    expect(Tuning::eval().knightDevelopmentBonus == 0, "failed back-rank feature disabled");
    expect(Tuning::eval().knightEnemyPressureBonus == 0, "failed enemy proximity feature disabled");
    expect(Tuning::eval().knightKingApproachBonus == 0, "failed king proximity feature disabled");

    auto red = kings();
    red.placePiece(sq('e', 5), makePiece(RED, KNIGHT));
    red.placePiece(sq('d', 4), makePiece(RED, PAWN)); // Supports e5.
    red.placePiece(sq('c', 6), makePiece(BLUE, PAWN)); // Nearby, doesn't attack e5.
    red.finalizeSetup();
    expect(outpostDelta(red) == 6, "pawn-backed knight active on Blue flank");
    expect(outpostDelta(red, 12) == 12, "outpost weight scales");
    expect(outpostDelta(red, 0) == 0, "outpost can be disabled");

    red.setSideToMove(YELLOW);
    red.finalizeSetup();
    expect(outpostDelta(red) == 6, "partner to move sees the same team bonus");
    red.setSideToMove(BLUE);
    red.finalizeSetup();
    expect(outpostDelta(red) == -6, "enemy side-to-move receives reversed sign");

    {
        auto p = kings();
        p.placePiece(sq('e',5),makePiece(RED,KNIGHT));
        p.placePiece(sq('f',6),makePiece(YELLOW,PAWN)); // Partner supports e5.
        p.placePiece(sq('c',6),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 6, "partner pawn can defend outpost");

        p.setAliveMask(0xBu); // Yellow eliminated.
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "eliminated partner pawn cannot defend outpost");
    }
    {
        auto p = kings();
        p.placePiece(sq('e',5),makePiece(RED,KNIGHT));
        p.placePiece(sq('c',6),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "no bonus without pawn support");
        p.placePiece(sq('d',4),makePiece(RED,PAWN));
        p.placePiece(sq('d',6),makePiece(BLUE,PAWN)); // Blue pawn attacks e5.
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "enemy pawn attack disqualifies outpost");

        p.erasePiece(sq('d',6));
        p.setAliveMask(0xDu); // Blue eliminated.
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "eliminated enemy army does not count");
    }
    {
        auto p = kings();
        p.placePiece(sq('e',5),makePiece(RED,KNIGHT));
        p.placePiece(sq('d',4),makePiece(RED,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "isolated flank with no nearby enemy piece");
        p.placePiece(sq('c',6),makePiece(YELLOW,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "allied pawn cannot become enemy target");
    }
    {
        auto p = kings();
        p.placePiece(sq('g',7),makePiece(RED,KNIGHT));
        p.placePiece(sq('f',6),makePiece(RED,PAWN));
        p.placePiece(sq('h',8),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "center is not enemy flank even if supported");
    }
    {
        auto p = kings();
        p.placePiece(sq('e',1),makePiece(RED,KNIGHT));
        p.placePiece(sq('d',2),makePiece(YELLOW,PAWN));
        p.placePiece(sq('d',3),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 0, "back-rank knight has not developed");
    }
    {
        auto p = kings(BLUE);
        p.placePiece(sq('d',5),makePiece(BLUE,KNIGHT));
        p.placePiece(sq('c',4),makePiece(BLUE,PAWN));
        p.placePiece(sq('f',6),makePiece(RED,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 6, "Blue knight approaching Red flank");
        p.setSideToMove(RED);
        p.finalizeSetup();
        expect(outpostDelta(p) == -6, "Blue flank activity team-relative");
    }
    {
        auto p = kings(YELLOW);
        p.placePiece(sq('e',10),makePiece(YELLOW,KNIGHT));
        p.placePiece(sq('f',11),makePiece(YELLOW,PAWN));
        p.placePiece(sq('c',9),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 6, "Yellow supported knight near Blue flank");
    }
    {
        auto p = kings(GREEN);
        p.placePiece(sq('k',5),makePiece(GREEN,KNIGHT));
        p.placePiece(sq('l',4),makePiece(GREEN,PAWN));
        p.placePiece(sq('i',6),makePiece(RED,PAWN));
        p.finalizeSetup();
        expect(outpostDelta(p) == 6, "Green supported knight near Red flank");
    }
    {
        auto p = kings();
        p.placePiece(sq('e',5),makePiece(RED,KNIGHT));
        p.placePiece(sq('e',6),makePiece(RED,KNIGHT));
        p.placePiece(sq('d',5),makePiece(RED,KNIGHT));
        p.placePiece(sq('d',4),makePiece(RED,PAWN));
        p.placePiece(sq('f',5),makePiece(RED,PAWN));
        p.placePiece(sq('c',4),makePiece(RED,PAWN));
        p.placePiece(sq('d',6),makePiece(BLUE,KNIGHT));
        p.finalizeSetup();
        expect(outpostDelta(p) == 12, "outposts capped at two per team");
    }

    expect(Tuning::setEvalParameter("EvalKnightOutpostBonus", 0), "zero allowed");
    expect(Tuning::eval().knightOutpostBonus == 0, "zero stored");
    expect(Tuning::setEvalParameter("EvalKnightOutpostBonus", 50), "max allowed");
    expect(!Tuning::setEvalParameter("EvalKnightOutpostBonus", -1), "negative rejected");
    expect(!Tuning::setEvalParameter("EvalKnightOutpostBonus", 51), "over max rejected");
    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream out;
    expect(Tuning::handleSetOption("name EvalKnightOutpostBonus value 8", *engine, out),
           "outpost UCI setter recognized");
    expect(Tuning::eval().knightOutpostBonus == 8, "UCI stores 8");
    Tuning::printUciOptions(out);
    Tuning::printCurrent(out);
    expect(out.str().find("option name EvalKnightOutpostBonus type spin default 6 min 0 max 50")
           != std::string::npos, "UCI advertises new option");
    expect(out.str().find("EvalKnightOutpostBonus=8") != std::string::npos,
           "UCI prints current value");
    Tuning::resetEval();
    expect(Tuning::eval().knightOutpostBonus == 6, "reset restores outpost default");

    if (!failures)
        std::cout << "Conservative knight outpost evaluation: PASS\n";
    return failures ? 1 : 0;
}
