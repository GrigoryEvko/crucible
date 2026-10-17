"""calls — the call profile of a process of the package: the count and the time of each function or of each line.

THE RECORD
    With the variable CALL_PROFILE_ENV of assay.core.meter in the environment,
    each master of utils/bin calls start() before the module of its
    subcommand.  start() enables a profile for the process.  At the end of the
    process, finish() writes one file in the directory of the variable,
    PID-NS.json.  The file holds the words of the command, the process and its
    parent, the mode, and the CPU time.  It also holds the start and the end
    on the monotonic clock, which all processes of the host share.

    The mode MODE_FUNCTIONS uses cProfile.  The file then holds one row for
    each kept function.  A row holds the file, the line, the name, the calls,
    the primitive calls, the own time and the cumulative time.  A kept
    function is in a file of the package (UTILS of assay.core.meter), or it
    has a cumulative time of KEEP_S or more.  The file then holds the import
    machinery of Python, such as _find_and_load, which has a small own time
    and a large cumulative time.  `loop audit --calls DIR` adds the files of a
    run (THE CALL PROFILES of assay.loop.audit).

THE LINES
    With LINES_ENV of assay.core.meter set to LINES_ON, the mode is
    MODE_LINES.  The profile then records each line of the package with
    sys.monitoring (LineProfile), and it does not use cProfile.  The two
    profiles do not run together, because each one makes the times of the
    other larger.  The file holds one row for each line of the package that
    ran.  The row holds these fields:
      - The file and the line
      - The full name of its function (co_qualname) and its first line
      - The hits, the wall time and the CPU time.
    The time of a line
    starts at its start and stops at the start of the next line of the same
    frame.  The time of a line then holds each call on the line, as in
    line_profiler.  The CPU time is the time of the thread
    (time.thread_time_ns()).  sys.monitoring gives an event when the line
    changes.  A loop on one line, such as a comprehension, then counts one hit
    for each entry into the line, and not one for each turn.  A generator
    that continues after a yield gets no event of a line.  Its frame then
    starts at the line of the instruction where it continues.

    The file also holds a row for each function of the package, with the
    shape of a row of cProfile.  The calls count each call.  When a generator
    continues after a yield, the profile counts no call.  The cumulative time of a call starts at its
    start and stops at its return.  A recursive call adds no cumulative time.
    The own time is the cumulative time less the cumulative time of each call
    of a function of the package.  The time of a function of a different
    file is then own time of its caller.  Such a function returns DISABLE
    from each event, and after its first call it gives no event.  On
    2026-10-17, `guard selftest tests.core.test_ledger` took 0.078 s with no
    profile, 0.125 s with cProfile and 0.249 s in the mode MODE_LINES, with
    71,000 events of a line.  The CPU clock of each line was about 0.04 s of
    that time.

THE CALLS OF A LINE
    In the mode MODE_LINES, the file also holds a row for each line that
    calls a function of the package, and for each such function.  The row
    holds these fields:
      - The file, the first line and the full name of the caller
      - The line
      - The file, the first line and the full name of the callee
      - The calls and the wall time.
    The wall time of a call starts at its start and stops at its return, as
    the cumulative time of a function does.  When a generator continues after
    a yield, its time goes to the line that makes it continue, and the
    profile counts no call.  A call through a function of a different file
    goes to the last line of the package on the stack.  An example is the
    line that calls sorted() with a key of the package.  With these rows, `loop audit --lines` expands
    a hot line into the table of each function that it calls, and no list of
    functions is necessary (THE LINES of assay.loop.audit).

THE FORK
    A fork copies the profile of the parent into the child.  The handler of
    os.register_at_fork() then starts a new profile in the child, which does
    not count the work of the parent.  A child of forked() of assay.core.proc
    ends with os._exit(), which runs no handler of atexit, and it calls
    finish() first.  A child that runs a new program writes no file.

THE COST
    A profile counts each call or each line of the Python code.  A profiled
    process then runs slower, and its times are larger than the times of a
    process with no profile.  The profile is for a diagnostic run, and it
    changes no result.  The launcher of a test keeps the two variables out of
    the key of the test (THE ENVIRONMENT of assay.loop.teststep).

`guard selftest tests.core.test_calls` does a test of the module.
"""

import atexit
import cProfile
import json
import os
import sys
import time
from _thread import get_ident
from dataclasses import dataclass
from pathlib import Path
from types import CodeType

from assay.core import meter as cost_meter

# The format of a call profile, and the smallest cumulative time in seconds of a kept function of a different file.
FORMAT = 2
KEEP_S = 0.001
SUFFIX = ".json"
# The file of a built-in function in a row, as pstats gives it.  Its line is 0.
BUILTIN_FILE = "~"
# The modes of a profile (THE RECORD, THE LINES), and the value of LINES_ENV that selects the mode of the lines.
MODE_FUNCTIONS = "functions"
MODE_LINES = "lines"
LINES_ON = "1"
# The tool IDs of sys.monitoring that no tool of Python reserves.  The profile of the lines takes the first free one.
FREE_TOOLS = (3, 4)
TOOL_NAME = "assay.core.calls"


class _Frame:
    """One call of a function of the package on the stack of a thread (THE LINES).

    `table` is the table of the lines of its code, which each event of a line
    reads with no hash of a code object (THE COST).
    """

    __slots__ = ("caller", "child_ns", "code", "cpu_ns", "is_call", "key", "line", "start_ns", "table", "wall_ns")

    def __init__(
        self,
        code: CodeType,
        table: dict[int, list[int]],
        times: tuple[int, int],
        is_call: bool,
        caller: _Frame | None,
    ) -> None:
        """Start a frame of `code` at the wall time and the CPU time of `times`.

        `is_call` is False when a generator continues.  `caller` is the frame of the package
        below it on the stack, or None (THE CALLS OF A LINE).
        """
        self.code = code
        self.key = id(code)
        self.table = table
        self.line = 0
        self.wall_ns, self.cpu_ns = times
        self.start_ns = self.wall_ns
        self.child_ns = 0
        self.is_call = is_call
        self.caller = (caller.key, caller.line) if caller is not None else None


class LineProfile:
    """The profile of the lines of this process: a stack of frames for each thread, and the counts (THE LINES).

    Each table has the key id(code), and `codes` keeps each code object alive.
    CPython does not keep the hash of a code object, and a hash costs about
    0.3 microseconds.  Each callback of sys.monitoring costs O(1), and a return
    with frames of an earlier start costs O(d) for d frames on the stack.
    """

    __slots__ = ("calls", "codes", "depth", "frames", "functions", "lines", "offsets", "root", "tool")

    def __init__(self, tool: int) -> None:
        """Make an empty profile that uses the tool ID `tool` of sys.monitoring."""
        self.tool = tool
        self.root = cost_meter.UTILS + os.sep
        self.codes: dict[int, CodeType] = {}
        self.frames: dict[int, list[_Frame]] = {}
        # The hits, the wall time and the CPU time of each line of each code object.
        self.lines: dict[int, dict[int, list[int]]] = {}
        # The calls, the own time and the cumulative time of each code object.
        self.functions: dict[int, list[int]] = {}
        # The calls and the wall time of each pair of a line and a callee (THE CALLS OF A LINE).
        self.calls: dict[tuple[int, int, int], list[int]] = {}
        self.depth: dict[int, int] = {}
        # The line of each instruction offset of each code object of a generator that continued after a yield.
        self.offsets: dict[int, dict[int, int]] = {}

    def reset(self) -> None:
        """Remove each frame and each count.  A fork calls it in the child (THE FORK)."""
        self.frames.clear()
        self.lines.clear()
        self.functions.clear()
        self.calls.clear()
        self.depth.clear()

    def enable(self) -> None:
        """Register the callbacks of the tool, and enable its events."""
        monitoring = sys.monitoring
        events = monitoring.events
        monitoring.use_tool_id(self.tool, TOOL_NAME)
        monitoring.register_callback(self.tool, events.PY_START, self.on_start)
        monitoring.register_callback(self.tool, events.PY_RESUME, self.on_resume)
        monitoring.register_callback(self.tool, events.PY_THROW, self.on_throw)
        monitoring.register_callback(self.tool, events.LINE, self.on_line)
        monitoring.register_callback(self.tool, events.PY_RETURN, self.on_return)
        monitoring.register_callback(self.tool, events.PY_YIELD, self.on_return)
        monitoring.register_callback(self.tool, events.PY_UNWIND, self.on_unwind)
        monitoring.set_events(
            self.tool,
            events.PY_START
            | events.PY_RESUME
            | events.PY_THROW
            | events.LINE
            | events.PY_RETURN
            | events.PY_YIELD
            | events.PY_UNWIND,
        )

    def disable(self) -> None:
        """Disable the events of the tool, and free its tool ID."""
        sys.monitoring.set_events(self.tool, 0)
        sys.monitoring.free_tool_id(self.tool)

    def _table(self, code: CodeType) -> dict[int, list[int]]:
        """Return the table of the lines of `code`, and keep the code alive."""
        key = id(code)
        table = self.lines.get(key)
        if table is None:
            table = self.lines[key] = {}
            self.codes[key] = code
        return table

    def _push(self, code: CodeType, is_call: bool) -> _Frame:
        """Push a frame of `code` on the stack of this thread, and return it."""
        stack = self.frames.setdefault(get_ident(), [])
        times = (time.perf_counter_ns(), time.thread_time_ns())
        frame = _Frame(code, self._table(code), times, is_call, stack[-1] if stack else None)
        stack.append(frame)
        self.depth[frame.key] = self.depth.get(frame.key, 0) + 1
        return frame

    def _line_of(self, code: CodeType, offset: int) -> int:
        """Return the line of the instruction at `offset` of `code`, or 0.  The first call for a code reads its table."""
        table = self.offsets.get(id(code))
        if table is None:
            table = self.offsets[id(code)] = {}
            for start, end, line in code.co_lines():
                if line is not None:
                    table.update(dict.fromkeys(range(start, end, 2), line))
        return table.get(offset, 0)

    def on_start(self, code: CodeType, offset: int) -> object:
        """Count a call of a function of the package, or return DISABLE for code of a different file."""
        if not code.co_filename.startswith(self.root):
            return sys.monitoring.DISABLE
        self._push(code, is_call=True)
        return None

    def on_resume(self, code: CodeType, offset: int) -> object:
        """Push the frame of a generator of the package that continues after a yield.  The profile counts no call.

        The generator continues in a line, which gives no event of a line.
        The frame then starts at the line of the instruction where it continues.
        """
        if not code.co_filename.startswith(self.root):
            return sys.monitoring.DISABLE
        self._push(code, is_call=False).line = self._line_of(code, offset)
        return None

    def on_throw(self, code: CodeType, offset: int, exception: BaseException) -> object:
        """Push the frame of a generator that gets an exception.  This event cannot return DISABLE."""
        if code.co_filename.startswith(self.root):
            self._push(code, is_call=False).line = self._line_of(code, offset)
        return None

    def on_line(self, code: CodeType, line: int) -> object:
        """Give the time since the last line of the frame to that line, and count a hit of `line`."""
        if not code.co_filename.startswith(self.root):
            return sys.monitoring.DISABLE
        wall_ns, cpu_ns = time.perf_counter_ns(), time.thread_time_ns()
        stack = self.frames.setdefault(get_ident(), [])
        if stack and stack[-1].code is code:
            frame = stack[-1]
            if frame.line:
                last = frame.table.get(frame.line)
                if last is None:
                    frame.table[frame.line] = [0, wall_ns - frame.wall_ns, cpu_ns - frame.cpu_ns]
                else:
                    last[1] += wall_ns - frame.wall_ns
                    last[2] += cpu_ns - frame.cpu_ns
        else:
            # The frame started before start(), for example the body of the master.
            frame = _Frame(code, self._table(code), (wall_ns, cpu_ns), False, stack[-1] if stack else None)
            stack.append(frame)
            self.depth[frame.key] = self.depth.get(frame.key, 0) + 1
        counts = frame.table.get(line)
        if counts is None:
            frame.table[line] = [1, 0, 0]
        else:
            counts[0] += 1
        frame.line, frame.wall_ns, frame.cpu_ns = line, wall_ns, cpu_ns
        return None

    def on_return(self, code: CodeType, offset: int, value: object) -> object:
        """Close the frame of a function of the package that returns or yields."""
        if not code.co_filename.startswith(self.root):
            return sys.monitoring.DISABLE
        self._pop(code)
        return None

    def on_unwind(self, code: CodeType, offset: int, exception: BaseException) -> object:
        """Close the frame of a function of the package that an exception stops.  This event cannot return DISABLE."""
        if code.co_filename.startswith(self.root):
            self._pop(code)
        return None

    def _pop(self, code: CodeType) -> None:
        """Close each frame of this thread down to the last frame of `code`.  A code that no frame holds closes none."""
        stack = self.frames.get(get_ident())
        if not stack:
            return
        if stack[-1].code is not code and not any(frame.code is code for frame in stack):
            return
        wall_ns, cpu_ns = time.perf_counter_ns(), time.thread_time_ns()
        while stack:
            frame = stack.pop()
            self._close(frame, stack, wall_ns, cpu_ns)
            if frame.code is code:
                return

    def _close(self, frame: _Frame, stack: list[_Frame], wall_ns: int, cpu_ns: int) -> None:
        """Give the last line of a frame its time, and add the frame to the counts of its function (THE LINES)."""
        if frame.line:
            last = frame.table.get(frame.line)
            if last is None:
                frame.table[frame.line] = [0, wall_ns - frame.wall_ns, cpu_ns - frame.cpu_ns]
            else:
                last[1] += wall_ns - frame.wall_ns
                last[2] += cpu_ns - frame.cpu_ns
        cumulative = wall_ns - frame.start_ns
        depth = self.depth.get(frame.key, 1) - 1
        self.depth[frame.key] = depth
        counts = self.functions.get(frame.key)
        if counts is None:
            counts = self.functions[frame.key] = [0, 0, 0]
        counts[0] += 1 if frame.is_call else 0
        counts[1] += cumulative - frame.child_ns
        counts[2] += cumulative if depth == 0 else 0
        if stack:
            stack[-1].child_ns += cumulative
        if frame.caller is not None:
            key = (frame.caller[0], frame.caller[1], frame.key)
            edge = self.calls.get(key)
            if edge is None:
                edge = self.calls[key] = [0, 0]
            edge[0] += 1 if frame.is_call else 0
            edge[1] += cumulative if depth == 0 else 0

    def close_open_frames(self) -> None:
        """Close each frame of this thread at this time.  The frames of the entry points then get their time."""
        stack = self.frames.get(get_ident(), [])
        wall_ns, cpu_ns = time.perf_counter_ns(), time.thread_time_ns()
        while stack:
            self._close(stack.pop(), stack, wall_ns, cpu_ns)

    def line_rows(self) -> list[list[object]]:
        """Return the row of each line that ran (THE LINES).  Complexity: linear in the lines."""
        rows: list[list[object]] = []
        for key, table in self.lines.items():
            code = self.codes[key]
            rows.extend(
                [code.co_filename, line, code.co_qualname, code.co_firstlineno, hits, wall_ns / 1e9, cpu_ns / 1e9]
                for line, (hits, wall_ns, cpu_ns) in table.items()
            )
        return rows

    def function_rows(self) -> list[list[object]]:
        """Return the row of each function that ran, with the shape of a row of cProfile (THE LINES)."""
        rows: list[list[object]] = []
        for key, (calls, own_ns, cumulative_ns) in self.functions.items():
            code = self.codes[key]
            name = code.co_qualname
            rows.append([code.co_filename, code.co_firstlineno, name, calls, calls, own_ns / 1e9, cumulative_ns / 1e9])
        return rows

    def call_rows(self) -> list[list[object]]:
        """Return the row of each pair of a line and a callee (THE CALLS OF A LINE).  Complexity: linear in the pairs."""
        rows: list[list[object]] = []
        for (caller_key, line, callee_key), (calls, wall_ns) in self.calls.items():
            caller, callee = self.codes[caller_key], self.codes[callee_key]
            rows.append(
                [
                    caller.co_filename,
                    caller.co_firstlineno,
                    caller.co_qualname,
                    line,
                    callee.co_filename,
                    callee.co_firstlineno,
                    callee.co_qualname,
                    calls,
                    wall_ns / 1e9,
                ]
            )
        return rows


@dataclass(frozen=True, slots=True)
class Profile:
    """The profile of this process: cProfile or the lines, the directory of the file, and the start (THE RECORD)."""

    profiler: cProfile.Profile | None
    lines: LineProfile | None
    directory: Path
    started: float


_active: Profile | None = None


def free_tool() -> int | None:
    """Return the first tool ID of FREE_TOOLS that no tool uses, or None."""
    return next((tool for tool in FREE_TOOLS if sys.monitoring.get_tool(tool) is None), None)


def _new_profile(directory: Path, is_lines: bool) -> Profile:
    """Enable a new profile for this process, and return it.  With no free tool ID, the mode is MODE_FUNCTIONS."""
    tool = free_tool() if is_lines else None
    if tool is not None:
        lines = LineProfile(tool)
        lines.enable()
        return Profile(None, lines, directory, time.monotonic())
    profiler = cProfile.Profile()
    profiler.enable()
    return Profile(profiler, None, directory, time.monotonic())


def _restart_in_child() -> None:
    """Start a new profile in the child of a fork, which does not count the work of the parent (THE FORK)."""
    global _active
    if _active is None:
        return
    if _active.lines is not None:
        _active.lines.reset()
        _active = Profile(None, _active.lines, _active.directory, time.monotonic())
    elif _active.profiler is not None:
        _active.profiler.disable()
        _active = _new_profile(_active.directory, is_lines=False)


def start() -> None:
    """Enable the profile of this process when CALL_PROFILE_ENV names a directory (THE RECORD, THE LINES)."""
    global _active
    directory = os.environ.get(cost_meter.CALL_PROFILE_ENV, "")
    if not directory or _active is not None:
        return
    _active = _new_profile(Path(directory), os.environ.get(cost_meter.LINES_ENV, "") == LINES_ON)
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
    """Stop the profile of this process, and write its file (THE RECORD).  A second call writes nothing."""
    global _active
    profile, _active = _active, None
    if profile is None:
        return
    if profile.lines is not None:
        profile.lines.disable()
        profile.lines.close_open_frames()
        mode, functions = MODE_LINES, profile.lines.function_rows()
        lines, line_calls = profile.lines.line_rows(), profile.lines.call_rows()
    else:
        assert profile.profiler is not None
        profile.profiler.disable()
        mode, functions, lines, line_calls = MODE_FUNCTIONS, rows_of(profile.profiler), [], []
    document = {
        "format": FORMAT,
        "mode": mode,
        "argv": sys.argv,
        "pid": os.getpid(),
        "parent": os.getppid(),
        "start": profile.started,
        "end": time.monotonic(),
        "cpu_s": time.process_time(),
        "functions": functions,
        "lines": lines,
        "calls": line_calls,
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
