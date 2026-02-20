#!/usr/bin/env python3
"""Record ViT-B/16 training trace → .crtrace binary for C++ benchmarking.

Runs 2 training iterations through Crucible, exports the second iteration
(steady-state, no warmup artifacts) as a binary trace file.

Usage:
    python record_vit_b.py [output_path]
"""

import sys
import time

import torch

from crucible_mode import CrucibleMode


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "vit_b.crtrace"

    print("=" * 60)
    print("Crucible Vessel — ViT-B/16 Trace Export")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")

    from transformers import ViTForImageClassification, ViTConfig

    config = ViTConfig(
        hidden_size=768,
        num_hidden_layers=12,
        num_attention_heads=12,
        intermediate_size=3072,
        image_size=224,
        patch_size=16,
        num_channels=3,
        num_labels=1000,
        hidden_dropout_prob=0.0,
        attention_probs_dropout_prob=0.0,
    )
    model = ViTForImageClassification(config)
    model.train()

    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: ViT-B/16 (12L, d=768, h=12), {n_params:,} params")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4)
    torch.manual_seed(42)
    images = torch.randn(2, 3, 224, 224)
    labels = torch.randint(0, 1000, (2,))

    with CrucibleMode(record_trace=True) as mode:
        # Iter 1: warmup (optimizer state init, different op count)
        print("\n  iter 0: warmup...")
        t0 = time.perf_counter()
        optimizer.zero_grad()
        out = model(images, labels=labels)
        out.loss.backward()
        optimizer.step()
        print(f"    {mode._op_count} ops, loss={out.loss.item():.4f}, "
              f"{1000*(time.perf_counter()-t0):.0f}ms")

        # Clear warmup trace, record only iter 2 (steady-state)
        mode.clear_trace()

        print("  iter 1: recording...")
        t0 = time.perf_counter()
        optimizer.zero_grad()
        out = model(images, labels=labels)
        out.loss.backward()
        optimizer.step()
        ops_iter2 = mode._op_count  # total across both iters
        print(f"    loss={out.loss.item():.4f}, "
              f"{1000*(time.perf_counter()-t0):.0f}ms")

        mode.export_trace(out_path)

    print(f"\n  Recorded ops:  {len(mode._trace_ops)}")
    print(f"  Recorded metas: {len(mode._trace_metas) // 144}")
    print()
    print("=" * 60)
    print("Done.")
    print("=" * 60)


if __name__ == "__main__":
    main()
