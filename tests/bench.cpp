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

    const Bitboard occ = p.occupancy();
    auto rayBegin = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < N; ++i) {
        const Square s = static_cast<Square>((i * 37u) % SQUARE_NB);
        const Bitboard a = Geometry::queenAttacks(s, occ);
        sink = sink ^ a.w0 ^ a.w1 ^ a.w2;
    }
    auto rayEnd = std::chrono::steady_clock::now();
    const double raySec = std::chrono::duration<double>(rayEnd - rayBegin).count();

    auto lineBegin = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < N; ++i) {
        const Square s = static_cast<Square>((i * 37u) % SQUARE_NB);
        const Bitboard a = p.queenAttacks(s);
        sink = sink ^ a.w0 ^ a.w1 ^ a.w2;
    }
    auto lineEnd = std::chrono::steady_clock::now();
    const double lineSec = std::chrono::duration<double>(lineEnd - lineBegin).count();

    const auto rayRate = static_cast<std::uint64_t>(N / raySec);
    const auto lineRate = static_cast<std::uint64_t>(N / lineSec);

    std::cout << "ray queenAttacks calls/s:  " << rayRate << '\n'
              << "line queenAttacks calls/s: " << lineRate << '\n'
              << "line/ray speedup:           " << (raySec / lineSec) << "x\n"
              << "sink=" << sink << '\n';
}
