#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

using namespace ironphoenix;

namespace {
int failures = 0;
void expect(bool yes, const char* label) {
    if (!yes) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
Square sq(char f, int rank) { return Geometry::square(f - 'a', rank - 1); }
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
int delta(Position p, int development = 3, int pressure = 4, int approach = 5) {
    const auto saved = Tuning::eval();
    Tuning::eval().knightDevelopmentBonus = 0;
    Tuning::eval().knightEnemyPressureBonus = 0;
    Tuning::eval().knightKingApproachBonus = 0;
    const int baseline = Eval::evaluate(p);
    Tuning::eval().knightDevelopmentBonus = development;
    Tuning::eval().knightEnemyPressureBonus = pressure;
    Tuning::eval().knightKingApproachBonus = approach;
    const int updated = Eval::evaluate(p);
    Tuning::eval() = saved;
    return updated - baseline;
}
void trial(Color c, char homeFile, int homeRank, char developedFile, int developedRank) {
    auto p = kings(c);
    p.placePiece(sq(homeFile, homeRank), makePiece(c, KNIGHT));
    p.finalizeSetup();
    expect(delta(p,3,0,0) == 0, "back-rank knight un-developed");
    p.erasePiece(sq(homeFile,homeRank));
    p.placePiece(sq(developedFile,developedRank), makePiece(c,KNIGHT));
    p.finalizeSetup();
    expect(delta(p,3,0,0) == 3, "off home rank/file earns development");
}
} // namespace

int main() {
    Geometry::init();
    NNUE::unloadNetwork();
    Tuning::resetEval();
    const auto defaults = Tuning::EvalParameters{};
    expect(Tuning::eval().knightDevelopmentBonus == defaults.knightDevelopmentBonus, "development default");
    expect(Tuning::eval().knightEnemyPressureBonus == defaults.knightEnemyPressureBonus, "pressure default");
    expect(Tuning::eval().knightKingApproachBonus == defaults.knightKingApproachBonus, "king approach default");

    // All four player orientations must use the proper home row or file.
    trial(RED, 'f', 1, 'f', 2);
    trial(BLUE, 'a', 5, 'b', 5);
    trial(YELLOW, 'f', 14, 'f', 13);
    trial(GREEN, 'n', 8, 'm', 8);

    {
        auto p = kings();
        p.placePiece(sq('g', 7),makePiece(RED,KNIGHT));
        p.finalizeSetup();
        expect(delta(p) == 3, "centre without enemy targets receives no centre bonus");
        p.placePiece(sq('h', 8),makePiece(BLUE,PAWN));
        p.finalizeSetup();
        expect(delta(p) == 7, "enemy pawn near center activates army-pressure bonus");
        p.erasePiece(sq('h',8));
        p.placePiece(sq('h',8),makePiece(YELLOW,PAWN));
        p.finalizeSetup();
        expect(delta(p) == 3, "allied piece near knight is not enemy pressure");
        p.erasePiece(sq('h',8));
        p.placePiece(sq('h',8),makePiece(BLUE,PAWN));
        p.placePiece(sq('h',9),makePiece(GREEN,PAWN));
        p.finalizeSetup();
        expect(delta(p) == 7, "several nearby enemy non-knights count once per knight");
        p.setAliveMask(0xDu); // Blue eliminated; Green stays active.
        p.erasePiece(sq('h',9));
        p.finalizeSetup();
        expect(delta(p) == 3, "eliminated opponent army does not count");
    }
    {
        auto p = kings();
        p.placePiece(sq('g',7), makePiece(RED, KNIGHT));
        p.placePiece(sq('h',9), makePiece(GREEN, KNIGHT));
        p.finalizeSetup();
        // Both knights receive development and nearby enemy-piece bonuses;
        // their identical +7 scores cancel for a team-relative zero.
        expect(delta(p) == 0, "opposing knight activity cancels symmetrically");
    }
    {
        auto p = kings();
        p.placePiece(sq('d', 7),makePiece(RED,KNIGHT)); // Blue king a7: dist 3
        p.finalizeSetup();
        expect(delta(p,0,0,5) == 5, "enemy king proximity rewarded");
        expect(delta(p,0,0,0) == 0, "king approach independently disabled");
        p.setSideToMove(BLUE);
        p.finalizeSetup();
        expect(delta(p,0,0,5) == -5, "scores use side-to-move team");
        p.setSideToMove(RED);
        p.placePiece(sq('k', 7),makePiece(RED,KNIGHT)); // Green king n7: dist 3
        p.finalizeSetup();
        expect(delta(p,0,0,5) == 10, "each knight's pressure contributes");
    }
    {
        auto p = kings();
        p.placePiece(sq('g', 7),makePiece(RED,KNIGHT));
        p.setAliveMask(0xEu); // Red eliminated.
        p.finalizeSetup();
        expect(delta(p) == 0, "eliminated friendly knight never receives bonus");
    }

    expect(Tuning::setEvalParameter("EvalKnightDevelopmentBonus", 0), "development 0 accepted");
    expect(Tuning::setEvalParameter("EvalKnightEnemyPressureBonus", 20), "pressure 20 accepted");
    expect(Tuning::setEvalParameter("EvalKnightKingApproachBonus", 50), "approach 50 accepted");
    expect(!Tuning::setEvalParameter("EvalKnightDevelopmentBonus", -1), "negative rejected");
    expect(!Tuning::setEvalParameter("EvalKnightEnemyPressureBonus", 51), "large pressure rejected");
    expect(!Tuning::setEvalParameter("EvalKnightKingApproachBonus", 51), "large approach rejected");
    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream out;
    expect(Tuning::handleSetOption("name EvalKnightDevelopmentBonus value 8", *engine, out),
           "UCI development setting recognized");
    expect(Tuning::eval().knightDevelopmentBonus == 8, "UCI value applied");
    Tuning::printUciOptions(out);
    Tuning::printCurrent(out);
    expect(out.str().find("option name EvalKnightDevelopmentBonus type spin default "
          + std::to_string(defaults.knightDevelopmentBonus) + " min 0 max 50") != std::string::npos,
           "option advertised");
    expect(out.str().find("EvalKnightEnemyPressureBonus=20") != std::string::npos,
           "current enemy proximity value shown");
    Tuning::resetEval();
    expect(Tuning::eval().knightDevelopmentBonus == defaults.knightDevelopmentBonus, "reset development");
    expect(Tuning::eval().knightEnemyPressureBonus == defaults.knightEnemyPressureBonus, "reset pressure");
    expect(Tuning::eval().knightKingApproachBonus == defaults.knightKingApproachBonus, "reset king approach");

    // Precomputed radius-two masks should match the full 14x14 geometry.
    for (unsigned s = 0; s < SQUARE_NB; ++s) {
        Bitboard expected{};
        const int sf = Geometry::fileOf(static_cast<Square>(s));
        const int sr = Geometry::rankOf(static_cast<Square>(s));
        for (unsigned t = 0; t < SQUARE_NB; ++t) {
            const int df = std::abs(sf - int(Geometry::fileOf(static_cast<Square>(t))));
            const int dr = std::abs(sr - int(Geometry::rankOf(static_cast<Square>(t))));
            if (std::max(df, dr) <= 2 && (df || dr))
                expected.set(static_cast<Square>(t));
        }
        expect(expected == Geometry::KnightEnemyProximity[s], "knight proximity masks cover radius 2");
    }

    if (!failures) std::cout << "Knight development and enemy activity: PASS\n";
    return failures ? 1 : 0;
}
