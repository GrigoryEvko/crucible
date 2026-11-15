"""heavy — the slot pool of the build host, the lock of an exclusive heavy step, and the jobs that the memory gives.

THE POOL
    Each heavy job of the host takes its jobs from one pool of slots.  A
    heavy job is a configuration of a run of the gate, a `loop test` with no
    jobserver, or a command of `loop heavy`.  The pool is the directory
    POOL_NAME of the work root of assay.core.cache, with one file for each
    slot.  The variable POOL_VARIABLE moves the pool to a different
    directory (private_pool()).  A self-test that starts `loop test` holds a
    private pool, because the gate runs it in a lease of the gate (THE
    PRIORITY OF THE GATE).  The leases of the gate can hold each slot of the
    host, and their jobs then wait for the test, so a lease of the test in
    the pool of the host waits for ever.  The pool has SLOTS_PER_CPU slots
    for each CPU of the process.  A
    process holds a slot with an exclusive lock of flock(2) on the file of
    the slot.  The kernel frees the lock when the process ends, also on
    SIGKILL, so a slot never stays lost.  No daemon owns the slots.

    A lease is the slots of one job (leased()).  Its cap is the slice of
    the caller, with SLICE as the usual value, the size of the pool, and the
    jobs that the memory holds (THE MEMORY).  Its most slots (the burst) are
    the size of the pool, or the jobs of the memory.  A capped lease has its
    cap as its burst, so it never grows past its cap.  An example is the
    vessel of the gate, whose large compiles must not take the memory of a
    quiet host (THE VESSEL of assay.loop.gate).  The lease waits until
    one slot is free, and it takes at most START_SLOTS free slots.  It makes
    a jobserver of its burst with one token for each slot less one
    (assay.core.jobserver, THE SERVER).  Its job gives MAKEFLAGS of that
    jobserver to each command.  A thread of the lease (the pump) looks at
    the idle tokens of the FIFO each TICK_S seconds (THE VALVE):
      * No idle token: the job uses each slot, and the lease is hungry.
        The pump doubles the slots, with the free slots and the memory as
        limits.  It grows to the burst while no different lease waits, and
        to the cap else.  A lease waits when it is hungry and holds less
        than its cap.
      * A different lease waits: the pump pulls each idle token above the
        cap at once.  A lease then grows past its cap only on slots that no
        different lease wants, and no lease keeps a slot that it does not
        use.
      * A different hungry lease of the same class holds less than its fair
        share: the pump pulls each idle token above its own fair share at
        once, and it does not grow above that share.  The fair share of a
        lease is the size of the pool times the weight of the lease,
        divided by the sum of the weights of the leases of the class.  A
        lease has the weight 1, unless its caller gives a larger weight
        (THE WEIGHT).
      * Idle tokens for IDLE_TICKS ticks: the pump pulls them, with one
        slot as the minimum, and it frees their slots.  It does not grow
        for HOLDOFF_S seconds after that.
    The pump pulls only idle tokens, so it never stops a job that runs.  A
    lease that must give slots back waits on the FIFO between two ticks.  It
    pulls a token that a child gives back before the child takes it again.
    Each lease writes its slots, its cap, its hunger, its class, its weight
    and the memory of one job into a marker file in the directory
    LEASES_NAME of the pool, which it locks.  A marker with no lock is the
    marker of a process that ended, and a reader removes it.  A marker that
    does not read as four to six numbers counts as a lease of the weight 1
    of the class of the reader that does not wait (parsed_marker()).

THE WEIGHT
    A job whose length sets the length of a larger job can take a weight
    above 1.  An example is the vessel of the gate: its shard compiles are
    often the longest path of a landing (THE VESSEL of assay.loop.gate).
    The weight multiplies the fair share of the lease in its class, and the
    caller gives the lease a cap of its slice times the weight.  The weight
    changes no priority between the classes, and it changes no limit of
    THE MEMORY.

    The leases of a run of the gate (leased() with is_gate) go first.  A
    lease of the gate waits when it is hungry and holds less than its cap
    and less than the fair share of the leases of the gate.  While a lease
    of the gate waits, a different lease starts with one free slot, does not
    grow, and gives each idle token above one at once.  So it always runs,
    with one job as the least.  The leases of the gate share the pool fairly
    among themselves.

    A token that a child holds when SIGKILL ends it does not come back to
    the FIFO.  The job then runs one job less, and the lease keeps the
    slot of that token until it ends.

THE LOCK
    The work trees of one repository share the build host, and a large
    virtual machine of the owner shares it too.  A lease holds the lock of
    the file LOCK_FILE of the main work tree in the shared mode, so the
    leases run at the same time.  An exclusive heavy step, such as a step of
    `loop nightly`, holds an exclusive lock on that file.  No lease then
    runs at the same time as it (THE TURNSTILE).  The main work
    tree is the parent of the git directory that each work tree of the
    repository shares.  Each work tree then finds the same file.  The lock is
    a lock of flock(2), as the program flock(1) takes it.  A heavy step of
    this module and a step that flock(1) runs on the same file then keep each
    other apart.  The kernel removes the lock when the process ends, also on
    a signal.

    A child of a holder does not share the lock of its parent.  The child
    opens the file again, and flock(2) sees a different open file.  A child
    that asks for the lock then waits for its parent, and the parent waits
    for the child.  For this reason, a holder whose command can ask for a
    lease gives that command MAKEFLAGS of a jobserver.  `loop heavy` does
    this for each command (THE COMMAND OF A STEP), and the tests job of
    `loop nightly` does it for `loop test`.  `loop test` with such a
    jobserver takes no lease (assay.loop.affected, THE JOBSERVER).

THE PRIORITY OF THE GATE
    The gate of `loop land` is the one serial path of each landing
    (assay.loop.land).  All work trees of the host wait for it, because no
    change gets on main before the gate accepts it.  A heavy step of a work
    tree only makes its own work shorter.  For this reason, the gate goes
    first.  A session of the gate holds a lock of flock(2) in the shared
    mode on the file GATE_FILE for all of its batch (gate_session()), so the
    sessions of the lanes of the gate run at the same time.  That file is
    adjacent to LOCK_FILE.  A heavy step that is not a step of the gate
    takes that lock in the exclusive mode and frees it immediately.  Then it
    waits for LOCK_FILE, and it holds nothing while it waits.  The session
    then gets its lock with no wait.  When the step gets LOCK_FILE, it looks
    again.  When a session started while it waited, it frees LOCK_FILE and
    waits for the session to end.  A step that started before the session
    continues to its end, and the gate waits for it.  The steps of the gate
    do not look at GATE_FILE.  The kernel frees the lock of a session that
    ends, also on a signal.

THE TURNSTILE
    An exclusive heavy step goes before each new lease (writer preference).
    The step and each new lease pass the file TURN_FILE, which is adjacent
    to LOCK_FILE.  The step holds an exclusive lock of TURN_FILE while it
    waits for its exclusive lock of LOCK_FILE, and it frees TURN_FILE when it
    gets LOCK_FILE.  A new lease holds TURN_FILE only while it waits for its
    shared lock of LOCK_FILE.  So a waiting step stops each new lease, and it
    waits only for the leases that run.  A lease that waits behind a running
    step holds TURN_FILE, and the next step then waits for that lease.  The
    steps and the leases then take turns, and neither one waits for ever.

THE COMMAND OF A STEP
    `loop heavy` holds a lease of -j slots in its own process, on
    descriptors that the command does not get (THE POOL).  A process that
    the command starts and that continues after it then does not keep the
    slots.  The command gets MAKEFLAGS of the jobserver of the lease, and
    the variable STEP_VARIABLE.  It does not get the variable
    BUILD_PARALLEL_VARIABLE, because `cmake --build` gives ninja -j from it,
    and ninja obeys the jobserver only with no -j.  So `loop heavy -j 32 --
    cmake --build build` builds in the lease, and `loop heavy -j 32 --
    ctest --test-dir build -j 32` tests in it.  ctest obeys the jobserver
    also with -j.  The gate of `loop land` takes a lease for
    each configuration.  A landing in an exclusive heavy step then waits
    for itself, and `loop land` refuses to run with that variable
    (assay.loop.land, THE LOCK OF A HEAVY STEP).

THE MEMORY
    While heavy_step() holds the lock, it reads one time the memory that the
    host keeps available.  The available memory is the free memory of THE
    BUDGET (available_gb()).  The step keeps RESERVE_GB for the host,
    and it gives GB_PER_JOB of the remaining memory to each job.  The job
    count is the smaller of the count of the caller and the count of the
    memory, with a minimum of one (jobs_for_memory()).  The step writes the
    available memory and the two counts.  When the step cannot read the
    file, it gives the job count of the caller, and it writes a note.

    The step does not wait for the memory.  A wait for a level of the memory
    does not make the memory of a build smaller, but a smaller job count
    does.  heavy_lock() holds only the lock and reads no memory.

    A lease reads the memory when it starts, and its cap holds the jobs
    of the memory.  Each slot that it takes, at its start and at each growth,
    goes through THE BUDGET.

    A lease can give the memory of one of its jobs, when its jobs are larger
    than GB_PER_JOB (the parameter gb_per_job of leased()).  Its cap, its
    reservation and each growth then count that memory.  An example is the
    vessel of the gate, whose shard compiles use more than 1.5 GB each (THE
    VESSEL of assay.loop.gate).

THE BUDGET
    The leases of the host share one memory budget.  A lease reserves its
    slots times the memory of one of its jobs, and its marker holds that
    memory as its sixth number, in MB.  A dead lease loses its marker with
    its lock, so it loses its reservation with its slots.  Each take of
    slots holds the lock BUDGET_LOCK_NAME of the pool while it reads the
    other markers, takes the slots and writes its marker.  Two leases then
    never count the same memory.

    A take fits two limits (budget_jobs()):
      * The sum of the reservations of the pool after the take stays at
        most the ceiling of the pool less RESERVE_GB.  The ceiling is the
        smallest of MemTotal and the memory.max of each group of the
        control group chain of the process (memory_ceiling_gb()).  This
        limit holds also when many leases start together, before a job
        holds its memory.
      * The new jobs fit in the free memory less RESERVE_GB.  The free
        memory is the smallest of MemAvailable and, for each group of the
        chain, memory.max less memory.current plus its inactive page cache
        (available_gb()).  This limit holds when processes with no lease use
        the memory.
    The free memory already holds the memory of the jobs that run, so the
    first limit does not count it again.  A pool where each job holds its
    memory then still grows to its ceiling, not to half of it.

    A lease with no slot takes one slot when no other lease holds a
    reservation, so a job runs also on a host whose ceiling is below the
    reserve.  Else it waits, and it counts as a waiting lease (THE PRIORITY
    OF THE GATE).  A marker of older code has no memory field, and it counts
    each slot at UNKNOWN_GB_PER_JOB.  Such a lease does not obey the budget
    until it ends.

Usage
    loop heavy [-C DIR] [-j N] -- COMMAND [ARGUMENT]...

    `loop heavy` runs one command with a lease of at most N slots, SLICE as
    the default, in the directory DIR (THE COMMAND OF A STEP).  It exits
    with the exit of the command.  A command that ends on a signal gives 128
    plus the number of the signal.  `guard selftest tests.loop.test_heavy`
    does a test of the module.
"""

import contextlib
import fcntl
import math
import os
import random
import secrets
import tempfile
import threading
import time
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import IO

from assay.core import cache, cli, git, jobserver, proc
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT

# The lock file of the heavy steps, relative to the main work tree of the repository (THE LOCK).
LOCK_FILE = Path(".worktrees/.grun/heavy.lock")
# The lock file of a session of the gate, relative to the main work tree of the repository (THE PRIORITY OF THE GATE).
GATE_FILE = Path(".worktrees/.grun/gate.lock")
# The turnstile in front of LOCK_FILE, relative to the main work tree of the repository (THE TURNSTILE).
TURN_FILE = Path(".worktrees/.grun/turn.lock")
# The memory of one job, in GB (THE MEMORY).  On 2026-11-15 the heaviest compile of the Debug build with ASan in the
# lanes of the gate peaked at 0.74 GB, and its heaviest test at 0.64 GB.
GB_PER_JOB = 0.8
# The memory of one job of a lease whose marker gives no memory, in GB (THE BUDGET): the largest memory of one job of
# the gate, a compile of a shard of the vessel.
UNKNOWN_GB_PER_JOB = 1.7
# The memory that the pool keeps for the processes that hold no lease, in GB (THE MEMORY, THE BUDGET).
RESERVE_GB = 8.0
MEMORY_FILE = Path("/proc/meminfo")
MEMORY_FIELD = "MemAvailable:"
TOTAL_FIELD = "MemTotal:"
KB_PER_GB = 1024 * 1024
BYTES_PER_GB = 1024**3
MB_PER_GB = 1024
# The file that names the control group of this process, and the root of the control groups (THE BUDGET).
CGROUP_FILE = Path("/proc/self/cgroup")
CGROUP_ROOT = Path("/sys/fs/cgroup")
# The page cache that the kernel can take back before an out-of-memory kill, in memory.stat of a control group.
RECLAIMABLE_FIELD = "inactive_file"
# The lock of the pool for each change of the slots under the budget (THE BUDGET).
BUDGET_LOCK_NAME = "budget.lock"
# The exit of a command that ends on a signal: this base plus the number of the signal, as a shell gives it.
SIGNAL_EXIT_BASE = 128
# The variable that the command of a step gets, and its value (THE COMMAND OF A STEP).
STEP_VARIABLE = "CRUCIBLE_HEAVY_STEP"
STEP_VALUE = "1"
# The variable that makes `cmake --build` give ninja -j, which the command of a step does not get (THE COMMAND OF A
# STEP).
BUILD_PARALLEL_VARIABLE = "CMAKE_BUILD_PARALLEL_LEVEL"
# The pool of the slots of the host in the work root, the directory of the markers of the leases in it, and the digits
# of the name of a slot file (THE POOL).
POOL_NAME = "slots"
# The slots of the pool for each CPU of the process (THE POOL).  A job of the gate waits for much of its time: a test
# waits for its children, and a check in Python uses one core.  On 2026-11-13, 35 leases of the gate held each of the
# 224 slots of one slot for each CPU, and the load of the host was 49.  With two slots for each CPU, 40 jobs at the same
# time get approximately 11 slots each.
SLOTS_PER_CPU = 2
# The variable that names a different directory of the pool, and the prefix of the directory of a private pool (THE
# POOL).
POOL_VARIABLE = "CRUCIBLE_SLOT_POOL"
PRIVATE_PREFIX = "crucible-slots-"
LEASES_NAME = "leases"
SLOT_WIDTH = 4
MARKER_SUFFIX = ".lease"
NEW_MARKER_PREFIX = ".new-"
# The most slots of one lease.  The owner measured a good use of the host with 24 to 32 jobs for each job and many jobs
# at one time.  One job with all the slots made each other job wait.
SLICE = 32
# The most slots that a lease takes when it starts.  The pump doubles them while the job uses each one (THE POOL).
START_SLOTS = 4
# The interval of the pump, the ticks of idle tokens before the pump pulls them, and the time with no growth after a
# pull, in seconds.  The interval of the wait for the first slot and for the shared lock, in seconds.
TICK_S = 0.25
IDLE_TICKS = 8
HOLDOFF_S = 2.0
WAIT_S = 0.25

type Reader = Callable[[], float | None]
# A function that waits a number of seconds (assay.loop.land).
type Pause = Callable[[float], None]
# The check after the lock: it takes the open file of GATE_FILE, and it tells if a session of the gate runs.
type SessionCheck = Callable[[IO[str]], bool]


def meminfo_gb(field: str, path: Path = MEMORY_FILE) -> float | None:
    """Return one field of /proc/meminfo in GB, such as MemAvailable, or None when it cannot be read."""
    try:
        with open(path, encoding="ascii") as handle:
            for line in handle:
                if line.startswith(field):
                    return int(line.split()[1]) / KB_PER_GB
    except OSError, ValueError, IndexError:
        return None
    return None


def cgroup_chain(cgroup_file: Path = CGROUP_FILE, cgroup_root: Path = CGROUP_ROOT) -> list[Path]:
    """Return the control group of this process and each parent with a file memory.max, the innermost first.

    The chain goes up to the user slice and above, and it does not hold the root, which has no limit (THE BUDGET).  A
    host with no control group of version 2 gives no group.  Complexity: linear in the depth of the group.
    """
    try:
        text = cgroup_file.read_text(encoding="ascii")
    except OSError:
        return []
    relative = next((line.split("::", 1)[1].strip() for line in text.splitlines() if line.startswith("0::")), None)
    if relative is None:
        return []
    chain: list[Path] = []
    directory = cgroup_root / relative.lstrip("/")
    while directory != cgroup_root and cgroup_root in directory.parents:
        if (directory / "memory.max").is_file():
            chain.append(directory)
        directory = directory.parent
    return chain


def cgroup_bytes(path: Path) -> int | None:
    """Return the number of bytes of one file of a control group, or None for the value max or a file that does not
    read."""
    try:
        text = path.read_text(encoding="ascii").strip()
    except OSError:
        return None
    return int(text) if text.isdigit() else None


def reclaimable_bytes(directory: Path) -> int:
    """Return the page cache of a control group that the kernel can take back (RECLAIMABLE_FIELD), or 0."""
    try:
        for line in (directory / "memory.stat").read_text(encoding="ascii").splitlines():
            name, _, value = line.partition(" ")
            if name == RECLAIMABLE_FIELD and value.strip().isdigit():
                return int(value)
    except OSError:
        return 0
    return 0


def available_gb(
    path: Path = MEMORY_FILE, cgroup_file: Path = CGROUP_FILE, cgroup_root: Path = CGROUP_ROOT
) -> float | None:
    """Return the free memory of this process in GB, or None when nothing can be read (THE BUDGET).

    The free memory is the smallest of MemAvailable of the host and, for each group of the control group chain with a
    limit, its memory.max less its memory.current, plus the page cache that the group can give back.
    """
    values = [meminfo_gb(MEMORY_FIELD, path)]
    for directory in cgroup_chain(cgroup_file, cgroup_root):
        limit, current = cgroup_bytes(directory / "memory.max"), cgroup_bytes(directory / "memory.current")
        if limit is not None and current is not None:
            values.append((limit - current + reclaimable_bytes(directory)) / BYTES_PER_GB)
    known = [value for value in values if value is not None]
    return min(known) if known else None


def memory_ceiling_gb(
    path: Path = MEMORY_FILE, cgroup_file: Path = CGROUP_FILE, cgroup_root: Path = CGROUP_ROOT
) -> float | None:
    """Return the most memory that this process can use in GB, or None when nothing can be read (THE BUDGET).

    The ceiling is the smallest of MemTotal of the host and the memory.max of each group of the control group chain.
    """
    values = [meminfo_gb(TOTAL_FIELD, path)]
    for directory in cgroup_chain(cgroup_file, cgroup_root):
        limit = cgroup_bytes(directory / "memory.max")
        if limit is not None:
            values.append(limit / BYTES_PER_GB)
    known = [value for value in values if value is not None]
    return min(known) if known else None


def main_work_tree(root: Path) -> Path:
    """Return the main work tree of the repository of a tree, the parent of its shared git directory (THE LOCK).

    A tree that git cannot read gives the tree itself.
    """
    try:
        return git.common_dir(root).parent
    except git.GitError:
        return root


def lock_path(root: Path) -> Path:
    """Return the lock file of the heavy steps of the repository of a tree (THE LOCK)."""
    return main_work_tree(root) / LOCK_FILE


def gate_path(root: Path) -> Path:
    """Return the lock file of a session of the gate of the repository of a tree (THE PRIORITY OF THE GATE)."""
    return main_work_tree(root) / GATE_FILE


def turn_path(root: Path) -> Path:
    """Return the turnstile in front of the lock of the heavy steps of the repository of a tree (THE TURNSTILE)."""
    return main_work_tree(root) / TURN_FILE


@contextlib.contextmanager
def gate_session(root: Path = REPO_ROOT) -> Iterator[None]:
    """Hold the lock of a session of the gate in the shared mode for the block (THE PRIORITY OF THE GATE).

    The sessions of the lanes of the gate run at the same time.  The block does not wait for a heavy step, because a
    heavy step holds no lock of GATE_FILE while it waits.
    """
    path = gate_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_SH)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def is_locked(handle: IO[str], mode: int) -> bool:
    """Tell if a different open file holds a lock of flock(2) that keeps the lock of a mode from an open file."""
    try:
        fcntl.flock(handle, mode | fcntl.LOCK_NB)
    except BlockingIOError:
        return True
    fcntl.flock(handle, fcntl.LOCK_UN)
    return False


def is_held(path: Path) -> bool:
    """Tell if a different open file holds a lock of flock(2) on a file.  A missing file gives False."""
    try:
        with open(path, encoding="ascii") as handle:
            return is_locked(handle, fcntl.LOCK_EX)
    except FileNotFoundError:
        return False


def is_gate_running(handle: IO[str]) -> bool:
    """Tell if a session of the gate holds its lock: the exclusive mode is not free (THE PRIORITY OF THE GATE)."""
    return is_locked(handle, fcntl.LOCK_EX)


def wait_for_gate(handle: IO[str]) -> None:
    """Wait while a session of the gate holds its lock, and hold nothing after the wait (THE PRIORITY OF THE GATE)."""
    if is_gate_running(handle):
        check_report.write_line("heavy: a session of the gate runs, and the step waits for it")
        fcntl.flock(handle, fcntl.LOCK_EX)
        fcntl.flock(handle, fcntl.LOCK_UN)


def jobs_for_memory(requested: int, read: Reader = available_gb, gb_per_job: float = GB_PER_JOB) -> int:
    """Return the job count that the available memory holds, with a minimum of 1 (THE MEMORY).

    Args:
        requested: The job count of the caller, which is the maximum
        read: The reader of the available memory, which gives None when it cannot read it
        gb_per_job: The memory of one job of the caller, in GB

    Returns:
        The count of the caller when the reader gives None.  Else the smaller of that count and the count of
        `gb_per_job` in the available memory less RESERVE_GB, with a minimum of 1
    """
    value = read()
    if value is None:
        return requested
    return max(1, min(requested, math.floor((value - RESERVE_GB) / gb_per_job)))


@contextlib.contextmanager
def heavy_lock(root: Path, is_gate_step: bool = False, is_session: SessionCheck = is_gate_running) -> Iterator[None]:
    """Hold the lock of the heavy steps for the block, and read no memory (THE LOCK).

    A step that is not a step of the gate also waits while a session of the gate runs (THE PRIORITY OF THE GATE).

    Args:
        root: A tree of the repository
        is_gate_step: True for a step of the gate, which does not wait for a session of the gate
        is_session: The check after the lock, which tells if a session of the gate started.  A step of the gate
            does not call it
    """
    path = lock_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    with (
        open(path, "a", encoding="ascii") as handle,
        open(gate_path(root), "a", encoding="ascii") as gate_handle,
        open(turn_path(root), "a", encoding="ascii") as turn,
    ):
        while True:
            if not is_gate_step:
                wait_for_gate(gate_handle)
            fcntl.flock(turn, fcntl.LOCK_EX)
            try:
                fcntl.flock(handle, fcntl.LOCK_EX)
            finally:
                fcntl.flock(turn, fcntl.LOCK_UN)
            if is_gate_step or not is_session(gate_handle):
                break
            fcntl.flock(handle, fcntl.LOCK_UN)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


@contextlib.contextmanager
def heavy_step(
    root: Path,
    jobs: int,
    read: Reader = available_gb,
    is_gate_step: bool = False,
    is_session: SessionCheck = is_gate_running,
) -> Iterator[int]:
    """Hold the lock of the heavy steps for the block, and give the block the job count of the memory (THE MEMORY).

    The step reads the memory one time, after it gets the lock, and it does not wait for the memory (heavy_lock()).

    Args:
        root: A tree of the repository
        jobs: The job count of the caller, which is the maximum of the step
        read: The reader of the available memory, which gives None when it cannot read it
        is_gate_step: True for a step of the gate, which does not wait for a session of the gate
        is_session: The check after the lock, which tells if a session of the gate started.  A step of the gate
            does not call it

    Yields:
        The job count of the step (jobs_for_memory())
    """
    with heavy_lock(root, is_gate_step, is_session):
        value = read()
        step_jobs = jobs_for_memory(jobs, lambda: value)
        if value is None:
            check_report.write_line(f"memory: {MEMORY_FILE} gives no {MEMORY_FIELD}, so the step keeps {jobs} jobs")
        else:
            check_report.write_line(f"memory: available {value:.1f} GB, {step_jobs} of {jobs} jobs")
        yield step_jobs


@dataclass(frozen=True, slots=True)
class Pool:
    """The slot pool of a host: its directory, its count of slots, and its memory ceiling in GB (THE POOL, THE BUDGET).

    A ceiling of None puts no limit on the sum of the reservations of the leases.
    """

    directory: Path
    size: int
    ceiling_gb: float | None = None


def host_pool() -> Pool:
    """Return the slot pool of this host, with SLOTS_PER_CPU slots for each CPU of the process (THE POOL).

    The directory is the value of POOL_VARIABLE when the variable is set, else POOL_NAME in the work root.  The
    ceiling is memory_ceiling_gb() (THE BUDGET).
    """
    moved = os.environ.get(POOL_VARIABLE)
    directory = Path(moved) if moved else cache.work_root() / POOL_NAME
    return Pool(directory, proc.DEFAULT_JOBS * SLOTS_PER_CPU, memory_ceiling_gb())


@contextlib.contextmanager
def private_pool() -> Iterator[Path]:
    """Give this process and each child a new empty pool for the block, and remove it after the block (THE POOL).

    No other lease uses the pool, so a lease in it gets its first slots at once.

    Yields:
        The directory of the pool
    """
    saved = os.environ.get(POOL_VARIABLE)
    with tempfile.TemporaryDirectory(prefix=PRIVATE_PREFIX) as directory:
        os.environ[POOL_VARIABLE] = directory
        try:
            yield Path(directory)
        finally:
            if saved is None:
                os.environ.pop(POOL_VARIABLE, None)
            else:
                os.environ[POOL_VARIABLE] = saved


def budget_jobs(pool: Pool, reserved_gb: float, free_gb: float | None, gb_per_job: float, count: int) -> int:
    """Return how many of `count` new jobs of `gb_per_job` GB the memory budget of the pool holds (THE BUDGET).

    The sum of the reservations of the pool after the growth stays at most the ceiling of the pool less RESERVE_GB.
    The growth also fits in the free memory less RESERVE_GB.  `reserved_gb` is the sum of the reservations of each
    lease, with the lease that grows.  A ceiling or a free memory of None gives no limit of its kind.
    """
    fits = count
    if pool.ceiling_gb is not None:
        fits = min(fits, math.floor((pool.ceiling_gb - RESERVE_GB - reserved_gb) / gb_per_job))
    if free_gb is not None:
        fits = min(fits, math.floor((free_gb - RESERVE_GB) / gb_per_job))
    return max(0, fits)


def fair_share(pool: Pool, weight: int, total: int) -> int:
    """Return the fair share of a lease of `weight` in a class whose weights sum to `total`, at least one slot.

    THE POOL and THE WEIGHT give the rule.
    """
    return max(1, pool.size * weight // max(1, total))


@dataclass(frozen=True, slots=True)
class Demand:
    """The other leases of the pool at one time, from their markers (THE POOL).

    `weight` is the sum of the weights of the leases of the class of the reader, with the reader (THE WEIGHT).  A lease
    waits when it is hungry and holds less than its cap.  `peers_below_fair` counts the hungry leases of the class of
    the reader below their fair share: the size of the pool times their weight, divided by `weight`.
    """

    weight: int
    waiting_gate: int
    waiting_other: int
    peers_below_fair: int

    @property
    def is_waiting(self) -> bool:
        """Tell if a different lease waits."""
        return self.waiting_gate + self.waiting_other > 0


@dataclass(frozen=True, slots=True)
class Marker:
    """The marker of a different live lease: its slots, cap, hunger, class, weight and memory of a job (THE POOL).

    `is_read` is False for a marker that does not read as numbers.  Such a marker counts as a lease of the weight 1 that
    does not wait and holds no reservation.
    """

    held: int = 0
    cap: int = 0
    is_hungry: bool = False
    is_gate: bool = False
    weight: int = 1
    gb_per_job: float = UNKNOWN_GB_PER_JOB
    is_read: bool = False

    @property
    def reserved_gb(self) -> float:
        """The reservation of the lease: its slots times the memory of one of its jobs (THE BUDGET)."""
        return self.held * self.gb_per_job


def parsed_marker(words: Sequence[bytes]) -> Marker:
    """Return the marker of the words of a marker file: four, five or six numbers (THE POOL, THE BUDGET).

    The fifth number is the weight, and the sixth the memory of one job in MB.  A lease of code that writes no weight
    has the weight 1, and one that writes no memory has UNKNOWN_GB_PER_JOB.
    """
    if not 4 <= len(words) <= 6 or not all(word.isdigit() for word in words):
        return Marker()
    numbers = [int(word) for word in words]
    weight = max(1, numbers[4]) if len(numbers) > 4 else 1
    gb_per_job = numbers[5] / MB_PER_GB if len(numbers) > 5 else UNKNOWN_GB_PER_JOB
    return Marker(numbers[0], numbers[1], numbers[2] == 1, numbers[3] == 1, weight, gb_per_job, True)


class Lease:
    """The slots of the pool that one job holds, and the marker of the job (THE POOL).

    `cap` is the slice of the job, and `is_gate` tells that the job is a part of a run of the gate (THE PRIORITY OF THE
    GATE).  `weight` multiplies the fair share of the lease in its class (THE WEIGHT).  `gb_per_job` is the memory of
    one job, for the reservation of the lease (THE BUDGET).  One thread at a time changes the slots: the pump while it
    runs, else the owner of the lease.
    """

    __slots__ = (
        "_held",
        "_marker",
        "_marker_path",
        "cap",
        "gb_per_job",
        "hungry",
        "is_gate",
        "pool",
        "slot_seconds",
        "weight",
    )

    def __init__(self, pool: Pool, cap: int, is_gate: bool, weight: int = 1, gb_per_job: float = GB_PER_JOB) -> None:
        """Keep the pool, the cap, the class, the weight and the memory of a job, and hold no slot and no marker."""
        self.pool = pool
        self.cap = cap
        self.is_gate = is_gate
        self.weight = weight
        self.gb_per_job = gb_per_job
        self._held: list[int] = []
        self._marker = -1
        self._marker_path = Path()
        self.hungry = True
        self.slot_seconds = 0.0

    @property
    def held(self) -> int:
        """The count of the slots that the lease holds."""
        return len(self._held)

    def register(self) -> None:
        """Write the marker of the lease with its lock, then give it its name, so no reader sees it with no lock."""
        markers = self.pool.directory / LEASES_NAME
        markers.mkdir(parents=True, exist_ok=True)
        name = f"{os.getpid()}-{secrets.token_hex(4)}"
        fresh = markers / f"{NEW_MARKER_PREFIX}{name}"
        self._marker = os.open(fresh, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
        fcntl.flock(self._marker, fcntl.LOCK_EX)
        self._marker_path = markers / f"{name}{MARKER_SUFFIX}"
        os.rename(fresh, self._marker_path)
        self.mark()

    def mark(self) -> None:
        """Write the slots, the cap, the hunger, the class, the weight and the memory of a job in MB into the marker.

        Each field has a fixed width, so one write of 32 bytes replaces the line, and a reader sees the old line or the
        new one.
        """
        if self._marker >= 0:
            megabytes = math.ceil(self.gb_per_job * MB_PER_GB)
            line = (
                f"{self.held:6d} {self.cap:6d} {int(self.hungry)} {int(self.is_gate)} {self.weight:6d} {megabytes:6d}\n"
            )
            os.pwrite(self._marker, line.encode("ascii"), 0)

    def take(self, count: int) -> int:
        """Take at most `count` free slots with no wait, and return the count taken.

        The scan starts at a random slot, so two leases seldom try the same file.  Complexity: linear in the size of
        the pool.
        """
        taken = 0
        size = self.pool.size
        start = random.randrange(size)
        for step in range(size):
            if taken >= count:
                break
            path = self.pool.directory / f"{(start + step) % size:0{SLOT_WIDTH}d}"
            descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                os.close(descriptor)
                continue
            self._held.append(descriptor)
            taken += 1
        return taken

    def give(self, count: int) -> None:
        """Free `count` slots of the lease, the last ones first."""
        for _ in range(min(count, len(self._held))):
            descriptor = self._held.pop()
            fcntl.flock(descriptor, fcntl.LOCK_UN)
            os.close(descriptor)

    def markers(self) -> list[Marker]:
        """Read the marker of each different live lease of the pool, and remove each marker with no lock (THE POOL).

        The kernel frees the lock of a lease that ends, so a dead lease loses its slots and its reservation at once.
        Complexity: linear in the markers.
        """
        found: list[Marker] = []
        for path in sorted((self.pool.directory / LEASES_NAME).glob(f"*{MARKER_SUFFIX}")):
            if path == self._marker_path:
                continue
            try:
                descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
            except FileNotFoundError:
                continue
            try:
                if not is_locked_descriptor(descriptor):
                    path.unlink(missing_ok=True)
                    continue
                found.append(parsed_marker(os.pread(descriptor, 64, 0).split()))
            finally:
                os.close(descriptor)
        return found

    def demand(self) -> Demand:
        """Read the markers of the pool, remove each one with no lock, and give the demand (THE POOL).

        A marker that does not read as numbers counts as a lease of the weight 1 of the class of the reader that does
        not wait.  A lease of the gate waits only below the smaller of its cap and its fair share of the gate leases, so
        a busy gate lease at its share does not stop the other leases.  Complexity: linear in the markers.
        """
        weight = self.weight
        gate_weight = self.weight if self.is_gate else 0
        gate_rows: list[tuple[int, int, int]] = []
        waiting_other = 0
        peers: list[tuple[int, int]] = []
        for marker in self.markers():
            if not marker.is_read:
                weight += 1
                continue
            gate_weight += marker.weight if marker.is_gate else 0
            if marker.is_gate and marker.is_hungry:
                gate_rows.append((marker.held, marker.cap, marker.weight))
            waiting_other += int(not marker.is_gate and marker.is_hungry and marker.held < marker.cap)
            if marker.is_gate == self.is_gate:
                weight += marker.weight
                if marker.is_hungry:
                    peers.append((marker.held, marker.weight))
        waiting_gate = sum(
            1 for held, cap, row_weight in gate_rows if held < min(cap, fair_share(self.pool, row_weight, gate_weight))
        )
        below = sum(1 for held, peer_weight in peers if held < fair_share(self.pool, peer_weight, weight))
        return Demand(weight, waiting_gate, waiting_other, below)

    def take_within_budget(self, count: int, read: Reader) -> int:
        """Take at most `count` free slots that the memory budget of the pool holds, and return the count (THE BUDGET).

        The lease holds the lock BUDGET_LOCK_NAME of the pool while it reads the reservations of the other live leases,
        takes its slots and writes its marker.  Two leases then never count the same free memory.  A lease with no slot
        takes one slot when no other lease holds a reservation, so a job on a small host still runs.
        """
        with open(self.pool.directory / BUDGET_LOCK_NAME, "a", encoding="ascii") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            try:
                others_gb = sum(marker.reserved_gb for marker in self.markers())
                reserved_gb = others_gb + self.held * self.gb_per_job
                fits = budget_jobs(self.pool, reserved_gb, read(), self.gb_per_job, count)
                if fits == 0 and self.held == 0 and others_gb == 0 and count > 0:
                    fits = 1
                taken = self.take(fits)
                self.mark()
                return taken
            finally:
                fcntl.flock(handle, fcntl.LOCK_UN)

    def close(self) -> None:
        """Free each slot, and remove the marker."""
        self.give(len(self._held))
        if self._marker >= 0:
            self._marker_path.unlink(missing_ok=True)
            os.close(self._marker)
            self._marker = -1


def is_locked_descriptor(descriptor: int) -> bool:
    """Tell if a different open file holds a lock of flock(2) on the file of a descriptor."""
    try:
        fcntl.flock(descriptor, fcntl.LOCK_SH | fcntl.LOCK_NB)
    except BlockingIOError:
        return True
    fcntl.flock(descriptor, fcntl.LOCK_UN)
    return False


def pulled_count(lease: Lease, demand: Demand, idle: int, idle_ticks: int) -> int:
    """Return the count of idle tokens that the pump pulls at one tick (THE POOL).

    A lease that is not of the gate gives each idle token above one while a lease of the gate waits.  A lease above its
    cap gives the idle tokens above its cap while a different lease waits.  A lease above its fair share gives the idle
    tokens above that share while a hungry lease of its class holds less.  Else a lease gives its idle tokens after
    IDLE_TICKS ticks, with one slot as the minimum.
    """
    fair = fair_share(lease.pool, lease.weight, demand.weight)
    if not lease.is_gate and demand.waiting_gate:
        return min(idle, lease.held - 1)
    if lease.held > lease.cap and demand.is_waiting:
        return min(idle, lease.held - lease.cap)
    if lease.held > fair and demand.peers_below_fair:
        return min(idle, lease.held - fair)
    return min(idle, lease.held - 1) if idle_ticks >= IDLE_TICKS else 0


def grown_count(lease: Lease, demand: Demand, burst: int) -> int:
    """Return the count of slots that the pump tries to add at one tick to a lease that uses each slot (THE POOL).

    The count doubles the slots.  A lease grows to `burst` while no different lease waits, and to its cap else.  A lease
    with a hungry peer below the fair share grows only to that share.  A lease that is not of the gate does not grow
    while a lease of the gate waits.
    """
    if not lease.is_gate and demand.waiting_gate:
        return 0
    limit = lease.cap if demand.is_waiting else burst
    if demand.peers_below_fair:
        limit = min(limit, fair_share(lease.pool, lease.weight, demand.weight))
    return max(0, min(lease.held, limit - lease.held))


def pumped(
    lease: Lease,
    opened: jobserver.Valve,
    burst: int,
    read: Reader,
    stop: threading.Event,
) -> None:
    """Change the slots of a lease to the use of its job, each TICK_S seconds, until the stop event (THE POOL).

    The lease holds one slot more than the tokens of its jobserver.  The function puts one token for each new slot,
    and it frees one slot for each token that it pulls.  `burst` is the most slots of the lease, the jobs of its
    jobserver.  A growth adds at most the jobs that the memory budget of the pool holds (THE BUDGET).
    """
    idle_ticks = 0
    hold_until = 0.0
    last = time.monotonic()
    is_draining = False
    while not stop.is_set():
        # A lease that must give slots back waits for the next token that a child gives back, and pulls it before the
        # child takes it again.  Each other tick waits for the stop event.
        if is_draining:
            opened.wait_idle(TICK_S)
        elif stop.wait(TICK_S):
            break
        now = time.monotonic()
        lease.slot_seconds += lease.held * (now - last)
        last = now
        idle = opened.idle()
        demand = lease.demand()
        if idle > 0:
            idle_ticks += 1
            pulled = opened.pull(pulled_count(lease, demand, idle, idle_ticks))
            lease.give(pulled)
            if pulled:
                idle_ticks = 0
                hold_until = now + HOLDOFF_S
            lease.hungry = False
        else:
            idle_ticks = 0
            lease.hungry = lease.held < burst
            count = grown_count(lease, demand, burst) if lease.hungry and now >= hold_until else 0
            opened.put(lease.take_within_budget(count, read) if count > 0 else 0)
        lease.mark()
        is_yielding = not lease.is_gate and demand.waiting_gate > 0 and lease.held > 1
        is_draining = is_yielding or (lease.held > lease.cap and demand.is_waiting)
    lease.slot_seconds += lease.held * (time.monotonic() - last)


@dataclass(frozen=True, slots=True)
class Share:
    """The jobs of one lease: the jobserver, a client of it, its cap, the wait for the first slot, and the lease.

    `lease.slot_seconds` holds the sum of the slots over the time of the lease after the end of its block.
    """

    server: jobserver.Server
    client: jobserver.Client
    jobs: int
    wait_s: float
    lease: Lease

    @property
    def environment(self) -> dict[str, str]:
        """The variable MAKEFLAGS of the jobserver of the lease."""
        return {jobserver.VARIABLE: self.server.makeflags}


def held_flock(handle: IO[str], mode: int, stop: threading.Event | None, pause: Pause) -> bool:
    """Wait for a lock of flock(2) in a mode, and tell if the lock is held.  A set stop event gives False."""
    while True:
        try:
            fcntl.flock(handle, mode | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            if stop is not None and stop.is_set():
                return False
            pause(WAIT_S)


def held_shared(handle: IO[str], turn: IO[str], stop: threading.Event | None, pause: Pause) -> bool:
    """Pass the turnstile, and wait for the lock of LOCK_FILE in the shared mode (THE TURNSTILE).

    The function holds the lock of TURN_FILE only while it waits for the shared lock.  A set stop event gives False.
    """
    if not held_flock(turn, fcntl.LOCK_EX, stop, pause):
        return False
    try:
        return held_flock(handle, fcntl.LOCK_SH, stop, pause)
    finally:
        fcntl.flock(turn, fcntl.LOCK_UN)


@contextlib.contextmanager
def leased(
    root: Path,
    want: int,
    pool: Pool | None = None,
    read: Reader = available_gb,
    stop: threading.Event | None = None,
    pause: Pause = time.sleep,
    is_gate: bool = False,
    gb_per_job: float = GB_PER_JOB,
    is_capped: bool = False,
    weight: int = 1,
) -> Iterator[Share | None]:
    """Hold a lease with the cap `want` of the pool for the block, and give its share (THE POOL).

    The block gets None when the stop event is set before the lease holds a slot.  The lease also holds LOCK_FILE of
    the repository of root in the shared mode (THE LOCK, THE TURNSTILE).  A lease of the gate goes before each other
    lease (THE PRIORITY OF THE GATE).  The jobs of the share are the most slots of the lease: the size of the pool, or
    the jobs of the memory.  A lease with `is_capped` never grows past its cap, and its most slots are its cap.
    `gb_per_job` is the memory of one job of the lease, for its cap and for each growth (THE MEMORY).  `weight`
    multiplies the fair share of the lease in its class (THE WEIGHT).

    Raises:
        jobserver.JobserverError: If the jobserver of the lease cannot be made
        OSError: If the pool cannot be written
    """
    chosen = pool if pool is not None else host_pool()
    chosen.directory.mkdir(parents=True, exist_ok=True)
    lock = lock_path(root)
    lock.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    value = read()
    cap = jobs_for_memory(max(1, min(want, chosen.size)), lambda: value, gb_per_job)
    burst = cap if is_capped else max(cap, jobs_for_memory(chosen.size, lambda: value, gb_per_job))
    each = f", {gb_per_job:.2f} GB for each job" if gb_per_job != GB_PER_JOB else ""
    if value is None:
        check_report.write_line(f"memory: {MEMORY_FILE} gives no {MEMORY_FIELD}, so the lease keeps {cap} jobs")
    else:
        check_report.write_line(f"memory: available {value:.1f} GB, {cap} of {want} jobs, {burst} at most{each}")
    lease = Lease(chosen, cap, is_gate, weight, gb_per_job)
    with open(lock, "a", encoding="ascii") as handle, open(turn_path(root), "a", encoding="ascii") as turn:
        try:
            if not held_shared(handle, turn, stop, pause):
                yield None
                return
            lease.register()
            while True:
                is_yielding = not lease.is_gate and lease.demand().waiting_gate > 0
                if lease.take_within_budget(1 if is_yielding else min(START_SLOTS, cap), read):
                    break
                if stop is not None and stop.is_set():
                    yield None
                    return
                pause(WAIT_S)
            wait_s = time.monotonic() - started
            lease.hungry = False
            lease.mark()
            halt = threading.Event()
            with (
                jobserver.serving(burst, lease.held - 1) as server,
                jobserver.valve(server.path) as opened,
                jobserver.connected(server.path) as client,
            ):
                pump = threading.Thread(target=pumped, args=(lease, opened, burst, read, halt), daemon=True)
                pump.start()
                try:
                    yield Share(server, client, burst, wait_s, lease)
                finally:
                    halt.set()
                    pump.join()
        finally:
            lease.close()


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
    parser = cli.parser("loop heavy", "Run one command with a lease of the slot pool of the host.")
    parser.add_argument("-C", dest="directory", type=Path, default=Path.cwd(), help="the directory of the command")
    parser.add_argument("-j", dest="jobs", type=int, default=SLICE, help="the most slots of the lease")
    options = parser.parse_args(argv[:split])
    command = argv[split + 1 :]
    if not command:
        check_report.write_line("loop heavy: give a command after --", is_error=True)
        return ExitCode.USAGE
    if options.jobs < 1:
        check_report.write_line(f"loop heavy: -j takes a number above 0, not {options.jobs}", is_error=True)
        return ExitCode.USAGE
    try:
        with leased(options.directory.resolve(), options.jobs) as share:
            if share is None:
                return ExitCode.TOOL_FAILED
            environment = proc.environment({STEP_VARIABLE: STEP_VALUE, **share.environment})
            environment.pop(BUILD_PARALLEL_VARIABLE, None)
            completed = proc.run(
                proc.Command(
                    command,
                    timeout_s=proc.BUILD_S,
                    cwd=options.directory,
                    env=environment,
                    stdout=proc.Output.INHERIT,
                    stderr=proc.Output.INHERIT,
                )
            )
    except (proc.ToolFailed, jobserver.JobserverError, OSError) as problem:
        check_report.write_line(f"loop heavy: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
    return exit_of(completed)
