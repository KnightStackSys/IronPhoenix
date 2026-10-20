# Complementary team bishop-pair evaluation

**Branch:** `eval/team-bishop-pair-v2` (fresh from current `main`).

In four-player **Teams** mode (Red + Yellow vs Blue + Green), the HCE
awards **one team bonus** when the surviving team members jointly possess
**at least one light-squared and one dark-squared bishop**. One player
may supply both colors, or teammates may each supply a different color.
Bishops belonging to an eliminated teammate do not count.

The two square colors use **absolute** file/rank parity
(`(file + rank) & 1`), not the engine's dense Square index. Both color
masks are precomputed **once at Geometry initialization** (80 squares
per mask), and evaluation uses bitboard intersection checks rather than
looping over bishops at every search node. Having two or more bishops
on *only* light squares or *only* dark squares grants **no pair bonus**.
Multiple complete pairs do not multiply the bonus. At the experimental
default, the relative score is +10 cp for the owning team (or -10 cp
from the opposing team's perspective).

This is complementary to the existing material, mobility and attacked-piece
evaluation, and is implemented only in **HCE**, not loaded NNUE.

## Tunable UCI option

```text
setoption name EvalBishopPairBonus value 10
```

Defaults to **10 cp**, tunable **0 to 1000**, where **0 disables the term**
for direct A/B comparisons. Changing tuning via UCI clears the search
transposition table; `ResetTuning` returns to default.

The previous **40 cp** setting reportedly lost about **40 Elo** and
reduced search speed in a user-reported match. The faster mask lookup
addresses per-evaluation overhead, while the lower default reduces
the heuristic's influence on evaluation and static-eval pruning.
Neither improvement should be assumed to regain Elo or NPS until
controlled benchmarks and matched SPRT results confirm it.

## Testing

CTest target: `ironphoenix_bishop_pair`. Regression cases cover both
teams, the same player's complementary bishops, partner-split pairs,
same-color-only bishops, eliminated partner, extra bishops, score sign
for all four seats, option validation and UCI advertisement.

SPRT with matched x64 Release builds and the same seat-swapped 4PC
opening suite is required before claiming an Elo or NPS gain.
