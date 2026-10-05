#include "ironphoenix/zobrist.hpp"

#include <mutex>

namespace ironphoenix::Zobrist {

std::array<std::array<Key, SQUARE_NB>, 32> PieceSquare{};
std::array<Key, COLOR_NB> Side{};
std::array<std::array<Key, SQUARE_NB>, COLOR_NB> EnPassant{};
std::array<Key, 8> CastleRight{};
std::array<Key, COLOR_NB> Alive{};
std::array<Key, 16> Ruleset{};

namespace {
std::once_flag initFlag;

IRONPHOENIX_FORCE_INLINE std::uint64_t splitmix64(std::uint64_t& x) noexcept {
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void initialize() {
    std::uint64_t seed = 0x4E455855535F3450ULL;

    for (auto& piece : PieceSquare)
        for (auto& key : piece)
            key = splitmix64(seed);

    for (auto& key : Side)
        key = splitmix64(seed);

    for (auto& color : EnPassant)
        for (auto& key : color)
            key = splitmix64(seed);

    for (auto& key : CastleRight)
        key = splitmix64(seed);

    for (auto& key : Alive)
        key = splitmix64(seed);

    for (auto& key : Ruleset)
        key = splitmix64(seed);
}
}

void init() {
    std::call_once(initFlag, initialize);
}

}
