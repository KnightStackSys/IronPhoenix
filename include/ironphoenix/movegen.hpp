#pragma once

#include "position.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace ironphoenix {

constexpr std::size_t MAX_MOVES = 512;

struct MoveList {
    std::array<Move, MAX_MOVES> moves{};
    std::size_t size = 0;

    IRONPHOENIX_FORCE_INLINE void clear() noexcept { size = 0; }
    IRONPHOENIX_FORCE_INLINE void push(Move m) noexcept {
        if (size < moves.size())
            moves[size++] = m;
    }
    IRONPHOENIX_FORCE_INLINE Move* begin() noexcept { return moves.data(); }
    IRONPHOENIX_FORCE_INLINE Move* end() noexcept { return moves.data() + size; }
    IRONPHOENIX_FORCE_INLINE const Move* begin() const noexcept { return moves.data(); }
    IRONPHOENIX_FORCE_INLINE const Move* end() const noexcept { return moves.data() + size; }
};

void generatePseudoLegalMoves(const Position& pos, MoveList& out);
void generateLegalMoves(Position& pos, MoveList& out);

[[nodiscard]] bool isTerminalKingCapture(const Position& pos, Move m) noexcept;
[[nodiscard]] std::string moveToString(Move m);

[[nodiscard]] bool parseLegalMove(Position& pos,
                                  std::string_view text,
                                  Move& move,
                                  std::string& error);

}
