#include "ironphoenix/eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/nnue.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

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

        int kingPressureForColor(const Position& pos, Color c) noexcept {
            const Bitboard occ = pos.occupancy();
            const auto& params = Tuning::eval();
            const int ourTeam = teamOf(c);

            int score = 0;

            // In Teams, each player contributes pressure against both enemy
            // kings. Summing playerScore() for the two partners naturally
            // combines their attacks into one team score.
            for (unsigned enemyIndex = 0; enemyIndex < COLOR_NB; ++enemyIndex) {
                const Color enemy = static_cast<Color>(enemyIndex);
                if (teamOf(enemy) == ourTeam)
                    continue;

                const Square king = pos.kingSquare(enemy);
                if (king == SQ_NONE)
                    continue;

                const Bitboard kingRing = Geometry::KingAttacks[king];

                for (unsigned ptIndex = PAWN; ptIndex <= QUEEN; ++ptIndex) {
                    const PieceType pt = static_cast<PieceType>(ptIndex);
                    const int weight = kingPressureWeight(pt);
                    Bitboard pieces = pos.pieces(c, pt);

                    while (pieces) {
                        const Square sq = popLsb(pieces);
                        const Bitboard attacks = attacksFrom(c, pt, sq, occ);

                        // Every controlled escape/adjacent square adds pressure.
                        // Multiple attackers deliberately stack: coordinated
                        // attacks from partners are especially important in 4PC.
                        score += (attacks & kingRing).popcount() * weight;

                        // A direct attack on the king is much more urgent in
                        // IronPhoenix because an enemy king capture is terminal.
                        if (attacks.test(king))
                            score += params.kingPressureCheckBonus;
                    }
                }
            }

            return score;
        }

        int playerScore(const Position& pos, Color c) noexcept {
            return materialForColor(pos, c)
                + mobilityForColor(pos, c)
                + kingPressureForColor(pos, c);
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
