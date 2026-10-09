"""PhoenixNet v1 training model for IronPhoenix 4-player Teams chess."""

from __future__ import annotations

import torch
from torch import Tensor, nn

KING_BUCKETS = 17
RELATIVE_COLORS = 4
PIECE_TYPES = 6
SQUARES = 160
FEATURE_COUNT = KING_BUCKETS * RELATIVE_COLORS * PIECE_TYPES * SQUARES
FT_SIZE = 128
HIDDEN1_SIZE = 32
HIDDEN2_SIZE = 32


class PhoenixNet(nn.Module):
    """Dual-allied-king sparse NNUE network.

    Each position supplies two sparse feature bags from the side-to-move
    perspective: one anchored on its own king and one anchored on its partner's
    king. The embedding/feature-transformer weights are shared by both streams.
    """

    def __init__(self) -> None:
        super().__init__()

        self.feature_transformer = nn.EmbeddingBag(
            FEATURE_COUNT,
            FT_SIZE,
            mode="sum",
            include_last_offset=False,
        )
        self.ft_bias = nn.Parameter(torch.zeros(FT_SIZE))

        self.hidden1 = nn.Linear(FT_SIZE * 2, HIDDEN1_SIZE)
        self.hidden2 = nn.Linear(HIDDEN1_SIZE, HIDDEN2_SIZE)
        self.output = nn.Linear(HIDDEN2_SIZE, 1)

        self.reset_parameters()

    def reset_parameters(self) -> None:
        nn.init.normal_(self.feature_transformer.weight, mean=0.0, std=0.01)
        nn.init.zeros_(self.ft_bias)
        nn.init.xavier_uniform_(self.hidden1.weight)
        nn.init.zeros_(self.hidden1.bias)
        nn.init.xavier_uniform_(self.hidden2.weight)
        nn.init.zeros_(self.hidden2.bias)
        nn.init.xavier_uniform_(self.output.weight)
        nn.init.zeros_(self.output.bias)

    @staticmethod
    def crelu(x: Tensor) -> Tensor:
        return torch.clamp(x, 0.0, 1.0)

    def _transform(self, indices: Tensor, offsets: Tensor) -> Tensor:
        return self.crelu(self.feature_transformer(indices, offsets) + self.ft_bias)

    def forward(
        self,
        own_indices: Tensor,
        own_offsets: Tensor,
        partner_indices: Tensor,
        partner_offsets: Tensor,
    ) -> Tensor:
        own = self._transform(own_indices, own_offsets)
        partner = self._transform(partner_indices, partner_offsets)

        x = torch.cat((own, partner), dim=1)
        x = self.crelu(self.hidden1(x))
        x = self.crelu(self.hidden2(x))
        return self.output(x).squeeze(1)
