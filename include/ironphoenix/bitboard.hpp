#pragma once

#include "types.hpp"

#include <bit>
#include <cassert>
#include <cstdint>

namespace ironphoenix {
    
struct Bitboard {
    std::uint64_t w0 = 0;
    std::uint64_t w1 = 0;
    std::uint64_t w2 = 0;

    IRONPHOENIX_FORCE_INLINE constexpr explicit operator bool() const noexcept {
        return (w0 | w1 | w2) != 0;
    }

    IRONPHOENIX_FORCE_INLINE constexpr bool empty() const noexcept {
        return (w0 | w1 | w2) == 0;
    }

    IRONPHOENIX_FORCE_INLINE constexpr void clear() noexcept {
        w0 = w1 = w2 = 0;
    }

    IRONPHOENIX_FORCE_INLINE constexpr bool test(Square sq) const noexcept {
        const unsigned s = static_cast<unsigned>(sq);
        const std::uint64_t b = 1ULL << (s & 63u);
        return s < 64u ? (w0 & b) != 0
             : s < 128u ? (w1 & b) != 0
                        : (w2 & b) != 0;
    }

    IRONPHOENIX_FORCE_INLINE constexpr void set(Square sq) noexcept {
        const unsigned s = static_cast<unsigned>(sq);
        const std::uint64_t b = 1ULL << (s & 63u);
        if (s < 64u) w0 |= b;
        else if (s < 128u) w1 |= b;
        else w2 |= b;
    }

    IRONPHOENIX_FORCE_INLINE constexpr void reset(Square sq) noexcept {
        const unsigned s = static_cast<unsigned>(sq);
        const std::uint64_t b = ~(1ULL << (s & 63u));
        if (s < 64u) w0 &= b;
        else if (s < 128u) w1 &= b;
        else w2 &= b;
    }

    IRONPHOENIX_FORCE_INLINE constexpr int popcount() const noexcept {
        return std::popcount(w0) + std::popcount(w1) + std::popcount(w2);
    }
};

IRONPHOENIX_FORCE_INLINE constexpr Bitboard operator|(Bitboard a, Bitboard b) noexcept {
    return {a.w0 | b.w0, a.w1 | b.w1, a.w2 | b.w2};
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard operator&(Bitboard a, Bitboard b) noexcept {
    return {a.w0 & b.w0, a.w1 & b.w1, a.w2 & b.w2};
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard operator^(Bitboard a, Bitboard b) noexcept {
    return {a.w0 ^ b.w0, a.w1 ^ b.w1, a.w2 ^ b.w2};
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard operator~(Bitboard a) noexcept {
    return {~a.w0, ~a.w1, (~a.w2) & 0xFFFFFFFFULL};
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard& operator|=(Bitboard& a, Bitboard b) noexcept {
    a.w0 |= b.w0; a.w1 |= b.w1; a.w2 |= b.w2; return a;
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard& operator&=(Bitboard& a, Bitboard b) noexcept {
    a.w0 &= b.w0; a.w1 &= b.w1; a.w2 &= b.w2; return a;
}
IRONPHOENIX_FORCE_INLINE constexpr Bitboard& operator^=(Bitboard& a, Bitboard b) noexcept {
    a.w0 ^= b.w0; a.w1 ^= b.w1; a.w2 ^= b.w2; return a;
}
IRONPHOENIX_FORCE_INLINE constexpr bool operator==(Bitboard a, Bitboard b) noexcept {
    return a.w0 == b.w0 && a.w1 == b.w1 && a.w2 == b.w2;
}
IRONPHOENIX_FORCE_INLINE constexpr bool operator!=(Bitboard a, Bitboard b) noexcept {
    return !(a == b);
}

IRONPHOENIX_FORCE_INLINE constexpr Bitboard squareBB(Square sq) noexcept {
    Bitboard b{};
    b.set(sq);
    return b;
}

IRONPHOENIX_FORCE_INLINE Square lsb(Bitboard b) noexcept {
    assert(static_cast<bool>(b));
    if (b.w0) return static_cast<Square>(std::countr_zero(b.w0));
    if (b.w1) return static_cast<Square>(64 + std::countr_zero(b.w1));
    return static_cast<Square>(128 + std::countr_zero(b.w2));
}

IRONPHOENIX_FORCE_INLINE Square msb(Bitboard b) noexcept {
    assert(static_cast<bool>(b));
    if (b.w2) return static_cast<Square>(128 + (63 - std::countl_zero(b.w2)));
    if (b.w1) return static_cast<Square>(64 + (63 - std::countl_zero(b.w1)));
    return static_cast<Square>(63 - std::countl_zero(b.w0));
}

IRONPHOENIX_FORCE_INLINE Square popLsb(Bitboard& b) noexcept {
    assert(static_cast<bool>(b));
    if (b.w0) {
        const Square s = static_cast<Square>(std::countr_zero(b.w0));
        b.w0 &= b.w0 - 1;
        return s;
    }
    if (b.w1) {
        const Square s = static_cast<Square>(64 + std::countr_zero(b.w1));
        b.w1 &= b.w1 - 1;
        return s;
    }
    const Square s = static_cast<Square>(128 + std::countr_zero(b.w2));
    b.w2 &= b.w2 - 1;
    return s;
}

}
