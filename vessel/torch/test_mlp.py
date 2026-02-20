#!/usr/bin/env python3
"""Test Crucible's Vigil with a real PyTorch MLP forward pass.

Runs a 3-layer MLP through TorchDispatchMode, feeding every ATen op
into Vigil's recording pipeline. After 2 iterations, the background
thread should detect the iteration boundary and transition to COMPILED.

Usage:
    python test_mlp.py [--verbose]
"""

import sys
import time

import torch
import torch.nn as nn

from crucible_mode import CrucibleMode


def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv

    print("=" * 60)
    print("Crucible Vessel — Real PyTorch MLP test")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")
    print(f"Device:  CPU")
    print()

    # ── Model: 3-layer MLP ──────────────────────────────────────
    model = nn.Sequential(
        nn.Linear(16, 64),
        nn.ReLU(),
        nn.Linear(64, 32),
        nn.ReLU(),
        nn.Linear(32, 10),
    )
    model.eval()

    x = torch.randn(4, 16)

    # ── Phase 1: Discover the op sequence ───────────────────────
    print("Phase 1: Discovering ATen op sequence...")

    class OpCounter(torch.utils._python_dispatch.TorchDispatchMode):
        def __init__(self):
            super().__init__()
            self.ops = []
        def __torch_dispatch__(self, func, types, args=(), kwargs=None):
            self.ops.append(func.name())
            return func(*args, **(kwargs or {}))

    with OpCounter() as counter:
        _ = model(x)

    print(f"  {len(counter.ops)} ATen ops per forward pass:")
    for i, name in enumerate(counter.ops):
        print(f"    [{i:2d}] {name}")
    print()

    # ── Phase 2: Feed ops to Crucible via CrucibleMode ──────────
    print("Phase 2: Recording iterations through Crucible...")

    with CrucibleMode(verbose=verbose) as mode:
        # Run several iterations
        for i in range(12):
            _ = model(x)

            # Check status periodically
            compiled = mode.is_compiled()
            citers = mode.compiled_iterations()
            diverged = mode.diverged_count()

            status = "COMPILED" if compiled else "RECORDING"
            print(f"  iter {i:2d}: [{status}] "
                  f"compiled_iters={citers} diverged={diverged}")

            # After 2 recording iters, flush to help bg thread catch up
            if i == 2:
                mode.flush()
                time.sleep(0.1)  # Give bg thread time to process

        print()

        # ── Phase 3: Report ─────────────────────────────────────
        mode.flush()
        time.sleep(0.1)

        print("Final status:")
        print(f"  compiled:    {mode.is_compiled()}")
        print(f"  iterations:  {mode.compiled_iterations()}")
        print(f"  divergences: {mode.diverged_count()}")
        print(f"  total ops:   {mode._op_count}")

        # Verify we reached COMPILED mode
        if mode.is_compiled():
            print("\n  ✓ Crucible detected iteration boundary and compiled!")
        else:
            print("\n  ✗ Did not reach COMPILED mode")
            print("    (This may happen if the ATen op sequence varies "
                  "between iterations)")

    print()
    print("=" * 60)
    print("Test complete.")
    print("=" * 60)


if __name__ == "__main__":
    main()
