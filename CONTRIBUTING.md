<a id="contributing-to-ironphoenix-"></a>

# <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/git-pull-request.svg" alt="" width="20" height="20"> Contributing to IronPhoenix

Thank you for your interest in contributing to **IronPhoenix**, a C++20 engine for **four-player Teams chess**. Contributions that improve correctness, playing strength, performance, testing, documentation, tooling, or the localhost analysis GUI are welcome.

This guide explains how to propose changes that are easy to review and evaluate. For an overview of the engine and PhoenixNet, start with [README.md](README.md) and [nnue/README.md](nnue/README.md).

<a id="before-you-start"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/clipboard-list.svg" alt="" width="20" height="20"> Before you start

- Review open issues and pull requests to avoid duplicating work. For a larger feature or a substantial search/evaluation redesign, open an issue to discuss the approach first.
- Keep changes focused: ideally, **one feature, fix, or measurable optimization per pull request**.
- Create a feature branch from the latest `main`. Do not mix unrelated search changes, evaluation parameters, refactors, and formatting cleanups in one PR.
- Describe the motivation, expected trade-offs, and a way to reproduce or measure the result.
- Do not commit generated build directories, large datasets, private credentials, or machine-specific configuration files. Add appropriate ignore rules when new tooling creates local artifacts.

<a id="understand-the-four-player-rules"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/crown.svg" alt="" width="20" height="20"> Understand the four-player rules

IronPhoenix is **not** a standard two-player chess engine. All changes must respect:

- **Teams:** Red + Yellow versus Blue + Green.
- **Turn order:** Red → Blue → Yellow → Green, with appropriate handling of eliminated players.
- **Board:** 14 × 14 cross-shaped board with **160 playable squares**.
- **Scoring:** Positive evaluations favor the **side-to-move team**, not necessarily the individual player or Red's team.
- **Moves and state:** Four-player check/king-capture and terminal rules, castling, promotion, en passant, and supported setup variations.
- **Notation:** IronPhoenix's four-player **FEN4** and UCI-style coordinate moves. Ordinary 8 × 8 FEN and two-player SAN are not interchangeable with FEN4/PGN4.

Take particular care with teammate cooperation, the rotation of moves, positions where a king or player is eliminated, and scoring signs. Never assume a two-player chess algorithm or rule transfers unchanged.

<a id="development-environment"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/settings.svg" alt="" width="20" height="20"> Development environment

You will need:

- **CMake 3.24 or newer**
- A **C++20** compiler (MSVC, GCC, or Clang)
- A Git client
- **Node.js 18+** only if working on the localhost web GUI
- Python and any required Python dependencies only for PhoenixNet training/export changes

<a id="build-on-windows-visual-studio--msvc"></a>

### <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/terminal.svg" alt="" width="20" height="20"> Build on Windows (Visual Studio / MSVC)

From the repository root:

```powershell
cmake -S . -B build -A x64 -DIRONPHOENIX_BUILD_TESTS=ON -DIRONPHOENIX_BUILD_BENCHMARK=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

If using the repository's existing Visual Studio CMake `out/build/x64-release` configuration, you can build its `ironphoenix` target instead.

<a id="build-on-linux-gcc-or-clang"></a>

### <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/terminal.svg" alt="" width="20" height="20"> Build on Linux (GCC or Clang)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DIRONPHOENIX_BUILD_TESTS=ON -DIRONPHOENIX_BUILD_BENCHMARK=ON
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

These commands closely match the project's GitHub Actions build-and-test workflow. For generator work, keep `IRONPHOENIX_BUILD_DATASET_GENERATOR=ON` or configure the relevant target explicitly.

<a id="coding-guidelines"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/code.svg" alt="" width="20" height="20"> Coding guidelines

- Follow the surrounding project's **C++20** naming, formatting, headers, and namespace conventions.
- Prefer focused changes with clear invariants over broad rewrites.
- Preserve search correctness: principal-variation handling, score and mate conventions, TT bounds, stop conditions, and four-player turn rotation.
- Do not introduce expensive calculations inside search or evaluation hot paths without measuring their cost. Consider bitboards, reuse of previously computed information, and avoiding redundant allocations.
- For **evaluation changes**, state which positions receive a bonus or penalty, whose team's score changes, and whether the term belongs to **HCE**, **PhoenixNet NNUE**, or both. An HCE change may have no effect when an NNUE network is loaded.
- For **search heuristics** such as LMR, RFP, NMP, SEE, and move pruning, document the guards that protect checks, tactical moves, mate scores, and unstable four-player positions. More pruning is not automatically better.
- Give new evaluation and search parameters understandable defaults and, where appropriate, tunable UCI options and a way to disable them for testing.
- Include explanatory comments where four-player semantics or non-obvious performance assumptions matter; avoid commenting every obvious line.

<a id="testing-changes"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/flask-conical.svg" alt="" width="20" height="20"> Testing changes

**Every code PR should pass the existing tests and include a targeted regression test** when changing behavior.

```bash
ctest --test-dir build -C Release --output-on-failure
```

Relevant test areas include move generation and board correctness, search-stack behavior, mate stopping, pruning, evaluation, and UCI integration. Add new C++ test targets to `CMakeLists.txt` so CTest and CI execute them.

For a new evaluation term, test at least:

1. Both teams and all relevant player orientations.
2. Side-to-move team score signs.
3. The condition that earns the bonus **and** conditions that must not earn it.
4. Special cases such as missing or eliminated pieces, when applicable.
5. Tuning boundaries, reset behavior, and disabling the feature.
6. No unintended changes to the evaluation path when PhoenixNet NNUE is loaded.

For a bug fix, provide the smallest reproducible FEN4 position or command sequence you can. Tests should fail before the fix and pass afterward whenever practical.

<a id="web-gui-changes"></a>

### <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/monitor.svg" alt="" width="20" height="20"> Web GUI changes

The local web UI lives in [`web/`](web/). It uses Node.js to launch the native engine and supports FEN4, PGN4, and UCI analysis. See [web/README.md](web/README.md).

Run the JavaScript test suite from the repository root:

```bash
node --test web/tests/*.test.js
```

Also check JavaScript syntax if you modified client or server code:

```bash
node --check web/app.js
node --check web/server.js
node --check web/notation.js
```

For protocol changes, test with the real compiled engine as well as mock fixtures. Keep the localhost server bound to loopback, validate inputs, and do not introduce arbitrary remote command execution.

<a id="phoenixnet-changes"></a>

### <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/brain.svg" alt="" width="20" height="20"> PhoenixNet changes

If you change `nnue/` training, features, export, or inference:

- Describe any **feature layout**, tensor-shape, output-scale, quantization, or model-format changes.
- Check that the C++ loader and Python exporter agree on serialization and score perspective.
- Clearly separate training/validation results from game-playing results.
- Preserve an HCE fallback and explain model compatibility when changing formats.
- Avoid committing large trained models or generated datasets unless specifically agreed upon.

<a id="measuring-playing-strength-and-speed"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/gauge.svg" alt="" width="20" height="20"> Measuring playing strength and speed

A successful compile, deeper search, higher NPS, or a plausible evaluation term **does not prove an Elo improvement**. For strength-related PRs, include results when available and make clear when a change is still experimental.

For fair A/B testing:

- Compare against a **known `main` commit** with the same compiler, optimization settings, CPU allocation, hash size, time control, and engine options.
- Use consistent four-player starting positions or an opening suite when available; balance teams/seats and repeat enough games to reduce random variation.
- Report the **number of games**, time control, concurrency, engine options, Elo estimate, and uncertainty/confidence interval when available.
- Use SPRT or another documented stopping rule for larger strength tests. Do not label a tiny or inconclusive sample as a confirmed win.
- Test **NPS or elapsed time separately** from playing strength, ideally on a fixed set of positions under comparable conditions.
- Report regressions as well as gains. If a heuristic loses Elo, consider disabling it or testing smaller weights before merging.

If you cannot run a meaningful match, say **"strength not tested"** rather than estimating an Elo improvement.

<a id="opening-a-pull-request"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/git-pull-request.svg" alt="" width="20" height="20"> Opening a pull request

1. Fork the repository or create a feature branch from the latest `main` if you have write access.
2. Make a focused change with tests and documentation as needed.
3. Build and run the appropriate local checks.
4. Open a PR targeting **`main`**. Prefer a descriptive title such as `Eval: team bishop-pair bonus` or `Search: reduce redundant check queries`.
5. Address reviewer feedback and keep your branch reasonably current with `main`. Do not rewrite shared history without coordinating with collaborators.

A useful PR description includes:

```markdown
## Summary
What changed and why?

## Four-player considerations
How are teams, turn order, score signs, or special positions handled?

## Testing
- [ ] C++ build and CTest (Release)
- [ ] Relevant new regression tests
- [ ] Web / PhoenixNet checks, if applicable

## Performance / Elo
Baseline commit:
Test settings:
Results (or "not tested"):

## Risks / trade-offs
Known limitations, edge cases, or follow-up work:
```

A PR can be reviewed before strength testing is complete; just label its status accurately. **Do not claim a performance or Elo improvement without supporting measurements.** The repository's GitHub Actions workflow builds and tests with **Linux GCC and Windows MSVC** for PRs targeting `main`.

<a id="reporting-bugs-and-suggesting-features"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/bug.svg" alt="" width="20" height="20"> Reporting bugs and suggesting features

For a bug, include the operating system, build configuration, relevant commit, exact UCI commands, FEN4 (or a minimal PGN4), expected behavior, actual behavior, and available logs or test output. For crashes, provide a reproducible case if possible.

For a feature request, describe the four-player problem it solves, the intended behavior, possible search/evaluation overhead, and how success could be measured. Benchmarks and representative positions are especially helpful.

<a id="working-together"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/handshake.svg" alt="" width="20" height="20"> Working together

Please keep issue discussions and code reviews respectful, technical, and constructive. Critique implementations and results, not contributors. Credit external ideas, datasets, and code appropriately, and check their licenses before incorporating them.

Thank you for helping make IronPhoenix stronger, faster, and more reliable. <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/flame.svg" alt="" width="20" height="20">
