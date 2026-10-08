#include "ironphoenix/lmr.hpp"

#include <algorithm>
#include <cmath>

namespace ironphoenix {

    namespace {

        LmrReductionTable buildLmrReductionTable() {
            LmrReductionTable table{};

            for (int depth = 1; depth <= LMR_MAX_DEPTH; ++depth) {
                for (int moveCount = 1; moveCount <= LMR_MAX_MOVES; ++moveCount) {

                    if (depth < 3 || moveCount <= 3) {
                        table[depth][moveCount] = 0;
                        continue;
                    }
                    
                    const double raw =
                        0.75
                        + (std::log(static_cast<double>(depth))
                            * std::log(static_cast<double>(moveCount))) / 2.75;

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

    const LmrReductionTable lmrReduction = buildLmrReductionTable();

}
