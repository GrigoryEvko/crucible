"""The test of the lock of the heavy steps and of the job count that the memory gives a step (assay.loop.heavy).

`guard selftest tests.loop.test_heavy` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases read a planted file of the memory.  They give the job count a
planted reader, and a step must read it one time and must not wait.  They
make sure that a linked work tree of a planted repository finds the lock of
the main work tree.  They also make sure that the lock is exclusive while a
step holds it.  A session of the gate must let a step of the gate go first.
A different step must wait for the session, also when it gets the lock after
the session starts.

The cases of the pool use a planted pool.  A lease must keep its cap, take
only free slots, grow while its job uses each slot, and give idle slots
back.  SIGKILL of a holder must free each of its slots.  A lease that holds
each slot must give its idle slots above its fair share to a hungry lease.
A lease that is not of the gate must give its idle slots to a waiting lease
of the gate, and a waiting exclusive step must stop each new lease.
"""

import contextlib
import fcntl
import os
import signal
import sys
import tempfile
import threading
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import IO

from assay.core import git, jobserver, paths, proc, testing
from assay.loop import heavy

# The table of the locks of the kernel (has_waiter()).
LOCKS_FILE = Path("/proc/locks")
# The available memory of a lease of a case, which holds each job.
MUCH_MEMORY_GB = 1e6
# A process that holds a lease of 4 jobs of a planted pool, writes the count of its slots to a file, and waits.
HOLDER = """import pathlib, sys, time
sys.path.insert(0, sys.argv[1])
from assay.loop import heavy
pool = heavy.Pool(pathlib.Path(sys.argv[2]), int(sys.argv[3]))
with heavy.leased(pathlib.Path(sys.argv[4]), 4, pool, lambda: 1e6) as share:
    pathlib.Path(sys.argv[5]).write_text(str(share.lease.held))
    time.sleep(60)
"""


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
    """Hold the read of the available memory and the job count that the memory gives."""
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
    counts = [
        heavy.jobs_for_memory(224, reader([14.0])),
        heavy.jobs_for_memory(224, reader([191.0])),
        heavy.jobs_for_memory(8, reader([14.0])),
        heavy.jobs_for_memory(224, reader([None])),
        heavy.jobs_for_memory(224, reader([1.0])),
    ]
    expect(
        f"the memory less RESERVE_GB gives one job for each GB_PER_JOB, with the count of the caller as the maximum, "
        f"no value gives the count of the caller, and memory below the reserve gives one job: {counts}",
        counts == [24, 224, 8, 224, 1],
    )


def step_cases(expect: testing.Expect) -> None:
    """Hold the job count of a heavy step: one read of the memory, no wait, and one line of the memory."""
    with tempfile.TemporaryDirectory(prefix="heavy-step-") as root_text:
        root = Path(root_text)
        cases = (
            (14.0, 24, "memory: available 14.0 GB, 24 of 224 jobs"),
            (191.0, 224, "memory: available 191.0 GB, 224 of 224 jobs"),
            (None, 224, "so the step keeps 224 jobs"),
        )
        for value, expected, line in cases:
            reads: list[float | None] = []

            def read(value: float | None = value, reads: list[float | None] = reads) -> float | None:
                """Give the planted value, and keep each read."""
                reads.append(value)
                return value

            def step(read: heavy.Reader = read) -> int:
                """Run a heavy step with 224 jobs of the caller, and give the job count of the step."""
                with heavy.heavy_step(root, 224, read) as jobs:
                    return jobs

            jobs, printed = testing.captured(step)
            expect(
                f"with {value} GB of available memory and 224 jobs, the step reads the memory one time, does not "
                f"wait, and gives {expected} jobs: {jobs} jobs after {len(reads)} reads, {printed.strip()!r}",
                jobs == expected and len(reads) == 1 and printed.count("memory: ") == 1 and line in printed,
            )


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
            heavy.heavy_step(linked, 1, reader([32.0])),
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


def wait_for_lock(condition: Callable[[], bool]) -> bool:
    """Wait in steps of 1 ms until a condition of a lock holds, and tell if it holds (THE WAIT of assay.core.testing).

    A lock changes in less than 1 ms.  A step of 20 ms makes each case slower.
    """
    return testing.wait_until(condition, step_s=0.001)


def has_waiter(path: Path) -> bool:
    """Tell if a request of flock(2) on a file waits in the kernel, from the blocked rows of /proc/locks.

    A blocked request is a row with the second word "->".  Its seventh word names the device and the inode of the
    file.  A step that waits in the kernel cannot enter its block until the holder frees the lock.  The check then
    proves a wait with no sleep.  Complexity: O(rows of /proc/locks).
    """
    inode = f":{path.stat().st_ino}"
    with open(LOCKS_FILE, encoding="ascii") as rows:
        for row in rows:
            words = row.split()
            if len(words) > 6 and words[1] == "->" and words[6].endswith(inode):
                return True
    return False


def gate_cases(expect: testing.Expect) -> None:
    """Hold a session of the gate: a step of the gate goes first, and each other step waits."""
    with tempfile.TemporaryDirectory(prefix="heavy-gate-") as work_text:
        root = Path(work_text)
        entered = threading.Event()
        # The check after the lock found a session: the step held the lock of the heavy steps at that time.
        found_session = threading.Event()

        def is_session(gate_handle: IO[str]) -> bool:
            """Tell if a session runs, as the step does after it gets the lock, and mark a session that runs."""
            is_running = heavy.is_gate_running(gate_handle)
            if is_running:
                found_session.set()
            return is_running

        def other_step() -> None:
            """Run a heavy step that is not a step of the gate, and mark the entry into its block."""
            with heavy.heavy_step(root, 1, reader([32.0]), is_session=is_session):
                entered.set()

        with heavy.gate_session(root):
            with heavy.heavy_step(root, 1, reader([32.0]), is_gate_step=True):
                expect("a step of the gate takes the lock while its session runs", not is_free(heavy.lock_path(root)))
            worker = threading.Thread(target=other_step, daemon=True)
            worker.start()
            expect(
                "a different step waits while a session of the gate runs, and it holds no lock of the heavy steps",
                wait_for_lock(lambda: has_waiter(heavy.gate_path(root)))
                and not entered.is_set()
                and is_free(heavy.lock_path(root)),
            )
        worker.join(10.0)
        expect("the step goes on after the session ends", entered.is_set())
        entered.clear()
        with open(heavy.lock_path(root), "a", encoding="ascii") as busy:
            fcntl.flock(busy, fcntl.LOCK_EX)
            worker = threading.Thread(target=other_step, daemon=True)
            worker.start()
            expect(
                "a step waits for the lock of a step that runs",
                wait_for_lock(lambda: has_waiter(heavy.lock_path(root))) and not entered.is_set(),
            )
            with heavy.gate_session(root):
                fcntl.flock(busy, fcntl.LOCK_UN)
                expect(
                    "a step that gets the lock after a session started finds the session, frees the lock for the "
                    "gate, and waits",
                    found_session.wait(10.0)
                    and wait_for_lock(lambda: is_free(heavy.lock_path(root)))
                    and wait_for_lock(lambda: has_waiter(heavy.gate_path(root)))
                    and not entered.is_set(),
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


def locked_slots(pool: heavy.Pool) -> int:
    """Return the count of the slot files of a pool that a lock of flock(2) holds at this time."""
    return sum(heavy.is_held(pool.directory / f"{index:0{heavy.SLOT_WIDTH}d}") for index in range(pool.size))


def captured_lease(root: Path, want: int, pool: heavy.Pool, body: Callable[[heavy.Share], object]) -> object:
    """Run the body in a lease with much memory, with the line of the memory captured, and give its result."""

    def run() -> object:
        """Hold the lease for the body."""
        with heavy.leased(root, want, pool, reader([MUCH_MEMORY_GB])) as share:
            return None if share is None else body(share)

    result, _ = testing.captured(run)
    return result


def lease_cases(expect: testing.Expect) -> None:
    """Hold a lease: its cap, its slots in the pool, its growth while its job uses each slot, and its idle return."""
    with tempfile.TemporaryDirectory(prefix="heavy-lease-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 6)

        def first(share: heavy.Share) -> tuple[str, int, int, int]:
            """Hold a second lease in the first one, and give the cap, the slots and the locked files."""
            inner = captured_lease(root, 6, pool, lambda other: other.lease.held)
            return share.server.makeflags, share.lease.held, int(str(inner)), locked_slots(pool)

        first_result = captured_lease(root, 10, pool, first)
        flags, held, inner, locked = first_result if isinstance(first_result, tuple) else ("", 0, 0, -1)
        expect(
            f"a lease of 10 jobs in a pool of 6 slots has the cap 6, starts with at most START_SLOTS slots, and a "
            f"second lease gets only the free slots: {first_result}",
            flags.startswith("-j6 --jobserver-auth=fifo:")
            and 1 <= held <= heavy.START_SLOTS
            and 1 <= inner <= pool.size - held
            and locked == held,
        )
        expect("the slots of a lease are free after its block", locked_slots(pool) == 0)

        def grown(share: heavy.Share) -> tuple[int, int, int]:
            """Take each token that the lease gives, then give each one back, and give the slots at each point."""
            taken: list[jobserver.Slot] = []
            deadline = time.monotonic() + 10.0
            while len(taken) < share.jobs and time.monotonic() < deadline:
                slot = share.client.try_take()
                if slot is None:
                    time.sleep(0.01)
                    continue
                taken.append(slot)
            full = share.lease.held
            for slot in taken:
                share.client.give(slot)
            testing.wait_until(lambda: share.lease.held == 1, limit_s=10.0)
            return len(taken), full, share.lease.held

        grown_result = captured_lease(root, 6, pool, grown)
        expect(
            f"a job that uses each slot makes its lease grow to the cap, and the idle tokens go back to the pool "
            f"after IDLE_TICKS ticks, with one slot as the minimum: (jobs taken, slots full, slots idle) "
            f"{grown_result}",
            grown_result == (6, 6, 1),
        )


def kill_cases(expect: testing.Expect) -> None:
    """Hold the end of a holder on SIGKILL: the kernel frees each slot of its lease (THE POOL)."""
    with tempfile.TemporaryDirectory(prefix="heavy-kill-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 6)
        marker = root / "held.txt"
        words = [str(paths.REPO_ROOT / "utils"), str(pool.directory), str(pool.size), str(root), str(marker)]
        command = proc.Command([sys.executable, "-S", "-B", "-c", HOLDER, *words], timeout_s=proc.QUICK_S)
        running = proc.start(command)
        is_ready = testing.wait_until(marker.is_file, limit_s=20.0)
        held = int(marker.read_text(encoding="ascii")) if is_ready else -1
        locked = locked_slots(pool)
        os.kill(running.pid, signal.SIGKILL)
        with contextlib.suppress(proc.ToolFailed):
            running.wait()
        expect(
            f"a process with a lease holds its slots, and SIGKILL frees each of them: {held} held, {locked} locked "
            f"before, {locked_slots(pool)} after",
            held >= 1 and locked == held and locked_slots(pool) == 0,
        )


def fair_cases(expect: testing.Expect) -> None:
    """Hold the fair share of the pool: a lease gives its idle tokens above its share to a hungry lease (THE POOL).

    Job A starts alone and takes each slot of a pool of 8.  Job B then waits for its first slot.  Job A has a serial
    phase, and then both jobs use each slot that they hold.  No job then stays at one slot while the other holds idle
    tokens, and each one holds approximately its fair share.
    """
    with tempfile.TemporaryDirectory(prefix="heavy-fair-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 8)
        shares: dict[str, heavy.Share] = {}
        started = {name: threading.Event() for name in ("a", "b")}
        samples: list[tuple[int, int]] = []

        def job(name: str, phases: tuple[tuple[int, float], ...]) -> None:
            """Hold a lease of 8 jobs, and run each phase: its workers use the slots of the lease for its time."""
            with heavy.leased(root, 8, pool, reader([MUCH_MEMORY_GB])) as share:
                if share is None:
                    return
                shares[name] = share
                started[name].set()
                for workers, seconds in phases:
                    stop = threading.Event()
                    threads = [threading.Thread(target=cycled, args=(share.client, stop)) for _ in range(workers)]
                    for thread in threads:
                        thread.start()
                    time.sleep(seconds)
                    stop.set()
                    for thread in threads:
                        thread.join()

        def run() -> None:
            """Start job A, wait until it holds each slot, start job B, and sample the slots of the two jobs."""
            first = threading.Thread(target=job, args=("a", ((8, 1.5), (1, 1.0), (8, 2.5))))
            first.start()
            started["a"].wait(10.0)
            testing.wait_until(lambda: shares["a"].lease.held == pool.size, limit_s=10.0)
            second = threading.Thread(target=job, args=("b", ((8, 4.0),)))
            second.start()
            while first.is_alive():
                held_b = shares["b"].lease.held if "b" in shares else 0
                samples.append((shares["a"].lease.held, held_b))
                time.sleep(0.05)
            second.join()

        testing.captured(run)
        last = samples[-10:]
        holds = (
            bool(last)
            and max(a for a, _ in samples) == pool.size
            and all(a + b <= pool.size for a, b in samples)
            and min(b for _, b in last) >= pool.size // 2 - 1
            and max(a for a, _ in last) <= pool.size // 2 + 1
        )
        expect(
            f"a lease that took each slot gives its idle tokens above its fair share to a hungry lease, and the two "
            f"jobs then hold approximately half of the pool each: the last samples (a, b) {last}",
            holds,
        )


def priority_cases(expect: testing.Expect) -> None:
    """Hold THE PRIORITY OF THE GATE in the pool and THE TURNSTILE.

    A lease that is not of the gate gives its idle slots to a waiting lease of the gate at once.  An exclusive step
    that waits stops each new lease, and runs after the lease in flight.
    """
    with tempfile.TemporaryDirectory(prefix="heavy-priority-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 4)

        def gate_after_other() -> tuple[int, float]:
            """Hold a lease of each slot that is not of the gate, and start a lease of the gate in a thread."""
            got: list[tuple[int, float]] = []
            with heavy.leased(root, 4, pool, reader([MUCH_MEMORY_GB])) as other:
                testing.wait_until(lambda: other is not None and other.lease.held == pool.size, limit_s=5.0)

                def gate_lease() -> None:
                    """Wait for a lease of the gate, and keep its first slots and its wait."""
                    with heavy.leased(root, 4, pool, reader([MUCH_MEMORY_GB]), is_gate=True) as share:
                        if share is not None:
                            got.append((share.lease.held, share.wait_s))

                worker = threading.Thread(target=gate_lease)
                worker.start()
                worker.join(10.0)
            return got[0] if got else (0, -1.0)

        (held, wait_s), _ = testing.captured(gate_after_other)
        expect(
            f"a lease that holds each slot idle gives them to a waiting lease of the gate at once: {held} slots after "
            f"{wait_s:.2f} s",
            held >= 1 and 0.0 <= wait_s < heavy.IDLE_TICKS * heavy.TICK_S,
        )
        order: list[str] = []
        lease_free = threading.Event()

        def writer() -> None:
            """Run an exclusive step after the lease in flight."""
            with heavy.heavy_lock(root):
                order.append("step")

        def late_lease() -> None:
            """Start a lease after the step waits."""
            with heavy.leased(root, 1, pool, reader([MUCH_MEMORY_GB])):
                order.append("late lease")

        def run() -> None:
            """Hold a lease, start a waiting step, then a late lease, and end the first lease."""
            with heavy.leased(root, 1, pool, reader([MUCH_MEMORY_GB])):
                step = threading.Thread(target=writer)
                step.start()
                testing.wait_until(lambda: has_waiter(heavy.lock_path(root)), limit_s=5.0)
                late = threading.Thread(target=late_lease)
                late.start()
                time.sleep(4 * heavy.WAIT_S)
                order.append("first lease ends")
            lease_free.set()
            step.join(10.0)
            late.join(10.0)

        testing.captured(run)
        expect(
            f"a waiting exclusive step stops a new lease, and runs when the lease in flight ends: {order}",
            order == ["first lease ends", "step", "late lease"],
        )


def cycled(client: jobserver.Client, stop: threading.Event) -> None:
    """Use one slot of a jobserver after the other for a short job, until the stop event."""
    while not stop.is_set():
        with client.held(stop) as is_held:
            if not is_held:
                return
            time.sleep(0.02)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            memory_cases,
            step_cases,
            lock_cases,
            gate_cases,
            exit_cases,
            lease_cases,
            kill_cases,
            fair_cases,
            priority_cases,
        ),
    )
