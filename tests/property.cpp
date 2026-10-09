#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>

using namespace ironphoenix;

static Bitboard naiveRay(Square s, Direction d, Bitboard occ) {
    Bitboard out{};
    Square cur = Geometry::step(s, d);
    while (cur != SQ_NONE) {
        out.set(cur);
        if (occ.test(cur))
            break;
        cur = Geometry::step(cur, d);
    }
    return out;
}

int main() {
    Geometry::init();
    std::mt19937_64 rng(0x4E45585553425043ULL);

    for (int iter = 0; iter < 1500; ++iter) {
        Bitboard occ{};
        for (unsigned s = 0; s < SQUARE_NB; ++s)
            if ((rng() & 7ULL) < 2ULL)
                occ.set(static_cast<Square>(s));

        for (unsigned s = 0; s < SQUARE_NB; ++s) {
            for (unsigned d = 0; d < 8; ++d) {
                const auto dir = static_cast<Direction>(d);
                assert(Geometry::rayAttack(static_cast<Square>(s), dir, occ)
                       == naiveRay(static_cast<Square>(s), dir, occ));
            }
        }
    }

    for (unsigned c = 0; c < COLOR_NB; ++c)
        for (unsigned from = 0; from < SQUARE_NB; ++from)
            for (unsigned to = 0; to < SQUARE_NB; ++to)
                assert(Geometry::PawnAttacks[c][from].test(static_cast<Square>(to))
                    == Geometry::PawnAttackers[c][to].test(static_cast<Square>(from)));

    for (int iter = 0; iter < 5000; ++iter) {
        Position p;
        p.clear();
        const Color stm = static_cast<Color>(rng() & 3ULL);

        std::array<bool, SQUARE_NB> used{};
        for (unsigned c = 0; c < COLOR_NB; ++c) {
            Square s;
            do s = static_cast<Square>(rng() % SQUARE_NB); while (used[s]);
            used[s] = true;
            p.placePiece(s, makePiece(static_cast<Color>(c), KING));
        }

        for (int n = 0; n < 20; ++n) {
            Square s;
            do s = static_cast<Square>(rng() % SQUARE_NB); while (used[s]);
            used[s] = true;
            const Color c = static_cast<Color>(rng() & 3ULL);
            const PieceType pt = static_cast<PieceType>(1 + (rng() % 5));
            p.placePiece(s, makePiece(c, pt));
        }
        p.setSideToMove(stm);
        p.setCastlingRights(static_cast<std::uint8_t>(rng() & 0xFFULL));
        p.setRulesetId(static_cast<std::uint8_t>(rng() & 0xFULL));
        p.finalizeSetup();
        assert(p.key() == p.recomputeKey());

        // Null move must advance exactly one 4PC turn and then restore the
        // position byte-for-byte from the searcher's point of view.
        const Key beforeNull = p.key();
        const Color beforeNullSide = p.sideToMove();
        const Bitboard beforeNullCheckers = p.checkers();
        StateInfo nullState;
        p.makeNullMove(nullState);
        assert(p.sideToMove() == nextColor(beforeNullSide));
        assert(p.key() == p.recomputeKey());
        p.undoNullMove(nullState);
        assert(p.sideToMove() == beforeNullSide);
        assert(p.checkers() == beforeNullCheckers);
        assert(p.key() == beforeNull);
        assert(p.key() == p.recomputeKey());
        assert(p.verify());

        Bitboard movers = p.occupancy(stm);
        if (!movers)
            continue;
        const Square from = lsb(movers);
        Square to = SQ_NONE;
        for (unsigned tries = 0; tries < 200; ++tries) {
            const Square s = static_cast<Square>(rng() % SQUARE_NB);
            if (p.pieceAt(s) == NO_PIECE) { to = s; break; }
        }
        if (to == SQ_NONE)
            continue;

        const Key before = p.key();
        StateInfo st;
        Move m(from, to);
        p.makeMove(m, st);
        assert(p.key() == p.recomputeKey());
        p.undoMove(m, st);
        assert(p.key() == before);
        assert(p.key() == p.recomputeKey());
        assert(p.verify());
    }

    std::cout << "iron_phoenix property tests: PASS\n";
}
