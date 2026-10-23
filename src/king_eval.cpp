#include "ironphoenix/king_eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <cstdlib>

namespace ironphoenix::Eval {
namespace {

constexpr int PHASE_MAX = 24; // two opposing armies: Q=4, R=2, B/N=1

int enemyPhase(const Position& pos, Color defender) noexcept {
    int phase = 0;
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const auto foe = static_cast<Color>(i);
        if (!pos.isAlive(foe) || !enemy(defender, foe))
            continue;
        phase += 4 * pos.pieces(foe, QUEEN).popcount()
               + 2 * pos.pieces(foe, ROOK).popcount()
               + pos.pieces(foe, BISHOP).popcount()
               + pos.pieces(foe, KNIGHT).popcount();
    }
    return std::min(PHASE_MAX, phase);
}

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

int centralDistance(Square sq) noexcept {
    const int f = Geometry::fileOf(sq), r = Geometry::rankOf(sq);
    return std::min(std::abs(f - 6), std::abs(f - 7))
         + std::min(std::abs(r - 6), std::abs(r - 7));
}

Square sideSquare(Color c, Square king, int offset) noexcept {
    const int file = Geometry::fileOf(king), rank = Geometry::rankOf(king);
    const int f = file + ((c == RED || c == YELLOW) ? offset : 0);
    const int r = rank + ((c == BLUE || c == GREEN) ? offset : 0);
    return Geometry::validXY(f, r) ? Geometry::square(f, r) : SQ_NONE;
}

bool friendlyPawn(const Position& pos, Color c, Square sq) noexcept {
    const Piece p = pos.pieceAt(sq);
    return p != NO_PIECE && typeOf(p) == PAWN && allied(c, colorOf(p));
}

int shieldMissing(const Position& pos, Color c, Square king) noexcept {
    if (homeDepth(c, king) > 4)
        return 0; // A wandering king does not have a meaningful home shield.
    int missing = 0;
    for (int offset = -1; offset <= 1; ++offset) {
        const Square start = sideSquare(c, king, offset);
        if (start == SQ_NONE)
            continue;
        const Square first = Geometry::step(start, Geometry::pawnForward(c));
        const Square second = first == SQ_NONE
            ? SQ_NONE : Geometry::step(first, Geometry::pawnForward(c));
        if ((first == SQ_NONE || !friendlyPawn(pos, c, first))
            && (second == SQ_NONE || !friendlyPawn(pos, c, second)))
            ++missing;
    }
    return missing;
}

int enemyPawnStorm(const Position& pos, Color c, Square king) noexcept {
    if (homeDepth(c, king) > 4)
        return 0;
    int nearby = 0;
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const auto foe = static_cast<Color>(i);
        if (!pos.isAlive(foe) || !enemy(c, foe))
            continue;
        Bitboard pawns = pos.pieces(foe, PAWN);
        while (pawns) {
            const Square pawn = popLsb(pawns);
            const int dx = std::abs(int(Geometry::fileOf(pawn)) - int(Geometry::fileOf(king)));
            const int dy = std::abs(int(Geometry::rankOf(pawn)) - int(Geometry::rankOf(king)));
            if (std::max(dx, dy) <= 3)
                ++nearby;
        }
    }
    return std::min(4, nearby);
}

bool pawnOnKingWing(const Position& pos, Color c, Square king) noexcept {
    const int across = lateral(c, king);
    if (across > 4 && across < 9)
        return true; // Not on a wing.
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const auto owner = static_cast<Color>(i);
        if (!pos.isAlive(owner) || !allied(c, owner))
            continue;
        Bitboard pawns = pos.pieces(owner, PAWN);
        while (pawns) {
            const int lane = lateral(c, popLsb(pawns));
            if (across <= 4 ? lane <= 4 : lane >= 9)
                return true;
        }
    }
    return false;
}

int castleRights(const Position& pos, Color c) noexcept {
    int available = 0;
    for (unsigned lane = 0; lane < 2; ++lane) {
        const CastleLane& def = pos.castleDefinition(c, lane);
        if (!def.valid() || def.rightBit >= 8
            || !(pos.castlingRights() & (1u << def.rightBit)))
            continue;
        if (pos.kingSquare(c) == def.kingFrom
            && pos.pieceAt(def.kingFrom) == makePiece(c, KING)
            && pos.pieceAt(def.rookFrom) == makePiece(c, ROOK))
            ++available;
    }
    return available;
}

// Count absolute pins of friendly material to a king. For x-rays, count
// friendly pieces shielding a friendly queen from an enemy slider.
// No ray can cross a corner hole: Geometry::step returns SQ_NONE.
int rayPressure(const Position& pos, Color c, Square target) noexcept {
    int count = 0;
    for (unsigned d = NORTH; d <= SOUTH_WEST; ++d) {
        const auto dir = static_cast<Direction>(d);
        Square cur = Geometry::step(target, dir);
        while (cur != SQ_NONE && pos.pieceAt(cur) == NO_PIECE)
            cur = Geometry::step(cur, dir);
        if (cur == SQ_NONE)
            continue;
        const Piece screen = pos.pieceAt(cur);
        if (!allied(c, colorOf(screen)) || typeOf(screen) == KING)
            continue;
        cur = Geometry::step(cur, dir);
        while (cur != SQ_NONE && pos.pieceAt(cur) == NO_PIECE)
            cur = Geometry::step(cur, dir);
        if (cur == SQ_NONE)
            continue;
        const Piece slider = pos.pieceAt(cur);
        if (!enemy(c, colorOf(slider)))
            continue;
        const auto pt = typeOf(slider);
        const bool orthogonal = d <= WEST;
        if (pt == QUEEN || (orthogonal && pt == ROOK)
            || (!orthogonal && pt == BISHOP))
            ++count;
    }
    return std::min(4, count);
}

int kingScore(const Position& pos, Color c, const Bitboard (&attacks)[2]) noexcept {
    if (!pos.isAlive(c))
        return 0;
    const Square king = pos.kingSquare(c);
    if (king == SQ_NONE)
        return 0;

    const auto& w = Tuning::eval();
    const int phase = enemyPhase(pos, c);
    const int endPhase = PHASE_MAX - phase;
    const int team = teamOf(c);
    int score = 0;

    if (phase > 0) {
        int danger = 0;
        if (w.kingShieldPenalty)
            danger += w.kingShieldPenalty * shieldMissing(pos, c, king);
        if (w.kingPawnStormPenalty)
            danger += w.kingPawnStormPenalty * enemyPawnStorm(pos, c, king);

        const Bitboard zone = Geometry::KingAttacks[king];
        const Bitboard hostile = attacks[team ^ 1];
        const int controlled = (zone & hostile).popcount();
        if (w.kingZoneAttackPenalty)
            danger += w.kingZoneAttackPenalty * std::min(8, controlled);

        // "Mate at a glance" is a king-danger pattern, NOT a forced mate
        // or terminal score. Search decides whether king capture is legal.
        if (w.kingMateThreatPenalty && hostile.test(king)) {
            const int escapes = (zone & ~pos.occupancy() & ~hostile).popcount();
            if (escapes <= 2)
                danger += w.kingMateThreatPenalty * (3 - escapes);
        }

        if (w.kingCastlingRightsBonus)
            score += phase * w.kingCastlingRightsBonus * castleRights(pos, c) / PHASE_MAX;

        if (w.kingPinPenalty)
            danger += w.kingPinPenalty * rayPressure(pos, c, king);

        score -= danger * phase / PHASE_MAX;
    }

    if (endPhase > 0) {
        if (w.kingCentralizationBonus)
            score += w.kingCentralizationBonus
                * (12 - centralDistance(king)) * endPhase / PHASE_MAX;
        if (w.kingPawnlessWingPenalty && !pawnOnKingWing(pos, c, king))
            score -= w.kingPawnlessWingPenalty * endPhase / PHASE_MAX;
    }

    if (w.kingXrayPenalty) {
        Bitboard queens = pos.pieces(c, QUEEN);
        int rays = 0;
        while (queens)
            rays += rayPressure(pos, c, popLsb(queens));
        score -= w.kingXrayPenalty * std::min(4, rays);
    }
    return score;
}

} // namespace

int kingFeatureDifference(const Position& pos,
    const Bitboard (&teamAttacks)[2]) noexcept {
    const auto& w = Tuning::eval();
    if (!(w.kingShieldPenalty || w.kingPawnStormPenalty
        || w.kingZoneAttackPenalty || w.kingMateThreatPenalty
        || w.kingCentralizationBonus || w.kingPawnlessWingPenalty
        || w.kingPinPenalty || w.kingXrayPenalty
        || w.kingCastlingRightsBonus))
        return 0;

    int teamScore[2]{};
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const auto c = static_cast<Color>(i);
        teamScore[teamOf(c)] += kingScore(pos, c, teamAttacks);
    }
    return teamScore[0] - teamScore[1];
}

} // namespace ironphoenix::Eval
