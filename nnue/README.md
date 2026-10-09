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

# Creating the first dataset

The CMake build now produces a second executable when `IRONPHOENIX_BUILD_DATASET_GENERATOR=ON`:

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
    --random-plies 4 `
    --skip-plies 8 `
    --sample-every 3 `
    --output nnue\phoenix-v1.ipd
```

On a 16-core / 24-thread CPU, start around 8-12 generator workers. Each worker owns its own search engine and transposition table. `--hash` is per worker, so 12 workers with `--hash 16` use roughly 192 MB just for transposition tables.

Useful generator options:

```text
--positions N       number of samples to generate in this run
--append            safely append this run to an existing compatible .ipd file
--depth N           teacher search depth
--workers N         independent parallel self-play workers
--hash MB           transposition-table size per worker
--random-plies N    random legal opening plies for diversity
--skip-plies N      earliest ply that may be saved
--sample-every N    save one of every N searched positions
--max-plies N       maximum length of one self-play game
--cp-clamp N        clamp search CP before conversion to target
--score-scale N     CP represented by one network output unit
--seed N            deterministic opening RNG seed
```

## Continuing an existing dataset

Use `--append` to grow an existing `.ipd` file without erasing its current records.

If `phoenix-v1.ipd` already contains 500,000 positions, this command adds another 500,000:

```powershell
.\build\Release\ironphoenix_dataset.exe `
    --append `
    --positions 500000 `
    --depth 8 `
    --workers 8 `
    --hash 16 `
    --output nnue\phoenix-v1.ipd
```

After the run the dataset contains 1,000,000 positions. In append mode, `--positions` always means the number of **new** positions to add during that run.

Append mode is deliberately strict. Before modifying the existing file it validates:

- `IPDATA1` version
- PhoenixNet feature count
- maximum sparse feature count
- teacher search depth
- score scale

If those do not match, the append is refused and the existing dataset is left unchanged. Settings that are meant to add diversity, such as random opening plies or sampling cadence, may be changed between append runs.

If `--seed` is not supplied during an append, IronPhoenix derives a different deterministic seed from the original dataset seed and its current record count. This avoids regenerating the exact same random opening sequence. Supplying `--seed` explicitly overrides this behavior.

Appended game IDs are automatically shifted above the highest existing game ID. This preserves game-level train/validation splitting even after many append runs.

The record count is committed only after all new records are flushed. If an append is interrupted after data reaches disk but before the count is committed, the next `--append` run detects and safely removes those uncommitted trailing bytes before continuing.

If `--append` is used and the output file does not exist yet, the generator simply creates a new dataset normally.

Mate scores are not written as ordinary training targets. Non-mate search scores are side-to-move-team relative, clipped to `--cp-clamp`, and divided by `--score-scale` before storage.

## Native IPD dataset format

`ironphoenix_dataset` writes a compact little-endian `IPDATA1` stream. Each record contains:

- own-king sparse feature indices
- partner-king sparse feature indices
- scaled training target
- original teacher centipawn score
- game ID
- ply
- side to move
- flags for check/eliminated-player state

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
