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
# The most time that a step of the case of the fair share waits for a state of the pool.  It is only the guard against
# a hang, and the verdict does not read a time.
STATE_LIMIT_S = 60.0
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
        no_group = scratch / "no-cgroup"
        planted.write_text("MemTotal:       33554432 kB\nMemAvailable:   16777216 kB\n", encoding="ascii")
        expect(
            "with no control group, the read gives the field MemAvailable in GB, and the ceiling the field MemTotal",
            heavy.available_gb(planted, no_group) == 16.0 and heavy.memory_ceiling_gb(planted, no_group) == 32.0,
        )
        planted.write_text("MemTotal:       33554432 kB\n", encoding="ascii")
        expect(
            "a file with no field MemAvailable, and a file that does not exist, give no value",
            heavy.available_gb(planted, no_group) is None and heavy.available_gb(scratch / "missing", no_group) is None,
        )
    counts = [
        heavy.jobs_for_memory(224, reader([14.0])),
        heavy.jobs_for_memory(224, reader([191.0])),
        heavy.jobs_for_memory(8, reader([24.0])),
        heavy.jobs_for_memory(224, reader([None])),
        heavy.jobs_for_memory(224, reader([1.0])),
    ]
    expect(
        f"the memory less RESERVE_GB gives one job for each GB_PER_JOB, with the count of the caller as the maximum, "
        f"no value gives the count of the caller, and memory below the reserve gives one job: {counts}",
        counts == [10, 224, 8, 224, 1],
    )
    plain = heavy.Pool(Path("/nonexistent"), 64)
    large = [
        heavy.jobs_for_memory(224, reader([14.0]), 1.7),
        heavy.budget_jobs(plain, 0.0, 14.0, 1.7, 224),
        heavy.budget_jobs(plain, 0.0, 14.0, heavy.GB_PER_JOB, 224),
        heavy.budget_jobs(plain, 0.0, None, 1.7, 224),
    ]
    expect(
        f"a larger memory of each job gives fewer jobs and less room for a growth, and no free memory and no ceiling "
        f"give no limit: {large}",
        large == [3, 3, 10, 224],
    )


def large_job_cases(expect: testing.Expect) -> None:
    """Hold a lease that gives the memory of one of its jobs: its cap, its most jobs and its line of the memory."""
    with tempfile.TemporaryDirectory(prefix="heavy-large-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 64)
        shares: list[tuple[int, int]] = []

        def run(gb_per_job: float) -> None:
            """Hold a lease of 32 jobs with 20 GB of memory, and keep its cap and its jobs."""
            with heavy.leased(root, 32, pool, reader([20.0]), gb_per_job=gb_per_job) as share:
                if share is not None:
                    shares.append((share.lease.cap, share.jobs))

        _, plain = testing.captured(lambda: run(heavy.GB_PER_JOB))
        _, large = testing.captured(lambda: run(1.7))
        expect(
            f"a lease with jobs of 1.7 GB in 20 GB of memory has the cap and the most jobs of that memory, and its line "
            f"gives the memory of one job, but a plain lease keeps its line: {shares}, {large.strip()!r}",
            shares == [(20, 20), (7, 7)]
            and "memory: available 20.0 GB, 7 of 32 jobs, 7 at most, 1.70 GB for each job" in large
            and "for each job" not in plain,
        )


def step_cases(expect: testing.Expect) -> None:
    """Hold the job count of a heavy step: one read of the memory, no wait, and one line of the memory."""
    with tempfile.TemporaryDirectory(prefix="heavy-step-") as root_text:
        root = Path(root_text)
        cases = (
            (14.0, 10, "memory: available 14.0 GB, 10 of 224 jobs"),
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


def captured_lease(
    root: Path, want: int, pool: heavy.Pool, body: Callable[[heavy.Share], object], is_capped: bool = False
) -> object:
    """Run the body in a lease with much memory, with the line of the memory captured, and give its result."""

    def run() -> object:
        """Hold the lease for the body."""
        with heavy.leased(root, want, pool, reader([MUCH_MEMORY_GB]), is_capped=is_capped) as share:
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
        capped_result = captured_lease(root, 3, pool, grown, is_capped=True)
        loose_result = captured_lease(root, 3, pool, grown)
        expect(
            f"a capped lease of 3 jobs in a pool of 6 slots never grows past its cap, and a lease with no cap grows to "
            f"the pool while no other lease waits: (jobs taken, slots full, slots idle) {capped_result} and "
            f"{loose_result}",
            capped_result == (3, 3, 1) and loose_result == (6, 6, 1),
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


class HeldJob:
    """A lease of the size of the pool in a thread of its own, whose workers each take one slot and hold it (THE POOL).

    The case sets the count of the workers.  A worker holds its slot until the case stops it, so the lease has no idle
    token apart from the tokens of the stopped workers.  The pump then never sees a token that is idle only because the
    host gave the thread of a worker no CPU.
    """

    def __init__(self, root: Path, pool: heavy.Pool, workers: int) -> None:
        """Keep the root, the pool and the count of the first workers.  The case starts the thread."""
        self.root = root
        self.pool = pool
        self.first_workers = workers
        self.share: heavy.Share | None = None
        self.is_ready = threading.Event()
        self.finish = threading.Event()
        self.workers: list[tuple[threading.Event, threading.Thread]] = []
        self.thread = threading.Thread(target=self.run)

    def run(self) -> None:
        """Hold the lease, start the first workers at once, and keep the lease until the case sets the finish event."""
        with heavy.leased(self.root, self.pool.size, self.pool, reader([MUCH_MEMORY_GB])) as share:
            if share is not None:
                self.share = share
                self.set_workers(self.first_workers)
                self.is_ready.set()
                self.finish.wait()
                self.set_workers(0)
        self.is_ready.set()

    @property
    def held(self) -> int:
        """The slots of the lease at this time, or 0 before the lease holds a slot."""
        return 0 if self.share is None else self.share.lease.held

    def set_workers(self, count: int) -> None:
        """Stop the last workers, or start new ones, until `count` workers hold a slot or wait for one.

        The thread of the job calls it at its start and at its end, and the case calls it between them.
        """
        while len(self.workers) > count:
            stop, thread = self.workers.pop()
            stop.set()
            thread.join()
        while len(self.workers) < count and self.share is not None:
            stop = threading.Event()
            thread = threading.Thread(target=held_until, args=(self.share.client, stop))
            thread.start()
            self.workers.append((stop, thread))


def fair_cases(expect: testing.Expect) -> None:
    """Hold the fair share of the pool: a lease gives its idle tokens above its share to a hungry lease (THE POOL).

    Job A starts alone with a worker for each slot of a pool of 8, and it takes each slot.  Job B then waits for its
    first slot, a hungry lease below its fair share of 4.  Job A stops half of its workers, so it holds idle tokens
    above its share.  Job A gives them back, job B takes the free slots, and each job holds approximately its share.
    Job A keeps one slot above its share when a stopped worker held its implicit slot, which is no token of the FIFO.
    Each step waits for a state of the pool and not for a time, and STATE_LIMIT_S is only the guard against a hang.  A
    worker holds its slot, so a host at a high load gives the pump no idle token, and the state then stays.
    """
    with tempfile.TemporaryDirectory(prefix="heavy-fair-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 8)
        fair = pool.size // 2
        first = HeldJob(root, pool, pool.size)
        second = HeldJob(root, pool, pool.size)
        samples: list[tuple[int, int]] = []
        steps: list[bool] = []

        def sampled() -> tuple[int, int]:
            """Give the slots of the two jobs at this time, and keep them."""
            sample = (first.held, second.held)
            samples.append(sample)
            return sample

        def is_shared() -> bool:
            """Tell if the jobs hold each slot of the pool, and each one its fair share with one slot more or less."""
            held_a, held_b = sampled()
            return held_a + held_b == pool.size and held_a <= fair + 1 and held_b >= fair - 1

        def peers_below_fair() -> int:
            """Give the hungry leases below their fair share that job A sees."""
            share = first.share
            return 0 if share is None else share.lease.demand().peers_below_fair

        def run() -> None:
            """Run the steps of the case, and end the two jobs."""
            first.thread.start()
            steps.append(first.is_ready.wait(STATE_LIMIT_S))
            steps.append(testing.wait_until(lambda: sampled()[0] == pool.size, limit_s=STATE_LIMIT_S))
            second.thread.start()
            steps.append(testing.wait_until(lambda: peers_below_fair() == 1, limit_s=STATE_LIMIT_S))
            first.set_workers(fair)
            steps.append(testing.wait_until(is_shared, limit_s=STATE_LIMIT_S))
            # Four ticks of each pump: the state stays, because no worker gives a slot back.
            time.sleep(4 * heavy.TICK_S)
            steps.append(is_shared())
            first.finish.set()
            second.finish.set()
            first.thread.join()
            second.thread.join()

        testing.captured(run)
        expect(
            f"a lease that took each slot gives its idle tokens above its fair share to a hungry lease, and the two "
            f"jobs then hold approximately half of the pool each: the steps {steps}, the last samples (a, b) "
            f"{samples[-5:]}",
            steps == [True] * 5 and all(held_a + held_b <= pool.size for held_a, held_b in samples),
        )


def weight_cases(expect: testing.Expect) -> None:
    """Hold THE WEIGHT: the fair share of a lease is the pool times its weight over the sum of the weights of its class.

    Three hungry leases of the gate share a pool of 12: one of the weight 2 and two of the weight 1.  The fair share of
    the first is 6 slots, and of each other one 3.  The case calls the pump counts on the markers, with no time.
    """
    with tempfile.TemporaryDirectory(prefix="heavy-weight-") as work_text:
        pool = heavy.Pool(Path(work_text) / "slots", 12)
        pool.directory.mkdir()
        heavy_lease = heavy.Lease(pool, 12, True, weight=2)
        light = heavy.Lease(pool, 12, True)
        other = heavy.Lease(pool, 12, True)
        leases = (heavy_lease, light, other)
        old_marker = pool.directory / heavy.LEASES_NAME / f"old{heavy.MARKER_SUFFIX}"
        try:
            for lease in leases:
                lease.register()
                lease.take(1)
                lease.hungry = True
            heavy_lease.take(2)
            for lease in leases:
                lease.mark()
            grown = heavy.grown_count(heavy_lease, heavy_lease.demand(), pool.size)
            heavy_lease.take(2)
            heavy_lease.mark()
            seen_below = light.demand().peers_below_fair
            heavy_lease.take(3)
            heavy_lease.mark()
            pulled = heavy.pulled_count(heavy_lease, heavy_lease.demand(), heavy_lease.held, 0)
            with open(old_marker, "w", encoding="ascii") as handle:
                fcntl.flock(handle, fcntl.LOCK_EX)
                handle.write("     1     12 hungry 1\n")
                handle.flush()
                with_old = heavy_lease.demand()
            weights = sorted(
                words[4]
                for path in old_marker.parent.glob(f"*{heavy.MARKER_SUFFIX}")
                if len(words := path.read_text(encoding="ascii").split()) == 6
            )
        finally:
            for lease in leases:
                lease.close()
        expect(
            f"a lease of the weight 2 among two leases of the weight 1 grows to its share of 6 of 12 slots, a peer sees "
            f"it below its share at 5 slots, and at 8 slots it gives the idle tokens above 6: it grows by {grown}, the "
            f"peer sees {seen_below} leases below their share, and it gives {pulled}",
            grown == 3 and seen_below == 2 and pulled == 2,
        )
        expect(
            f"each marker holds the weight of its lease, and a marker that does not read as numbers counts as a lease "
            f"of the weight 1 that does not wait: the weights {weights}, the sum {with_old.weight} and the waiting leases "
            f"{with_old.waiting_gate}",
            weights == ["1", "1", "2"] and with_old.weight == 5 and with_old.waiting_gate == 2,
        )


def planted_leases(pool: heavy.Pool, *gb_per_job: float) -> list[heavy.Lease]:
    """Register one lease of the gate with a cap of the pool size for each memory of a job, and return them."""
    leases = [heavy.Lease(pool, pool.size, True, gb_per_job=gb) for gb in gb_per_job]
    for lease in leases:
        lease.register()
    return leases


def budget_cases(expect: testing.Expect) -> None:
    """Hold THE BUDGET: the reservations of the pool stay at most its ceiling less RESERVE_GB, and a growth fits in the
    free memory.

    The cases plant leases in a pool and call the admission of the lease directly, with no thread and no time.
    """
    with tempfile.TemporaryDirectory(prefix="heavy-budget-") as work_text:
        work = Path(work_text)
        pool = heavy.Pool(work / "slots", 128, ceiling_gb=40.0)
        pool.directory.mkdir()
        leases = planted_leases(pool, 2.0, 2.0, 2.0, 2.0)
        taken = [lease.take_within_budget(8, reader([40.0])) for lease in leases]
        reserved = sum(lease.held * lease.gb_per_job for lease in leases)
        for lease in leases:
            lease.close()
        expect(
            f"four leases of jobs of 2 GB that start together, before a job holds memory, reserve at most the ceiling "
            f"of 40 GB less RESERVE_GB: they take {taken} and reserve {reserved:.0f} GB",
            taken == [8, 8, 0, 0] and reserved <= 40.0 - heavy.RESERVE_GB,
        )
        steady = heavy.Pool(work / "steady", 128, ceiling_gb=100.0)
        steady.directory.mkdir()
        running, growing = planted_leases(steady, 1.0, 1.0)
        running.take(50)
        running.mark()
        grown = growing.take_within_budget(40, reader([50.0]))
        running.close()
        growing.close()
        expect(
            f"when the 50 jobs of 1 GB of one lease hold their memory and 50 GB stay free, a second lease grows by 40 "
            f"jobs, so the pool does not stop at half of the memory: it takes {grown}",
            grown == 40,
        )
        alone = heavy.Pool(work / "alone", 128, ceiling_gb=100.0)
        alone.directory.mkdir()
        (lone,) = planted_leases(alone, 1.0)
        short = lone.take_within_budget(32, reader([20.0]))
        lone.close()
        expect(
            f"when other processes use the memory and 20 GB stay free, a growth fits in the free memory less "
            f"RESERVE_GB: it takes {short} of 32",
            short == 12,
        )
        markers = pool.directory / heavy.LEASES_NAME
        dead = markers / f"dead{heavy.MARKER_SUFFIX}"
        dead.write_text("    30     32 1 1      1   1024\n", encoding="ascii")
        (fresh,) = planted_leases(pool, 1.0)
        with_dead = fresh.take_within_budget(32, reader([40.0]))
        fresh.give(fresh.held)
        fresh.mark()
        with open(markers / f"old{heavy.MARKER_SUFFIX}", "w", encoding="ascii") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            handle.write("    10     32 1 1      1\n")
            handle.flush()
            with_old = fresh.take_within_budget(32, reader([40.0]))
        fresh.close()
        expect(
            f"a marker with no lock is the marker of a dead lease, so its reservation goes with it, and a live marker "
            f"of older code with no memory field counts each slot at UNKNOWN_GB_PER_JOB: {with_dead} and {with_old}",
            with_dead == 32 and not dead.exists() and with_old == 15,
        )
        phases = heavy.Pool(work / "phases", 64, ceiling_gb=40.0)
        phases.directory.mkdir()
        builder, peer = planted_leases(phases, 2.0, 1.0)
        built = builder.take_within_budget(16, reader([40.0]))
        before = peer.take_within_budget(32, reader([40.0]))
        builder.set_gb_per_job(1.0)
        after = peer.take_within_budget(32, reader([40.0]))
        rows = [
            path.read_text(encoding="ascii").split() for path in phases.directory.glob(f"leases/*{heavy.MARKER_SUFFIX}")
        ]
        builder.close()
        peer.close()
        expect(
            f"a lease that moves from its build to its tests lowers its memory of a job, and its marker and its "
            f"reservation follow, so a peer grows into the freed memory: the builder takes {built}, the peer {before} "
            f"and then {after} more, and the markers give the megabytes {sorted(row[5] for row in rows)}",
            built == 16 and before == 0 and after == 16 and sorted(row[5] for row in rows) == ["1024", "1024"],
        )
        tiny = heavy.Pool(work / "tiny", 8, ceiling_gb=4.0)
        tiny.directory.mkdir()
        first, second = planted_leases(tiny, 1.0, 1.0)
        minimum = [first.take_within_budget(4, reader([4.0])), second.take_within_budget(4, reader([4.0]))]
        first.close()
        second.close()
        expect(
            f"a lease with no slot takes one slot when no other lease holds a reservation, also with a ceiling below "
            f"the reserve, and waits when one does: {minimum}",
            minimum == [1, 0],
        )
        cgroups = work / "cgroup"
        group = cgroups / "user.slice" / "user-1000.slice"
        (group / "app.scope").mkdir(parents=True)
        gib = 1024**3
        files = {
            cgroups / "user.slice" / "memory.max": "max\n",
            cgroups / "user.slice" / "memory.current": f"{31 * gib}\n",
            group / "memory.max": f"{40 * gib}\n",
            group / "memory.current": f"{30 * gib}\n",
            group / "memory.stat": f"anon {19 * gib}\ninactive_file {10 * gib}\n",
            group / "app.scope" / "memory.max": "max\n",
            group / "app.scope" / "memory.current": f"{1 * gib}\n",
        }
        for path, text in files.items():
            path.write_text(text, encoding="ascii")
        own = work / "self-cgroup"
        own.write_text("0::/user.slice/user-1000.slice/app.scope\n", encoding="ascii")
        meminfo = work / "meminfo"
        meminfo.write_text(f"MemTotal: {64 * 1024 * 1024} kB\nMemAvailable: {50 * 1024 * 1024} kB\n", encoding="ascii")
        chain = [path.relative_to(cgroups).as_posix() for path in heavy.cgroup_chain(own, cgroups)]
        free = heavy.available_gb(meminfo, own, cgroups)
        ceiling = heavy.memory_ceiling_gb(meminfo, own, cgroups)
        expect(
            f"the chain of control groups goes from the own group to the user slice, the free memory is the smallest "
            f"of MemAvailable and each limit less its use plus its inactive page cache, and the ceiling is the smallest "
            f"of MemTotal and each limit: the chain {chain}, free {free} GB, ceiling {ceiling} GB",
            chain == ["user.slice/user-1000.slice/app.scope", "user.slice/user-1000.slice", "user.slice"]
            and free == 20.0
            and ceiling == 40.0,
        )
        (group / "memory.high").write_text(f"{36 * gib}\n", encoding="ascii")
        (group / "app.scope" / "memory.high").write_text("max\n", encoding="ascii")
        throttled_free = heavy.available_gb(meminfo, own, cgroups)
        throttled_ceiling = heavy.memory_ceiling_gb(meminfo, own, cgroups)
        (group / "memory.high").write_text(f"{48 * gib}\n", encoding="ascii")
        above_free = heavy.available_gb(meminfo, own, cgroups)
        above_ceiling = heavy.memory_ceiling_gb(meminfo, own, cgroups)
        expect(
            f"a group whose memory.high is below its memory.max limits the pool at memory.high, where the kernel "
            f"throttles each task of the group, and a memory.high of max or above memory.max changes nothing: free "
            f"{throttled_free} and {above_free} GB, ceiling {throttled_ceiling} and {above_ceiling} GB",
            throttled_free == 16.0 and throttled_ceiling == 36.0 and above_free == 20.0 and above_ceiling == 40.0,
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


def start_cases(expect: testing.Expect) -> None:
    """Hold the start of a lease beside a waiting lease of the gate: it starts with one free slot (THE POOL)."""
    with tempfile.TemporaryDirectory(prefix="heavy-start-") as work_text:
        root = Path(work_text)
        pool = heavy.Pool(root / "slots", 8)
        waiting = heavy.Lease(pool, 8, True)
        waiting.register()
        stop = threading.Event()
        timer = threading.Timer(3.0, stop.set)
        timer.start()

        def run() -> int:
            """Wait for a lease that is not of the gate, and give its slots, or 0 when the stop came first."""
            with heavy.leased(root, 8, pool, reader([MUCH_MEMORY_GB]), stop) as share:
                return 0 if share is None else share.lease.held

        held, _ = testing.captured(run)
        timer.cancel()
        waiting.close()
        expect(
            f"a lease that is not of the gate starts with one free slot while a lease of the gate waits: {held} slots",
            held == 1,
        )


def held_until(client: jobserver.Client, stop: threading.Event) -> None:
    """Take one slot of a jobserver, and hold it until the stop event."""
    with client.held(stop) as is_held:
        if is_held:
            stop.wait()


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            memory_cases,
            large_job_cases,
            step_cases,
            lock_cases,
            gate_cases,
            exit_cases,
            lease_cases,
            kill_cases,
            fair_cases,
            weight_cases,
            budget_cases,
            priority_cases,
            start_cases,
        ),
    )
