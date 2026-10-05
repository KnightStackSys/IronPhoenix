#include "ironphoenix/tt.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace ironphoenix {
namespace {

constexpr int MATE_SCORE = 30000;
constexpr int MATE_THRESHOLD = 29000;
constexpr std::uint8_t BOUND_MASK = 0x03u;
constexpr std::uint8_t PV_MASK = 0x04u;
constexpr unsigned GENERATION_SHIFT = 3u;
constexpr std::uint8_t GENERATION_MASK = 0xF8u;
constexpr std::uint8_t GENERATION_MOD = 32u;

}

TranspositionTable::TranspositionTable(std::size_t megabytes) {
    if (!resizeMB(megabytes))
        resizeMB(1);
}

bool TranspositionTable::resizeMB(std::size_t megabytes) {
    megabytes = std::max<std::size_t>(1, megabytes);
    const std::size_t bytes = megabytes * 1024ull * 1024ull;
    std::size_t clusters = std::max<std::size_t>(1, bytes / sizeof(Cluster));
    clusters = std::bit_floor(clusters);
    if (clusters == 0)
        clusters = 1;

    try {
        std::vector<Cluster> replacement(clusters);
        table_.swap(replacement);
    }
    catch (...) {
        return false;
    }

    mask_ = table_.size() - 1;
    sizeMB_ = std::max<std::size_t>(1, table_.size() * sizeof(Cluster) / (1024ull * 1024ull));
    generation_ = 0;
    return true;
}

void TranspositionTable::clear() noexcept {
    for (Cluster& cluster : table_)
        for (Entry& entry : cluster.entry)
            entry = Entry{};
}

void TranspositionTable::newGeneration() noexcept {
    generation_ = static_cast<std::uint8_t>((generation_ + 1u) & (GENERATION_MOD - 1u));
}

TTBound TranspositionTable::boundOf(const Entry& e) noexcept {
    return static_cast<TTBound>(e.flags & BOUND_MASK);
}

bool TranspositionTable::pvOf(const Entry& e) noexcept {
    return (e.flags & PV_MASK) != 0;
}

std::uint8_t TranspositionTable::generationOf(const Entry& e) noexcept {
    return static_cast<std::uint8_t>((e.flags & GENERATION_MASK) >> GENERATION_SHIFT);
}

int TranspositionTable::scoreToTT(int score, int ply) noexcept {
    if (score >= MATE_THRESHOLD)
        return std::min(MATE_SCORE, score + ply);
    if (score <= -MATE_THRESHOLD)
        return std::max(-MATE_SCORE, score - ply);
    return score;
}

int TranspositionTable::scoreFromTT(int score, int ply) noexcept {
    if (score >= MATE_THRESHOLD)
        return score - ply;
    if (score <= -MATE_THRESHOLD)
        return score + ply;
    return score;
}

bool TranspositionTable::probe(Key key, int ply, TTProbe& out) const noexcept {
    out = TTProbe{};
    if (table_.empty())
        return false;

    const Cluster& cluster = table_[static_cast<std::size_t>(key) & mask_];
    for (const Entry& entry : cluster.entry) {
        if (entry.key == key && entry.key != 0) {
            out.hit = true;
            out.move = Move(entry.move);
            out.score = scoreFromTT(entry.score, ply);
            out.depth = entry.depth;
            out.bound = boundOf(entry);
            out.pvNode = pvOf(entry);
            return true;
        }
    }
    return false;
}

void TranspositionTable::store(Key key,
                               int depth,
                               int score,
                               TTBound bound,
                               Move bestMove,
                               bool pvNode,
                               int ply) noexcept {
    if (table_.empty())
        return;

    Cluster& cluster = table_[static_cast<std::size_t>(key) & mask_];
    Entry* replace = &cluster.entry[0];

    for (Entry& entry : cluster.entry) {
        if (entry.key == key) {
            replace = &entry;
            break;
        }
        if (entry.key == 0) {
            replace = &entry;
            break;
        }

        const unsigned age = (generation_ - generationOf(entry)) & (GENERATION_MOD - 1u);
        const int value = static_cast<int>(entry.depth) - static_cast<int>(age) * 4 + (pvOf(entry) ? 2 : 0);

        const unsigned replaceAge = (generation_ - generationOf(*replace)) & (GENERATION_MOD - 1u);
        const int replaceValue = static_cast<int>(replace->depth) - static_cast<int>(replaceAge) * 4
                               + (pvOf(*replace) ? 2 : 0);
        if (value < replaceValue)
            replace = &entry;
    }

    if (!bestMove && replace->key == key)
        bestMove = Move(replace->move);

    const int ttScore = scoreToTT(score, ply);
    replace->key = key;
    replace->move = bestMove.v;
    replace->score = static_cast<std::int16_t>(std::clamp(ttScore, -32767, 32767));
    replace->depth = static_cast<std::int8_t>(std::clamp(depth, -1, 127));
    replace->flags = static_cast<std::uint8_t>(static_cast<std::uint8_t>(bound)
                     | (pvNode ? PV_MASK : 0u)
                     | ((generation_ & (GENERATION_MOD - 1u)) << GENERATION_SHIFT));
}

int TranspositionTable::hashfull() const noexcept {
    if (table_.empty())
        return 0;

    const std::size_t samples = std::min<std::size_t>(250, table_.size());
    std::size_t used = 0;
    std::size_t total = samples * 4;
    for (std::size_t i = 0; i < samples; ++i) {
        for (const Entry& entry : table_[i].entry) {
            if (entry.key != 0 && generationOf(entry) == generation_)
                ++used;
        }
    }
    return total ? static_cast<int>(used * 1000 / total) : 0;
}

}
