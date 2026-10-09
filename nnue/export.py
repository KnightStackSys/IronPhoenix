"""Export a trained PhoenixNet checkpoint to IronPhoenix's .nnue format."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import torch

from model import (
    FEATURE_COUNT,
    FT_SIZE,
    HIDDEN1_SIZE,
    HIDDEN2_SIZE,
    PhoenixNet,
)

MAGIC = b"IPNNUE1\0"
VERSION = 1


def _write_tensor(file, tensor: torch.Tensor) -> None:
    data = tensor.detach().cpu().contiguous().float().numpy().tobytes(order="C")
    file.write(data)


def export_network(model: PhoenixNet, output: Path, output_scale: float = 400.0) -> None:
    model.eval()

    with output.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack(
            "<IIIII f",
            VERSION,
            FEATURE_COUNT,
            FT_SIZE,
            HIDDEN1_SIZE,
            HIDDEN2_SIZE,
            output_scale,
        ))

        _write_tensor(f, model.feature_transformer.weight)
        _write_tensor(f, model.ft_bias)
        _write_tensor(f, model.hidden1.weight)
        _write_tensor(f, model.hidden1.bias)
        _write_tensor(f, model.hidden2.weight)
        _write_tensor(f, model.hidden2.bias)
        _write_tensor(f, model.output.weight.view(-1))
        _write_tensor(f, model.output.bias.view(()))


def load_checkpoint(path: Path) -> PhoenixNet:
    model = PhoenixNet()
    checkpoint = torch.load(path, map_location="cpu")

    if isinstance(checkpoint, dict) and "model" in checkpoint:
        state_dict = checkpoint["model"]
    else:
        state_dict = checkpoint

    model.load_state_dict(state_dict)
    return model


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("-o", "--output", type=Path, default=Path("ironphoenix.nnue"))
    parser.add_argument("--output-scale", type=float, default=400.0)
    args = parser.parse_args()

    model = load_checkpoint(args.checkpoint)
    export_network(model, args.output, args.output_scale)
    print(f"exported {args.output}")


if __name__ == "__main__":
    main()
