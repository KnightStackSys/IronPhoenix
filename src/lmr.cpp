#include "ironphoenix/lmr.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace ironphoenix {

    namespace {

        LmrReductionTable buildLmrReductionTable() noexcept {
            LmrReductionTable table{};
            const auto& params = Tuning::search();

            const double base = static_cast<double>(params.lmrBaseX100) / 100.0;
            const double divisor = static_cast<double>(std::max(1, params.lmrDivisorX100)) / 100.0;

            for (int depth = 1; depth <= LMR_MAX_DEPTH; ++depth) {
                for (int moveCount = 1; moveCount <= LMR_MAX_MOVES; ++moveCount) {

                    if (depth < params.lmrMinDepth || moveCount <= params.lmrFullMoves) {
                        table[depth][moveCount] = 0;
                        continue;
                    }

                    const double raw =
                        base
                        + (std::log(static_cast<double>(depth))
                            * std::log(static_cast<double>(moveCount))) / divisor;

                    int reduction = static_cast<int>(raw);

                    reduction = std::max(reduction, 1);
                    reduction = std::min(reduction, std::max(1, depth - 2));

                    table[depth][moveCount] =
                        static_cast<std::uint8_t>(reduction);
                }
            }

            return table;
        }

    }

    LmrReductionTable lmrReduction = buildLmrReductionTable();

    void rebuildLmrReductionTable() noexcept {
        lmrReduction = buildLmrReductionTable();
    }

    int lmrQuietReduction(int depth, int moveCount, bool pvNode, int historyScore) noexcept {
        const auto& params = Tuning::search();
        // Keep the eligibility gates ahead of every adjustment, including when
        // tuning disables reductions for early moves or shallow depths.
        if (depth < std::max(3, params.lmrMinDepth)
            || moveCount <= std::max(1, params.lmrFullMoves))
            return 0;

        int reduction = lmrBaseReduction(depth, moveCount);
        reduction += pvNode ? -1 : 1;
        // Poor history earns up to two extra plies; reliable quiets and
        // countermoves recover up to two. Zero history remains neutral.
        reduction -= std::clamp(historyScore / 8000, -2, 2);
        return std::clamp(reduction, 0, depth - 2);
    }

}

