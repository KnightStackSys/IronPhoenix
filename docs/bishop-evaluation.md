# 4PC Teams bishop evaluation

Experimental, zero-by-default UCI knobs for the handcrafted evaluator (HCE).
**These terms are not used by PhoenixNet/NNUE**; set UseNNUE=false when SPSA tuning.
Scores are Red+Yellow versus Blue+Green and flipped by side-to-move **team**.

| UCI option | 4PC Teams semantics |
| --- | --- |
| EvalBadBishopPenalty | For each bishop, penalize same-colored *allied* pawns ahead of its owner's home edge, within four forward steps and four lateral cells. Capped at four; no penalty for bishops outside the chain. |
| EvalBishopPairBonus | Once per team for bishops covering both square colors, even when one belongs to each teammate. Same-colored promoted bishops do not count. |
| EvalBishopVsKnightBonus | Favor surviving allied bishops against surviving enemy knights in relatively open positions. Openness is scaled against up to 32 pawns (all four armies); bonuses are capped at two bishops and two enemy knights. |
| EvalBishopColorWeaknessPenalty | For each allied king, count up to two empty, enemy-bishop-attacked surrounding squares of a color for which *neither teammate* has a bishop and which *neither teammate* defends with a pawn. |
| EvalBishopFianchettoBonus | Per bishop two lateral steps from its own king, on its own second row/file, with advanced same-file pawn and adjacent pawn support. All four orientations supported. |
| EvalBishopReturningBonus | **Approximation**: own home-square bishop with clear departure square, after matching configured castled king/rook destinations. The evaluator cannot prove the bishop moved away and returned; original move history is not part of static position evaluation, and imported FEN-only castling geometry may lack the required information. |

All six are bounded UCI integer spin options in [0, 100], and reset to 0.
The use of zero defaults prevents unvalidated new heuristics from silently changing playing strength. Enable them individually and run independent SPRT matches before adopting tuned defaults; SPSA perturbations can otherwise spend games on noisy or unproductive parameters.

## Why this is 4PC rather than copied 8×8 evaluation

* Each individual color has its own rotated forward direction; the Red/Yellow partnership faces **opposite** home edges, as do Blue/Green.
* The board is 14×14 with blocked corners; squares are always obtained through IronPhoenix's geometry tables.
* Two teammate kings can create two distinct weaknesses. Enemy bishop pressure is measured against both.
* Promoted bishops and mixed-color teammate bishop pairs are counted without awarding a duplicate team bonus.
* The returning-bishop pattern is only a conservative geometry proxy; it does **not** infer historical moves from positions.

Reference concepts: [Bad Bishop](https://chessprogramming.org/Bad_Bishop),
[Bishop Pair](https://chessprogramming.org/Bishop_Pair),
[Bishop versus Knight](https://chessprogramming.org/Bishop_versus_Knight),
[Color Weakness](https://chessprogramming.org/Color_Weakness),
[Fianchetto](https://chessprogramming.org/Fianchetto),
[Returning Bishop](https://chessprogramming.org/Returning_Bishop).

## Performance optimization

The tuned bishop weights are nonzero by default and can make HCE leaf nodes more expensive. The evaluator collects both teams' bishop/pawn material **once**, and reuses enemy bishop attack maps rather than recalculating them for each team. Precomputed `Geometry::BishopBadPawnMask[color][square]` bitboards turn the former per-bishop, per-pawn coordinate scan into a three-word bitboard intersection and popcount. Unit tests check all 4 × 160 × 160 bishop/pawn mask combinations. Evaluation and search scores are intended to be identical to the original implementation.

NPS depends on the CPU, build mode, position, Hash, thread count, NNUE state and evaluation weights; these are **throughput optimizations, not a promise of a fixed NPS**. Compare on the same Release build and position and validate engine strength independently.

### Reproducible search throughput

Build with `-DIRONPHOENIX_BUILD_BENCHMARK=ON`, then run `ironphoenix_nps_bench 1000000` from your Release build directory. The tool searches a fixed nonterminal 4PC Teams middlegame with the branch's currently tuned HCE weights, prints the last completed search iteration's nodes/NPS, and uses a fixed node target. Compare the same executable configuration, CPU, hash and node target before/after. CI prints this diagnostic without enforcing a machine-specific NPS threshold.
