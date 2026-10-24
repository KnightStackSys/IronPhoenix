#include "ironphoenix/search.hpp"

#include "ironphoenix/eval.hpp"
#include "ironphoenix/lmr.hpp"
#include "ironphoenix/king_legality.hpp"
#include "ironphoenix/tuning.hpp"

#include "ironphoenix/movegen.hpp"
#include "ironphoenix/see.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <vector>
#include <string>
#include <utility>

namespace ironphoenix {
    namespace {

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

        // Conservative cheap prefilter for expensive quiet-move givesCheck().
        // Only return false if neither enemy king can be checked by the moved
        // piece nor unmasked by vacating the source square. Special moves and
        // pre-existing checks are handled by the caller with full givesCheck().
        IRONPHOENIX_FORCE_INLINE bool quietMayGiveCheck(
            const Position& pos, Move move) noexcept {
            const Piece moving = pos.pieceAt(move.from());
            if (!moving)
                return true;
            const Color us = colorOf(moving);
            const PieceType type = typeOf(moving);
            const Square from = move.from(), to = move.to();
            const int fromF = Geometry::fileOf(from), fromR = Geometry::rankOf(from);
            const int toF = Geometry::fileOf(to), toR = Geometry::rankOf(to);
            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const Color opponent = static_cast<Color>(ci);
                if (!enemy(us, opponent) || !pos.isAlive(opponent))
                    continue;
                const Square king = pos.kingSquare(opponent);
                if (king == SQ_NONE)
                    continue;
                const int kf = Geometry::fileOf(king), kr = Geometry::rankOf(king);
                const int fdx = fromF - kf, fdy = fromR - kr;
                // Uncovering a friendly slider check requires vacating a
                // square collinear with its target king.
                if (!fdx || !fdy || std::abs(fdx) == std::abs(fdy))
                    return true;

                const int dx = toF - kf, dy = toR - kr;
                const bool orthogonal = !dx || !dy;
                const bool diagonal = std::abs(dx) == std::abs(dy);
                switch (type) {
                case PAWN:
                    if (Geometry::PawnAttacks[us][to].test(king))
                        return true;
                    break;
                case KNIGHT:
                    if (Geometry::KnightAttacks[to].test(king))
                        return true;
                    break;
                case BISHOP:
                    if (diagonal) return true;
                    break;
                case ROOK:
                    if (orthogonal) return true;
                    break;
                case QUEEN:
                    if (orthogonal || diagonal) return true;
                    break;
                case KING:
                    if (Geometry::KingAttacks[to].test(king))
                        return true;
                    break;
                default:
                    return true;
                }
            }
            return false;
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

    SearchEngine::SearchEngine(bool helper)
        : tt_(helper ? 1u : 64u), helperMode_(helper) {}

    SearchEngine::~SearchEngine() {
        stopAndWait();
    }

    bool SearchEngine::setThreads(int threads) {
        if (threads < 1 || threads > MAX_THREADS)
            return false;
        stopAndWait();
        threads_ = threads;
        return true;
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
        // One search always sees one set of tuning weights; do not reuse HCE
        // entries across successive searches or engine option changes.
        if (++evalCacheEpoch_ == 0) {
            evalCacheEpoch_ = 1;
            evalCache_.fill(EvalCacheEntry{});
        }
        evalCacheHits_ = 0;
        evalCacheActive_ = true;
        stopRequested_.store(false, std::memory_order_relaxed);
        ponderHitRequested_.store(false, std::memory_order_relaxed);
        searching_.store(true, std::memory_order_relaxed);
        if (!helperMode_) {
            tt_.setConcurrent(threads_ > 1);
            groupNodes_.store(0, std::memory_order_relaxed);
            sharedNodes_ = threads_ > 1 ? &groupNodes_ : nullptr;
        }
        tt_.newGeneration();

        worker_ = std::thread([this, position = std::move(position), limits, &out]() mutable {
            limits_ = limits;
            runWithHelpers(std::move(position), 1, out);
            searching_.store(false, std::memory_order_relaxed);
            });
    }

    namespace {
        // Helpers must never emit info/bestmove onto the primary UCI stream.
        class NullStreamBuffer final : public std::streambuf {
        protected:
            int_type overflow(int_type ch) override {
                return traits_type::not_eof(ch);
            }
        };
    }

    void SearchEngine::runWithHelpers(Position position, int multiPV, std::ostream& out) {
        struct Sink {
            NullStreamBuffer buffer;
            std::ostream stream{&buffer};
        };
        // Each helper writes only to its own discarded stream.
        std::vector<std::unique_ptr<Sink>> sinks;
        std::vector<std::unique_ptr<SearchEngine>> helpers;
        if (!helperMode_ && threads_ > 1) {
            sinks.reserve(static_cast<std::size_t>(threads_ - 1));
            helpers.reserve(static_cast<std::size_t>(threads_ - 1));
            for (int i = 1; i < threads_; ++i) {
                auto sink = std::make_unique<Sink>();
                auto helper = std::make_unique<SearchEngine>(true);
                helper->activeTT_ = &tt_;
                helper->groupStop_ = &stopRequested_;
                helper->sharedNodes_ = &groupNodes_;
                helper->helperIndex_ = i;
                // Independent iterative deepening with staggered root depths
                // creates natural Lazy SMP diversity; only main emits moves.
                helper->start(position, limits_, sink->stream);
                sinks.push_back(std::move(sink));
                helpers.push_back(std::move(helper));
            }
        }

        if (multiPV > 1)
            runMultiPV(std::move(position), multiPV, out);
        else
            run(std::move(position), out);

        // Stop and join every helper before releasing the shared table/stream.
        for (auto& helper : helpers)
            helper->stopAndWait();
    }

    void SearchEngine::countNode() noexcept {
        ++nodes_;
        // Keep atomic contention out of the hot loop by reporting in batches.
        if (sharedNodes_ && (nodes_ & 255u) == 0)
            sharedNodes_->fetch_add(256u, std::memory_order_relaxed);
    }

    std::uint64_t SearchEngine::totalNodes() const noexcept {
        return sharedNodes_
            ? sharedNodes_->load(std::memory_order_relaxed) + (nodes_ & 255u)
            : nodes_;
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

    void SearchEngine::ponderHit() noexcept {
        ponderHitRequested_.store(true, std::memory_order_relaxed);
    }

    std::int64_t SearchEngine::elapsedMs() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
    }

    std::int64_t SearchEngine::budgetElapsedMs() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - budgetStartTime_).count();
    }

    SearchEngine::TimeBudget SearchEngine::calculateTimeBudget(
        const Position& pos,
        const SearchLimits& limits) const noexcept {

        TimeBudget budget{};

        if (limits.movetimeMs > 0) {
            const std::int64_t fixed = std::max<std::int64_t>(1, limits.movetimeMs);
            budget.minimumMs = fixed;
            budget.softMs = fixed;
            budget.hardMs = fixed;
            return budget;
        }

        if (!limits.hasClock)
            return budget;

        const unsigned c = static_cast<unsigned>(pos.sideToMove());
        const std::int64_t remaining = std::max<std::int64_t>(0, limits.timeMs[c]);
        const std::int64_t increment = std::max<std::int64_t>(0, limits.incrementMs[c]);
        const std::int64_t delay = std::max<std::int64_t>(0, limits.delayMs[c]);
        const std::int64_t overhead = std::min(
            remaining,
            std::max<std::int64_t>(0, limits.moveOverheadMs));
        const std::int64_t usable = std::max<std::int64_t>(0, remaining - overhead);
        const std::int64_t clockCap = delay + usable;

        if (clockCap <= 0) {
            budget.minimumMs = 1;
            budget.softMs = 1;
            budget.hardMs = 1;
            return budget;
        }

        // In 4PC a player only moves once per four plies, but each seat still
        // owns its own clock. Budget by that player's remaining moves rather than
        // by raw ply count. Increment is sustainable spend; delay is effectively
        // free thinking time before the clock starts decreasing.
        constexpr std::int64_t EXPECTED_REMAINING_MOVES = 32;
        const std::int64_t baseShare = usable / EXPECTED_REMAINING_MOVES;

        budget.minimumMs = std::max(
            delay,
            std::min(increment, usable));
        budget.minimumMs = std::clamp<std::int64_t>(budget.minimumMs, 1, clockCap);

        budget.softMs = delay + baseShare + increment;
        budget.softMs = std::clamp(budget.softMs, budget.minimumMs, clockCap);

        // Fischer-increment recovery mode. When the clock has fallen to only a
        // handful of increments, allow stable/obvious positions to finish below
        // the increment so the engine can actually rebuild clock time. The normal
        // iterative-deepening stability logic still expands the target when the
        // best move changes or the score is volatile, so difficult positions can
        // spend more when necessary.
        //
        // Example with a 10-second increment:
        //   <= 2 increments left: target about 5.5s
        //   <= 4 increments left: target about 7.0s
        //   <= 8 increments left: target about 8.5s
        // Stable PVs can stop earlier; unstable PVs may expand toward the hard cap.
        if (increment > 0 && delay == 0) {
            int gainTargetPercent = 0;
            int minimumPercent = 0;

            if (usable <= increment * 2) {
                gainTargetPercent = 55;
                minimumPercent = 20;
            }
            else if (usable <= increment * 4) {
                gainTargetPercent = 70;
                minimumPercent = 25;
            }
            else if (usable <= increment * 8) {
                gainTargetPercent = 85;
                minimumPercent = 35;
            }

            if (gainTargetPercent > 0) {
                const std::int64_t recoveryMinimum = std::max<std::int64_t>(
                    1,
                    (increment * minimumPercent) / 100);
                budget.minimumMs = std::clamp<std::int64_t>(
                    recoveryMinimum,
                    1,
                    clockCap);

                const std::int64_t recoverySoft = std::max(
                    budget.minimumMs,
                    (increment * gainTargetPercent) / 100);
                budget.softMs = std::clamp<std::int64_t>(
                    recoverySoft,
                    budget.minimumMs,
                    clockCap);
            }
        }

        // The hard limit protects the clock inside a deep iteration. Unstable
        // root choices may use more than the soft target, but never more than
        // roughly half of the usable clock or twice the current target. In
        // recovery mode this gives volatile positions permission to exceed the
        // increment while still making stable moves quickly enough to gain time.
        const std::int64_t halfClockCap = delay + usable / 2;
        const std::int64_t adaptiveCap = std::max(budget.softMs, halfClockCap);
        budget.hardMs = std::min(clockCap, std::min(budget.softMs * 2, adaptiveCap));
        budget.hardMs = std::max(budget.hardMs, budget.softMs);

        return budget;
    }

    void SearchEngine::processPonderHit() noexcept {
        if (!pondering_) {
            ponderHitRequested_.store(false, std::memory_order_relaxed);
            return;
        }

        if (!ponderHitRequested_.exchange(false, std::memory_order_relaxed))
            return;

        // Ponder time belongs to the opponent. Start our actual move budget from
        // the hit so a successful ponder still gets a normal increment/delay-aware
        // search window instead of immediately returning.
        pondering_ = false;
        budgetStartTime_ = std::chrono::steady_clock::now();
        nextTimeCheckNode_ = nodes_;
    }

    bool SearchEngine::shouldStop() noexcept {
        if (stopRequested_.load(std::memory_order_relaxed)
            || (groupStop_ && groupStop_->load(std::memory_order_relaxed)))
            return true;

        if (limits_.nodes > 0 && totalNodes() >= limits_.nodes) {
            stopRequested_.store(true, std::memory_order_relaxed);
            return true;
        }

        processPonderHit();

        // steady_clock::now() is surprisingly expensive when called several
        // times per searched move. Poll the hard clock periodically by node
        // count; soft time is checked only between completed root iterations.
        if (!pondering_ && hardTimeBudgetMs_ > 0 && nodes_ >= nextTimeCheckNode_) {
            nextTimeCheckNode_ = nodes_ + TIME_CHECK_NODE_INTERVAL;
            if (budgetElapsedMs() >= hardTimeBudgetMs_) {
                stopRequested_.store(true, std::memory_order_relaxed);
                return true;
            }
        }

        return false;
    }

    bool SearchEngine::iterationTimeExpired(
        int stableDepths,
        bool bestMoveChanged,
        int scoreDelta) noexcept {

        processPonderHit();

        if (pondering_ || softTimeBudgetMs_ <= 0)
            return false;

        int scalePercent = 100;

        // A changing PV is a useful uncertainty signal in 4PC's large tactical
        // root. Stable best moves let us bank time; instability spends more.
        if (bestMoveChanged)
            scalePercent += 35;
        else if (stableDepths >= 4)
            scalePercent -= 25;
        else if (stableDepths >= 2)
            scalePercent -= 15;

        if (scoreDelta >= 150)
            scalePercent += 20;
        else if (scoreDelta >= 75)
            scalePercent += 10;

        std::int64_t target = (softTimeBudgetMs_ * scalePercent) / 100;
        target = std::clamp(target, minimumTimeBudgetMs_, hardTimeBudgetMs_);
        return budgetElapsedMs() >= target;
    }

    bool SearchEngine::shouldStopOnWinningMate(int score, Move rootMove) const noexcept {
        // Completed root wins may stop immediately. Negative mate scores
        // continue searching for defenses. Infinite/ponder searches retain
        // their usual explicit-stop behavior.
        return !limits_.infinite && !pondering_ && static_cast<bool>(rootMove)
            && score >= MATE_THRESHOLD && score <= MATE_SCORE;
    }

    int SearchEngine::evaluate(const Position& pos) const noexcept {
        if (!evalCacheActive_ || !Tuning::search().evalCacheEnabled)
            return Eval::evaluate(pos);

        const Key key = pos.key();
        EvalCacheEntry& entry =
            evalCache_[static_cast<std::size_t>(key) & (EVAL_CACHE_CAPACITY - 1)];
        if (entry.epoch == evalCacheEpoch_ && entry.key == key) {
            ++evalCacheHits_;
            return entry.score;
        }
        const int score = Eval::evaluate(pos);
        entry.key = key;
        entry.score = score;
        entry.epoch = evalCacheEpoch_;
        return score;
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

        if (ply > 0 && searchStack_[ply].history.valid()) {
            score += history_.continuationScore(ContinuationKind::PreviousPly, searchStack_[ply].history, movingPiece, move.to());

            if (history_.counterMove(mover, searchStack_[ply].history) == move)
                score += tuning.counterMoveBonus;
        }

        if (ply >= 4 && searchStack_[ply - 3].history.valid()
            && searchStack_[ply - 3].history.mover == mover) {
            score += (history_.continuationScore(ContinuationKind::SamePlayer, searchStack_[ply - 3].history, movingPiece, move.to()) * 3) / 4;
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

        bool existingCheckKnown = false;
        bool existingCheck = false;
        for (std::size_t i = 0; i < count; ++i) {
            const Move move = begin[i];
            const bool terminal = move.isCapture() && isTerminalKingCapture(pos, move);

            // Capture/promotion scores do not consume the check bonus, so avoid
            // the expensive givesCheck() call unless the caller explicitly needs
            // check metadata for pruning safety.
            const bool needCheck = givesCheckBits != nullptr
                || ((!move.isCapture() && !move.isPromotion()) && move != ttMove);
            bool givesCheck = false;
            if (!terminal && needCheck) {
                bool possible = true;
                if (Tuning::search().fastCheckPrefilter
                    && !move.isCapture() && !move.isPromotion()
                    && !move.isEnPassant() && !move.isCastle()
                    && !quietMayGiveCheck(pos, move)) {
                    // The old givesCheck() also reports checks that were
                    // already present before a quiet move. Compute this once
                    // per node so the fast prefilter is semantically exact.
                    if (!existingCheckKnown) {
                        existingCheckKnown = true;
                        const Color mover = pos.sideToMove();
                        const Bitboard occ = pos.occupancy();
                        for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                            const Color foe = static_cast<Color>(ci);
                            if (!enemy(mover, foe) || !pos.isAlive(foe))
                                continue;
                            const Square king = pos.kingSquare(foe);
                            if (king != SQ_NONE && pos.isAttackedBy(king, mover, occ)) {
                                existingCheck = true;
                                break;
                            }
                        }
                    }
                    possible = existingCheck;
                }
                if (possible)
                    givesCheck = pos.givesCheck(move);
            }

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

        if (ply > 0 && searchStack_[ply].history.valid()) {
            history_.updateContinuation(ContinuationKind::PreviousPly, searchStack_[ply].history, movingPiece, cutoffMove.to(), bonus);
            history_.setCounterMove(mover, searchStack_[ply].history, cutoffMove);
        }

        if (ply >= 4 && searchStack_[ply - 3].history.valid()
            && searchStack_[ply - 3].history.mover == mover) {
            history_.updateContinuation(ContinuationKind::SamePlayer, searchStack_[ply - 3].history, movingPiece,
                cutoffMove.to(), (bonus * 3) / 4);
        }

        for (std::size_t i = 0; i < failedQuietCount; ++i) {
            const Move failed = failedQuiets[i];
            const Piece failedPiece = pos.pieceAt(failed.from());
            if (!isPiece(failedPiece))
                continue;

            history_.updateQuiet(mover, failed, penalty);
            if (ply > 0 && searchStack_[ply].history.valid())
                history_.updateContinuation(ContinuationKind::PreviousPly, searchStack_[ply].history, failedPiece, failed.to(), penalty);

            if (ply >= 4 && searchStack_[ply - 3].history.valid()
                && searchStack_[ply - 3].history.mover == mover) {
                history_.updateContinuation(ContinuationKind::SamePlayer, searchStack_[ply - 3].history, failedPiece,
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

    void SearchEngine::recordStackEvaluation(int ply, int score) noexcept {
        auto& current = searchStack_[ply];
        const SearchStackEntry* earlier = nullptr;

        // In 4PC the teammate generally moves two plies earlier, and the
        // same player four plies earlier. Eliminations may skip colors.
        // Only compare actual ancestors with the same team perspective.
        for (int back = 2; back <= 6 && back <= ply; ++back) {
            const auto& ancestor = searchStack_[ply - back];
            if (ancestor.staticEvalValid
                && teamOf(ancestor.sideToMove) == teamOf(current.sideToMove)) {
                earlier = &ancestor;
                break;
            }
        }
        current.recordStaticEval(score, earlier);
    }

    int SearchEngine::qsearch(Position& pos, int alpha, int beta, int ply, bool pvNode) {
        if (ply >= MAX_PLY - 1)
            return evaluate(pos);
        if (shouldStop())
            return 0;

        auto& ss = searchStack_[ply];
        ss.beginNode(pos.sideToMove(), 0, pvNode);

        countNode();
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
        ss.inCheck = inCheckNode;

        TTProbe ttProbe;
        const bool ttHit = activeTT_->probe(pos.key(), ply, ttProbe);
        ss.recordTT(ttProbe, ttHit);
        const Move ttMove = ss.ttMove;

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
            recordStackEvaluation(ply, bestScore);

            if (bestScore >= beta) {
                activeTT_->store(pos.key(), 0, bestScore, TTBound::Lower, Move{}, pvNode, ply);
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
            ss.currentMove = move;

            const bool terminalKingCapture = move.isCapture() && isTerminalKingCapture(pos, move);

            int score = -INF;
            bool legal = true;

            if (terminalKingCapture) {
                score = MATE_SCORE - (ply + 1);
                pvLength_[ply + 1] = ply + 1;
            }
            else {
                // A non-king move from a square outside every king ray
                // cannot expose a new enemy attack on an already-safe king.
                // King moves, EP, castling, checks and aligned sources keep
                // full verification. All subsequent move/search logic is same.
                const bool verifyKing = inCheckNode
                    || !Tuning::search().fastKingLegality
                    || kingCheckCanChange(pos, move);
                StateInfo st;
                pos.makeMove(move, st);

                if (verifyKing && pos.inCheck(us)) {
                    legal = false;
                }
                else {
                    searchStack_[ply + 1].history = HistoryContext{ move, pos.pieceAt(move.to()), us };
                    score = -qsearch(pos, -beta, -alpha, ply + 1, pvNode);
                }

                pos.undoMove(move, st);
            }

            if (!legal)
                continue;

            ++legalMoves;
            ss.moveCount = legalMoves;

            if (shouldStop())
                break;

            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
                ss.bestMove = move;
            }

            if (score > alpha) {
                alpha = score;

                pv_[ply][ply] = move;
                for (int i = ply + 1; i < pvLength_[ply + 1]; ++i)
                    pv_[ply][i] = pv_[ply + 1][i];
                pvLength_[ply] = std::max(ply + 1, pvLength_[ply + 1]);

                if (alpha >= beta) {
                    activeTT_->store(pos.key(), 0, bestScore, TTBound::Lower, bestMove, pvNode, ply);
                    return bestScore;
                }
            }
        }

        if (shouldStop())
            return bestScore == -INF ? 0 : bestScore;

        if (inCheckNode && legalMoves == 0) {
            const int mateScore = -MATE_SCORE + ply;
            activeTT_->store(pos.key(), 0, mateScore, TTBound::Exact, Move{}, pvNode, ply);
            return mateScore;
        }

        if (bestScore == -INF) {
            bestScore = evaluate(pos);
            recordStackEvaluation(ply, bestScore);
        }

        TTBound bound = TTBound::Exact;
        if (bestScore <= originalAlpha)
            bound = TTBound::Upper;
        else if (bestScore >= beta)
            bound = TTBound::Lower;

        activeTT_->store(pos.key(), 0, bestScore, bound, bestMove, pvNode, ply);
        return bestScore;
    }

    bool SearchEngine::reverseFutilitySafe(Position& pos) const {
        // This heuristic assumes the full alternating Teams turn cycle. Do not
        // infer anything about positions with missing/eliminated players.
        if (pos.aliveMask() != 0xF)
            return false;
        for (unsigned c = 0; c < COLOR_NB; ++c) {
            const Color color = static_cast<Color>(c);
            // Check either allied king (including the partner who cannot move
            // yet), and enemy kings so immediate terminal captures are searched.
            if (pos.kingSquare(color) == SQ_NONE || pos.inCheck(color))
                return false;
            auto pawns = pos.pieces(color, PAWN);
            while (pawns) {
                const Square from = popLsb(pawns);
                const Square to = Geometry::step(from, Geometry::pawnForward(color));
                // Deliberately conservative even if blocked: promotion by any
                // seat during the next cycle makes a static cutoff unreliable.
                if (to != SQ_NONE && Geometry::isPromotionSquare(color, to))
                    return false;
            }
        }

        // A static surplus must not turn stalemate into a win or hide a forced
        // move. Stop after two legal witnesses rather than building a full list.
        MoveList moves;
        generatePseudoLegalMoves(pos, moves);
        const Color us = pos.sideToMove();
        int legal = 0;
        for (Move move : moves) {
            if (isTerminalKingCapture(pos, move) || move.isPromotion())
                return false;
        }
        for (Move move : moves) {
            StateInfo state;
            pos.makeMove(move, state);
            const bool valid = !pos.inCheck(us);
            pos.undoMove(move, state);
            if (valid && ++legal == 2)
                return true;
        }
        return false;
    }

    int SearchEngine::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode, bool allowNull) {
        if (ply >= MAX_PLY - 1)
            return evaluate(pos);
        if (shouldStop())
            return 0;

        if (depth <= 0)
            return qsearch(pos, alpha, beta, ply, pvNode);

        auto& ss = searchStack_[ply];
        ss.beginNode(pos.sideToMove(), depth, pvNode);

        countNode();
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
        const bool ttHit = activeTT_->probe(pos.key(), ply, ttProbe);
        ss.recordTT(ttProbe, ttHit);
        const Move ttMove = ss.ttMove;

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
        ss.inCheck = inCheckNode;
        const auto& tuning = Tuning::search();
        const Color us = pos.sideToMove();
        const bool mateWindow = !(alpha > -MATE_THRESHOLD && beta < MATE_THRESHOLD);

        // RFP and NMP share all of these guards. In 4PC the partner-check
        // query can be expensive: compute it once for both heuristics.
        const bool canStaticPrune = allowNull && !pvNode && ply > 0
            && !inCheckNode && !mateWindow
            && hasNonPawnMaterial(pos, us)
            && !pos.inCheck(static_cast<Color>(static_cast<unsigned>(us) ^ 2u));

        // RFP estimates a fail-high in the side-to-move TEAM's perspective,
        // exactly as evaluate() does. Never negate for the partner. Keep this
        // out of null-move child nodes, root/PV nodes, checks and mate windows.
        if (canStaticPrune && depth <= tuning.rfpMaxDepth) {
            const int staticEval = evaluate(pos);
            recordStackEvaluation(ply, staticEval);
            // Search-stack evaluations are already team-relative. If a
            // comparable same-team ancestor shows the position improving,
            // use a smaller margin; otherwise keep the base margin.
            // The bounded discount never turns this into a zero-margin
            // static-evaluation cutoff, even at depth one.
            const int improvingDiscount = ss.improving
                ? std::min(tuning.rfpMaxImprovingDiscount, ss.improvingRate / 3)
                : 0;
            const int margin = std::max(tuning.rfpMinMargin,
                tuning.rfpMarginPerDepth * depth - improvingDiscount);
            const int estimate = staticEval - margin;
            if (staticEval > -MATE_THRESHOLD && staticEval < MATE_THRESHOLD
                && estimate >= beta && reverseFutilitySafe(pos))
                return estimate;
        }

        // Null Move Pruning (NMP): reuse the stack's raw evaluation when RFP
        // already evaluated this node. The existing NMP policy is unchanged.
        if (canStaticPrune && depth >= tuning.nullMoveMinDepth) {
            const int staticEval = ss.staticEvalValid ? ss.staticEval : evaluate(pos);
            if (!ss.staticEvalValid)
                recordStackEvaluation(ply, staticEval);

            if (staticEval >= beta) {
                StateInfo nullState;
                pos.makeNullMove(nullState);
                searchStack_[ply + 1].history = HistoryContext{};
                
                // Dynamic null-move reduction: reduce the depth more aggressively
                // at deeper nodes and for higher static evaluations.
                const int reduction = 4 + depth / 5 +
                    std::min(3, (staticEval - beta) / tuning.nullMoveDivisor);
                const int nullScore = -negamax(
                    pos,
                    depth - reduction,
                    -beta,
                    -beta + 1,
                    ply + 1,
                    false,
                    false
                );
                
				// Undo the null move before returning to restore the position state.
                pos.undoNullMove(nullState);
                
                if (shouldStop())
                    return 0;
                
                // Never manufacture a mate score from a synthetic pass.
                if (nullScore >= beta && nullScore < MATE_THRESHOLD)
                    return nullScore;
            }
        }

        MoveList moves;
        generatePseudoLegalMoves(pos, moves);

        std::array<int, MAX_MOVES> orderedScores;

        if (pvNode) {
            // PV nodes benefit from full sorting but never use the check
            // bitmap or pruning scores. Checking every capture/promotion
            // here adds work without changing ordering.
            orderMoves(pos,
                moves.begin(),
                moves.end(),
                ttMove,
                ply);
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
            ss.currentMove = move;
            const Piece movingPiece = pos.pieceAt(move.from());
            const bool terminalKingCapture = move.isCapture() && isTerminalKingCapture(pos, move);
            const bool quiet = !move.isCapture() && !move.isPromotion();
            const int lmrHistoryScore = (!pvNode && quiet) ? orderedScores[moveIndex] : 0;

            int score = -INF;
            bool legal = true;

            const bool moveMateWindow = !(alpha > -MATE_THRESHOLD && beta < MATE_THRESHOLD)
                && !terminalKingCapture;

            // Most 4PC moves are *not* candidates for LMP/HMP/SEE pruning.
            // Only call the expensive givesCheck() routine if a move would
            // otherwise be pruned. This retains the existing checks exemption
            // and avoids needless attack-map queries on every searched move.
            const bool canPruneMove = !pvNode && !inCheckNode
                && move != ttMove && !moveMateWindow;
            const bool lmpCandidate = canPruneMove && quiet
                && legalMoves >= (3 + depth * depth) / (2 - ss.improving)
                && lmrHistoryScore < tuning.lmpHistoryThreshold;
            const bool hmpCandidate = canPruneMove && quiet
                && depth <= tuning.hmpMaxDepth
                && legalMoves > 0
                && lmrHistoryScore < -tuning.hmpMarginPerDepth * depth;
            const bool seeCandidate = canPruneMove && legalMoves > 0
                && depth <= tuning.seePruneMaxDepth
                && move.isCapture() && !move.isPromotion();

            const bool givesCheckMove = (lmpCandidate || hmpCandidate || seeCandidate)
                && !terminalKingCapture && pos.givesCheck(move);

            // Late Move Pruning (LMP): keep checking moves and all exclusions.
            if (lmpCandidate && !givesCheckMove)
                continue;

            // History Move Pruning (HMP): do not discard checking moves.
            if (hmpCandidate && !givesCheckMove)
                continue;

            // Static Exchange Evaluation (SEE) Pruning: only investigate
            // losing captures if a check does not already protect the move.
            if (seeCandidate && !givesCheckMove) {
                const int seeThreshold = -tuning.seePruneMarginPerDepth * depth;
                if (!seeGE(pos, move, seeThreshold, SeeMode::Legal))
                    continue;
            }

            if (terminalKingCapture) {
                score = MATE_SCORE - (ply + 1);
                pvLength_[ply + 1] = ply + 1;
            }
            else {
                const bool verifyKing = inCheckNode
                    || !tuning.fastKingLegality
                    || kingCheckCanChange(pos, move);
                StateInfo st;
                pos.makeMove(move, st);
                if (verifyKing && pos.inCheck(us)) {
                    legal = false;
                }
                else {
                    searchStack_[ply + 1].history = HistoryContext{ move, pos.pieceAt(move.to()), us };

                    ++legalMoves;
                    ss.moveCount = legalMoves;
                    if (legalMoves == 1) {
                        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, pvNode);
                    }
                    else {

                        const bool lmrEligible = depth >= 2 && legalMoves >= 2 && quiet;

                        int reduction = 0;

                        if (lmrEligible) {
                            reduction = lmrBaseReduction(depth, legalMoves);
                            
							// If the current node is improving relative to a same-team ancestor,
                            // reduce the LMR reduction by 1 ply. This allows the search to explore
                            // more deeply in positions that are showing promise, while still applying
                            // LMR to less promising lines.
                            reduction += !ss.improving;

							// If this is a PV node, reduce the LMR reduction by 1 ply. This allows the
                            // search to explore more deeply in the principal variation, while still applying
                            // LMR to less promising lines.
                            reduction -= pvNode;
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
            if (terminalKingCapture) {
                ++legalMoves;
                ss.moveCount = legalMoves;
            }

            if (shouldStop())
                break;

            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
                ss.bestMove = move;
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

        activeTT_->store(pos.key(), depth, bestScore, bound, bestMove, pvNode, ply);
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
            ? totalNodes() * 1000ull / static_cast<std::uint64_t>(ms)
            : totalNodes() * 1000ull;

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

        line << " nodes " << totalNodes()
            << " nps " << nps
            << " hashfull " << activeTT_->hashfull()
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

    void SearchEngine::emitBestMove(std::ostream& out, Move move, Move ponderMove) {
        std::lock_guard lock(outputMutex_);
        out << "bestmove " << (move ? moveToString(move) : "0000");
        if (move && limits_.ponderEnabled && ponderMove)
            out << " ponder " << moveToString(ponderMove);
        out << '\n';
        out.flush();
    }

    void SearchEngine::run(Position position, std::ostream& out) {
        startTime_ = std::chrono::steady_clock::now();
        budgetStartTime_ = startTime_;
        const TimeBudget timeBudget = calculateTimeBudget(position, limits_);
        minimumTimeBudgetMs_ = timeBudget.minimumMs;
        softTimeBudgetMs_ = timeBudget.softMs;
        hardTimeBudgetMs_ = timeBudget.hardMs;
        pondering_ = limits_.ponder;
        nodes_ = 0;
        nextTimeCheckNode_ = 0;
        selDepth_ = 0;
        for (auto& row : pv_)
            row.fill(Move{});
        pvLength_.fill(0);
        searchStack_.fill(SearchStackEntry{});

        MoveList rootLegal;
        generateLegalMoves(position, rootLegal);
        Move bestMove = rootLegal.size ? rootLegal.moves[0] : Move{};
        Move bestPonderMove{};
        Move previousBestMove{};
        int previousBestScore = 0;
        bool havePreviousIteration = false;
        int stableBestDepths = 0;
        int bestScore = 0;

        if (rootLegal.size == 0) {
            emitBestMove(out, Move{}, Move{});
            return;
        }

        const auto& tuning = Tuning::search();
        const int requestedDepth = limits_.depth > 0 ? std::min(limits_.depth, MAX_PLY - 1) : 0;
        int depth = helperMode_ ? 1 + (helperIndex_ % 3) : 1;

        while (!shouldStop()) {
            if (requestedDepth > 0 && depth > requestedDepth)
                break;

            if (!limits_.infinite && !limits_.ponder && requestedDepth == 0 && limits_.nodes == 0
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

            bool bestMoveChanged = false;
            int scoreDelta = 0;

            if (pvLength_[0] > 0 && pv_[0][0]) {
                bestMove = pv_[0][0];
                bestPonderMove = pvLength_[0] > 1 ? pv_[0][1] : Move{};
                bestScore = score;

                bestMoveChanged = havePreviousIteration && bestMove != previousBestMove;
                if (havePreviousIteration && !bestMoveChanged)
                    ++stableBestDepths;
                else
                    stableBestDepths = 0;

                scoreDelta = havePreviousIteration
                    ? std::max(previousBestScore - bestScore, bestScore - previousBestScore)
                    : 0;

                previousBestMove = bestMove;
                previousBestScore = bestScore;
                havePreviousIteration = true;

                emitInfo(
                    out,
                    depth,
                    bestScore,
                    pv_[0].data(),
                    pvLength_[0]
                );

                // Stop after a completed root iteration, never on an
                // intermediate aspiration lower/upper bound.
                if (shouldStopOnWinningMate(bestScore, bestMove))
                    break;
            }

            if (requestedDepth > 0 && depth == requestedDepth)
                break;
            if (limits_.nodes > 0 && totalNodes() >= limits_.nodes)
                break;
            if (iterationTimeExpired(stableBestDepths, bestMoveChanged, scoreDelta))
                break;

            if (depth < MAX_PLY - 1)
                ++depth;
            else if (!limits_.infinite)
                break;
        }

        emitBestMove(out, bestMove, bestPonderMove);
    }

}
