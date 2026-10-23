# 4PC Teams King Evaluation

This branch adds original HCE heuristics inspired by Chess Programming Wiki
[King Safety], [King Centralization], [Mate at a Glance], [Pin], [X-ray]
and [Castling Rights], adapted to Red+Yellow vs Blue+Green and the 14x14 board.

These are evaluation weights, not illegal move pruning or forced mate detection.

## Features
- **Middlegame shield**: three oriented forward files/ranks from each king, allowing nearby allied pawn shelter and penalizing gaps.
- **Attacked king zone**: reused team attack maps from the existing mobility stage, covering both enemy colors.
- **Enemy pawn storm**: nearby enemy pawn advances toward a home king.
- **Mate at a glance**: a *king-danger proxy* for an already attacked king with two or fewer unattacked empty adjacent escape squares. This is not a legal mate detector and **never outputs a terminal mate score**.
- **Endgame centralization**: tapered central proximity reward based on surviving enemy non-pawn material.
- **Pawnless wing**: endgame penalty for a wing king when neither teammate has pawns on that wing, with seat-orientated transverse axes.
- **Absolute pins**: a friendly piece shielding a king from an enemy bishop/rook/queen on a real uninterrupted 4PC ray.
- **X-ray pressure**: an allied piece shielding a queen from an enemy slider. No invalid cross-corner rays.
- **Castling rights**: middlegame bonus only when a configured lane has the correct live right bit and original king/rook in place. Does not assume that immediate castling is currently legal.

The middlegame fraction is based on remaining **enemy** non-pawn material;
the endgame fraction is complementary. No change to the actual rules, moves,
mate scores, TT, or search. NNUE continues to bypass HCE as before.

## New UCI parameters

| Option | Default |
|---|---:|
| EvalKingShieldPenalty | 10 |
| EvalKingZoneAttackPenalty | 6 |
| EvalKingPawnStormPenalty | 5 |
| EvalKingMateThreatPenalty | 30 |
| EvalKingCentralizationBonus | 4 |
| EvalKingPawnlessWingPenalty | 14 |
| EvalKingPinPenalty | 14 |
| EvalKingXrayPenalty | 8 |
| EvalKingCastlingRightsBonus | 8 |

Each can be set to 0 to disable a feature and tuned separately through
Tunex. These defaults are *experimental* and require independent SPRT.
All previously tuned Search/Eval parameters remain intact.

HCE king terms are deliberately computed after the shared mobility/attack
stage. Reuse of existing attack maps limits extra full-board scans, but rays,
phase calculations and pawn scans cost NPS. Profile on real 4PC positions.

## Validation

The king-feature regression test covers: all nine parameter boundaries and
UCI declarations; material-phase king shield and pawn storm; threat proxy and
zone attacks; endgame centralization and wing pawns; pins and queen X-rays;
castling right preconditions; and team-relative sign reversal.

Run CMake/CTest on Linux and Windows before testing Elo.
Compare both **NPS and paired WDL/SPRT** against \`main\`, ideally varying
one new king term at a time.

[King Safety]: https://chessprogramming.org/King_Safety
[King Centralization]: https://chessprogramming.org/King_Centralization
[Mate at a Glance]: https://chessprogramming.org/Mate_at_a_Glance
[Pin]: https://chessprogramming.org/Pin
[X-ray]: https://chessprogramming.org/X-ray
[Castling Rights]: https://chessprogramming.org/Castling_Rights
