# 4PC Teams knight evaluation: revised after failed strength test

The combined first version of knight check-threat, moving from the back
rank, enemy-piece proximity, and enemy-king proximity reportedly scored
**approximately -20 Elo in a completed test**. It should not be merged
unchanged. These four knobs now default to **0** and remain accessible
only for independent, opt-in experiments.

## Revised default: small, tactical-context outpost (+6 cp)

A surviving knight qualifies only when ALL of the following hold:

1. It has moved at least three board coordinates inward from its own
   home edge (different for Red, Blue, Yellow, and Green).
2. A pawn owned by **the knight's player or the surviving partner**
   defends the knight's current square.
3. No currently alive opposing pawn attacks that square.
4. The knight has entered an opposing player's home-side flank, using
   that player's actual direction on the cross board.
5. At least one enemy **non-king** piece is within Chebyshev distance 2.

The bonus is **6 cp per qualifying knight, capped at two knights per
team (12 cp)**. A bare central knight, undefended advanced knight,
pawn-challenged knight, or knight far from enemy material gets no bonus.
This is *not* a proof of full safety from enemy pieces or of a legal
tactic—just a conservative positional signal.

The outpost check runs **inside the existing knight mobility loop**
and reuses precomputed pawn-attack lookup tables and local-proximity
masks. The old four terms short-circuit when disabled. NNUE is unchanged.

```text
setoption name EvalKnightOutpostBonus value 6
```

Range 0–50, **0 disables** for A/B baseline. The earlier experimental
options now default to 0:

```text
setoption name EvalKnightCheckThreatPenalty value 0
setoption name EvalKnightDevelopmentBonus value 0
setoption name EvalKnightEnemyPressureBonus value 0
setoption name EvalKnightKingApproachBonus value 0
```

`ResetTuning` restores these defaults. To fully match the old HCE
without new knight heuristics, also set `EvalKnightOutpostBonus` to 0.

## Validation / next strength test

Build both engines in identical x64 Release mode with the same
NNUE configuration, hash, threads, and four-player opening suite.
Compare default revised branch against main, then repeat using
`EvalKnightOutpostBonus=0` to isolate positional value versus
overhead. Monitor NPS and depth as well as Elo. No Elo improvement
is claimed until SPRT validates it.
