// Graceful/resumable PhoenixNet dataset generator entry point.
//
// Reuse the established generator implementation while replacing only the
// orchestration/search loop pieces needed for Ctrl+C, --resume, recovery, and
// denser progress reporting. The legacy entry point is compiled into this
// translation unit under a different name but is never invoked.

#define main ironphoenix_dataset_legacy_main
#define workerMain legacyWorkerMain
#define searchPosition legacySearchPosition
#include "nnue_dataset_with_append.cpp"
#undef searchPosition
#undef workerMain
#undef main

#include <csignal>

namespace ironphoenix {
namespace {

volatile std::sig_atomic_t gStopRequested = 0;
std::atomic<std::uint64_t> gLastReported{0};
std::mutex gProgressMutex;

void handleInterrupt(int) noexcept {
    gStopRequested = 1;
}

[[nodiscard]] bool stopRequested() noexcept {
    return gStopRequested != 0;
}

void reportProgress(
    const DatasetWriter& writer,
    const GenerationStats& stats,
    unsigned workerId) {

    const std::uint64_t count = writer.addedCount();
    if (count == 0)
        return;

    const std::uint64_t milestone = (count / 10ULL) * 10ULL;
    const bool finalCount = count >= writer.targetAdded();
    const std::uint64_t wanted = finalCount ? writer.targetAdded() : milestone;
    if (wanted == 0)
        return;

    std::uint64_t previous = gLastReported.load(std::memory_order_relaxed);
    while (wanted > previous) {
        if (gLastReported.compare_exchange_weak(
                previous,
                wanted,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {

            const std::uint64_t currentAdded = writer.addedCount();
            const std::uint64_t baseCount = writer.totalCount() - currentAdded;

            std::lock_guard lock(gProgressMutex);
            std::cout << "new positions " << wanted << '/' << writer.targetAdded()
                << " total " << (baseCount + wanted)
                << " duplicates " << stats.duplicateSamples.load(std::memory_order_relaxed)
                << " explored " << stats.exploredMoves.load(std::memory_order_relaxed)
                << " worker " << workerId << '\n';
            std::cout.flush();
            return;
        }
    }
}

// Recover complete records written after the authoritative header count. This
// makes datasets from an older abruptly-stopped generator salvageable. Only an
// incomplete final record is discarded.
bool recoverTrailingRecords(
    const std::filesystem::path& path,
    DatasetScan& scan,
    DatasetDeduper* deduper,
    std::uint64_t& recovered,
    std::string& error) {

    recovered = 0;
    if (!scan.hasTrailingBytes)
        return true;

    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        error = "unable to inspect interrupted dataset size: " + sizeError.message();
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "unable to reopen interrupted dataset";
        return false;
    }

    std::uintmax_t cursor = static_cast<std::uintmax_t>(scan.dataEnd);
    in.seekg(scan.dataEnd);
    if (!in) {
        error = "unable to seek to interrupted dataset tail";
        return false;
    }

    std::vector<std::uint16_t> own;
    std::vector<std::uint16_t> partner;
    std::uintmax_t validEnd = cursor;

    constexpr std::uintmax_t RECORD_METADATA_BYTES = 20;

    while (cursor < fileSize) {
        const std::uintmax_t remaining = fileSize - cursor;
        if (remaining < RECORD_METADATA_BYTES)
            break;

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
            break;
        }

        if (ownCount > scan.header.maxFeatures
            || partnerCount > scan.header.maxFeatures
            || side >= COLOR_NB
            || !std::isfinite(target)) {
            break;
        }

        const std::uintmax_t featureBytes =
            static_cast<std::uintmax_t>(ownCount + partnerCount) * sizeof(std::uint16_t);
        if (remaining < RECORD_METADATA_BYTES + featureBytes)
            break;

        if (!readFeatureVector(in, ownCount, own)
            || !readFeatureVector(in, partnerCount, partner)) {
            break;
        }

        const bool validFeatures = std::all_of(
                own.begin(), own.end(),
                [&](std::uint16_t feature) { return feature < scan.header.featureCount; })
            && std::all_of(
                partner.begin(), partner.end(),
                [&](std::uint16_t feature) { return feature < scan.header.featureCount; });
        if (!validFeatures)
            break;

        if (deduper != nullptr)
            deduper->preload(featureFingerprint(own, partner, side));

        if (!scan.hasGameId || gameId > scan.maxGameId) {
            scan.maxGameId = gameId;
            scan.hasGameId = true;
        }

        ++recovered;
        cursor += RECORD_METADATA_BYTES + featureBytes;
        validEnd = cursor;
    }

    in.close();

    if (validEnd < fileSize) {
        std::error_code resizeError;
        std::filesystem::resize_file(path, validEnd, resizeError);
        if (resizeError) {
            error = "unable to trim incomplete interrupted record: " + resizeError.message();
            return false;
        }
    }

    if (recovered != 0) {
        if (scan.header.recordCount > std::numeric_limits<std::uint64_t>::max() - recovered) {
            error = "dataset record count overflow during recovery";
            return false;
        }

        scan.header.recordCount += recovered;

        std::fstream header(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!header) {
            error = "unable to reopen dataset to commit recovered count";
            return false;
        }
        header.seekp(RECORD_COUNT_OFFSET);
        if (!writeValue(header, scan.header.recordCount)) {
            error = "unable to write recovered dataset count";
            return false;
        }
        header.flush();
        if (!header) {
            error = "unable to flush recovered dataset count";
            return false;
        }
    }

    scan.dataEnd = static_cast<std::streamoff>(validEnd);
    scan.hasTrailingBytes = false;
    return true;
}

TeacherResult searchPosition(
    SearchEngine& search,
    Position& pos,
    const Options& options,
    std::mt19937_64& rng) {

    TeacherResult result;
    if (stopRequested())
        return result;

    SearchLimits limits;
    limits.depth = options.depth;

    const int requestedMultiPV = options.exploration > 0.0
        ? std::clamp(options.explorationMultiPV, 2, SearchEngine::MAX_MULTI_PV)
        : 1;

    std::ostringstream output;
    if (requestedMultiPV > 1)
        search.startMultiPV(pos, limits, requestedMultiPV, output);
    else
        search.start(pos, limits, output);

    while (search.searching()) {
        if (stopRequested())
            search.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    search.stopAndWait();

    if (stopRequested())
        return result;

    int bestDepth = -1;
    std::vector<RootCandidate> finalLines;

    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line)) {
        int depth = -1;
        RootCandidate candidate;
        if (!parseInfoLine(pos, line, depth, candidate))
            continue;

        if (depth > bestDepth) {
            bestDepth = depth;
            finalLines.clear();
        }
        if (depth == bestDepth) {
            auto it = std::find_if(
                finalLines.begin(),
                finalLines.end(),
                [&](const RootCandidate& item) { return item.rank == candidate.rank; });
            if (it == finalLines.end())
                finalLines.push_back(candidate);
            else
                *it = candidate;
        }
    }

    std::sort(
        finalLines.begin(),
        finalLines.end(),
        [](const RootCandidate& a, const RootCandidate& b) {
            return a.rank < b.rank;
        });

    if (finalLines.empty())
        return result;

    const RootCandidate& best = finalLines.front();
    result.ok = true;
    result.bestMate = best.mate;
    result.bestCp = best.cp;
    result.move = best.move;
    result.candidates = finalLines;

    if (best.mate || options.exploration <= 0.0 || finalLines.size() < 2)
        return result;

    std::uniform_real_distribution<double> probability(0.0, 1.0);
    if (probability(rng) >= options.exploration)
        return result;

    std::vector<const RootCandidate*> alternatives;
    std::vector<double> weights;

    for (std::size_t i = 1; i < finalLines.size(); ++i) {
        const RootCandidate& candidate = finalLines[i];
        if (candidate.mate)
            continue;

        const int loss = best.cp - candidate.cp;
        if (loss < 0 || loss > options.explorationMaxLoss)
            continue;

        alternatives.push_back(&candidate);
        weights.push_back(std::exp(
            -static_cast<double>(loss) / options.explorationTemperature));
    }

    if (alternatives.empty())
        return result;

    std::discrete_distribution<std::size_t> choose(weights.begin(), weights.end());
    result.move = alternatives[choose(rng)]->move;
    result.explored = true;
    return result;
}

void workerMain(
    unsigned workerId,
    const Options& options,
    DatasetWriter& writer,
    DatasetDeduper& deduper,
    GenerationStats& stats,
    std::atomic<std::uint32_t>& nextGameId,
    std::atomic<bool>& failed) {

    SearchEngine search;
    if (!search.setHashSizeMB(options.hashMB)) {
        std::cerr << "worker " << workerId << ": unable to allocate hash\n";
        failed.store(true, std::memory_order_relaxed);
        return;
    }

    std::mt19937_64 rng(
        options.seed
        + 0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(workerId) + 1ULL));

    Position pos;
    Fen4State fenState;
    std::string error;
    const int firstSamplePly = std::max({1, options.randomPlies, options.skipPlies});

    while (!stopRequested()
        && !failed.load(std::memory_order_relaxed)
        && writer.addedCount() < writer.targetAdded()) {

        const std::uint32_t gameId = nextGameId.fetch_add(1, std::memory_order_relaxed);

        if (!resetModern(pos, fenState, error)) {
            std::cerr << "worker " << workerId << ": start position failed: " << error << '\n';
            failed.store(true, std::memory_order_relaxed);
            return;
        }
        search.newGame();

        for (int ply = 0; ply < options.maxPlies; ++ply) {
            if (stopRequested()
                || failed.load(std::memory_order_relaxed)
                || writer.addedCount() >= writer.targetAdded())
                break;

            MoveList legal;
            generateLegalMoves(pos, legal);
            if (legal.size == 0)
                break;

            Move move;
            TeacherResult teacher;

            if (ply < options.randomPlies) {
                move = chooseRandomOpeningMove(pos, rng);
            }
            else {
                teacher = searchPosition(search, pos, options, rng);
                if (stopRequested())
                    break;
                if (!teacher.ok)
                    break;

                move = teacher.move;
                if (teacher.explored)
                    stats.exploredMoves.fetch_add(1, std::memory_order_relaxed);

                const bool samplePly = ply >= firstSamplePly
                    && ((ply - firstSamplePly) % options.sampleEvery == 0);

                if (samplePly && !teacher.bestMate) {
                    const int clippedCp = std::clamp(
                        teacher.bestCp,
                        -options.cpClamp,
                        options.cpClamp);

                    Record record;
                    record.target = static_cast<float>(clippedCp) / options.scoreScale;
                    record.teacherCp = teacher.bestCp;
                    record.gameId = gameId;
                    record.ply = static_cast<std::uint16_t>(std::min(ply, 65535));
                    record.sideToMove = static_cast<std::uint8_t>(pos.sideToMove());

                    if (pos.inCheck())
                        record.flags |= 1u;
                    if (pos.aliveMask() != 0xF)
                        record.flags |= 2u;
                    if (teacher.explored)
                        record.flags |= 4u;

                    const Color perspective = pos.sideToMove();
                    const Color partner = static_cast<Color>(
                        static_cast<unsigned>(perspective) ^ 2u);

                    collectFeatures(
                        pos,
                        perspective,
                        pos.kingSquare(perspective),
                        record.own);
                    collectFeatures(
                        pos,
                        perspective,
                        pos.kingSquare(partner),
                        record.partner);

                    if (record.own.size() != record.partner.size()) {
                        std::cerr << "worker " << workerId
                            << ": feature stream size mismatch\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }

                    bool unique = true;
                    if (options.dedup) {
                        unique = deduper.claim(featureFingerprint(
                            record.own,
                            record.partner,
                            record.sideToMove));
                    }

                    if (!unique) {
                        stats.duplicateSamples.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (!writer.tryWrite(record)) {
                        std::cerr << "worker " << workerId << ": dataset write failed\n";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    else {
                        reportProgress(writer, stats, workerId);
                    }
                }
            }

            if (stopRequested() || !move)
                break;

            const bool terminalKingCapture = isTerminalKingCapture(pos, move);
            StateInfo state;
            pos.makeMove(move, state);

            if (terminalKingCapture)
                break;
        }
    }

    search.stopAndWait();
}

bool hasCommandLineOption(int argc, char** argv, std::string_view wanted) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == wanted)
            return true;
    return false;
}

std::vector<std::string> filteredArgumentsWithoutResume(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        if (i != 0 && std::string_view(argv[i]) == "--resume")
            continue;
        args.emplace_back(argv[i]);
    }
    return args;
}

std::vector<char*> writableArgv(std::vector<std::string>& args) {
    std::vector<char*> result;
    result.reserve(args.size());
    for (std::string& arg : args)
        result.push_back(arg.data());
    return result;
}

} // namespace
} // namespace ironphoenix

int main(int argc, char** argv) {
    using namespace ironphoenix;

    const bool resume = hasCommandLineOption(argc, argv, "--resume");
    const bool appendOnCommandLine = hasCommandLineOption(argc, argv, "--append");
    if (resume && appendOnCommandLine) {
        std::cerr << "--resume and --append are different modes; choose only one\n";
        return 1;
    }

    auto filteredArgs = filteredArgumentsWithoutResume(argc, argv);
    auto filteredArgv = writableArgv(filteredArgs);

    Options options;
    bool showedHelp = false;
    if (!parseOptions(
            static_cast<int>(filteredArgv.size()),
            filteredArgv.data(),
            options,
            showedHelp)) {
        if (!showedHelp)
            printUsage();
        return showedHelp ? 0 : 1;
    }

    if (resume && showedHelp)
        return 0;

    const std::uint64_t requestedPositionArgument = options.positions;
    const std::filesystem::path outputPath(options.output);
    const bool outputExists = std::filesystem::exists(outputPath);

    DatasetDeduper deduper;
    DatasetScan existing;
    DatasetScan* existingPtr = nullptr;
    std::string error;

    const bool continueExisting = (options.append || resume) && outputExists;
    if (continueExisting) {
        if (!scanDataset(outputPath, existing, options.dedup ? &deduper : nullptr, error)) {
            std::cerr << "cannot continue dataset: " << error << '\n';
            return 1;
        }

        if (existing.hasTrailingBytes) {
            std::uint64_t recovered = 0;
            if (!recoverTrailingRecords(
                    outputPath,
                    existing,
                    options.dedup ? &deduper : nullptr,
                    recovered,
                    error)) {
                std::cerr << "cannot recover interrupted dataset: " << error << '\n';
                return 1;
            }
            if (recovered != 0)
                std::cout << "recovered " << recovered
                    << " complete positions from an interrupted run\n";
            else
                std::cout << "trimmed incomplete interrupted dataset tail\n";
        }

        if (existing.header.depth != static_cast<std::uint32_t>(options.depth)) {
            std::cerr << "cannot continue: teacher depth differs (existing "
                << existing.header.depth << ", requested " << options.depth << ")\n";
            return 1;
        }
        if (std::fabs(existing.header.scoreScale - options.scoreScale) > 1e-6f) {
            std::cerr << "cannot continue: score scale differs\n";
            return 1;
        }

        if (!options.seedExplicit) {
            options.seed = existing.header.seed
                ^ mix64(existing.header.recordCount + 1ULL)
                ^ 0xA5A5A5A55A5A5A5AULL;
        }

        existingPtr = &existing;

        if (resume) {
            if (existing.header.recordCount >= requestedPositionArgument) {
                std::cout << "resume target already reached: dataset has "
                    << existing.header.recordCount << " positions (target "
                    << requestedPositionArgument << ")\n";
                return 0;
            }

            options.positions = requestedPositionArgument - existing.header.recordCount;
            std::cout << "resume mode: existing " << existing.header.recordCount
                << ", target total " << requestedPositionArgument
                << ", remaining " << options.positions << '\n';
        }
        else {
            std::cout << "append mode: existing positions "
                << existing.header.recordCount
                << ", adding " << options.positions << '\n';
        }

        if (options.dedup)
            std::cout << "preloaded dedup fingerprints " << deduper.size() << '\n';
    }
    else if (resume) {
        std::cout << "resume requested but dataset does not exist; creating it toward total target "
            << requestedPositionArgument << '\n';
    }
    else if (options.append) {
        std::cout << "append requested but dataset does not exist; creating it\n";
    }

    DatasetWriter writer(options, existingPtr, error);
    if (!writer.good()) {
        std::cerr << error << '\n';
        return 1;
    }

    gStopRequested = 0;
    gLastReported.store(0, std::memory_order_relaxed);
    std::signal(SIGINT, handleInterrupt);
#ifdef SIGTERM
    std::signal(SIGTERM, handleInterrupt);
#endif

    std::cout
        << "PhoenixNet dataset generation\n"
        << "output          " << options.output << '\n'
        << "mode            " << (resume ? "resume" : (options.append ? "append" : "new")) << '\n'
        << "new positions   " << options.positions << '\n';
    if (resume)
        std::cout << "target total    " << requestedPositionArgument << '\n';
    std::cout
        << "depth           " << options.depth << '\n'
        << "workers         " << options.workers << '\n'
        << "hash/worker     " << options.hashMB << " MB\n"
        << "random plies    " << options.randomPlies << '\n'
        << "first sample    " << std::max({1, options.randomPlies, options.skipPlies}) << '\n'
        << "sample every    " << options.sampleEvery << '\n'
        << "exploration     " << options.exploration << '\n'
        << "explore MultiPV " << options.explorationMultiPV << '\n'
        << "explore maxloss " << options.explorationMaxLoss << " cp\n"
        << "dedup           " << (options.dedup ? "on" : "off") << '\n'
        << "progress every  10 positions\n"
        << "seed            " << options.seed << "\n\n"
        << "Press Ctrl+C once to stop safely and commit the current dataset.\n\n";

    std::atomic<std::uint32_t> nextGameId{0};
    std::atomic<bool> failed{false};
    GenerationStats stats;
    std::vector<std::thread> workers;
    workers.reserve(options.workers);

    for (unsigned workerId = 0; workerId < options.workers; ++workerId) {
        workers.emplace_back(
            workerMain,
            workerId,
            std::cref(options),
            std::ref(writer),
            std::ref(deduper),
            std::ref(stats),
            std::ref(nextGameId),
            std::ref(failed));
    }

    for (auto& worker : workers)
        worker.join();

    // Restore the normal Ctrl+C behavior after workers are stopped.
    std::signal(SIGINT, SIG_DFL);
#ifdef SIGTERM
    std::signal(SIGTERM, SIG_DFL);
#endif

    if (!writer.commit(error)) {
        std::cerr << "dataset commit failed: " << error << '\n';
        return 1;
    }

    if (failed.load(std::memory_order_relaxed)) {
        std::cerr << "dataset generation encountered an error, but "
            << writer.totalCount() << " completed positions were committed safely\n";
        return 1;
    }

    if (stopRequested()) {
        std::cout
            << "stopped safely: added " << writer.addedCount()
            << " positions this run; total " << writer.totalCount()
            << ". Resume with --resume --positions "
            << (resume ? requestedPositionArgument : writer.totalCount() + options.positions)
            << " using the same output file.\n";
        return 0;
    }

    std::cout
        << "done: added " << writer.addedCount()
        << " unique positions; total " << writer.totalCount()
        << "; skipped duplicates " << stats.duplicateSamples.load(std::memory_order_relaxed)
        << "; explored moves " << stats.exploredMoves.load(std::memory_order_relaxed)
        << '\n';

    return 0;
}
