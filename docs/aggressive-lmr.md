# Aggressive quiet LMR

This is an experimental strength change, not a measured Elo gain. It replaces
the unfinished adjustment block in `SearchEngine::negamax`, after a legal move
has been made and before its PVS zero-window search. Red/Yellow and Blue/Green
alternate teams on every ply, so the existing negated child windows and mate
score conventions remain intact.

## Initial parameters

| UCI option | Default |
| --- | ---: |
| SearchLMRMinDepth | 3 |
| SearchLMRFullMoves | 2 |
| SearchLMRBaseX100 | 75 |
| SearchLMRDivisorX100 | 225 |

The base is `floor(0.75 + ln(depth) * ln(legalMoveNumber) / 2.25)`.
The first two legal moves are protected. Non-PV nodes add one reduction ply;
PV nodes subtract one. The combined quiet/continuation/countermove ordering
score subtracts `clamp(history / 8000, -2, 2)` plies. Negative history therefore
reduces more, and reliable moves reduce less. TT ordering scores are excluded
by protecting the TT move. Final reductions are clamped to `[0, depth - 2]`,
leaving at least one full child ply before quiescence.

For example, at depth 8 and legal move 16 with neutral history, the reduction
is 4 plies at a non-PV node and 2 at a PV node (formerly 2 at either).

## Tactical protection and verification

Captures, promotions, castling, TT moves, checks, checked movers, positions
with a checked partner, and mate windows are excluded from LMR. After making
an eligible move, check both enemy kings and the partner again. This covers
discovered attacks by the partner that the mover-only `givesCheck()` helper
does not recognize, and newly exposed partner kings.

A reduced search that exceeds alpha is always searched again at the full child
depth with a zero window, including fail-high results. A result inside the
parent window then receives the existing full-window PVS re-search. LMR does
not skip eligible moves outright. Other existing pruning is unchanged; these
protections describe LMR specifically.

Four-player quiet threats and cooperative sequences can remain invisible at
reduced depth. Re-search only helps when a reduced result beats alpha, so it
cannot guarantee recovery of a threat that fails low. Extra tactical gates
also mean that this version can search more nodes in some positions despite
its larger quiet reductions.

## Validation and strength testing

The CMake test target `ironphoenix_lmr` covers reduction bounds, history/PV
monotonicity, tuning gates, and terminal king captures while checked for all
four movers. It uses explicit checks that remain active in Release builds.

Compare against the parent commit using identical compiler settings, hash,
threads, evaluation mode, and (for NNUE) identical network files. Use varied
paired openings with teams swapped and the same time control. Run an SPRT
with a chosen small gain hypothesis (for example Elo0=0, Elo1=5 and 5% error
rates) using a tester appropriate for paired 4PC Teams games. Confirm a pass
at a longer time control and examine tactical losses, especially partner
defense and promotion races. Lower node counts or higher depth alone do not
establish strength. Test HCE and NNUE separately if both modes will be used.

For a milder follow-up experiment, raise SearchLMRDivisorX100 to 250 or 275,
or SearchLMRFullMoves to 3. These retain the new safety/history logic and do
not reproduce the old implementation exactly.
