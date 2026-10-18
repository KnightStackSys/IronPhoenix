# Stop iterative deepening on a winning mate

After a **completed** root search iteration produces an exact winning mate
score (between `MATE_THRESHOLD` and `MATE_SCORE`) and a valid best move,
IronPhoenix emits its ordinary PV/info and then exits the depth loop to
send `bestmove` immediately.

The guard runs **after** aspiration-window re-search is finished. Lower- and
upper-bound intermediate scores cannot trigger it. MultiPV waits for all
requested lines of the completed depth to be emitted first.

* **Winning mate:** stop early after a completed iteration.
* **Losing mate:** keep searching for defenses / a way to delay defeat.
* **`go infinite`:** keep analysing until an explicit `stop`.
* **Active `go ponder`:** keep thinking until `ponderhit` or `stop`;
  after a hit, normal early-exit behavior applies.
* **No valid root move:** do not trigger the shortcut.

This changes search *duration* only, not the mate scoring, pruning rules,
transposition table logic, or evaluation.

Regression test: `tests/mate_stop.cpp` checks the predicate and searches
a 4PC Teams position with a winning king capture at depth one in normal and
MultiPV modes; the search should report `score mate 1`, emit `bestmove`,
and not deepen to depth two.

A mate-score-based early exit is still only as reliable as the engine's
mate detection and evaluation; this does not independently prove a checkmate.
