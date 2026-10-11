"""heavy — the one lock of the heavy steps of the build host, and the wait for the memory of a heavy step.

THE LOCK
    The work trees of one repository share the build host, and a large
    virtual machine of the owner shares it too.  One heavy step runs at a
    time.  A heavy step is a build, a test run, a run of the fixtures, a
    parse build or a step of a landing.  It holds an exclusive lock on the
    file LOCK_FILE of the main work tree of the repository.  The main work
    tree is the parent of the git directory that each work tree of the
    repository shares.  Each work tree then finds the same file.  The lock is
    a lock of flock(2), as the program flock(1) takes it.  A heavy step of
    this module and a step that flock(1) runs on the same file then keep each
    other apart.  The kernel removes the lock when the process ends, also on
    a signal.

THE MEMORY
    While the step holds the lock, it waits until the host keeps a level of
    memory available for SAMPLES samples in sequence, INTERVAL_S seconds
    apart.  A sample below the level starts the count again.  The available
    memory is the field MemAvailable of /proc/meminfo.  A step of a small
    number of named targets or tests waits for NAMED_LEVEL_GB, and each other
    heavy step waits for FULL_LEVEL_GB.  When the file cannot be read, the
    step does not wait, and it writes a note.

Usage
    loop heavy [--memory-gb N] [-C DIR] -- COMMAND [ARGUMENT]...

    `loop heavy` runs one command as a heavy step, in the directory DIR, and
    it exits with the exit of the command.  A command that ends on a signal
    gives 128 plus the number of the signal.  `guard selftest
    tests.loop.test_heavy` does a test of the module.
"""

import contextlib
import fcntl
import time
from collections.abc import Callable, Iterator
from pathlib import Path

from assay.core import cli, git, proc
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT

# The lock file of the heavy steps, relative to the main work tree of the repository (THE LOCK).
LOCK_FILE = Path(".worktrees/.grun/heavy.lock")
# The levels of the available memory, in GB, and the samples of the wait (THE MEMORY).
NAMED_LEVEL_GB = 8.0
FULL_LEVEL_GB = 16.0
SAMPLES = 3
INTERVAL_S = 10.0
MEMORY_FILE = Path("/proc/meminfo")
MEMORY_FIELD = "MemAvailable:"
KB_PER_GB = 1024 * 1024
# The exit of a command that ends on a signal: this base plus the number of the signal, as a shell gives it.
SIGNAL_EXIT_BASE = 128

type Reader = Callable[[], float | None]
type Pause = Callable[[float], None]


def available_gb(path: Path = MEMORY_FILE) -> float | None:
    """Return the available memory of the host in GB, from the field MemAvailable, or None when it cannot be read."""
    try:
        with open(path, encoding="ascii") as handle:
            for line in handle:
                if line.startswith(MEMORY_FIELD):
                    return int(line.split()[1]) / KB_PER_GB
    except OSError, ValueError, IndexError:
        return None
    return None


def main_work_tree(root: Path) -> Path:
    """Return the main work tree of the repository of a tree, the parent of its shared git directory (THE LOCK).

    A tree that git cannot read gives the tree itself.
    """
    try:
        shared = git.run(root, "rev-parse", "--path-format=absolute", "--git-common-dir").decode().strip()
    except git.GitError:
        return root
    return Path(shared).parent


def lock_path(root: Path) -> Path:
    """Return the lock file of the heavy steps of the repository of a tree (THE LOCK)."""
    return main_work_tree(root) / LOCK_FILE


def wait_for_memory(
    level_gb: float, read: Reader = available_gb, pause: Pause = time.sleep
) -> tuple[float, float | None]:
    """Wait until SAMPLES samples in sequence give the level or more (THE MEMORY).

    Args:
        level_gb: The level of the available memory, in GB
        read: The reader of the available memory, which gives None when it cannot read it
        pause: The function that waits one interval

    Returns:
        The seconds of the wait, and the available memory of the last sample, or None when the reader cannot read it

    Complexity: one sample for each interval of the wait.
    """
    waited = 0.0
    good = 0
    while True:
        value = read()
        if value is None:
            return waited, None
        good = good + 1 if value >= level_gb else 0
        if good >= SAMPLES:
            return waited, value
        pause(INTERVAL_S)
        waited += INTERVAL_S


@contextlib.contextmanager
def heavy_step(
    level_gb: float, root: Path = REPO_ROOT, read: Reader = available_gb, pause: Pause = time.sleep
) -> Iterator[None]:
    """Hold the lock of the heavy steps for the block, and wait for the memory first (THE LOCK, THE MEMORY).

    Args:
        level_gb: The level of the available memory, in GB
        root: A tree of the repository
        read: The reader of the available memory
        pause: The function that waits one interval
    """
    path = lock_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        try:
            waited, value = wait_for_memory(level_gb, read, pause)
            if value is None:
                check_report.write_line(f"memwait: {MEMORY_FILE} gives no {MEMORY_FIELD}, so the step does not wait")
            else:
                check_report.write_line(f"memwait: available {value:.0f} GB after {waited:.0f} s")
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def exit_of(completed: proc.Completed) -> int:
    """Return the exit of a command as a shell gives it: the status, or SIGNAL_EXIT_BASE plus the number of a signal."""
    return completed.exit if completed.exit >= 0 else SIGNAL_EXIT_BASE - completed.exit


def main(argv: list[str]) -> int:
    """Run one command as a heavy step, and return its exit.

    Args:
        argv: The words after `loop heavy`

    Returns:
        The exit of the command, or 2 on a usage error
    """
    if "--" not in argv:
        check_report.write_line("loop heavy: put -- before the command", is_error=True)
        return ExitCode.USAGE
    split = argv.index("--")
    parser = cli.parser("loop heavy", "Run one command under the lock of the heavy steps, after the memory wait.")
    parser.add_argument(
        "--memory-gb",
        type=float,
        default=FULL_LEVEL_GB,
        help=f"the level of the available memory, {NAMED_LEVEL_GB:g} for a small number of named targets or tests",
    )
    parser.add_argument("-C", dest="directory", type=Path, default=Path.cwd(), help="the directory of the command")
    options = parser.parse_args(argv[:split])
    command = argv[split + 1 :]
    if not command:
        check_report.write_line("loop heavy: give a command after --", is_error=True)
        return ExitCode.USAGE
    with heavy_step(options.memory_gb, options.directory.resolve()):
        try:
            completed = proc.run(
                proc.Command(
                    command,
                    timeout_s=proc.BUILD_S,
                    cwd=options.directory,
                    stdout=proc.Output.INHERIT,
                    stderr=proc.Output.INHERIT,
                )
            )
        except proc.ToolFailed as problem:
            check_report.write_line(f"loop heavy: {problem}", is_error=True)
            return ExitCode.TOOL_FAILED
    return exit_of(completed)
