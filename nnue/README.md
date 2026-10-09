# PhoenixNet NNUE v1

PhoenixNet is IronPhoenix's first neural evaluator for four-player Teams chess.

## Architecture

- 160 playable squares on the 14x14 cross board
- 4 relative colors: self, next enemy, partner, previous enemy
- 6 piece types
- 17 king buckets: 16 spatial buckets plus one missing/eliminated-king bucket
- shared sparse feature transformer: `65280 -> 128`
- two accumulator streams evaluated from side-to-move's perspective:
  - current player's king
  - partner king
- concatenated dense input: `256`
- hidden layers: `32 -> 32`
- scalar output converted to a centipawn-like score

The engine score is always side-to-move-team relative. Positive values favor the team of the player whose turn it is.

## Feature index

Each sparse feature is:

```text
king bucket × relative piece color × piece type × canonical piece square
```

Positions are rotated into the current player's orientation before indexing. Red is canonical, Blue rotates 90 degrees, Yellow 180 degrees, and Green 270 degrees.

## Runtime behavior

IronPhoenix tries to load `ironphoenix.nnue` from its working directory at startup.

You can override the path with:

```text
IRONPHOENIX_NNUE=/path/to/network.nnue
```

If no valid network is found, the engine automatically falls back to the existing handcrafted material + mobility evaluator.

# MultiPV

IronPhoenix exposes a UCI `MultiPV` option:

```text
setoption name MultiPV value 4
```

Valid values are `1..32`. `MultiPV=1` keeps the original PVS/aspiration search path unchanged. Values greater than one use a dedicated root MultiPV search and emit normal UCI lines such as:

```text
info depth 8 seldepth 18 multipv 1 score cp 74 ... pv ...
info depth 8 seldepth 18 multipv 2 score cp 51 ... pv ...
info depth 8 seldepth 18 multipv 3 score cp 29 ... pv ...
info depth 8 seldepth 18 multipv 4 score cp 12 ... pv ...
```

The first line is the best move and is still returned as `bestmove`.

# Dataset generation

The CMake build produces `ironphoenix_dataset` when `IRONPHOENIX_BUILD_DATASET_GENERATOR=ON`.

The generator uses HCE-backed search as the teacher, writes exact C++ `NNUE::featureIndex()` sparse features, supports deduplication, controlled exploration, append/resume, and graceful Ctrl+C stopping.

A typical dataset command is:

```powershell
.\build\Release\ironphoenix_dataset.exe `
    --positions 500000 `
    --depth 8 `
    --workers 8 `
    --hash 16 `
    --random-plies 8 `
    --skip-plies 12 `
    --sample-every 3 `
    --exploration 0.15 `
    --exploration-multipv 4 `
    --exploration-temperature 120 `
    --exploration-max-loss 250 `
    --output nnue\phoenix-v1.ipd
```

## Faster exploration scheduling

Exploration no longer forces every teacher search through MultiPV.

With `--exploration 0.15`:

- about 85% of eligible teacher searches stay normal single-PV searches
- about 15% are selected up front for MultiPV exploration
- exploration is limited to ply 96 and earlier
- positions after ply 96 always use single-PV search
- if an exploration search has no acceptable alternative within `--exploration-max-loss`, the best move is still used
- the training target always remains MultiPV #1's evaluation

This preserves diversity while avoiding the old behavior where MultiPV was paid for on 100% of searches even though only a small fraction actually explored.

Set `--exploration 0` for pure single-PV generation.

## Opening diversity and deduplication

The starting position is never sampled. The default opening phase uses 8 random non-terminal plies and sampling starts no earlier than ply 12.

Deduplication is enabled by default. The generator fingerprints the exact PhoenixNet own-king and partner-king feature streams plus side to move. Duplicate neural inputs are skipped and generation continues until the requested number of unique positions is reached.

Append/resume preloads fingerprints from the existing dataset so deduplication works across runs.

## Graceful stop and resume

Press `Ctrl+C` once to stop safely. Active searches are stopped, worker threads exit, record bytes are flushed, and the authoritative record count is committed before the process returns.

Resume toward a total target:

```powershell
.\build\Release\ironphoenix_dataset.exe `
    --resume `
    --positions 500000 `
    --depth 8 `
    --workers 8 `
    --hash 16 `
    --output nnue\phoenix-v1.ipd
```

If 50,000 positions already exist, `--resume --positions 500000` generates the remaining 450,000.

`--append --positions 500000` has different semantics: it adds another 500,000 positions.

Progress is printed every 10 saved positions.

Older abruptly interrupted files with stale header counts are scanned for complete trailing records. Complete records are recovered; only an incomplete final record is discarded.

## Useful generator options

```text
--positions N              number of samples (or total target with --resume)
--append                   add N new samples to an existing compatible .ipd
--resume                   continue toward N total samples
--depth N                  teacher search depth
--workers N                parallel self-play workers
--hash MB                  transposition-table size per worker
--random-plies N           random legal opening plies
--skip-plies N             earliest configured sample ply
--sample-every N           save one of every N searched positions
--max-plies N              maximum game length
--cp-clamp N               teacher CP clamp
--score-scale N            CP represented by one network output unit
--exploration P            probability of a MultiPV exploration search
--exploration-multipv N    number of root lines available to exploration
--exploration-temperature N softmax temperature in centipawns
--exploration-max-loss N   reject alternatives worse than best by more than N CP
--dedup                    enable position-feature deduplication (default)
--no-dedup                 disable deduplication
--seed N                   deterministic RNG seed
```

# Training

```powershell
py -m venv nnue\.venv
.\nnue\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install torch numpy
```

Train:

```powershell
python nnue\train.py nnue\phoenix-v1.ipd `
    --epochs 20 `
    --batch-size 1024 `
    --lr 0.001 `
    -o nnue\phoenixnet-v1.pt
```

If CUDA is available, `train.py` selects it automatically. Use `--device cpu` or `--device cuda` to override.

# Exporting the network

```powershell
python nnue\export.py nnue\phoenixnet-v1.pt `
    -o ironphoenix.nnue `
    --output-scale 400
```

Place `ironphoenix.nnue` beside `ironphoenix.exe`, or set `IRONPHOENIX_NNUE` to its full path.

A successful load prints:

```text
PhoenixNet loaded: ironphoenix.nnue
```

## NNUE binary file format

All values are little-endian. PhoenixNet v1 currently stores float32 weights.

```text
char[8]  magic = "IPNNUE1\0"
u32      version = 1
u32      feature_count = 65280
u32      ft_size = 128
u32      hidden1_size = 32
u32      hidden2_size = 32
f32      output_scale

f32[65280 * 128] feature_transformer_weights
f32[128]         feature_transformer_bias
f32[32 * 256]    hidden1_weights
f32[32]          hidden1_bias
f32[32 * 32]     hidden2_weights
f32[32]          hidden2_bias
f32[32]          output_weights
f32              output_bias
```

## Development stages

PhoenixNet v1 intentionally starts with full accumulator refreshes. Next optimization stages are incremental accumulators, quantization, SIMD, and SPRT validation against the reference implementation.
