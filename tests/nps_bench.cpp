#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/search.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

using namespace ironphoenix;
namespace {
Square sq(char file, int rank) { return Geometry::square(file - 'a', rank - 1); }

Position searchFixture() {
    // Fixed 4PC Teams nonterminal middlegame-like position. Rotated pawn
    // fronts, both team partners, and multiple bishops exercising all HCE.
    Position p;
    p.clear();
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
    for (char f : {'d','e','f','h','i','j'}) {
        p.placePiece(sq(f, 3), makePiece(RED, PAWN));
        p.placePiece(sq(f, 12), makePiece(YELLOW, PAWN));
    }
    for (int rank : {4,5,6,8,9,10}) {
        p.placePiece(sq('c', rank), makePiece(BLUE, PAWN));
        p.placePiece(sq('l', rank), makePiece(GREEN, PAWN));
    }
    p.placePiece(sq('e', 4), makePiece(RED, BISHOP));
    p.placePiece(sq('i', 4), makePiece(RED, BISHOP));
    p.placePiece(sq('f', 11), makePiece(YELLOW, BISHOP));
    p.placePiece(sq('i', 11), makePiece(YELLOW, BISHOP));
    p.placePiece(sq('d', 6), makePiece(BLUE, BISHOP));
    p.placePiece(sq('d', 9), makePiece(BLUE, BISHOP));
    p.placePiece(sq('k', 5), makePiece(GREEN, BISHOP));
    p.placePiece(sq('k', 9), makePiece(GREEN, BISHOP));
    p.placePiece(sq('f', 5), makePiece(RED, KNIGHT));
    p.placePiece(sq('h', 10), makePiece(YELLOW, KNIGHT));
    p.placePiece(sq('e', 8), makePiece(BLUE, KNIGHT));
    p.placePiece(sq('j', 8), makePiece(GREEN, KNIGHT));
    p.placePiece(sq('g', 2), makePiece(RED, ROOK));
    p.placePiece(sq('g', 13), makePiece(YELLOW, ROOK));
    p.placePiece(sq('b', 7), makePiece(BLUE, ROOK));
    p.placePiece(sq('m', 7), makePiece(GREEN, ROOK));
    p.setSideToMove(RED);
    p.finalizeSetup();
    return p;
}
} // namespace

int main(int argc, char** argv) {
    Geometry::init();
    std::uint64_t requested = 300'000;
    if (argc > 1) {
        try { requested = std::stoull(argv[1]); }
        catch (...) { std::cerr << "Usage: ironphoenix_nps_bench [node_limit]\n"; return 2; }
    }
    if (requested < 10'000 || requested > 100'000'000) {
        std::cerr << "node_limit must be from 10000 to 100000000\n";
        return 2;
    }

    const Position pos = searchFixture();
    auto engine = std::make_unique<SearchEngine>();
    SearchLimits limits{};
    limits.nodes = requested;
    std::ostringstream out;
    const auto start = std::chrono::steady_clock::now();
    engine->start(pos, limits, out);
    while (engine->searching()
           && std::chrono::steady_clock::now() - start < std::chrono::seconds(120))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool finished = !engine->searching();
    engine->stopAndWait();

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    const std::string log = out.str();
    const std::regex info(R"( nodes ([0-9]+) nps ([0-9]+) hashfull )");
    std::uint64_t lastNodes = 0, lastNps = 0;
    for (auto it = std::sregex_iterator(log.begin(), log.end(), info);
         it != std::sregex_iterator(); ++it) {
        lastNodes = std::stoull((*it)[1].str());
        lastNps = std::stoull((*it)[2].str());
    }

    std::cout << "IronPhoenix HCE search throughput, fixed 4PC Teams fixture\n"
              << "Node target: " << requested << "\n"
              << "Elapsed wall ms: " << millis << "\n"
              << "Last completed iteration: " << lastNodes
              << " nodes, " << lastNps << " NPS\n"
              << "Engine finished: " << (finished ? "yes" : "no (time limit)") << '\n';
    // This is a diagnostic, not an NPS threshold test. CI runner CPU differs
    // from local hardware, and iteration counters may be below node budget.
    return finished ? 0 : 1;
}
