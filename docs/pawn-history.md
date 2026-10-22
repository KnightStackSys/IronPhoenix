# Pawn-structure history in IronPhoenix

This is a **move-ordering heuristic**, not pawn evaluation or a history
restricted to pawn moves. It learns preferences for quiet moves by any
piece, conditioned on the four-player pawn structure.

- Incremental Zobrist signature includes pawn locations for **Red, Blue,
  Yellow, and Green**, alive-player mask, and ruleset. Excludes castling
  rights, en passant markers, and the side to move.
- History indexed by **pawn hash bucket + moving piece (color and type)
  + destination square**, so Red and Yellow retain separate histories.
- Bounded table: **256 pawn contexts × 32 piece codes × 160 board squares**
  with signed 16-bit entries (~2.5 MiB). Context collisions only impact
  move ordering hints, not correctness of board state or legal moves.
- Successful quiet beta cutoffs add history bonuses, while failed quiet
  moves receive negative feedback. Existing bounded history updates are
  reused. TT moves, captures and promotions retain existing precedence.
- Existing LMP/HMP use combined quiet history scores; a positive Elo or NPS
  impact is not guaranteed and must be tested against main.

## UCI tuning

    setoption name SearchPawnHistoryWeight value 50

Range **0–200%**, default **50%**. At 0 the table is neither queried nor
updated. Use 'ucinewgame' or restart the engine before comparing runs.

The incremental pawn hash updates on pawn moves, captures and promotions,
and is restored on undo. Null moves leave the hash unchanged. Test with
CTests 'ironphoenix_pawn_history' and 'ironphoenix_property' and use
matched 4PC Teams STC/LTC and an SPRT before merging.
