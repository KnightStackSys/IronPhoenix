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


## Updated tuned search defaults

The search structure now includes the user's latest SPSA values for all
21 non-IIR parameters. `uci` reports the live `SearchParameters{}`
defaults rather than stale hard-coded literals. Existing evaluation
parameters are unchanged.

## Is IIR actually running?

By design, the default policy only reduces **internal PV nodes at depth
at least six** with **no TT move** and **neither king in check**. In iterative
deepening the PV nodes frequently have TT moves from earlier iterations;
therefore a zero count at short time controls is entirely possible.

After each completed regular or MultiPV search, the engine emits
`info string iir reductions N` immediately before `bestmove`. This is
the count of **actual depth reductions applied**, not just considered nodes.
The counter resets on each search. With the supplied cut/all defaults
both zero, enabling IIR at non-PV nodes requires an explicit UCI change
and independent playing-strength testing.

Example:
```text
setoption name SearchIIRMinDepth value 6
setoption name SearchIIRReduction value 1
setoption name SearchIIRCutNode value 1
setoption name SearchIIRAllNode value 0
go depth 10
```
This example changes search behavior only if explicitly entered; the
repository defaults remain PV-only.
