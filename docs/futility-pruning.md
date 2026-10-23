# Forward futility pruning — move loop

This is separate from **reverse futility pruning (RFP)**. RFP may return
a node-level static cutoff; forward futility **never returns an evaluation
instead of searching a node**. It only skips *later quiet moves* in the
`SearchEngine::negamax` move loop.

## UCI tunable parameters

| UCI name | Default | Range | Meaning |
|---|---:|---:|---|
| `SearchFutilityMaxDepth` | 2 | 0–16 | Maximum remaining depth; 0 disables |
| `SearchFutilityBaseMargin` | 100 | 0–5000 | Static margin |
| `SearchFutilityMarginPerDepth` | 150 | 0–5000 | Additional margin per remaining ply |

The optimistic estimate is `staticEval + baseMargin + depth * marginPerDepth`.
A later move is skipped when the estimate is at or below the **current alpha**.

## 4PC-specific safety rules

Eligible moves must be quiet (no capture/promotion), must not give check,
must not be the TT move, and must come after at least one **searched legal**
move. The node must be non-PV, non-root, not in the null-move subtree, not
in a mate window, and neither moving team's king can be checked.
The existing non-pawn-material condition also applies.
No search is pruned because of the static estimate alone if the move checks
a king. `pos.givesCheck` is evaluated only for plausible pruning candidates.

Static evaluations are reused from the search stack when available; otherwise
computed once per eligible node. Existing RFP, NMP, LMP, HMP, SEE and Lazy SMP
search behavior is preserved. Defaults for all previously tuned search and
evaluation options are unchanged. IIR is not part of this feature.

## Testing

Run `ironphoenix_futility` or `ctest` from the build directory. The
dedicated test checks UCI bounds/defaults and node-count differences, plus
PV, root, partner-check, own-check, null-move and maximum-depth exclusions.

Use a paired SPRT match against `SearchFutilityMaxDepth=0` before
merging. A lower node count does not establish an Elo gain.
