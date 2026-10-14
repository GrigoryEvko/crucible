"""tracer — the trace of the system calls of one test, for its read set (assay.loop.readset).

THE TRACE
    The launcher starts the test in a child process (Tracer.start()).  Before
    the exec, the child installs a seccomp filter that returns
    SECCOMP_RET_USER_NOTIF for the calls of THE CALLS.  The kernel then stops
    the caller of such a call, and it sends a notification to the launcher.
    The launcher reads the path from the memory of the caller with
    process_vm_readv, records it, and answers with
    SECCOMP_USER_NOTIF_FLAG_CONTINUE.  Then the kernel does the call.  Each
    thread and each child of the test gets the filter, across fork, vfork,
    clone and execve.  Each other call goes through the filter with no stop.
      * The base issues raw system calls (foundation::os::raw_syscall), and a
        sanitizer runtime does too.  An interposer of the C library through
        LD_PRELOAD does not see such a call.  The filter of the kernel sees
        each call.
      * LeakSanitizer refuses to operate when a tracer of ptrace holds the
        process.  is_debugger_present() of the base also gives a different
        result to a death test then.  On 2026-10-13, ptrace broke 348 of 355
        executable tests, and the notification broke none of them.
      * The child sets PR_SET_NO_NEW_PRIVS, because the kernel admits the
        filter of an unprivileged process only with that bit.  A test then
        gets no privileges from a set-user-ID program.
      * The kernel admits one filter with a listener in the chain of a task.
        A test that installs its own filter with a listener then gets EBUSY.
        No test of the tree installs a filter.
      * A kernel that refuses the filter gives no listener.  The test then runs
        with no trace, and the trace is not complete.  An architecture with no
        row of ARCHITECTURES gives no tracer.

THE CALLS
    The filter stops these calls:
      * open, openat and openat2 with read access: an access mode that is not
        O_WRONLY, and no O_PATH.  The filter lets a write-only open of open and
        openat go through with no stop.  The flags of openat2 are in memory.
        The launcher reads them, and it ignores a write-only open.
      * execve and execveat: the program file of a process.
      * getdents and getdents64: the listing of a directory.  The launcher
        reads the directory from /proc/PID/fd of the caller.
    The filter does not stop a stat, an access or a readlink.  A test that
    only probes for a file with such a call does not give the file to its
    read set.  An open through io_uring and a read through an inherited
    descriptor are not in the read set.  The kernel opens the interpreter of
    a program itself, and the interpreter is not in the read set.  The C
    library opens the other libraries of the package of the interpreter.
    A new version of the C library then changes a key part.
    A relative path takes the directory of its descriptor, or the working
    directory of the caller, from /proc/PID at the stop.  After the read, the
    launcher makes sure that the task waits for its answer at this time.  A
    task that ended between the stop and the read then adds no path.

THE END
    When the program of the test ends, the launcher answers the other
    processes of the test for at most GRACE_S.  It stops when the kernel tells
    that no process of the filter stays.  A process that stays longer makes
    the trace not complete, and the launcher then writes no read set.  When
    the launcher closes the listener, such a process gets ENOSYS for each call
    of THE CALLS.

THE COST
    On 2026-10-13, a supervisor in C answered one stop in 2.88 us of CPU time
    and 3.64 us of wall time.  Each value is the median of 5 runs of 200,000
    stops.  A call that the filter lets go through cost 0.015 us.  The
    supervisor of this module answers in Python, and it stops only the calls
    of THE CALLS.

`guard selftest tests.loop.test_tracer` does a test of the module.
"""

import array
import ctypes
import errno
import fcntl
import os
import select
import signal
import socket
import struct
import time

from assay.core.exit import ExitCode
from assay.core.value import Value
from assay.loop import readset

TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

# The longest wait for the processes of a test after the end of its program, in seconds (THE END).
GRACE_S = 5.0
# The wait of one poll in that time, in milliseconds.
POLL_MS = 100
# The longest path that the launcher reads from the memory of a test, and the length of a page.
PATH_LIMIT = 4096
PAGE = 4096
# The length of struct seccomp_notif of the kernel, and the offset of struct seccomp_data in it.
NOTIF_SIZE = 80
NOTIF_DATA = 16
# The ioctl of each operation of the listener: receive a stop, send an answer, and find if a stop waits at this time.
NOTIF_RECV = 0xC0502100
NOTIF_SEND = 0xC0182101
NOTIF_ID_VALID = 0x40082102
# The flag of an answer that lets the kernel do the call.
FLAG_CONTINUE = 1
# The arguments of the call seccomp that install a filter with a listener, and the option of prctl.
SET_MODE_FILTER = 1
FILTER_FLAG_NEW_LISTENER = 1 << 3
PR_SET_NO_NEW_PRIVS = 38
# The offsets of struct seccomp_data: the call number, the architecture and the first argument.
DATA_NUMBER = 0
DATA_ARCHITECTURE = 4
DATA_ARGUMENTS = 16
# The classic BPF codes of the filter, and its two results.
BPF_LOAD_WORD = 0x20
BPF_JUMP_EQUAL = 0x15
BPF_JUMP_SET = 0x45
BPF_AND = 0x54
BPF_RETURN = 0x06
RETURN_ALLOW = 0x7FFF0000
RETURN_NOTIFY = 0x7FC00000
# The bits of the flags of an open, and the arguments of a call relative to a descriptor.  The values are the same on
# x86_64 and on aarch64.
ACCESS_MODE = 3
WRITE_ONLY = 1
PATH_ONLY = 0o10000000
AT_FDCWD = -100
AT_EMPTY_PATH = 0x1000


class Shape(Value):
    """The arguments of one call of THE CALLS.

    `directory` is the index of the argument with the directory descriptor, or -1 for the working directory.  `path`
    is the index of the argument with the path, or -1 for a call on a descriptor.  `flags` is the index of the
    argument with the flags, or -1.  `in_memory` is True when that argument is the address of the flags.
    """

    __match_args__ = ("kind", "directory", "path", "flags", "in_memory")
    kind: str
    directory: int
    path: int
    flags: int
    in_memory: bool

    def __init__(self, kind: str, directory: int, path: int, flags: int, in_memory: bool = False) -> None:
        """Hold the kind and the argument indexes of one call."""
        super().__init__(kind, directory, path, flags, in_memory)

    @property
    def has_access_filter(self) -> bool:
        """Return True when the filter itself lets a write-only open of this call go through with no stop."""
        return self.kind == readset.READ and self.flags >= 0 and not self.in_memory


OPEN = Shape(readset.READ, -1, 0, 1)
OPENAT = Shape(readset.READ, 0, 1, 2)
OPENAT2 = Shape(readset.READ, 0, 1, 2, in_memory=True)
EXECVE = Shape(readset.EXEC, -1, 0, -1)
EXECVEAT = Shape(readset.EXEC, 0, 1, 4)
GETDENTS = Shape(readset.LIST, 0, -1, -1)


class Architecture(Value):
    """The audit number of an architecture, its number of the call seccomp, and the shape of each traced call."""

    __match_args__ = ("audit", "seccomp", "calls")
    audit: int
    seccomp: int
    calls: Mapping[int, Shape]

    def __init__(self, audit: int, seccomp: int, calls: Mapping[int, Shape]) -> None:
        """Hold the numbers of one architecture."""
        super().__init__(audit, seccomp, calls)


# The architectures of the tree (CLAUDE.md section XIV).  aarch64 has no open and no getdents.
ARCHITECTURES = {
    "x86_64": Architecture(
        0xC000003E,
        317,
        {2: OPEN, 257: OPENAT, 437: OPENAT2, 59: EXECVE, 322: EXECVEAT, 78: GETDENTS, 217: GETDENTS},
    ),
    "aarch64": Architecture(0xC00000B7, 277, {56: OPENAT, 437: OPENAT2, 221: EXECVE, 281: EXECVEAT, 61: GETDENTS}),
}


def filter_program(architecture: Architecture) -> bytes:
    """Return the classic BPF program of the filter of THE CALLS, as the bytes of its struct sock_filter array.

    The program lets each call of a different architecture go through.  For open and openat, it lets a write-only
    open and an open with O_PATH go through.  It stops each other call of THE CALLS with RETURN_NOTIFY, and lets each
    other call go through.  Complexity: linear in the number of traced calls.
    """
    filtered = sorted(number for number, shape in architecture.calls.items() if shape.has_access_filter)
    plain = sorted(number for number, shape in architecture.calls.items() if not shape.has_access_filter)
    # Each instruction is (code, jump when the test holds, jump when it does not hold, constant).  A jump is a label
    # until the end.
    code: list[tuple[int, str | int, str | int, int]] = [
        (BPF_LOAD_WORD, 0, 0, DATA_ARCHITECTURE),
        (BPF_JUMP_EQUAL, 0, "allow", architecture.audit),
        (BPF_LOAD_WORD, 0, 0, DATA_NUMBER),
    ]
    code += [(BPF_JUMP_EQUAL, f"flags {number}", 0, number) for number in filtered]
    code += [(BPF_JUMP_EQUAL, "notify", 0, number) for number in plain]
    code.append((BPF_RETURN, 0, 0, RETURN_ALLOW))
    labels: dict[str, int] = {}
    for number in filtered:
        labels[f"flags {number}"] = len(code)
        code += [
            (BPF_LOAD_WORD, 0, 0, DATA_ARGUMENTS + 8 * architecture.calls[number].flags),
            (BPF_JUMP_SET, "allow", 0, PATH_ONLY),
            (BPF_AND, 0, 0, ACCESS_MODE),
            (BPF_JUMP_EQUAL, "allow", "notify", WRITE_ONLY),
        ]
    labels["notify"] = len(code)
    code.append((BPF_RETURN, 0, 0, RETURN_NOTIFY))
    labels["allow"] = len(code)
    code.append((BPF_RETURN, 0, 0, RETURN_ALLOW))

    def offset(target: str | int, index: int) -> int:
        """Return a jump of the instruction at `index` as the count of instructions that it skips."""
        return target if isinstance(target, int) else labels[target] - index - 1

    return b"".join(
        struct.pack("=HBBI", operation, offset(true, index), offset(false, index), constant)
        for index, (operation, true, false, constant) in enumerate(code)
    )


def signed(word: int) -> int:
    """Return the low 32 bits of an argument word as a signed int, as the kernel reads a descriptor argument."""
    low = word & 0xFFFFFFFF
    return low - (1 << 32) if low >= 1 << 31 else low


class Tracer:
    """The supervisor of the trace of one run (THE TRACE).

    make_tracer() gives a tracer for the architecture of the host.  The measure of assay.core.meter calls start(),
    serve() and finish(), in that sequence.  `events` gives the signature at its first stop of each kind and path, and
    `is_complete` tells if the trace saw each call of THE CALLS.  `stops` counts the stops that the tracer answered.
    """

    def __init__(self, architecture: Architecture, roots: Sequence[str]) -> None:
        """Prepare the filter and the functions of the C library, before the fork of the child."""
        self.architecture = architecture
        self.roots = tuple(roots)
        self.events: dict[tuple[str, str], str] = {}
        self.is_complete = False
        self.stops = 0
        library = ctypes.CDLL(None, use_errno=True)
        self._prctl = library.prctl
        self._prctl.restype = ctypes.c_int
        self._syscall = library.syscall
        self._syscall.restype = ctypes.c_long
        self._read_memory = library.process_vm_readv
        self._read_memory.restype = ctypes.c_ssize_t
        program = filter_program(architecture)
        self._program = ctypes.create_string_buffer(program, len(program))
        self._description = ctypes.create_string_buffer(
            struct.pack("@HP", len(program) // 8, ctypes.addressof(self._program))
        )
        self._buffer = ctypes.create_string_buffer(PAGE)
        # Each iovec is two words: the address and the length.
        self._local = (ctypes.c_size_t * 2)(ctypes.addressof(self._buffer), 0)
        self._remote = (ctypes.c_size_t * 2)(0, 0)
        self._listener = -1
        self._pidfd = -1
        self._error = -1
        self._poller = select.poll()

    def install(self) -> int:
        """In the child: set PR_SET_NO_NEW_PRIVS, install the filter, and return the listener, or -1."""
        is_set = self._prctl(
            ctypes.c_int(PR_SET_NO_NEW_PRIVS),
            ctypes.c_ulong(1),
            ctypes.c_ulong(0),
            ctypes.c_ulong(0),
            ctypes.c_ulong(0),
        )
        if is_set != 0:
            return -1
        listener = self._syscall(
            ctypes.c_long(self.architecture.seccomp),
            ctypes.c_long(SET_MODE_FILTER),
            ctypes.c_long(FILTER_FLAG_NEW_LISTENER),
            ctypes.c_void_p(ctypes.addressof(self._description)),
        )
        return int(listener)

    def start(self, command: list[str], environment: Mapping[str, str]) -> int:
        """Start the command in a child with the filter, and return its process ID (THE TRACE).

        The child sets the default action of SIGINT, as assay.core.meter gives it to a command.  It sends the
        listener to this process over a socket pair, or a byte with no descriptor when the kernel refuses the filter.
        Then it runs the command.  When the exec fails, the child writes its errno to a pipe, and finish() raises it.
        After the child installs the filter, it must not open a file before this process holds the listener.  No
        process answers such a stop.  The child then imports no module, and it calls only sendmsg, close, the exec
        and write.  socket.send_fds() imports the module array, and the child calls sendmsg itself for that reason.

        Raises:
            OSError: If the socket pair, the pipe, the fork or the process descriptor cannot be made
        """
        mine, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        error_read, error_write = os.pipe2(os.O_CLOEXEC)
        descriptor = array.array("i", [0])
        child = os.fork()
        if child == 0:
            try:
                mine.close()
                os.close(error_read)
                signal.signal(signal.SIGINT, signal.SIG_DFL)
                listener = self.child_listener()
                descriptor[0] = listener
                theirs.sendmsg(
                    [b"y" if listener >= 0 else b"n"],
                    [(socket.SOL_SOCKET, socket.SCM_RIGHTS, descriptor)] if listener >= 0 else [],
                )
                if listener >= 0:
                    os.close(listener)
                theirs.close()
                os.execvpe(command[0], command, environment)
            except OSError as problem:
                os.write(error_write, (problem.errno or errno.ENOEXEC).to_bytes(4, "little"))
            finally:
                os._exit(ExitCode.CANNOT_START)
        theirs.close()
        os.close(error_write)
        self._error = error_read
        try:
            _, descriptors, _, _ = socket.recv_fds(mine, 1, 1)
        finally:
            mine.close()
        self._listener = descriptors[0] if descriptors else -1
        self.is_complete = self._listener >= 0
        self._pidfd = os.pidfd_open(child)
        return child

    def child_listener(self) -> int:
        """In the child: return the listener of the filter, or -1 when the kernel or the C library refuses it."""
        try:
            return self.install()
        except Exception:  # noqa: BLE001
            return -1

    def serve(self, child: int) -> None:
        """Answer each stop of the test until its program ends (THE TRACE).

        The caller then waits for the child.  Complexity: one answer for each stop.
        """
        self._poller.register(self._pidfd, select.POLLIN)
        if self._listener >= 0:
            self._poller.register(self._listener, select.POLLIN)
        while True:
            ready = dict(self._poller.poll())
            if self._listener >= 0 and ready.get(self._listener, 0) & select.POLLIN:
                self.answer()
            if self._pidfd in ready:
                break
        self._poller.unregister(self._pidfd)
        os.close(self._pidfd)
        self._pidfd = -1

    def finish(self) -> None:
        """Answer the processes of the test that outlive its program, close the listener, and raise an exec error.

        THE END gives the rules.

        Raises:
            OSError: If the program of the test did not start
        """
        if self._listener >= 0:
            deadline = time.monotonic() + GRACE_S
            is_alone = False
            while not is_alone and time.monotonic() < deadline:
                mask = dict(self._poller.poll(POLL_MS)).get(self._listener, 0)
                if mask & select.POLLIN:
                    self.answer()
                is_alone = bool(mask & (select.POLLHUP | select.POLLERR)) and not mask & select.POLLIN
            self.is_complete = self.is_complete and is_alone
            os.close(self._listener)
            self._listener = -1
        found = b""
        if self._error >= 0:
            found = os.read(self._error, 4)
            os.close(self._error)
            self._error = -1
        if len(found) == 4:
            number = int.from_bytes(found, "little")
            raise OSError(number, os.strerror(number))

    def answer(self) -> None:
        """Receive one stop, record its path, and let the kernel do the call.

        A stop of a task that ended gives no path.  A failure to read a path makes the trace not complete, and the
        answer goes out also then.
        """
        request = bytearray(NOTIF_SIZE)
        try:
            fcntl.ioctl(self._listener, NOTIF_RECV, request, True)
        except OSError as problem:
            if problem.errno in (errno.ENOENT, errno.EINTR):
                return
            raise
        self.stops += 1
        ident, pid, _, number = struct.unpack_from("=QIIi", request)
        arguments = struct.unpack_from("=6Q", request, NOTIF_DATA + DATA_ARGUMENTS)
        try:
            found = self.call_path(pid, number, arguments)
            if found is not None and self.is_valid(ident):
                self.keep(*found)
        except Exception:  # noqa: BLE001
            self.is_complete = False
        finally:
            response = bytearray(struct.pack("=QqiI", ident, 0, 0, FLAG_CONTINUE))
            try:
                fcntl.ioctl(self._listener, NOTIF_SEND, response, True)
            except OSError:
                pass

    def is_valid(self, ident: int) -> bool:
        """Return True when the task of a stop waits for its answer at this time: the path then came from that task."""
        try:
            fcntl.ioctl(self._listener, NOTIF_ID_VALID, struct.pack("=Q", ident))
        except OSError:
            return False
        return True

    def keep(self, kind: str, path: str) -> None:
        """Record the first stop of a kind and a path, with the signature of the path before the call."""
        key = (kind, path)
        if key not in self.events:
            is_runtime = readset.is_runtime(path, self.roots)
            self.events[key] = readset.NOT_MEASURED if is_runtime else readset.signature(path)

    def call_path(self, pid: int, number: int, arguments: Sequence[int]) -> tuple[str, str] | None:
        """Return the kind and the absolute path of one stopped call, or None for a call that reads nothing.

        Complexity: one or two reads of the memory of the task, and one read of a link of /proc for a relative path.
        """
        shape = self.architecture.calls.get(number)
        if shape is None:
            return None
        if shape.path < 0:
            return self.descriptor_path(pid, signed(arguments[shape.directory]), shape.kind)
        flags = arguments[shape.flags] if shape.flags >= 0 else 0
        if shape.in_memory:
            flags = int.from_bytes(self.memory(pid, arguments[shape.flags], 8) or bytes(8), "little")
        if shape.kind == readset.READ and (flags & PATH_ONLY or flags & ACCESS_MODE == WRITE_ONLY):
            return None
        raw = self.text(pid, arguments[shape.path])
        if raw is None:
            return None
        directory = signed(arguments[shape.directory]) if shape.directory >= 0 else AT_FDCWD
        if not raw and shape.kind == readset.EXEC and flags & AT_EMPTY_PATH:
            return self.descriptor_path(pid, directory, shape.kind)
        path = os.fsdecode(raw)
        if path.startswith("/"):
            return shape.kind, path
        base = self.link(f"/proc/{pid}/cwd" if directory == AT_FDCWD else f"/proc/{pid}/fd/{directory}")
        return None if base is None else (shape.kind, os.path.join(base, path))

    def descriptor_path(self, pid: int, descriptor: int, kind: str) -> tuple[str, str] | None:
        """Return the kind and the path of a descriptor of a task, or None when the descriptor names no path."""
        found = self.link(f"/proc/{pid}/fd/{descriptor}")
        return None if found is None else (kind, found)

    def link(self, path: str) -> str | None:
        """Return the target of a link of /proc when it is an absolute path, else None."""
        try:
            found = os.readlink(path)
        except OSError:
            return None
        return found if found.startswith("/") else None

    def memory(self, pid: int, address: int, size: int) -> bytes | None:
        """Return at most `size` bytes of the memory of a task at an address, or None when the read fails."""
        if address == 0 or size <= 0:
            return None
        self._local[1] = size
        self._remote[0] = address
        self._remote[1] = size
        got = self._read_memory(
            ctypes.c_int(pid),
            ctypes.c_void_p(ctypes.addressof(self._local)),
            ctypes.c_ulong(1),
            ctypes.c_void_p(ctypes.addressof(self._remote)),
            ctypes.c_ulong(1),
            ctypes.c_ulong(0),
        )
        return ctypes.string_at(self._buffer, got) if got > 0 else None

    def text(self, pid: int, address: int) -> bytes | None:
        """Return the text that ends with a NUL at an address of a task, without the NUL, or None.

        The read takes one page at a time, because a read across a page that the task did not map fails.  Complexity:
        one read for each page of the text, at most PATH_LIMIT bytes.
        """
        parts: list[bytes] = []
        size = 0
        while size < PATH_LIMIT:
            chunk = self.memory(pid, address + size, PAGE - (address + size) % PAGE)
            if chunk is None:
                return None
            end = chunk.find(b"\0")
            if end >= 0:
                parts.append(chunk[:end])
                return b"".join(parts)
            parts.append(chunk)
            size += len(chunk)
        return None


def make_tracer(roots: Sequence[str]) -> Tracer | None:
    """Return a tracer for the architecture of the host, or None for an architecture with no row of ARCHITECTURES.

    Args:
        roots: The runtime roots of the test (assay.loop.readset, THE CLASSES OF A READ)
    """
    architecture = ARCHITECTURES.get(os.uname().machine)
    return None if architecture is None else Tracer(architecture, roots)
