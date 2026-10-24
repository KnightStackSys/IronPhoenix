#include "ironphoenix/king_eval.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"
#include "ironphoenix/tuning.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace ironphoenix::Eval {
namespace {

constexpr int PHASE_MAX = 24; // Opponent pair: Q=4, R=2, B/N=1

int homeDepth(Color c, Square sq) noexcept {
    switch (c) {
    case RED:    return Geometry::rankOf(sq);
    case BLUE:   return Geometry::fileOf(sq);
    case YELLOW: return BOARD_RANKS - 1 - Geometry::rankOf(sq);
    case GREEN:  return BOARD_FILES - 1 - Geometry::fileOf(sq);
    }
    return 0;
}

int lateral(Color c, Square sq) noexcept {
    return (c == RED || c == YELLOW)
        ? Geometry::fileOf(sq) : Geometry::rankOf(sq);
}

Square sideSquare(Color c, Square king, int offset) noexcept {
    const int f = Geometry::fileOf(king);
    const int r = Geometry::rankOf(king);
    const int x = f + ((c == RED || c == YELLOW) ? offset : 0);
    const int y = r + ((c == BLUE || c == GREEN) ? offset : 0);
    return Geometry::validXY(x, y) ? Geometry::square(x, y) : SQ_NONE;
}

// Geometry depends only on king square and seat. Build the tiny lookup once,
// after Geometry::init(), rather than recomputing it for millions of evals.
// The C++11 function-local static initialization is thread-safe for Lazy SMP.
struct KingMasks {
    Bitboard storm{};
    Bitboard rookRays{};
    Bitboard bishopRays{};
    std::array<std::array<Bitboard, 3>, COLOR_NB> shield{};
    std::array<std::array<bool, 3>, COLOR_NB> shieldLane{};
    std::array<std::uint8_t, COLOR_NB> home{};
    std::array<std::uint8_t, COLOR_NB> lateral{};
    std::uint8_t centralDistance = 0;
};

struct Lookup {
    std::array<KingMasks, SQUARE_NB> kings{};
    std::array<std::array<Bitboard, 2>, COLOR_NB> wings{};

    Lookup() noexcept {
        for (unsigned i = 0; i < SQUARE_NB; ++i) {
            const Square sq = static_cast<Square>(i);
            const int f = Geometry::fileOf(sq), r = Geometry::rankOf(sq);
            if (!Geometry::validXY(f, r))
                continue;
            auto& m = kings[i];
            m.rookRays = Geometry::Ray[NORTH][sq]
                | Geometry::Ray[SOUTH][sq]
                | Geometry::Ray[EAST][sq]
                | Geometry::Ray[WEST][sq];
            m.bishopRays = Geometry::Ray[NORTH_EAST][sq]
                | Geometry::Ray[NORTH_WEST][sq]
                | Geometry::Ray[SOUTH_EAST][sq]
                | Geometry::Ray[SOUTH_WEST][sq];
            m.centralDistance = static_cast<std::uint8_t>(
                std::min(std::abs(f - 6), std::abs(f - 7))
                + std::min(std::abs(r - 6), std::abs(r - 7)));

            for (int dx = -3; dx <= 3; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    if (Geometry::validXY(f + dx, r + dy))
                        m.storm.set(Geometry::square(f + dx, r + dy));
                }
            }

            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const auto c = static_cast<Color>(ci);
                m.home[ci] = static_cast<std::uint8_t>(homeDepth(c, sq));
                m.lateral[ci] = static_cast<std::uint8_t>(lateral(c, sq));
                if (m.home[ci] > 4)
                    continue;
                for (int lane = -1; lane <= 1; ++lane) {
                    const Square start = sideSquare(c, sq, lane);
                    if (start == SQ_NONE)
                        continue;
                    m.shieldLane[ci][lane + 1] = true;
                    const Square first = Geometry::step(start, Geometry::pawnForward(c));
                    if (first != SQ_NONE) {
                        m.shield[ci][lane + 1].set(first);
                        const Square second = Geometry::step(first, Geometry::pawnForward(c));
                        if (second != SQ_NONE)
                            m.shield[ci][lane + 1].set(second);
                    }
                }
            }
        }
        for (unsigned sq = 0; sq < SQUARE_NB; ++sq) {
            const int f = Geometry::fileOf(static_cast<Square>(sq));
            const int r = Geometry::rankOf(static_cast<Square>(sq));
            if (!Geometry::validXY(f, r))
                continue;
            for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
                const int across = lateral(static_cast<Color>(ci), static_cast<Square>(sq));
                if (across <= 4)
                    wings[ci][0].set(static_cast<Square>(sq));
                if (across >= 9)
                    wings[ci][1].set(static_cast<Square>(sq));
            }
        }
    }
};

const Lookup& geometryMasks() noexcept {
    static const Lookup lookup{};
    return lookup;
}

int castleRights(const Position& pos, Color c) noexcept {
    if (pos.castlingRights() == 0)
        return 0;
    int available = 0;
    for (unsigned lane = 0; lane < 2; ++lane) {
        const CastleLane& def = pos.castleDefinition(c, lane);
        if (!def.valid() || def.rightBit >= 8
            || !(pos.castlingRights() & (1u << def.rightBit)))
            continue;
        if (pos.kingSquare(c) == def.kingFrom
            && pos.pieceAt(def.kingFrom) == makePiece(c, KING)
            && pos.pieceAt(def.rookFrom) == makePiece(c, ROOK))
            ++available;
    }
    return available;
}

struct TeamData {
    Bitboard pawns{};
    Bitboard occupants{};
    Bitboard orthogonalSliders{};
    Bitboard diagonalSliders{};
    int phase = 0;
};

struct FastContext {
    TeamData teams[2]{};
    Bitboard occupancy{};
};

FastContext makeContext(const Position& pos) noexcept {
    FastContext ctx{};
    ctx.occupancy = pos.occupancy();
    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color c = static_cast<Color>(ci);
        if (!pos.isAlive(c))
            continue;
        auto& t = ctx.teams[teamOf(c)];
        const Bitboard queens = pos.pieces(c, QUEEN);
        const Bitboard rooks = pos.pieces(c, ROOK);
        const Bitboard bishops = pos.pieces(c, BISHOP);
        const Bitboard knights = pos.pieces(c, KNIGHT);
        t.pawns |= pos.pieces(c, PAWN);
        t.occupants |= pos.occupancy(c);
        t.orthogonalSliders |= rooks | queens;
        t.diagonalSliders |= bishops | queens;
        t.phase += 4 * queens.popcount() + 2 * rooks.popcount()
                 + bishops.popcount() + knights.popcount();
    }
    ctx.teams[0].phase = std::min(PHASE_MAX, ctx.teams[0].phase);
    ctx.teams[1].phase = std::min(PHASE_MAX, ctx.teams[1].phase);
    return ctx;
}

// A screened friendly piece in a slider attack ray is a true geometric
// pin (to a king) or x-ray (to a queen). Current-occupancy rook/bishop
// lookups identify the FIRST occupied square in all directions at once;
// only candidate friendly screens then need a second ray lookup.
int rayPressure(const Position& pos, Square target,
                Bitboard friends, const TeamData& opponents,
                Bitboard occupancy, Bitboard sliderAttacks) noexcept {
    int count = 0;
    const auto& kingMasks = geometryMasks().kings[target];
    // An x-ray/pin on a king or queen requires a hostile slider already
    // collinear with it. If none exists, skip occupancy-dependent ray work.
    if (opponents.orthogonalSliders
        && (!Tuning::search().kingRayPrefilter
            || (opponents.orthogonalSliders & kingMasks.rookRays))) {
        Bitboard screens = pos.rookAttacks(target) & friends & sliderAttacks;
        while (screens) {
            const Square screen = popLsb(screens);
            if (typeOf(pos.pieceAt(screen)) == KING)
                continue;
            const int f = Geometry::fileOf(screen) - int(Geometry::fileOf(target));
            const int r = Geometry::rankOf(screen) - int(Geometry::rankOf(target));
            const Direction dir = f ? (f > 0 ? EAST : WEST)
                                    : (r > 0 ? NORTH : SOUTH);
            if (Geometry::rayAttack(screen, dir, occupancy)
                    & opponents.orthogonalSliders)
                ++count;
        }
    }
    if (opponents.diagonalSliders
        && (!Tuning::search().kingRayPrefilter
            || (opponents.diagonalSliders & kingMasks.bishopRays))) {
        Bitboard screens = pos.bishopAttacks(target) & friends & sliderAttacks;
        while (screens) {
            const Square screen = popLsb(screens);
            if (typeOf(pos.pieceAt(screen)) == KING)
                continue;
            const int f = Geometry::fileOf(screen) - int(Geometry::fileOf(target));
            const int r = Geometry::rankOf(screen) - int(Geometry::rankOf(target));
            const Direction dir = f > 0
                ? (r > 0 ? NORTH_EAST : SOUTH_EAST)
                : (r > 0 ? NORTH_WEST : SOUTH_WEST);
            if (Geometry::rayAttack(screen, dir, occupancy)
                    & opponents.diagonalSliders)
                ++count;
        }
    }
    return std::min(4, count);
}

int kingScore(const Position& pos, Color c,
              const Bitboard (&attacks)[2], const Bitboard (&sliderAttacks)[2],
              const FastContext& ctx,
              const Lookup& lookup, const Tuning::EvalParameters& w) noexcept {
    if (!pos.isAlive(c))
        return 0;
    const Square king = pos.kingSquare(c);
    if (king == SQ_NONE)
        return 0;

    const unsigned ci = static_cast<unsigned>(c);
    const int team = teamOf(c);
    const TeamData& own = ctx.teams[team];
    const TeamData& foe = ctx.teams[team ^ 1];
    const KingMasks& m = lookup.kings[king];
    const int phase = foe.phase;
    const int endPhase = PHASE_MAX - phase;
    int score = 0;

    if (phase) {
        int danger = 0;
        if (m.home[ci] <= 4) {
            if (w.kingShieldPenalty) {
                int missing = 0;
                for (int lane = 0; lane < 3; ++lane)
                    missing += m.shieldLane[ci][lane]
                        && !(m.shield[ci][lane] & own.pawns);
                danger += w.kingShieldPenalty * missing;
            }
            if (w.kingPawnStormPenalty)
                danger += w.kingPawnStormPenalty
                    * std::min(4, (m.storm & foe.pawns).popcount());
        }

        const Bitboard hostile = attacks[team ^ 1];
        const Bitboard zone = Geometry::KingAttacks[king];
        if (w.kingZoneAttackPenalty)
            danger += w.kingZoneAttackPenalty
                * std::min(8, (zone & hostile).popcount());

        // This is a heuristic danger pattern, not a mate conclusion.
        if (w.kingMateThreatPenalty && hostile.test(king)) {
            const int escapes = (zone & ~ctx.occupancy & ~hostile).popcount();
            if (escapes <= 2)
                danger += w.kingMateThreatPenalty * (3 - escapes);
        }

        if (w.kingCastlingRightsBonus && pos.castlingRights())
            score += phase * w.kingCastlingRightsBonus
                * castleRights(pos, c) / PHASE_MAX;

        // Every absolute pin needs a friendly screen already attacked
        // by an enemy slider. Use existing mobility attack maps to avoid
        // unnecessary x-ray geometry in the common case.
        if (w.kingPinPenalty && (sliderAttacks[team ^ 1] & own.occupants))
            danger += w.kingPinPenalty
                * rayPressure(pos, king, own.occupants, foe,
                              ctx.occupancy, sliderAttacks[team ^ 1]);

        score -= danger * phase / PHASE_MAX;
    }

    if (endPhase) {
        if (w.kingCentralizationBonus)
            score += w.kingCentralizationBonus
                * (12 - int(m.centralDistance)) * endPhase / PHASE_MAX;

        if (w.kingPawnlessWingPenalty) {
            const int across = m.lateral[ci];
            if (across <= 4) {
                if (!(lookup.wings[ci][0] & own.pawns))
                    score -= w.kingPawnlessWingPenalty * endPhase / PHASE_MAX;
            } else if (across >= 9) {
                if (!(lookup.wings[ci][1] & own.pawns))
                    score -= w.kingPawnlessWingPenalty * endPhase / PHASE_MAX;
            }
        }
    }

    if (w.kingXrayPenalty && (foe.orthogonalSliders || foe.diagonalSliders)
        && (sliderAttacks[team ^ 1] & own.occupants)) {
        Bitboard queens = pos.pieces(c, QUEEN);
        int rays = 0;
        while (queens)
            rays += rayPressure(pos, popLsb(queens), own.occupants,
                                foe, ctx.occupancy, sliderAttacks[team ^ 1]);
        score -= w.kingXrayPenalty * std::min(4, rays);
    }
    return score;
}

} // namespace

int kingFeatureDifference(const Position& pos,
    const Bitboard (&teamAttacks)[2],
    const Bitboard (&sliderAttacks)[2]) noexcept {
    const auto& w = Tuning::eval();
    if (!(w.kingShieldPenalty || w.kingPawnStormPenalty
        || w.kingZoneAttackPenalty || w.kingMateThreatPenalty
        || w.kingCentralizationBonus || w.kingPawnlessWingPenalty
        || w.kingPinPenalty || w.kingXrayPenalty
        || w.kingCastlingRightsBonus))
        return 0;

    const FastContext ctx = makeContext(pos);
    const Lookup& lookup = geometryMasks();
    int teams[2]{};
    for (unsigned i = 0; i < COLOR_NB; ++i) {
        const Color c = static_cast<Color>(i);
        teams[teamOf(c)] += kingScore(pos, c, teamAttacks, sliderAttacks,
                                      ctx, lookup, w);
    }
    return teams[0] - teams[1];
}

} // namespace ironphoenix::Eval
