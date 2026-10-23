#pragma once

#include "tuning.hpp"

#include <algorithm>

namespace ironphoenix::IIR {

// Internal Iterative Reduction for every negamax node without a TT move.
// The minimum depth and reduction size are independently UCI-tunable.
[[nodiscard]] inline int reduction(int depth, bool hasTTMove,
    const Tuning::SearchParameters& tune) noexcept {
    if (depth < tune.iirMinDepth || hasTTMove || tune.iirReduction <= 0)
        return 0;
    // Even when the configured reduction exceeds this node's depth, keep
    // at least one search ply instead of feeding a negative depth to move gen.
    return std::min(tune.iirReduction, depth - 1);
}

} // namespace ironphoenix::IIR
