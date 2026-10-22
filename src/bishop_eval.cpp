#include "ironphoenix/bishop_eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <cstdlib>

namespace ironphoenix::Eval {
namespace {

int squareColor(Square sq) noexcept {
    return (int(Geometry::fileOf(sq)) + int(Geometry::rankOf(sq))) & 1;
}

// R/Y advance vertically, B/G horizontally. Do not transplant 8x8 ranks.
int homeDepth(Color c, Square sq) noexcept {
    switch (c) {
    case RED:    return Geometry::rankOf(sq);
    case BLUE:   return Geometry::fileOf(sq);
    case YELLOW: return BOARD_RANKS - 1 - Geometry::rankOf(sq);
    case GREEN:  return BOARD_FILES - 1 - Geometry::fileOf(sq);
    }
    return 0;
}

int lateral(Color c, Square sq) noexcept {
    return (c == RED || c == YELLOW)
        ? Geometry::fileOf(sq) : Geometry::rankOf(sq);
}

Square homeSquare(Color c, int across, int depth) noexcept {
    if (across < 0 || across >= BOARD_FILES || depth < 0 || depth >= BOARD_RANKS)
        return SQ_NONE;
    int f = across, r = depth;
    switch (c) {
    case RED:    break;
    case BLUE:   f = depth; r = across; break;
    case YELLOW: r = BOARD_RANKS - 1 - depth; break;
    case GREEN:  f = BOARD_FILES - 1 - depth; r = across; break;
    }
    return Geometry::validXY(f, r) ? Geometry::square(f, r) : SQ_NONE;
}

bool pawnAt(const Position& pos, Color c, int across, int depth) noexcept {
    const Square sq = homeSquare(c, across, depth);
    return sq != SQ_NONE && pos.pieceAt(sq) == makePiece(c, PAWN);
}

bool isFianchetto(const Position& pos, Color c, Square bishop) noexcept {
    const Square king = pos.kingSquare(c);
    if (king == SQ_NONE || homeDepth(c, king) > 2 || homeDepth(c, bishop) != 1)
        return false;
    const int side = lateral(c, bishop);
    if (std::abs(side - lateral(c, king)) != 2)
        return false;
    // A displaced own knight-pawn and a neighboring own shielding pawn:
    // the exact squares rotate with each of the four home edges.
    if (!pawnAt(pos, c, side, 2) && !pawnAt(pos, c, side, 3))
        return false;
    return pawnAt(pos, c, side - 1, 1)
        || pawnAt(pos, c, side + 1, 1)
        || pawnAt(pos, c, side - 1, 2)
        || pawnAt(pos, c, side + 1, 2);
}

bool looksLikeReturningBishop(const Position& pos, Color c, Square bishop) noexcept {
    if (homeDepth(c, bishop) != 0)
        return false;
    // Standard 4PC has home bishops on the 6th and 9th board coordinates.
    // This is an intentionally conservative placement proxy, not history.
    const int across = lateral(c, bishop);
    if (across != 5 && across != 8)
        return false;
    const Square exit = homeSquare(c, across, 1);
    if (exit == SQ_NONE || pos.pieceAt(exit) != NO_PIECE)
        return false;
    const Square king = pos.kingSquare(c);
    for (unsigned lane = 0; lane < 2; ++lane) {
        const CastleLane& castle = pos.castleDefinition(c, lane);
        if (castle.valid() && king == castle.kingTo
            && pos.pieceAt(castle.rookTo) == makePiece(c, ROOK))
            return true;
    }
    return false;
}

bool protectedByTeamPawn(const Position& pos, int team, Square target) noexcept {
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const Color c = static_cast<Color>(i);
        if (teamOf(c) == team && pos.isAlive(c)
            && (Geometry::PawnAttackers[c][target] & pos.pieces(c, PAWN)))
            return true;
    }
    return false;
}

} // namespace

int bishopFeaturesForTeam(const Position& pos, int team) noexcept {
    const auto& tune = Tuning::eval();
    if (tune.badBishopPenalty == 0 && tune.bishopPairBonus == 0
        && tune.bishopVsKnightBonus == 0 && tune.bishopColorWeaknessPenalty == 0
        && tune.bishopFianchettoBonus == 0 && tune.bishopReturningBonus == 0)
        return 0;

    Bitboard ownBishops{}, enemyBishops{}, ownPawns{};
    int ownBishopColors[2] = {0, 0};
    int enemyKnights = 0, allPawns = 0;
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const Color c = static_cast<Color>(i);
        if (!pos.isAlive(c))
            continue;
        const Bitboard bishops = pos.pieces(c, BISHOP);
        allPawns += pos.pieces(c, PAWN).popcount();
        if (teamOf(c) == team) {
            ownBishops |= bishops;
            ownPawns |= pos.pieces(c, PAWN);
        } else {
            enemyBishops |= bishops;
            enemyKnights += pos.pieces(c, KNIGHT).popcount();
        }
    }

    // Teammates can provide complementary light/dark-square coverage,
    // including promoted bishops. Award at most one team-level pair bonus.
    Bitboard bs = ownBishops;
    while (bs) ++ownBishopColors[squareColor(popLsb(bs))];
    int score = 0;
    if (ownBishopColors[0] && ownBishopColors[1])
        score += tune.bishopPairBonus;

    if (tune.bishopVsKnightBonus && enemyKnights) {
        // Four players may retain up to 32 initial pawns. Open positions
        // increasingly favor bishops over enemy knights. No pair duplication.
        const int openness = 32 - std::min(32, allPawns);
        score += tune.bishopVsKnightBonus
            * std::min(2, ownBishops.popcount())
            * std::min(2, enemyKnights) * openness / 64;
    }

    // Per-piece pawn structure and king-shelter patterns have to use the
    // owner's orientation, even when two allied colors cooperate.
    if (tune.badBishopPenalty || tune.bishopFianchettoBonus
        || tune.bishopReturningBonus) {
        for (unsigned i = 0; i < COLOR_NB; ++i) {
            const Color c = static_cast<Color>(i);
            if (teamOf(c) != team || !pos.isAlive(c))
                continue;
            Bitboard bishops = pos.pieces(c, BISHOP);
            while (bishops) {
                const Square bishop = popLsb(bishops);
                if (tune.badBishopPenalty) {
                    int sameColorAhead = 0;
                    Bitboard pawns = ownPawns;
                    while (pawns) {
                        const Square pawn = popLsb(pawns);
                        const int ahead = homeDepth(c, pawn) - homeDepth(c, bishop);
                        if (ahead > 0 && ahead <= 4
                            && std::abs(lateral(c, pawn) - lateral(c, bishop)) <= 4
                            && squareColor(pawn) == squareColor(bishop))
                            ++sameColorAhead;
                    }
                    // An active bishop *outside* the own/team pawn chain is
                    // not treated as bad. Cap promotions and congested setups.
                    score -= tune.badBishopPenalty * std::min(4, sameColorAhead) / 4;
                }
                if (tune.bishopFianchettoBonus && isFianchetto(pos, c, bishop))
                    score += tune.bishopFianchettoBonus;
                if (tune.bishopReturningBonus && looksLikeReturningBishop(pos, c, bishop))
                    score += tune.bishopReturningBonus;
            }
        }
    }

    if (tune.bishopColorWeaknessPenalty && enemyBishops) {
        Bitboard enemyAttacks[2]{};
        while (enemyBishops) {
            const Square bishop = popLsb(enemyBishops);
            enemyAttacks[squareColor(bishop)] |= pos.bishopAttacks(bishop);
        }
        for (unsigned i = 0; i < COLOR_NB; ++i) {
            const Color c = static_cast<Color>(i);
            if (teamOf(c) != team || !pos.isAlive(c))
                continue;
            const Square king = pos.kingSquare(c);
            if (king == SQ_NONE)
                continue;
            int weaknesses = 0;
            Bitboard aroundKing = Geometry::KingAttacks[king];
            while (aroundKing) {
                const Square target = popLsb(aroundKing);
                const int color = squareColor(target);
                if (ownBishopColors[color] || !enemyAttacks[color].test(target)
                    || pos.pieceAt(target) != NO_PIECE
                    || protectedByTeamPawn(pos, team, target))
                    continue;
                ++weaknesses;
            }
            // Each exposed king contributes a maximum of two weak squares.
            score -= tune.bishopColorWeaknessPenalty * std::min(2, weaknesses);
        }
    }
    return score;
}

} // namespace ironphoenix::Eval
