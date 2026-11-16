#!/usr/bin/env python3
"""Each overload of the profiler namespace falls through the Crucible key, so no profiler op enters a region.

PyTorch runs the ops of the namespace profiler for its own bookkeeping: the
record_function scope of an optimizer step runs _record_function_enter_new
and _record_function_exit._RecordFunction.  They compute nothing of the model,
so the dispatch library registers a fallthrough kernel for them, and the
recorder never sees them.  A fallthrough kernel covers one overload, so each
overload needs its own.  The check asks the dispatcher of PyTorch, for each
overload of each op of the namespace, if its kernel of the Crucible key is a
fallthrough.  An overload that PyTorch adds later fails the check until the
library registers it.

Usage:
    python test_profiler_ops.py

Exit 0 when each overload falls through, 1 otherwise.
"""

from __future__ import annotations

import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import CrucibleNative  # noqa: E402

torch.set_num_threads(1)
torch.set_num_interop_threads(1)


def profiler_overloads() -> list[str]:
    """Return the qualified name of each overload of each op of the namespace profiler."""
    names = []
    for op_name in dir(torch.ops.profiler):
        packet = getattr(torch.ops.profiler, op_name, None)
        if packet is None or not hasattr(packet, "overloads"):
            continue
        for overload in packet.overloads():
            suffix = "" if overload == "default" else f".{overload}"
            names.append(f"profiler::{op_name}{suffix}")
    return names


def main() -> int:
    """Load the dispatch library, then check each overload."""
    problems = []
    # The attach loads the dispatch library, which registers its kernels.
    with CrucibleNative():
        overloads = profiler_overloads()
        if not overloads:
            problems.append("the namespace profiler has no op, so the check checks nothing")
        key = torch._C.DispatchKey.Crucible
        for name in overloads:
            # An overload with no kernel of the key reaches the boxed fallback
            # of the library, which records it.
            if not torch._C._dispatch_has_kernel_for_dispatch_key(name, key):
                problems.append(f"{name} has no kernel of the Crucible key, so the boxed fallback records it")
            elif not torch._C._dispatch_kernel_for_dispatch_key_is_fallthrough(name, key):
                problems.append(f"{name} has a kernel of the Crucible key that is no fallthrough, so it records")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_profiler_ops: each of the {len(overloads)} overloads of the namespace profiler falls through")
    return 0


if __name__ == "__main__":
    sys.exit(main())
