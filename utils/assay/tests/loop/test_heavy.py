"""The test of the lock of the heavy steps and of the memory wait (assay.loop.heavy).

`guard selftest tests.loop.test_heavy` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases read a planted file of the memory.  They give the wait a planted
reader and a pause that does not sleep.  They make sure that a linked work
tree of a planted repository finds the lock of the main work tree.  They also
make sure that the lock is exclusive while a step holds it.
"""

import fcntl
import tempfile
from collections.abc import Iterator
from pathlib import Path

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
    return testing.run(__name__.removeprefix("assay."), argv, (memory_cases, lock_cases, exit_cases))
