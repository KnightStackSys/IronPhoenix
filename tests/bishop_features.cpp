#include "ironphoenix/eval.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <iostream>
#include <algorithm>
#include <cstdlib>

using namespace ironphoenix;

namespace {
int failures = 0;
void expect(bool yes, const char* why) {
    if (!yes) { ++failures; std::cerr << "FAIL: " << why << '\n'; }
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
int contribution(Position pos, int Tuning::EvalParameters::*field, int amount) {
    const Tuning::EvalParameters before = Tuning::eval();
    Tuning::eval().*field = 0;
    const int zero = Eval::evaluate(pos);
    Tuning::eval().*field = amount;
    const int enabled = Eval::evaluate(pos);
    Tuning::eval() = before;
    return enabled - zero;
}
} // namespace

int main() {
    Geometry::init();
    expect(!NNUE::loaded(), "bishop feature tests require HCE");
    Tuning::resetEval();
    using P = Tuning::EvalParameters;
    const P tunedDefaults{};

    struct Weight { const char* name; int P::*field; };
    const Weight weights[] = {
        {"EvalBadBishopPenalty", &P::badBishopPenalty},
        {"EvalBishopPairBonus", &P::bishopPairBonus},
        {"EvalBishopVsKnightBonus", &P::bishopVsKnightBonus},
        {"EvalBishopColorWeaknessPenalty", &P::bishopColorWeaknessPenalty},
        {"EvalBishopFianchettoBonus", &P::bishopFianchettoBonus},
        {"EvalBishopReturningBonus", &P::bishopReturningBonus},
    };
    for (const auto& w : weights) {
        expect(Tuning::eval().*(w.field) == tunedDefaults.*(w.field),
               "feature starts at the current SPSA-tuned default");
        expect(Tuning::setEvalParameter(w.name, 25), "UCI accepts bishop feature");
        expect(Tuning::eval().*(w.field) == 25, "value stored");
        expect(!Tuning::setEvalParameter(w.name, -1), "negative weight rejected");
        expect(!Tuning::setEvalParameter(w.name, 101), "overflow weight rejected");
        expect(Tuning::eval().*(w.field) == 25, "invalid values do not modify setting");
    }
    Tuning::resetEval();
    for (const auto& w : weights)
        expect(Tuning::eval().*(w.field) == tunedDefaults.*(w.field),
               "reset restores current tuned bishop default");

    // Verify that the precomputed 4PC bad-bishop masks exactly match the
    // old pawn-loop geometry on every valid square and all four orientations.
    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color c = static_cast<Color>(ci);
        for (unsigned bi = 0; bi < SQUARE_NB; ++bi) {
            const Square bishop = static_cast<Square>(bi);
            const int bf = Geometry::fileOf(bishop), br = Geometry::rankOf(bishop);
            for (unsigned pi = 0; pi < SQUARE_NB; ++pi) {
                const Square pawn = static_cast<Square>(pi);
                const int pf = Geometry::fileOf(pawn), pr = Geometry::rankOf(pawn);
                int ahead = 0, across = 0;
                switch (c) {
                case RED:    ahead = pr - br; across = pf - bf; break;
                case BLUE:   ahead = pf - bf; across = pr - br; break;
                case YELLOW: ahead = br - pr; across = pf - bf; break;
                case GREEN:  ahead = bf - pf; across = pr - br; break;
                }
                const bool expected = ahead > 0 && ahead <= 4 && std::abs(across) <= 4
                    && (((bf + br) & 1) == ((pf + pr) & 1));
                expect(Geometry::BishopBadPawnMask[ci][bi].test(pawn) == expected,
                       "precomputed bishop/pawn mask matches original geometry");
            }
        }
    }

    {
        auto pos = kings();
        pos.placePiece(sq('e', 5), makePiece(RED, BISHOP)); // light
        pos.placePiece(sq('f', 5), makePiece(YELLOW, BISHOP)); // dark
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopPairBonus, 20) == 20,
               "cross-partner complementary bishops form one team pair");
        pos.erasePiece(sq('f', 5));
        pos.placePiece(sq('f', 6), makePiece(YELLOW, BISHOP)); // same color
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopPairBonus, 20) == 0,
               "two same-colored bishops do not count as a pair");
        pos.setSideToMove(BLUE);
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopPairBonus, 20) == 0,
               "team pair is independent of side to move");
    }
    {
        auto pos = kings();
        pos.placePiece(sq('g', 3), makePiece(RED, BISHOP));
        pos.placePiece(sq('f', 4), makePiece(RED, PAWN));
        pos.placePiece(sq('h', 4), makePiece(RED, PAWN));
        pos.placePiece(sq('e', 5), makePiece(RED, PAWN));
        pos.finalizeSetup();
        expect(contribution(pos, &P::badBishopPenalty, 16) == -12,
               "same-color forward pawn chain penalizes bishop");
        pos.erasePiece(sq('g', 3));
        pos.placePiece(sq('g', 8), makePiece(RED, BISHOP));
        pos.finalizeSetup();
        expect(contribution(pos, &P::badBishopPenalty, 16) == 0,
               "bishop outside pawn chain is not penalized");
    }
    {
        auto pos = kings();
        pos.placePiece(sq('e', 5), makePiece(RED, BISHOP));
        pos.placePiece(sq('j', 7), makePiece(BLUE, KNIGHT));
        pos.finalizeSetup();
        const int open = contribution(pos, &P::bishopVsKnightBonus, 20);
        expect(open == 10, "open board favors bishop against enemy knight");
        pos.placePiece(sq('d', 5), makePiece(RED, PAWN));
        pos.placePiece(sq('f', 5), makePiece(YELLOW, PAWN));
        pos.placePiece(sq('j', 5), makePiece(BLUE, PAWN));
        pos.placePiece(sq('k', 5), makePiece(GREEN, PAWN));
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopVsKnightBonus, 20) < open,
               "pawn density weakens open-position bishop bonus");
    }
    {
        auto pos = kings();
        pos.placePiece(sq('e', 3), makePiece(BLUE, BISHOP));
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopColorWeaknessPenalty, 11) < 0,
               "missing matching team bishop exposes attacked king-zone squares");
        pos.placePiece(sq('d', 4), makePiece(YELLOW, BISHOP));
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopColorWeaknessPenalty, 11) == 0,
               "partner's matching bishop covers team color weakness");
    }
    {
        struct Pattern {
            Color c; char bishopFile; int bishopRank;
            char pawnFile; int pawnRank; char supportFile; int supportRank;
        };
        const Pattern patterns[] = {
            {RED, 'i', 2, 'i', 3, 'j', 2},
            {BLUE, 'b', 9, 'c', 9, 'b', 10},
            {YELLOW, 'i', 13, 'i', 12, 'j', 13},
            {GREEN, 'm', 9, 'l', 9, 'm', 10},
        };
        for (const auto& p : patterns) {
            auto pos = kings();
            pos.placePiece(sq(p.bishopFile, p.bishopRank), makePiece(p.c, BISHOP));
            pos.placePiece(sq(p.pawnFile, p.pawnRank), makePiece(p.c, PAWN));
            pos.placePiece(sq(p.supportFile, p.supportRank), makePiece(p.c, PAWN));
            pos.setSideToMove(p.c);
            pos.finalizeSetup();
            expect(contribution(pos, &P::bishopFianchettoBonus, 9) == 9,
                   "fianchetto recognizes four rotated player home edges");
            pos.erasePiece(sq(p.supportFile, p.supportRank));
            pos.finalizeSetup();
            expect(contribution(pos, &P::bishopFianchettoBonus, 9) == 0,
                   "missing supporting pawn voids fianchetto bonus");
        }
    }
    {
        auto pos = kings();
        pos.erasePiece(sq('g', 1));
        pos.placePiece(sq('i', 1), makePiece(RED, KING));
        pos.placePiece(sq('h', 1), makePiece(RED, ROOK));
        pos.placePiece(sq('f', 1), makePiece(RED, BISHOP));
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopReturningBonus, 6) == 0,
               "without castling geometry no return bonus is invented");
        pos.configureCastle(RED, 0,
            CastleLane{sq('g', 1), sq('d', 1), sq('i', 1), sq('h', 1), 0});
        expect(contribution(pos, &P::bishopReturningBonus, 6) == 6,
               "castled king and rook with backrank bishop satisfy proxy");
        pos.placePiece(sq('f', 2), makePiece(RED, PAWN));
        pos.finalizeSetup();
        expect(contribution(pos, &P::bishopReturningBonus, 6) == 0,
               "blocked home bishop has no return bonus");
    }

    Tuning::resetEval();
    if (!failures) std::cout << "4PC Teams bishop evaluation tests: PASS\n";
    return failures ? 1 : 0;
}
