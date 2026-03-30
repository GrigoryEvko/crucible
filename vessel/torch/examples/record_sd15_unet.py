#!/usr/bin/env python3
"""Record SD 1.5 UNet training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  SD 1.5 UNet: ~860M params, cross-attention,
U-shaped encoder/decoder with skip connections.

Output: traces/sd15_unet.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_sd15_unet.py
"""

import sys
import time

import torch

from crucible_native import CrucibleNative


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "traces/sd15_unet.crtrace"

    print("=" * 60)
    print("Crucible Vessel — SD 1.5 UNet (native C++ dispatch)")
    print("=" * 60)

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
    print(f"Model: SD 1.5 UNet, {n_params:,} params")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-4)
    torch.manual_seed(42)

    batch = 2
    latents = torch.randn(batch, 4, 64, 64)
    timesteps = torch.randint(0, 1000, (batch,))
    encoder_hidden_states = torch.randn(batch, 77, 768)
    target_noise = torch.randn_like(latents)

    with CrucibleNative(verbose=True) as ctx:
        ctx.track_modules(model)

        for i in range(4):
            t0 = time.perf_counter()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.zero_grad()
            ctx.set_training_phase(ctx.PHASE_FORWARD)
            pred = model(latents, timesteps, encoder_hidden_states).sample
            loss = torch.nn.functional.mse_loss(pred, target_noise)
            ctx.set_training_phase(ctx.PHASE_BACKWARD)
            loss.backward()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.step()
            dt = (time.perf_counter() - t0) * 1000
            print(f"  iter {i}: loss={loss.item():.4f} ({dt:.1f}ms) "
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
