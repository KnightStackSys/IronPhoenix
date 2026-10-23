# IronPhoenix performance experiments

Branch: \`feature/king-evaluation\`. Keep \`main\` stable until Elo tests.

## Changes

- Precomputed immutable, seat-aware king shield/storm/wing masks.
- Hoisted four-army material phase, slider presence, team pawns and occupancy
  out of the per-king evaluation path.
- Reuse the existing mobility slider-attack maps to avoid pin/X-ray probes
  if no opposing slider attacks an allied screening piece.
- Fast (exact) geometric prefilter for PV quiet move check ordering. Discovered
  checks and checks that already exist are handled by the full evaluator.
- UCI \`SearchFastCheckPrefilter\` defaults to 1; use 0 for A/B comparison.
  A broader version in non-PV pruning was tested and **reverted** because it
  showed no measurable benefit on the same runner.
- Benchmark \`ironphoenix_nps_bench\` now measures feature groups in addition
  to its original fixed-position search throughput.

## Native CPU Release build (opt-in)

CMake option: \`IRONPHOENIX_NATIVE_CPU=ON\` (default OFF).

**Windows (PowerShell / Visual Studio generator):**

\`\`\`powershell
cmake -S . -B build-native -DIRONPHOENIX_NATIVE_CPU=ON
cmake --build build-native --config Release --parallel
.\build-native\Release\ironphoenix_nps_bench.exe
\`\`\`

**Linux (GCC/Clang):**

\`\`\`bash
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DIRONPHOENIX_NATIVE_CPU=ON
cmake --build build-native --parallel
./build-native/ironphoenix_nps_bench
\`\`\`

GCC/Clang use \`-march=native\`; MSVC uses \`/arch:AVX2\` for Release
and RelWithDebInfo. The MSVC option is a broad, predictable instruction-set
target rather than necessarily the exact highest available CPU features.

**Do not redistribute a native build to arbitrary computers**: it may use
instructions unsupported by another CPU. Keep portable builds with the
option OFF.

## How to judge speed

1. Run portable and native binaries on the **same computer** with the
   same node limit and fixture.
2. Repeat each benchmark (e.g., five times), compare median elapsed time
   and NPS rather than a single run.
3. Run both with \`Threads=1\` when comparing per-core changes; then compare
   total throughput with \`Threads=4\`, \`8\`, etc.
4. Judge search strength separately via paired SPRT. Higher NPS alone is
   not evidence that a pruning change increases Elo.

CI cloud runners have different CPU allocations, so absolute NPS between
different workflow runs is not a valid speedup measurement. The benchmark's
same-runner feature variants provide better relative evidence.

Recent **single Linux CI runner** experimental comparisons (not user-machine
NPS and not proven Elo gains):

| Switch | Disabled | Enabled | Speedup |
|---|---:|---:|---:|
| `SearchEvalCache` | 598,037 | 612,344 | about 2.4% |
| `SearchFastKingLegality` | 856,053 | 861,818 | about 0.7% |

Benchmark runs use the same fixed 4PC fixture, node budget and worker count,
but the repeated variant searches occur sequentially, so CPU variability and
hash/iteration rounding still affect short measurements. Run longer tests
locally, repeat trials, and rely on medians.

This feature branch retains all new king-evaluation terms. NNUE still
bypasses HCE; the HCE speed work helps only when NNUE is disabled.
