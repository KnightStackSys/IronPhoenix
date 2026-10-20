# One-knight-move-away check threat (Teams HCE)

Experimental penalty on `eval/knight-check-threat`, based on current
`main`. The objective is to slightly discourage allowing a hostile knight
to check **either** member of our team on its next knight move.

This **does not** count knights already checking the king. It identifies
knights that could move to a checking square, assuming the king remains
where it is. Opposing-team-occupied landing squares are disallowed; capturing
a victim-team piece at the landing is possible. A pinned knight or a
knight that would expose its own king may still be counted, because the
calculation is intentionally *pseudo-legal* rather than doing expensive
legal move generation at every HCE evaluation.

It also does not assert that the attacker moves on the very next overall
**ply**, because IronPhoenix rotates turns Red → Blue → Yellow → Green.
Search resolves actual move ordering and whether the king can escape.

## Score and performance

- **5 cp per potentially threatened allied king when enabled** (Red+Yellow or Blue+Green)
- **Maximum 10 cp per team**, even with many attacking knights
- No penalty if the potential checking landing squares are occupied by
  the *knight's* team, if the target king is eliminated, or if the knight
  itself belongs to an eliminated opponent
- Existing knight checks are excluded from this *future threat* term
- Scores remain team-relative
- **NNUE unchanged**: only HCE and its static pruning evaluations are affected

The implementation caches `Geometry::KnightCheckApproach` for each king
square when geometry is initialized, using two knight jumps. During HCE,
it filters candidate knights with a single bitboard intersection and only
examines destinations for the candidates. This reduces search overhead,
but the resulting effect on actual NPS needs measurement.

```text
setoption name EvalKnightCheckThreatPenalty value 0
```

Range **0–100**; the **default is now 0** after the combined knight
experiment reportedly lost 20 Elo. For an independent check-threat
experiment, set a positive value (e.g. 5). `ResetTuning` restores 0.
See [revised knight activity](knight-activity.md) for the focused default.

**Testing:** `ironphoenix_knight_check_threat` verifies knight masks, both
friendly kings, both opponent teams, blocked landing squares, one penalty
per king, score signs, eliminated pieces, and tuning. Run matched x64
Release four-player Teams SPRT tests and compare NPS before merging;
Elo and speed gains are unproven.
