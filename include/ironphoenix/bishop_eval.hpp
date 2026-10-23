#pragma once

#include "bitboard.hpp"

namespace ironphoenix {
class Position;
namespace Eval {
// Signed value from one team's perspective. Handcrafted evaluation only.
int bishopFeatureDifference(const Position& pos, const Bitboard bishopAttacks[2][2]) noexcept;
}
}
