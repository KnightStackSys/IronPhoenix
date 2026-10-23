#pragma once

#include "bitboard.hpp"

namespace ironphoenix {
class Position;
namespace Eval {
    // Red+Yellow advantage minus Blue+Green advantage.
    // Supplied attack maps are collected by the existing mobility evaluator.
    [[nodiscard]] int kingFeatureDifference(const Position& pos,
        const Bitboard (&teamAttacks)[2]) noexcept;
}
}
