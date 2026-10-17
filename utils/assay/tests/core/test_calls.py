"""The test of the call profile (assay.core.calls).

`guard selftest tests.core.test_calls` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

Each case starts a profile in a child process of its own.  The profile of a
case then does not stay in the process of the test.  A case reads the file
that finish() writes with the reader of `loop audit`.  A run of the test can
have a profile of its own, such as in a traced gate.  Each child then ends
the profile of its parent first (restart_profile()).
"""

import os
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import calls, proc, testing
from assay.core import meter as cost_meter
from assay.core.exit import ExitCode
from assay.loop import audit


class OneSlot:
    """A planted pool of slots for forked(): each take gives a slot."""

    def try_take(self) -> int | None:
        """Give a slot."""
        return 1

    def give(self, slot: int) -> None:
        """Take a slot back."""

    def fileno(self) -> int:
        """Return a descriptor for poll(2).  forked() reads it only when no slot is free."""
        return 0


def planted_work(count: int) -> int:
    """Do a small loop, the work that the profile must count."""
    return sum(range(count))


def child_work() -> int:
    """Do the work of a forked child, and return OK."""
    planted_work(10)
    return ExitCode.OK


def in_child(action: Callable[[], int]) -> tuple[int, str]:
    """Run an action in a child process with forked(), and return its status and its output."""
    end = proc.forked([action], OneSlot())[0]
    return end.status, proc.decoded(end.output)


def restart_profile(directory: Path | None) -> None:
    """End the profile that this child got from its parent, and start the profile of a case in `directory`, or none."""
    calls.finish()
    if directory is None:
        os.environ.pop(cost_meter.CALL_PROFILE_ENV, None)
    else:
        os.environ[cost_meter.CALL_PROFILE_ENV] = str(directory)
    calls.start()


def calls_of(profile: audit.CallProfile, name: str) -> list[int]:
    """Return the call count of each row of a function in one call profile."""
    return [row[3] for row in profile.functions if row[2] == name]


def kept_cases(expect: testing.Expect) -> None:
    """Check the functions that a profile keeps (THE RECORD)."""
    expect(
        "a function of the package goes into the file, also with no time",
        calls.is_kept(f"{cost_meter.UTILS}/assay/core/rows.py", 0.0),
    )
    expect(
        "a function of a different file with a small cumulative time does not go into the file",
        not calls.is_kept("/usr/lib64/python3.14/json/decoder.py", calls.KEEP_S / 2),
    )
    expect(
        "a function of a different file with KEEP_S of cumulative time goes into the file",
        calls.is_kept("<frozen importlib._bootstrap>", calls.KEEP_S),
    )


def record_cases(expect: testing.Expect) -> None:
    """Check the file of one profile, a second finish(), and a process with no variable (THE RECORD)."""
    with tempfile.TemporaryDirectory(prefix="assay-calls-") as scratch:
        directory = Path(scratch) / "profiles"

        def profiled() -> int:
            restart_profile(directory)
            planted_work(1000)
            calls.finish()
            calls.finish()
            return ExitCode.OK

        status, _ = in_child(profiled)
        found = audit.read_profiles(directory)
        expect(
            "finish() writes one file, and a second finish() writes nothing", status == ExitCode.OK and len(found) == 1
        )
        expect(
            "the file holds the process, its parent, and an end at the start or after it",
            bool(found) and found[0].parent == os.getpid() and found[0].end >= found[0].start,
        )
        expect(
            "the file holds the profiled function with its one call",
            bool(found) and calls_of(found[0], "planted_work") == [1],
        )

        quiet = Path(scratch) / "quiet"

        def unprofiled() -> int:
            restart_profile(None)
            planted_work(10)
            calls.finish()
            return ExitCode.OK

        status, _ = in_child(unprofiled)
        expect("with no variable, start() and finish() write no file", status == ExitCode.OK and not quiet.exists())


def fork_cases(expect: testing.Expect) -> None:
    """Check a profile across a fork: the child writes its own file and does not count the work of the parent."""
    with tempfile.TemporaryDirectory(prefix="assay-calls-") as scratch:
        directory = Path(scratch) / "profiles"

        def parent() -> int:
            restart_profile(directory)
            planted_work(1000)
            child_status, _ = in_child(child_work)
            calls.finish()
            return child_status

        status, _ = in_child(parent)
        found = audit.read_profiles(directory)
        pids = {profile.pid for profile in found}
        children = [profile for profile in found if profile.parent in pids]
        expect(
            "a parent and a forked child each write one file",
            status == ExitCode.OK and len(found) == 2 and len(children) == 1,
        )
        expect(
            "the profile of the child counts only the call of the child",
            bool(children) and calls_of(children[0], "planted_work") == [1],
        )


def failure_cases(expect: testing.Expect) -> None:
    """Check a directory that cannot be made: finish() writes one line to the standard error and raises nothing."""
    with tempfile.TemporaryDirectory(prefix="assay-calls-") as scratch:
        blocker = Path(scratch) / "file"
        blocker.write_text("", encoding="utf-8")

        def blocked() -> int:
            restart_profile(blocker / "profiles")
            calls.finish()
            return ExitCode.OK

        status, output = in_child(blocked)
        expect(
            "finish() names the directory that it cannot make, and the process ends with OK",
            status == ExitCode.OK and "is not written" in output and str(blocker) in output,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (kept_cases, record_cases, fork_cases, failure_cases))
