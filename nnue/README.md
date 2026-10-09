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

## Binary file format

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
