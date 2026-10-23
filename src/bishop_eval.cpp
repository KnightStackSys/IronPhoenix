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

int bishopFeatureDifference(const Position& pos) noexcept {
    const auto& tune = Tuning::eval();
    if (tune.badBishopPenalty == 0 && tune.bishopPairBonus == 0
        && tune.bishopVsKnightBonus == 0 && tune.bishopColorWeaknessPenalty == 0
        && tune.bishopFianchettoBonus == 0 && tune.bishopReturningBonus == 0)
        return 0;

    // Gather 4PC team data ONCE for Red+Yellow and Blue+Green, instead
    // of repeating identical pawn and bishop scans for each team.
    Bitboard teamBishops[2]{}, teamPawns[2]{};
    int knights[2]{}, bishopColors[2]{}, score[2]{};
    int allPawns = 0;
    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color c = static_cast<Color>(ci);
        if (!pos.isAlive(c))
            continue;
        const int team = teamOf(c);
        const Bitboard pawns = pos.pieces(c, PAWN);
        teamBishops[team] |= pos.pieces(c, BISHOP);
        teamPawns[team] |= pawns;
        knights[team] += pos.pieces(c, KNIGHT).popcount();
        allPawns += pawns.popcount();
    }

    for (int team = 0; team < 2; ++team) {
        Bitboard bishops = teamBishops[team];
        while (bishops)
            bishopColors[team] |= 1 << squareColor(popLsb(bishops));

        // At most one complementary pair across both teammates.
        if (bishopColors[team] == 3)
            score[team] += tune.bishopPairBonus;

        if (tune.bishopVsKnightBonus && knights[team ^ 1]) {
            const int openness = 32 - std::min(32, allPawns);
            score[team] += tune.bishopVsKnightBonus
                * std::min(2, teamBishops[team].popcount())
                * std::min(2, knights[team ^ 1]) * openness / 64;
        }
    }

    if (tune.badBishopPenalty || tune.bishopFianchettoBonus
        || tune.bishopReturningBonus) {
        for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
            const Color c = static_cast<Color>(ci);
            if (!pos.isAlive(c))
                continue;
            const int team = teamOf(c);
            Bitboard bishops = pos.pieces(c, BISHOP);
            while (bishops) {
                const Square bishop = popLsb(bishops);
                if (tune.badBishopPenalty) {
                    // One 3-word intersection+popcount replaces repeated
                    // per-pawn coordinate calculations in the hot eval loop.
                    const int blocked = (teamPawns[team]
                        & Geometry::BishopBadPawnMask[ci][bishop]).popcount();
                    score[team] -= tune.badBishopPenalty * std::min(4, blocked) / 4;
                }
                if (tune.bishopFianchettoBonus && isFianchetto(pos, c, bishop))
                    score[team] += tune.bishopFianchettoBonus;
                if (tune.bishopReturningBonus && looksLikeReturningBishop(pos, c, bishop))
                    score[team] += tune.bishopReturningBonus;
            }
        }
    }

    if (tune.bishopColorWeaknessPenalty) {
        // Both teams need the other team's sliding attack masks. Calculate
        // each team's bishop attacks only once, then reuse them for both kings.
        Bitboard bishopAttacks[2][2]{};
        for (int team = 0; team < 2; ++team) {
            if (!teamBishops[team])
                continue;
            Bitboard bishops = teamBishops[team];
            while (bishops) {
                const Square bishop = popLsb(bishops);
                bishopAttacks[team][squareColor(bishop)] |= pos.bishopAttacks(bishop);
            }
        }

        for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
            const Color c = static_cast<Color>(ci);
            if (!pos.isAlive(c))
                continue;
            const int team = teamOf(c);
            const Square king = pos.kingSquare(c);
            if (king == SQ_NONE)
                continue;

            int weak = 0;
            Bitboard nearby = Geometry::KingAttacks[king];
            while (nearby && weak < 2) {
                const Square target = popLsb(nearby);
                const int color = squareColor(target);
                if ((bishopColors[team] & (1 << color))
                    || !bishopAttacks[team ^ 1][color].test(target)
                    || pos.pieceAt(target) != NO_PIECE
                    || protectedByTeamPawn(pos, team, target))
                    continue;
                ++weak;
            }
            score[team] -= tune.bishopColorWeaknessPenalty * weak;
        }
    }
    return score[0] - score[1];
}

} // namespace ironphoenix::Eval
