// Append-capable entry point for the PhoenixNet dataset generator.
//
// The original generator remains in nnue_dataset.cpp. We include it here with
// its main() renamed, then wrap it with safe --append behavior. This keeps the
// generation logic in one place while adding resumable dataset growth.

#define main ironphoenix_dataset_generate_main
#include "nnue_dataset.cpp"
#undef main

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::array<char, 8> APPEND_MAGIC = {'I', 'P', 'D', 'A', 'T', 'A', '1', '\0'};
constexpr std::streamoff RECORD_COUNT_OFFSET = 36;

struct DatasetHeader final {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t featureCount = 0;
    std::uint32_t maxFeatures = 0;
    std::uint32_t depth = 0;
    float scoreScale = 0.0f;
    std::uint64_t seed = 0;
    std::uint64_t recordCount = 0;
    std::streamoff dataOffset = 0;
};

struct DatasetScan final {
    DatasetHeader header{};
    std::uint32_t maxGameId = 0;
    bool hasGameId = false;
    std::streamoff dataEnd = 0;
    bool hasTrailingBytes = false;
};

template <typename T>
bool readValue(std::istream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
bool writeValue(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

bool readHeader(std::istream& in, DatasetHeader& header, std::string& error) {
    if (!in.read(header.magic.data(), static_cast<std::streamsize>(header.magic.size()))) {
        error = "dataset header is truncated";
        return false;
    }

    if (header.magic != APPEND_MAGIC) {
        error = "not an IronPhoenix IPDATA1 dataset";
        return false;
    }

    if (!readValue(in, header.version)
        || !readValue(in, header.featureCount)
        || !readValue(in, header.maxFeatures)
        || !readValue(in, header.depth)
        || !readValue(in, header.scoreScale)
        || !readValue(in, header.seed)
        || !readValue(in, header.recordCount)) {
        error = "dataset header is truncated";
        return false;
    }

    if (header.version != 1) {
        error = "unsupported dataset version " + std::to_string(header.version);
        return false;
    }

    if (header.featureCount != ironphoenix::NNUE::FEATURE_COUNT) {
        error = "dataset feature count does not match this PhoenixNet build";
        return false;
    }

    if (header.maxFeatures == 0 || header.maxFeatures > 4096) {
        error = "dataset max-feature count is invalid";
        return false;
    }

    if (!std::isfinite(header.scoreScale) || header.scoreScale <= 0.0f) {
        error = "dataset score scale is invalid";
        return false;
    }

    header.dataOffset = static_cast<std::streamoff>(in.tellg());
    return true;
}

bool scanDataset(const std::filesystem::path& path, DatasetScan& scan, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "unable to open dataset";
        return false;
    }

    if (!readHeader(in, scan.header, error))
        return false;

    for (std::uint64_t i = 0; i < scan.header.recordCount; ++i) {
        std::uint16_t ownCount = 0;
        std::uint16_t partnerCount = 0;
        float target = 0.0f;
        std::int32_t teacherCp = 0;
        std::uint32_t gameId = 0;
        std::uint16_t ply = 0;
        std::uint8_t side = 0;
        std::uint8_t flags = 0;

        if (!readValue(in, ownCount)
            || !readValue(in, partnerCount)
            || !readValue(in, target)
            || !readValue(in, teacherCp)
            || !readValue(in, gameId)
            || !readValue(in, ply)
            || !readValue(in, side)
            || !readValue(in, flags)) {
            error = "dataset is truncated while reading record " + std::to_string(i);
            return false;
        }

        if (ownCount > scan.header.maxFeatures || partnerCount > scan.header.maxFeatures) {
            error = "dataset record has an invalid sparse feature count";
            return false;
        }

        const std::uint64_t featureBytes =
            static_cast<std::uint64_t>(ownCount + partnerCount) * sizeof(std::uint16_t);
        in.seekg(static_cast<std::streamoff>(featureBytes), std::ios::cur);
        if (!in) {
            error = "dataset is truncated inside sparse features";
            return false;
        }

        if (!scan.hasGameId || gameId > scan.maxGameId) {
            scan.maxGameId = gameId;
            scan.hasGameId = true;
        }
    }

    scan.dataEnd = static_cast<std::streamoff>(in.tellg());
    char extra = 0;
    scan.hasTrailingBytes = static_cast<bool>(in.read(&extra, 1));
    return true;
}

bool headersCompatible(const DatasetHeader& existing, const DatasetHeader& incoming, std::string& error) {
    if (existing.version != incoming.version) {
        error = "dataset format versions differ";
        return false;
    }
    if (existing.featureCount != incoming.featureCount) {
        error = "PhoenixNet feature counts differ";
        return false;
    }
    if (existing.maxFeatures != incoming.maxFeatures) {
        error = "maximum sparse feature counts differ";
        return false;
    }
    if (existing.depth != incoming.depth) {
        error = "teacher depths differ (existing=" + std::to_string(existing.depth)
            + ", new=" + std::to_string(incoming.depth) + ")";
        return false;
    }
    if (std::fabs(existing.scoreScale - incoming.scoreScale) > 1e-6f) {
        error = "score scales differ";
        return false;
    }
    return true;
}

bool copyAppendedRecords(
    const std::filesystem::path& existingPath,
    const DatasetScan& existing,
    const std::filesystem::path& incomingPath,
    const DatasetScan& incoming,
    std::string& error) {

    // If a previous append was interrupted after data was written but before
    // the authoritative record count was updated, discard those stale bytes.
    if (existing.hasTrailingBytes) {
        std::error_code resizeError;
        std::filesystem::resize_file(
            existingPath,
            static_cast<std::uintmax_t>(existing.dataEnd),
            resizeError);
        if (resizeError) {
            error = "unable to remove stale trailing bytes: " + resizeError.message();
            return false;
        }
    }

    std::ifstream source(incomingPath, std::ios::binary);
    if (!source) {
        error = "unable to reopen generated append batch";
        return false;
    }
    source.seekg(incoming.header.dataOffset);

    std::fstream destination(existingPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!destination) {
        error = "unable to reopen destination dataset for append";
        return false;
    }
    destination.seekp(existing.dataEnd);

    const std::uint64_t gameOffset = existing.hasGameId
        ? static_cast<std::uint64_t>(existing.maxGameId) + 1ULL
        : 0ULL;

    for (std::uint64_t i = 0; i < incoming.header.recordCount; ++i) {
        std::uint16_t ownCount = 0;
        std::uint16_t partnerCount = 0;
        float target = 0.0f;
        std::int32_t teacherCp = 0;
        std::uint32_t gameId = 0;
        std::uint16_t ply = 0;
        std::uint8_t side = 0;
        std::uint8_t flags = 0;

        if (!readValue(source, ownCount)
            || !readValue(source, partnerCount)
            || !readValue(source, target)
            || !readValue(source, teacherCp)
            || !readValue(source, gameId)
            || !readValue(source, ply)
            || !readValue(source, side)
            || !readValue(source, flags)) {
            error = "append batch became truncated while copying";
            return false;
        }

        const std::uint64_t adjustedGameId = gameOffset + static_cast<std::uint64_t>(gameId);
        if (adjustedGameId > std::numeric_limits<std::uint32_t>::max()) {
            error = "game id overflow while appending dataset";
            return false;
        }
        const auto newGameId = static_cast<std::uint32_t>(adjustedGameId);

        if (!writeValue(destination, ownCount)
            || !writeValue(destination, partnerCount)
            || !writeValue(destination, target)
            || !writeValue(destination, teacherCp)
            || !writeValue(destination, newGameId)
            || !writeValue(destination, ply)
            || !writeValue(destination, side)
            || !writeValue(destination, flags)) {
            error = "failed while writing appended record metadata";
            return false;
        }

        const std::size_t featureCount = static_cast<std::size_t>(ownCount) + partnerCount;
        std::vector<std::uint16_t> features(featureCount);
        if (featureCount != 0) {
            source.read(
                reinterpret_cast<char*>(features.data()),
                static_cast<std::streamsize>(featureCount * sizeof(std::uint16_t)));
            if (!source) {
                error = "append batch became truncated inside sparse features";
                return false;
            }
            destination.write(
                reinterpret_cast<const char*>(features.data()),
                static_cast<std::streamsize>(featureCount * sizeof(std::uint16_t)));
            if (!destination) {
                error = "failed while writing appended sparse features";
                return false;
            }
        }
    }

    destination.flush();
    if (!destination) {
        error = "failed to flush appended dataset records";
        return false;
    }

    // Update the authoritative count last. If the process dies before here,
    // the next --append run safely trims the uncommitted trailing bytes.
    const std::uint64_t newCount = existing.header.recordCount + incoming.header.recordCount;
    if (newCount < existing.header.recordCount) {
        error = "dataset record count overflow";
        return false;
    }

    destination.seekp(RECORD_COUNT_OFFSET);
    if (!writeValue(destination, newCount)) {
        error = "failed to update dataset record count";
        return false;
    }
    destination.flush();
    if (!destination) {
        error = "failed to commit appended dataset record count";
        return false;
    }

    return true;
}

std::vector<std::string> stripAppendOnly(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    args.emplace_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--append")
            continue;
        args.emplace_back(argv[i]);
    }
    return args;
}

std::vector<char*> makeArgv(std::vector<std::string>& args) {
    std::vector<char*> result;
    result.reserve(args.size());
    for (std::string& arg : args)
        result.push_back(arg.data());
    return result;
}

int runOriginal(std::vector<std::string> args) {
    auto argv = makeArgv(args);
    return ironphoenix_dataset_generate_main(static_cast<int>(argv.size()), argv.data());
}

bool findOutputPath(int argc, char** argv, std::string& output) {
    output = "phoenix_dataset.ipd";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--output") {
            if (i + 1 >= argc)
                return false;
            output = argv[++i];
        }
    }
    return true;
}

bool hasOption(int argc, char** argv, std::string_view wanted) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == wanted)
            return true;
    return false;
}

std::vector<std::string> makeAppendGenerationArgs(
    int argc,
    char** argv,
    const std::filesystem::path& temporaryOutput,
    std::uint64_t derivedSeed) {

    const bool seedProvided = hasOption(argc, argv, "--seed");
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc) + 4);
    args.emplace_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--append")
            continue;
        if (arg == "--output") {
            if (i + 1 < argc)
                ++i;
            continue;
        }
        args.emplace_back(argv[i]);
    }

    args.emplace_back("--output");
    args.emplace_back(temporaryOutput.string());

    if (!seedProvided) {
        args.emplace_back("--seed");
        args.emplace_back(std::to_string(derivedSeed));
    }

    return args;
}

} // namespace

int main(int argc, char** argv) {
    const bool append = hasOption(argc, argv, "--append");
    if (!append)
        return runOriginal(stripAppendOnly(argc, argv));

    std::string outputText;
    if (!findOutputPath(argc, argv, outputText)) {
        std::cerr << "--output requires a file path\n";
        return 1;
    }

    const std::filesystem::path outputPath(outputText);
    if (!std::filesystem::exists(outputPath)) {
        std::cout << "append requested, but dataset does not exist; creating a new dataset\n";
        return runOriginal(stripAppendOnly(argc, argv));
    }

    DatasetScan existing;
    std::string error;
    if (!scanDataset(outputPath, existing, error)) {
        std::cerr << "cannot append: " << error << '\n';
        return 1;
    }

    const std::uint64_t derivedSeed = existing.header.seed
        ^ (0x9E3779B97F4A7C15ULL * (existing.header.recordCount + 1ULL))
        ^ 0xA5A5A5A55A5A5A5AULL;

    std::filesystem::path temporaryPath = outputPath;
    temporaryPath += ".append.tmp";
    std::error_code removeError;
    std::filesystem::remove(temporaryPath, removeError);

    std::cout << "append mode: existing dataset has " << existing.header.recordCount
        << " positions\n";
    if (existing.hasTrailingBytes)
        std::cout << "warning: stale uncommitted trailing bytes detected; they will be removed safely\n";

    auto generationArgs = makeAppendGenerationArgs(argc, argv, temporaryPath, derivedSeed);
    const int generationResult = runOriginal(std::move(generationArgs));
    if (generationResult != 0) {
        std::filesystem::remove(temporaryPath, removeError);
        return generationResult;
    }

    DatasetScan incoming;
    if (!scanDataset(temporaryPath, incoming, error)) {
        std::cerr << "cannot append generated batch: " << error << '\n';
        std::filesystem::remove(temporaryPath, removeError);
        return 1;
    }

    if (!headersCompatible(existing.header, incoming.header, error)) {
        std::cerr << "cannot append incompatible dataset: " << error << '\n';
        std::filesystem::remove(temporaryPath, removeError);
        return 1;
    }

    if (incoming.hasTrailingBytes) {
        std::cerr << "cannot append: generated batch contains unexpected trailing bytes\n";
        std::filesystem::remove(temporaryPath, removeError);
        return 1;
    }

    if (!copyAppendedRecords(outputPath, existing, temporaryPath, incoming, error)) {
        std::cerr << "append failed: " << error << '\n';
        std::filesystem::remove(temporaryPath, removeError);
        return 1;
    }

    std::filesystem::remove(temporaryPath, removeError);

    const std::uint64_t total = existing.header.recordCount + incoming.header.recordCount;
    std::cout << "append complete: +" << incoming.header.recordCount
        << " positions, " << total << " total in " << outputPath.string() << '\n';
    return 0;
}
