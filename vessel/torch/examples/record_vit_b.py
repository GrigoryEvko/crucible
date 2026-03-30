#!/usr/bin/env python3
"""Record ViT-B/16 training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  ViT-B/16: 12 layers, d=768, 12 heads, ~86M params.

Output: traces/vit_b.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_vit_b.py
"""

import sys
import time

import torch

from crucible_native import CrucibleNative


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "traces/vit_b.crtrace"

    print("=" * 60)
    print("Crucible Vessel — ViT-B/16 (native C++ dispatch)")
    print("=" * 60)

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

    with CrucibleNative(verbose=True) as ctx:
        ctx.track_modules(model)

        for i in range(4):
            t0 = time.perf_counter()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.zero_grad()
            ctx.set_training_phase(ctx.PHASE_FORWARD)
            out = model(images, labels=labels)
            ctx.set_training_phase(ctx.PHASE_BACKWARD)
            out.loss.backward()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.step()
            dt = (time.perf_counter() - t0) * 1000
            print(f"  iter {i}: loss={out.loss.item():.4f} ({dt:.1f}ms) "
                  f"bg_iters={ctx.bg_iterations()} compiled={ctx.is_compiled()}")

        ok = ctx.export_trace(out_path)
        num_ops = ctx.active_num_ops()

    if ok:
        import os
        sz = os.path.getsize(out_path)
        print(f"\n  {out_path}: {sz:,} bytes, {num_ops} ops")
    print("=" * 60)


if __name__ == "__main__":
    main()
