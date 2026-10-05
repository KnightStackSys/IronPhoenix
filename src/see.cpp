#include "ironphoenix/see.hpp"

#include <algorithm>
#include <array>

namespace ironphoenix {

namespace {

IRONPHOENIX_FORCE_INLINE bool legalSeeAttacker(const Position& pos,
                                         Square target,
                                         Color c,
                                         Square from,
                                         PieceType pt,
                                         Bitboard occ,
                                         SeeMode mode) noexcept {
    Bitboard after = occ;
    after.reset(from);

    if (pt == KING) {
        Bitboard enemy = pos.attackersToEnemy(target, c, after);
        enemy.reset(target);
        return !enemy;
    }

    if (mode == SeeMode::Fast)
        return true;

    const Square king = pos.kingSquare(c);
    if (king == SQ_NONE)
        return true;

    Bitboard enemy = pos.attackersToEnemy(king, c, after);
    enemy.reset(target);
    return !enemy;
}

IRONPHOENIX_FORCE_INLINE PieceType promotedRecaptureType(Color c,
                                                   PieceType attacker,
                                                   Square target) noexcept {
    return attacker == PAWN && Geometry::isPromotionSquare(c, target) ? QUEEN : attacker;
}

IRONPHOENIX_FORCE_INLINE int promotionBonus(PieceType attacker,
                                      PieceType placed) noexcept {
    return attacker == PAWN && placed != PAWN
        ? pieceValue(placed) - pieceValue(PAWN)
        : 0;
}

IRONPHOENIX_FORCE_INLINE Piece actualCapturedPiece(const Position& pos, Move m) noexcept {
    const Piece moving = pos.pieceAt(m.from());
    if (moving == NO_PIECE)
        return NO_PIECE;
    const Square csq = pos.capturedSquare(m, colorOf(moving));
    return csq == SQ_NONE ? NO_PIECE : pos.pieceAt(csq);
}

}

SeeAttacker leastValuableAttacker(const Position& pos,
                                  Square target,
                                  Color color,
                                  Bitboard occ,
                                  SeeMode mode) noexcept {
    Bitboard attackers = pos.attackersTo(target, color, occ);
    if (!attackers)
        return {};

    constexpr std::array<PieceType, 6> ORDER = {
        PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
    };

    for (PieceType pt : ORDER) {
        Bitboard candidates = attackers & pos.pieces(color, pt) & occ;
        while (candidates) {
            const Square sq = popLsb(candidates);
            if (legalSeeAttacker(pos, target, color, sq, pt, occ, mode))
                return {sq, pt};
        }
    }
    return {};
}

int seeImmediateGain(const Position& pos, Move m) noexcept {
    const Piece moving = pos.pieceAt(m.from());
    if (moving == NO_PIECE)
        return 0;

    const Piece captured = actualCapturedPiece(pos, m);
    int gain = captured == NO_PIECE ? 0 : pieceValue(captured);

    if (m.isPromotion()) {
        const PieceType promo = promotedType(m.promotion());
        gain += pieceValue(promo) - pieceValue(PAWN);
    }
    return gain;
}

int see(const Position& pos, Move m, SeeMode mode) noexcept {
    const Square from = m.from();
    const Square target = m.to();
    if (from >= SQUARE_NB || target >= SQUARE_NB)
        return 0;

    const Piece moving = pos.pieceAt(from);
    if (moving == NO_PIECE)
        return 0;

    const Color us = colorOf(moving);
    const Piece captured = actualCapturedPiece(pos, m);

    if (captured != NO_PIECE && typeOf(captured) == KING && enemy(us, colorOf(captured)))
        return pieceValue(KING);

    std::array<int, 64> gain{};
    int depth = 0;
    gain[0] = captured == NO_PIECE ? 0 : pieceValue(captured);

    PieceType occupant = typeOf(moving);
    if (m.isPromotion()) {
        occupant = promotedType(m.promotion());
        gain[0] += pieceValue(occupant) - pieceValue(PAWN);
    }

    Bitboard occ = pos.occupancy();
    occ.reset(from);

    const Square capSq = pos.capturedSquare(m, us);
    if (captured != NO_PIECE && capSq != SQ_NONE)
        occ.reset(capSq);
    occ.set(target);

    if (m.isCastle()) {
        if (const CastleLane* lane = pos.castleLane(us, m)) {
            occ.reset(lane->rookFrom);
            occ.set(lane->rookTo);
        }
    }

    Color side = nextColor(us);

    while (depth + 1 < static_cast<int>(gain.size())) {
        const SeeAttacker a = leastValuableAttacker(pos, target, side, occ, mode);
        if (!a)
            break;

        ++depth;
        const PieceType placed = promotedRecaptureType(side, a.type, target);
        const int bonus = promotionBonus(a.type, placed);
        gain[depth] = pieceValue(occupant) + bonus - gain[depth - 1];

        occ.reset(a.square);
        occupant = placed;

        if (a.type == KING)
            break;

        side = nextColor(side);
    }

    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }
    return gain[0];
}

bool seeGE(const Position& pos, Move m, int threshold, SeeMode mode) noexcept {
    const int immediate = seeImmediateGain(pos, m);

    if (immediate < threshold)
        return false;

    return see(pos, m, mode) >= threshold;
}

}
