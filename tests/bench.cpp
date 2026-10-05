#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

using namespace ironphoenix;

static Square sq(char file, int rank) {
    return Geometry::square(file - 'a', rank - 1);
}

int main() {
    Geometry::init();
    Position p;
    p.clear();

    for (unsigned s = 0; s < SQUARE_NB; s += 5) {
        const Color c = static_cast<Color>((s / 5) & 3u);
        const PieceType pt = static_cast<PieceType>(1 + ((s / 5) % 5));
        p.placePiece(static_cast<Square>(s), makePiece(c, pt));
    }
    
    p.placePiece(sq('g', 1), makePiece(RED, KING));
    p.placePiece(sq('a', 7), makePiece(BLUE, KING));
    p.placePiece(sq('g', 14), makePiece(YELLOW, KING));
    p.placePiece(sq('n', 7), makePiece(GREEN, KING));
    p.finalizeSetup();

    constexpr std::uint64_t N = 20'000'000;
    volatile std::uint64_t sink = 0;
    auto begin = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < N; ++i) {
        const Square s = static_cast<Square>((i * 37u) % SQUARE_NB);
        const Bitboard a = Geometry::queenAttacks(s, p.occupancy());
        sink = sink ^ a.w0 ^ a.w1 ^ a.w2;
    }
    auto end = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(end - begin).count();
    std::cout << "queenAttacks calls/s: " << static_cast<std::uint64_t>(N / sec)
              << "  sink=" << sink << "\n";
}
