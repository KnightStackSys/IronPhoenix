#include "ironphoenix/search.hpp"

#include "ironphoenix/lmr.hpp"

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

        constexpr std::array<int, PIECE_TYPE_NB> MOBILITY_WEIGHT = {
            0, 0, 4, 4, 2, 1, 0
        };

        constexpr int ASPIRATION_START_DEPTH = 4;
        constexpr int ASPIRATION_INITIAL_DELTA = 50;

        constexpr int SEE_PRUNE_MAX_DEPTH = 4;
        constexpr int SEE_PRUNE_MARGIN_PER_DEPTH = 80;
        constexpr int QSEARCH_SEE_THRESHOLD = -50;

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        int mobilityForColor(const Position& pos, Color c) noexcept {
            const Bitboard occ = pos.occupancy();
            const Bitboard blockedByTeam = teamOccupancy(pos, c);
            int score = 0;

            auto scorePieces = [&](PieceType pt) {
                Bitboard pieces = pos.pieces(c, pt);
                while (pieces) {
                    const Square sq = popLsb(pieces);
                    Bitboard attacks{};

                    switch (pt) {
                    case KNIGHT:
                        attacks = Geometry::KnightAttacks[sq];
                        break;
                    case BISHOP:
                        attacks = Geometry::bishopAttacks(sq, occ);
                        break;
                    case ROOK:
                        attacks = Geometry::rookAttacks(sq, occ);
                        break;
                    case QUEEN:
                        attacks = Geometry::queenAttacks(sq, occ);
                        break;
                    default:
                        break;
                    }

                    const int mobility = (attacks & ~blockedByTeam).popcount();
                    score += mobility * MOBILITY_WEIGHT[static_cast<unsigned>(pt)];
                }
                };

            scorePieces(KNIGHT);
            scorePieces(BISHOP);
            scorePieces(ROOK);
            scorePieces(QUEEN);

            return score;
        }

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
        history_.clear();
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

            const int playerScore = material + mobilityForColor(pos, c);

            if (teamOf(c) == 0)
                team0 += playerScore;
            else
                team1 += playerScore;
        }

        const int score = team0 - team1;

        return teamOf(pos.sideToMove()) == 0 ? score : -score;
    }

    int SearchEngine::historyBonus(int depth) const noexcept {
        return std::min(2048, 64 * depth * depth);
    }

    int SearchEngine::moveScore(const Position& pos, Move move, Move ttMove, int ply) const noexcept {
        if (move == ttMove)
            return 50'000'000;
        if (isTerminalKingCapture(pos, move))
            return 45'000'000;

        const Color mover = pos.sideToMove();
        const Piece movingPiece = pos.pieceAt(move.from());

        if (move.isPromotion()) {
            int score = 30'000'000 + pieceValue(promotedType(move.promotion())) * 128;
            if (move.isCapture()) {
                score += std::clamp(see(pos, move), -5000, 5000) * 64;
                score += history_.captureScore(mover, movingPiece, move) * 2;
            }
            return score;
        }

        if (move.isCapture()) {
            int score = 20'000'000;
            score += std::clamp(see(pos, move), -5000, 5000) * 64;
            score += history_.captureScore(mover, movingPiece, move) * 2;
            return score;
        }

        int score = history_.quietScore(mover, move);

        if (ply > 0 && searchStack_[ply].valid()) {
            score += history_.continuationScore(ContinuationKind::PreviousPly, searchStack_[ply], movingPiece, move.to());

            if (history_.counterMove(mover, searchStack_[ply]) == move)
                score += 32'000;
        }

        if (ply >= 4 && searchStack_[ply - 3].valid()
            && searchStack_[ply - 3].mover == mover) {
            score += (history_.continuationScore(ContinuationKind::SamePlayer, searchStack_[ply - 3], movingPiece, move.to()) * 3) / 4;
        }

        if (pos.givesCheck(move))
            score += 10'000'000;

        return score;
    }

    void SearchEngine::orderMoves(const Position& pos, Move* begin, Move* end, Move ttMove, int ply) const {
        std::array<std::pair<int, Move>, MAX_MOVES> scored{};
        const std::size_t count = static_cast<std::size_t>(end - begin);
        for (std::size_t i = 0; i < count; ++i)
            scored[i] = { moveScore(pos, begin[i], ttMove, ply), begin[i] };

        std::sort(scored.begin(), scored.begin() + count,
            [](const auto& a, const auto& b) { return a.first > b.first; });

        for (std::size_t i = 0; i < count; ++i)
            begin[i] = scored[i].second;
    }

    void SearchEngine::updateQuietCutoff(const Position& pos,
        Color mover,
        Move cutoffMove,
        Piece movingPiece,
        int depth,
        int ply,
        const Move* failedQuiets,
        std::size_t failedQuietCount) noexcept {
        const int bonus = historyBonus(depth);
        const int penalty = -std::max(32, bonus / 2);

        history_.updateQuiet(mover, cutoffMove, bonus);

        if (ply > 0 && searchStack_[ply].valid()) {
            history_.updateContinuation(ContinuationKind::PreviousPly, searchStack_[ply], movingPiece, cutoffMove.to(), bonus);
            history_.setCounterMove(mover, searchStack_[ply], cutoffMove);
        }

        if (ply >= 4 && searchStack_[ply - 3].valid()
            && searchStack_[ply - 3].mover == mover) {
            history_.updateContinuation(ContinuationKind::SamePlayer, searchStack_[ply - 3], movingPiece,
                cutoffMove.to(), (bonus * 3) / 4);
        }

        for (std::size_t i = 0; i < failedQuietCount; ++i) {
            const Move failed = failedQuiets[i];
            const Piece failedPiece = pos.pieceAt(failed.from());
            if (!isPiece(failedPiece))
                continue;

            history_.updateQuiet(mover, failed, penalty);
            if (ply > 0 && searchStack_[ply].valid())
                history_.updateContinuation(ContinuationKind::PreviousPly, searchStack_[ply], failedPiece, failed.to(), penalty);

            if (ply >= 4 && searchStack_[ply - 3].valid()
                && searchStack_[ply - 3].mover == mover) {
                history_.updateContinuation(ContinuationKind::SamePlayer, searchStack_[ply - 3], failedPiece,
                    failed.to(), (penalty * 3) / 4);
            }
        }
    }

    void SearchEngine::updateCaptureCutoff(Color mover,
        Move cutoffMove,
        Piece movingPiece,
        int depth,
        const Move* failedCaptures,
        std::size_t failedCaptureCount,
        const Position& pos) noexcept {
        const int bonus = historyBonus(depth);
        const int penalty = -std::max(32, bonus / 2);

        history_.updateCapture(mover, movingPiece, cutoffMove, bonus);

        for (std::size_t i = 0; i < failedCaptureCount; ++i) {
            const Move failed = failedCaptures[i];
            const Piece failedPiece = pos.pieceAt(failed.from());
            if (isPiece(failedPiece))
                history_.updateCapture(mover, failedPiece, failed, penalty);
        }
    }

    int SearchEngine::qsearch(Position& pos, int alpha, int beta, int ply, bool pvNode) {
        if (ply >= MAX_PLY - 1)
            return evaluate(pos);
        if (shouldStop())
            return 0;

        ++nodes_;
        selDepth_ = std::max(selDepth_, ply);
        pvLength_[ply] = ply;

        const int originalAlpha = alpha;
        const bool inCheckNode = pos.inCheck();

        TTProbe ttProbe;
        const bool ttHit = tt_.probe(pos.key(), ply, ttProbe);
        const Move ttMove = ttHit ? ttProbe.move : Move{};

        if (!pvNode && ttHit && ttProbe.depth >= 0) {
            if (ttProbe.bound == TTBound::Exact)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Lower && ttProbe.score >= beta)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Upper && ttProbe.score <= alpha)
                return ttProbe.score;
        }

        int bestScore = -INF;
        Move bestMove{};

        if (!inCheckNode) {
            bestScore = evaluate(pos);

            if (bestScore >= beta) {
                tt_.store(pos.key(), 0, bestScore, TTBound::Lower, Move{}, pvNode, ply);
                return bestScore;
            }

            if (bestScore > alpha)
                alpha = bestScore;
        }

        MoveList pseudo;
        generatePseudoLegalMoves(pos, pseudo);

        MoveList moves;
        for (Move move : pseudo) {
            if (inCheckNode
                || move.isCapture()
                || move.isPromotion()
                || isTerminalKingCapture(pos, move)) {
                moves.push(move);
            }
        }

        orderMoves(pos, moves.begin(), moves.end(), ttMove, ply);

        const Color us = pos.sideToMove();
        int legalMoves = 0;

        for (Move move : moves) {
            if (shouldStop())
                break;

            const bool terminalKingCapture = isTerminalKingCapture(pos, move);
            const bool givesCheckMove = !terminalKingCapture && pos.givesCheck(move);

            int score = -INF;
            bool legal = true;

            if (terminalKingCapture) {
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
                    searchStack_[ply + 1] = HistoryContext{ move, pos.pieceAt(move.to()), us };
                    score = -qsearch(pos, -beta, -alpha, ply + 1, pvNode);
                }

                pos.undoMove(move, st);
            }

            if (!legal)
                continue;

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

                if (alpha >= beta) {
                    tt_.store(pos.key(), 0, bestScore, TTBound::Lower, bestMove, pvNode, ply);
                    return bestScore;
                }
            }
        }

        if (shouldStop())
            return bestScore == -INF ? 0 : bestScore;

        if (inCheckNode && legalMoves == 0) {
            const int mateScore = -MATE_SCORE + ply;
            tt_.store(pos.key(), 0, mateScore, TTBound::Exact, Move{}, pvNode, ply);
            return mateScore;
        }

        if (bestScore == -INF)
            bestScore = evaluate(pos);

        TTBound bound = TTBound::Exact;
        if (bestScore <= originalAlpha)
            bound = TTBound::Upper;
        else if (bestScore >= beta)
            bound = TTBound::Lower;

        tt_.store(pos.key(), 0, bestScore, bound, bestMove, pvNode, ply);
        return bestScore;
    }

    int SearchEngine::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode) {
        if (ply >= MAX_PLY - 1)
            return evaluate(pos);
        if (shouldStop())
            return 0;

        if (depth <= 0)
            return qsearch(pos, alpha, beta, ply, pvNode);

        ++nodes_;
        selDepth_ = std::max(selDepth_, ply);
        pvLength_[ply] = ply;

        const int originalAlpha = alpha;
        TTProbe ttProbe;
        const bool ttHit = tt_.probe(pos.key(), ply, ttProbe);
        const Move ttMove = ttHit ? ttProbe.move : Move{};

        if (!pvNode && ply > 0 && ttHit && ttProbe.depth >= depth) {
            if (ttProbe.bound == TTBound::Exact)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Lower && ttProbe.score >= beta)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Upper && ttProbe.score <= alpha)
                return ttProbe.score;
        }

        const bool inCheckNode = pos.inCheck();

        MoveList moves;
        generatePseudoLegalMoves(pos, moves);
        orderMoves(pos, moves.begin(), moves.end(), ttMove, ply);

        const Color us = pos.sideToMove();
        int legalMoves = 0;
        int bestScore = -INF;
        Move bestMove{};
        std::array<Move, MAX_MOVES> failedQuiets{};
        std::array<Move, MAX_MOVES> failedCaptures{};
        std::size_t failedQuietCount = 0;
        std::size_t failedCaptureCount = 0;

        for (Move move : moves) {
            if (shouldStop())
                break;

            const Piece movingPiece = pos.pieceAt(move.from());
            const bool terminalKingCapture = isTerminalKingCapture(pos, move);
            const bool givesCheckMove = !terminalKingCapture && pos.givesCheck(move);
            const int lmrHistoryScore = (!move.isCapture() && !move.isPromotion())
                ? moveScore(pos, move, Move{}, ply)
                : 0;
            int score = -INF;
            bool legal = true;

            const bool quiet = !move.isCapture() && !move.isPromotion();
            const int mateWindow = !(alpha > -MATE_THRESHOLD && beta < MATE_THRESHOLD)
                && !terminalKingCapture;

            if (!pvNode
                && !inCheckNode
                && depth <= SEE_PRUNE_MAX_DEPTH
                && legalMoves > 0
                && move.isCapture()
                && !move.isPromotion()
                && move != ttMove
                && !givesCheckMove
                && !mateWindow) {
                const int seeThreshold = -SEE_PRUNE_MARGIN_PER_DEPTH * depth;
                if (!seeGE(pos, move, seeThreshold, SeeMode::Legal))
                    continue;
            }

            if (terminalKingCapture) {
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
                    searchStack_[ply + 1] = HistoryContext{ move, pos.pieceAt(move.to()), us };

                    ++legalMoves;
                    if (legalMoves == 1) {
                        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
                    }
                    else {

                        const bool lmrEligible = depth >= 2 && legalMoves >= 2 && quiet;

                        int reduction = 0;

                        if (lmrEligible) {
                            reduction = lmrBaseReduction(depth, legalMoves);

                            // Reduction adjustments go here!

                            reduction = std::clamp(reduction, 0, depth - 2);
                        }

                        if (reduction > 0) {
                            const int reducedDepth = depth - 1 - reduction;
                            score = -negamax(pos, reducedDepth, -alpha - 1, -alpha, ply + 1, false);

                            if (!shouldStop() && score > alpha)
                                score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
                        }
                        else {
                            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
                        }

                        if (!shouldStop() && score > alpha && score < beta)
                            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
                    }
                }
                pos.undoMove(move, st);
            }

            if (!legal)
                continue;
            if (terminalKingCapture)
                ++legalMoves;

            if (shouldStop())
                break;

            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
            }

            bool cutoff = false;
            if (score > alpha) {
                alpha = score;
                pv_[ply][ply] = move;
                for (int i = ply + 1; i < pvLength_[ply + 1]; ++i)
                    pv_[ply][i] = pv_[ply + 1][i];
                pvLength_[ply] = std::max(ply + 1, pvLength_[ply + 1]);
                cutoff = alpha >= beta;
            }

            if (cutoff) {
                if (!terminalKingCapture) {
                    if (move.isCapture()) {
                        updateCaptureCutoff(us, move, movingPiece, depth,
                            failedCaptures.data(), failedCaptureCount, pos);
                    }
                    else if (!move.isPromotion()) {
                        updateQuietCutoff(pos, us, move, movingPiece, depth, ply,
                            failedQuiets.data(), failedQuietCount);
                    }
                }
                break;
            }

            if (move.isCapture()) {
                if (failedCaptureCount < failedCaptures.size())
                    failedCaptures[failedCaptureCount++] = move;
            }
            else if (!move.isPromotion()) {
                if (failedQuietCount < failedQuiets.size())
                    failedQuiets[failedQuietCount++] = move;
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

    void SearchEngine::emitInfo(
        std::ostream& out,
        int depth,
        int score,
        const Move* pv,
        int pvLength,
        ScoreBound bound)
    {
        const std::int64_t ms = elapsedMs();

        const std::uint64_t nps =
            ms > 0
            ? nodes_ * 1000ull / static_cast<std::uint64_t>(ms)
            : nodes_ * 1000ull;

        std::ostringstream line;

        line << "info depth " << depth
            << " seldepth " << selDepth_;

        if (score >= MATE_THRESHOLD) {
            line << " score mate "
                << (MATE_SCORE - score);
        }
        else if (score <= -MATE_THRESHOLD) {
            line << " score mate -"
                << (MATE_SCORE + score);
        }
        else {
            line << " score cp "
                << score;
        }

        if (bound == ScoreBound::Lower)
            line << " lowerbound";
        else if (bound == ScoreBound::Upper)
            line << " upperbound";

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
        searchStack_.fill(HistoryContext{});

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

            int score = 0;

            if (depth < ASPIRATION_START_DEPTH ||
                bestScore >= MATE_THRESHOLD ||
                bestScore <= -MATE_THRESHOLD)
            {
                pvLength_[0] = 0;

                score = negamax(
                    position,
                    depth,
                    -INF,
                    INF,
                    0,
                    true
                );
            }
            else
            {
                int delta = ASPIRATION_INITIAL_DELTA;

                int alpha = std::max(-INF, bestScore - delta);
                int beta = std::min(INF, bestScore + delta);

                while (!shouldStop())
                {
                    pvLength_[0] = 0;

                    score = negamax(
                        position,
                        depth,
                        alpha,
                        beta,
                        0,
                        true
                    );

                    if (shouldStop())
                        break;

                    if (score <= alpha)
                    {
                        emitInfo(
                            out,
                            depth,
                            score,
                            pv_[0].data(),
                            pvLength_[0],
                            ScoreBound::Upper
                        );

                        delta *= 2;

                        alpha = std::max(
                            -INF,
                            score - delta
                        );

                        beta = std::min(
                            INF,
                            score + delta / 2
                        );
                    }

                    else if (score >= beta)
                    {
                        emitInfo(
                            out,
                            depth,
                            score,
                            pv_[0].data(),
                            pvLength_[0],
                            ScoreBound::Lower
                        );

                        delta *= 2;

                        beta = std::min(
                            INF,
                            score + delta
                        );

                        alpha = std::max(
                            -INF,
                            score - delta / 2
                        );
                    }

                    else
                    {
                        break;
                    }

                    if (alpha <= -INF && beta >= INF)
                    {
                        pvLength_[0] = 0;

                        score = negamax(
                            position,
                            depth,
                            -INF,
                            INF,
                            0,
                            true
                        );

                        break;
                    }
                }
            }

            if (shouldStop())
                break;

            if (pvLength_[0] > 0 && pv_[0][0]) {
                bestMove = pv_[0][0];
                bestScore = score;

                emitInfo(
                    out,
                    depth,
                    bestScore,
                    pv_[0].data(),
                    pvLength_[0]
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

}
