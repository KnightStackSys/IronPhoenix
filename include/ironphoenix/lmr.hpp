#pragma once

#include <array>
#include <cstdint>

namespace ironphoenix {

    inline constexpr int LMR_MAX_DEPTH = 128;
    inline constexpr int LMR_MAX_MOVES = 512;

    using LmrReductionTable =
        std::array<std::array<std::uint8_t, LMR_MAX_MOVES + 1>,
        LMR_MAX_DEPTH + 1>;

    extern const LmrReductionTable lmrReduction;

    inline int lmrBaseReduction(int depth, int moveCount) noexcept {
        if (depth <= 0 || moveCount <= 0)
            return 0;

        if (depth > LMR_MAX_DEPTH)
            depth = LMR_MAX_DEPTH;

        if (moveCount > LMR_MAX_MOVES)
            moveCount = LMR_MAX_MOVES;

        return static_cast<int>(lmrReduction[depth][moveCount]);
    }

}
