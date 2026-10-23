# Internal Iterative Reduction (4PC Teams)

IIR is now allowed at **every `negamax` node**, not just PV nodes.
There is no PV/cut/all-node, king-check, partner-check, or root exclusion.
With the current defaults, the rule is equivalent to:

```cpp
if (depth >= tuning.iirMinDepth && !ttMove)
    depth -= std::min(tuning.iirReduction, depth - 1);
```

`SearchIIRMinDepth` and `SearchIIRReduction` are the only two UCI knobs.
The old `SearchIIRCutNode` and `SearchIIRAllNode` toggles were removed
because IIR no longer depends on node types; cut/all propagation was
also removed from `negamax`.

| UCI option | Default | Valid range | Purpose |
|---|---:|---:|---|
| `SearchIIRMinDepth` | 6 | 2–32 | Minimum depth to apply IIR |
| `SearchIIRReduction` | 1 | 0–4 | Depth reduction; 0 disables |

All other tuned search and evaluation values are unchanged. A safety clamp
keeps the remaining depth >= 1, even when tuning a large reduction.

The engine emits `info string iir reductions N` before the bestmove
for each normal/MultiPV search. This counts applied depth reductions.

**Important:** Applying IIR during checks, at the root and at non-PV
nodes can reduce tactical accuracy and playing strength. Run independent
paired SPRT before merging this aggressive variant into `main`.
