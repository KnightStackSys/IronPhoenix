# Queen Infiltration (Teams handcrafted evaluation)

`queen-infiltration-v2` starts from current `main` and ports the old
four-orientation queen-infiltration feature without discarding newer
material, mobility, attacked-piece evaluation, search or web GUI code.

A queen earns a **small positional bonus** when:

- It has left its own back/home camp.
- It has penetrated at least two squares past the central boundary
  towards an **alive enemy** king's side of the four-player cross board.
- It is within Chebyshev distance six of that enemy king.
- Its square is **not attacked by the opposing team**.

Each enemy color is evaluated using its own home-edge direction: RED
from the bottom, BLUE from the left, YELLOW from the top, GREEN from the
right. Teammates' kings do not count as infiltration targets.

For each eligible enemy king:

```text
bonus = (depth_into_enemy_half - 1) * (7 - chebyshev_distance) * EvalQueenInfiltration
```

The default `EvalQueenInfiltration` is **2**, configurable through UCI
from **0** (disabled) to **20**. Example:

```text
setoption name EvalQueenInfiltration value 0
```

The attack-map safety check is made only for queens with a candidate
infiltration bonus. This limits overhead when no enemy camp is reached.

**HCE only:** a loaded NNUE remains authoritative and is unchanged.
The positional bonus is experimental, can influence RFP/NMP static
evaluations, and may or may not improve Elo. Test against `main`
with a matched build and a paired four-player opening suite.

CTest: `ironphoenix_queen_infiltration`.
