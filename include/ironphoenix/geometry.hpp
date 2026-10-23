#pragma once

#include "bitboard.hpp"

#include <array>
#include <cstdint>

namespace ironphoenix::Geometry {

inline constexpr unsigned LINE_BITS = 14;
inline constexpr unsigned LINE_OCCUPANCIES = 1u << LINE_BITS;
inline constexpr unsigned LINE_HALF_BITS = 7;
inline constexpr unsigned LINE_HALF_OCCUPANCIES = 1u << LINE_HALF_BITS;

inline constexpr unsigned LINE_RANK = 0;
inline constexpr unsigned LINE_FILE = 1;
inline constexpr unsigned LINE_DIAG_A = 2; // file - rank
inline constexpr unsigned LINE_DIAG_B = 3; // file + rank
inline constexpr unsigned LINE_FAMILY_NB = 4;

inline constexpr unsigned RANK_LINE_BASE = 0;
inline constexpr unsigned FILE_LINE_BASE = RANK_LINE_BASE + BOARD_RANKS;
inline constexpr unsigned DIAG_A_LINE_BASE = FILE_LINE_BASE + BOARD_FILES;
inline constexpr unsigned DIAG_B_LINE_BASE = DIAG_A_LINE_BASE + 27;
inline constexpr unsigned LINE_NB = DIAG_B_LINE_BASE + 27;

extern std::array<std::array<Square, BOARD_RANKS>, BOARD_FILES> SquareFromXY;
extern std::array<std::uint8_t, SQUARE_NB> FileOf;
extern std::array<std::uint8_t, SQUARE_NB> RankOf;
extern std::array<std::array<Square, 8>, SQUARE_NB> Step;
extern std::array<std::array<Bitboard, SQUARE_NB>, 8> Ray;
extern std::array<Bitboard, SQUARE_NB> KnightAttacks;
// Current knight squares from which one further knight move COULD check
// the king on the indexed square (ignores dynamically blocked landings).
extern std::array<Bitboard, SQUARE_NB> KnightCheckApproach;
// Precomputed Chebyshev-distance <= 2 zones for knight activity near
// opposing non-king pieces. Reuse rather than scanning every enemy piece.
extern std::array<Bitboard, SQUARE_NB> KnightEnemyProximity;
extern std::array<Bitboard, SQUARE_NB> KingAttacks;
// Team pawn chain in front of a bishop; one precomputed mask per owner and square.
extern std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> BishopBadPawnMask;
extern std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttacks;
extern std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttackers;
extern Bitboard BoardMask;

// Compact 14-bit line representation used by Position for current-occupancy
// rook/bishop/queen attacks. Missing cross-board squares are encoded as
// permanent blockers so a diagonal can never jump across a board hole.
extern std::array<std::array<std::uint8_t, SQUARE_NB>, LINE_FAMILY_NB> LineId;
extern std::array<std::array<std::uint8_t, SQUARE_NB>, LINE_FAMILY_NB> LinePos;
extern std::array<std::array<std::uint16_t, SQUARE_NB>, LINE_FAMILY_NB> LineBit;
extern std::array<std::uint16_t, LINE_NB> LineBlockers;
extern std::array<std::array<std::uint16_t, LINE_OCCUPANCIES>, LINE_BITS> LineAttackMask;
extern std::array<std::array<std::array<Bitboard, LINE_HALF_OCCUPANCIES>, 2>, LINE_NB> LineExpand;

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

IRONPHOENIX_FORCE_INLINE Bitboard expandLine(unsigned line, std::uint16_t mask) noexcept {
    return LineExpand[line][0][mask & (LINE_HALF_OCCUPANCIES - 1u)]
         | LineExpand[line][1][mask >> LINE_HALF_BITS];
}

IRONPHOENIX_FORCE_INLINE Bitboard lineAttacks(unsigned line, unsigned pos, std::uint16_t occupancy) noexcept {
    const std::uint16_t attacks = LineAttackMask[pos][occupancy & (LINE_OCCUPANCIES - 1u)];
    return expandLine(line, attacks);
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

// Generic attacks for hypothetical occupancies (SEE, givesCheck, castling).
// Current-position move generation uses Position's compact line occupancy path.
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
