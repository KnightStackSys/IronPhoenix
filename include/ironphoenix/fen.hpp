#pragma once

#include "position.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ironphoenix {

struct Fen4State {
    std::array<int, COLOR_NB> points{0, 0, 0, 0};
    std::uint32_t halfmoveClock = 0;
    std::string metadata;
};

[[nodiscard]] bool setFromFen4(Position& pos,
                               std::string_view fen,
                               Fen4State& state,
                               std::string& error);

[[nodiscard]] std::string toFen4(const Position& pos, const Fen4State& state = {});

[[nodiscard]] std::string squareToString(Square sq);
[[nodiscard]] bool stringToSquare(std::string_view text, Square& sq, std::size_t& consumed);

}
