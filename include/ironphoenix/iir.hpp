#pragma once

#include "tuning.hpp"

#include <algorithm>

namespace ironphoenix::IIR {

// Select a reduction using the propagated PVS node type. The caller also
// verifies the partner king is not checked before applying this reduction.
// PV-only is the default, matching the original 4PC IIR experiment.
[[nodiscard]] inline int reduction(int depth, int ply, bool pvNode,
    bool cutNode, bool hasTTMove, bool inCheck,
    const Tuning::SearchParameters& tune) noexcept {

    if (tune.iirReduction <= 0 || ply <= 0 || depth < tune.iirMinDepth
        || depth <= 1 || hasTTMove || inCheck)
        return 0;

    if (!pvNode && !(cutNode ? tune.iirCutNode : tune.iirAllNode))
        return 0;

    // Depth never falls below one; avoid unbounded reductions if callers
    // pass pathological values even though UCI setters validate ranges.
    return std::min(tune.iirReduction, depth - 1);
}

} // namespace ironphoenix::IIR
