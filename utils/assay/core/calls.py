"""calls — the call profile of a process of the package: the count and the time of each function.

THE RECORD
    With the variable CALL_PROFILE_ENV of assay.core.meter in the environment,
    each master of utils/bin calls start() before the module of its
    subcommand.  start() enables cProfile for the process.  At the end of the
    process, finish() writes one file in the directory of the variable,
    PID-NS.json.  The file holds the words of the command, the process and its
    parent, and the CPU time.  It also holds the start and the end on the
    monotonic clock, which all processes of the host share.

    The file also holds one row for each kept function.  A row holds the
    file, the line, the name, the calls, the primitive calls, the own time
    and the cumulative time.  A kept function is in a file of the package
    (UTILS of assay.core.meter), or it has a cumulative time of KEEP_S or
    more.  The file then holds the import machinery of Python,
    such as _find_and_load, which has a small own time and a large cumulative
    time.  `loop audit --calls DIR` adds the files of a run (THE CALL PROFILES
    of assay.loop.audit).

THE FORK
    A fork copies the profile of the parent into the child.  The handler of
    os.register_at_fork() then stops that copy and starts a new profile in the
    child, which does not count the work of the parent.  A child of forked()
    of assay.core.proc ends with os._exit(), which runs no handler of atexit,
    and it calls finish() first.  A child that runs a new program writes no
    file.

THE COST
    cProfile counts each call of each Python function.  A profiled process
    then runs slower, and its times are larger than the times of a process
    with no profile.  The profile is for a diagnostic run, and it changes no
    result.  The launcher of a test keeps the variable out of the key of the
    test (THE ENVIRONMENT of assay.loop.teststep).

`guard selftest tests.core.test_calls` does a test of the module.
"""

import atexit
import cProfile
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from assay.core import meter as cost_meter

# The format of a call profile, and the smallest cumulative time in seconds of a kept function of a different file.
FORMAT = 1
KEEP_S = 0.001
SUFFIX = ".json"
# The file of a built-in function in a row, as pstats gives it.  Its line is 0.
BUILTIN_FILE = "~"


@dataclass(frozen=True, slots=True)
class Profile:
    """The profile of this process: cProfile, the directory of the file, and the start (THE RECORD)."""

    profiler: cProfile.Profile
    directory: Path
    started: float


_active: Profile | None = None


def _new_profile(directory: Path) -> Profile:
    """Enable a new cProfile for this process, and return its profile."""
    profiler = cProfile.Profile()
    profiler.enable()
    return Profile(profiler, directory, time.monotonic())


def _restart_in_child() -> None:
    """Stop the profile that a fork copied from the parent, and start a new profile in the child (THE FORK)."""
    global _active
    if _active is not None:
        _active.profiler.disable()
        _active = _new_profile(_active.directory)


def start() -> None:
    """Enable the call profile of this process when CALL_PROFILE_ENV names a directory (THE RECORD)."""
    global _active
    directory = os.environ.get(cost_meter.CALL_PROFILE_ENV, "")
    if not directory or _active is not None:
        return
    _active = _new_profile(Path(directory))
    atexit.register(finish)
    os.register_at_fork(after_in_child=_restart_in_child)


def is_kept(file: str, cumulative_s: float) -> bool:
    """Tell if a function goes into the file: a function of the package, or one of KEEP_S or more cumulative time."""
    return file.startswith(cost_meter.UTILS) or cumulative_s >= KEEP_S


def rows_of(profiler: cProfile.Profile) -> list[list[object]]:
    """Return the row of each kept function of a stopped profiler (THE RECORD).

    Complexity: linear in the functions that the profiler counted.
    """
    rows: list[list[object]] = []
    for entry in profiler.getstats():
        code = entry.code
        if isinstance(code, str):
            file, line, name = BUILTIN_FILE, 0, code
        else:
            file, line, name = code.co_filename, code.co_firstlineno, code.co_name
        if is_kept(file, entry.totaltime):
            primitive = entry.callcount - entry.reccallcount
            rows.append([file, line, name, entry.callcount, primitive, entry.inlinetime, entry.totaltime])
    return rows


def finish() -> None:
    """Stop the call profile of this process, and write its file (THE RECORD).  A second call writes nothing."""
    global _active
    profile, _active = _active, None
    if profile is None:
        return
    profile.profiler.disable()
    document = {
        "format": FORMAT,
        "argv": sys.argv,
        "pid": os.getpid(),
        "parent": os.getppid(),
        "start": profile.started,
        "end": time.monotonic(),
        "cpu_s": time.process_time(),
        "functions": rows_of(profile.profiler),
    }
    try:
        profile.directory.mkdir(parents=True, exist_ok=True)
        target = profile.directory / f"{os.getpid()}-{time.monotonic_ns()}{SUFFIX}"
        partial = target.with_suffix(".partial")
        partial.write_text(json.dumps(document), encoding="utf-8")
        partial.replace(target)
    except OSError as problem:
        from assay.core import report as check_report

        check_report.write_line(
            f"calls: the call profile in {profile.directory} is not written: {problem}", is_error=True
        )
