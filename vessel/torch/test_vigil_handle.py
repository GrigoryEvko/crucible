#!/usr/bin/env python3
"""A handle that names no live Vigil gives a typed Python error, and the process continues.

The handle of a Vigil crosses the C ABI from Python.  It is outside data, so
each C entry checks it, and a handle that names no live Vigil gives an error
status, never an end of the process.  Python raises CrucibleStaleHandleError
for that status.

The checks run in child processes, because the defects that they find end the
process:

    detached    each query of a CrucibleNative after the end of its session
                raises CrucibleStaleHandleError
    destroyed   a second destroy of one handle raises CrucibleStaleHandleError,
                and it frees nothing
    forged      a word with another generation, a bit between the index and
                the generation, another slot, or 0 raises
                CrucibleStaleHandleError, and the true handle still works
    thread      a query and a destroy on a thread that does not hold the Vigil
                raise CrucibleWrongThreadError, and the holder still destroys
    tls         a destroy while the TLS state of the thread still holds the
                handle, then one op: the op runs eagerly, and the process
                continues

A table of CRUCIBLE_VIGIL_SLOTS live Vigils needs about 12 GB, so the test
of FULL is test_vigil_handles of the C++ tests, on a table of two slots.

Usage:
    python test_vigil_handle.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
VESSEL_DIRECTORY = Path(__file__).resolve().parent

# The most seconds of one child.  A defect of the handle can make the child
# wait for a thread that a destroy already joined.
CHILD_SECONDS = 600

# The names of the errors of a handle that names no live Vigil, and of a
# handle that another thread holds.
STALE_ERROR = "CrucibleStaleHandleError"
WRONG_THREAD_ERROR = "CrucibleWrongThreadError"

# The start of each child: the import path, and one thread for each pool of
# PyTorch.  The child prints the name of the class of each error, so that it
# runs on a module with no such class too.
PRELUDE = """
import sys
sys.path.insert(0, sys.argv[1])
import torch
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

def report(name, call):
    try:
        call()
    except Exception as error:
        print(f"{name}: {type(error).__name__}", flush=True)
    else:
        print(f"{name}: no error", flush=True)
"""

# The queries of a CrucibleNative, each with no argument.  export_trace takes
# a path, and the child gives it one.
QUERIES = ("flush", "is_compiled", "compiled_iterations", "diverged_count", "bg_iterations", "ring_size",
           "metalog_size", "active_num_ops", "active_content_hash", "active_region_slots", "active_pool_bytes",
           "bound_scalars")

DETACHED = PRELUDE + f"""
from crucible_native import CrucibleNative
with CrucibleNative() as ctx:
    torch.relu(torch.ones(16) * 2.0 + 1.0)
for name in {QUERIES!r}:
    report(name, getattr(ctx, name))
report("export_trace", lambda: ctx.export_trace("unused.crtrace"))
print("the child ended", flush=True)
"""

DESTROYED = PRELUDE + """
from crucible_native import _DispatchLib, _VesselLib
dispatch = _DispatchLib()
vessel = _VesselLib()
handle = vessel.create(dispatch.eager_numerics())
vessel.destroy(handle)
report("second destroy", lambda: vessel.destroy(handle))
print("the child ended", flush=True)
"""


FORGED = PRELUDE + """
from crucible_native import _DispatchLib, _VesselLib
dispatch = _DispatchLib()
vessel = _VesselLib()
handle = vessel.create(dispatch.eager_numerics())
words = {"next generation": handle + (1 << 32), "middle bit": handle | (1 << 8), "other slot": handle ^ 1,
         "zero": 0, "each bit": (1 << 64) - 1}
for name, word in words.items():
    report(f"forged {name}", lambda: vessel.is_compiled(word))
print(f"the true handle works: {vessel.is_compiled(handle) is False}", flush=True)
vessel.destroy(handle)
print("the child ended", flush=True)
"""

THREAD = PRELUDE + """
import threading
from crucible_native import _DispatchLib, _VesselLib
dispatch = _DispatchLib()
vessel = _VesselLib()
handle = vessel.create(dispatch.eager_numerics())
def other():
    report("query on another thread", lambda: vessel.is_compiled(handle))
    report("destroy on another thread", lambda: vessel.destroy(handle))
thread = threading.Thread(target=other)
thread.start()
thread.join()
report("destroy on the holder thread", lambda: vessel.destroy(handle))
print("the child ended", flush=True)
"""

TLS = PRELUDE + """
from crucible_native import CrucibleNative
source = torch.ones(16)
with CrucibleNative() as ctx:
    torch.relu(source * 2.0 + 1.0)
    handle = ctx._handle
    ctx._vessel.destroy(handle)
    ctx._handle = 0
    result = torch.relu(source * 2.0 + 1.0)
    print(f"the op after the destroy ran: {bool((result == 3.0).all())}", flush=True)
print("the child ended", flush=True)
"""

def run_child(code: str) -> subprocess.CompletedProcess[str] | None:
    """Run CODE in a child interpreter with the vessel directory, and give its result, or None after a timeout."""
    try:
        return subprocess.run([sys.executable, "-E", "-B", "-c", code, str(VESSEL_DIRECTORY)],
                              env=dict(os.environ), capture_output=True, text=True, timeout=CHILD_SECONDS,
                              check=False)
    except subprocess.TimeoutExpired:
        return None


def check_child(problems: list[str], name: str, code: str, *markers: str) -> None:
    """Expect the child of CODE to exit with 0 and to print each of MARKERS."""
    child = run_child(code)
    if child is None:
        problems.append(f"{name}: the child did not end in {CHILD_SECONDS} s")
        return
    if child.returncode != 0:
        problems.append(f"{name}: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
    for marker in markers:
        if marker not in child.stdout:
            problems.append(f"{name}: the child does not print {marker!r}: {child.stdout!r}")


def main() -> int:
    """Run each check and report."""
    problems: list[str] = []
    query_markers = [f"{name}: {STALE_ERROR}" for name in (*QUERIES, "export_trace")]
    check_child(problems, "detached", DETACHED, *query_markers, "the child ended")
    check_child(problems, "destroyed", DESTROYED, f"second destroy: {STALE_ERROR}", "the child ended")
    forged_markers = [f"forged {name}: {STALE_ERROR}"
                      for name in ("next generation", "middle bit", "other slot", "zero", "each bit")]
    check_child(problems, "forged", FORGED, *forged_markers, "the true handle works: True", "the child ended")
    check_child(problems, "thread", THREAD, f"query on another thread: {WRONG_THREAD_ERROR}",
                f"destroy on another thread: {WRONG_THREAD_ERROR}", "destroy on the holder thread: no error",
                "the child ended")
    check_child(problems, "tls", TLS, "the op after the destroy ran: True", "the child ended")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_vigil_handle: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
