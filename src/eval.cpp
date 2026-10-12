#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <cstdlib>

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

        IRONPHOENIX_FORCE_INLINE int pawnAdvance(Color c, Square sq) noexcept {
            const int file = static_cast<int>(Geometry::fileOf(sq));
            const int rank = static_cast<int>(Geometry::rankOf(sq));

            int advance = 0;
            switch (c) {
            case RED:    advance = rank - 1; break;
            case BLUE:   advance = file - 1; break;
            case YELLOW: advance = 12 - rank; break;
            case GREEN:  advance = 12 - file; break;
            }

            return std::clamp(advance, 0, 9);
        }

        IRONPHOENIX_FORCE_INLINE int centralizationUnits(Square sq) noexcept {
            // The geometric center of the 14x14 board lies between coordinates
            // 6 and 7 on each axis. Work in doubled coordinates to avoid floats.
            const int file = static_cast<int>(Geometry::fileOf(sq));
            const int rank = static_cast<int>(Geometry::rankOf(sq));
            const int fileDistance = std::abs(file * 2 - 13);
            const int rankDistance = std::abs(rank * 2 - 13);
            return std::max(0, 12 - fileDistance - rankDistance);
        }

        int materialForColor(const Position& pos, Color c) noexcept {
            int material = 0;

            for (unsigned pt = PAWN; pt <= QUEEN; ++pt) {
                material += pos.pieces(c, static_cast<PieceType>(pt)).popcount()
                    * evalValue(static_cast<PieceType>(pt));
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
                    score += mobility * mobilityWeight(pt);
                }
            };

            scorePieces(KNIGHT);
            scorePieces(BISHOP);
            scorePieces(ROOK);
            scorePieces(QUEEN);

            return score;
        }

        int bishopPairForColor(const Position& pos, Color c) noexcept {
            const auto& params = Tuning::eval();
            return pos.pieces(c, BISHOP).popcount() >= 2
                ? params.bishopPairBonus
                : 0;
        }

        int pawnStructureForColor(const Position& pos, Color c) noexcept {
            const auto& params = Tuning::eval();
            const Bitboard allPawns = pos.pieces(c, PAWN);
            Bitboard pawns = allPawns;
            int score = 0;

            while (pawns) {
                const Square sq = popLsb(pawns);
                score += pawnAdvance(c, sq) * params.pawnAdvanceBonus;

                // A pawn supported by another pawn is harder to attack and forms
                // a more stable chain. PawnAttackers[c][sq] are the squares from
                // which one of c's pawns would attack sq.
                if (Geometry::PawnAttackers[c][sq] & allPawns)
                    score += params.supportedPawnBonus;
            }

            return score;
        }

        int kingSafetyForColor(const Position& pos, Color c) noexcept {
            if (!pos.isAlive(c))
                return 0;

            const auto& params = Tuning::eval();
            const Square king = pos.kingSquare(c);
            if (king == SQ_NONE)
                return 0;

            int score = 0;

            const int pawnShield = (Geometry::KingAttacks[king] & pos.pieces(c, PAWN)).popcount();
            score += pawnShield * params.kingPawnShieldBonus;

            if (pos.inCheck(c))
                score -= params.checkPenalty;

            return score;
        }

        int knightPlacementForColor(const Position& pos, Color c) noexcept {
            const auto& params = Tuning::eval();
            Bitboard knights = pos.pieces(c, KNIGHT);
            int score = 0;

            while (knights) {
                const Square sq = popLsb(knights);
                score += centralizationUnits(sq) * params.knightCentralization;
            }

            return score;
        }

        int playerScore(const Position& pos, Color c) noexcept {
            if (!pos.isAlive(c))
                return 0;

            return materialForColor(pos, c)
                + mobilityForColor(pos, c)
                + bishopPairForColor(pos, c)
                + pawnStructureForColor(pos, c)
                + kingSafetyForColor(pos, c)
                + knightPlacementForColor(pos, c);
        }

        int handcraftedEvaluate(const Position& pos) noexcept {
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
            const int relative = teamOf(pos.sideToMove()) == 0 ? score : -score;
            return relative + Tuning::eval().tempoBonus;
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
