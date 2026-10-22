<a id="reverse-futility-pruning-in-teams-search"></a>

# <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/scissors.svg" alt="" width="20" height="20"> Reverse futility pruning in Teams search

At shallow non-PV nodes, RFP skips the subtree when the side-to-move team's
static evaluation minus a safety margin is already at least beta. This can
save nodes in clearly favorable positions; it is a selective heuristic, not
a proof of a minimax bound or of increased playing strength.

The aggressive experimental settings are **depth 1–6** (plies, not full
four-seat cycles) and **130 evaluation units per remaining ply**. When a
same-team ancestor provides a valid improving trend, the margin shrinks by
`min(80, improvingRate / 3)` centipawns, never falling below 90 cp.
No improvement comparison means no discount. The returned fail-high estimate
is `staticEval - max(90, 130 * depth - improvingDiscount)`, rather than
the unadjusted static surplus. No exact TT entry or best move is manufactured
by the cutoff.

RFP runs after mate-distance/TT cutoffs and before NMP and ordinary move
search. Evaluation is already relative to the moving team's perspective:
Red/Yellow versus Blue/Green. The regular four-seat rotation alternates teams,
so the existing child negation is retained. The raw evaluation is recorded on
the per-ply search stack and reused by NMP if RFP declines to cut.

Guards exclude root/PV nodes, synthetic null-move child nodes, mate windows and
mate-range static evaluations, own/partner check, and movers without non-pawn
material. A candidate cutoff additionally requires four living players and
four present, unchecked kings. Checking enemy kings is conservative but also
protects immediate king-capture wins. Pawns one step from promotion in any
of the four orientations disable RFP, even when blocked. Existing geometry
helpers retain the standard rank 11/file k/rank 4/file d promotion lines.
Pseudo-legal terminal captures and promotions are explicitly excluded, and
two legal witnesses are required to preserve stalemate and forced moves.
Every witness make/undo restores the board and hash.

These guards cost work on candidate cutoffs and do not eliminate all tactical
or zugzwang errors. They leave NMP's existing policy and other pruning intact.
The increased max depth and smaller margin intentionally trade tactical safety
for more speculative cutoffs; no Elo gain is assumed. The improving discount
only applies to team-relative, previously recorded evaluations; missing history
does not imply improvement.

Use CTest for deterministic guard/score/state regressions, then compare to
the same main revision with identical compiler, network, hash, threads and
opening suite. Run paired seat/team-swapped fixed-node and fixed-time games,
inspect tactical losses and node/time/depth changes, and use a predeclared
SPRT before claiming an Elo gain.

The old experiment is PR #28 at branch tip
`dcefe70131df6c83a69910e2edec39086e3efa7f`. Its RFP idea and initial constants
are retained, while its unrelated merge history is excluded from this branch.

<a id="search-hot-path-optimization-nps-experiment"></a>

## <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/gauge.svg" alt="" width="20" height="20"> Search hot-path optimization (NPS experiment)

The `reverse-futility-pruning-teams` branch also defers expensive
`givesCheck()` calls at non-PV nodes until a move would otherwise be
pruned by LMP, HMP, or SEE. PV nodes still receive full move ordering but
no longer build check metadata used only by non-PV pruning. RFP and null-move
pruning share a single partner-check/non-pawn-material eligibility test and
reuse the recorded static evaluation. No pruning margins, move protections,
or cutoffs were relaxed in this optimization. It targets time per node, not
just reducing the number of nodes searched; measure NPS in an identical
fixed-position/fixed-node search before asserting a speedup.
