#!/usr/bin/env python3
"""Record SD 1.5 UNet training trace → .crtrace binary for C++ benchmarking.

Runs 2 training iterations through Crucible, exports the second iteration
(steady-state, no warmup artifacts) as a binary trace file.

SD 1.5 UNet: ~860M params, takes (latent, timestep, text_embed) inputs.

Usage:
    python record_sd15_unet.py [output_path]
"""

import sys
import time

import torch

from crucible_mode import CrucibleMode


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "sd15_unet.crtrace"

    print("=" * 60)
    print("Crucible Vessel — SD 1.5 UNet Trace Export")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")

    from diffusers import UNet2DConditionModel

    # SD 1.5 UNet config (exact match to runwayml/stable-diffusion-v1-5)
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

    # Synthetic inputs matching SD 1.5 latent space
    batch = 2
    latents = torch.randn(batch, 4, 64, 64)
    timesteps = torch.randint(0, 1000, (batch,))
    encoder_hidden_states = torch.randn(batch, 77, 768)  # CLIP text embeddings
    # Target: predict noise (same shape as latents)
    target_noise = torch.randn_like(latents)

    with CrucibleMode(record_trace=True) as mode:
        mode.track_modules(model)
        # Iter 0: warmup (optimizer state init)
        print("\n  iter 0: warmup...")
        t0 = time.perf_counter()
        optimizer.zero_grad()
        pred = model(latents, timesteps, encoder_hidden_states).sample
        loss = torch.nn.functional.mse_loss(pred, target_noise)
        loss.backward()
        optimizer.step()
        print(f"    {mode._op_count} ops, loss={loss.item():.4f}, "
              f"{1000*(time.perf_counter()-t0):.0f}ms")

        # Clear warmup trace, record only iter 1 (steady-state)
        mode.clear_trace()

        print("  iter 1: recording...")
        t0 = time.perf_counter()
        optimizer.zero_grad()
        pred = model(latents, timesteps, encoder_hidden_states).sample
        loss = torch.nn.functional.mse_loss(pred, target_noise)
        loss.backward()
        optimizer.step()
        print(f"    loss={loss.item():.4f}, "
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
