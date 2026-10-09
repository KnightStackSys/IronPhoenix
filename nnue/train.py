"""Train PhoenixNet from pre-extracted sparse feature samples.

Expected .pt dataset format is a dict with:
  own_indices: LongTensor [N, K] padded with -1
  partner_indices: LongTensor [N, K] padded with -1
  targets: FloatTensor [N] in logit space or centipawn-scaled values

This script converts each padded row into EmbeddingBag flat indices/offsets.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset, random_split

from model import PhoenixNet


def pack_embedding_bag(rows: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    chunks: list[torch.Tensor] = []
    offsets: list[int] = []
    cursor = 0

    for row in rows:
        values = row[row.ge(0)].long()
        offsets.append(cursor)
        chunks.append(values)
        cursor += values.numel()

    flat = torch.cat(chunks) if chunks else torch.empty(0, dtype=torch.long)
    return flat, torch.tensor(offsets, dtype=torch.long)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-fraction", type=float, default=0.05)
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("-o", "--output", type=Path, default=Path("phoenixnet-v1.pt"))
    args = parser.parse_args()

    data = torch.load(args.dataset, map_location="cpu")
    own = data["own_indices"].long()
    partner = data["partner_indices"].long()
    targets = data["targets"].float()

    dataset = TensorDataset(own, partner, targets)
    val_size = max(1, int(len(dataset) * args.val_fraction))
    train_size = len(dataset) - val_size
    train_set, val_set = random_split(dataset, [train_size, val_size])

    train_loader = DataLoader(train_set, batch_size=args.batch_size, shuffle=True)
    val_loader = DataLoader(val_set, batch_size=args.batch_size, shuffle=False)

    device = torch.device(args.device)
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

            own_idx = own_idx.to(device)
            own_offsets = own_offsets.to(device)
            partner_idx = partner_idx.to(device)
            partner_offsets = partner_offsets.to(device)
            target = target.to(device)

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
                    own_idx.to(device),
                    own_offsets.to(device),
                    partner_idx.to(device),
                    partner_offsets.to(device),
                )
                target = target.to(device)
                loss = loss_fn(prediction, target)
                val_loss += loss.item() * target.numel()
                val_items += target.numel()

        train_mean = train_loss / max(1, train_items)
        val_mean = val_loss / max(1, val_items)
        print(f"epoch={epoch:03d} train={train_mean:.6f} val={val_mean:.6f}")

        if val_mean < best_val:
            best_val = val_mean
            torch.save({"model": model.state_dict(), "val_loss": best_val, "epoch": epoch}, args.output)
            print(f"saved {args.output}")


if __name__ == "__main__":
    main()
