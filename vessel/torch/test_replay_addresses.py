#!/usr/bin/env python3
"""A read of an address of the replay while the Vigil records gives a typed Python error, and the process continues.

crucible_output_ptr and crucible_input_ptr give the address of a slot of the
current op of a compiled replay.  A Vigil that records has no such address.
The call comes from Python, which is outside the program, so the entry gives
the status NOT_REPLAYING and writes nothing.  Python raises
CrucibleNotReplayingError for that status.

The check runs in a child process, because the defect that it finds ends the
process:

    record    output_ptr and input_ptr of a session that records each raise
              CrucibleNotReplayingError, and the session then replays its loop

Usage:
    python test_replay_addresses.py

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

# The most seconds of the child.
CHILD_SECONDS = 600

# The name of the error of a read of an address while the Vigil records.
NOT_REPLAYING_ERROR = "CrucibleNotReplayingError"

# The child prints the name of the class of each error, so that it runs on a
# module with no such class too.  The loop flushes after each iteration, so its
# replay does not depend on the speed of the background thread.
RECORD = """
import sys
sys.path.insert(0, sys.argv[1])
import torch
torch.set_num_threads(1)
torch.set_num_interop_threads(1)
from crucible_native import CrucibleNative

def report(name, call):
    try:
        call()
    except Exception as error:
        print(f"{name}: {type(error).__name__}", flush=True)
    else:
        print(f"{name}: no error", flush=True)

with CrucibleNative() as ctx:
    vessel = ctx._live_vessel()
    report("output_ptr while the Vigil records", lambda: vessel.output_ptr(ctx._handle, 0))
    report("input_ptr while the Vigil records", lambda: vessel.input_ptr(ctx._handle, 0))
    source = torch.ones(16)
    replays = False
    for _ in range(32):
        torch.relu(source * 2.0 + 1.0)
        ctx.flush()
        if ctx.is_compiled():
            replays = True
            break
    print(f"the session replays after the refusals: {replays}", flush=True)
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


def main() -> int:
    """Run the check and report."""
    problems: list[str] = []
    child = run_child(RECORD)
    if child is None:
        problems.append(f"record: the child did not end in {CHILD_SECONDS} s")
    else:
        if child.returncode != 0:
            problems.append(f"record: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
        for marker in (f"output_ptr while the Vigil records: {NOT_REPLAYING_ERROR}",
                       f"input_ptr while the Vigil records: {NOT_REPLAYING_ERROR}",
                       "the session replays after the refusals: True", "the child ended"):
            if marker not in child.stdout:
                problems.append(f"record: the child does not print {marker!r}: {child.stdout!r}")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_replay_addresses: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
