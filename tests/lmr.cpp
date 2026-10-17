#include "ironphoenix/lmr.hpp"
#include "ironphoenix/tuning.hpp"
#include "ironphoenix/geometry.hpp"
#include "ironphoenix/movegen.hpp"
#include "ironphoenix/search.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

using namespace ironphoenix;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}

int main() {
    Tuning::resetSearch();
    for (int depth = 1; depth <= 128; ++depth) {
        for (int count = 1; count <= 512; ++count) {
            for (bool pv : {false, true}) {
                const int bad = lmrQuietReduction(depth, count, pv, -16000);
                const int neutral = lmrQuietReduction(depth, count, pv, 0);
                const int good = lmrQuietReduction(depth, count, pv, 32000);
                require(good >= 0 && good <= neutral && neutral <= bad,
                    "history must monotonically protect reliable moves");
                require(bad <= std::max(0, depth - 2), "must retain a full child ply");
                if (depth < 3 || count <= 2)
                    require(bad == 0, "adjustments must not bypass eligibility");
            }
            require(lmrQuietReduction(depth, count, true, 0)
                <= lmrQuietReduction(depth, count, false, 0), "PV must be protected");
        }
    }
    require(lmrQuietReduction(8, 16, false, 0) > 3,
        "late non-PV quiets should reduce more than the former default");
    require(Tuning::setSearchParameter("SearchLMRFullMoves", 128), "set full moves");
    require(lmrQuietReduction(12, 128, false, -16000) == 0, "respect tuned full moves");
    require(Tuning::setSearchParameter("SearchLMRMinDepth", 20), "set min depth");
    require(lmrQuietReduction(19, 200, false, -16000) == 0, "respect tuned min depth");
    Tuning::resetSearch();

    Geometry::init();
    // Every mover/team must retain terminal king captures, including when its
    // own king is checked. These bypass ordinary legality and reduction gates.
    for (unsigned color = 0; color < 4; ++color) {
        Position p;
        p.clear();
        const Color us = static_cast<Color>(color);
        const Color them = nextColor(us);
        const Color partner = static_cast<Color>(color ^ 2u);
        const Color other = static_cast<Color>(static_cast<unsigned>(them) ^ 2u);
        p.placePiece(sq('g', 1), makePiece(us, KING));
        p.placePiece(sq('a', 7), makePiece(them, KING));
        p.placePiece(sq('g', 14), makePiece(partner, KING));
        p.placePiece(sq('n', 7), makePiece(other, KING));
        p.placePiece(sq('d', 7), makePiece(us, ROOK));
        p.placePiece(sq('g', 3), makePiece(them, ROOK));
        p.setSideToMove(us);
        p.finalizeSetup();
        require(p.inCheck(), "fixture must start in check");
        auto engine = std::make_unique<SearchEngine>();
        std::ostringstream output;
        SearchLimits limits;
        limits.depth = 5;
        engine->start(p, limits, output);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (engine->searching() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool finished = !engine->searching();
        engine->stopAndWait();
        require(finished, "terminal regression timed out");
        require(output.str().find("score mate 1") != std::string::npos, "mate score changed");
        require(output.str().find("bestmove d7a7") != std::string::npos, "king capture lost");
    }
    std::cout << "LMR regressions: PASS\n";
}
