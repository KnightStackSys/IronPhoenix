#pragma once

#include "types.hpp"

#include <cstdint>

namespace ironphoenix {

enum MoveFlag : std::uint32_t {
    MF_NONE        = 0,
    MF_EN_PASSANT  = 1u << 24,
    MF_CASTLE      = 1u << 25,
    MF_DOUBLE_PUSH = 1u << 26
};

enum Promotion : std::uint8_t {
    PROMO_NONE   = 0,
    PROMO_KNIGHT = 1,
    PROMO_BISHOP = 2,
    PROMO_ROOK   = 3,
    PROMO_QUEEN  = 4
};

struct Move {
    std::uint32_t v = 0;

    constexpr Move() noexcept = default;
    constexpr explicit Move(std::uint32_t raw) noexcept : v(raw) {}

    constexpr Move(Square from,
                   Square to,
                   Piece captured = NO_PIECE,
                   Promotion promo = PROMO_NONE,
                   std::uint32_t flags = MF_NONE) noexcept
        : v(static_cast<std::uint32_t>(from)
          | (static_cast<std::uint32_t>(to) << 8u)
          | ((static_cast<std::uint32_t>(promo) & 0x7u) << 16u)
          | ((static_cast<std::uint32_t>(captured) & 0x1Fu) << 19u)
          | flags) {}

    IRONPHOENIX_FORCE_INLINE constexpr explicit operator bool() const noexcept { return v != 0; }
    IRONPHOENIX_FORCE_INLINE constexpr Square from() const noexcept { return static_cast<Square>(v & 0xFFu); }
    IRONPHOENIX_FORCE_INLINE constexpr Square to() const noexcept { return static_cast<Square>((v >> 8u) & 0xFFu); }
    IRONPHOENIX_FORCE_INLINE constexpr Promotion promotion() const noexcept {
        return static_cast<Promotion>((v >> 16u) & 0x7u);
    }
    IRONPHOENIX_FORCE_INLINE constexpr Piece captured() const noexcept {
        return static_cast<Piece>((v >> 19u) & 0x1Fu);
    }
    IRONPHOENIX_FORCE_INLINE constexpr bool isCapture() const noexcept {
        return captured() != NO_PIECE || isEnPassant();
    }
    IRONPHOENIX_FORCE_INLINE constexpr bool isPromotion() const noexcept { return promotion() != PROMO_NONE; }
    IRONPHOENIX_FORCE_INLINE constexpr bool isEnPassant() const noexcept { return (v & MF_EN_PASSANT) != 0; }
    IRONPHOENIX_FORCE_INLINE constexpr bool isCastle() const noexcept { return (v & MF_CASTLE) != 0; }
    IRONPHOENIX_FORCE_INLINE constexpr bool isDoublePush() const noexcept { return (v & MF_DOUBLE_PUSH) != 0; }

    IRONPHOENIX_FORCE_INLINE constexpr bool operator==(Move o) const noexcept { return v == o.v; }
    IRONPHOENIX_FORCE_INLINE constexpr bool operator!=(Move o) const noexcept { return v != o.v; }
};

IRONPHOENIX_FORCE_INLINE constexpr PieceType promotedType(Promotion p) noexcept {
    switch (p) {
        case PROMO_KNIGHT: return KNIGHT;
        case PROMO_BISHOP: return BISHOP;
        case PROMO_ROOK:   return ROOK;
        case PROMO_QUEEN:  return QUEEN;
        default:           return PT_NONE;
    }
}

}
