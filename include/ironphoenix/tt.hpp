#pragma once

#include "move.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ironphoenix {

enum class TTBound : std::uint8_t {
    None  = 0,
    Upper = 1,
    Lower = 2,
    Exact = 3
};

struct TTProbe {
    bool hit = false;
    Move move{};
    int score = 0;
    int depth = -1;
    TTBound bound = TTBound::None;
    bool pvNode = false;
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    bool resizeMB(std::size_t megabytes);
    void clear() noexcept;
    void newGeneration() noexcept;

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

    [[nodiscard]] static int scoreToTT(int score, int ply) noexcept;
    [[nodiscard]] static int scoreFromTT(int score, int ply) noexcept;
    [[nodiscard]] static TTBound boundOf(const Entry& e) noexcept;
    [[nodiscard]] static bool pvOf(const Entry& e) noexcept;
    [[nodiscard]] static std::uint8_t generationOf(const Entry& e) noexcept;
};

}
