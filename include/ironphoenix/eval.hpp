#pragma once

#include "tuning.hpp"

namespace ironphoenix {

    class Position;

    namespace Eval {

        using Parameters = Tuning::EvalParameters;

        [[nodiscard]] Parameters& parameters() noexcept;
        [[nodiscard]] int evaluate(const Position& pos) noexcept;

    }
}
