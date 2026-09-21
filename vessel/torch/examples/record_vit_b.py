#!/usr/bin/env python3
"""Record ViT-B/16 training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  ViT-B/16: 12 layers, d=768, 12 heads, ~86M params.

attach() enables the key, tracks the module hierarchy and wraps
Tensor.backward, so the loop is the loop a reader already writes.  The phase
of each operation is derived from that rather than declared.

Requires transformers.

Output: traces/vit_b.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_vit_b.py
"""

import logging
import sys
import time
from pathlib import Path

import torch

from crucible_native import attach


log = logging.getLogger("crucible.record_vit_b")


def configure_logging() -> None:
    """Send this script's report and the recorder's log to stdout."""
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    for name in ("crucible.record_vit_b", "crucible_native"):
        logger = logging.getLogger(name)
        logger.handlers.clear()
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
        logger.propagate = False


def main() -> None:
    """Train ViT-B/16 for four iterations and export the region."""
    configure_logging()
    out_path = Path(sys.argv[1] if len(sys.argv) > 1 else "traces/vit_b.crtrace")

    log.info("=" * 60)
    log.info("Crucible Vessel — ViT-B/16 (native C++ dispatch)")
    log.info("=" * 60)

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
    log.info("Model: ViT-B/16 (12L, d=768, h=12), %s params", f"{n_params:,}")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4)
    torch.manual_seed(42)
    images = torch.randn(2, 3, 224, 224)
    labels = torch.randint(0, 1000, (2,))

    with attach(model, optimizer, verbose=True) as ctx:
        for i in range(4):
            t0 = time.perf_counter()
            optimizer.zero_grad()
            out = model(images, labels=labels)
            out.loss.backward()
            optimizer.step()
            dt = (time.perf_counter() - t0) * 1000
            log.info("  iter %d: loss=%.4f (%.1fms) bg_iters=%d compiled=%s",
                     i, out.loss.item(), dt, ctx.bg_iterations(),
                     ctx.is_compiled())

        ok = ctx.export_trace(str(out_path))
        num_ops = ctx.active_num_ops()

    if ok:
        log.info("")
        log.info("  %s: %s bytes, %d ops", out_path,
                 f"{out_path.stat().st_size:,}", num_ops)
    log.info("=" * 60)


if __name__ == "__main__":
    main()
