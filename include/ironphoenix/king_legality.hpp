#pragma once
#include "position.hpp"

namespace ironphoenix {

// Only slider attacks can become newly exposed when an ordinary move vacates
// one source square. If that source is not on any rank/file/diagonal passing
// through our king, then an already-safe king cannot become checked.
// Castling/EP remove other pieces and king moves relocate the target, so they
// always require full validation. This returns true conservatively.
[[nodiscard]] IRONPHOENIX_FORCE_INLINE bool kingCheckCanChange(
    const Position& pos, Move move) noexcept {
    if (move.isEnPassant() || move.isCastle())
        return true;
    const Piece moving = pos.pieceAt(move.from());
    if (moving == NO_PIECE || typeOf(moving) == KING)
        return true;
    const Square king = pos.kingSquare(pos.sideToMove());
    if (king == SQ_NONE)
        return true;
    for (unsigned family = 0; family < Geometry::LINE_FAMILY_NB; ++family)
        if (Geometry::LineId[family][king] == Geometry::LineId[family][move.from()])
            return true;
    return false;
}

} // namespace ironphoenix
