# Knight development and enemy-side activity (4PC Teams HCE)

This evaluation experiment extends the `eval/knight-check-threat` branch
with **small, independently tunable knight activity terms**.

On the 160-square four-player cross board, occupying its geometric centre
does **not** automatically receive a positional bonus. Instead, knights
receive a development reward for leaving their own back rank/file, plus
additional rewards for approaching enemies.

| Condition (per surviving player's knight) | Default |
| --- | ---: |
| Left its own back rank (RED rank 1, BLUE file a, YELLOW rank 14, GREEN file n) | +3 cp |
| An alive enemy's non-king piece is within Chebyshev distance 2 | +4 cp |
| An alive enemy king is within Chebyshev distance 4 | +5 cp |

Bonuses stack but the enemy-army bonus is **once per knight**, even if
several enemy units are nearby. King approach counts **once per knight**,
even if both opposing kings are close. A central knight with no nearby
enemies earns only +3 cp for developing.

The code precomputes each square's radius-2 proximity mask at geometry
startup and uses bitboard intersections rather than scanning enemy pieces
each evaluation. Enemy king distance uses at most two small coordinate
checks per knight. Eliminated opponents and own teammates are ignored.

This is an experimental **static positional bonus**, not a proof that
the knight is safe, legal to move, defended, or can attack the enemy next
turn. The pre-existing one-move-away knight-check penalty is unchanged.

### UCI tuning

```text
setoption name EvalKnightDevelopmentBonus value 3
setoption name EvalKnightEnemyPressureBonus value 4
setoption name EvalKnightKingApproachBonus value 5
```

Each ranges **0–50**, and `0` disables that *specific* term for A/B
tests. `ResetTuning` restores defaults.

**HCE only:** NNUE scores remain authoritative when a net is loaded.
Because these terms affect HCE static eval and pruning, use controlled
four-player matched builds, consistent NNUE settings, and seat-swapped
SPRT before claiming any strength improvement. Compare NPS and depth too.

Note: This initial experiment is not explicitly game-phase tapered.
Its design favors useful threats over generic centre control, which can
be worthwhile in the opening and middlegame, but it may still need
late-game tuning.
