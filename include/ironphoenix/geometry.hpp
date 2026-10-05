#pragma once

#include "bitboard.hpp"

#include <array>
#include <cstdint>

namespace ironphoenix::Geometry {

extern std::array<std::array<Square, BOARD_RANKS>, BOARD_FILES> SquareFromXY;
extern std::array<std::uint8_t, SQUARE_NB> FileOf;
extern std::array<std::uint8_t, SQUARE_NB> RankOf;
extern std::array<std::array<Square, 8>, SQUARE_NB> Step;
extern std::array<std::array<Bitboard, SQUARE_NB>, 8> Ray;
extern std::array<Bitboard, SQUARE_NB> KnightAttacks;
extern std::array<Bitboard, SQUARE_NB> KingAttacks;
extern std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttacks;
extern std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttackers;
extern Bitboard BoardMask;

void init();

IRONPHOENIX_FORCE_INLINE constexpr bool validXY(int file, int rank) noexcept {
    return file >= 0 && file < BOARD_FILES && rank >= 0 && rank < BOARD_RANKS
        && ((file >= 3 && file <= 10) || (rank >= 3 && rank <= 10));
}

IRONPHOENIX_FORCE_INLINE Square square(int file, int rank) noexcept {
    return (file >= 0 && file < BOARD_FILES && rank >= 0 && rank < BOARD_RANKS)
        ? SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)]
        : SQ_NONE;
}

IRONPHOENIX_FORCE_INLINE std::uint8_t fileOf(Square sq) noexcept { return FileOf[sq]; }
IRONPHOENIX_FORCE_INLINE std::uint8_t rankOf(Square sq) noexcept { return RankOf[sq]; }

IRONPHOENIX_FORCE_INLINE Direction pawnForward(Color c) noexcept {
    switch (c) {
        case RED:    return NORTH;
        case BLUE:   return EAST;
        case YELLOW: return SOUTH;
        case GREEN:  return WEST;
    }
    return NORTH;
}

IRONPHOENIX_FORCE_INLINE Direction opposite(Direction d) noexcept {
    switch (d) {
        case NORTH: return SOUTH;
        case EAST: return WEST;
        case SOUTH: return NORTH;
        case WEST: return EAST;
        case NORTH_EAST: return SOUTH_WEST;
        case NORTH_WEST: return SOUTH_EAST;
        case SOUTH_EAST: return NORTH_WEST;
        case SOUTH_WEST: return NORTH_EAST;
    }
    return SOUTH;
}

IRONPHOENIX_FORCE_INLINE Square step(Square sq, Direction d) noexcept {
    return Step[sq][static_cast<unsigned>(d)];
}

IRONPHOENIX_FORCE_INLINE bool isPromotionSquare(Color c, Square sq) noexcept {
    const auto f = fileOf(sq);
    const auto r = rankOf(sq);
    switch (c) {
        case RED:    return r == 10;
        case BLUE:   return f == 10;
        case YELLOW: return r == 3;
        case GREEN:  return f == 3;
    }
    return false;
}

IRONPHOENIX_FORCE_INLINE bool isPawnHomeSquare(Color c, Square sq) noexcept {
    const auto f = fileOf(sq);
    const auto r = rankOf(sq);
    switch (c) {
        case RED:    return r == 1;
        case BLUE:   return f == 1;
        case YELLOW: return r == 12;
        case GREEN:  return f == 12;
    }
    return false;
}

IRONPHOENIX_FORCE_INLINE constexpr bool rayUsesLsb(Direction d) noexcept {
    return d == NORTH || d == EAST || d == NORTH_EAST || d == NORTH_WEST;
}

IRONPHOENIX_FORCE_INLINE Bitboard rayAttack(Square sq, Direction d, Bitboard occ) noexcept {
    const Bitboard ray = Ray[static_cast<unsigned>(d)][sq];
    const Bitboard blockers = ray & occ;
    if (!blockers)
        return ray;

    const Square blocker = rayUsesLsb(d) ? lsb(blockers) : msb(blockers);
    return ray ^ Ray[static_cast<unsigned>(d)][blocker];
}

IRONPHOENIX_FORCE_INLINE Bitboard rookAttacks(Square sq, Bitboard occ) noexcept {
    return rayAttack(sq, NORTH, occ)
         | rayAttack(sq, EAST,  occ)
         | rayAttack(sq, SOUTH, occ)
         | rayAttack(sq, WEST,  occ);
}

IRONPHOENIX_FORCE_INLINE Bitboard bishopAttacks(Square sq, Bitboard occ) noexcept {
    return rayAttack(sq, NORTH_EAST, occ)
         | rayAttack(sq, NORTH_WEST, occ)
         | rayAttack(sq, SOUTH_EAST, occ)
         | rayAttack(sq, SOUTH_WEST, occ);
}

IRONPHOENIX_FORCE_INLINE Bitboard queenAttacks(Square sq, Bitboard occ) noexcept {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

}
