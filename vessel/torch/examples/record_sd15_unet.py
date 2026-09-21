#!/usr/bin/env python3
"""Record SD 1.5 UNet training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  SD 1.5 UNet: ~860M params, cross-attention,
U-shaped encoder/decoder with skip connections.

attach() enables the key, tracks the module hierarchy and wraps
Tensor.backward, so the loop is the loop a reader already writes.  The phase
of each operation is derived from that rather than declared.

Requires diffusers.

Output: traces/sd15_unet.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_sd15_unet.py
"""

import logging
import sys
import time
from pathlib import Path

import torch

from crucible_native import attach


log = logging.getLogger("crucible.record_sd15_unet")


def configure_logging() -> None:
    """Send this script's report and the recorder's log to stdout."""
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    for name in ("crucible.record_sd15_unet", "crucible_native"):
        logger = logging.getLogger(name)
        logger.handlers.clear()
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
        logger.propagate = False


def main() -> None:
    """Train the SD 1.5 UNet for four iterations and export the region."""
    configure_logging()
    out_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "traces/sd15_unet.crtrace")

    log.info("=" * 60)
    log.info("Crucible Vessel — SD 1.5 UNet (native C++ dispatch)")
    log.info("=" * 60)

    from diffusers import UNet2DConditionModel

    model = UNet2DConditionModel(
        sample_size=64,
        in_channels=4,
        out_channels=4,
        center_input_sample=False,
        flip_sin_to_cos=True,
        freq_shift=0,
        down_block_types=(
            "CrossAttnDownBlock2D",
            "CrossAttnDownBlock2D",
            "CrossAttnDownBlock2D",
            "DownBlock2D",
        ),
        up_block_types=(
            "UpBlock2D",
            "CrossAttnUpBlock2D",
            "CrossAttnUpBlock2D",
            "CrossAttnUpBlock2D",
        ),
        block_out_channels=(320, 640, 1280, 1280),
        layers_per_block=2,
        downsample_padding=1,
        mid_block_scale_factor=1,
        act_fn="silu",
        norm_num_groups=32,
        norm_eps=1e-5,
        cross_attention_dim=768,
        attention_head_dim=8,
    )
    model.train()

    n_params = sum(p.numel() for p in model.parameters())
    log.info("Model: SD 1.5 UNet, %s params", f"{n_params:,}")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4)
    torch.manual_seed(42)

    batch = 2
    latents = torch.randn(batch, 4, 64, 64)
    timesteps = torch.randint(0, 1000, (batch,))
    encoder_hidden_states = torch.randn(batch, 77, 768)
    target_noise = torch.randn_like(latents)

    with attach(model, optimizer, verbose=True) as ctx:
        for i in range(4):
            t0 = time.perf_counter()
            optimizer.zero_grad()
            pred = model(latents, timesteps, encoder_hidden_states).sample
            loss = torch.nn.functional.mse_loss(pred, target_noise)
            loss.backward()
            optimizer.step()
            dt = (time.perf_counter() - t0) * 1000
            log.info("  iter %d: loss=%.4f (%.1fms) bg_iters=%d compiled=%s",
                     i, loss.item(), dt, ctx.bg_iterations(), ctx.is_compiled())

        ok = ctx.export_trace(str(out_path))
        num_ops = ctx.active_num_ops()

    if ok:
        log.info("")
        log.info("  %s: %s bytes, %d ops", out_path,
                 f"{out_path.stat().st_size:,}", num_ops)
    log.info("=" * 60)


if __name__ == "__main__":
    main()
