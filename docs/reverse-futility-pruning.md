# Reverse futility pruning in Teams search

At shallow non-PV nodes, RFP skips the subtree when the side-to-move team's
static evaluation minus a safety margin is already at least beta. This can
save nodes in clearly favorable positions; it is a selective heuristic, not
a proof of a minimax bound or of increased playing strength.

The initial settings are depth 1–4 (plies, not full four-seat cycles) and
200 evaluation units per remaining ply. The returned fail-high estimate is
`staticEval - 200 * depth`, rather than the unadjusted static surplus.
No exact TT entry or best move is manufactured by the cutoff.

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
Use CTest for deterministic guard/score/state regressions, then compare to
the same main revision with identical compiler, network, hash, threads and
opening suite. Run paired seat/team-swapped fixed-node and fixed-time games,
inspect tactical losses and node/time/depth changes, and use a predeclared
SPRT before claiming an Elo gain or tuning the depth/margin more aggressively.

The old experiment is PR #28 at branch tip
`dcefe70131df6c83a69910e2edec39086e3efa7f`. Its RFP idea and initial constants
are retained, while its unrelated merge history is excluded from this branch.
