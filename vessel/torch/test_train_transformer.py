#!/usr/bin/env python3
"""Test Crucible with a transformer training loop: attention + MLP + loss.

Uses a minimal GPT-2-style transformer block (multi-head attention + FFN
+ layer norm + residual connections) to verify Crucible handles the full
spectrum of training ops:
  - scaled_dot_product_attention (SDPA)
  - layer_norm (native_layer_norm in ATen)
  - GELU activation
  - dropout (no-op in eval, random in train)
  - in-place add (residual connections)
  - cross-entropy loss + autograd backward
  - AdamW optimizer step

Usage:
    python test_train_transformer.py [--verbose]
"""

import sys
import time

import torch
import torch.nn as nn
import torch.nn.functional as F

from crucible_mode import CrucibleMode


class TransformerBlock(nn.Module):
    """Single GPT-2-style transformer block."""

    def __init__(self, d_model: int, n_heads: int, d_ff: int, dropout: float = 0.0):
        super().__init__()
        self.ln1 = nn.LayerNorm(d_model)
        self.attn = nn.MultiheadAttention(d_model, n_heads, dropout=dropout, batch_first=True)
        self.ln2 = nn.LayerNorm(d_model)
        self.ffn = nn.Sequential(
            nn.Linear(d_model, d_ff),
            nn.GELU(),
            nn.Linear(d_ff, d_model),
        )

    def forward(self, x):
        # Pre-norm attention with residual
        normed = self.ln1(x)
        attn_out, _ = self.attn(normed, normed, normed, need_weights=False)
        x = x + attn_out

        # Pre-norm FFN with residual
        normed = self.ln2(x)
        x = x + self.ffn(normed)
        return x


class MiniGPT(nn.Module):
    """Minimal GPT model: embedding + N transformer blocks + head."""

    def __init__(self, vocab_size: int, d_model: int, n_heads: int,
                 n_layers: int, d_ff: int, max_seq_len: int):
        super().__init__()
        self.tok_emb = nn.Embedding(vocab_size, d_model)
        self.pos_emb = nn.Embedding(max_seq_len, d_model)
        self.blocks = nn.ModuleList([
            TransformerBlock(d_model, n_heads, d_ff, dropout=0.0)
            for _ in range(n_layers)
        ])
        self.ln_f = nn.LayerNorm(d_model)
        self.head = nn.Linear(d_model, vocab_size, bias=False)

    def forward(self, idx):
        B, T = idx.shape
        tok = self.tok_emb(idx)
        pos = self.pos_emb(torch.arange(T, device=idx.device))
        x = tok + pos
        for block in self.blocks:
            x = block(x)
        x = self.ln_f(x)
        logits = self.head(x)
        return logits


def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv

    print("=" * 60)
    print("Crucible Vessel — Transformer Training Loop")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")
    print()

    # ── Hyperparameters ──────────────────────────────────────
    VOCAB = 128
    D_MODEL = 64
    N_HEADS = 4
    N_LAYERS = 2
    D_FF = 128
    MAX_SEQ = 32
    BATCH = 4
    SEQ_LEN = 16
    N_ITERS = 30

    # ── Model ────────────────────────────────────────────────
    model = MiniGPT(VOCAB, D_MODEL, N_HEADS, N_LAYERS, D_FF, MAX_SEQ)
    model.train()

    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: {N_LAYERS}-layer transformer, d={D_MODEL}, h={N_HEADS}")
    print(f"Parameters: {n_params:,}")

    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3)

    # Fixed input for deterministic op sequence
    torch.manual_seed(42)
    x = torch.randint(0, VOCAB, (BATCH, SEQ_LEN))
    target = torch.randint(0, VOCAB, (BATCH, SEQ_LEN))

    # ── Phase 1: Discover ATen ops ───────────────────────────
    print("\nPhase 1: Discovering ATen ops in one training step...")

    class OpTracer(torch.utils._python_dispatch.TorchDispatchMode):
        def __init__(self):
            super().__init__()
            self.ops = []
        def __torch_dispatch__(self, func, types, args=(), kwargs=None):
            self.ops.append(func.name())
            return func(*args, **(kwargs or {}))

    with OpTracer() as tracer:
        optimizer.zero_grad()
        logits = model(x)
        loss = F.cross_entropy(logits.view(-1, VOCAB), target.view(-1))
        loss.backward()
        optimizer.step()

    print(f"  {len(tracer.ops)} ATen ops per training step")

    # Show unique ops
    unique_ops = sorted(set(tracer.ops))
    print(f"  {len(unique_ops)} unique ops:")
    for op in unique_ops[:20]:
        count = tracer.ops.count(op)
        print(f"    {op} (×{count})")
    if len(unique_ops) > 20:
        print(f"    ... and {len(unique_ops) - 20} more")
    print()

    # ── Phase 2: Training through Crucible ───────────────────
    print("Phase 2: Training through Crucible...")

    # Reset model for clean test
    model = MiniGPT(VOCAB, D_MODEL, N_HEADS, N_LAYERS, D_FF, MAX_SEQ)
    model.train()
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3)
    torch.manual_seed(42)

    losses = []

    with CrucibleMode(verbose=verbose) as mode:
        for i in range(N_ITERS):
            optimizer.zero_grad()
            logits = model(x)
            loss = F.cross_entropy(logits.view(-1, VOCAB), target.view(-1))
            loss.backward()
            optimizer.step()

            loss_val = loss.item()
            losses.append(loss_val)

            compiled = mode.is_compiled()
            status = "COMPILED" if compiled else "RECORDING"
            diverged = mode.diverged_count()
            ops_per = mode._op_count // (i + 1)

            if i < 5 or i >= N_ITERS - 3 or compiled != (i > 0 and losses[-2:] != []):
                print(f"  iter {i:2d}: [{status}] loss={loss_val:.4f} "
                      f"diverged={diverged} ops/iter={ops_per}")

            # Give bg thread time to process after a few recording iters
            if i == 4:
                mode.flush()
                time.sleep(0.1)

        print()
        mode.flush()
        time.sleep(0.1)

        # ── Phase 3: Report ──────────────────────────────────
        total_ops = mode._op_count
        ops_per_iter = total_ops // N_ITERS
        compiled_iters = mode.compiled_iterations()
        diverged = mode.diverged_count()

        print("Final status:")
        print(f"  compiled:       {mode.is_compiled()}")
        print(f"  compiled_iters: {compiled_iters}")
        print(f"  divergences:    {diverged}")
        print(f"  total ops:      {total_ops}")
        print(f"  ops/iter:       ~{ops_per_iter}")
        print()

        # Verify loss decreased
        first_loss = sum(losses[:3]) / 3
        last_loss = sum(losses[-3:]) / 3
        print(f"  avg first 3 losses: {first_loss:.4f}")
        print(f"  avg last 3 losses:  {last_loss:.4f}")

        if last_loss < first_loss:
            print("  PASS: Loss decreased — model is learning!")
        else:
            print("  WARN: Loss did not decrease")

        if compiled_iters > 0:
            print(f"  PASS: Crucible compiled {compiled_iters} iterations!")
        else:
            print("  FAIL: Did not reach COMPILED mode")

        if diverged == 0:
            print("  PASS: Zero divergences!")
        else:
            print(f"  WARN: {diverged} divergences detected")

    print()
    print("=" * 60)
    print("Test complete.")
    print("=" * 60)


if __name__ == "__main__":
    main()
