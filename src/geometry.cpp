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
std::array<Bitboard, SQUARE_NB> KnightCheckApproach{};
std::array<Bitboard, SQUARE_NB> KnightEnemyProximity{};
std::array<Bitboard, SQUARE_NB> KingAttacks{};
std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttacks{};
std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PawnAttackers{};
Bitboard BoardMask{};

std::array<std::array<std::uint8_t, SQUARE_NB>, LINE_FAMILY_NB> LineId{};
std::array<std::array<std::uint8_t, SQUARE_NB>, LINE_FAMILY_NB> LinePos{};
std::array<std::array<std::uint16_t, SQUARE_NB>, LINE_FAMILY_NB> LineBit{};
std::array<std::uint16_t, LINE_NB> LineBlockers{};
std::array<std::array<std::uint16_t, LINE_OCCUPANCIES>, LINE_BITS> LineAttackMask{};
std::array<std::array<std::array<Bitboard, LINE_HALF_OCCUPANCIES>, 2>, LINE_NB> LineExpand{};

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

void initializeLineTables() {
    std::array<std::array<Square, LINE_BITS>, LINE_NB> lineSquare{};
    for (auto& line : lineSquare)
        line.fill(SQ_NONE);

    // Ranks: position bit is the file coordinate.
    for (int rank = 0; rank < BOARD_RANKS; ++rank) {
        const unsigned line = RANK_LINE_BASE + static_cast<unsigned>(rank);
        for (int file = 0; file < BOARD_FILES; ++file) {
            const unsigned pos = static_cast<unsigned>(file);
            if (!validXY(file, rank)) {
                LineBlockers[line] |= static_cast<std::uint16_t>(1u << pos);
                continue;
            }
            lineSquare[line][pos] = SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)];
        }
    }

    // Files: position bit is the rank coordinate.
    for (int file = 0; file < BOARD_FILES; ++file) {
        const unsigned line = FILE_LINE_BASE + static_cast<unsigned>(file);
        for (int rank = 0; rank < BOARD_RANKS; ++rank) {
            const unsigned pos = static_cast<unsigned>(rank);
            if (!validXY(file, rank)) {
                LineBlockers[line] |= static_cast<std::uint16_t>(1u << pos);
                continue;
            }
            lineSquare[line][pos] = SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)];
        }
    }

    // Diagonal A: file - rank is constant; position bit is file.
    for (int delta = -13; delta <= 13; ++delta) {
        const unsigned line = DIAG_A_LINE_BASE + static_cast<unsigned>(delta + 13);
        for (int file = 0; file < BOARD_FILES; ++file) {
            const int rank = file - delta;
            const unsigned pos = static_cast<unsigned>(file);
            if (!validXY(file, rank)) {
                LineBlockers[line] |= static_cast<std::uint16_t>(1u << pos);
                continue;
            }
            lineSquare[line][pos] = SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)];
        }
    }

    // Diagonal B: file + rank is constant; position bit is file.
    for (int sum = 0; sum <= 26; ++sum) {
        const unsigned line = DIAG_B_LINE_BASE + static_cast<unsigned>(sum);
        for (int file = 0; file < BOARD_FILES; ++file) {
            const int rank = sum - file;
            const unsigned pos = static_cast<unsigned>(file);
            if (!validXY(file, rank)) {
                LineBlockers[line] |= static_cast<std::uint16_t>(1u << pos);
                continue;
            }
            lineSquare[line][pos] = SquareFromXY[static_cast<unsigned>(file)][static_cast<unsigned>(rank)];
        }
    }

    // One universal 14-bit attack table. Board holes are already present in a
    // Position's line occupancy as blockers, so the same table works for ranks,
    // files and both diagonal families.
    for (unsigned pos = 0; pos < LINE_BITS; ++pos) {
        for (unsigned occ = 0; occ < LINE_OCCUPANCIES; ++occ) {
            std::uint16_t attacks = 0;

            for (int i = static_cast<int>(pos) - 1; i >= 0; --i) {
                const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(i));
                attacks |= bit;
                if (occ & bit)
                    break;
            }

            for (unsigned i = pos + 1; i < LINE_BITS; ++i) {
                const auto bit = static_cast<std::uint16_t>(1u << i);
                attacks |= bit;
                if (occ & bit)
                    break;
            }

            LineAttackMask[pos][occ] = attacks;
        }
    }

    // Expand a 14-bit line mask back to the engine's 3x64 Bitboard with two
    // tiny 7-bit table lookups. Keeping these tables small (~0.5 MiB total)
    // is much friendlier to cache than storing a full Bitboard for every
    // square/occupancy combination.
    for (unsigned line = 0; line < LINE_NB; ++line) {
        for (unsigned half = 0; half < 2; ++half) {
            const unsigned base = half * LINE_HALF_BITS;
            for (unsigned mask = 0; mask < LINE_HALF_OCCUPANCIES; ++mask) {
                Bitboard expanded{};
                for (unsigned bit = 0; bit < LINE_HALF_BITS; ++bit) {
                    if ((mask & (1u << bit)) == 0)
                        continue;
                    const Square sq = lineSquare[line][base + bit];
                    if (sq != SQ_NONE)
                        expanded.set(sq);
                }
                LineExpand[line][half][mask] = expanded;
            }
        }
    }
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

        const unsigned rankLine = RANK_LINE_BASE + static_cast<unsigned>(r);
        const unsigned fileLine = FILE_LINE_BASE + static_cast<unsigned>(f);
        const unsigned diagALine = DIAG_A_LINE_BASE + static_cast<unsigned>(f - r + 13);
        const unsigned diagBLine = DIAG_B_LINE_BASE + static_cast<unsigned>(f + r);

        LineId[LINE_RANK][sq] = static_cast<std::uint8_t>(rankLine);
        LineId[LINE_FILE][sq] = static_cast<std::uint8_t>(fileLine);
        LineId[LINE_DIAG_A][sq] = static_cast<std::uint8_t>(diagALine);
        LineId[LINE_DIAG_B][sq] = static_cast<std::uint8_t>(diagBLine);

        LinePos[LINE_RANK][sq] = static_cast<std::uint8_t>(f);
        LinePos[LINE_FILE][sq] = static_cast<std::uint8_t>(r);
        LinePos[LINE_DIAG_A][sq] = static_cast<std::uint8_t>(f);
        LinePos[LINE_DIAG_B][sq] = static_cast<std::uint8_t>(f);

        LineBit[LINE_RANK][sq] = static_cast<std::uint16_t>(1u << f);
        LineBit[LINE_FILE][sq] = static_cast<std::uint16_t>(1u << r);
        LineBit[LINE_DIAG_A][sq] = static_cast<std::uint16_t>(1u << f);
        LineBit[LINE_DIAG_B][sq] = static_cast<std::uint16_t>(1u << f);

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

    // A knight can check a king on 'king' by landing on any square in
    // KnightAttacks[king]. Precompute possible KNIGHT START squares for
    // that next move; evaluation filters blocked landing squares at runtime.
    for (unsigned s = 0; s < SQUARE_NB; ++s) {
        Bitboard checkingSquares = KnightAttacks[s];
        while (checkingSquares)
            KnightCheckApproach[s] |= KnightAttacks[popLsb(checkingSquares)];
    }

    // Cache two-square Chebyshev neighborhoods for activity against enemy
    // non-king pieces. Board holes are naturally omitted via validXY().
    for (unsigned s = 0; s < SQUARE_NB; ++s) {
        const int file = FileOf[s];
        const int rank = RankOf[s];
        for (int df = -2; df <= 2; ++df) {
            for (int dr = -2; dr <= 2; ++dr) {
                if (df == 0 && dr == 0)
                    continue;
                const int nf = file + df;
                const int nr = rank + dr;
                if (validXY(nf, nr))
                    KnightEnemyProximity[s].set(
                        SquareFromXY[static_cast<unsigned>(nf)][static_cast<unsigned>(nr)]);
            }
        }
    }

    initializeLineTables();
}
}

void init() {
    std::call_once(initFlag, initialize);
}

}
