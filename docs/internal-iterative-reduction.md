# Internal Iterative Reduction (4PC Teams)

This feature branch merges current `main` with the original
`internal-iterative-reduction` experiment. Existing search pruning, eval,
NNUE, MoveGen, test infrastructure, and cut/all-node propagation are retained.

IIR reduces search depth when a sufficiently deep node lacks an ordering
move from the transposition table. It is deliberately conservative in 4PC:
the moving side's king and its **partner's king** must both be out of check,
and root positions are excluded. In the default configuration, only PV
nodes qualify. We keep the branch's original depth-six, one-ply policy.

| UCI option | Default | Valid range | Purpose |
|---|---:|---:|---|
| `SearchIIRMinDepth` | 6 | 2–32 | Minimum node depth |
| `SearchIIRReduction` | 1 | 0–4 | Depth reduction (0 disables) |
| `SearchIIRCutNode` | 0 | 0–1 | Opt into non-PV cut-node IIR |
| `SearchIIRAllNode` | 0 | 0–1 | Opt into non-PV all-node IIR |

The reduction is clamped so search depth remains at least 1. No IIR is
performed when a TT move exists, at root, in check, or when the teammate is
in check. PVS first-child, scout, null-move, and re-search propagation keeps
cut/all-node context explicit.

## SPSA / SPRT

Tunex can tune all four variables using UCI `setoption`. A recommended
starting point is to tune `SearchIIRMinDepth` and
`SearchIIRReduction` with `SearchIIRCutNode=0` and
`SearchIIRAllNode=0`; test enabling the non-PV switches separately.
Use paired-game SPRT before merging into `main` because IIR affects
playing strength and the search tree.

This feature's default is active only at sufficiently deep internal PV
nodes. It need not raise raw nodes/second (NPS); compare Elo and time
to depth rather than assuming a speedup from a smaller search.
