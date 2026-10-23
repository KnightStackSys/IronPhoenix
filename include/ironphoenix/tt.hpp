#pragma once

#include "move.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ironphoenix {

enum class TTBound : std::uint8_t {
    None  = 0,
    Upper = 1,
    Lower = 2,
    Exact = 3
};

struct TTProbe {
    // Probe fills every field on a hit. On a miss callers use the returned bool
    // and must not inspect this object, so leave plain local instances untouched.
    bool hit;
    Move move;
    int score;
    int depth;
    TTBound bound;
    bool pvNode;
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    bool resizeMB(std::size_t megabytes);
    void clear() noexcept;
    void newGeneration() noexcept;

    // Enable striped cluster locks before launching any SMP worker.
    // Never change this setting while a search is active.
    void setConcurrent(bool enabled) noexcept { concurrent_ = enabled; }

    [[nodiscard]] bool probe(Key key, int ply, TTProbe& out) const noexcept;
    void store(Key key,
               int depth,
               int score,
               TTBound bound,
               Move bestMove,
               bool pvNode,
               int ply) noexcept;

    [[nodiscard]] int hashfull() const noexcept;
    [[nodiscard]] std::size_t sizeMB() const noexcept { return sizeMB_; }

private:
    struct Entry {
        Key key = 0;
        std::uint32_t move = 0;
        std::int16_t score = 0;
        std::int8_t depth = -1;
        std::uint8_t flags = 0;
    };

    struct alignas(64) Cluster {
        Entry entry[4]{};
    };

    static_assert(sizeof(Entry) == 16);
    static_assert(sizeof(Cluster) == 64);

    std::vector<Cluster> table_;
    std::size_t mask_ = 0;
    std::size_t sizeMB_ = 0;
    std::uint8_t generation_ = 0;

    // A 64-byte cluster is a coherent TT update unit. Striped locks prevent
    // torn concurrent reads/writes without slowing down Threads=1 searches.
    static constexpr std::size_t LOCK_STRIPES = 4096;
    mutable std::array<std::mutex, LOCK_STRIPES> locks_{};
    bool concurrent_ = false;

    [[nodiscard]] static int scoreToTT(int score, int ply) noexcept;
    [[nodiscard]] static int scoreFromTT(int score, int ply) noexcept;
    [[nodiscard]] static TTBound boundOf(const Entry& e) noexcept;
    [[nodiscard]] static bool pvOf(const Entry& e) noexcept;
    [[nodiscard]] static std::uint8_t generationOf(const Entry& e) noexcept;
};

}
