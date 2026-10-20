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

        int mobilityForColor(const Position& pos, Color c, Bitboard& teamAttacks) noexcept {
            const Bitboard blockedByTeam = teamOccupancy(pos, c);
            const bool scoreThreats = Tuning::eval().attackingPiecesWeight != 0;

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
                    if (scoreThreats)
                        teamAttacks |= attacks;
                }
            };

            scorePieces(KNIGHT);
            scorePieces(BISHOP);
            scorePieces(ROOK);
            scorePieces(QUEEN);

            // Pawns' capture directions are threats, not mobility.
            if (scoreThreats) {
                Bitboard pawns = pos.pieces(c, PAWN);
                while (pawns)
                    teamAttacks |= Geometry::PawnAttacks[c][popLsb(pawns)];
            }
            return score;
        }

        int attackingPiecesForTeam(const Position& pos, int team, Bitboard attacks) noexcept {
            const int weight = Tuning::eval().attackingPiecesWeight;
            if (weight == 0)
                return 0;

            Bitboard pawns{}, minors{}, rooks{}, queens{};
            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color victim = static_cast<Color>(ci);
                if (teamOf(victim) == team || !pos.isAlive(victim))
                    continue;
                pawns |= pos.pieces(victim, PAWN);
                minors |= pos.pieces(victim, KNIGHT) | pos.pieces(victim, BISHOP);
                rooks |= pos.pieces(victim, ROOK);
                queens |= pos.pieces(victim, QUEEN);
            }

            // Per distinct enemy target across both teammates, not per attack.
            // Keep king threats out of this term: mate search handles kings.
            const int pressure = 4 * (attacks & pawns).popcount()
                + 10 * (attacks & minors).popcount()
                + 16 * (attacks & rooks).popcount()
                + 28 * (attacks & queens).popcount();
            return pressure * weight / 100;
        }

        // Counts the *potential* to give a knight check with the attacking
        // side's next knight move, not an already delivered knight check.
        // This is only a pseudo-legal geometric threat: pins, tempo and
        // whether the attacker will actually move are left to search.
        // Cap at one penalty per threatened king, including partner's king.
        IRONPHOENIX_FORCE_INLINE int imminentKnightCheckPenalty(
            const Position& pos, Color ally0, Color ally1,
            Color enemy0, Color enemy1) noexcept {
            const int penalty = Tuning::eval().knightCheckThreatPenalty;
            if (penalty <= 0)
                return 0;

            const Bitboard enemyKnights = (pos.isAlive(enemy0)
                    ? pos.pieces(enemy0, KNIGHT) : Bitboard{})
                | (pos.isAlive(enemy1)
                    ? pos.pieces(enemy1, KNIGHT) : Bitboard{});
            if (!enemyKnights)
                return 0;

            // Knights cannot jump onto squares occupied by their own team.
            const Bitboard blocked = pos.occupancy(enemy0) | pos.occupancy(enemy1);
            int threatenedKings = 0;

            for (Color kingOwner : {ally0, ally1}) {
                if (!pos.isAlive(kingOwner))
                    continue;
                const Square king = pos.kingSquare(kingOwner);
                if (king == SQ_NONE)
                    continue;

                Bitboard candidateKnights = enemyKnights & Geometry::KnightCheckApproach[king];
                if (!candidateKnights)
                    continue;

                const Bitboard destinations = Geometry::KnightAttacks[king] & ~blocked;
                if (!destinations)
                    continue;

                while (candidateKnights) {
                    const Square knight = popLsb(candidateKnights);
                    // Don't conflate an existing knight check with a
                    // possible checking move in the future.
                    if (Geometry::KnightAttacks[knight].test(king))
                        continue;
                    if (Geometry::KnightAttacks[knight] & destinations) {
                        ++threatenedKings;
                        break;
                    }
                }
            }
            return penalty * threatenedKings;
        }

        int handcraftedEvaluate(const Position& pos) noexcept {
            int team0 = 0, team1 = 0;
            Bitboard attacks[2]{};

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color c = static_cast<Color>(ci);
                const int score = materialForColor(pos, c)
                    + mobilityForColor(pos, c, attacks[teamOf(c)]);
                if (teamOf(c) == 0)
                    team0 += score;
                else
                    team1 += score;
            }

            team0 += attackingPiecesForTeam(pos, 0, attacks[0]);
            team1 += attackingPiecesForTeam(pos, 1, attacks[1]);

            // Each team pays for potential knight checks against either
            // surviving king. Team-relative scoring handles all four turns.
            team0 -= imminentKnightCheckPenalty(pos, RED, YELLOW, BLUE, GREEN);
            team1 -= imminentKnightCheckPenalty(pos, BLUE, GREEN, RED, YELLOW);

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
