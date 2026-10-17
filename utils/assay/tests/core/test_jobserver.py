"""The test of the jobserver of a heavy step (assay.core.jobserver).

`guard selftest tests.core.test_jobserver` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The cases make a jobserver, and hold the value of MAKEFLAGS, the tokens of
the FIFO and the implicit slot.  Threads with more jobs than the tokens must
run at most the jobs of the server at one time.  A wait with a stop event
must end when the event is set.  The FIFO must go at the end of the block,
also after an exception.  A child process must take a token through
MAKEFLAGS and give it back.
"""

import select
import sys
import threading
import time
from pathlib import Path

from assay.core import jobserver, paths, proc, testing

# The jobs of the server of the cases, the workers that share them, and the time of one job, in seconds.
JOBS = 3
WORKERS = 12
JOB_S = 0.05
# The wait before the stop event of a case is set, in seconds.
STOP_AFTER_S = 0.3
# A child that takes two slots through MAKEFLAGS: its implicit slot and one token.  It gives them back and exits 0.
CHILD = """import os, sys
sys.path.insert(0, sys.argv[1])
from assay.core import jobserver
with jobserver.client_of(os.environ) as client:
    first, second = client.take(), client.take()
    print("implicit" if first.is_implicit else "token", "token" if not second.is_implicit else "implicit")
    client.give(second)
    client.give(first)
"""


def protocol_cases(expect: testing.Expect) -> None:
    """Hold MAKEFLAGS, the tokens of the FIFO, the implicit slot and a wait that a stop event ends."""
    with jobserver.serving(JOBS) as server, jobserver.connected(server.path) as client:
        expect(
            f"the server gives MAKEFLAGS with -j and the FIFO of the block: {server.makeflags}",
            server.makeflags == f"-j{JOBS} --jobserver-auth=fifo:{server.path}" and server.path.exists(),
        )
        slots = [client.take() for _ in range(JOBS)]
        expect(
            "the first slot of a client is its implicit slot, and the FIFO holds one token less than the jobs",
            [slot is not None and slot.is_implicit for slot in slots] == [True] + [False] * (JOBS - 1),
        )
        stop = threading.Event()
        timer = threading.Timer(STOP_AFTER_S, stop.set)
        started = time.monotonic()
        timer.start()
        late = client.take(stop)
        waited_s = time.monotonic() - started
        timer.join()
        expect(
            f"with each slot held, a wait with a stop event ends with no slot when the event is set: {waited_s:.2f} s",
            late is None and STOP_AFTER_S <= waited_s < STOP_AFTER_S + 5.0,
        )
        for slot in slots:
            if slot is not None:
                client.give(slot)
        again = [client.take() for _ in range(JOBS)]
        expect(
            "each slot that a client gives back can be taken again",
            all(slot is not None for slot in again) and client.take(stop) is None,
        )
        for slot in again:
            if slot is not None:
                client.give(slot)


def try_take_cases(expect: testing.Expect) -> None:
    """Hold a take that does not wait, and the descriptor of the FIFO for select(2) (THE CLIENT)."""
    with jobserver.serving(2) as server, jobserver.connected(server.path) as client:
        first, second = client.try_take(), client.try_take()
        started = time.monotonic()
        third = client.try_take()
        waited_s = time.monotonic() - started
        expect(
            f"try_take gives the implicit slot, then a token, then None at once with each slot held: {waited_s:.3f} s",
            first is not None
            and first.is_implicit
            and second is not None
            and not second.is_implicit
            and third is None
            and waited_s < 1.0,
        )
        empty, _, _ = select.select([client.fileno()], [], [], 0)
        if second is not None:
            client.give(second)
        full, _, _ = select.select([client.fileno()], [], [], 0)
        expect(
            "select(2) calls the descriptor of the FIFO readable only when the FIFO holds a token",
            not empty and full == [client.fileno()],
        )
        again = client.try_take()
        expect(
            "a token that a client gives back can be taken again at once", again is not None and not again.is_implicit
        )
        for slot in (again, first):
            if slot is not None:
                client.give(slot)


def bound_cases(expect: testing.Expect) -> None:
    """Hold the bound: workers with more jobs than the slots run at most the jobs of the server at one time."""
    with jobserver.serving(JOBS) as server, jobserver.connected(server.path) as client:
        lock = threading.Lock()
        running = 0
        peak = 0

        def work() -> None:
            """Hold one slot for one job, and keep the peak of the jobs at one time."""
            nonlocal running, peak
            with client.held() as is_held:
                if not is_held:
                    return
                with lock:
                    running += 1
                    peak = max(peak, running)
                time.sleep(JOB_S)
                with lock:
                    running -= 1

        threads = [threading.Thread(target=work) for _ in range(WORKERS)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        expect(
            f"{WORKERS} workers on a server of {JOBS} jobs run at most {JOBS} jobs at one time: the peak is {peak}",
            1 <= peak <= JOBS and running == 0,
        )


def removal_cases(expect: testing.Expect) -> None:
    """Hold the end of a server: its FIFO and its directory go at the end, also after an exception."""
    with jobserver.serving(1) as server:
        kept = server.path
        expect("a server of one job holds no token, and its FIFO exists in the block", kept.exists())
    expect("the FIFO and its directory go at the end of the block", not kept.exists() and not kept.parent.exists())

    class Planted(Exception):
        """The exception of the block of the case."""

    gone = Path()
    try:
        with jobserver.serving(2) as server:
            gone = server.path
            raise Planted
    except Planted:
        pass
    expect(
        "the FIFO and its directory go after an exception in the block",
        gone != Path() and not gone.exists() and not gone.parent.exists(),
    )
    try:
        with jobserver.serving(0):
            pass
        refused = False
    except ValueError:
        refused = True
    expect("a server of no job is refused", refused)


def environment_cases(expect: testing.Expect) -> None:
    """Hold the FIFO of MAKEFLAGS: no variable, no FIFO, the last word, and a FIFO that is not there."""
    expect(
        "an environment with no MAKEFLAGS, with no --jobserver-auth, or with two descriptors names no FIFO",
        jobserver.client_path({}) is None
        and jobserver.client_path({"MAKEFLAGS": "-j8"}) is None
        and jobserver.client_path({"MAKEFLAGS": "-j8 --jobserver-auth=3,4"}) is None,
    )
    expect(
        "the last --jobserver-auth of MAKEFLAGS names the FIFO",
        jobserver.client_path({"MAKEFLAGS": "-j8 --jobserver-auth=fifo:/a --jobserver-auth=fifo:/b"}) == Path("/b"),
    )
    with jobserver.client_of({}) as client:
        expect("an environment with no FIFO gives no client", client is None)
    try:
        with jobserver.client_of({"MAKEFLAGS": "-j2 --jobserver-auth=fifo:/nonexistent/jobs.fifo"}):
            pass
        problem = ""
    except jobserver.JobserverError as error:
        problem = str(error)
    expect(f"a FIFO that is not there gives an error that names it: {problem}", "/nonexistent/jobs.fifo" in problem)


def child_cases(expect: testing.Expect) -> None:
    """Hold a child process that takes a token through MAKEFLAGS and gives it back."""
    utils = str(paths.REPO_ROOT / "utils")
    with jobserver.serving(2) as server:
        command = proc.Command(
            [sys.executable, "-S", "-B", "-c", CHILD, utils],
            timeout_s=proc.QUICK_S,
            env=proc.environment({jobserver.VARIABLE: server.makeflags}),
        )
        completed = proc.run(command)
        with jobserver.connected(server.path) as client:
            stop = threading.Event()
            stop.set()
            mine, token = client.take(), client.take()
            late = client.take(stop)
        expect(
            f"a child takes its implicit slot and the token of MAKEFLAGS, and the token comes back: "
            f"{completed.out.strip()!r}",
            completed.exit == 0
            and completed.out.split() == ["implicit", "token"]
            and mine is not None
            and mine.is_implicit
            and token is not None
            and not token.is_implicit
            and late is None,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (protocol_cases, try_take_cases, bound_cases, removal_cases, environment_cases, child_cases),
    )
