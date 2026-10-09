#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
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

        IRONPHOENIX_FORCE_INLINE int infiltrationWeight(PieceType pt) noexcept {
            const auto& params = Tuning::eval();

            switch (pt) {
            case KNIGHT: return params.infiltrationKnight;
            case BISHOP: return params.infiltrationBishop;
            case ROOK:   return params.infiltrationRook;
            case QUEEN:  return params.infiltrationQueen;
            default:     return 0;
            }
        }

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        IRONPHOENIX_FORCE_INLINE Bitboard attacksFrom(
            Color c,
            PieceType pt,
            Square sq,
            Bitboard occ) noexcept {
            switch (pt) {
            case PAWN:
                return Geometry::PawnAttacks[c][sq];
            case KNIGHT:
                return Geometry::KnightAttacks[sq];
            case BISHOP:
                return Geometry::bishopAttacks(sq, occ);
            case ROOK:
                return Geometry::rookAttacks(sq, occ);
            case QUEEN:
                return Geometry::queenAttacks(sq, occ);
            default:
                return Bitboard{};
            }
        }

        // Normalize a square into forward progress for each seat. The pawn home
        // line is progress 0 and the promotion line is progress 9 for all colors.
        IRONPHOENIX_FORCE_INLINE int forwardProgress(Color c, Square sq) noexcept {
            const int file = static_cast<int>(Geometry::fileOf(sq));
            const int rank = static_cast<int>(Geometry::rankOf(sq));

            switch (c) {
            case RED:    return rank - 1;
            case BLUE:   return file - 1;
            case YELLOW: return 12 - rank;
            case GREEN:  return 12 - file;
            }
            return 0;
        }

        const Bitboard& spaceMask(Color c) noexcept {
            // Build the four directional masks once, after Geometry::init() has
            // initialized FileOf/RankOf. Progress 4..9 represents useful forward
            // space from the middle of the board through the promotion line.
            static const std::array<Bitboard, COLOR_NB> masks = [] {
                std::array<Bitboard, COLOR_NB> result{};

                for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                    const Color color = static_cast<Color>(ci);
                    for (unsigned si = 0; si < SQUARE_NB; ++si) {
                        const Square sq = static_cast<Square>(si);
                        const int progress = forwardProgress(color, sq);
                        if (progress >= 4 && progress <= 9)
                            result[ci].set(sq);
                    }
                }

                return result;
            }();

            return masks[static_cast<unsigned>(c)];
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

        int spaceAndInfiltrationForColor(const Position& pos, Color c) noexcept {
            const Bitboard occ = pos.occupancy();
            const auto& params = Tuning::eval();

            // Space is unique control of empty squares in the player's forward
            // half. Using a union keeps several pieces attacking the same square
            // from artificially inflating the score.
            Bitboard controlled{};
            for (unsigned ptIndex = PAWN; ptIndex <= QUEEN; ++ptIndex) {
                const PieceType pt = static_cast<PieceType>(ptIndex);
                Bitboard pieces = pos.pieces(c, pt);

                while (pieces) {
                    const Square sq = popLsb(pieces);
                    controlled |= attacksFrom(c, pt, sq, occ);
                }
            }

            controlled &= spaceMask(c);
            controlled &= ~occ;

            int score = controlled.popcount() * params.spaceControl;

            // Infiltration rewards minor/major pieces that physically penetrate
            // into the opponent-facing side of the board. Pawns are intentionally
            // excluded here because promotion potential belongs in a pawn term.
            for (unsigned ptIndex = KNIGHT; ptIndex <= QUEEN; ++ptIndex) {
                const PieceType pt = static_cast<PieceType>(ptIndex);
                const int weight = infiltrationWeight(pt);
                if (weight == 0)
                    continue;

                Bitboard pieces = pos.pieces(c, pt);
                while (pieces) {
                    const Square sq = popLsb(pieces);
                    const int progress = forwardProgress(c, sq);
                    if (progress < 6)
                        continue;

                    // Depth 1 begins at progress 6. Cap the depth component so a
                    // piece deep in an outer arm does not overwhelm the HCE.
                    const int depth = std::min(progress - 5, 6);
                    score += depth * weight;
                }
            }

            return score;
        }

        int playerScore(const Position& pos, Color c) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c)
                + spaceAndInfiltrationForColor(pos, c);
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
