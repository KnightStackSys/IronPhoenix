# Per-ply search stack

`SearchEngine::searchStack_` now stores `SearchStackEntry`, defined in
`include/ironphoenix/search_stack.hpp`, instead of only `HistoryContext`.

| Field | Meaning |
| --- | --- |
| `history` | Parent move, moved piece and mover for continuation/countermove history |
| `currentMove`, `bestMove` | Current candidate and best searched move at this ply |
| `moveCount` | Legal moves searched so far |
| `depth`, `sideToMove` | Search depth (0 in qsearch) and active color |
| `pvNode`, `ttPv` | PV-window flag and PV-from-node/TT flag |
| `ttHit`, `ttMove`, `ttDepth`, `ttBound` | Result of the node's TT probe |
| `inCheck` | Whether the active player is in check, when that is computed |
| `staticEvalValid`, `staticEval` | Whether this node was evaluated, and its static score |
| `improving`, `improvingRate` | Positive trend and centipawn difference versus an evaluated same-team ancestor |

Each node resets its node-local fields with `beginNode()`; incoming
`history` is intentionally preserved so continuation and countermove history
still function. Null moves clear the incoming history, as before.

Improvement is compared against the **nearest evaluated same-team ancestor 2–6
plies earlier**. The team check matters because 4PC evaluations are relative
to the side-to-move team (RED/YELLOW vs BLUE/GREEN), and eliminated players
may be skipped. `improvingRate` is a centipawn *difference*, not an Elo or
wall-clock rate.

For performance, evaluations are recorded only where the engine already
evaluates a position: qsearch stand-pat/fallback and the non-PV null-move
pruning evaluation. The absence of a valid static evaluation means
`staticEvalValid == false`, **not** that the position is equal.

This PR adds observational metadata; it does not alter pruning, reductions,
move ordering, or TT replacement. Future heuristics can use the fields with
their validity checks.

Tests: `tests/search_stack.cpp`.
