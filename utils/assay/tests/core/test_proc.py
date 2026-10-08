"""The test of the process module (assay.core.proc): the run, the limit, the environment, the streams and the import.

`guard selftest tests.core.test_proc` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.  The commands are sh, cat and
the Python of this process, so the test needs no tool of the tree.
"""

import os
import signal
import sys
import tempfile
import time
from collections.abc import Callable
from pathlib import Path

from assay.core import proc

PYTHON = sys.executable
UTILS = str(Path(__file__).resolve().parents[3])
# A limit that no case of this test reaches when the host works, in seconds.
SAFE_S = 60.0


def python(code: str) -> list[str]:
    """Return the command that runs a line of Python with no site module, and that writes no compiled module (-B).

    A child that imports the package from utils/ would else write __pycache__ into the tree.
    """
    return [PYTHON, "-S", "-B", "-c", code]


def refuses(action: Callable[[], object], error: type[BaseException]) -> BaseException | None:
    """Return the error that an action raises, or None when it raises no such error."""
    try:
        action()
    except error as problem:
        return problem
    return None


def is_gone(pid: int, wait_s: float = 5.0) -> bool:
    """Whether a process is gone or a zombie within a time.  init reaps an orphan that a signal stopped."""
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        try:
            state = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
        except (FileNotFoundError, ProcessLookupError, IndexError):
            return True
        if state in ("Z", "X"):
            return True
        time.sleep(0.02)
    return False


def run_cases(expect: Callable[[str, bool], None]) -> None:
    """Check a captured run: the exit, the outputs, the times and the raw status."""
    done = proc.run(proc.Command(python("import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"),
                                 timeout_s=SAFE_S))
    expect("a run gives the exit and each captured output",
           done.exit == 3 and done.out == "out\n" and done.err == "err\n" and done.stdout == b"out\n")
    expect("a run gives the wall time, the CPU time, the peak memory and the raw wait status",
           done.wall_s > 0 and done.cpu_s >= 0 and done.peak_kb > 0 and os.WIFEXITED(done.wait_status)
           and os.WEXITSTATUS(done.wait_status) == 3)
    problem = refuses(lambda: proc.require(done), proc.ToolFailed)
    expect("require() refuses an exit that is not 0, with the command, the exit and the end of stderr",
           isinstance(problem, proc.ToolFailed) and "exits with 3" in str(problem) and str(problem).endswith(": err")
           and problem.completed is done)
    passed = proc.run(proc.Command(["sh", "-c", "exit 0"], timeout_s=SAFE_S))
    expect("require() returns a run whose exit is 0", proc.require(passed) is passed)
    killed = proc.run(proc.Command(["sh", "-c", "kill -TERM $$"], timeout_s=SAFE_S))
    expect("a command that a signal ends gives minus the signal", killed.exit == -signal.SIGTERM)
    expect("require() says the signal",
           "ends on the signal 15" in str(refuses(lambda: proc.require(killed), proc.ToolFailed)))
    text = proc.run(proc.Command(python("import sys; sys.stdout.buffer.write(b'a\\r\\nb\\rc\\xff\\n')"),
                                 timeout_s=SAFE_S))
    expect("out decodes UTF-8 with U+FFFD and changes \\r\\n and \\r to \\n",
           text.out == "a\nb\nc\ufffd\n" and text.stdout == b"a\r\nb\rc\xff\n")
    expect("decoded() with surrogateescape keeps each byte that is not UTF-8",
           proc.decoded(text.stdout, "surrogateescape").encode("utf-8", "surrogateescape") == b"a\nb\nc\xff\n")
    directory = proc.run(proc.Command(["sh", "-c", "pwd"], timeout_s=SAFE_S, cwd=Path("/")))
    expect("a run takes its working directory", directory.out == "/\n")


def start_failure_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the refusals before a start, and a start that fails."""
    missing = refuses(lambda: proc.run(proc.Command(["no-such-program-of-the-tree"], timeout_s=SAFE_S)),
                      proc.CannotStart)
    expect("a program that does not exist gives CannotStart with the error of the system",
           isinstance(missing, proc.CannotStart) and isinstance(missing.error, FileNotFoundError)
           and "no-such-program-of-the-tree" in str(missing))
    no_directory = refuses(lambda: proc.run(proc.Command(["true"], timeout_s=SAFE_S, cwd="/no/such/directory")),
                           proc.CannotStart)
    expect("a working directory that does not exist gives CannotStart that names it",
           isinstance(no_directory, proc.CannotStart) and "/no/such/directory" in str(no_directory))
    for name, action in (
            ("an empty command", lambda: proc.Command([], timeout_s=1)),
            ("an empty word", lambda: proc.Command(["true", ""], timeout_s=1)),
            ("a limit of 0", lambda: proc.Command(["true"], timeout_s=0)),
            ("an infinite limit", lambda: proc.Command(["true"], timeout_s=float("inf"))),
            ("a negative grace", lambda: proc.Command(["true"], timeout_s=1, grace_s=-1)),
            ("a text as the standard input", lambda: proc.Command(["true"], timeout_s=1, stdin="x")),  # type: ignore[arg-type]
            ("MERGE as the destination of stdout", lambda: proc.Command(["true"], timeout_s=1, stdout=proc.Output.MERGE)),
            ("STREAM as the destination of stderr", lambda: proc.Command(["true"], timeout_s=1,
                                                                        stderr=proc.Output.STREAM)),
            ("an unknown destination", lambda: proc.Command(["true"], timeout_s=1, stdout="file.txt")),
            ("an unknown stop word", lambda: proc.Command(["true"], timeout_s=1, stop="kill")),
            ("a stream in run()", lambda: proc.run(proc.Command(["true"], timeout_s=1, stdout=proc.Output.STREAM)))):
        expect(f"the module refuses {name}", refuses(action, ValueError) is not None)


def limit_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the limit: SIGTERM to the group, SIGKILL after the grace, and no process of the group after it."""
    started = time.monotonic()
    command = proc.Command(["sh", "-c", "sleep 60 & echo $!; wait"], timeout_s=0.5, grace_s=1.0)
    problem = refuses(lambda: proc.run(command), proc.TimedOut)
    child_pid = int(problem.completed.out.split()[0]) if isinstance(problem, proc.TimedOut) and problem.completed \
        and problem.completed.out.split() else -1
    expect("a command past its limit gives TimedOut with the output that it gave, soon after the limit",
           isinstance(problem, proc.TimedOut) and child_pid > 0 and time.monotonic() - started < 10)
    expect("no process of the group runs after the limit", child_pid > 0 and is_gone(child_pid))
    started = time.monotonic()
    stubborn = proc.Command(python("import signal, time\nsignal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                                   "print('ready', flush=True)\ntime.sleep(60)"), timeout_s=1.0, grace_s=0.3)
    problem = refuses(lambda: proc.run(stubborn), proc.TimedOut)
    expect("a command that ignores SIGTERM gets SIGKILL after the grace",
           isinstance(problem, proc.TimedOut) and problem.completed is not None
           and problem.completed.exit == -signal.SIGKILL and time.monotonic() - started < 10)

    class Interrupt(Exception):
        """The error that the timer of the case raises in the loop of the run."""

    def interrupt(_number: int, _frame: object) -> None:
        raise Interrupt

    previous = signal.signal(signal.SIGALRM, interrupt)
    running = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S))
    signal.setitimer(signal.ITIMER_REAL, 0.3)
    try:
        caught = refuses(running.wait, Interrupt)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
    expect("an interrupt of the wait stops the group and then goes on",
           isinstance(caught, Interrupt) and is_gone(running.pid))
    stopped = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S)).stop()
    expect("stop() ends the group with SIGTERM and gives the result", stopped.exit == -signal.SIGTERM)


def interrupted(action: Callable[[], object], first_s: float, interval_s: float = 0.0) -> bool:
    """Run an action while a timer raises KeyboardInterrupt, as Ctrl-C does, and return whether it reached the caller.

    The timer fires first after first_s, and then each interval_s when interval_s is more than 0.
    """
    def interrupt(_number: int, _frame: object) -> None:
        raise KeyboardInterrupt

    previous = signal.signal(signal.SIGALRM, interrupt)
    signal.setitimer(signal.ITIMER_REAL, first_s, interval_s)
    try:
        action()
    except KeyboardInterrupt:
        return True
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
    return False


def wait_for_file(path: Path, wait_s: float = 10.0) -> bool:
    """Whether a file exists within a time."""
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        if path.exists():
            return True
        time.sleep(0.01)
    return False


def interrupt_cases(expect: Callable[[str, bool], None], scratch: Path) -> None:
    """Check AN INTERRUPT: SIGINT to the group, the end of the command, and SIGKILL at a second interrupt."""
    summary = scratch / "summary.txt"
    ready = scratch / "ready"
    polite = python("import signal, sys, time\n"
                    "def stop(_n, _f):\n"
                    "    open(sys.argv[1], 'w').write('summary\\n')\n"
                    "    print('summary', flush=True)\n"
                    "    sys.exit(7)\n"
                    "signal.signal(signal.SIGINT, stop)\n"
                    "open(sys.argv[2], 'w').close()\n"
                    "print('ready', flush=True)\n"
                    "time.sleep(60)")
    running = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S))
    started = time.monotonic()
    is_ready = wait_for_file(ready)
    reached = interrupted(running.wait, 0.1)
    expect("an interrupt of the wait sends SIGINT to the group, waits for the end of the command, and then goes on",
           is_ready and reached and summary.is_file() and summary.read_text() == "summary\n" and is_gone(running.pid)
           and time.monotonic() - started < 10)
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    streamed = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S,
                                       stdout=proc.Output.STREAM))
    first = next(streamed)
    rest: list[str] = []
    reached = interrupted(lambda: rest.extend(streamed), 0.1)
    expect("an interrupt of a stream gives the lines that the command writes after SIGINT, and then goes on",
           first == "ready\n" and rest == ["summary\n"] and reached and is_gone(streamed.pid))
    deaf = proc.start(proc.Command(python("import signal, time\nsignal.signal(signal.SIGINT, signal.SIG_IGN)\n"
                                          "print('ready', flush=True)\ntime.sleep(60)"),
                                   timeout_s=SAFE_S, stdout=proc.Output.STREAM))
    next(deaf)
    started = time.monotonic()
    reached = interrupted(deaf.wait, 0.1, 0.3)
    expect("a second interrupt sends SIGKILL to a group that ignores SIGINT, and then goes on",
           reached and is_gone(deaf.pid) and time.monotonic() - started < 10)
    started = time.monotonic()
    late = proc.start(proc.Command(python("import signal, time\nsignal.signal(signal.SIGINT, signal.SIG_IGN)\n"
                                          "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                                          "print('ready', flush=True)\ntime.sleep(60)"),
                                   timeout_s=1.0, grace_s=0.3, stdout=proc.Output.STREAM))
    next(late)
    reached = interrupted(late.wait, 0.1)
    expect("after one interrupt, the limit still stops a command that ignores SIGINT and SIGTERM",
           reached and is_gone(late.pid) and time.monotonic() - started < 10)
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    gentle = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S, stop=proc.Stop.INTERRUPT))
    is_ready = wait_for_file(ready)
    stopped = gentle.stop()
    expect("stop() of a command with the stop word interrupt sends SIGINT, and the command ends in its own way",
           is_ready and stopped.exit == 7 and summary.is_file() and summary.read_text() == "summary\n")
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    started = time.monotonic()
    timed = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=1.0, stop=proc.Stop.INTERRUPT))
    is_ready = wait_for_file(ready)
    problem = refuses(timed.wait, proc.TimedOut)
    expect("the limit of a command with the stop word interrupt sends SIGINT first, and the command ends in its own way",
           is_ready and isinstance(problem, proc.TimedOut) and problem.completed is not None
           and problem.completed.exit == 7 and summary.is_file() and "SIGINT" in str(problem)
           and time.monotonic() - started < 10)


def stream_cases(expect: Callable[[str, bool], None], scratch: Path) -> None:
    """Check the input, the large outputs, the files, the merge and a stream of lines."""
    data = bytes(range(256)) * 8192
    echoed = proc.run(proc.Command(["cat"], timeout_s=SAFE_S, stdin=data))
    expect("2 MB of input through cat comes back whole, and no pipe stops the command", echoed.stdout == data)
    both = proc.run(proc.Command(python("import sys\nfor _ in range(4096):\n    sys.stdout.write('o' * 255 + '\\n')\n"
                                        "    sys.stderr.write('e' * 255 + '\\n')"), timeout_s=SAFE_S))
    expect("1 MB on each output at the same time comes back whole",
           len(both.stdout) == 1 << 20 and len(both.stderr) == 1 << 20)
    log = scratch / "log.txt"
    merged = proc.run(proc.Command(python("import sys; print('one', flush=True); print('two', file=sys.stderr)"),
                                   timeout_s=SAFE_S, stdout=log, stderr=proc.Output.MERGE))
    expect("a path gets stdout, MERGE adds stderr, and the result keeps no output",
           log.read_text() == "one\ntwo\n" and merged.stdout == b"" and merged.stderr == b"")
    log.write_text("an earlier log that the run replaces, longer than the new one\n")
    proc.run(proc.Command(["sh", "-c", "echo new"], timeout_s=SAFE_S, stdout=log))
    expect("a path gets a new file", log.read_text() == "new\n")
    quiet = proc.run(proc.Command(python("import sys; print('x'); print('y', file=sys.stderr)"), timeout_s=SAFE_S,
                                  stdout=proc.Output.DISCARD, stderr=proc.Output.DISCARD))
    expect("DISCARD keeps no output", quiet.stdout == b"" and quiet.stderr == b"" and quiet.exit == 0)
    inherited = proc.run(proc.Command([*python(
        "import sys; sys.path.insert(0, sys.argv[1]); from assay.core import proc\n"
        "proc.run(proc.Command(['sh', '-c', 'echo inner'], timeout_s=60, stdout=proc.Output.INHERIT))"), UTILS],
        timeout_s=SAFE_S))
    expect("INHERIT writes to the stream of the caller", inherited.out == "inner\n")
    running = proc.start(proc.Command(["sh", "-c", "printf 'a\\nbb\\nlast'"], timeout_s=SAFE_S,
                                      stdout=proc.Output.STREAM))
    lines = list(running)
    expect("a stream gives each line with its \\n, and the last line with no \\n", lines == ["a\n", "bb\n", "last"])
    expect("the result of a stream that the loop read to its end holds no output", running.wait().stdout == b"")
    unread = proc.start(proc.Command(["sh", "-c", "printf 'a\\nb\\n'"], timeout_s=SAFE_S, stdout=proc.Output.STREAM))
    expect("the result of a stream holds the lines that the loop did not give", unread.wait().stdout == b"a\nb\n")
    slow = proc.start(proc.Command(["sh", "-c", "echo first; sleep 60"], timeout_s=0.5, grace_s=0.2,
                                   stdout=proc.Output.STREAM))
    first = next(slow)
    expect("the limit counts while the caller reads the lines of a stream",
           first == "first\n" and refuses(lambda: list(slow), proc.TimedOut) is not None)
    expect("a loop over a run with no stream is refused",
           refuses(lambda: iter(proc.start(proc.Command(["true"], timeout_s=SAFE_S))), ValueError) is not None)
    started = time.monotonic()
    many = proc.start(proc.Command(python("import sys\nfor index in range(200000):\n    print(index)"),
                                   timeout_s=SAFE_S, stdout=proc.Output.STREAM))
    count = sum(1 for _line in many)
    expect("a stream of 200,000 lines gives each line, in a time that is linear in the lines",
           count == 200000 and many.wait().exit == 0 and time.monotonic() - started < 10)


def pidfd_failure_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that a kernel with no pidfd gives CannotStart, and that the start leaves no process and no pipe."""
    real = os.pidfd_open
    before = set(os.listdir("/proc/self/fd"))

    def refused(_pid: int, _flags: int = 0) -> int:
        raise OSError(38, "Function not implemented")

    os.pidfd_open = refused
    try:
        problem = refuses(lambda: proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S, stdin=b"x")),
                          proc.CannotStart)
    finally:
        os.pidfd_open = real
    after = set(os.listdir("/proc/self/fd"))
    expect("a kernel with no pidfd gives CannotStart that names Linux 5.3",
           isinstance(problem, proc.CannotStart) and "Linux 5.3" in str(problem))
    expect("the failed start leaves no open pipe", after <= before)


def environment_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the environment: no repository variable reaches a child, and the overrides do."""
    show = python("import os, sys; print(os.environ.get(sys.argv[1], '-'))")
    saved = os.environ.get("GIT_DIR")
    os.environ["GIT_DIR"] = "/no/such/repository"
    try:
        inherited = proc.run(proc.Command([*show, "GIT_DIR"], timeout_s=SAFE_S)).out
        given = proc.run(proc.Command([*show, "GIT_DIR"], timeout_s=SAFE_S,
                                      env=dict(os.environ, GIT_WORK_TREE="/x"))).out
        override = proc.environment({"GIT_INDEX_FILE": "/x", "CRUCIBLE_PROC_PLANTED": "1"})
    finally:
        if saved is None:
            os.environ.pop("GIT_DIR")
        else:
            os.environ["GIT_DIR"] = saved
    expect("a child of this environment gets no GIT_DIR", inherited == "-\n")
    expect("a child of a full mapping gets no GIT_DIR", given == "-\n")
    expect("environment() keeps an override and drops a repository variable",
           override.get("CRUCIBLE_PROC_PLANTED") == "1" and not set(override) & set(proc.REPOSITORY_VARIABLES))
    planted = proc.run(proc.Command([*show, "CRUCIBLE_PROC_PLANTED"], timeout_s=SAFE_S,
                                    env=proc.environment({"CRUCIBLE_PROC_PLANTED": "yes"}))).out
    expect("a child gets the override", planted == "yes\n")


def import_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that the import of the module costs little: it imports no heavy module (WHY THE MODULE IMPORTS LITTLE)."""
    heavy = ("subprocess", "selectors", "signal", "dataclasses", "enum", "typing", "pathlib", "collections")
    listed = proc.run(proc.Command([*python(
        "import sys; before = set(sys.modules); sys.path.insert(0, sys.argv[1]); import assay.core.proc\n"
        "print(' '.join(sorted(set(sys.modules) - before)))"), UTILS], timeout_s=SAFE_S)).out.split()
    expect(f"an import of the module with -S imports none of {', '.join(heavy)}",
           "assay.core.proc" in listed and not set(heavy) & set(listed))


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_proc", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    run_cases(expect)
    start_failure_cases(expect)
    limit_cases(expect)
    with tempfile.TemporaryDirectory(prefix="assay-proc-") as scratch:
        stream_cases(expect, Path(scratch))
        interrupt_cases(expect, Path(scratch))
    environment_cases(expect)
    pidfd_failure_cases(expect)
    import_cases(expect)
    if failures:
        print(f"tests.core.test_proc: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_proc: every case holds.")
    return 0
