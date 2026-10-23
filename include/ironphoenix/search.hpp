#pragma once

#include "history.hpp"
#include "search_stack.hpp"
#include "position.hpp"
#include "tt.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <thread>

namespace ironphoenix {

    enum class ScoreBound {
        Exact,
        Lower,
        Upper
    };

    struct SearchLimits {
        int depth = 0;
        std::uint64_t nodes = 0;
        std::int64_t movetimeMs = 0;
        bool infinite = false;
        bool ponder = false;
        bool ponderEnabled = false;
        std::int64_t moveOverheadMs = 50;

        bool hasClock = false;
        std::array<std::int64_t, COLOR_NB> timeMs{};
        std::array<std::int64_t, COLOR_NB> incrementMs{};
        std::array<std::int64_t, COLOR_NB> delayMs{};
    };

    class SearchEngine {
    public:
        static constexpr int MAX_MULTI_PV = 32;

        SearchEngine();
        ~SearchEngine();

        SearchEngine(const SearchEngine&) = delete;
        SearchEngine& operator=(const SearchEngine&) = delete;

        void start(Position position, SearchLimits limits, std::ostream& out);

        // Dedicated root MultiPV search. MultiPV=1 should continue to use
        // start(), preserving the normal PVS/aspiration search path exactly.
        void startMultiPV(Position position, SearchLimits limits, int multiPV, std::ostream& out);

        void stop() noexcept;
        void stopAndWait() noexcept;
        void ponderHit() noexcept;

        [[nodiscard]] bool searching() const noexcept { return searching_.load(std::memory_order_relaxed); }

        bool setHashSizeMB(std::size_t megabytes);
        [[nodiscard]] std::size_t hashSizeMB() const noexcept { return tt_.sizeMB(); }
        void clearHash() noexcept;
        void newGame() noexcept;

    private:
        // Exposed only to the mate-stop regression test.
        friend struct MateStopTestAccess;
        friend struct ReverseFutilityTestAccess;
        [[nodiscard]] bool reverseFutilitySafe(Position& pos) const;

        struct TimeBudget final {
            std::int64_t minimumMs = 0;
            std::int64_t softMs = 0;
            std::int64_t hardMs = 0;
        };

        static constexpr int MAX_PLY = 128;
        static constexpr int INF = 32000;
        static constexpr int MATE_SCORE = 30000;
        static constexpr int MATE_THRESHOLD = 29000;
        static constexpr std::uint64_t TIME_CHECK_NODE_INTERVAL = 1024;

        TranspositionTable tt_{ 64 };
        HistoryTables history_{};
        std::thread worker_;
        std::atomic<bool> stopRequested_{ false };
        std::atomic<bool> searching_{ false };
        std::atomic<bool> ponderHitRequested_{ false };

        SearchLimits limits_{};
        std::chrono::steady_clock::time_point startTime_{};
        std::chrono::steady_clock::time_point budgetStartTime_{};
        std::int64_t minimumTimeBudgetMs_ = 0;
        std::int64_t softTimeBudgetMs_ = 0;
        std::int64_t hardTimeBudgetMs_ = 0;
        bool pondering_ = false;
        std::uint64_t nodes_ = 0;
        std::uint64_t nextTimeCheckNode_ = 0;
        int selDepth_ = 0;

        std::array<std::array<Move, MAX_PLY>, MAX_PLY> pv_{};
        std::array<int, MAX_PLY> pvLength_{};
        std::array<SearchStackEntry, MAX_PLY> searchStack_{};
        mutable std::mutex outputMutex_;

        void run(Position position, std::ostream& out);
        void runMultiPV(Position position, int multiPV, std::ostream& out);

        int negamax(Position& pos, int depth, int alpha, int beta, int ply,
                    bool pvNode, bool allowNull = true);
        int qsearch(Position& pos, int alpha, int beta, int ply, bool pvNode);
        int evaluate(const Position& pos) const noexcept;
        void recordStackEvaluation(int ply, int score) noexcept;
        int moveScore(const Position& pos, Move move, Move ttMove, int ply, bool givesCheck) const noexcept;
        void orderMoves(const Position& pos,
                        Move* begin,
                        Move* end,
                        Move ttMove,
                        int ply,
                        int* orderedScores = nullptr,
                        std::uint64_t* givesCheckBits = nullptr) const;

        [[nodiscard]] int historyBonus(int depth) const noexcept;
        void updateQuietCutoff(const Position& pos, Color mover, Move cutoffMove, Piece movingPiece,
            int depth, int ply, const Move* failedQuiets,
            std::size_t failedQuietCount) noexcept;
        void updateCaptureCutoff(Color mover, Move cutoffMove, Piece movingPiece, int depth,
            const Move* failedCaptures, std::size_t failedCaptureCount,
            const Position& pos) noexcept;

        [[nodiscard]] bool shouldStop() noexcept;
        [[nodiscard]] bool iterationTimeExpired(int stableDepths, bool bestMoveChanged, int scoreDelta) noexcept;
        [[nodiscard]] bool shouldStopOnWinningMate(int score, Move rootMove) const noexcept;
        void processPonderHit() noexcept;
        [[nodiscard]] std::int64_t elapsedMs() const noexcept;
        [[nodiscard]] std::int64_t budgetElapsedMs() const noexcept;
        [[nodiscard]] TimeBudget calculateTimeBudget(const Position& pos, const SearchLimits& limits) const noexcept;

        void emitInfo(
            std::ostream& out,
            int depth,
            int score,
            const Move* pv,
            int pvLength,
            ScoreBound bound = ScoreBound::Exact
        );

        void emitMultiPVInfo(
            std::ostream& out,
            int depth,
            int multiPvIndex,
            int score,
            const Move* pv,
            int pvLength
        );

        void emitBestMove(std::ostream& out, Move move, Move ponderMove = Move{});
    };

}
