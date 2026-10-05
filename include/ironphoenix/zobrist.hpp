#pragma once

#include "types.hpp"

#include <array>

namespace ironphoenix::Zobrist {

extern std::array<std::array<Key, SQUARE_NB>, 32> PieceSquare;
extern std::array<Key, COLOR_NB> Side;
extern std::array<std::array<Key, SQUARE_NB>, COLOR_NB> EnPassant;
extern std::array<Key, 8> CastleRight;
extern std::array<Key, COLOR_NB> Alive;
extern std::array<Key, 16> Ruleset;

void init();

}
