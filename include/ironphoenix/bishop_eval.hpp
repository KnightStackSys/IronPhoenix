#pragma once

namespace ironphoenix {
class Position;
namespace Eval {
// Signed value from one team's perspective. Handcrafted evaluation only.
int bishopFeaturesForTeam(const Position& pos, int team) noexcept;
}
}
