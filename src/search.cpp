#include "ironphoenix/search.hpp"

#include "ironphoenix/movegen.hpp"
#include "ironphoenix/see.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>

namespace ironphoenix {
namespace {

constexpr std::array<int, PIECE_TYPE_NB> EVAL_VALUE = {
    0, 100, 300, 400, 500, 1000, 0
};

}

SearchEngine::SearchEngine() = default;

SearchEngine::~SearchEngine() {
    stopAndWait();
}

bool SearchEngine::setHashSizeMB(std::size_t megabytes) {
    stopAndWait();
    return tt_.resizeMB(megabytes);
}

void SearchEngine::clearHash() noexcept {
    stopAndWait();
    tt_.clear();
}

void SearchEngine::newGame() noexcept {
    stopAndWait();
    tt_.clear();
}

void SearchEngine::start(Position position, SearchLimits limits, std::ostream& out) {
    stopAndWait();
    stopRequested_.store(false, std::memory_order_relaxed);
    searching_.store(true, std::memory_order_relaxed);
    tt_.newGeneration();

    worker_ = std::thread([this, position = std::move(position), limits, &out]() mutable {
        limits_ = limits;
        run(std::move(position), out);
        searching_.store(false, std::memory_order_relaxed);
    });
}

void SearchEngine::stop() noexcept {
    stopRequested_.store(true, std::memory_order_relaxed);
}

void SearchEngine::stopAndWait() noexcept {
    stop();
    if (worker_.joinable())
        worker_.join();
    searching_.store(false, std::memory_order_relaxed);
}

std::int64_t SearchEngine::elapsedMs() const noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime_).count();
}

std::int64_t SearchEngine::calculateTimeBudget(const Position& pos, const SearchLimits& limits) const noexcept {
    if (limits.movetimeMs > 0)
        return limits.movetimeMs;

    if (!limits.hasClock)
        return 0;

    const unsigned c = static_cast<unsigned>(pos.sideToMove());
    const std::int64_t remaining = std::max<std::int64_t>(0, limits.timeMs[c]);
    const std::int64_t increment = std::max<std::int64_t>(0, limits.incrementMs[c]);
    const std::int64_t delay = std::max<std::int64_t>(0, limits.delayMs[c]);

    std::int64_t budget = delay + remaining / 30 + (increment * 3) / 4;
    const std::int64_t cap = delay + std::max<std::int64_t>(1, remaining / 2);
    budget = std::min(budget, cap);

    if (remaining > 20)
        budget = std::min(budget, delay + remaining - 10);

    return std::max<std::int64_t>(1, budget);
}

bool SearchEngine::shouldStop() noexcept {
    if (stopRequested_.load(std::memory_order_relaxed))
        return true;

    if (limits_.nodes > 0 && nodes_ >= limits_.nodes) {
        stopRequested_.store(true, std::memory_order_relaxed);
        return true;
    }

    if (timeBudgetMs_ > 0 && elapsedMs() >= timeBudgetMs_) {
        stopRequested_.store(true, std::memory_order_relaxed);
        return true;
    }

    return false;
}

int SearchEngine::evaluate(const Position& pos) const noexcept {
    int team0 = 0;
    int team1 = 0;

    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color c = static_cast<Color>(ci);
        int material = 0;
        for (unsigned pt = PAWN; pt <= QUEEN; ++pt)
            material += pos.pieces(c, static_cast<PieceType>(pt)).popcount() * EVAL_VALUE[pt];

        if (teamOf(c) == 0)
            team0 += material;
        else
            team1 += material;
    }

    const int score = team0 - team1;

    return teamOf(pos.sideToMove()) == 0 ? score : -score;
}

int SearchEngine::moveScore(const Position& pos, Move move, Move ttMove) const noexcept {
    if (move == ttMove)
        return 10'000'000;
    if (isTerminalKingCapture(pos, move))
        return 9'000'000;

    int score = 0;
    if (move.isPromotion())
        score += 8'000'000 + pieceValue(promotedType(move.promotion()));

    if (move.isCapture()) {
        const Piece attacker = pos.pieceAt(move.from());
        score += 7'000'000 + see(pos, move) * 16 - pieceValue(attacker);
    }
    else if (pos.givesCheck(move)) {
        score += 6'000'000;
    }

    return score;
}

void SearchEngine::orderMoves(const Position& pos, Move* begin, Move* end, Move ttMove) const {
    std::array<std::pair<int, Move>, MAX_MOVES> scored{};
    const std::size_t count = static_cast<std::size_t>(end - begin);
    for (std::size_t i = 0; i < count; ++i)
        scored[i] = { moveScore(pos, begin[i], ttMove), begin[i] };

    std::sort(scored.begin(), scored.begin() + count,
        [](const auto& a, const auto& b) { return a.first > b.first; });

    for (std::size_t i = 0; i < count; ++i)
        begin[i] = scored[i].second;
}

int SearchEngine::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode) {
    if (ply >= MAX_PLY - 1)
        return evaluate(pos);
    if (shouldStop())
        return 0;

    ++nodes_;
    selDepth_ = std::max(selDepth_, ply);
    pvLength_[ply] = ply;

    if (depth <= 0)
        return evaluate(pos);

    const int originalAlpha = alpha;
    TTProbe ttProbe;
    const bool ttHit = tt_.probe(pos.key(), ply, ttProbe);
    const Move ttMove = ttHit ? ttProbe.move : Move{};

    if (ply > 0 && ttHit && ttProbe.depth >= depth) {
        if (ttProbe.bound == TTBound::Exact)
            return ttProbe.score;
        if (ttProbe.bound == TTBound::Lower && ttProbe.score >= beta)
            return ttProbe.score;
        if (ttProbe.bound == TTBound::Upper && ttProbe.score <= alpha)
            return ttProbe.score;
    }

    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    orderMoves(pos, moves.begin(), moves.end(), ttMove);

    const Color us = pos.sideToMove();
    int legalMoves = 0;
    int bestScore = -INF;
    Move bestMove{};

    for (Move move : moves) {
        if (shouldStop())
            break;

        int score = -INF;
        bool legal = true;

        if (isTerminalKingCapture(pos, move)) {
            score = MATE_SCORE - (ply + 1);
            pvLength_[ply + 1] = ply + 1;
        }
        else {
            StateInfo st;
            pos.makeMove(move, st);
            if (pos.inCheck(us)) {
                legal = false;
            }
            else {
                ++legalMoves;
                if (legalMoves == 1) {
                    score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
                }
                else {
                    score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
                    if (!shouldStop() && score > alpha && score < beta)
                        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
                }
            }
            pos.undoMove(move, st);
        }

        if (!legal)
            continue;
        if (isTerminalKingCapture(pos, move))
            ++legalMoves;

        if (shouldStop())
            break;

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }

        if (score > alpha) {
            alpha = score;
            pv_[ply][ply] = move;
            for (int i = ply + 1; i < pvLength_[ply + 1]; ++i)
                pv_[ply][i] = pv_[ply + 1][i];
            pvLength_[ply] = std::max(ply + 1, pvLength_[ply + 1]);

            if (alpha >= beta)
                break;
        }
    }

    if (shouldStop())
        return bestScore == -INF ? 0 : bestScore;

    if (legalMoves == 0) {
        return pos.inCheck() ? -MATE_SCORE + ply : 0;
    }

    TTBound bound = TTBound::Exact;
    if (bestScore <= originalAlpha)
        bound = TTBound::Upper;
    else if (bestScore >= beta)
        bound = TTBound::Lower;

    tt_.store(pos.key(), depth, bestScore, bound, bestMove, pvNode, ply);
    return bestScore;
}

void SearchEngine::emitInfo(std::ostream& out, int depth, int score, const Move* pv, int pvLength) {
    const std::int64_t ms = elapsedMs();
    const std::uint64_t nps = ms > 0 ? nodes_ * 1000ull / static_cast<std::uint64_t>(ms) : nodes_ * 1000ull;

    std::ostringstream line;
    line << "info depth " << depth
         << " seldepth " << selDepth_;

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

void SearchEngine::emitBestMove(std::ostream& out, Move move) {
    std::lock_guard lock(outputMutex_);
    out << "bestmove " << (move ? moveToString(move) : "0000") << '\n';
    out.flush();
}

void SearchEngine::run(Position position, std::ostream& out) {
    startTime_ = std::chrono::steady_clock::now();
    timeBudgetMs_ = calculateTimeBudget(position, limits_);
    nodes_ = 0;
    selDepth_ = 0;
    for (auto& row : pv_)
        row.fill(Move{});
    pvLength_.fill(0);

    MoveList rootLegal;
    generateLegalMoves(position, rootLegal);
    Move bestMove = rootLegal.size ? rootLegal.moves[0] : Move{};
    int bestScore = 0;

    if (rootLegal.size == 0) {
        emitBestMove(out, Move{});
        return;
    }

    const int requestedDepth = limits_.depth > 0 ? std::min(limits_.depth, MAX_PLY - 1) : 0;
    int depth = 1;

    while (!shouldStop()) {
        if (requestedDepth > 0 && depth > requestedDepth)
            break;

        if (!limits_.infinite && requestedDepth == 0 && limits_.nodes == 0
            && limits_.movetimeMs == 0 && !limits_.hasClock && depth > 8)
            break;

        selDepth_ = 0;
        pvLength_[0] = 0;
        const int score = negamax(position, depth, -INF, INF, 0, true);

        if (shouldStop())
            break;

        if (pvLength_[0] > 0 && pv_[0][0]) {
            bestMove = pv_[0][0];
            bestScore = score;
            emitInfo(out, depth, bestScore, pv_[0].data(), pvLength_[0]);
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

}
