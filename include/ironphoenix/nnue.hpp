#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ironphoenix {

class Position;

namespace NNUE {

constexpr std::size_t KING_BUCKETS = 17;
constexpr std::size_t RELATIVE_COLORS = 4;
constexpr std::size_t NNUE_PIECE_TYPES = 6;
constexpr std::size_t FEATURE_COUNT = KING_BUCKETS * RELATIVE_COLORS * NNUE_PIECE_TYPES * SQUARE_NB;
constexpr std::size_t FT_SIZE = 128;
constexpr std::size_t DENSE_INPUT = FT_SIZE * 2;
constexpr std::size_t HIDDEN1_SIZE = 32;
constexpr std::size_t HIDDEN2_SIZE = 32;

constexpr int ACTIVATION_MAX = 127;
constexpr int HIDDEN_SHIFT = 6;
constexpr int OUTPUT_DIVISOR = 64;
constexpr int MAX_STATIC_EVAL = 28000;

// PhoenixNet evaluates from the current side-to-move player's perspective.
// Positive values mean an advantage for side-to-move's team.
[[nodiscard]] bool loadNetwork(const std::string& path) noexcept;
void unloadNetwork() noexcept;
[[nodiscard]] bool loaded() noexcept;
[[nodiscard]] const std::string& loadedPath() noexcept;

[[nodiscard]] int evaluate(const Position& pos) noexcept;

// Public helpers are useful for dataset generation and correctness tests.
[[nodiscard]] Square canonicalSquare(Color perspective, Square sq) noexcept;
[[nodiscard]] unsigned relativeColor(Color perspective, Color pieceColor) noexcept;
[[nodiscard]] unsigned kingBucket(Color perspective, Square kingSq) noexcept;
[[nodiscard]] std::size_t featureIndex(
    Color perspective,
    Square anchorKing,
    Piece piece,
    Square pieceSq) noexcept;

} // namespace NNUE
} // namespace ironphoenix
