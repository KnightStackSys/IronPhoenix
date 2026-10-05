#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#  define IRONPHOENIX_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define IRONPHOENIX_FORCE_INLINE inline __attribute__((always_inline))
#else
#  define IRONPHOENIX_FORCE_INLINE inline
#endif

namespace ironphoenix {

using Square = std::uint8_t;
using Piece  = std::uint8_t;
using Key    = std::uint64_t;

constexpr Square SQ_NONE = 0xFF;
constexpr Piece  NO_PIECE = 0;
constexpr int BOARD_FILES = 14;
constexpr int BOARD_RANKS = 14;
constexpr int SQUARE_NB   = 160;
constexpr int COLOR_NB    = 4;
constexpr int PIECE_TYPE_NB = 7;

enum Color : std::uint8_t {
    RED    = 0,
    BLUE   = 1,
    YELLOW = 2,
    GREEN  = 3
};

enum PieceType : std::uint8_t {
    PT_NONE = 0,
    PAWN    = 1,
    KNIGHT  = 2,
    BISHOP  = 3,
    ROOK    = 4,
    QUEEN   = 5,
    KING    = 6
};

enum Direction : std::uint8_t {
    NORTH = 0,
    EAST  = 1,
    SOUTH = 2,
    WEST  = 3,
    NORTH_EAST = 4,
    NORTH_WEST = 5,
    SOUTH_EAST = 6,
    SOUTH_WEST = 7
};

constexpr std::array<int, 7> SEE_VALUE = {
    0, 100, 300, 400, 500, 1000, 30000
};

IRONPHOENIX_FORCE_INLINE constexpr Color nextColor(Color c) noexcept {
    return static_cast<Color>((static_cast<unsigned>(c) + 1u) & 3u);
}

IRONPHOENIX_FORCE_INLINE constexpr int teamOf(Color c) noexcept {
    return static_cast<int>(c) & 1;
}

IRONPHOENIX_FORCE_INLINE constexpr bool allied(Color a, Color b) noexcept {
    return teamOf(a) == teamOf(b);
}

IRONPHOENIX_FORCE_INLINE constexpr bool enemy(Color a, Color b) noexcept {
    return teamOf(a) != teamOf(b);
}

IRONPHOENIX_FORCE_INLINE constexpr Piece makePiece(Color c, PieceType pt) noexcept {
    return pt == PT_NONE ? NO_PIECE
                         : static_cast<Piece>((static_cast<unsigned>(c) << 3u)
                                             | static_cast<unsigned>(pt));
}

IRONPHOENIX_FORCE_INLINE constexpr PieceType typeOf(Piece p) noexcept {
    return static_cast<PieceType>(p & 0x7u);
}

IRONPHOENIX_FORCE_INLINE constexpr Color colorOf(Piece p) noexcept {
    return static_cast<Color>((p >> 3u) & 0x3u);
}

IRONPHOENIX_FORCE_INLINE constexpr bool isPiece(Piece p) noexcept {
    return p != NO_PIECE;
}

IRONPHOENIX_FORCE_INLINE constexpr int pieceValue(PieceType pt) noexcept {
    return SEE_VALUE[static_cast<unsigned>(pt)];
}

IRONPHOENIX_FORCE_INLINE constexpr int pieceValue(Piece p) noexcept {
    return pieceValue(typeOf(p));
}

}
