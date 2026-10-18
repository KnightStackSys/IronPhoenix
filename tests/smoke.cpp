#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/see.hpp"

#include <cassert>
#include <iostream>

using namespace ironphoenix;

static Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}

static void putKings(Position& p) {
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
}

// Measure only the new queen term, holding material and mobility constant.
static int queenInfiltrationDelta(Color queenColor, char file, int rank, bool threatened = false) {
    Position p;
    p.clear();
    putKings(p);
    p.placePiece(sq(file, rank), makePiece(queenColor, QUEEN));
    if (threatened)
        p.placePiece(sq('d', 9), makePiece(BLUE, ROOK));

    p.setSideToMove(queenColor);
    p.finalizeSetup();

    auto& params = Eval::parameters();
    const int saved = params.queenInfiltration;
    params.queenInfiltration = 0;
    const int baseline = Eval::evaluate(p);
    params.queenInfiltration = saved;
    return Eval::evaluate(p) - baseline;
}

int main() {
    Geometry::init();

    int valid = 0;
    for (int r = 0; r < 14; ++r)
        for (int f = 0; f < 14; ++f)
            valid += Geometry::validXY(f, r) ? 1 : 0;
    assert(valid == 160);
    assert(Geometry::square(0, 0) == SQ_NONE);
    assert(sq('d', 1) != SQ_NONE);
    assert(sq('a', 4) != SQ_NONE);

    {
        Position p;
        p.clear();
        putKings(p);
        p.placePiece(sq('d', 1), makePiece(BLUE, ROOK));
        p.setSideToMove(RED);
        p.finalizeSetup();
        assert(p.inCheck());
        assert(p.inCheck(RED));
        assert(!p.inCheck(YELLOW));
    }

    {
        Position p;
        p.clear();
        putKings(p);
        p.placePiece(sq('d', 6), makePiece(RED, ROOK));
        p.setSideToMove(RED);
        p.finalizeSetup();
        Move m(sq('d', 6), sq('d', 7));
        assert(p.givesCheck(m));
    }

    {
        Position p;
        p.clear();
        putKings(p);
        p.placePiece(sq('g', 2), makePiece(RED, PAWN));
        p.setSideToMove(RED);
        p.finalizeSetup();
        const Key before = p.key();

        StateInfo st;
        Move push(sq('g', 2), sq('g', 4), NO_PIECE, PROMO_NONE, MF_DOUBLE_PUSH);
        p.makeMove(push, st);
        assert(p.enPassant(RED) == sq('g', 3));
        assert(p.sideToMove() == BLUE);
        assert(p.key() == p.recomputeKey());
        assert(p.verify());

        p.undoMove(push, st);
        assert(p.key() == before);
        assert(p.pieceAt(sq('g', 2)) == makePiece(RED, PAWN));
        assert(p.enPassant(RED) == SQ_NONE);
        assert(p.verify());
    }

    {
        Position p;
        p.clear();
        putKings(p);
        p.placePiece(sq('e', 6), makePiece(RED, KNIGHT));
        p.placePiece(sq('g', 7), makePiece(BLUE, PAWN));
        p.placePiece(sq('h', 8), makePiece(GREEN, PAWN));
        p.setSideToMove(RED);
        p.finalizeSetup();

        Move cap(sq('e', 6), sq('g', 7), makePiece(BLUE, PAWN));
        assert(see(p, cap) == 100);
        assert(seeGE(p, cap, 0));
        assert(seeGE(p, cap, 100));
        assert(!seeGE(p, cap, 101));
    }

    assert(Geometry::isPromotionSquare(RED, sq('g', 11)));
    assert(Geometry::isPromotionSquare(BLUE, sq('k', 7)));
    assert(Geometry::isPromotionSquare(YELLOW, sq('g', 4)));
    assert(Geometry::isPromotionSquare(GREEN, sq('d', 7)));

    // All four orientations must reward a safe queen penetrating an enemy
    // camp near the enemy king, from the perspective of the queen's team.
    assert(queenInfiltrationDelta(RED, 'd', 7) > 0);     // Blue home edge
    assert(queenInfiltrationDelta(YELLOW, 'k', 7) > 0);  // Green home edge
    assert(queenInfiltrationDelta(BLUE, 'g', 4) > 0);    // Red home edge
    assert(queenInfiltrationDelta(GREEN, 'g', 11) > 0);  // Yellow home edge

    // No reward for own/allied territory or an enemy-attacked queen.
    assert(queenInfiltrationDelta(RED, 'g', 4) == 0);
    assert(queenInfiltrationDelta(RED, 'g', 11) == 0);
    assert(queenInfiltrationDelta(RED, 'd', 7, true) == 0);
    assert(Tuning::setEvalParameter("EvalQueenInfiltration", 0));
    assert(queenInfiltrationDelta(RED, 'd', 7) == 0);
    Tuning::resetEval();

    std::cout << "nexus_fast_board smoke tests: PASS\n";
    return 0;
}
