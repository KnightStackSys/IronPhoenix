#include "ironphoenix/history.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ironphoenix {

HistoryTables::HistoryTables()
    : quiet_(std::make_unique<std::int16_t[]>(QUIET_COUNT)),
      capture_(std::make_unique<std::int16_t[]>(CAPTURE_COUNT)),
      continuationPrevious_(std::make_unique<std::int16_t[]>(CONTINUATION_COUNT)),
      continuationSamePlayer_(std::make_unique<std::int16_t[]>(CONTINUATION_COUNT)),
      counter_(std::make_unique<std::uint32_t[]>(COUNTER_COUNT)) {
    clear();
}

void HistoryTables::clear() noexcept {
    std::memset(quiet_.get(), 0, QUIET_COUNT * sizeof(std::int16_t));
    std::memset(capture_.get(), 0, CAPTURE_COUNT * sizeof(std::int16_t));
    std::memset(continuationPrevious_.get(), 0, CONTINUATION_COUNT * sizeof(std::int16_t));
    std::memset(continuationSamePlayer_.get(), 0, CONTINUATION_COUNT * sizeof(std::int16_t));
    std::memset(counter_.get(), 0, COUNTER_COUNT * sizeof(std::uint32_t));
}

std::size_t HistoryTables::quietIndex(Color mover, Move move) noexcept {
    return (static_cast<std::size_t>(mover) * SQUARE_NB + move.from()) * SQUARE_NB + move.to();
}

std::size_t HistoryTables::captureIndex(Color mover, Piece attacker, Move move) noexcept {
    const Piece victim = move.captured();
    const unsigned attackerType = static_cast<unsigned>(typeOf(attacker));
    const unsigned victimType = isPiece(victim) ? static_cast<unsigned>(typeOf(victim)) : 0u;
    const unsigned victimColor = isPiece(victim) ? static_cast<unsigned>(colorOf(victim)) : 0u;

    std::size_t idx = static_cast<std::size_t>(mover);
    idx = idx * PIECE_TYPE_NB + attackerType;
    idx = idx * SQUARE_NB + move.to();
    idx = idx * COLOR_NB + victimColor;
    idx = idx * PIECE_TYPE_NB + victimType;
    return idx;
}

std::size_t HistoryTables::continuationIndex(const HistoryContext& previous,
                                             Piece currentPiece,
                                             Square currentTo) noexcept {
    std::size_t idx = static_cast<std::size_t>(previous.mover);
    idx = idx * PIECE_TYPE_NB + static_cast<unsigned>(typeOf(previous.piece));
    idx = idx * SQUARE_NB + previous.move.to();
    idx = idx * PIECE_TYPE_NB + static_cast<unsigned>(typeOf(currentPiece));
    idx = idx * SQUARE_NB + currentTo;
    return idx;
}

std::size_t HistoryTables::counterIndex(Color responder,
                                        const HistoryContext& previous) noexcept {
    std::size_t idx = static_cast<std::size_t>(responder);
    idx = idx * COLOR_NB + static_cast<unsigned>(previous.mover);
    idx = idx * PIECE_TYPE_NB + static_cast<unsigned>(typeOf(previous.piece));
    idx = idx * SQUARE_NB + previous.move.to();
    return idx;
}

void HistoryTables::updateEntry(std::int16_t& entry, int bonus) noexcept {
    bonus = std::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
    int value = static_cast<int>(entry);
    
    value += bonus - value * std::abs(bonus) / MAX_HISTORY;
    entry = static_cast<std::int16_t>(std::clamp(value, -MAX_HISTORY, MAX_HISTORY));
}

int HistoryTables::quietScore(Color mover, Move move) const noexcept {
    return quiet_[quietIndex(mover, move)];
}

int HistoryTables::captureScore(Color mover, Piece attacker, Move move) const noexcept {
    if (!isPiece(attacker) || !move.isCapture())
        return 0;
    return capture_[captureIndex(mover, attacker, move)];
}

int HistoryTables::continuationScore(ContinuationKind kind,
                                     const HistoryContext& previous,
                                     Piece currentPiece,
                                     Square currentTo) const noexcept {
    if (!previous.valid() || !isPiece(currentPiece))
        return 0;
    const std::size_t idx = continuationIndex(previous, currentPiece, currentTo);
    return kind == ContinuationKind::PreviousPly
        ? continuationPrevious_[idx]
        : continuationSamePlayer_[idx];
}

Move HistoryTables::counterMove(Color responder, const HistoryContext& previous) const noexcept {
    if (!previous.valid())
        return Move{};
    return Move(counter_[counterIndex(responder, previous)]);
}

void HistoryTables::updateQuiet(Color mover, Move move, int bonus) noexcept {
    updateEntry(quiet_[quietIndex(mover, move)], bonus);
}

void HistoryTables::updateCapture(Color mover, Piece attacker, Move move, int bonus) noexcept {
    if (!isPiece(attacker) || !move.isCapture())
        return;
    updateEntry(capture_[captureIndex(mover, attacker, move)], bonus);
}

void HistoryTables::updateContinuation(ContinuationKind kind,
                                       const HistoryContext& previous,
                                       Piece currentPiece,
                                       Square currentTo,
                                       int bonus) noexcept {
    if (!previous.valid() || !isPiece(currentPiece))
        return;
    const std::size_t idx = continuationIndex(previous, currentPiece, currentTo);
    if (kind == ContinuationKind::PreviousPly)
        updateEntry(continuationPrevious_[idx], bonus);
    else
        updateEntry(continuationSamePlayer_[idx], bonus);
}

void HistoryTables::setCounterMove(Color responder,
                                   const HistoryContext& previous,
                                   Move move) noexcept {
    if (!previous.valid())
        return;
    counter_[counterIndex(responder, previous)] = move.v;
}

}
