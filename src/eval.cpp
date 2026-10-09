#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>

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

        IRONPHOENIX_FORCE_INLINE int kingPressureWeight(PieceType pt) noexcept {
            const auto& params = Tuning::eval();

            switch (pt) {
            case PAWN:   return params.kingPressurePawn;
            case KNIGHT: return params.kingPressureKnight;
            case BISHOP: return params.kingPressureBishop;
            case ROOK:   return params.kingPressureRook;
            case QUEEN:  return params.kingPressureQueen;
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

        int kingPressureForTeam(const Position& pos, int attackingTeam) noexcept {
            const Bitboard occ = pos.occupancy();
            const auto& params = Tuning::eval();

            int score = 0;

            for (unsigned enemyIndex = 0; enemyIndex < COLOR_NB; ++enemyIndex) {
                const Color enemy = static_cast<Color>(enemyIndex);
                if (teamOf(enemy) == attackingTeam)
                    continue;

                const Square king = pos.kingSquare(enemy);
                if (king == SQ_NONE)
                    continue;

                const Bitboard kingRing = Geometry::KingAttacks[king];
                Bitboard coveredRing{};
                int attackerCount = 0;
                int attackUnits = 0;
                int directAttackers = 0;

                // Evaluate the two partners together. This prevents the same
                // king-ring square from being rewarded twice just because both
                // partners attack it, while still rewarding real coordination.
                for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                    const Color c = static_cast<Color>(ci);
                    if (teamOf(c) != attackingTeam)
                        continue;

                    for (unsigned ptIndex = PAWN; ptIndex <= QUEEN; ++ptIndex) {
                        const PieceType pt = static_cast<PieceType>(ptIndex);
                        Bitboard pieces = pos.pieces(c, pt);

                        while (pieces) {
                            const Square sq = popLsb(pieces);
                            const Bitboard attacks = attacksFrom(c, pt, sq, occ);
                            const Bitboard ringHits = attacks & kingRing;
                            const bool directAttack = attacks.test(king);

                            if (!ringHits && !directAttack)
                                continue;

                            ++attackerCount;
                            attackUnits += kingPressureWeight(pt);
                            coveredRing |= ringHits;

                            if (directAttack)
                                ++directAttackers;
                        }
                    }
                }

                // A single piece hovering near the king is usually tactical
                // noise that search can resolve. Require coordinated pressure
                // before awarding the positional king-zone term.
                if (attackerCount >= 2) {
                    const int coverage = coveredRing.popcount();
                    const int coordinatedAttackers = std::min(attackerCount, 4);

                    score += attackUnits;
                    score += coverage * 2;
                    score += (coordinatedAttackers - 1) * 2;
                }

                // Direct king attacks still matter because king capture is
                // terminal in 4PC, but cap repeated bonuses so the evaluation
                // does not drown out material and mobility.
                if (directAttackers > 0)
                    score += std::min(directAttackers, 2) * params.kingPressureCheckBonus;
            }

            return score;
        }

        int playerScore(const Position& pos, Color c) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c);
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

            team0 += kingPressureForTeam(pos, 0);
            team1 += kingPressureForTeam(pos, 1);

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
