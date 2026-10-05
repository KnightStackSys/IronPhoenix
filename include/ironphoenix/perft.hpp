#pragma once

#include "movegen.hpp"

#include <cstdint>
#include <iosfwd>

namespace ironphoenix {

struct PerftResult {
    std::uint64_t nodes = 0;
    std::uint64_t captures = 0;
    std::uint64_t enPassants = 0;
    std::uint64_t castles = 0;
    std::uint64_t promotions = 0;
    std::uint64_t checks = 0;
    std::uint64_t kingCaptures = 0;
};

[[nodiscard]] std::uint64_t perft(Position& pos, int depth);
[[nodiscard]] PerftResult perftDetailed(Position& pos, int depth);
[[nodiscard]] std::uint64_t perftDivide(Position& pos, int depth, std::ostream& out);
[[nodiscard]] bool runPerftSelfTests(std::ostream& out);

}
