#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/tuning.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"
#include <iostream>
#include <memory>
#include <sstream>

using namespace ironphoenix;
namespace {
int failures = 0;
void expect(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }
Position base() {
    Position pos;
    pos.clear();
    pos.placePiece(sq('g', 1), makePiece(RED, KING));
    pos.placePiece(sq('a', 7), makePiece(BLUE, KING));
    pos.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    pos.placePiece(sq('n', 7), makePiece(GREEN, KING));
    return pos;
}
void check(Position pos, int teamDelta, const char* message) {
    for (unsigned c = 0; c < COLOR_NB; ++c) {
        pos.setSideToMove(static_cast<Color>(c));
        pos.finalizeSetup();
        const Key key = pos.key();
        Tuning::eval().bishopPairBonus = 0;
        const int without = Eval::evaluate(pos);
        Tuning::eval().bishopPairBonus = 10;
        const int with = Eval::evaluate(pos);
        expect(with - without == (teamOf(static_cast<Color>(c)) == 0 ? teamDelta : -teamDelta), message);
        expect(pos.key() == key && pos.verify(), "evaluation leaves position intact");
    }
}
}
int main() {
    Geometry::init();
    NNUE::unloadNetwork();
    Tuning::resetAll();
    expect(Tuning::eval().bishopPairBonus == 10, "default bonus is 10 cp");
    // The masks must cover every playable square exactly once.
    const Bitboard dark = Geometry::SquareColorMasks[0];
    const Bitboard light = Geometry::SquareColorMasks[1];
    expect((dark & light).empty(), "square-color masks are disjoint");
    expect((dark | light) == Geometry::BoardMask, "square-color masks cover board");
    expect(dark.popcount() == 80 && light.popcount() == 80,
           "cross board has 80 squares of each color");
    expect(dark.test(sq('g', 4)) != dark.test(sq('g', 5)),
           "adjacent rank switches bishop square color");
    expect(dark.test(sq('g', 4)) == dark.test(sq('i', 4)),
           "two-file step does not switch square color");
    check(base(), 0, "no bishops");
    for (unsigned team = 0; team < 2; ++team) {
        const Color first = static_cast<Color>(team);
        const Color partner = static_cast<Color>(team + 2);
        const int bonus = team == 0 ? 10 : -10;
        auto pos = base();
        pos.placePiece(sq('g', 4), makePiece(first, BISHOP));
        check(pos, 0, "single bishop");
        pos.placePiece(sq('g', 5), makePiece(partner, BISHOP));
        check(pos, bonus, "partners cover opposite colors, including adjacent ranks");
        pos.erasePiece(sq('g', 5));
        pos.placePiece(sq('i', 4), makePiece(partner, BISHOP));
        check(pos, 0, "partners with only one square color");
        pos.placePiece(sq('k', 4), makePiece(first, BISHOP));
        check(pos, 0, "three same-colored bishops still not a pair");
        pos.placePiece(sq('h', 4), makePiece(first, BISHOP));
        check(pos, bonus, "extra bishops do not multiply the team bonus");
        pos = base();
        pos.placePiece(sq('g', 4), makePiece(partner, BISHOP));
        pos.placePiece(sq('g', 5), makePiece(partner, BISHOP));
        check(pos, bonus, "one player alone supplies both square colors");
    }
    // Eliminated partners do not supply a useful complementary bishop.
    auto eliminated = base();
    eliminated.placePiece(sq('g', 4), makePiece(RED, BISHOP));
    eliminated.placePiece(sq('g', 5), makePiece(YELLOW, BISHOP));
    eliminated.setAliveMask(0xBu); // Red, Blue, Green alive; Yellow eliminated.
    // Avoid invoking verify() on artificial positions with residual pieces
    // from an eliminated seat: compare the evaluated delta only.
    eliminated.setSideToMove(RED);
    eliminated.finalizeSetup();
    Tuning::eval().bishopPairBonus = 0;
    const int withoutEliminated = Eval::evaluate(eliminated);
    Tuning::eval().bishopPairBonus = 10;
    expect(Eval::evaluate(eliminated) == withoutEliminated,
           "an eliminated partner cannot supply the missing bishop color");

    auto both = base();
    both.placePiece(sq('g', 4), makePiece(RED, BISHOP));
    both.placePiece(sq('g', 5), makePiece(YELLOW, BISHOP));
    both.placePiece(sq('i', 4), makePiece(BLUE, BISHOP));
    both.placePiece(sq('i', 5), makePiece(GREEN, BISHOP));
    check(both, 0, "equal team pairs cancel");
    expect(Tuning::setEvalParameter("EvalBishopPairBonus", 73), "bonus is tunable");
    expect(Tuning::eval().bishopPairBonus == 73, "tuned value applied");
    expect(!Tuning::setEvalParameter("EvalBishopPairBonus", -1), "negative bonus rejected");
    expect(!Tuning::setEvalParameter("EvalBishopPairBonus", 1001), "oversized bonus rejected");
    auto engine = std::make_unique<SearchEngine>();
    std::ostringstream output;
    expect(Tuning::handleSetOption("name EvalBishopPairBonus value 55", *engine, output), "UCI setter recognized");
    expect(Tuning::eval().bishopPairBonus == 55, "UCI setter updates bonus");
    Tuning::printUciOptions(output);
    Tuning::printCurrent(output);
    expect(output.str().find("option name EvalBishopPairBonus type spin default 10 min 0 max 1000") != std::string::npos,
           "UCI option advertised");
    expect(output.str().find("EvalBishopPairBonus=55") != std::string::npos, "current tuning reports bonus");
    Tuning::resetEval();
    expect(Tuning::eval().bishopPairBonus == 10, "reset restores initial bonus");
    if (!failures) std::cout << "Team bishop-pair evaluation: PASS\n";
    return failures ? 1 : 0;
}
