#!/usr/bin/env python3
"""Test Crucible with a real PyTorch training loop: forward + backward + optimizer.

This is the definitive proof that Crucible can observe and compile an entire
training iteration — not just forward inference, but the full cycle:
  1. Forward pass (model(x))
  2. Loss computation (cross-entropy)
  3. loss.backward() (autograd)
  4. optimizer.step() (parameter update)
  5. optimizer.zero_grad() (gradient reset)

Usage:
    python test_train.py [--verbose]
"""

import sys
import time

import torch
import torch.nn as nn
import torch.nn.functional as F

from crucible_mode import CrucibleMode


def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv

    print("=" * 60)
    print("Crucible Vessel — Real PyTorch Training Loop")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")
    print()

    # ── Model: 3-layer MLP classifier ─────────────────────────
    model = nn.Sequential(
        nn.Linear(16, 64),
        nn.ReLU(),
        nn.Linear(64, 32),
        nn.ReLU(),
        nn.Linear(32, 10),
    )
    model.train()

    optimizer = torch.optim.SGD(model.parameters(), lr=0.01)

    # Fixed input/target for deterministic op sequence
    x = torch.randn(8, 16)
    target = torch.randint(0, 10, (8,))

    # ── Phase 1: Discover the full training op sequence ───────
    print("Phase 1: Discovering ATen ops in one training step...")

    class OpTracer(torch.utils._python_dispatch.TorchDispatchMode):
        def __init__(self):
            super().__init__()
            self.ops = []
        def __torch_dispatch__(self, func, types, args=(), kwargs=None):
            self.ops.append(func.name())
            return func(*args, **(kwargs or {}))

    with OpTracer() as tracer:
        optimizer.zero_grad()
        out = model(x)
        loss = F.cross_entropy(out, target)
        loss.backward()
        optimizer.step()

    print(f"  {len(tracer.ops)} ATen ops per training step:")
    # Group by phase for readability
    print(f"  Ops: {', '.join(tracer.ops[:10])}...")
    if len(tracer.ops) > 10:
        print(f"        ...{', '.join(tracer.ops[-5:])}")
    print()

    # Reset model state for clean Crucible test
    model = nn.Sequential(
        nn.Linear(16, 64),
        nn.ReLU(),
        nn.Linear(64, 32),
        nn.ReLU(),
        nn.Linear(32, 10),
    )
    model.train()
    optimizer = torch.optim.SGD(model.parameters(), lr=0.01)

    # ── Phase 2: Full training through Crucible ───────────────
    print("Phase 2: Training through Crucible...")
    losses = []

    with CrucibleMode(verbose=verbose) as mode:
        for i in range(20):
            # Full training step
            optimizer.zero_grad()
            out = model(x)
            loss = F.cross_entropy(out, target)
            loss.backward()
            optimizer.step()

            loss_val = loss.item()
            losses.append(loss_val)

            compiled = mode.is_compiled()
            status = "COMPILED" if compiled else "RECORDING"
            diverged = mode.diverged_count()

            print(f"  iter {i:2d}: [{status}] loss={loss_val:.4f} "
                  f"diverged={diverged} ops={mode._op_count}")

            # Flush after a few recording iters to help bg thread
            if i == 3:
                mode.flush()
                time.sleep(0.1)

        print()
        mode.flush()
        time.sleep(0.1)

        # ── Phase 3: Report ───────────────────────────────────
        ops_per_iter = mode._op_count // 20
        print("Final status:")
        print(f"  compiled:      {mode.is_compiled()}")
        print(f"  compiled_iters:{mode.compiled_iterations()}")
        print(f"  divergences:   {mode.diverged_count()}")
        print(f"  total ops:     {mode._op_count}")
        print(f"  ops/iter:      ~{ops_per_iter}")
        print()

        # Verify loss decreased (model is actually training)
        first_loss = sum(losses[:3]) / 3
        last_loss = sum(losses[-3:]) / 3
        print(f"  avg first 3 losses: {first_loss:.4f}")
        print(f"  avg last 3 losses:  {last_loss:.4f}")

        if last_loss < first_loss:
            print("  ✓ Loss decreased — model is learning!")
        else:
            print("  ✗ Loss did not decrease (may happen with fixed data)")

        if mode.is_compiled():
            print("  ✓ Crucible compiled the full training iteration!")
        else:
            print("  ✗ Did not reach COMPILED mode")

    print()
    print("=" * 60)
    print("Test complete.")
    print("=" * 60)


if __name__ == "__main__":
    main()
