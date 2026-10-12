#include "ironphoenix/search.hpp"

#include "ironphoenix/eval.hpp"
#include "ironphoenix/lmr.hpp"
#include "ironphoenix/tuning.hpp"

#include "ironphoenix/movegen.hpp"
#include "ironphoenix/see.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>

namespace ironphoenix {
    namespace {

        constexpr int LMP_HISTORY_THRESHOLD = 2'000;

        // History Move Pruning (HMP): only prune very poorly scoring quiet
        // moves at shallow non-PV nodes. The threshold becomes more conservative
        // with depth because deeper nodes deserve more tactical coverage.
        constexpr int HISTORY_MOVE_PRUNE_MAX_DEPTH = 4;
        constexpr int HISTORY_MOVE_PRUNE_MARGIN_PER_DEPTH = 1'500;

        constexpr int NULL_MOVE_MIN_DEPTH = 2;
        constexpr int NULL_MOVE_DIVISOR = 190;
        constexpr int REVERSE_FUTILITY_MAX_DEPTH = 4;
        constexpr int REVERSE_FUTILITY_MARGIN_PER_DEPTH = 200;
        constexpr std::size_t CHECK_BIT_WORDS = (MAX_MOVES + 63u) / 64u;

        // Non-PV nodes normally cut off quickly. Selecting only the strongest
        // prefix lazily avoids sorting the entire high-branching 4PC move list.
        constexpr std::size_t LAZY_PICK_LIMIT = 16;

        struct ScoredMove final {
            int score;
            Move move;
            bool givesCheck;
        };

        IRONPHOENIX_FORCE_INLINE Bitboard teamOccupancy(const Position& pos, Color c) noexcept {
            const Color partner = static_cast<Color>(static_cast<unsigned>(c) ^ 2u);
            return pos.occupancy(c) | pos.occupancy(partner);
        }

        IRONPHOENIX_FORCE_INLINE bool hasNonPawnMaterial(const Position& pos, Color c) noexcept {
            const Bitboard pieces = pos.pieces(c, KNIGHT)
                | pos.pieces(c, BISHOP)
                | pos.pieces(c, ROOK)
                | pos.pieces(c, QUEEN);
            return static_cast<bool>(pieces);
        }

        IRONPHOENIX_FORCE_INLINE bool cachedGivesCheck(const std::uint64_t* bits, std::size_t index) noexcept {
            return ((bits[index >> 6u] >> (index & 63u)) & 1ull) != 0;
        }

        IRONPHOENIX_FORCE_INLINE int captureOrderingScore(Piece movingPiece, Move move) noexcept {
            // Full four-player SEE is expensive enough that using it on every
            // generated capture purely for ordering costs substantial NPS.
            // MVV/LVA gives us a cheap first-order tactical score; SEE is still
            // used later by actual SEE pruning where its result affects search.
            const int victim = pieceValue(move.captured());
            const int attacker = pieceValue(movingPiece);
            return victim * 128 - attacker * 8;
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

        // steady_clock::now() is surprisingly expensive when called several
        // times per searched move. Poll the clock periodically by node count;
        // explicit root checks still enforce the budget between iterations.
        if (timeBudgetMs_ > 0 && nodes_ >= nextTimeCheckNode_) {
            nextTimeCheckNode_ = nodes_ + TIME_CHECK_NODE_INTERVAL;
            if (elapsedMs() >= timeBudgetMs_) {
                stopRequested_.store(true, std::memory_order_relaxed);
                return true;
            }
        }

        return false;
    }

    int SearchEngine::evaluate(const Position& pos) const noexcept {
        return Eval::evaluate(pos);
    }

    int SearchEngine::historyBonus(int depth) const noexcept {
        const auto& params = Tuning::search();
        return std::min(params.historyBonusMax, params.historyBonusScale * depth * depth);
    }

    int SearchEngine::moveScore(const Position& pos,
        Move move,
        Move ttMove,
        int ply,
        bool givesCheck) const noexcept {

        if (move == ttMove)
            return 50'000'000;
        if (move.isCapture() && isTerminalKingCapture(pos, move))
            return 45'000'000;

        const auto& tuning = Tuning::search();
        const Color mover = pos.sideToMove();
        const Piece movingPiece = pos.pieceAt(move.from());

        if (move.isPromotion()) {
            int score = 30'000'000 + pieceValue(promotedType(move.promotion())) * 128;
            if (move.isCapture()) {
                score += captureOrderingScore(movingPiece, move);
                score += history_.captureScore(mover, movingPiece, move) * 2;
            }
            return score;
        }

        if (move.isCapture()) {
            int score = 20'000'000;
            score += captureOrderingScore(movingPiece, move);
            score += history_.captureScore(mover, movingPiece, move) * 2;
            return score;
        }

        int score = history_.quietScore(mover, move);

        if (ply > 0 && searchStack_[ply].valid()) {
            score += history_.continuationScore(ContinuationKind::PreviousPly, searchStack_[ply], movingPiece, move.to());

            if (history_.counterMove(mover, searchStack_[ply]) == move)
                score += tuning.counterMoveBonus;
        }

        if (ply >= 4 && searchStack_[ply - 3].valid()
            && searchStack_[ply - 3].mover == mover) {
            score += (history_.continuationScore(ContinuationKind::SamePlayer, searchStack_[ply - 3], movingPiece, move.to()) * 3) / 4;
        }

        if (givesCheck)
            score += tuning.checkMoveBonus;

        return score;
    }

    void SearchEngine::orderMoves(const Position& pos,
        Move* begin,
        Move* end,
        Move ttMove,
        int ply,
        int* orderedScores,
        std::uint64_t* givesCheckBits) const {

        // Do not value-initialize all 512 scratch entries at every node. Only
        // the first count entries are written and subsequently read.
        std::array<ScoredMove, MAX_MOVES> scored;
        const std::size_t count = static_cast<std::size_t>(end - begin);

        for (std::size_t i = 0; i < count; ++i) {
            const Move move = begin[i];
            const bool terminal = move.isCapture() && isTerminalKingCapture(pos, move);

            // Capture/promotion scores do not consume the check bonus, so avoid
            // the expensive givesCheck() call unless the caller explicitly needs
            // check metadata for pruning safety.
            const bool needCheck = givesCheckBits != nullptr
                || ((!move.isCapture() && !move.isPromotion()) && move != ttMove);
            const bool givesCheck = !terminal && needCheck && pos.givesCheck(move);

            scored[i].score = moveScore(pos, move, ttMove, ply, givesCheck);
            scored[i].move = move;
            scored[i].givesCheck = givesCheck;
        }

        std::sort(scored.begin(), scored.begin() + count,
            [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

        if (givesCheckBits) {
            for (std::size_t i = 0; i < CHECK_BIT_WORDS; ++i)
                givesCheckBits[i] = 0;
        }

        for (std::size_t i = 0; i < count; ++i) {
            begin[i] = scored[i].move;
            if (orderedScores)
                orderedScores[i] = scored[i].score;
            if (givesCheckBits && scored[i].givesCheck)
                givesCheckBits[i >> 6u] |= 1ull << (i & 63u);
        }
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

        // Mate Distance Pruning (MDP): scores closer to +/-MATE_SCORE than the
        // current ply can physically achieve are impossible. IronPhoenix scores
        // an immediate terminal king capture as MATE_SCORE - (ply + 1), while a
        // side already mated at this node scores -MATE_SCORE + ply.
        alpha = std::max(alpha, -MATE_SCORE + ply);
        beta = std::min(beta, MATE_SCORE - (ply + 1));
        if (alpha >= beta)
            return alpha;

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

        // In check, every evasion is relevant. Otherwise generate only captures,
        // en-passant and promotions directly instead of generating/copying every
        // quiet pseudo-legal move through a second 512-entry buffer.
        MoveList moves;
        if (inCheckNode)
            generatePseudoLegalMoves(pos, moves);
        else
            generateTacticalMoves(pos, moves);

        orderMoves(pos, moves.begin(), moves.end(), ttMove, ply);

        const Color us = pos.sideToMove();
        int legalMoves = 0;

        for (Move move : moves) {
            if (shouldStop())
                break;

            const bool terminalKingCapture = move.isCapture() && isTerminalKingCapture(pos, move);

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

    int SearchEngine::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode, bool allowNull) {
        if (ply >= MAX_PLY - 1)
            return evaluate(pos);
        if (shouldStop())
            return 0;

        if (depth <= 0)
            return qsearch(pos, alpha, beta, ply, pvNode);

        ++nodes_;
        selDepth_ = std::max(selDepth_, ply);
        pvLength_[ply] = ply;

        // Mate Distance Pruning (MDP): tighten the search window to the best and
        // worst mate scores that are still reachable from this ply. If the mate
        // bounds collapse the window, the node cannot improve the caller's result.
        alpha = std::max(alpha, -MATE_SCORE + ply);
        beta = std::min(beta, MATE_SCORE - (ply + 1));
        if (alpha >= beta)
            return alpha;

        const int originalAlpha = alpha;
        TTProbe ttProbe;
        const bool ttHit = tt_.probe(pos.key(), ply, ttProbe);
        const Move ttMove = ttHit ? ttProbe.move : Move{};

        // Transposition Table (TT) Cutoffs: if the current position has been
        // previously searched at a depth equal to or greater than the current
        // depth, and the stored score is sufficient to cause a cutoff, return
        // the stored score. This is only done at non-PV nodes, since PV nodes
        // require a full search to find the best move.
        if (!pvNode && ply > 0 && ttHit && ttProbe.depth >= depth) {
            if (ttProbe.bound == TTBound::Exact)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Lower && ttProbe.score >= beta)
                return ttProbe.score;
            if (ttProbe.bound == TTBound::Upper && ttProbe.score <= alpha)
                return ttProbe.score;
        }

        const bool inCheckNode = pos.inCheck();
        const auto& tuning = Tuning::search();
        const Color us = pos.sideToMove();
        const Color partner = static_cast<Color>(static_cast<unsigned>(us) ^ 2u);
        const bool mateWindow = !(alpha > -MATE_THRESHOLD && beta < MATE_THRESHOLD);

        // Static pruning shares one evaluator result between reverse futility
        // pruning and null-move pruning. Keep partner-check detection lazy so PV,
        // check and mate-window nodes do not pay for it.
        int staticEval = 0;
        bool staticEvalValid = false;
        bool partnerInCheck = false;
        bool partnerCheckKnown = false;

        // Reverse Futility Pruning (RFP): at shallow non-PV nodes, if the static
        // evaluation already exceeds beta by a depth-scaled safety margin, the
        // position is unlikely to fall below beta after a normal move. Avoid RFP
        // while either teammate is in check and never return a synthetic mate.
        if (!pvNode
            && ply > 0
            && depth <= REVERSE_FUTILITY_MAX_DEPTH
            && !inCheckNode
            && !mateWindow
            && hasNonPawnMaterial(pos, us)) {

            partnerInCheck = pos.inCheck(partner);
            partnerCheckKnown = true;

            if (!partnerInCheck) {
                staticEval = evaluate(pos);
                staticEvalValid = true;

                const int margin = REVERSE_FUTILITY_MARGIN_PER_DEPTH * depth;
                if (staticEval < MATE_THRESHOLD && staticEval - margin >= beta)
                    return staticEval;
            }
        }

        // Null Move Pruning (NMP): reuse the static evaluation and partner-check
        // result when RFP already computed them at shallow nodes.
        if (allowNull
            && !pvNode
            && ply > 0
            && depth >= NULL_MOVE_MIN_DEPTH
            && !inCheckNode
            && !mateWindow
            && hasNonPawnMaterial(pos, us)) {

            if (!partnerCheckKnown) {
                partnerInCheck = pos.inCheck(partner);
                partnerCheckKnown = true;
            }

            if (!partnerInCheck) {
                if (!staticEvalValid) {
                    staticEval = evaluate(pos);
                    staticEvalValid = true;
                }

                if (staticEval >= beta) {
                    StateInfo nullState;
                    pos.makeNullMove(nullState);
                    searchStack_[ply + 1] = HistoryContext{};

                    // Dynamic null-move reduction: reduce the depth more aggressively
                    // at deeper nodes and for higher static evaluations.
                    const int reduction = 4 + depth / 5 + std::min(3, (staticEval - beta) / NULL_MOVE_DIVISOR);
                    const int nullScore = -negamax(
                        pos,
                        depth - reduction,
                        -beta,
                        -beta + 1,
                        ply + 1,
                        false,
                        false
                    );

                    pos.undoNullMove(nullState);

                    if (shouldStop())
                        return 0;

                    // Never manufacture a mate score from a synthetic pass.
                    if (nullScore >= beta && nullScore < MATE_THRESHOLD)
                        return nullScore;
                }
            }
        }

        MoveList moves;
        generatePseudoLegalMoves(pos, moves);

        std::array<int, MAX_MOVES> orderedScores;
        std::array<std::uint64_t, CHECK_BIT_WORDS> givesCheckBits;

        if (pvNode) {
            // PV nodes are comparatively rare and benefit from complete ordering.
            // Keep their existing full sort and cached check metadata.
            orderMoves(pos,
                moves.begin(),
                moves.end(),
                ttMove,
                ply,
                orderedScores.data(),
                givesCheckBits.data());
        }
        else {
            // At cut nodes avoid sorting the entire move list and avoid eager
            // givesCheck() calls. Score cheaply, then select the best few moves
            // only as they are actually requested by the search loop.
            for (std::size_t i = 0; i < moves.size; ++i)
                orderedScores[i] = moveScore(pos, moves.moves[i], ttMove, ply, false);
        }

        int legalMoves = 0;
        int bestScore = -INF;
        Move bestMove{};

        // Quiet failures grow from the front and capture failures from the back,
        // replacing two 512-Move arrays with one without changing history updates.
        std::array<Move, MAX_MOVES> failedMoves;
        std::size_t failedQuietCount = 0;
        std::size_t failedCaptureCount = 0;

        for (std::size_t moveIndex = 0; moveIndex < moves.size; ++moveIndex) {
            if (shouldStop())
                break;

            if (!pvNode && moveIndex < LAZY_PICK_LIMIT) {
                std::size_t bestIndex = moveIndex;
                int bestOrderScore = orderedScores[moveIndex];

                for (std::size_t j = moveIndex + 1; j < moves.size; ++j) {
                    if (orderedScores[j] > bestOrderScore) {
                        bestOrderScore = orderedScores[j];
                        bestIndex = j;
                    }
                }

                if (bestIndex != moveIndex) {
                    std::swap(moves.moves[moveIndex], moves.moves[bestIndex]);
                    std::swap(orderedScores[moveIndex], orderedScores[bestIndex]);
                }
            }

            const Move move = moves.moves[moveIndex];
            const Piece movingPiece = pos.pieceAt(move.from());
            const bool terminalKingCapture = move.isCapture() && isTerminalKingCapture(pos, move);
            const bool givesCheckMove = pvNode
                ? cachedGivesCheck(givesCheckBits.data(), moveIndex)
                : (!terminalKingCapture && pos.givesCheck(move));
            const bool quiet = !move.isCapture() && !move.isPromotion();
            const int lmrHistoryScore = quiet ? orderedScores[moveIndex] : 0;

            int score = -INF;
            bool legal = true;

            const bool moveMateWindow = !(alpha > -MATE_THRESHOLD && beta < MATE_THRESHOLD)
                && !terminalKingCapture;

            // Late Move Pruning (LMP): at non-PV nodes, stop spending search
            // effort on late quiet moves unless history says the move is
            // promising. Checks, TT moves, promotions/captures, and mate-window
            // searches are never pruned here. This intentionally has no depth cap.
            if (!pvNode
                && !inCheckNode
                && legalMoves >= 3 + depth * depth
                && quiet
                && move != ttMove
                && !givesCheckMove
                && !moveMateWindow
                && lmrHistoryScore < LMP_HISTORY_THRESHOLD) {
                continue;
            }

            // History Move Pruning (HMP): prune quiet moves with strongly negative
            // accumulated history before they consume a search. Keep it shallow,
            // protect the first legal move, and never prune tactical/special moves.
            if (!pvNode
                && !inCheckNode
                && depth <= HISTORY_MOVE_PRUNE_MAX_DEPTH
                && legalMoves > 0
                && quiet
                && move != ttMove
                && !givesCheckMove
                && !moveMateWindow
                && lmrHistoryScore < -HISTORY_MOVE_PRUNE_MARGIN_PER_DEPTH * depth) {
                continue;
            }

            // Static Exchange Evaluation (SEE) Pruning: at non-PV nodes, if a
            // capture move is likely to lose material, skip it. This is only done
            // at shallow depths, since SEE is not always accurate in complex positions.
            // SEE pruning is not applied to TT moves, promotions, checks, or mate-window searches.
            if (!pvNode
                && !inCheckNode
                && depth <= tuning.seePruneMaxDepth
                && legalMoves > 0
                && move.isCapture()
                && !move.isPromotion()
                && move != ttMove
                && !givesCheckMove
                && !moveMateWindow) {
                const int seeThreshold = -tuning.seePruneMarginPerDepth * depth;
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
                        const Move* failedCaptures = failedMoves.data() + MAX_MOVES - failedCaptureCount;
                        updateCaptureCutoff(us, move, movingPiece, depth,
                            failedCaptures, failedCaptureCount, pos);
                    }
                    else if (!move.isPromotion()) {
                        updateQuietCutoff(pos, us, move, movingPiece, depth, ply,
                            failedMoves.data(), failedQuietCount);
                    }
                }
                break;
            }

            if (failedQuietCount + failedCaptureCount < failedMoves.size()) {
                if (move.isCapture()) {
                    ++failedCaptureCount;
                    failedMoves[MAX_MOVES - failedCaptureCount] = move;
                }
                else if (!move.isPromotion()) {
                    failedMoves[failedQuietCount++] = move;
                }
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
        nextTimeCheckNode_ = 0;
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

        const auto& tuning = Tuning::search();
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

            if (depth < tuning.aspirationStartDepth ||
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
                int delta = tuning.aspirationInitialDelta;

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
