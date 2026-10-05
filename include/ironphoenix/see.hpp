#pragma once

#include "position.hpp"

namespace ironphoenix {

enum class SeeMode : std::uint8_t {
    Fast,
    Legal
};

struct SeeAttacker {
    Square square = SQ_NONE;
    PieceType type = PT_NONE;

    IRONPHOENIX_FORCE_INLINE constexpr explicit operator bool() const noexcept {
        return square != SQ_NONE;
    }
};

[[nodiscard]] SeeAttacker leastValuableAttacker(const Position& pos,
                                                Square target,
                                                Color color,
                                                Bitboard occ,
                                                SeeMode mode = SeeMode::Fast) noexcept;

[[nodiscard]] int see(const Position& pos, Move m, SeeMode mode = SeeMode::Fast) noexcept;

[[nodiscard]] IRONPHOENIX_FORCE_INLINE int seeLegal(const Position& pos, Move m) noexcept {
    return see(pos, m, SeeMode::Legal);
}

[[nodiscard]] bool seeGE(const Position& pos,
                         Move m,
                         int threshold,
                         SeeMode mode = SeeMode::Fast) noexcept;

[[nodiscard]] int seeImmediateGain(const Position& pos, Move m) noexcept;

}
