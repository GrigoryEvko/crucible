#!/usr/bin/env python3
"""Record Llama 3.1 8B training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  Llama 3.1 8B: 32 layers, GQA (32Q/8KV), RoPE,
SwiGLU, ~8.03B params.  Uses batch=1, seq=128 to fit in CPU RAM.

attach() enables the key, tracks the module hierarchy and wraps
Tensor.backward, so the loop is the loop a reader already writes.  The phase
of each operation is derived from that rather than declared.

Requires transformers.

Output: traces/llama8b.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_llama8b.py
"""

import logging
import sys
import time
from pathlib import Path

import torch

from crucible_native import attach


log = logging.getLogger("crucible.record_llama8b")


def configure_logging() -> None:
    """Send this script's report and the recorder's log to stdout."""
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    for name in ("crucible.record_llama8b", "crucible_native"):
        logger = logging.getLogger(name)
        logger.handlers.clear()
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
        logger.propagate = False


def main() -> None:
    """Train Llama 3.1 8B for four iterations and export the region."""
    configure_logging()
    out_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "traces/llama8b.crtrace")

    log.info("=" * 60)
    log.info("Crucible Vessel — Llama 3.1 8B (native C++ dispatch)")
    log.info("=" * 60)

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
    log.info("Model: Llama 3.1 8B (32L, d=4096, h=32, kv=8), %s params",
             f"{n_params:,}")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-5)
    torch.manual_seed(42)

    seq_len = 128
    input_ids = torch.randint(0, config.vocab_size, (1, seq_len))
    labels = input_ids.clone()

    with attach(model, optimizer, verbose=True) as ctx:
        for i in range(4):
            t0 = time.perf_counter()
            optimizer.zero_grad()
            out = model(input_ids, labels=labels)
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
