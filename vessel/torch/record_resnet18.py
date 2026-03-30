#!/usr/bin/env python3
"""Record ResNet-18 training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  Runs 4 iterations: iter 0 is warmup, iter 1-2
give the IterationDetector two boundaries, iter 3 confirms compilation.

Output: traces/resnet18.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_resnet18.py
"""

import sys
import time

import torch
import torch.nn as nn
import torchvision.models as models

from crucible_native import CrucibleNative


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "../../traces/resnet18.crtrace"

    print("=" * 60)
    print("Crucible Vessel — ResNet-18 (native C++ dispatch)")
    print("=" * 60)

    model = models.resnet18(weights=None, num_classes=1000)
    model.train()
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: ResNet-18, {n_params:,} params")

    optimizer = torch.optim.SGD(model.parameters(), lr=0.1, momentum=0.9)
    criterion = nn.CrossEntropyLoss()
    torch.manual_seed(42)
    images = torch.randn(4, 3, 224, 224)
    labels = torch.randint(0, 1000, (4,))

    with CrucibleNative(verbose=True) as ctx:
        ctx.track_modules(model)

        for i in range(4):
            t0 = time.perf_counter()
            optimizer.zero_grad()
            out = model(images)
            loss = criterion(out, labels)
            loss.backward()
            optimizer.step()
            dt = (time.perf_counter() - t0) * 1000
            print(f"  iter {i}: loss={loss.item():.4f} ({dt:.1f}ms) "
                  f"bg_iters={ctx.bg_iterations()} compiled={ctx.is_compiled()}")

        ok = ctx.export_trace(out_path)

    if ok:
        import os
        sz = os.path.getsize(out_path)
        print(f"\n  {out_path}: {sz:,} bytes, {ctx.active_num_ops()} ops")
    print("=" * 60)


if __name__ == "__main__":
    main()
