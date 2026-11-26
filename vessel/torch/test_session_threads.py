#!/usr/bin/env python3
"""The sessions of the vessel in one process run on one thread.

A session writes structures of the process with no lock: the state of the
sessions of the generation allocator, the CPU allocator of PyTorch and the wrap
of Tensor.backward.  The thread that creates the Vigil of a session claims it
(crucible_create), and each live Vigil of the process has one producer thread.
So a second thread that opens a session while a session of the first thread
lives gets CrucibleWrongThreadError at the create of its Vigil.  The error
comes before the second thread writes a structure of the process, and the
process continues.

The checks run in child processes, because the attach refusal ends the
process:

    second    a session on a second thread, while a session of the main thread
              lives, raises CrucibleWrongThreadError before the session opens,
              and the session of the main thread replays its loop after it
    attach    an attach of the generation allocator on a second thread, while a
              session of the main thread lives, ends the child with SIGABRT
    later     a session on a second thread after the session of the main thread
              closed replays its loop, and the child exits with 0
    nested    a session in a session on one thread replays its loop, and the
              child exits with 0
    mode      a CrucibleMode gives up the claim of its thread at its exit
              (crucible_release_claim).  A session on a second thread, while
              the mode object of the main thread lives in a reference cycle,
              replays its loop.  Then the mode records on the main thread
              again, and the child exits with 0
    held      a session on a second thread, while a CrucibleMode of the main
              thread is inside its context, raises CrucibleWrongThreadError
              before the session opens

Usage:
    python test_session_threads.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
VESSEL_DIRECTORY = Path(__file__).resolve().parent

# The start of each child: the import path, and a loop that replays.  The loop
# flushes after each iteration, so its replay does not depend on the speed of
# the background thread.
PRELUDE = """
import sys
import threading
sys.path.insert(0, sys.argv[1])
import torch
from crucible_native import CrucibleNative

def give_loop(ctx):
    source = torch.ones(16)
    for _ in range(32):
        torch.relu(source * 2.0 + 1.0)
        ctx.flush()
        if ctx.is_compiled():
            return True
    return False

def run_on_a_thread(body):
    thread = threading.Thread(target=body)
    thread.start()
    thread.join()

def open_second_session():
    try:
        with CrucibleNative():
            print("the second session opened", flush=True)
    except Exception as error:
        print(f"the second session raised {type(error).__name__}", flush=True)
"""

SECOND = PRELUDE + """
with CrucibleNative() as ctx:
    give_loop(ctx)
    run_on_a_thread(open_second_session)
    print(f"the first session replays after the refusal: {give_loop(ctx)}", flush=True)
print("the child ended", flush=True)
"""

ATTACH = PRELUDE + """
with CrucibleNative() as ctx:
    give_loop(ctx)
    def attach():
        ctx._dispatch.attach_allocator()
        print("the second attach returned", flush=True)
    run_on_a_thread(attach)
print("the child ended", flush=True)
"""

LATER = PRELUDE + """
with CrucibleNative() as ctx:
    give_loop(ctx)
results = []
def later():
    with CrucibleNative() as ctx:
        results.append(give_loop(ctx))
run_on_a_thread(later)
print(f"the later session replays: {results == [True]}", flush=True)
"""

NESTED = PRELUDE + """
with CrucibleNative():
    with CrucibleNative() as inner:
        replays = give_loop(inner)
print(f"the nested session replays: {replays}", flush=True)
"""

# The mode records one op, and the reference cycle keeps the mode object alive
# after its name goes, as a frame or a closure can keep it.
MODE = PRELUDE + """
from crucible_mode import CrucibleMode
mode = CrucibleMode()
with mode:
    torch.relu(torch.ones(16) * 2.0 + 1.0)
first_count = mode._op_count
mode.cycle = mode
results = []
def later():
    with CrucibleNative() as ctx:
        results.append(give_loop(ctx))
run_on_a_thread(later)
print(f"the session after the exit of a live mode replays: {results == [True]}", flush=True)
with mode:
    torch.relu(torch.ones(16) * 2.0 + 1.0)
print(f"the mode records again: {mode._op_count > first_count > 0}", flush=True)
"""

HELD = PRELUDE + """
from crucible_mode import CrucibleMode
with CrucibleMode():
    torch.relu(torch.ones(16) * 2.0 + 1.0)
    run_on_a_thread(open_second_session)
print("the child ended", flush=True)
"""

# The error of a session on a second thread, and the text of the refusal of a
# write of the state of the sessions on a thread that holds no Vigil claim.
SECOND_SESSION_REFUSAL = "the second session raised CrucibleWrongThreadError"
ATTACH_REFUSAL = "can_caller_use_brand"


def run_child(code: str) -> subprocess.CompletedProcess[str]:
    """Run CODE in a child interpreter with the vessel directory, and give its result."""
    return subprocess.run([sys.executable, "-E", "-B", "-c", code, str(VESSEL_DIRECTORY)], env=dict(os.environ),
                          capture_output=True, text=True, timeout=600, check=False)


def check_refused(problems: list[str], name: str, code: str, refusal: str, marker: str) -> None:
    """Expect the child of CODE to end with SIGABRT and REFUSAL, before it prints MARKER."""
    child = run_child(code)
    if child.returncode != -signal.SIGABRT:
        problems.append(f"{name}: the child ends with {child.returncode}, not SIGABRT: {child.stderr[-2000:]!r}")
    if refusal not in child.stderr:
        problems.append(f"{name}: the child does not give the text {refusal!r}: {child.stderr[-2000:]!r}")
    if marker in child.stdout:
        problems.append(f"{name}: the child printed {marker!r} before the refusal")


def check_admitted(problems: list[str], name: str, code: str, *markers: str) -> None:
    """Expect the child of CODE to exit with 0 and to print each of MARKERS."""
    child = run_child(code)
    if child.returncode != 0:
        problems.append(f"{name}: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
    for marker in markers:
        if marker not in child.stdout:
            problems.append(f"{name}: the child does not print {marker!r}: {child.stdout!r}")


def check_raised(problems: list[str], name: str, code: str, forbidden: str, *markers: str) -> None:
    """Expect the child of CODE to exit with 0, to print each of MARKERS, and not to print FORBIDDEN."""
    child = run_child(code)
    if child.returncode != 0:
        problems.append(f"{name}: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
    for marker in markers:
        if marker not in child.stdout:
            problems.append(f"{name}: the child does not print {marker!r}: {child.stdout!r}")
    if forbidden in child.stdout:
        problems.append(f"{name}: the child printed {forbidden!r}")


def main() -> int:
    """Run each check and report."""
    problems: list[str] = []
    check_raised(problems, "second", SECOND, "the second session opened", SECOND_SESSION_REFUSAL,
                 "the first session replays after the refusal: True", "the child ended")
    check_refused(problems, "attach", ATTACH, ATTACH_REFUSAL, "the second attach returned")
    check_admitted(problems, "later", LATER, "the later session replays: True")
    check_admitted(problems, "nested", NESTED, "the nested session replays: True")
    check_admitted(problems, "mode", MODE, "the session after the exit of a live mode replays: True",
                   "the mode records again: True")
    check_raised(problems, "held", HELD, "the second session opened", SECOND_SESSION_REFUSAL, "the child ended")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_session_threads: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
