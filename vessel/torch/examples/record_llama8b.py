#!/usr/bin/env python3
"""Record Llama 3.1 8B training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  Llama 3.1 8B: 32 layers, GQA (32Q/8KV), RoPE,
SwiGLU, ~8.03B params.  Uses batch=1, seq=128 to fit in CPU RAM.

Output: traces/llama8b.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_llama8b.py
"""

import sys
import time

import torch

from crucible_native import CrucibleNative


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "traces/llama8b.crtrace"

    print("=" * 60)
    print("Crucible Vessel — Llama 3.1 8B (native C++ dispatch)")
    print("=" * 60)

    from transformers import LlamaForCausalLM, LlamaConfig

    config = LlamaConfig(
        hidden_size=4096,
        intermediate_size=14336,
        num_hidden_layers=32,
        num_attention_heads=32,
        num_key_value_heads=8,
        vocab_size=128256,
        max_position_embeddings=131072,
        rope_theta=500000.0,
        rms_norm_eps=1e-5,
        hidden_act="silu",
        tie_word_embeddings=False,
        attention_dropout=0.0,
    )
    model = LlamaForCausalLM(config)
    model.train()

    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: Llama 3.1 8B (32L, d=4096, h=32, kv=8), {n_params:,} params")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-5)
    torch.manual_seed(42)

    seq_len = 128
    input_ids = torch.randint(0, config.vocab_size, (1, seq_len))
    labels = input_ids.clone()

    with CrucibleNative(verbose=True) as ctx:
        ctx.track_modules(model)

        for i in range(4):
            t0 = time.perf_counter()
            ctx.set_training_phase(ctx.PHASE_OPTIMIZER)
            optimizer.zero_grad()
            ctx.set_training_phase(ctx.PHASE_FORWARD)
            out = model(input_ids, labels=labels)
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
