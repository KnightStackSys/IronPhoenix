#pragma once

#include "move.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ironphoenix {

enum class ContinuationKind : std::uint8_t { PreviousPly = 0, SamePlayer = 1 };

struct HistoryContext {
    Move move{};
    Piece piece = NO_PIECE;
    Color mover = RED;

    [[nodiscard]] IRONPHOENIX_FORCE_INLINE constexpr bool valid() const noexcept {
        return static_cast<bool>(move) && isPiece(piece);
    }
};

class HistoryTables {
public:
    static constexpr int MAX_HISTORY = 16384;

    HistoryTables();
    ~HistoryTables() = default;

    HistoryTables(const HistoryTables&) = delete;
    HistoryTables& operator=(const HistoryTables&) = delete;

    void clear() noexcept;

    [[nodiscard]] int quietScore(Color mover, Move move) const noexcept;
    // Pawn-history: condition ANY quiet move on all four players' pawn
    // structures. This is not history exclusive to pawn moves.
    [[nodiscard]] int pawnScore(Key pawnKey, Piece movingPiece, Square to) const noexcept;
    [[nodiscard]] int captureScore(Color mover, Piece attacker, Move move) const noexcept;
    [[nodiscard]] int continuationScore(ContinuationKind kind,
                                        const HistoryContext& previous,
                                        Piece currentPiece,
                                        Square currentTo) const noexcept;
    [[nodiscard]] Move counterMove(Color responder, const HistoryContext& previous) const noexcept;

    void updateQuiet(Color mover, Move move, int bonus) noexcept;
    void updatePawn(Key pawnKey, Piece movingPiece, Square to, int bonus) noexcept;
    void updateCapture(Color mover, Piece attacker, Move move, int bonus) noexcept;
    void updateContinuation(ContinuationKind kind,
                            const HistoryContext& previous,
                            Piece currentPiece,
                            Square currentTo,
                            int bonus) noexcept;
    void setCounterMove(Color responder, const HistoryContext& previous, Move move) noexcept;

private:
    static constexpr std::size_t QUIET_COUNT =
        static_cast<std::size_t>(COLOR_NB) * SQUARE_NB * SQUARE_NB;

    // 256 structure buckets x 32 color-coded piece IDs x 160 squares,
    // approximately 2.5 MiB of saturated 16-bit entries.
    static constexpr std::size_t PAWN_BUCKETS = 256;
    static constexpr std::size_t PAWN_COUNT = PAWN_BUCKETS * 32 * SQUARE_NB;

    static constexpr std::size_t CAPTURE_COUNT =
        static_cast<std::size_t>(COLOR_NB) * PIECE_TYPE_NB * SQUARE_NB
        * COLOR_NB * PIECE_TYPE_NB;

    static constexpr std::size_t CONTINUATION_COUNT =
        static_cast<std::size_t>(COLOR_NB) * PIECE_TYPE_NB * SQUARE_NB
        * PIECE_TYPE_NB * SQUARE_NB;

    static constexpr std::size_t COUNTER_COUNT =
        static_cast<std::size_t>(COLOR_NB) * COLOR_NB * PIECE_TYPE_NB * SQUARE_NB;

    std::unique_ptr<std::int16_t[]> quiet_;
    std::unique_ptr<std::int16_t[]> pawn_;
    std::unique_ptr<std::int16_t[]> capture_;
    std::unique_ptr<std::int16_t[]> continuationPrevious_;
    std::unique_ptr<std::int16_t[]> continuationSamePlayer_;
    std::unique_ptr<std::uint32_t[]> counter_;

    [[nodiscard]] static std::size_t quietIndex(Color mover, Move move) noexcept;
    [[nodiscard]] static std::size_t pawnIndex(Key pawnKey, Piece movingPiece, Square to) noexcept;
    [[nodiscard]] static std::size_t captureIndex(Color mover, Piece attacker, Move move) noexcept;
    [[nodiscard]] static std::size_t continuationIndex(const HistoryContext& previous,
                                                       Piece currentPiece,
                                                       Square currentTo) noexcept;
    [[nodiscard]] static std::size_t counterIndex(Color responder,
                                                  const HistoryContext& previous) noexcept;

    static void updateEntry(std::int16_t& entry, int bonus) noexcept;
};

}
