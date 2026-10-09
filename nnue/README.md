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

# Creating the first dataset

The CMake build produces a second executable when `IRONPHOENIX_BUILD_DATASET_GENERATOR=ON`:

```text
ironphoenix_dataset
```

It generates self-play positions using the current HCE-backed search as the teacher and writes the exact sparse feature indices produced by IronPhoenix's C++ `NNUE::featureIndex()` implementation.

The generator does not load `ironphoenix.nnue`, so the first dataset is taught by the existing HCE + search rather than by an untrained neural network.

A quick smoke dataset:

```powershell
.\build\Release\ironphoenix_dataset.exe `
    --positions 10000 `
    --depth 6 `
    --workers 4 `
    --hash 16 `
    --output nnue\phoenix-smoke.ipd
```

A better first training set:

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

On a 16-core / 24-thread CPU, start around 8-12 generator workers. Each worker owns its own search engine and transposition table. `--hash` is per worker, so 12 workers with `--hash 16` use roughly 192 MB just for transposition tables.

Useful generator options:

```text
--positions N              number of new samples to generate in this run
--append                   safely append this run to an existing compatible .ipd file
--resume                   continue an existing dataset toward --positions total
--depth N                  teacher search depth
--workers N                independent parallel self-play workers
--hash MB                  transposition-table size per worker
--random-plies N           random legal opening plies for diversity (default 8)
--skip-plies N             earliest configured sample ply (default 12)
--sample-every N           save one of every N searched positions
--max-plies N              maximum length of one self-play game
--cp-clamp N               clamp search CP before conversion to target
--score-scale N            CP represented by one network output unit
--exploration P            probability 0..1 of using MultiPV exploration
--exploration-multipv N    number of top root lines available to exploration
--exploration-temperature N softmax temperature in centipawns
--exploration-max-loss N   reject alternatives worse than best by more than N CP
--dedup                    enable position-feature deduplication (default)
--no-dedup                 disable deduplication
--seed N                   deterministic opening RNG seed
```

## Exploration

The generator uses controlled search exploration instead of unrestricted random moves after the opening.

With the default settings:

```text
--exploration 0.15
--exploration-multipv 4
--exploration-temperature 120
--exploration-max-loss 250
```

IronPhoenix now performs a normal single-PV teacher search on most positions. Only positions selected by the exploration probability pay for the MultiPV search. With `--exploration 0.15`, about 85% of eligible teacher searches remain single-PV and about 15% use MultiPV. Exploration is also limited to ply 96 and earlier; later positions always use the faster single-PV teacher search.

When an exploration search is selected, IronPhoenix normally continues self-play with MultiPV #1 but may select another strong line, weighted by score gap. Alternatives more than 250 centipawns below the best line are rejected.

The important detail is that the **training target always remains MultiPV #1's evaluation**. Exploration changes only the move used to continue self-play. This increases position variety without teaching the network that an intentionally exploratory move was the best evaluation.

Set:

```text
--exploration 0
```

to disable search exploration completely.

## Opening diversity and deduplication

The generator deliberately avoids repeatedly training on the initial position:

- the starting position is never eligible to be saved
- the default opening phase makes 8 random non-terminal legal plies
- sampling starts no earlier than the maximum of ply 1, `--random-plies`, and `--skip-plies`
- default `--skip-plies 12` therefore keeps the earliest repeated opening states out of the dataset

Deduplication is enabled by default. Before writing a record, the generator hashes its exact PhoenixNet own-king and partner-king sparse feature streams plus side to move. If that same neural input has already been saved, the duplicate is skipped and generation continues until the requested number of **unique** samples is reached.

When `--append` or `--resume` is used, IronPhoenix scans the existing `.ipd` file first and preloads those feature fingerprints. This means the duplicate filter also works across later runs, not just within one process invocation.

## Graceful stop and resume

Press `Ctrl+C` once to stop the current generation safely. Active searches are stopped, workers exit, completed records are flushed, and the dataset header count is committed before the process returns.

Resume toward a total size with:

```powershell
.\build\Release\ironphoenix_dataset.exe `
    --resume `
    --positions 500000 `
    --depth 8 `
    --workers 8 `
    --hash 16 `
    --output nnue\phoenix-v1.ipd
```

If the dataset already contains 50,000 records, `--resume --positions 500000` generates the remaining 450,000. By contrast, `--append --positions 500000` adds another 500,000 records.

Progress is reported every 10 saved positions.

Older abruptly-stopped datasets with stale header counts can be recovered: complete trailing records are scanned and committed, while only an incomplete final record is discarded.

## Native IPD dataset format

`ironphoenix_dataset` writes a compact little-endian `IPDATA1` stream. Each record contains:

- own-king sparse feature indices
- partner-king sparse feature indices
- scaled training target
- original teacher centipawn score
- game ID
- ply
- side to move
- flags for check, eliminated-player state, and whether the continuation move was exploratory

Feature indices are stored as `uint16` because PhoenixNet v1 has 65,280 features.

`train.py` reads `.ipd` directly and uses the game IDs to keep entire self-play games on one side of the train/validation split.

# Training

Install Python dependencies:

```powershell
py -m venv nnue\.venv
.\nnue\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install torch numpy
```

Train a smoke network:

```powershell
python nnue\train.py nnue\phoenix-smoke.ipd `
    --epochs 5 `
    --batch-size 1024 `
    -o nnue\phoenix-smoke.pt
```

Train the first larger network:

```powershell
python nnue\train.py nnue\phoenix-v1.ipd `
    --epochs 20 `
    --batch-size 1024 `
    --lr 0.001 `
    -o nnue\phoenixnet-v1.pt
```

If CUDA is available, `train.py` selects it automatically. Use `--device cpu` or `--device cuda` to override the choice.

# Exporting the network

Export the best checkpoint to the format consumed by the engine:

```powershell
python nnue\export.py nnue\phoenixnet-v1.pt `
    -o ironphoenix.nnue `
    --output-scale 400
```

Place `ironphoenix.nnue` beside `ironphoenix.exe`, or set `IRONPHOENIX_NNUE` to its full path.

At startup a successful load prints:

```text
PhoenixNet loaded: ironphoenix.nnue
```

If the file is missing or incompatible, IronPhoenix prints the HCE fallback message and continues using material + mobility.

## NNUE binary file format

All values are little-endian. PhoenixNet v1 currently stores float32 weights to make training/export validation straightforward before the later quantized/incremental optimization pass.

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

PhoenixNet v1 intentionally starts with full accumulator refreshes. This gives us a correctness baseline that is easy to compare against the trainer.

Next optimization stages:

1. add make/undo incremental accumulator updates
2. maintain four seat perspectives and own/partner king streams
3. refresh only streams whose king bucket changes
4. quantize feature-transformer weights to int16
5. quantize dense weights to int8 with int32 accumulation
6. add AVX2 inference
7. SPRT each optimized implementation against the reference implementation

Do not remove the refresh implementation until incremental make/undo tests prove byte-for-byte/eval equivalence across captures, promotions, en-passant, castling and king bucket changes.
