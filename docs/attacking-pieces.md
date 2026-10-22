<a id="attacking-piece-evaluation-for-ironphoenix-teams-hce"></a>

# <img src="https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/swords.svg" alt="" width="20" height="20"> Attacking-piece evaluation for IronPhoenix Teams HCE

This experiment adds a small **direct-pressure bonus against enemy pieces** to
the existing handcrafted material and mobility evaluation.

The two teammates' attack maps are united before evaluating threats. A target
is counted at most once per TEAM, even when multiple allied pieces attack it.
Pawn capture squares count as attacks, not mobility. King attacks and the king
as a target are excluded; mate/check assessment remains in search.

| Attacked enemy piece | Default bonus (cp) |
|---|---:|
| Pawn | 4 |
| Knight or bishop | 10 |
| Rook | 16 |
| Queen | 28 |

The UCI tuning option `EvalAttackingPiecesWeight` ranges from 0–300.
`100` is the default percentage, `0` disables the bonus for testing:

```text
setoption name EvalAttackingPiecesWeight value 0
```

This is **geometric pressure**, not proof of a free winning capture. Checks,
pins, defenders, and SEE are intentionally not fully resolved here to avoid
making every static evaluation dramatically more expensive.

**Only HCE uses this feature.** If a PhoenixNet NNUE is loaded, its output
remains unchanged rather than mixing differently calibrated scores. Threat
bonuses can influence static-eval decisions in RFP/NMP; benchmark NPS and
run an SPRT before assuming a strength increase.

Tests: `ironphoenix_attacking_pieces`.
