# Team bishop pair

The handcrafted evaluator awards one bonus to Red/Yellow or Blue/Green when
that team's bishops occupy both checkerboard colors. Either player can supply
either color. Two or more bishops confined to one square color receive no
pair bonus. Multiple complete pairs still earn only one team bonus.

Square color uses absolute file-plus-rank parity on the 14x14 cross board,
not the engine's dense square index. The team bonus enters the existing
team-score difference before conversion to the moving team's perspective.

`EvalBishopPairBonus` is a UCI tuning option, initially 40 evaluation units,
with range 0–1000. Set it to 0 for an otherwise identical baseline. ResetTuning
restores 40. This is an initial experiment; paired opening/seat-swapped games
and strength testing are required to establish or tune a playing-strength gain.

The term belongs to handcrafted evaluation. A loaded NNUE network retains its
existing independent evaluation path, avoiding an untrained extra correction.
