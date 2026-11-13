"""The test of the process module (assay.core.proc): the run, the limit, the environment, the streams and the import.

`guard selftest tests.core.test_proc` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.  The commands are sh, sleep,
cat, true and the Python of this process, so the test needs no tool of the
tree.

THE START OF A CHILD
    On a host under load, a child can start seconds late, and the host can
    stop this process for a time at each point.  No case lets a limit or a
    time of the test count against such a delay:
      * The loop of a run applies the limit of the command, and nothing
        else does.  A case that needs the child in a state before the limit,
        such as a trap that it set or a line that it wrote, waits for the
        ready file of the child before it enters the loop.  The limit then
        applies after that state.
      * A case that needs an interrupt makes the first waits of the loop
        raise it (raised_in_loop()).  The interrupt then comes in the loop
        at each speed of the host.  A timer could fire before the loop
        starts, and the group would then get no signal.
      * A command that must ignore the signals of an interrupt and of the
        limit ignores them from its start: the test ignores them itself
        when it starts the command, and an exec keeps an ignored signal.
      * The case of the bound of forked() makes each action wait for a second
        action, so the overlap does not depend on the time of a fork.
      * A case tells a command that a signal ended from a command that ended
        alone by its exit, and not by a time.  The case of the stream of
        many lines bounds the CPU time of this process, and not the wall
        time.
"""

import contextlib
import os
import selectors
import signal
import sys
import tempfile
import threading
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from unittest import mock

from assay.core import jobserver, proc, testing
from assay.core.exit import ExitCode

PYTHON = sys.executable
UTILS = str(Path(__file__).resolve().parents[3])
# A limit that no case of this test reaches when the host works, in seconds.
SAFE_S = 60.0
# The actions of the case of the bound of forked(), the time that an action looks for a third action after it saw a
# second one, and the interval of its look, in seconds (paired_action()).
PAIRED_ACTIONS = 6
PAIR_LOOK_S = 0.05
PAIR_STEP_S = 0.005


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


def run_cases(expect: Callable[[str, bool], None]) -> None:
    """Check a captured run: the exit, the outputs, the times and the raw status."""
    done = proc.run(
        proc.Command(python("import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"), timeout_s=SAFE_S)
    )
    expect(
        "a run gives the exit and each captured output",
        done.exit == 3 and done.out == "out\n" and done.err == "err\n" and done.stdout == b"out\n",
    )
    expect(
        "a run gives the wall time, the CPU time, the peak memory and the raw wait status",
        done.wall_s > 0
        and done.cpu_s >= 0
        and done.peak_kb > 0
        and os.WIFEXITED(done.wait_status)
        and os.WEXITSTATUS(done.wait_status) == 3,
    )
    problem = refuses(lambda: proc.require(done), proc.ToolFailed)
    expect(
        "require() refuses an exit that is not 0, with the command, the exit and the end of stderr",
        isinstance(problem, proc.ToolFailed)
        and "exits with 3" in str(problem)
        and str(problem).endswith(": err")
        and problem.completed is done,
    )
    passed = proc.run(proc.Command(["sh", "-c", "exit 0"], timeout_s=SAFE_S))
    expect("require() returns a run whose exit is 0", proc.require(passed) is passed)
    killed = proc.run(proc.Command(["sh", "-c", "kill -TERM $$"], timeout_s=SAFE_S))
    expect("a command that a signal ends gives minus the signal", killed.exit == -signal.SIGTERM)
    expect(
        "require() says the signal",
        "ends on the signal 15" in str(refuses(lambda: proc.require(killed), proc.ToolFailed)),
    )
    text = proc.run(
        proc.Command(python("import sys; sys.stdout.buffer.write(b'a\\r\\nb\\rc\\xff\\n')"), timeout_s=SAFE_S)
    )
    expect(
        "out decodes UTF-8 with U+FFFD and changes \\r\\n and \\r to \\n",
        text.out == "a\nb\nc\ufffd\n" and text.stdout == b"a\r\nb\rc\xff\n",
    )
    expect(
        "decoded() with surrogateescape keeps each byte that is not UTF-8",
        proc.decoded(text.stdout, "surrogateescape").encode("utf-8", "surrogateescape") == b"a\nb\nc\xff\n",
    )
    directory = proc.run(proc.Command(["sh", "-c", "pwd"], timeout_s=SAFE_S, cwd=Path("/")))
    expect("a run takes its working directory", directory.out == "/\n")


def start_failure_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the refusals before a start, and a start that fails."""
    missing = refuses(
        lambda: proc.run(proc.Command(["no-such-program-of-the-tree"], timeout_s=SAFE_S)), proc.CannotStart
    )
    expect(
        "a program that does not exist gives CannotStart with the error of the system",
        isinstance(missing, proc.CannotStart)
        and isinstance(missing.error, FileNotFoundError)
        and "no-such-program-of-the-tree" in str(missing),
    )
    no_directory = refuses(
        lambda: proc.run(proc.Command(["true"], timeout_s=SAFE_S, cwd="/no/such/directory")), proc.CannotStart
    )
    expect(
        "a working directory that does not exist gives CannotStart that names it",
        isinstance(no_directory, proc.CannotStart) and "/no/such/directory" in str(no_directory),
    )
    for name, action in (
        ("an empty command", lambda: proc.Command([], timeout_s=1)),
        ("an empty word", lambda: proc.Command(["true", ""], timeout_s=1)),
        ("a limit of 0", lambda: proc.Command(["true"], timeout_s=0)),
        ("an infinite limit", lambda: proc.Command(["true"], timeout_s=float("inf"))),
        ("a negative grace", lambda: proc.Command(["true"], timeout_s=1, grace_s=-1)),
        ("a text as the standard input", lambda: proc.Command(["true"], timeout_s=1, stdin="x")),  # type: ignore[arg-type]
        ("MERGE as the destination of stdout", lambda: proc.Command(["true"], timeout_s=1, stdout=proc.Output.MERGE)),
        ("STREAM as the destination of stderr", lambda: proc.Command(["true"], timeout_s=1, stderr=proc.Output.STREAM)),
        ("an unknown destination", lambda: proc.Command(["true"], timeout_s=1, stdout="file.txt")),
        ("an unknown stop word", lambda: proc.Command(["true"], timeout_s=1, stop="kill")),
        ("a stream in run()", lambda: proc.run(proc.Command(["true"], timeout_s=1, stdout=proc.Output.STREAM))),
    ):
        expect(f"the module refuses {name}", refuses(action, ValueError) is not None)


def limit_cases(expect: Callable[[str, bool], None], scratch: Path) -> None:
    """Check the limit: SIGTERM to the group, SIGKILL after the grace, and no process of the group after it."""
    # The shell writes the pid of its child and then its ready file.  The wait starts after the ready file, so the
    # limit counts after the output (THE START OF A CHILD).
    ready = scratch / "limit-ready"
    running = proc.start(
        proc.Command(["sh", "-c", 'sleep 60 & echo $!; : > "$0"; wait', str(ready)], timeout_s=0.2, grace_s=0.2)
    )
    is_ready = wait_for_file(ready)
    problem = refuses(running.wait, proc.TimedOut)
    child_pid = (
        int(problem.completed.out.split()[0])
        if isinstance(problem, proc.TimedOut) and problem.completed and problem.completed.out.split()
        else -1
    )
    # The shell ends on SIGTERM, or on SIGKILL when it is slow to end in the grace.  A shell that the limit does not
    # stop ends after 60 s with the exit 0.
    expect(
        "a command past its limit gives TimedOut with the output that it gave, and a signal of the limit ends it",
        is_ready
        and isinstance(problem, proc.TimedOut)
        and child_pid > 0
        and problem.completed is not None
        and problem.completed.exit in (-signal.SIGTERM, -signal.SIGKILL),
    )
    expect("no process of the group runs after the limit", child_pid > 0 and testing.is_gone(child_pid))
    # The ready file comes after the trap, and sleep keeps the ignored signal across the exec.
    ready = scratch / "stubborn-ready"
    stubborn = proc.start(
        proc.Command(["sh", "-c", "trap '' TERM; : > \"$0\"; exec sleep 60", str(ready)], timeout_s=0.2, grace_s=0.1)
    )
    is_ready = wait_for_file(ready)
    problem = refuses(stubborn.wait, proc.TimedOut)
    expect(
        "a command that ignores SIGTERM gets SIGKILL after the grace",
        is_ready
        and isinstance(problem, proc.TimedOut)
        and problem.completed is not None
        and problem.completed.exit == -signal.SIGKILL,
    )

    class Interrupt(Exception):
        """The error that the case raises in the loop of the run."""

    running = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S))
    with raised_in_loop(running, Interrupt):
        caught = refuses(running.wait, Interrupt)
    expect(
        "an interrupt of the wait stops the group and then goes on",
        isinstance(caught, Interrupt) and testing.is_gone(running.pid),
    )
    stopped = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S)).stop()
    expect("stop() ends the group with SIGTERM and gives the result", stopped.exit == -signal.SIGTERM)
    poll_cases(expect)


def poll_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the bounded loop: poll() gives None while the command runs, and the result or the limit at its end."""
    running = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S))
    started = time.monotonic()
    first = running.poll(0.2)
    waited_s = time.monotonic() - started
    stopped = running.stop()
    # poll() waits at least its time.  A poll() that waits for the end of the command gives a result, and not None.
    expect(
        f"poll() of a command that runs gives None after its time, and stop() then ends the group: {waited_s:.2f} s",
        first is None and waited_s >= 0.15 and stopped.exit == -signal.SIGTERM and testing.is_gone(running.pid),
    )
    quick = proc.start(proc.Command(python("import sys; print('done'); sys.exit(4)"), timeout_s=SAFE_S))
    found = None
    deadline = time.monotonic() + SAFE_S
    while found is None and time.monotonic() < deadline:
        found = quick.poll(0.05)
    expect(
        "poll() gives the result of a command that ended, with its exit and its output",
        found is not None and found.exit == 4 and found.out == "done\n",
    )
    limited = proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=0.2, grace_s=0.1))

    def poll_to_end() -> None:
        while limited.poll(0.05) is None:
            pass

    problem = refuses(poll_to_end, proc.TimedOut)
    expect(
        "poll() of a command past its limit gives TimedOut, as wait() does",
        isinstance(problem, proc.TimedOut) and testing.is_gone(limited.pid),
    )


@contextlib.contextmanager
def raised_in_loop(running: proc.Running, error: type[BaseException], count: int = 1) -> Iterator[None]:
    """Make the first `count` waits of the loop of a run raise `error` in the block, as a signal in a wait does.

    A wait of the loop is one select of the run (assay.core.proc, AN INTERRUPT).  The error then comes in the loop
    at each speed of the host.  After the block, the selector has its own select again.
    """
    selector = running._selector
    real_select = selector.select
    raised = 0

    def select(timeout: float | None = None) -> list[tuple[selectors.SelectorKey, int]]:
        nonlocal raised
        if raised < count:
            raised += 1
            raise error
        return real_select(timeout)

    with mock.patch.object(selector, "select", select):
        yield


def interrupted(running: proc.Running, action: Callable[[], object], count: int = 1) -> bool:
    """Run an action while the first `count` waits of the loop of a run raise KeyboardInterrupt, as Ctrl-C does.

    Returns:
        True when the KeyboardInterrupt reached the caller
    """
    with raised_in_loop(running, KeyboardInterrupt, count):
        try:
            action()
        except KeyboardInterrupt:
            return True
    return False


@contextlib.contextmanager
def ignored(*numbers: int) -> Iterator[None]:
    """Ignore the signals in this process for the block, and give each one its earlier handler after it.

    A child that the block starts keeps each ignored signal across its exec (THE START OF A CHILD).
    """
    previous = [(number, signal.signal(number, signal.SIG_IGN)) for number in numbers]
    try:
        yield
    finally:
        for number, handler in previous:
            signal.signal(number, handler)


def wait_for_file(path: Path, wait_s: float = SAFE_S) -> bool:
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
    # The child blocks SIGINT before its ready file, and then waits for it for 60 s.  A SIGINT that comes before the
    # wait stays pending, and the wait takes it.  With a handler, a SIGINT just before time.sleep() runs the handler
    # only after the sleep, and a SIGINT in a print() makes the print of the handler fail as a reentrant call.
    polite = python(
        "import os, signal, sys\n"
        "signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})\n"
        "open(sys.argv[2], 'w').close()\n"
        "os.write(1, b'ready\\n')\n"
        "if signal.sigtimedwait({signal.SIGINT}, 60) is not None:\n"
        "    open(sys.argv[1], 'w').write('summary\\n')\n"
        "    os.write(1, b'summary\\n')\n"
        "    sys.exit(7)"
    )
    # After the interrupt, stop() gives the result of the command that ended, and sends no signal.  The exit tells the
    # end of the command in its own way from an end on a signal, or from the end of its sleep.
    running = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S))
    is_ready = wait_for_file(ready)
    reached = interrupted(running, running.wait)
    expect(
        "an interrupt of the wait sends SIGINT to the group, waits for the end of the command, and then goes on",
        is_ready
        and reached
        and summary.is_file()
        and summary.read_text() == "summary\n"
        and running.stop().exit == 7
        and testing.is_gone(running.pid),
    )
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    streamed = proc.start(
        proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S, stdout=proc.Output.STREAM)
    )
    first = next(streamed)
    rest: list[str] = []
    reached = interrupted(streamed, lambda: rest.extend(streamed))
    expect(
        "an interrupt of a stream gives the lines that the command writes after SIGINT, and then goes on",
        first == "ready\n" and rest == ["summary\n"] and reached and testing.is_gone(streamed.pid),
    )
    deaf = proc.start(
        proc.Command(
            python(
                "import signal, time\nsignal.signal(signal.SIGINT, signal.SIG_IGN)\n"
                "print('ready', flush=True)\ntime.sleep(60)"
            ),
            timeout_s=SAFE_S,
            stdout=proc.Output.STREAM,
        )
    )
    next(deaf)
    reached = interrupted(deaf, deaf.wait, count=2)
    expect(
        "a second interrupt sends SIGKILL to a group that ignores SIGINT, and then goes on",
        reached and deaf.stop().exit == -signal.SIGKILL and testing.is_gone(deaf.pid),
    )
    # sleep ignores the two signals from its exec, so only the SIGKILL of the limit ends it (THE START OF A CHILD).
    with ignored(signal.SIGINT, signal.SIGTERM):
        late = proc.start(proc.Command(["sleep", "60"], timeout_s=0.5, grace_s=0.1))
    reached = interrupted(late, late.wait)
    expect(
        "after one interrupt, the limit still stops a command that ignores SIGINT and SIGTERM",
        reached and late.stop().exit == -signal.SIGKILL and testing.is_gone(late.pid),
    )
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    gentle = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=SAFE_S, stop=proc.Stop.INTERRUPT))
    is_ready = wait_for_file(ready)
    stopped = gentle.stop()
    expect(
        "stop() of a command with the stop word interrupt sends SIGINT, and the command ends in its own way",
        is_ready and stopped.exit == 7 and summary.is_file() and summary.read_text() == "summary\n",
    )
    summary.unlink(missing_ok=True)
    ready.unlink(missing_ok=True)
    timed = proc.start(proc.Command([*polite, str(summary), str(ready)], timeout_s=0.5, stop=proc.Stop.INTERRUPT))
    is_ready = wait_for_file(ready)
    problem = refuses(timed.wait, proc.TimedOut)
    expect(
        "the limit of a command with the stop word interrupt sends SIGINT first, and the command ends in its own way",
        is_ready
        and isinstance(problem, proc.TimedOut)
        and problem.completed is not None
        and problem.completed.exit == 7
        and summary.is_file()
        and "SIGINT" in str(problem),
    )


def stream_cases(expect: Callable[[str, bool], None], scratch: Path) -> None:
    """Check the input, the large outputs, the files, the merge and a stream of lines."""
    data = bytes(range(256)) * 8192
    echoed = proc.run(proc.Command(["cat"], timeout_s=SAFE_S, stdin=data))
    expect("2 MB of input through cat comes back whole, and no pipe stops the command", echoed.stdout == data)
    both = proc.run(
        proc.Command(
            python(
                "import sys\nfor _ in range(4096):\n    sys.stdout.write('o' * 255 + '\\n')\n"
                "    sys.stderr.write('e' * 255 + '\\n')"
            ),
            timeout_s=SAFE_S,
        )
    )
    expect(
        "1 MB on each output at the same time comes back whole",
        len(both.stdout) == 1 << 20 and len(both.stderr) == 1 << 20,
    )
    log = scratch / "log.txt"
    merged = proc.run(
        proc.Command(
            python("import sys; print('one', flush=True); print('two', file=sys.stderr)"),
            timeout_s=SAFE_S,
            stdout=log,
            stderr=proc.Output.MERGE,
        )
    )
    expect(
        "a path gets stdout, MERGE adds stderr, and the result keeps no output",
        log.read_text() == "one\ntwo\n" and merged.stdout == b"" and merged.stderr == b"",
    )
    log.write_text("an earlier log that the run replaces, longer than the new one\n")
    proc.run(proc.Command(["sh", "-c", "echo new"], timeout_s=SAFE_S, stdout=log))
    expect("a path gets a new file", log.read_text() == "new\n")
    quiet = proc.run(
        proc.Command(
            python("import sys; print('x'); print('y', file=sys.stderr)"),
            timeout_s=SAFE_S,
            stdout=proc.Output.DISCARD,
            stderr=proc.Output.DISCARD,
        )
    )
    expect("DISCARD keeps no output", quiet.stdout == b"" and quiet.stderr == b"" and quiet.exit == 0)
    inherited = proc.run(
        proc.Command(
            [
                *python(
                    "import sys; sys.path.insert(0, sys.argv[1]); from assay.core import proc\n"
                    "proc.run(proc.Command(['sh', '-c', 'echo inner'], timeout_s=60, stdout=proc.Output.INHERIT))"
                ),
                UTILS,
            ],
            timeout_s=SAFE_S,
        )
    )
    expect("INHERIT writes to the stream of the caller", inherited.out == "inner\n")
    running = proc.start(
        proc.Command(["sh", "-c", "printf 'a\\nbb\\nlast'"], timeout_s=SAFE_S, stdout=proc.Output.STREAM)
    )
    lines = list(running)
    expect("a stream gives each line with its \\n, and the last line with no \\n", lines == ["a\n", "bb\n", "last"])
    expect("the result of a stream that the loop read to its end holds no output", running.wait().stdout == b"")
    unread = proc.start(proc.Command(["sh", "-c", "printf 'a\\nb\\n'"], timeout_s=SAFE_S, stdout=proc.Output.STREAM))
    expect("the result of a stream holds the lines that the loop did not give", unread.wait().stdout == b"a\nb\n")
    # The shell writes its line and then its ready file.  The loop starts after the ready file, so the line is in
    # the pipe when the limit counts (THE START OF A CHILD).
    ready = scratch / "stream-ready"
    slow = proc.start(
        proc.Command(
            ["sh", "-c", 'echo first; : > "$0"; sleep 60', str(ready)],
            timeout_s=0.2,
            grace_s=0.1,
            stdout=proc.Output.STREAM,
        )
    )
    is_ready = wait_for_file(ready)
    first = next(slow)
    expect(
        "the limit counts while the caller reads the lines of a stream",
        is_ready and first == "first\n" and refuses(lambda: list(slow), proc.TimedOut) is not None,
    )
    expect(
        "a loop over a run with no stream is refused",
        refuses(lambda: iter(proc.start(proc.Command(["true"], timeout_s=SAFE_S))), ValueError) is not None,
    )
    many = proc.start(
        proc.Command(
            python("import sys\nfor index in range(200000):\n    print(index)"),
            timeout_s=SAFE_S,
            stdout=proc.Output.STREAM,
        )
    )
    # The CPU time of this process counts from the first line.  The load of the host increases the wall time of the
    # loop much more than its CPU time.  A loop that is quadratic in the lines runs much longer than the bound.
    count = 0 if next(many, None) is None else 1
    started = time.process_time()
    count += sum(1 for _line in many)
    expect(
        "a stream of 200,000 lines gives each line, in a CPU time that is linear in the lines",
        count == 200000 and many.wait().exit == 0 and time.process_time() - started < 10,
    )


def pidfd_failure_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that a kernel with no pidfd gives CannotStart, and that the start leaves no process and no pipe."""
    real = os.pidfd_open
    before = set(os.listdir("/proc/self/fd"))

    def refused(_pid: int, _flags: int = 0) -> int:
        raise OSError(38, "Function not implemented")

    os.pidfd_open = refused
    try:
        problem = refuses(
            lambda: proc.start(proc.Command(["sh", "-c", "sleep 60"], timeout_s=SAFE_S, stdin=b"x")), proc.CannotStart
        )
    finally:
        os.pidfd_open = real
    after = set(os.listdir("/proc/self/fd"))
    expect(
        "a kernel with no pidfd gives CannotStart that names Linux 5.3",
        isinstance(problem, proc.CannotStart) and "Linux 5.3" in str(problem),
    )
    expect("the failed start leaves no open pipe", after <= before)


def environment_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the environment: no repository variable reaches a child, and the overrides do."""
    show = python("import os, sys; print(os.environ.get(sys.argv[1], '-'))")
    saved = os.environ.get("GIT_DIR")
    os.environ["GIT_DIR"] = "/no/such/repository"
    try:
        inherited = proc.run(proc.Command([*show, "GIT_DIR"], timeout_s=SAFE_S)).out
        given = proc.run(
            proc.Command([*show, "GIT_DIR"], timeout_s=SAFE_S, env=dict(os.environ, GIT_WORK_TREE="/x"))
        ).out
        override = proc.environment({"GIT_INDEX_FILE": "/x", "CRUCIBLE_PROC_PLANTED": "1"})
    finally:
        if saved is None:
            os.environ.pop("GIT_DIR")
        else:
            os.environ["GIT_DIR"] = saved
    expect("a child of this environment gets no GIT_DIR", inherited == "-\n")
    expect("a child of a full mapping gets no GIT_DIR", given == "-\n")
    expect(
        "environment() keeps an override and drops a repository variable",
        override.get("CRUCIBLE_PROC_PLANTED") == "1" and not set(override) & set(proc.REPOSITORY_VARIABLES),
    )
    planted = proc.run(
        proc.Command(
            [*show, "CRUCIBLE_PROC_PLANTED"], timeout_s=SAFE_S, env=proc.environment({"CRUCIBLE_PROC_PLANTED": "yes"})
        )
    ).out
    expect("a child gets the override", planted == "yes\n")


def forked_writes() -> int:
    """A forked action that writes one line to each standard descriptor."""
    os.write(1, b"planted forked stdout\n")
    os.write(2, b"planted forked stderr\n")
    return ExitCode.OK


def forked_fails() -> int:
    """A forked action that gives the status FINDINGS."""
    return ExitCode.FINDINGS


def forked_wrong_status() -> int:
    """A forked action that returns a status that is not a member of ExitCode."""
    return 9


def forked_raises() -> int:
    """A forked action that raises an exception."""
    raise RuntimeError("planted forked exception")


def forked_killed() -> int:
    """A forked action that SIGKILL ends."""
    os.kill(os.getpid(), signal.SIGKILL)
    return ExitCode.OK


def forked_child() -> int:
    """A forked action with a child that writes to the standard output of the action."""
    proc.run(
        proc.Command([PYTHON, "-c", "print('planted forked child')"], timeout_s=SAFE_S, stdout=proc.Output.INHERIT)
    )
    return ExitCode.OK


def paired_action(scratch: Path, action_count: int) -> Callable[[], int]:
    """Return a forked action that waits for a second action that runs at the same time (THE START OF A CHILD).

    The action puts a mark of its pid into the directory running, and one into the directory started.  It looks at
    the count of the marks of running until it sees a second mark, and then for PAIR_LOOK_S more, so a third action
    at the same time is visible too.  The last action of the call has no partner, so it stops when each action
    started.  The action writes the largest count that it saw to the file seen.txt, and it removes its mark before
    it returns.  forked() gives its slot back only after that, so no action sees the mark of an action that ended.
    """
    running = scratch / "running"
    started = scratch / "started"

    def action() -> int:
        mark = running / str(os.getpid())
        mark.touch()
        (started / str(os.getpid())).touch()
        largest = 0
        partner_at: float | None = None
        deadline = time.monotonic() + SAFE_S
        while time.monotonic() < deadline:
            largest = max(largest, len(os.listdir(running)))
            if largest >= 2 and partner_at is None:
                partner_at = time.monotonic()
            if partner_at is not None and time.monotonic() - partner_at >= PAIR_LOOK_S:
                break
            if partner_at is None and len(os.listdir(started)) == action_count:
                break
            time.sleep(PAIR_STEP_S)
        with (scratch / "seen.txt").open("a", encoding="utf-8") as stream:
            stream.write(f"{largest}\n")
        mark.unlink()
        return ExitCode.OK

    return action


def forked_cases(expect: Callable[[str, bool], None], scratch: Path) -> None:
    """Check forked(): the status and the output of each action, the bound of the jobs, and the thread check."""
    with jobserver.serving(2) as server, jobserver.connected(server.path) as client:
        actions = [forked_writes, forked_fails, forked_raises, forked_killed, forked_child, forked_wrong_status]
        ends = proc.forked(actions, client)
        outputs = [end.output.decode() for end in ends]
        expect(
            "forked() gives the status of each action in sequence: the ExitCode of the action, TOOL_FAILED after an "
            "exception or a status that is not an ExitCode, and minus the signal of a child that a signal ends: "
            f"{[end.status for end in ends]}",
            [end.status for end in ends]
            == [
                ExitCode.OK,
                ExitCode.FINDINGS,
                ExitCode.TOOL_FAILED,
                -signal.SIGKILL,
                ExitCode.OK,
                ExitCode.TOOL_FAILED,
            ],
        )
        expect(
            "the output of an action holds its two standard streams, the traceback of its exception, and the output "
            "of its own child",
            outputs[0] == "planted forked stdout\nplanted forked stderr\n"
            and "Traceback" in outputs[2]
            and "planted forked exception" in outputs[2]
            and outputs[4] == "planted forked child\n",
        )
        expect(
            "a child ends with os._exit, so the temporary directory of the parent stays",
            scratch.is_dir(),
        )
        (scratch / "running").mkdir()
        (scratch / "started").mkdir()
        proc.forked([paired_action(scratch, PAIRED_ACTIONS)] * PAIRED_ACTIONS, client)
        seen = [int(line) for line in (scratch / "seen.txt").read_text(encoding="utf-8").splitlines()]
        expect(
            f"with 2 jobs, {PAIRED_ACTIONS} actions run two at a time and never more: {max(seen, default=0)} at the most",
            len(seen) == PAIRED_ACTIONS and max(seen) == 2,
        )
        slots = [client.try_take(), client.try_take(), client.try_take()]
        expect(
            "forked() gives each slot back: the implicit slot and the one token are free, and no third slot",
            slots[0] is not None and slots[1] is not None and slots[2] is None,
        )
        for slot in slots:
            if slot is not None:
                client.give(slot)
        waiting = threading.Event()
        thread = threading.Thread(target=waiting.wait)
        thread.start()
        try:
            refused = refuses(lambda: proc.forked([forked_writes], client), ValueError)
        finally:
            waiting.set()
            thread.join()
        expect("forked() refuses a process with a second thread", refused is not None)


def import_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that the import of the module costs little: it imports no heavy module (WHY THE MODULE IMPORTS LITTLE)."""
    heavy = ("subprocess", "selectors", "signal", "dataclasses", "enum", "typing", "pathlib", "collections")
    listed = proc.run(
        proc.Command(
            [
                *python(
                    "import sys; before = set(sys.modules); sys.path.insert(0, sys.argv[1]); import assay.core.proc\n"
                    "print(' '.join(sorted(set(sys.modules) - before)))"
                ),
                UTILS,
            ],
            timeout_s=SAFE_S,
        )
    ).out.split()
    expect(
        f"an import of the module with -S imports none of {', '.join(heavy)}",
        "assay.core.proc" in listed and not set(heavy) & set(listed),
    )


def job_count_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that `loop jobs` prints DEFAULT_JOBS and refuses an argument (main(), THE JOB COUNT)."""
    status, said = testing.captured(lambda: proc.main([]))
    expect("main() prints DEFAULT_JOBS", status == 0 and said == f"{proc.DEFAULT_JOBS}\n")
    status, said = testing.captured(lambda: proc.main(["extra"]))
    expect("main() refuses an argument with the status 2", status == 2 and "usage: loop jobs" in said)


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
    with tempfile.TemporaryDirectory(prefix="assay-proc-") as scratch:
        limit_cases(expect, Path(scratch))
        stream_cases(expect, Path(scratch))
        interrupt_cases(expect, Path(scratch))
        forked_cases(expect, Path(scratch))
    environment_cases(expect)
    pidfd_failure_cases(expect)
    import_cases(expect)
    job_count_cases(expect)
    if failures:
        print(f"tests.core.test_proc: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_proc: every case holds.")
    return 0
