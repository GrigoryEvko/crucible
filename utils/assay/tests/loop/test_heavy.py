"""The test of the lock of the heavy steps and of the memory wait (assay.loop.heavy).

`guard selftest tests.loop.test_heavy` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases read a planted file of the memory.  They give the wait a planted
reader and a pause that does not sleep.  They make sure that a linked work
tree of a planted repository finds the lock of the main work tree.  They also
make sure that the lock is exclusive while a step holds it.  A session of the
gate must let a step of the gate go first.  A different step must wait for
the session, also when it gets the lock after the session starts.
"""

import fcntl
import tempfile
import threading
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import IO

from assay.core import git, proc, testing
from assay.loop import heavy


def reader(values: list[float | None]) -> heavy.Reader:
    """Return a reader of the available memory that gives the values in sequence, and then the last value."""
    remaining: Iterator[float | None] = iter(values)
    last: list[float | None] = [None]

    def read() -> float | None:
        """Give the next value."""
        last[0] = next(remaining, last[0])
        return last[0]

    return read


def memory_cases(expect: testing.Expect) -> None:
    """Hold the read of the available memory and the wait for a level."""
    with tempfile.TemporaryDirectory(prefix="heavy-memory-") as scratch_text:
        scratch = Path(scratch_text)
        planted = scratch / "meminfo"
        planted.write_text("MemTotal:       33554432 kB\nMemAvailable:   16777216 kB\n", encoding="ascii")
        expect("the read gives the field MemAvailable in GB", heavy.available_gb(planted) == 16.0)
        planted.write_text("MemTotal:       33554432 kB\n", encoding="ascii")
        expect(
            "a file with no field MemAvailable, and a file that does not exist, give no value",
            heavy.available_gb(planted) is None and heavy.available_gb(scratch / "missing") is None,
        )
    pauses: list[float] = []
    waited, value = heavy.wait_for_memory(16.0, reader([20.0]), pauses.append)
    expect(
        "with the level from the first sample, the wait takes the samples of the rule and gives the last value",
        waited == (heavy.SAMPLES - 1) * heavy.INTERVAL_S and value == 20.0 and len(pauses) == heavy.SAMPLES - 1,
    )
    pauses.clear()
    waited, value = heavy.wait_for_memory(16.0, reader([20.0, 5.0, 20.0]), pauses.append)
    expect(
        "a sample below the level starts the count again",
        waited == (heavy.SAMPLES + 1) * heavy.INTERVAL_S and value == 20.0,
    )
    waited, value = heavy.wait_for_memory(16.0, reader([None]), pauses.append)
    expect("a reader with no value ends the wait at once", waited == 0.0 and value is None)


def lock_cases(expect: testing.Expect) -> None:
    """Hold the position of the lock file and the exclusion of the lock."""
    with tempfile.TemporaryDirectory(prefix="heavy-lock-") as work_text:
        work = Path(work_text)
        main_tree = work / "main"
        main_tree.mkdir()
        git.init(main_tree)
        (main_tree / "file.txt").write_text("one\n", encoding="utf-8")
        git.commit_all(main_tree, "one")
        linked = work / "linked"
        git.run(main_tree, "worktree", "add", "-q", "--detach", str(linked))
        (main_tree / "sub").mkdir()
        expected = main_tree.resolve() / heavy.LOCK_FILE
        expect(
            "the main work tree, a directory of it and a linked work tree find the one lock of the main work tree",
            heavy.lock_path(main_tree) == expected
            and heavy.lock_path(main_tree / "sub") == expected
            and heavy.lock_path(linked) == expected,
        )
        outside = work / "outside"
        outside.mkdir()
        expect(
            "a directory outside a repository finds the lock in itself",
            heavy.lock_path(outside) == outside / heavy.LOCK_FILE,
        )
        held: list[bool] = []
        with (
            heavy.heavy_step(16.0, linked, reader([32.0]), lambda _seconds: None),
            open(expected, "a", encoding="ascii") as other,
        ):
            try:
                fcntl.flock(other, fcntl.LOCK_EX | fcntl.LOCK_NB)
                held.append(False)
            except BlockingIOError:
                held.append(True)
        with open(expected, "a", encoding="ascii") as other:
            fcntl.flock(other, fcntl.LOCK_EX | fcntl.LOCK_NB)
            held.append(True)
        expect("a heavy step holds the lock exclusively, and the lock is free after the step", held == [True, True])
        expect(
            "the main work tree, a directory of it and a linked work tree find the one lock of a session of the gate",
            heavy.gate_path(linked) == main_tree.resolve() / heavy.GATE_FILE
            and heavy.gate_path(main_tree / "sub") == main_tree.resolve() / heavy.GATE_FILE,
        )


def is_free(path: Path) -> bool:
    """Tell if a different holder can take the exclusive lock of a file at this time."""
    return not heavy.is_held(path)


def wait_until(condition: Callable[[], bool], limit_s: float = 10.0) -> bool:
    """Wait in steps of 10 ms until a condition holds, at most for the limit, and tell if it holds."""
    waited = 0.0
    while not condition() and waited < limit_s:
        time.sleep(0.01)
        waited += 0.01
    return condition()


def gate_cases(expect: testing.Expect) -> None:
    """Hold a session of the gate: a step of the gate goes first, and each other step waits."""
    with tempfile.TemporaryDirectory(prefix="heavy-gate-") as work_text:
        root = Path(work_text)
        entered = threading.Event()
        # The check after the lock found a session: the step held the lock of the heavy steps at that time.
        found_session = threading.Event()

        def no_pause(_seconds: float) -> None:
            """A pause that does not wait."""

        def is_session(gate_handle: IO[str]) -> bool:
            """Tell if a session runs, as the step does after it gets the lock, and mark a session that runs."""
            is_running = heavy.is_gate_running(gate_handle)
            if is_running:
                found_session.set()
            return is_running

        def other_step() -> None:
            """Run a heavy step that is not a step of the gate, and mark the entry into its block."""
            with heavy.heavy_step(16.0, root, reader([32.0]), no_pause, is_session=is_session):
                entered.set()

        with heavy.gate_session(root):
            with heavy.heavy_step(16.0, root, reader([32.0]), no_pause, is_gate_step=True):
                expect("a step of the gate takes the lock while its session runs", not is_free(heavy.lock_path(root)))
            worker = threading.Thread(target=other_step, daemon=True)
            worker.start()
            expect(
                "a different step waits while a session of the gate runs, and it holds no lock of the heavy steps",
                not entered.wait(0.3) and is_free(heavy.lock_path(root)),
            )
        worker.join(10.0)
        expect("the step goes on after the session ends", entered.is_set())
        entered.clear()
        with open(heavy.lock_path(root), "a", encoding="ascii") as busy:
            fcntl.flock(busy, fcntl.LOCK_EX)
            worker = threading.Thread(target=other_step, daemon=True)
            worker.start()
            expect("a step waits for the lock of a step that runs", not entered.wait(0.3))
            with heavy.gate_session(root):
                fcntl.flock(busy, fcntl.LOCK_UN)
                expect(
                    "a step that gets the lock after a session started finds the session, frees the lock for the "
                    "gate, and waits",
                    found_session.wait(10.0)
                    and wait_until(lambda: is_free(heavy.lock_path(root)))
                    and not entered.wait(0.3),
                )
            worker.join(10.0)
        expect("that step goes on after the session ends", entered.is_set())


def exit_cases(expect: testing.Expect) -> None:
    """Hold the exit of a command and the usage errors of the command line."""
    command = proc.Command(["true"], timeout_s=1.0)
    signalled = proc.Completed(command, 9, b"", b"", 0.0, 0.0, 0.0, 0)
    exited = proc.Completed(command, 3 << 8, b"", b"", 0.0, 0.0, 0.0, 0)
    expect(
        "a command that ends on a signal gives 128 plus the signal, and an exit gives its status",
        heavy.exit_of(signalled) == heavy.SIGNAL_EXIT_BASE + 9 and heavy.exit_of(exited) == 3,
    )
    for name, words in (("no --", ["true"]), ("no command after --", ["--"])):

        def run(words: list[str] = words) -> int:
            """Run the command line of the case."""
            return heavy.main(words)

        status, _ = testing.captured(run)
        expect(f"the command line with {name} is a usage error", status == 2)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (memory_cases, lock_cases, gate_cases, exit_cases))
