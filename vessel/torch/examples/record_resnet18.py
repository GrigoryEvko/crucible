#!/usr/bin/env python3
"""Record ResNet-18 training trace via native C++ dispatch.

Uses DispatchKey::Crucible to intercept ALL ATen ops (forward, backward,
optimizer) at ~100ns/op.  Runs 4 iterations: iter 0 is warmup, iter 1-2
give the IterationDetector two boundaries, iter 3 confirms compilation.

attach() enables the key, tracks the module hierarchy and wraps
Tensor.backward, so the loop is the loop a reader already writes.  The phase
of each operation is derived from that rather than declared.

Requires torchvision.

Output: traces/resnet18.crtrace

Usage:
    PYTHONPATH=~/Downloads/pytorch python record_resnet18.py
"""

import logging
import sys
import time
from pathlib import Path

import torch
import torch.nn as nn
import torchvision.models as models

from crucible_native import attach


log = logging.getLogger("crucible.record_resnet18")


def configure_logging() -> None:
    """Send this script's report and the recorder's log to stdout."""
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    for name in ("crucible.record_resnet18", "crucible_native"):
        logger = logging.getLogger(name)
        logger.handlers.clear()
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
        logger.propagate = False


def main() -> None:
    """Train ResNet-18 for four iterations and export the region."""
    configure_logging()
    out_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "traces/resnet18.crtrace")

    log.info("=" * 60)
    log.info("Crucible Vessel — ResNet-18 (native C++ dispatch)")
    log.info("=" * 60)

    model = models.resnet18(weights=None, num_classes=1000)
    model.train()
    n_params = sum(p.numel() for p in model.parameters())
    log.info("Model: ResNet-18, %s params", f"{n_params:,}")

    optimizer = torch.optim.SGD(model.parameters(), lr=0.1, momentum=0.9)
    criterion = nn.CrossEntropyLoss()
    torch.manual_seed(42)
    images = torch.randn(4, 3, 224, 224)
    labels = torch.randint(0, 1000, (4,))

    with attach(model, optimizer, verbose=True) as ctx:
        for i in range(4):
            t0 = time.perf_counter()
            optimizer.zero_grad()
            out = model(images)
            loss = criterion(out, labels)
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
