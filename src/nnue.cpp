#include "ironphoenix/nnue.hpp"

#include "ironphoenix/geometry.hpp"
#include "ironphoenix/position.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ironphoenix::NNUE {
namespace {

constexpr std::array<char, 8> MAGIC = {'I', 'P', 'N', 'N', 'U', 'E', '1', '\0'};
constexpr std::uint32_t VERSION = 1;
constexpr int MAX_STATIC_EVAL = 28000;

struct Network final {
    float outputScale = 400.0f;

    // Shared sparse feature transformer. The same table is used for the
    // current player's king and the partner king accumulator streams.
    std::vector<float> ftWeights;
    std::array<float, FT_SIZE> ftBias{};

    // [own 128 | partner 128] -> 32 -> 32 -> 1
    std::array<float, HIDDEN1_SIZE * (FT_SIZE * 2)> hidden1Weights{};
    std::array<float, HIDDEN1_SIZE> hidden1Bias{};
    std::array<float, HIDDEN2_SIZE * HIDDEN1_SIZE> hidden2Weights{};
    std::array<float, HIDDEN2_SIZE> hidden2Bias{};
    std::array<float, HIDDEN2_SIZE> outputWeights{};
    float outputBias = 0.0f;
};

Network gNetwork;
bool gLoaded = false;
std::string gLoadedPath;

IRONPHOENIX_FORCE_INLINE float crelu(float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

template <typename T>
bool readExact(std::ifstream& in, T& value) noexcept {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T, std::size_t N>
bool readArray(std::ifstream& in, std::array<T, N>& values) noexcept {
    return static_cast<bool>(in.read(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(sizeof(T) * N)));
}

bool readVector(std::ifstream& in, std::vector<float>& values) noexcept {
    return static_cast<bool>(in.read(
        reinterpret_cast<char*>(values.data()),
        static_cast<std::streamsize>(sizeof(float) * values.size())));
}

void refreshAccumulator(
    const Position& pos,
    Color perspective,
    Square anchorKing,
    std::array<float, FT_SIZE>& accumulator) noexcept {

    accumulator = gNetwork.ftBias;

    for (unsigned ci = 0; ci < COLOR_NB; ++ci) {
        const Color pieceColor = static_cast<Color>(ci);

        for (unsigned pti = PAWN; pti <= KING; ++pti) {
            const PieceType pt = static_cast<PieceType>(pti);
            Bitboard pieces = pos.pieces(pieceColor, pt);

            while (pieces) {
                const Square sq = popLsb(pieces);
                const Piece piece = makePiece(pieceColor, pt);
                const std::size_t feature = featureIndex(perspective, anchorKing, piece, sq);
                const std::size_t base = feature * FT_SIZE;

                for (std::size_t i = 0; i < FT_SIZE; ++i)
                    accumulator[i] += gNetwork.ftWeights[base + i];
            }
        }
    }

    for (float& value : accumulator)
        value = crelu(value);
}

} // namespace

Square canonicalSquare(Color perspective, Square sq) noexcept {
    if (sq == SQ_NONE || sq >= SQUARE_NB)
        return SQ_NONE;

    const int file = static_cast<int>(Geometry::fileOf(sq));
    const int rank = static_cast<int>(Geometry::rankOf(sq));

    int canonicalFile = file;
    int canonicalRank = rank;

    switch (perspective) {
    case RED:
        break;
    case BLUE:
        canonicalFile = 13 - rank;
        canonicalRank = file;
        break;
    case YELLOW:
        canonicalFile = 13 - file;
        canonicalRank = 13 - rank;
        break;
    case GREEN:
        canonicalFile = rank;
        canonicalRank = 13 - file;
        break;
    }

    return Geometry::square(canonicalFile, canonicalRank);
}

unsigned relativeColor(Color perspective, Color pieceColor) noexcept {
    return (static_cast<unsigned>(pieceColor) + COLOR_NB
        - static_cast<unsigned>(perspective)) & 3u;
}

unsigned kingBucket(Color perspective, Square kingSq) noexcept {
    if (kingSq == SQ_NONE || kingSq >= SQUARE_NB)
        return 16u;

    const Square canonical = canonicalSquare(perspective, kingSq);
    if (canonical == SQ_NONE)
        return 16u;

    const unsigned file = Geometry::fileOf(canonical);
    const unsigned rank = Geometry::rankOf(canonical);

    // Four coarse regions on each axis. 14 does not divide evenly by four,
    // so multiply before dividing to keep the buckets balanced.
    const unsigned fileBucket = std::min(3u, (file * 4u) / BOARD_FILES);
    const unsigned rankBucket = std::min(3u, (rank * 4u) / BOARD_RANKS);
    return rankBucket * 4u + fileBucket;
}

std::size_t featureIndex(
    Color perspective,
    Square anchorKing,
    Piece piece,
    Square pieceSq) noexcept {

    if (piece == NO_PIECE || pieceSq == SQ_NONE || pieceSq >= SQUARE_NB)
        return 0;

    const PieceType pt = typeOf(piece);
    if (pt < PAWN || pt > KING)
        return 0;

    const Square canonical = canonicalSquare(perspective, pieceSq);
    if (canonical == SQ_NONE)
        return 0;

    const std::size_t bucket = kingBucket(perspective, anchorKing);
    const std::size_t relColor = relativeColor(perspective, colorOf(piece));
    const std::size_t pieceType = static_cast<std::size_t>(pt) - 1u;

    return (((bucket * RELATIVE_COLORS + relColor) * NNUE_PIECE_TYPES + pieceType)
        * SQUARE_NB) + canonical;
}

bool loadNetwork(const std::string& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;

        std::array<char, 8> magic{};
        if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size())) || magic != MAGIC)
            return false;

        std::uint32_t version = 0;
        std::uint32_t featureCount = 0;
        std::uint32_t ftSize = 0;
        std::uint32_t hidden1Size = 0;
        std::uint32_t hidden2Size = 0;
        float outputScale = 0.0f;

        if (!readExact(in, version)
            || !readExact(in, featureCount)
            || !readExact(in, ftSize)
            || !readExact(in, hidden1Size)
            || !readExact(in, hidden2Size)
            || !readExact(in, outputScale))
            return false;

        if (version != VERSION
            || featureCount != FEATURE_COUNT
            || ftSize != FT_SIZE
            || hidden1Size != HIDDEN1_SIZE
            || hidden2Size != HIDDEN2_SIZE
            || !std::isfinite(outputScale)
            || outputScale <= 0.0f)
            return false;

        Network candidate;
        candidate.outputScale = outputScale;
        candidate.ftWeights.resize(FEATURE_COUNT * FT_SIZE);

        if (!readVector(in, candidate.ftWeights)
            || !readArray(in, candidate.ftBias)
            || !readArray(in, candidate.hidden1Weights)
            || !readArray(in, candidate.hidden1Bias)
            || !readArray(in, candidate.hidden2Weights)
            || !readArray(in, candidate.hidden2Bias)
            || !readArray(in, candidate.outputWeights)
            || !readExact(in, candidate.outputBias))
            return false;

        // Reject malformed files with trailing data. This catches most
        // architecture/export mismatches instead of silently evaluating junk.
        char trailing = 0;
        if (in.read(&trailing, 1))
            return false;

        gNetwork = std::move(candidate);
        gLoadedPath = path;
        gLoaded = true;
        return true;
    }
    catch (...) {
        return false;
    }
}

void unloadNetwork() noexcept {
    gNetwork = Network{};
    gLoadedPath.clear();
    gLoaded = false;
}

bool loaded() noexcept {
    return gLoaded;
}

const std::string& loadedPath() noexcept {
    return gLoadedPath;
}

int evaluate(const Position& pos) noexcept {
    if (!gLoaded)
        return 0;

    const Color perspective = pos.sideToMove();
    const Color partner = static_cast<Color>(static_cast<unsigned>(perspective) ^ 2u);

    std::array<float, FT_SIZE> own{};
    std::array<float, FT_SIZE> partnerAcc{};
    refreshAccumulator(pos, perspective, pos.kingSquare(perspective), own);
    refreshAccumulator(pos, perspective, pos.kingSquare(partner), partnerAcc);

    std::array<float, FT_SIZE * 2> input{};
    std::copy(own.begin(), own.end(), input.begin());
    std::copy(partnerAcc.begin(), partnerAcc.end(), input.begin() + FT_SIZE);

    std::array<float, HIDDEN1_SIZE> hidden1{};
    for (std::size_t out = 0; out < HIDDEN1_SIZE; ++out) {
        float sum = gNetwork.hidden1Bias[out];
        const std::size_t row = out * input.size();
        for (std::size_t in = 0; in < input.size(); ++in)
            sum += gNetwork.hidden1Weights[row + in] * input[in];
        hidden1[out] = crelu(sum);
    }

    std::array<float, HIDDEN2_SIZE> hidden2{};
    for (std::size_t out = 0; out < HIDDEN2_SIZE; ++out) {
        float sum = gNetwork.hidden2Bias[out];
        const std::size_t row = out * HIDDEN1_SIZE;
        for (std::size_t in = 0; in < HIDDEN1_SIZE; ++in)
            sum += gNetwork.hidden2Weights[row + in] * hidden1[in];
        hidden2[out] = crelu(sum);
    }

    float output = gNetwork.outputBias;
    for (std::size_t i = 0; i < HIDDEN2_SIZE; ++i)
        output += gNetwork.outputWeights[i] * hidden2[i];

    if (!std::isfinite(output))
        return 0;

    const float cp = output * gNetwork.outputScale;
    return std::clamp(static_cast<int>(std::lround(cp)), -MAX_STATIC_EVAL, MAX_STATIC_EVAL);
}

} // namespace ironphoenix::NNUE
