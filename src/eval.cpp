#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>

namespace ironphoenix::Eval {

    namespace {

        constexpr int STARTING_NON_PAWN_MATERIAL = 13'600;

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

        IRONPHOENIX_FORCE_INLINE int developmentWeight(PieceType pt) noexcept {
            const auto& params = Tuning::eval();

            switch (pt) {
            case KNIGHT: return params.developmentKnight;
            case BISHOP: return params.developmentBishop;
            case ROOK:   return params.developmentRook;
            case QUEEN:  return params.developmentQueen;
            default:     return 0;
            }
        }

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        IRONPHOENIX_FORCE_INLINE bool onHomeBackLine(Color c, Square sq) noexcept {
            switch (c) {
            case RED:    return Geometry::rankOf(sq) == 0;
            case BLUE:   return Geometry::fileOf(sq) == 0;
            case YELLOW: return Geometry::rankOf(sq) == BOARD_RANKS - 1;
            case GREEN:  return Geometry::fileOf(sq) == BOARD_FILES - 1;
            }
            return false;
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

        int remainingNonPawnMaterial(const Position& pos) noexcept {
            int material = 0;

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color c = static_cast<Color>(ci);
                for (unsigned pt = KNIGHT; pt <= QUEEN; ++pt) {
                    const PieceType pieceType = static_cast<PieceType>(pt);
                    material += pos.pieces(c, pieceType).popcount() * pieceValue(pieceType);
                }
            }

            // Promotions can take the board above starting material. Development
            // should never grow beyond its full opening weight.
            return std::min(material, STARTING_NON_PAWN_MATERIAL);
        }

        int developmentForColor(const Position& pos, Color c, int phaseMaterial) noexcept {
            int score = 0;

            // Development is position based rather than move-history based: a
            // surviving piece earns its bonus while it is off the player's home
            // back line. A piece that is captured cannot continue earning it.
            for (unsigned pt = KNIGHT; pt <= QUEEN; ++pt) {
                const PieceType pieceType = static_cast<PieceType>(pt);
                const int weight = developmentWeight(pieceType);
                if (weight == 0)
                    continue;

                Bitboard pieces = pos.pieces(c, pieceType);
                while (pieces) {
                    const Square sq = popLsb(pieces);
                    if (!onHomeBackLine(c, sq))
                        score += weight;
                }
            }

            // Taper toward zero as non-pawn material disappears. This keeps the
            // term focused on opening development instead of rewarding arbitrary
            // piece placement in simplified middlegames and endgames.
            return score * phaseMaterial / STARTING_NON_PAWN_MATERIAL;
        }

        int playerScore(const Position& pos, Color c, int phaseMaterial) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c)
                + developmentForColor(pos, c, phaseMaterial);
        }

        int handcraftedEvaluate(const Position& pos) noexcept {
            int team0 = 0;
            int team1 = 0;
            const int phaseMaterial = remainingNonPawnMaterial(pos);

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color c = static_cast<Color>(ci);
                const int score = playerScore(pos, c, phaseMaterial);

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
