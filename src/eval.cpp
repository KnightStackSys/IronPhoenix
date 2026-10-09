#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"

#include <array>

namespace ironphoenix::Eval {

    namespace {

        constexpr std::array<int, PIECE_TYPE_NB> EVAL_VALUE = {
            0, 100, 300, 400, 500, 1000, 0
        };

        constexpr std::array<int, PIECE_TYPE_NB> MOBILITY_WEIGHT = {
            0, 0, 4, 4, 2, 1, 0
        };

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        int materialForColor(const Position& pos, Color c) noexcept {
            int material = 0;

            for (unsigned pt = PAWN; pt <= QUEEN; ++pt) {
                material += pos.pieces(c, static_cast<PieceType>(pt)).popcount()
                    * EVAL_VALUE[pt];
            }

            return material;
        }

        int mobilityForColor(const Position& pos, Color c) noexcept {
            const Bitboard occ = pos.occupancy();
            const Bitboard blockedByTeam = teamOccupancy(pos, c);

            int score = 0;

            auto scorePieces = [&](PieceType pt) {
                Bitboard pieces = pos.pieces(c, pt);

                while (pieces) {
                    const Square sq = popLsb(pieces);
                    Bitboard attacks{};

                    switch (pt) {
                    case KNIGHT:
                        attacks = Geometry::KnightAttacks[sq];
                        break;
                    case BISHOP:
                        attacks = Geometry::bishopAttacks(sq, occ);
                        break;
                    case ROOK:
                        attacks = Geometry::rookAttacks(sq, occ);
                        break;
                    case QUEEN:
                        attacks = Geometry::queenAttacks(sq, occ);
                        break;
                    default:
                        break;
                    }

                    const int mobility = (attacks & ~blockedByTeam).popcount();
                    score += mobility * MOBILITY_WEIGHT[static_cast<unsigned>(pt)];
                }
                };

            scorePieces(KNIGHT);
            scorePieces(BISHOP);
            scorePieces(ROOK);
            scorePieces(QUEEN);

            return score;
        }

        int playerScore(const Position& pos,
            Color c) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c);
        }

    }

    int evaluate(const Position& pos) noexcept {

        int team0 = 0;
        int team1 = 0;

        for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
            const Color c = static_cast<Color>(ci);
            const int score = playerScore(pos, c);

            if (teamOf(c) == 0)
                team0 += score;
            else
                team1 += score;
        }

        const int score = team0 - team1;

        return teamOf(pos.sideToMove()) == 0 ? score : -score;
    }

}
