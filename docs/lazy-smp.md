# IronPhoenix Lazy SMP (4PC Teams)

Lazy SMP is being developed on `feature/lazy-smp` to avoid breaking the
independent IIR experiments or the stable `main` search.

Use `setoption name Threads value N` in UCI (`1..64`, default `1`).
For example:

```text
uci
setoption name Threads value 8
setoption name Hash value 512
position startpos
go movetime 10000
stop
```

The primary search handles time management, aspiration/PVS and final UCI
`bestmove`. Additional workers execute independent iterative-deepening
searches from the same four-player team position. Helpers start at staggered
depths to diversify their exploration and **do not emit** UCI output.
Workers have separate PV buffers, SearchStack and history. A shared TT
is protected by striped cluster mutexes (uncontended when `Threads=1`).

The reported `nodes`/`nps` include batched counts from helpers as well as
the main search. Fixed node budgets use this aggregate (up to a small
batching overshoot). Stopping or starting a new search joins every helper;
`setoption` and `ucinewgame` stop workers before changing hash or
configuration. `MultiPV` keeps one authoritative primary output and
uses standard search helpers to populate the shared TT.

**Note:** Multi-thread search can increase total throughput while hurting
single-thread NPS due to synchronization, cache, and CPU contention. Verify
strength via paired SPRT/benchmarks across thread counts. Test with a
reasonable Hash allocation: helper workers share a TT but have private
small allocation for initialization. Does not include IIR.
