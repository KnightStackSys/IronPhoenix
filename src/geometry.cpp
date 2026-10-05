#include "ironphoenix/geometry.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <mutex>

namespace ironphoenix::Geometry {

std::array<std::array<Square, BOARD_RANKS>, BOARD_FILES> SquareFromXY{};
std::array<std::uint8_t, SQUARE_NB> FileOf{};
std::array<std::uint8_t, SQUARE_NB> RankOf{};
std::array<std::array<Square, 8>, SQUARE_NB> Step{};
std::array<std::array<Bitboard, SQUARE_NB>, 8> Ray{};
std::array<Bitboard, SQUARE_NB> KnightAttacks{};
std::array<Bitboard, SQUARE_NB> KingAttacks{};
std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttacks{};
std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttackers{};
Bitboard BoardMask{};

namespace {
std::once_flag initFlag;

constexpr std::array<int, 8> DX = { 0, 1, 0, -1, 1, -1, 1, -1 };
constexpr std::array<int, 8> DY = { 1, 0, -1, 0, 1, 1, -1, -1 };

constexpr std::array<std::array<int, 2>, 8> KNIGHT_DELTA = {{
    {{ 1,  2}}, {{ 2,  1}}, {{ 2, -1}}, {{ 1, -2}},
    {{-1, -2}}, {{-2, -1}}, {{-2,  1}}, {{-1,  2}}
}};

void addPawnAttack(Color c, Square from, int dx, int dy) {
    const int f = static_cast<int>(FileOf[from]) + dx;
    const int r = static_cast<int>(RankOf[from]) + dy;
    if (!validXY(f, r))
        return;
    const Square to = SquareFromXY[static_cast<unsigned>(f)][static_cast<unsigned>(r)];
    PawnAttacks[static_cast<unsigned>(c)][from].set(to);
    PawnAttackers[static_cast<unsigned>(c)][to].set(from);
}

void initialize() {
    for (auto& file : SquareFromXY)
        file.fill(SQ_NONE);
    for (auto& n : Step)
        n.fill(SQ_NONE);

    Square dense = 0;
    for (int rank = 0; rank < BOARD_RANKS; ++rank) {
        for (int file = 0; file < BOARD_FILES; ++file) {
            if (!validXY(file, rank))
                continue;
            assert(dense < SQUARE_NB);
            SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)] = dense;
            FileOf[dense] = static_cast<std::uint8_t>(file);
            RankOf[dense] = static_cast<std::uint8_t>(rank);
            BoardMask.set(dense);
            ++dense;
        }
    }
    assert(dense == SQUARE_NB);

    for (unsigned s = 0; s < SQUARE_NB; ++s) {
        const Square sq = static_cast<Square>(s);
        const int f = FileOf[sq];
        const int r = RankOf[sq];

        for (unsigned d = 0; d < 8; ++d) {
            const int nf = f + DX[d];
            const int nr = r + DY[d];
            Step[sq][d] = validXY(nf, nr)
                ? SquareFromXY[static_cast<unsigned>(nf)][static_cast<unsigned>(nr)]
                : SQ_NONE;

            int rf = nf;
            int rr = nr;
            while (validXY(rf, rr)) {
                const Square rs = SquareFromXY[static_cast<unsigned>(rf)][static_cast<unsigned>(rr)];
                Ray[d][sq].set(rs);
                rf += DX[d];
                rr += DY[d];
            }
        }

        for (unsigned d = 0; d < 8; ++d) {
            if (Step[sq][d] != SQ_NONE)
                KingAttacks[sq].set(Step[sq][d]);
        }

        for (const auto& delta : KNIGHT_DELTA) {
            const int nf = f + delta[0];
            const int nr = r + delta[1];
            if (validXY(nf, nr))
                KnightAttacks[sq].set(SquareFromXY[static_cast<unsigned>(nf)][static_cast<unsigned>(nr)]);
        }

        addPawnAttack(RED,    sq, -1, +1);
        addPawnAttack(RED,    sq, +1, +1);
        addPawnAttack(BLUE,   sq, +1, -1);
        addPawnAttack(BLUE,   sq, +1, +1);
        addPawnAttack(YELLOW, sq, -1, -1);
        addPawnAttack(YELLOW, sq, +1, -1);
        addPawnAttack(GREEN,  sq, -1, -1);
        addPawnAttack(GREEN,  sq, -1, +1);
    }
}
}

void init() {
    std::call_once(initFlag, initialize);
}

}
