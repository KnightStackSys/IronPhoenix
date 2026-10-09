"""Train PhoenixNet from IronPhoenix sparse NNUE datasets.

Supported inputs:

1. Native IronPhoenix .ipd files produced by ironphoenix_dataset.
2. Legacy .pt dictionaries with:
     own_indices: [N, K] padded with -1
     partner_indices: [N, K] padded with -1
     targets: [N]

The native .ipd format also contains game IDs, allowing validation to split by
whole games instead of leaking neighboring positions across train/validation.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, Subset, TensorDataset, random_split

from model import FEATURE_COUNT, PhoenixNet

DATA_MAGIC = b"IPDATA1\0"
DATA_VERSION = 1
DATA_HEADER = struct.Struct("<IIIIfQQ")
RECORD_HEADER = struct.Struct("<HHfiIHBB")


def load_ipd(path: Path) -> dict[str, torch.Tensor]:
    with path.open("rb") as f:
        magic = f.read(8)
        if magic != DATA_MAGIC:
            raise ValueError(f"{path} is not an IronPhoenix IPDATA1 dataset")

        raw_header = f.read(DATA_HEADER.size)
        if len(raw_header) != DATA_HEADER.size:
            raise ValueError("truncated IPD header")

        (
            version,
            feature_count,
            max_features,
            teacher_depth,
            score_scale,
            seed,
            record_count,
        ) = DATA_HEADER.unpack(raw_header)

        if version != DATA_VERSION:
            raise ValueError(f"unsupported IPD version {version}")
        if feature_count != FEATURE_COUNT:
            raise ValueError(
                f"feature count mismatch: dataset={feature_count} model={FEATURE_COUNT}"
            )
        if max_features <= 0:
            raise ValueError("invalid max feature count")

        own = np.full((record_count, max_features), -1, dtype=np.int32)
        partner = np.full((record_count, max_features), -1, dtype=np.int32)
        targets = np.empty(record_count, dtype=np.float32)
        teacher_cp = np.empty(record_count, dtype=np.int32)
        game_ids = np.empty(record_count, dtype=np.int64)
        plies = np.empty(record_count, dtype=np.int32)
        sides = np.empty(record_count, dtype=np.int16)
        flags = np.empty(record_count, dtype=np.uint8)

        for row in range(record_count):
            raw_record = f.read(RECORD_HEADER.size)
            if len(raw_record) != RECORD_HEADER.size:
                raise ValueError(f"truncated record header at sample {row}")

            (
                own_count,
                partner_count,
                target,
                cp,
                game_id,
                ply,
                side,
                record_flags,
            ) = RECORD_HEADER.unpack(raw_record)

            if own_count > max_features or partner_count > max_features:
                raise ValueError(f"feature count exceeds header limit at sample {row}")

            total_features = own_count + partner_count
            raw_features = f.read(total_features * 2)
            if len(raw_features) != total_features * 2:
                raise ValueError(f"truncated feature payload at sample {row}")

            values = np.frombuffer(raw_features, dtype="<u2", count=total_features)
            if own_count:
                own[row, :own_count] = values[:own_count]
            if partner_count:
                partner[row, :partner_count] = values[own_count:]

            targets[row] = target
            teacher_cp[row] = cp
            game_ids[row] = game_id
            plies[row] = ply
            sides[row] = side
            flags[row] = record_flags

        trailing = f.read(1)
        if trailing:
            raise ValueError("IPD file contains trailing data after declared records")

    print(
        f"loaded {record_count:,} samples from {path} "
        f"(teacher depth={teacher_depth}, scale={score_scale:g}, seed={seed})"
    )

    return {
        "own_indices": torch.from_numpy(own),
        "partner_indices": torch.from_numpy(partner),
        "targets": torch.from_numpy(targets),
        "teacher_cp": torch.from_numpy(teacher_cp),
        "game_ids": torch.from_numpy(game_ids),
        "plies": torch.from_numpy(plies),
        "sides": torch.from_numpy(sides),
        "flags": torch.from_numpy(flags),
    }


def load_dataset(path: Path) -> dict[str, torch.Tensor]:
    if path.suffix.lower() == ".ipd":
        return load_ipd(path)

    data = torch.load(path, map_location="cpu")
    if not isinstance(data, dict):
        raise ValueError(".pt dataset must be a dictionary")

    required = {"own_indices", "partner_indices", "targets"}
    missing = required.difference(data)
    if missing:
        raise ValueError(f"dataset is missing keys: {sorted(missing)}")

    return data


def pack_embedding_bag(rows: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    """Convert padded [B, K] sparse rows into EmbeddingBag indices/offsets."""
    mask = rows.ge(0)
    counts = mask.sum(dim=1, dtype=torch.long)
    flat = rows[mask].long()

    offsets = torch.zeros(rows.shape[0], dtype=torch.long)
    if rows.shape[0] > 1:
        offsets[1:] = torch.cumsum(counts[:-1], dim=0)
    return flat, offsets


def split_dataset(
    dataset: TensorDataset,
    game_ids: torch.Tensor | None,
    val_fraction: float,
    seed: int,
) -> tuple[Subset | torch.utils.data.Dataset, Subset | torch.utils.data.Dataset]:
    if game_ids is None:
        val_size = max(1, int(len(dataset) * val_fraction))
        train_size = len(dataset) - val_size
        generator = torch.Generator().manual_seed(seed)
        return random_split(dataset, [train_size, val_size], generator=generator)

    unique_games = torch.unique(game_ids)
    if unique_games.numel() < 2:
        val_size = max(1, int(len(dataset) * val_fraction))
        train_size = len(dataset) - val_size
        generator = torch.Generator().manual_seed(seed)
        return random_split(dataset, [train_size, val_size], generator=generator)

    generator = torch.Generator().manual_seed(seed)
    permutation = torch.randperm(unique_games.numel(), generator=generator)
    unique_games = unique_games[permutation]

    val_game_count = max(1, int(unique_games.numel() * val_fraction))
    val_games = unique_games[:val_game_count]
    val_mask = torch.isin(game_ids, val_games)

    val_indices = torch.nonzero(val_mask, as_tuple=False).flatten().tolist()
    train_indices = torch.nonzero(~val_mask, as_tuple=False).flatten().tolist()

    if not train_indices or not val_indices:
        val_size = max(1, int(len(dataset) * val_fraction))
        train_size = len(dataset) - val_size
        return random_split(dataset, [train_size, val_size], generator=generator)

    print(
        f"game split: {unique_games.numel():,} games, "
        f"{len(train_indices):,} train positions, {len(val_indices):,} validation positions"
    )
    return Subset(dataset, train_indices), Subset(dataset, val_indices)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-fraction", type=float, default=0.05)
    parser.add_argument("--split-seed", type=int, default=20261009)
    parser.add_argument("--workers", type=int, default=0, help="DataLoader worker processes")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("-o", "--output", type=Path, default=Path("phoenixnet-v1.pt"))
    args = parser.parse_args()

    if not (0.0 < args.val_fraction < 1.0):
        raise ValueError("--val-fraction must be between 0 and 1")

    data = load_dataset(args.dataset)
    own = data["own_indices"]
    partner = data["partner_indices"]
    targets = data["targets"].float()

    if own.ndim != 2 or partner.ndim != 2 or targets.ndim != 1:
        raise ValueError("dataset tensor shapes are invalid")
    if own.shape[0] != partner.shape[0] or own.shape[0] != targets.shape[0]:
        raise ValueError("dataset tensors have different sample counts")
    if own.shape[0] < 2:
        raise ValueError("dataset needs at least two samples")

    dataset = TensorDataset(own, partner, targets)
    game_ids = data.get("game_ids")
    if game_ids is not None:
        game_ids = game_ids.long()

    train_set, val_set = split_dataset(
        dataset,
        game_ids,
        args.val_fraction,
        args.split_seed,
    )

    use_pin_memory = args.device.startswith("cuda")
    train_loader = DataLoader(
        train_set,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.workers,
        pin_memory=use_pin_memory,
    )
    val_loader = DataLoader(
        val_set,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.workers,
        pin_memory=use_pin_memory,
    )

    device = torch.device(args.device)
    print(f"training on {device}")

    model = PhoenixNet().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr)
    loss_fn = nn.SmoothL1Loss()

    best_val = float("inf")

    for epoch in range(1, args.epochs + 1):
        model.train()
        train_loss = 0.0
        train_items = 0

        for own_rows, partner_rows, target in train_loader:
            own_idx, own_offsets = pack_embedding_bag(own_rows)
            partner_idx, partner_offsets = pack_embedding_bag(partner_rows)

            own_idx = own_idx.to(device, non_blocking=True)
            own_offsets = own_offsets.to(device, non_blocking=True)
            partner_idx = partner_idx.to(device, non_blocking=True)
            partner_offsets = partner_offsets.to(device, non_blocking=True)
            target = target.to(device, non_blocking=True)

            optimizer.zero_grad(set_to_none=True)
            prediction = model(own_idx, own_offsets, partner_idx, partner_offsets)
            loss = loss_fn(prediction, target)
            loss.backward()
            optimizer.step()

            train_loss += loss.item() * target.numel()
            train_items += target.numel()

        model.eval()
        val_loss = 0.0
        val_items = 0
        with torch.no_grad():
            for own_rows, partner_rows, target in val_loader:
                own_idx, own_offsets = pack_embedding_bag(own_rows)
                partner_idx, partner_offsets = pack_embedding_bag(partner_rows)

                prediction = model(
                    own_idx.to(device, non_blocking=True),
                    own_offsets.to(device, non_blocking=True),
                    partner_idx.to(device, non_blocking=True),
                    partner_offsets.to(device, non_blocking=True),
                )
                target = target.to(device, non_blocking=True)
                loss = loss_fn(prediction, target)
                val_loss += loss.item() * target.numel()
                val_items += target.numel()

        train_mean = train_loss / max(1, train_items)
        val_mean = val_loss / max(1, val_items)
        print(f"epoch={epoch:03d} train={train_mean:.6f} val={val_mean:.6f}")

        if val_mean < best_val:
            best_val = val_mean
            torch.save(
                {
                    "model": model.state_dict(),
                    "val_loss": best_val,
                    "epoch": epoch,
                    "dataset": str(args.dataset),
                },
                args.output,
            )
            print(f"saved {args.output}")


if __name__ == "__main__":
    main()
