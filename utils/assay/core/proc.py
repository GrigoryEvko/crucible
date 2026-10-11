"""proc — the one module of the package that starts a process.

THE TOOL RUN
    run(command) starts one Command and waits for its end.  A Command holds
    the program and its arguments as a list of words, which no shell reads.
    It also holds the time limit, the working directory, the environment,
    the bytes of the standard input and the destination of each output.
    run() returns a Completed: the exit, the bytes of each captured output,
    the wall time, the CPU time and the peak memory from wait4, and the raw
    wait status.  The exit is the status, or minus the number of the signal
    that ended the command, as subprocess gives it.  run() raises CannotStart
    when the program cannot start, and TimedOut when the command runs past
    its time limit.  require(completed) raises ToolFailed when the exit is
    not 0, and else it returns the completed run.

THE TIME LIMIT
    Each Command has a time limit, timeout_s, which counts from the start.
    The command runs in a process group of its own.  At the limit, the
    module sends the stop signal to the group and waits grace_s for the end
    of the command.  Then it sends SIGKILL to the group.  So no process of
    the group runs on after the limit.  The stop signal is SIGTERM, or SIGINT
    for a Command with stop=Stop.INTERRUPT: afl-fuzz writes its state and
    ends on SIGINT.  Running.stop() sends the same signals in the same
    sequence.  QUICK_S, TOOL_S and BUILD_S are the
    limits of three classes of work: a query that a tool answers at once,
    one run of a tool, and a build or a test run of the tree.  The module
    reaps the command only after its last signal, so the process group keeps
    its identity while the module can send a signal to it.

AN INTERRUPT
    The terminal sends SIGINT only to its foreground process group, and the
    command runs in a group of its own.  So a KeyboardInterrupt in the wait
    sends SIGINT to the group of the command, and the wait goes on.  The
    command can then stop in its own way: ctest stops its tests and prints
    its summary, and ninja prints that the build stopped.  When the command
    ends, the wait raises the KeyboardInterrupt again.  The time limit still
    applies.  A second KeyboardInterrupt, or another exception in the wait,
    sends SIGKILL to the group, and then the exception goes on.

THE ENVIRONMENT
    A child gets the environment of this process with no change, or the full
    mapping of its Command.  The module removes each variable of
    REPOSITORY_VARIABLES from that environment.  Such a variable tells git
    which repository to use, whatever directory git runs in, so a child that
    runs git would read or write that repository.  On 2026-09-24 an exported
    GIT_DIR sent the commits of one self-test into the real repository and
    emptied its main branch.  environment(overrides) gives the environment of
    this process with the overrides, for a Command that needs more variables.

THE STREAMS
    stdin    The bytes of the Command go to the standard input of the command,
             which then ends.  With no bytes, the standard input is /dev/null.
    stdout   Output.CAPTURE keeps the bytes in the result.  Output.INHERIT
    stderr   writes to the stream of this process.  Output.DISCARD writes to
             /dev/null.  A path writes a new file at that path.  For stderr,
             Output.MERGE writes to the destination of stdout.  For stdout
             with start(), Output.STREAM gives the lines to the caller.
    Completed.out and Completed.err decode a captured output as UTF-8, with
    U+FFFD in place of a byte that is not UTF-8, and they change \\r\\n and
    \\r to \\n, as the text mode of subprocess does.

A LONG RUN
    start(command) starts one Command and returns a Running.  A loop over
    the Running gives the lines of the standard output, each with its \\n,
    when the stdout of the Command is Output.STREAM.  Running.wait() waits
    for the end and returns the Completed, or it raises TimedOut at the
    limit.  The limit counts from the start, also while the caller reads
    the lines.  The Completed of a stream holds the lines that the loop did
    not give, as UTF-8.  Running.stop() stops the group at once, as the
    limit does.  run(command) is start(command).wait().

    Each Running reads and writes its pipes in one loop with a selector, so
    no pipe can fill and stop the command.  A pidfd of the command (Linux
    5.3) tells the loop the end of the command, and the loop reaps the
    command only at the end.

THE TWO STARTS OUTSIDE THIS MODULE
    loop step, loop test-step and loop meter-compiler put a meter in front of
    one compile, one link or one test.  measure() of assay.core.meter starts
    that command with os.posix_spawnp and reads its usage with os.wait4, and
    loop step runs a command that writes no output in its own place with
    os.execvp.  These two starts do not import this module, because CMake
    runs loop step for each compile and each link.  On 2026-10-08, pinned to
    one core, an import of this module made loop step slower at p50: from
    8.36 ms to 8.63 ms for a compile, and from 6.16 ms to 6.38 ms for a
    command in its own place (40 runs after 3 warm runs).  A meter must not
    change its step, so these starts keep the environment that the build tool
    gives, and they keep the process group of the meter: ninja and make
    interrupt the process group of a job.  They have no time limit, because
    the build tool or ctest owns the life of the step, and RLIMIT_CPU of the
    meter stops a step that runs away.

THE JOB COUNT
    DEFAULT_JOBS is the one default count of the parallel work of the package.
    That work is a build, a test run, a parse build and the compiles of a
    measure.  The error threshold of the rule compile-jobs of
    utils/registry.txt is the count of compile jobs at one time.  That rule
    holds DEFAULT_JOBS, and the jobs of each build preset and each test preset
    of CMakePresets.json, equal to it.  This module does not read the
    registry, because each run of `loop fixture` imports it.  The count is low,
    because large virtual machines share the build host with the tree.  On
    2026-10-09 the host held 22 GB of available memory with no build, and a
    compile of the tree can take 1.3 GB.  A build runs no more compiles than
    the depth of the Ninja pool compile (cmake/JobPools.cmake), also with a
    larger -j N.

WHY THE MODULE IMPORTS LITTLE
    loop fixture runs for each negative fixture of each test run, and it
    imports this module.  So the module imports only os and time at its top,
    and start() imports subprocess, selectors and signal when it runs.  The
    records are classes with __slots__ and no dataclass, because dataclasses
    costs 21 ms to import.  The words of a destination are constants of the
    class Output and no enum, because enum costs 1.1 ms (both measured with
    -X importtime on the build host).  TYPE_CHECKING is False when the module
    runs, and the type checker reads the imports of its block.
"""

import os
import time

TYPE_CHECKING = False
if TYPE_CHECKING:
    import subprocess

# The default count of parallel jobs of a build, a test run or a measure (THE JOB COUNT).  The rule compile-jobs holds
# it equal to the error threshold of its row in utils/registry.txt.
DEFAULT_JOBS = 16
# The time limits of three classes of work, in seconds (THE TIME LIMIT).
QUICK_S = 120.0
TOOL_S = 900.0
BUILD_S = 7200.0
# The wait between SIGTERM and SIGKILL to the group of a command at its limit, in seconds.
KILL_GRACE_S = 5.0
# The longest wait for the end of the outputs of a group after SIGKILL, in seconds.  A process that left the group
# can keep a pipe open, and the module then closes the pipe.
DRAIN_S = 5.0
# The bytes of one write to the standard input of a command.  A write of at most PIPE_BUF bytes to a pipe that the
# selector calls writable does not block.
STDIN_CHUNK = 4096
# The bytes of one read of an output pipe.
READ_CHUNK = 65536
# Each variable that tells git which repository, index or object store to use, whatever directory it runs in
# (THE ENVIRONMENT).
REPOSITORY_VARIABLES = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_NAMESPACE",
    "GIT_CEILING_DIRECTORIES",
)
# The phases of the end of a run: no signal yet, the stop signal sent, SIGKILL sent.
RUNNING, TERMINATED, KILLED = 0, 1, 2


class Stop:
    """The words of the first signal that stops the group of a command (THE TIME LIMIT)."""

    TERMINATE = "terminate"
    INTERRUPT = "interrupt"


STOP_WORDS = frozenset({Stop.TERMINATE, Stop.INTERRUPT})


class Output:
    """The words of the destination of an output of a command (THE STREAMS)."""

    CAPTURE = "capture"
    INHERIT = "inherit"
    DISCARD = "discard"
    MERGE = "merge"
    STREAM = "stream"


STDOUT_WORDS = frozenset({Output.CAPTURE, Output.INHERIT, Output.DISCARD, Output.STREAM})
STDERR_WORDS = frozenset({Output.CAPTURE, Output.INHERIT, Output.DISCARD, Output.MERGE})
KEPT_WORDS = frozenset({Output.CAPTURE, Output.STREAM})


class Command:
    """One command: its words, its time limit, its directory, its environment, its input and its outputs."""

    __slots__ = ("argv", "timeout_s", "cwd", "env", "stdin", "stdout", "stderr", "grace_s", "stop")

    def __init__(self, argv: list[str] | tuple[str, ...], *, timeout_s: float,
                 cwd: str | os.PathLike[str] | None = None, env: dict[str, str] | None = None,
                 stdin: bytes = b"", stdout: str | os.PathLike[str] = Output.CAPTURE,
                 stderr: str | os.PathLike[str] = Output.CAPTURE, grace_s: float = KILL_GRACE_S,
                 stop: str = Stop.TERMINATE) -> None:
        """Hold the parts of one command.

        Args:
            argv: The program and its arguments.  A word can be a path
            timeout_s: The time limit, a finite number of seconds from the start, more than 0
            cwd: The working directory, or None for the directory of this process
            env: The full environment of the command, or None for the environment of this process
            stdin: The bytes of the standard input
            stdout: A word of Output other than MERGE, or a path
            stderr: A word of Output other than STREAM, or a path
            grace_s: The wait between the stop signal and SIGKILL at the limit, in seconds
            stop: A word of Stop: the first signal at the limit and of Running.stop()

        Raises:
            ValueError: If argv is empty, a limit is not permitted, a destination is not permitted, or the stop word
                is not a word of Stop
        """
        words = tuple(os.fspath(word) for word in argv)
        if not words or not all(isinstance(word, str) and word for word in words):
            raise ValueError(f"a command has a program and its arguments as words that are not empty, not {argv!r}")
        if not 0 < timeout_s < float("inf") or not 0 <= grace_s < float("inf"):
            raise ValueError(f"the command {words[0]} needs a finite time limit above 0 and a finite grace, not "
                             f"{timeout_s!r} and {grace_s!r}")
        if not isinstance(stdin, bytes):
            raise ValueError(f"the standard input of the command {words[0]} is bytes, not {type(stdin).__name__}")
        check_destination(words[0], "stdout", stdout, STDOUT_WORDS)
        check_destination(words[0], "stderr", stderr, STDERR_WORDS)
        if stop not in STOP_WORDS:
            raise ValueError(f"the stop word of the command {words[0]} is {stop!r}, and the words are "
                             f"{', '.join(sorted(STOP_WORDS))}")
        self.argv = words
        self.timeout_s = float(timeout_s)
        self.cwd = None if cwd is None else os.fspath(cwd)
        self.env = env
        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr
        self.grace_s = float(grace_s)
        self.stop = stop

    def shown(self) -> str:
        """Return the words of the command as one line for a message, with each word that holds a space quoted."""
        return " ".join(repr(word) if " " in word else word for word in self.argv)

    def __repr__(self) -> str:
        """Return the command for a message."""
        return f"Command({self.shown()!r}, timeout_s={self.timeout_s:g})"


class Completed:
    """The result of one command: its exit, its kept outputs, its times, its peak memory and its wait status."""

    __slots__ = ("command", "wait_status", "exit", "stdout", "stderr", "wall_s", "user_s", "system_s", "peak_kb")

    def __init__(self, command: Command, wait_status: int, stdout: bytes, stderr: bytes, wall_s: float,
                 user_s: float, system_s: float, peak_kb: int) -> None:
        """Hold the result of one command, from its raw wait status and its resource use."""
        self.command = command
        self.wait_status = wait_status
        self.exit = os.waitstatus_to_exitcode(wait_status)
        self.stdout = stdout
        self.stderr = stderr
        self.wall_s = wall_s
        self.user_s = user_s
        self.system_s = system_s
        self.peak_kb = peak_kb

    @property
    def out(self) -> str:
        """The captured standard output as text (THE STREAMS)."""
        return decoded(self.stdout)

    @property
    def err(self) -> str:
        """The captured standard error as text (THE STREAMS)."""
        return decoded(self.stderr)

    @property
    def cpu_s(self) -> float:
        """The user and system CPU time of the command and of each process that it waited for."""
        return self.user_s + self.system_s

    def __repr__(self) -> str:
        """Return the result for a message."""
        return f"Completed({self.command.shown()!r}, exit={self.exit}, wall_s={self.wall_s:.3f})"


class ToolFailed(Exception):
    """A command did not give its result: it cannot start, it ran past its limit, or its exit is not 0."""

    def __init__(self, message: str, command: Command, completed: Completed | None = None) -> None:
        """Hold the message, the command and the result of the command, or None when the command did not end."""
        super().__init__(message)
        self.command = command
        self.completed = completed


class CannotStart(ToolFailed):
    """The program of a command cannot start: it does not exist, it cannot run, or its directory does not exist."""

    def __init__(self, message: str, command: Command, error: OSError) -> None:
        """Hold the message, the command and the error of the operating system."""
        super().__init__(message, command)
        self.error = error


class TimedOut(ToolFailed):
    """A command ran past its time limit.  The module stopped its process group, and completed holds its output."""


def decoded(data: bytes, errors: str = "replace") -> str:
    """Return bytes of an output as text: UTF-8, and \\n for \\r\\n and \\r, as the text mode of subprocess gives it.

    Args:
        data: The bytes
        errors: "replace" gives U+FFFD for a byte that is not UTF-8.  "surrogateescape" keeps each byte, so a path
            in the output still names its file

    Complexity: linear in the size of the data.
    """
    text = data.decode("utf-8", errors)
    return text.replace("\r\n", "\n").replace("\r", "\n") if "\r" in text else text


def environment(overrides: dict[str, str] | None = None) -> dict[str, str]:
    """Return the environment of this process with the overrides, and with no variable of REPOSITORY_VARIABLES.

    Complexity: linear in the size of the environment.
    """
    found = {name: value for name, value in os.environ.items() if name not in REPOSITORY_VARIABLES}
    for name, value in (overrides or {}).items():
        if name not in REPOSITORY_VARIABLES:
            found[name] = value
    return found


def require(completed: Completed) -> Completed:
    """Return the completed run when its exit is 0.

    Raises:
        ToolFailed: If the exit is not 0.  The message gives the command, the exit and the end of the captured stderr
    """
    if completed.exit == 0:
        return completed
    lines = completed.err.strip().splitlines()
    detail = f": {' '.join(line.strip() for line in lines[-3:])}" if lines else ""
    ending = f"ends on the signal {-completed.exit}" if completed.exit < 0 else f"exits with {completed.exit}"
    raise ToolFailed(f"the command `{completed.command.shown()}` {ending}{detail}", completed.command, completed)


def run(command: Command) -> Completed:
    """Start one command, wait for its end, and return its result (THE TOOL RUN).

    Raises:
        ValueError: If the stdout of the command is Output.STREAM, which only start() reads
        CannotStart: If the program cannot start
        TimedOut: If the command runs past its limit
    """
    if command.stdout == Output.STREAM:
        raise ValueError(f"the command {command.argv[0]} gives its lines to a caller: start it with start()")
    return start(command).wait()


def start(command: Command) -> "Running":
    """Start one command and return the Running (A LONG RUN).

    Raises:
        CannotStart: If the program cannot start, an output file cannot open, or the kernel gives no pidfd
    """
    import subprocess

    opened: list[int] = []
    try:
        targets = {Output.CAPTURE: subprocess.PIPE, Output.STREAM: subprocess.PIPE, Output.INHERIT: None,
                   Output.DISCARD: subprocess.DEVNULL, Output.MERGE: subprocess.STDOUT}
        stdout = file_target(command.stdout, opened) if isinstance(command.stdout, os.PathLike) else targets[
            command.stdout]
        stderr = file_target(command.stderr, opened) if isinstance(command.stderr, os.PathLike) else targets[
            command.stderr]
        popen = subprocess.Popen(command.argv, cwd=command.cwd, env=child_environment(command.env),
                                 stdin=subprocess.PIPE if command.stdin else subprocess.DEVNULL, stdout=stdout,
                                 stderr=stderr, process_group=0)
    except OSError as error:
        where = f" in {command.cwd}" if command.cwd else ""
        raise CannotStart(f"the command `{command.shown()}` cannot start{where}: {error}", command, error) from error
    finally:
        for descriptor in opened:
            os.close(descriptor)
    return Running(command, popen)


class Running:
    """A started command: its pid, its pipes, its time limit and the lines that it gave (A LONG RUN)."""

    __slots__ = ("command", "pid", "_popen", "_started", "_deadline", "_pidfd", "_selector", "_has_exited",
                 "_reads", "_chunks", "_stdout_fd", "_stderr_fd", "_input", "_input_offset", "_lines", "_next_line",
                 "_tail", "_phase", "_phase_at", "_completed", "_timed_out", "_was_interrupted")

    def __init__(self, command: Command, popen: "subprocess.Popen[bytes]") -> None:
        """Watch a command that subprocess started: its pidfd, its pipes and its limit.

        Raises:
            CannotStart: If the kernel gives no pidfd for the command (Linux 5.3)
        """
        import selectors

        self.command = command
        self.pid = popen.pid
        self._popen = popen
        self._started = time.monotonic()
        self._deadline = self._started + command.timeout_s
        self._has_exited = False
        self._phase = RUNNING
        self._phase_at = 0.0
        self._completed: Completed | None = None
        self._timed_out = False
        self._was_interrupted = False
        self._reads: set[int] = set()
        self._chunks: dict[int, list[bytes]] = {}
        self._stdout_fd = popen.stdout.fileno() if popen.stdout is not None else -1
        self._stderr_fd = popen.stderr.fileno() if popen.stderr is not None else -1
        self._lines: list[str] = []
        self._next_line = 0
        self._tail = b""
        self._input = command.stdin
        self._input_offset = 0
        self._selector = selectors.DefaultSelector()
        try:
            self._pidfd = os.pidfd_open(self.pid)
        except OSError as error:
            self._pidfd = -1
            self._kill_now()
            raise CannotStart(f"the kernel gives no pidfd for the command `{command.shown()}` (Linux 5.3 gives "
                              f"one): {error}", command, error) from error
        # The pipes join the selector only after the pidfd, so the failure above closes no pipe that it registered.
        self._selector.register(self._pidfd, selectors.EVENT_READ)
        for stream in (popen.stdout, popen.stderr):
            if stream is not None:
                descriptor = stream.fileno()
                os.set_blocking(descriptor, False)
                self._reads.add(descriptor)
                self._chunks[descriptor] = []
                self._selector.register(descriptor, selectors.EVENT_READ)
        if popen.stdin is not None:
            os.set_blocking(popen.stdin.fileno(), False)
            self._selector.register(popen.stdin.fileno(), selectors.EVENT_WRITE)

    def __iter__(self) -> "Running":
        """Return the Running itself: a loop over it gives the lines of the standard output.

        Raises:
            ValueError: If the stdout of the command is not Output.STREAM
        """
        if self.command.stdout != Output.STREAM:
            raise ValueError(f"the command {self.command.argv[0]} gives no lines: its stdout is not Output.STREAM")
        return self

    def __next__(self) -> str:
        """Return the next line of the standard output, with its \\n, or stop at the end of the output.

        Raises:
            TimedOut: If the command runs past its limit

        Complexity: constant for each line.  The loop clears the list of lines only when it gave each line.
        """
        while self._next_line >= len(self._lines) and self._stdout_fd in self._reads:
            self._lines.clear()
            self._next_line = 0
            self._advance()
        if self._next_line < len(self._lines):
            self._next_line += 1
            return self._lines[self._next_line - 1]
        if self._timed_out or self._was_interrupted:
            self.wait()
        raise StopIteration

    def wait(self) -> Completed:
        """Wait for the end of the command and of its outputs, and return its result.

        Raises:
            KeyboardInterrupt: If an interrupt came in the wait (AN INTERRUPT).  The command ended first
            TimedOut: If the command runs past its limit
        """
        while self._completed is None:
            self._advance()
        if self._was_interrupted:
            raise KeyboardInterrupt
        if self._timed_out:
            first = "SIGINT" if self.command.stop == Stop.INTERRUPT else "SIGTERM"
            raise TimedOut(f"the command `{self.command.shown()}` ran past its limit of {self.command.timeout_s:g} s, "
                           f"so its process group got {first} and then SIGKILL", self.command, self._completed)
        return self._completed

    def stop(self) -> Completed:
        """Stop the process group of the command now, as the limit does, and return the result of the command.

        The group gets the stop signal of the command, and SIGKILL when it does not end after grace_s.
        """
        if self._completed is None and self._phase == RUNNING:
            self._signal_group(TERMINATED)
        while self._completed is None:
            self._advance()
        return self._completed

    def _advance(self) -> None:
        """Do one step of the loop.

        The first KeyboardInterrupt sends SIGINT to the group, and the loop
        goes on (AN INTERRUPT).  A second one, an interrupt after the limit,
        or another exception sends SIGKILL to the group, and then it goes on.
        """
        try:
            self._step()
        except KeyboardInterrupt:
            if self._completed is not None:
                raise
            if self._was_interrupted or self._phase != RUNNING:
                self._kill_now()
                raise
            self._was_interrupted = True
            self._signal_interrupt()
        except BaseException:
            if self._completed is None:
                self._kill_now()
            raise

    def _step(self) -> None:
        """Send the signal that the time asks for, then do one select and handle each ready pipe and the end."""
        now = time.monotonic()
        if self._phase == RUNNING and now >= self._deadline:
            self._timed_out = True
            self._signal_group(TERMINATED)
        elif self._phase == TERMINATED and now >= self._phase_at + self.command.grace_s:
            self._signal_group(KILLED)
        elif self._phase == KILLED and self._reads and now >= self._phase_at + DRAIN_S:
            self._close_reads()
        if self._has_exited and not self._reads:
            self._finish()
            return
        if self._phase == RUNNING:
            wait_s: float | None = self._deadline - now
        elif self._phase == TERMINATED:
            wait_s = self._phase_at + self.command.grace_s - now
        else:
            wait_s = self._phase_at + DRAIN_S - now if self._reads else None
        for key, _events in self._selector.select(None if wait_s is None else max(0.0, wait_s)):
            if key.fd == self._pidfd:
                self._has_exited = True
                self._selector.unregister(key.fd)
            elif key.fd in self._reads:
                self._read(key.fd)
            else:
                self._write()

    def _read(self, descriptor: int) -> None:
        """Read one chunk of an output pipe, and close the pipe at its end."""
        try:
            chunk = os.read(descriptor, READ_CHUNK)
        except BlockingIOError:
            return
        if not chunk:
            self._close_read(descriptor)
        elif descriptor == self._stdout_fd and self.command.stdout == Output.STREAM:
            lines = (self._tail + chunk).split(b"\n")
            self._tail = lines.pop()
            self._lines.extend(decoded(line) + "\n" for line in lines)
        else:
            self._chunks[descriptor].append(chunk)

    def _write(self) -> None:
        """Write one chunk of the standard input, and close the pipe after the last byte or when the command closed it."""
        stdin = self._popen.stdin
        if stdin is None:
            return
        try:
            written = os.write(stdin.fileno(), self._input[self._input_offset:self._input_offset + STDIN_CHUNK])
        except BlockingIOError:
            return
        except BrokenPipeError:
            written = len(self._input)
        self._input_offset += written
        if self._input_offset >= len(self._input):
            self._close_input()

    def _unregister(self, descriptor: int) -> None:
        """Remove a descriptor from the selector, when the selector holds it."""
        if descriptor in self._selector.get_map():
            self._selector.unregister(descriptor)

    def _close_input(self) -> None:
        """Close the pipe of the standard input."""
        stdin = self._popen.stdin
        if stdin is not None and not stdin.closed:
            self._unregister(stdin.fileno())
            try:
                stdin.close()
            except BrokenPipeError:
                pass

    def _close_read(self, descriptor: int) -> None:
        """Close one output pipe, and give the last line of a stream that has no \\n."""
        self._unregister(descriptor)
        self._reads.discard(descriptor)
        if descriptor == self._stdout_fd and self._tail:
            self._lines.append(decoded(self._tail))
            self._tail = b""
        for stream in (self._popen.stdout, self._popen.stderr):
            if stream is not None and not stream.closed and stream.fileno() == descriptor:
                stream.close()

    def _close_reads(self) -> None:
        """Close each output pipe that is open: a process that left the group can keep one open after SIGKILL."""
        for descriptor in sorted(self._reads):
            self._close_read(descriptor)

    def _signal_group(self, phase: int) -> None:
        """Send the stop signal (phase TERMINATED) or SIGKILL (phase KILLED) to the process group of the command."""
        import signal

        self._phase = phase
        self._phase_at = time.monotonic()
        stop_signal = signal.SIGINT if self.command.stop == Stop.INTERRUPT else signal.SIGTERM
        try:
            os.killpg(self.pid, stop_signal if phase == TERMINATED else signal.SIGKILL)
        except ProcessLookupError:
            pass

    def _signal_interrupt(self) -> None:
        """Send SIGINT to the process group of the command, as a terminal does to its foreground group."""
        import signal

        try:
            os.killpg(self.pid, signal.SIGINT)
        except ProcessLookupError:
            pass

    def _kill_now(self) -> None:
        """Send SIGKILL to the group, close each pipe, and reap the command."""
        self._signal_group(KILLED)
        self._close_reads()
        self._close_input()
        self._finish()

    def _finish(self) -> None:
        """Close the input, reap the command, close the pipes, the selector and the pidfd, and make the result."""
        self._close_input()
        _pid, wait_status, usage = os.wait4(self.pid, 0)
        wall_s = time.monotonic() - self._started
        self._popen.returncode = os.waitstatus_to_exitcode(wait_status)
        for stream in (self._popen.stdout, self._popen.stderr):
            if stream is not None and not stream.closed:
                stream.close()
        self._selector.close()
        if self._pidfd >= 0:
            os.close(self._pidfd)
        stdout = b"".join(self._chunks.get(self._stdout_fd, ()))
        if self.command.stdout == Output.STREAM:
            stdout = "".join(self._lines[self._next_line:]).encode() + self._tail
        stderr = b"".join(self._chunks.get(self._stderr_fd, ()))
        self._completed = Completed(self.command, wait_status, stdout, stderr, wall_s, usage.ru_utime, usage.ru_stime,
                                    usage.ru_maxrss)


def check_destination(program: str, name: str, value: object, words: frozenset[str]) -> None:
    """Refuse a destination of an output that is not a permitted word and not a path.

    Raises:
        ValueError: If the destination is not permitted
    """
    if (isinstance(value, str) and value in words) or isinstance(value, os.PathLike):
        return
    raise ValueError(f"the {name} of the command {program} is {value!r}: give one of {sorted(words)} or a path")


def file_target(path: os.PathLike[str], opened: list[int]) -> int:
    """Open a new file at a path for one output, and return its descriptor.  start() closes it after the start."""
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC, 0o644)
    opened.append(descriptor)
    return descriptor


def child_environment(env: dict[str, str] | None) -> dict[str, str]:
    """Return the environment of a child: the mapping or environment(), with no variable of REPOSITORY_VARIABLES."""
    if env is None:
        return environment()
    return {name: value for name, value in env.items() if name not in REPOSITORY_VARIABLES}
