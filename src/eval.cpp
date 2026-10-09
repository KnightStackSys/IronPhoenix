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
            const Color sideToMove = pos.sideToMove();

            int score = 0;

            for (unsigned enemyIndex = 0; enemyIndex < COLOR_NB; ++enemyIndex) {
                const Color enemy = static_cast<Color>(enemyIndex);
                if (teamOf(enemy) == attackingTeam)
                    continue;

                const Square king = pos.kingSquare(enemy);
                if (king == SQ_NONE)
                    continue;

                // Only score king-ring squares the defending king could at least
                // potentially use. Friendly pieces around the defending king are
                // shields, not escape squares, so do not reward pseudo-pressure
                // against those occupied squares.
                const Bitboard kingZone = Geometry::KingAttacks[king]
                    & ~teamOccupancy(pos, enemy);

                Bitboard coveredZone{};
                int attackerCount = 0;
                int attackUnits = 0;
                bool immediateDirectAttack = false;

                // Evaluate the two partners together so overlapping pressure is
                // counted once. Positional king pressure may come from either
                // partner, but a direct king attack is only immediately urgent
                // when it belongs to the actual side to move.
                for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                    const Color c = static_cast<Color>(ci);
                    if (teamOf(c) != attackingTeam)
                        continue;

                    for (unsigned ptIndex = PAWN; ptIndex <= QUEEN; ++ptIndex) {
                        const PieceType pt = static_cast<PieceType>(ptIndex);
                        const int weight = kingPressureWeight(pt);
                        Bitboard pieces = pos.pieces(c, pt);

                        while (pieces) {
                            const Square sq = popLsb(pieces);
                            const Bitboard attacks = attacksFrom(c, pt, sq, occ);
                            const Bitboard zoneHits = attacks & kingZone;
                            const bool directAttack = attacks.test(king);

                            // A zero weight cleanly disables this piece type from
                            // the positional king-pressure term while preserving
                            // genuinely immediate king attacks.
                            if (weight != 0 && zoneHits) {
                                ++attackerCount;
                                attackUnits += weight;
                                coveredZone |= zoneHits;
                            }

                            if (c == sideToMove && directAttack)
                                immediateDirectAttack = true;
                        }
                    }
                }

                const int coverage = coveredZone.popcount();

                // Require both multiple attackers and multiple useful king-zone
                // squares. This filters out single-ray and one-square pressure
                // that search already handles well.
                if (attackerCount >= 2 && coverage >= 2) {
                    const int coordinatedAttackers = std::min(attackerCount, 4);

                    score += attackUnits;
                    score += coverage;
                    score += coordinatedAttackers - 1;
                }

                // King capture is terminal, but extra checking pieces do not make
                // one available king capture more terminal. Award this once per
                // enemy king and only to the color that can move right now.
                if (immediateDirectAttack)
                    score += params.kingPressureCheckBonus;
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
