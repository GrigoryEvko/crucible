"""jobserver — the GNU make jobserver of one heavy step, and a client of it for a pool of threads.

THE PROTOCOL
    A jobserver is a FIFO that holds one byte for each job that can start.
    A process gives its children the variable MAKEFLAGS with the words -jN
    and --jobserver-auth=fifo:PATH.  Each child opens the FIFO by its path.
    A child takes one byte before a job, and it gives the same byte back
    after the job.  Each process also has one implicit slot, which its parent
    gave it when the parent started it.  A process runs its first job on that
    slot, and it takes a byte for each other job.  The jobs of all the
    processes together are then at most N, and their memory stays that of
    one build of N jobs.  On 2026-10-17, ninja 1.13.2 obeyed the jobserver
    only when its command line gave no -j, and ctest 4.4.2 obeyed it also
    with -j N.

THE SERVER
    serving(jobs) makes the FIFO in a new private directory, and it writes
    jobs - 1 bytes into it.  The process of the server keeps the last slot
    as its own implicit slot.  The server keeps a descriptor of the FIFO open
    for the block.  Linux keeps the bytes of a FIFO only while a descriptor
    of it is open.  At the end of the block, also on an exception, the
    server closes its descriptor, and it removes the FIFO and its directory.
    A child with an open descriptor of the FIFO keeps that descriptor.

THE CLIENT
    client_of(environment) gives the client of the jobserver that MAKEFLAGS
    names, or None when MAKEFLAGS names no FIFO.  connected(path) gives the
    client of a FIFO, for the threads of the server itself.  Client.held()
    holds one slot for a block: the implicit slot when it is free, else one
    byte of the FIFO.  The client opens its own descriptor of the FIFO with
    O_NONBLOCK, and it waits for a byte with select(2) and a timeout of
    WAIT_S seconds.  A wait with a stop event ends when the event is set.

    A byte that a child holds when SIGKILL ends it does not come back.  The
    jobs of the heavy step then become fewer.  The implicit slot of each
    process keeps one job of that process alive.  A byte that does not come
    back then cannot stop the work of a process.

`guard selftest tests.core.test_jobserver` does a test of the module.
"""

import contextlib
import os
import select
import tempfile
import threading
from collections.abc import Iterator, Mapping
from dataclasses import dataclass
from pathlib import Path

# The byte of one token, as GNU make writes it.
TOKEN = b"+"
# The variable of the jobserver, and the word that names its FIFO (THE PROTOCOL).
VARIABLE = "MAKEFLAGS"
AUTH_PREFIX = "--jobserver-auth="
FIFO_PREFIX = "fifo:"
# The directory and the FIFO of a server (THE SERVER).  A write of at most PIPE_BUF bytes to a pipe is atomic.  A server
# then holds at most that many tokens.
DIRECTORY_PREFIX = "crucible-jobserver-"
FIFO_NAME = "jobs.fifo"
MAX_JOBS = 4096
# The longest wait of select(2) for a token, in seconds.  A stop event and a free implicit slot get a look after it.
WAIT_S = 0.1


class JobserverError(Exception):
    """The FIFO of a jobserver cannot be made, opened or read."""


@dataclass(frozen=True, slots=True)
class Server:
    """The jobserver of one block: the path of its FIFO and its jobs (THE SERVER)."""

    path: Path
    jobs: int

    @property
    def makeflags(self) -> str:
        """The value of MAKEFLAGS that gives a child the jobserver (THE PROTOCOL)."""
        return f"-j{self.jobs} {AUTH_PREFIX}{FIFO_PREFIX}{self.path}"


@dataclass(frozen=True, slots=True)
class Slot:
    """One slot that a client holds: the byte of the FIFO, or no byte for the implicit slot (THE CLIENT)."""

    token: bytes

    @property
    def is_implicit(self) -> bool:
        """Tell if the slot is the implicit slot of the process."""
        return not self.token


class Client:
    """A client of one jobserver: its descriptor of the FIFO, and the implicit slot of the process (THE CLIENT).

    Each thread of the process can call take(), give() and held().
    """

    __slots__ = ("_descriptor", "_is_implicit_free", "_lock", "path")

    def __init__(self, path: Path, descriptor: int) -> None:
        """Keep the path of the FIFO and an open descriptor of it.  The client owns the descriptor."""
        self.path = path
        self._descriptor = descriptor
        self._lock = threading.Lock()
        self._is_implicit_free = True

    def take(self, stop: threading.Event | None = None) -> Slot | None:
        """Wait for one slot, and return it.  With a stop event, return None when the event is set.

        Raises:
            JobserverError: If the FIFO gives an end of file.  The FIFO of a server in its block gives none
        """
        while True:
            if stop is not None and stop.is_set():
                return None
            with self._lock:
                if self._is_implicit_free:
                    self._is_implicit_free = False
                    return Slot(b"")
            try:
                token = os.read(self._descriptor, 1)
            except BlockingIOError:
                token = None
            if token:
                return Slot(token)
            if token == b"":
                raise JobserverError(f"the jobserver FIFO {self.path} gives an end of file, so its server ended")
            select.select([self._descriptor], [], [], WAIT_S)

    def give(self, slot: Slot) -> None:
        """Give a slot back: free the implicit slot, or write the byte of the slot into the FIFO."""
        if slot.is_implicit:
            with self._lock:
                self._is_implicit_free = True
            return
        os.write(self._descriptor, slot.token)

    @contextlib.contextmanager
    def held(self, stop: threading.Event | None = None) -> Iterator[bool]:
        """Hold one slot for the block, and give True.  With a stop event that is set, give False and hold nothing."""
        slot = self.take(stop)
        if slot is None:
            yield False
            return
        try:
            yield True
        finally:
            self.give(slot)

    def close(self) -> None:
        """Close the descriptor of the FIFO."""
        if self._descriptor >= 0:
            os.close(self._descriptor)
            self._descriptor = -1


@contextlib.contextmanager
def serving(jobs: int) -> Iterator[Server]:
    """Make a jobserver of `jobs` jobs for the block, and remove its FIFO at the end, also on an exception (THE SERVER).

    Raises:
        ValueError: If jobs is not in the range 1 to MAX_JOBS
        JobserverError: If the FIFO cannot be made or written
    """
    if not 1 <= jobs <= MAX_JOBS:
        raise ValueError(f"a jobserver takes 1 to {MAX_JOBS} jobs, not {jobs}")
    directory = Path(tempfile.mkdtemp(prefix=DIRECTORY_PREFIX))
    path = directory / FIFO_NAME
    descriptor = -1
    try:
        try:
            os.mkfifo(path, 0o600)
            descriptor = os.open(path, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
            os.write(descriptor, TOKEN * (jobs - 1))
        except OSError as problem:
            raise JobserverError(f"the jobserver FIFO {path} cannot be made: {problem}") from None
        yield Server(path, jobs)
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        path.unlink(missing_ok=True)
        with contextlib.suppress(OSError):
            directory.rmdir()


def client_path(environment: Mapping[str, str]) -> Path | None:
    """Return the FIFO that MAKEFLAGS of an environment names, or None when it names no FIFO (THE CLIENT).

    The last word --jobserver-auth= decides, as GNU make reads it.  A jobserver of two descriptors gives None.
    Complexity: linear in the words of MAKEFLAGS.
    """
    words = [word for word in environment.get(VARIABLE, "").split() if word.startswith(AUTH_PREFIX)]
    if not words:
        return None
    value = words[-1].removeprefix(AUTH_PREFIX)
    return Path(value.removeprefix(FIFO_PREFIX)) if value.startswith(FIFO_PREFIX) else None


@contextlib.contextmanager
def connected(path: Path) -> Iterator[Client]:
    """Give the client of a FIFO for the block, and close its descriptor at the end (THE CLIENT).

    Raises:
        JobserverError: If the FIFO cannot be opened
    """
    try:
        descriptor = os.open(path, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    except OSError as problem:
        raise JobserverError(
            f"the jobserver FIFO {path} cannot be opened: {problem}.  Run the command under its server, or remove "
            f"--jobserver-auth from {VARIABLE}"
        ) from None
    client = Client(path, descriptor)
    try:
        yield client
    finally:
        client.close()


@contextlib.contextmanager
def client_of(environment: Mapping[str, str]) -> Iterator[Client | None]:
    """Give the client of the jobserver that MAKEFLAGS of an environment names, or None, for the block (THE CLIENT).

    Raises:
        JobserverError: If MAKEFLAGS names a FIFO that cannot be opened
    """
    path = client_path(environment)
    if path is None:
        yield None
        return
    with connected(path) as client:
        yield client
