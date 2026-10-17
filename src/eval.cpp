#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <array>

namespace ironphoenix::Eval {

    namespace {

        IRONPHOENIX_FORCE_INLINE int evalValue(PieceType pt) noexcept {
            const auto& params = Tuning::eval();

            switch (pt) {
            case PAWN:   return params.pawnValue;
            case KNIGHT: return params.knightValue;
            case BISHOP: return params.bishopValue;
            case ROOK:   return params.rookValue;
            case QUEEN:  return params.queenValue;
            default:     return 0;
            }
        }

        IRONPHOENIX_FORCE_INLINE int mobilityWeight(PieceType pt) noexcept {
            const auto& params = Tuning::eval();

            switch (pt) {
            case KNIGHT: return params.knightMobility;
            case BISHOP: return params.bishopMobility;
            case ROOK:   return params.rookMobility;
            case QUEEN:  return params.queenMobility;
            default:     return 0;
            }
        }

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        int materialForColor(const Position& pos, Color c) noexcept {
            int material = 0;

            for (unsigned pt = PAWN; pt <= QUEEN; ++pt) {
                material += pos.pieces(c, static_cast<PieceType>(pt)).popcount()
                    * evalValue(static_cast<PieceType>(pt));
            }

            return material;
        }

        int mobilityForColor(const Position& pos, Color c, Bitboard enemyPawnAttacks) noexcept {
            // Pawn-safe mobility is a positional approximation, not a legal
            // move count. Keep real occupancy for slider rays; unsafe squares
            // are excluded as destinations, not treated as extra blockers.
            const Bitboard mobilityArea = ~(teamOccupancy(pos, c) | enemyPawnAttacks);

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
                        attacks = pos.bishopAttacks(sq);
                        break;
                    case ROOK:
                        attacks = pos.rookAttacks(sq);
                        break;
                    case QUEEN:
                        attacks = pos.queenAttacks(sq);
                        break;
                    default:
                        break;
                    }

                    const int mobility = (attacks & mobilityArea).popcount();
                    score += mobility * mobilityWeight(pt);
                }
            };

            scorePieces(KNIGHT);
            scorePieces(BISHOP);
            scorePieces(ROOK);
            scorePieces(QUEEN);

            return score;
        }

        int playerScore(const Position& pos, Color c, Bitboard enemyPawnAttacks) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c, enemyPawnAttacks);
        }

        int handcraftedEvaluate(const Position& pos) noexcept {
            int team0 = 0;
            int team1 = 0;

            // Build each team's pawn-control map once, respecting all four
            // pawn orientations. Geometric attacks include pinned pawns.
            std::array<Bitboard, 2> pawnAttacksByTeam{};
            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color c = static_cast<Color>(ci);
                Bitboard pawns = pos.pieces(c, PAWN);
                while (pawns)
                    pawnAttacksByTeam[teamOf(c)] |= Geometry::PawnAttacks[c][popLsb(pawns)];
            }

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color c = static_cast<Color>(ci);
                const int score = playerScore(pos, c, pawnAttacksByTeam[teamOf(c) ^ 1]);

                if (teamOf(c) == 0)
                    team0 += score;
                else
                    team1 += score;
            }

            const int score = team0 - team1;
            return teamOf(pos.sideToMove()) == 0 ? score : -score;
        }

    }

    Parameters& parameters() noexcept {
        return Tuning::eval();
    }

    int evaluate(const Position& pos) noexcept {
        if (NNUE::loaded())
            return NNUE::evaluate(pos);

        return handcraftedEvaluate(pos);
    }

}
