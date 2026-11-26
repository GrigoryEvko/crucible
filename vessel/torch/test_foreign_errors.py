#!/usr/bin/env python3
"""An error of PyTorch in a C entry of the dispatch library gives a Python RuntimeError, and the process continues.

PyTorch reports an error with a C++ exception.  ctypes has no C++ unwind, so
each C entry of the dispatch library is noexcept, and an entry that calls
PyTorch code that can throw catches the error at its boundary.  The entry gives
the status FOREIGN_ERROR, crucible_dispatch_last_error gives the text of the
error, and Python raises RuntimeError with that text.

The getters of the TF32 bits of PyTorch raise an error for a mix of the legacy
and the new API: here cuDNN conv and cuDNN RNN with two different TF32 flags.
The text of that error is longer than the 224 bytes that the library keeps,
so the Python text ends with a cut mark.

The check runs in a child process, because the defect that it finds ends the
process:

    mix    the read of the eager word and the attach of a session each raise
           RuntimeError with the text of PyTorch and a cut mark.  After the
           flags agree again, a session opens and replays its loop

Usage:
    python test_foreign_errors.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
VESSEL_DIRECTORY = Path(__file__).resolve().parent

# The most seconds of the child.
CHILD_SECONDS = 600

# The bytes of the text of an error that the dispatch library keeps.
KEPT_BYTES = 224

# The child prints what it sees, so the parent judges each line.
MIX = """
import re
import sys
sys.path.insert(0, sys.argv[1])
import torch
torch.set_num_threads(1)
torch.set_num_interop_threads(1)
from crucible_native import CrucibleNative, _DispatchLib

torch.backends.cudnn.conv.fp32_precision = "tf32"
torch.backends.cudnn.rnn.fp32_precision = "ieee"

def report(name, call):
    try:
        call()
    except RuntimeError as error:
        text = str(error)
        cut = re.search(r"\\[cut: (\\d+) of (\\d+) bytes\\]$", text)
        print(f"{name}: RuntimeError", flush=True)
        print(f"{name} names the flags: {'cuDNN conv and cuDNN RNN' in text}", flush=True)
        if cut:
            print(f"{name} cut: {cut.group(1)} of {cut.group(2)}", flush=True)
    except Exception as error:
        print(f"{name}: {type(error).__name__}", flush=True)
    else:
        print(f"{name}: no error", flush=True)

def attach():
    with CrucibleNative():
        pass

report("eager word", lambda: _DispatchLib().eager_numerics())
report("attach", attach)

torch.backends.cudnn.rnn.fp32_precision = "tf32"
with CrucibleNative() as ctx:
    source = torch.ones(16)
    replays = False
    for _ in range(32):
        torch.relu(source * 2.0 + 1.0)
        ctx.flush()
        if ctx.is_compiled():
            replays = True
            break
print(f"the session replays after the errors: {replays}", flush=True)
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


def check_cut(problems: list[str], name: str, stdout: str) -> None:
    """Expect the error of NAME to end with a cut mark of KEPT_BYTES bytes of a longer text."""
    found = re.search(rf"^{name} cut: (\d+) of (\d+)$", stdout, re.MULTILINE)
    if found is None:
        problems.append(f"mix: the error of the {name} has no cut mark: {stdout!r}")
        return
    kept, full = int(found.group(1)), int(found.group(2))
    if kept != KEPT_BYTES or full <= KEPT_BYTES:
        problems.append(f"mix: the error of the {name} keeps {kept} of {full} bytes, not {KEPT_BYTES} of more")


def main() -> int:
    """Run the check and report."""
    problems: list[str] = []
    child = run_child(MIX)
    if child is None:
        problems.append(f"mix: the child did not end in {CHILD_SECONDS} s")
    else:
        if child.returncode != 0:
            problems.append(f"mix: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
        for marker in ("eager word: RuntimeError", "eager word names the flags: True", "attach: RuntimeError",
                       "attach names the flags: True", "the session replays after the errors: True",
                       "the child ended"):
            if marker not in child.stdout:
                problems.append(f"mix: the child does not print {marker!r}: {child.stdout!r}")
        for name in ("eager word", "attach"):
            check_cut(problems, name, child.stdout)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_foreign_errors: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
