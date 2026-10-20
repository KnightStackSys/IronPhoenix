# Complementary team bishop-pair evaluation

**Branch:** `eval/team-bishop-pair-v2` (fresh from current `main`).

In four-player **Teams** mode (Red + Yellow vs Blue + Green), the HCE
awards **one team bonus** when the surviving team members jointly possess
**at least one light-squared and one dark-squared bishop**. One player
may supply both colors, or teammates may each supply a different color.
Bishops belonging to an eliminated teammate do not count.

The two square colors are calculated from the **absolute** file/rank parity
(`(file + rank) & 1`), not the engine's dense Square index. Having two
or more bishops on *only* light squares or *only* dark squares grants
**no bishop-pair bonus**. Multiple complete pairs do not multiply the
bonus. The relative scores are +40 cp to the owning team at the default
setting, or -40 cp from the opposing team's perspective.

This is complementary to the existing material, mobility and attacked-piece
evaluation, and is implemented only in **HCE**, not loaded NNUE.

## Tunable UCI option

```text
setoption name EvalBishopPairBonus value 40
```

Defaults to **40 cp**, tunable **0 to 1000**, where **0 disables the term**
for direct A/B comparisons. Changing tuning via UCI clears the search
transposition table; `ResetTuning` returns to default.

## Testing

CTest target: `ironphoenix_bishop_pair`. Regression cases cover both
teams, the same player's complementary bishops, partner-split pairs,
same-color-only bishops, eliminated partner, extra bishops, score sign
for all four seats, option validation and UCI advertisement.

SPRT with matched x64 Release builds and the same seat-swapped 4PC
opening suite is required before claiming an Elo or NPS gain.
