#pragma once

namespace ironphoenix {

    class Position;

    namespace Eval {

        struct Parameters final {

        };

        [[nodiscard]] int evaluate(const Position& pos) noexcept;

    }
}
