#include "ironphoenix/search.hpp"

#include "ironphoenix/movegen.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ironphoenix {
namespace {

constexpr std::size_t ROOT_PV_CAPACITY = 128;

struct RootLine final {
    Move move{};
    int score = 0;
    std::array<Move, ROOT_PV_CAPACITY> pv{};
    int pvLength = 0;
};

} // namespace

void SearchEngine::startMultiPV(
    Position position,
    SearchLimits limits,
    int multiPV,
    std::ostream& out) {

    if (multiPV <= 1) {
        start(std::move(position), limits, out);
        return;
    }

    multiPV = std::clamp(multiPV, 2, MAX_MULTI_PV);

    stopAndWait();
    stopRequested_.store(false, std::memory_order_relaxed);
    searching_.store(true, std::memory_order_relaxed);
    tt_.newGeneration();

    worker_ = std::thread([
        this,
        position = std::move(position),
        limits,
        multiPV,
        &out]() mutable {

        limits_ = limits;
        runMultiPV(std::move(position), multiPV, out);
        searching_.store(false, std::memory_order_relaxed);
    });
}

void SearchEngine::emitMultiPVInfo(
    std::ostream& out,
    int depth,
    int multiPvIndex,
    int score,
    const Move* pv,
    int pvLength) {

    const std::int64_t ms = elapsedMs();
    const std::uint64_t nps = ms > 0
        ? nodes_ * 1000ull / static_cast<std::uint64_t>(ms)
        : nodes_ * 1000ull;

    std::ostringstream line;
    line << "info depth " << depth
        << " seldepth " << selDepth_
        << " multipv " << multiPvIndex;

    if (score >= MATE_THRESHOLD) {
        line << " score mate " << (MATE_SCORE - score);
    }
    else if (score <= -MATE_THRESHOLD) {
        line << " score mate -" << (MATE_SCORE + score);
    }
    else {
        line << " score cp " << score;
    }

    line << " nodes " << nodes_
        << " nps " << nps
        << " hashfull " << tt_.hashfull()
        << " time " << ms
        << " pv";

    for (int i = 0; i < pvLength; ++i) {
        if (!pv[i])
            break;
        line << ' ' << moveToString(pv[i]);
    }

    std::lock_guard lock(outputMutex_);
    out << line.str() << '\n';
    out.flush();
}

void SearchEngine::runMultiPV(Position position, int multiPV, std::ostream& out) {
    startTime_ = std::chrono::steady_clock::now();
    timeBudgetMs_ = calculateTimeBudget(position, limits_);
    nodes_ = 0;
    selDepth_ = 0;

    for (auto& row : pv_)
        row.fill(Move{});
    pvLength_.fill(0);
    searchStack_.fill(HistoryContext{});

    MoveList rootLegal;
    generateLegalMoves(position, rootLegal);

    if (rootLegal.size == 0) {
        emitBestMove(out, Move{});
        return;
    }

    // Initial move ordering still benefits from TT/history/check/capture ordering.
    orderMoves(position, rootLegal.begin(), rootLegal.end(), Move{}, 0);

    const int requestedLines = std::min<int>(multiPV, static_cast<int>(rootLegal.size));
    const int requestedDepth = limits_.depth > 0
        ? std::min(limits_.depth, MAX_PLY - 1)
        : 0;

    Move bestMove = rootLegal.moves[0];
    std::vector<RootLine> lastComplete;
    int depth = 1;

    while (!shouldStop()) {
        if (requestedDepth > 0 && depth > requestedDepth)
            break;

        if (!limits_.infinite
            && requestedDepth == 0
            && limits_.nodes == 0
            && limits_.movetimeMs == 0
            && !limits_.hasClock
            && depth > 8) {
            break;
        }

        // Previous-iteration score ordering is especially important for
        // MultiPV because every legal root move receives a full-window search.
        if (!lastComplete.empty()) {
            for (std::size_t i = 0; i < lastComplete.size() && i < rootLegal.size; ++i)
                rootLegal.moves[i] = lastComplete[i].move;
        }

        selDepth_ = 0;
        std::vector<RootLine> current;
        current.reserve(rootLegal.size);
        bool completedDepth = true;

        for (Move move : rootLegal) {
            if (shouldStop()) {
                completedDepth = false;
                break;
            }

            RootLine line;
            line.move = move;
            line.pv[0] = move;
            line.pvLength = 1;

            if (isTerminalKingCapture(position, move)) {
                line.score = MATE_SCORE - 1;
            }
            else {
                const Color mover = position.sideToMove();
                StateInfo state;
                position.makeMove(move, state);

                // rootLegal already contains only legal moves, but keeping this
                // guard protects MultiPV if move generation rules evolve.
                if (position.inCheck(mover)) {
                    position.undoMove(move, state);
                    continue;
                }

                searchStack_[1] = HistoryContext{
                    move,
                    position.pieceAt(move.to()),
                    mover
                };

                pvLength_[1] = 1;
                line.score = -negamax(
                    position,
                    depth - 1,
                    -INF,
                    INF,
                    1,
                    true
                );

                if (!shouldStop()) {
                    const int childLength = std::clamp(pvLength_[1], 1, MAX_PLY);
                    for (int i = 1; i < childLength; ++i)
                        line.pv[static_cast<std::size_t>(i)] = pv_[1][i];
                    line.pvLength = childLength;
                }

                position.undoMove(move, state);

                if (shouldStop()) {
                    completedDepth = false;
                    break;
                }
            }

            current.push_back(line);
        }

        if (!completedDepth || current.size() != rootLegal.size)
            break;

        std::stable_sort(
            current.begin(),
            current.end(),
            [](const RootLine& a, const RootLine& b) {
                if (a.score != b.score)
                    return a.score > b.score;
                return a.move.v < b.move.v;
            });

        lastComplete = current;
        bestMove = lastComplete.front().move;

        const int lineCount = std::min<int>(requestedLines, static_cast<int>(lastComplete.size()));
        for (int i = 0; i < lineCount; ++i) {
            const RootLine& line = lastComplete[static_cast<std::size_t>(i)];
            emitMultiPVInfo(
                out,
                depth,
                i + 1,
                line.score,
                line.pv.data(),
                line.pvLength
            );
        }

        if (requestedDepth > 0 && depth == requestedDepth)
            break;
        if (limits_.nodes > 0 && nodes_ >= limits_.nodes)
            break;
        if (timeBudgetMs_ > 0 && elapsedMs() >= timeBudgetMs_)
            break;

        if (depth < MAX_PLY - 1)
            ++depth;
        else if (!limits_.infinite)
            break;
    }

    emitBestMove(out, bestMove);
}

} // namespace ironphoenix
