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

        int materialForColor(const Position& pos, Color c) noexcept {
            int material = 0;

            for (unsigned pt = PAWN; pt <= QUEEN; ++pt) {
                material += pos.pieces(c, static_cast<PieceType>(pt)).popcount()
                    * evalValue(static_cast<PieceType>(pt));
            }

            return material;
        }

        int mobilityForColor(const Position& pos, Color c) noexcept {
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

        // Distance past the board's central boundary towards an enemy home edge.
        // The four home edges rotate, so ordinary two-player rank advancement
        // cannot describe queen infiltration on a cross-shaped 4PC board.
        IRONPHOENIX_FORCE_INLINE int enemyCampDepth(Color enemyColor, int file, int rank) noexcept {
            switch (enemyColor) {
            case RED:    return 7 - rank;
            case BLUE:   return 7 - file;
            case YELLOW: return rank - 6;
            case GREEN:  return file - 6;
            }
            return 0;
        }

        IRONPHOENIX_FORCE_INLINE bool inHomeCamp(Color color, int file, int rank) noexcept {
            switch (color) {
            case RED:    return rank <= 2;
            case BLUE:   return file <= 2;
            case YELLOW: return rank >= 11;
            case GREEN:  return file >= 11;
            }
            return false;
        }

        int queenInfiltrationForColor(const Position& pos, Color c) noexcept {
            const int weight = Tuning::eval().queenInfiltration;
            if (weight == 0)
                return 0;

            int score = 0;
            Bitboard queens = pos.pieces(c, QUEEN);
            const Bitboard occupancy = pos.occupancy();

            while (queens) {
                const Square sq = popLsb(queens);
                const int file = static_cast<int>(Geometry::fileOf(sq));
                const int rank = static_cast<int>(Geometry::rankOf(sq));

                // Do not reward a queen that has not left its starting camp,
                // or one that the opposing team can capture immediately.
                if (inHomeCamp(c, file, rank)
                    || pos.isAttackedByEnemy(sq, c, occupancy))
                    continue;

                for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                    const Color opponent = static_cast<Color>(ci);
                    if (!enemy(c, opponent) || !pos.isAlive(opponent))
                        continue;

                    const Square king = pos.kingSquare(opponent);
                    if (king == SQ_NONE)
                        continue;

                    // A shallow central queen is not an infiltration.
                    const int depth = enemyCampDepth(opponent, file, rank);
                    if (depth < 2)
                        continue;

                    const int kingFile = static_cast<int>(Geometry::fileOf(king));
                    const int kingRank = static_cast<int>(Geometry::rankOf(king));
                    const int distance = std::max(std::abs(file - kingFile), std::abs(rank - kingRank));
                    if (distance > 6)
                        continue;

                    // More credit for penetrating deeper while staying close
                    // enough to an enemy king to create threats.
                    score += (depth - 1) * (7 - distance) * weight;
                }
            }

            return score;
        }

        int playerScore(const Position& pos, Color c) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c)
                + queenInfiltrationForColor(pos, c);
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
