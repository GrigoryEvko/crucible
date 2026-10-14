"""readset — the files that one test reads, from a trace of the system calls of the test.

`loop test-step` (assay.loop.teststep) traces a test with a program in the build
directory, and it writes the read set of the run in the directory of the record
of the test.  The read set also holds a file that the test opens by a path
that it makes at run time.  assay.loop.tracer holds the
trace: the seccomp filter, the calls that it stops and the supervisor that
answers them.  This module holds the read set: the classes of a read, the
signatures, the record and the decision to trace.

THE CLASSES OF A READ
    The read set holds each path with its kind (read, exec or list) and its
    role: a key part of the test, or a fact of the run.
      * A path in a runtime root is a fact.  The runtime roots are /proc,
        /sys, /dev, /run, /tmp, /var/tmp, the TMPDIR of the test and the root
        of the caches (assay.core.cache_root).  The kernel and the processes
        write the files of these roots at run time.  The files then change
        from run to run, also with no change of the tree.  The launcher does
        not stat such a path, because /proc/self of the launcher is not
        /proc/self of the test.
      * A path with a device, a socket or a pipe at the end of the run is a
        fact.  Such a file is not a regular file and not a directory.
      * A path with no file at its first stop and a file at the end of the run
        is a fact: the test made it.  A path with a file at its first stop and
        no file at the end is a fact too: the test removed a scratch file.
      * An open of a directory reads no bytes of it.  Only a listing makes a
        directory a key part.
      * Each other path is a key part.  It is a file of the tree, of the build
        directory, of the toolchain, or a stable file of the host.  A stable
        file is a file in no runtime root, such as a library of the system or
        a file of /etc.  Only an install changes such a file, and an install
        can change the result of a test.  A path with no file at its first stop
        and at the end of the run is a key part too.  A file there can change
        the result of the next run.

THE SIGNATURE
    signature() gives the state of a path in one word.  A regular file gives
    its length, its modification time and its inode.  A directory gives a
    digest of the sorted names in it.  Each other file gives its type.  A path
    with no file gives ABSENT.  The tracer takes the signature of each path at
    its first stop, before the kernel does the call.  The launcher takes it
    again at the end of the run.  When the two signatures of a key part are not
    equal, the file changed while the test ran.  The read set is then not
    settled.

THE RECORD
    The read set of the test NAME is BUILD_DIR/test-records/NAME.test.reads.
    The cost record of the test (assay.core.record) is in the same directory.
    The read set is text in UTF-8 with surrogate escapes.  Its lines are, in
    this sequence:
        crucible-read-set 1
        version HEX
        status INTEGER
        settled yes|no
        command<TAB>WORD<TAB>WORD...
        KIND<TAB>ROLE<TAB>FIRST<TAB>END<TAB>PATH
    The version is the version of the traced run (THE RETRACE).  The status is
    the exit status of the program, or minus its signal.  Each word and each
    path escapes a backslash, a tab and a line break with a backslash.  The
    entries are sorted by kind and path.  read() refuses a record of a
    different shape, and the launcher then traces the test again.

THE RETRACE
    plan() gives the plan of a traced run, or no plan.  The launcher traces a
    run in these conditions:
      * The test has no read set.
      * The read set came from a run with a status that is not 0, or it is not
        settled.
      * The version or the command of the read set is not the version or the
        command of the run.
      * A key part has a different signature at this time.
    The version holds the words of the command and the clean environment.  It
    also holds the length, the time and the inode of each file that the
    command names (assay.loop.teststep, THE VERSION).  A new build of the
    program then makes the next run a traced run.  A change of a file that the
    test read has the same effect.  On 2026-10-13, three traced runs of each
    of 355 executable tests gave the same read set.  A host with no trace (assay.loop.tracer, THE TRACE)
    gets no plan.  The launcher then removes the read set of an earlier run,
    because that read set can be stale.

`guard selftest tests.loop.test_readset` does a test of the module.
"""

import os
import stat

from assay.core import cache_root
from assay.core import record as cost_record
from assay.core.value import Value

# The names are only for the types, and a test that the launcher does not trace pays no import of them.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping, Sequence

    from assay.loop.tracer import Tracer

# The suffix and the first line of a read set (THE RECORD).
SUFFIX = ".test.reads"
FORMAT = "crucible-read-set 1"
# The kinds of an entry.
READ = "read"
EXEC = "exec"
LIST = "list"
KINDS = frozenset({READ, EXEC, LIST})
# The roles of an entry (THE CLASSES OF A READ).
KEY = "key"
FACT = "fact"
ROLES = frozenset({KEY, FACT})
# The signature of a path with no file, and of a path that the launcher cannot stat (THE SIGNATURE).
ABSENT = "-"
UNKNOWN = "?"
# The signature of a path in a runtime root.  The launcher does not stat such a path.
NOT_MEASURED = "."
# The starts of the signature of a regular file, of a directory and of each other file.
FILE_SIGNATURE = "f:"
DIRECTORY_SIGNATURE = "d:"
OTHER_SIGNATURE = "o:"
# The runtime roots of each host (THE CLASSES OF A READ).
RUNTIME_ROOTS = ("/proc", "/sys", "/dev", "/run", "/tmp", "/var/tmp")
TMPDIR = "TMPDIR"
# The escape of each character that a word or a path of a read set escapes (THE RECORD).
ESCAPES = {"\\": "\\", "t": "\t", "n": "\n"}


class Entry(Value):
    """One path of a read set: its kind, its role, its signatures at the first stop and at the end, and the path."""

    __match_args__ = ("kind", "role", "first", "end", "path")
    kind: str
    role: str
    first: str
    end: str
    path: str

    def __init__(self, kind: str, role: str, first: str, end: str, path: str) -> None:
        """Hold the fields of one entry."""
        super().__init__(kind, role, first, end, path)


class ReadSet(Value):
    """The read set of one traced run of a test (THE RECORD)."""

    __match_args__ = ("version", "status", "is_settled", "command", "entries")
    version: str
    status: int
    is_settled: bool
    command: tuple[str, ...]
    entries: tuple[Entry, ...]

    def __init__(
        self, version: str, status: int, is_settled: bool, command: Sequence[str], entries: Iterable[Entry]
    ) -> None:
        """Hold the version and the status of the run, the settled flag, the command and the entries."""
        super().__init__(version, status, is_settled, tuple(command), tuple(entries))

    def key_parts(self) -> tuple[Entry, ...]:
        """Return each key part of the read set."""
        return tuple(entry for entry in self.entries if entry.role == KEY)

    def is_current(self) -> bool:
        """Return True when each key part has the signature that it had at the end of the run.

        Complexity: one stat of each key part, and one listing of each directory.
        """
        return all(signature(entry.path) == entry.end for entry in self.key_parts())


def read_set_file(build_dir: str | os.PathLike[str], name: str) -> str:
    """Return the read set of one test of a build directory: BUILD_DIR/test-records/NAME.test.reads."""
    return os.path.join(build_dir, cost_record.TEST_DIRECTORY, name + SUFFIX)


def listing_digest(path: str) -> str:
    """Return a digest of the sorted names in a directory: the CRC-32 of the text and of the text in reverse.

    The two find a change, and the launcher then pays no import of hashlib.  Complexity: O(n log n) for n names.

    Raises:
        OSError: If the directory cannot be listed
    """
    import binascii

    text = "\0".join(sorted(os.listdir(path))).encode(errors="surrogateescape")
    return f"{binascii.crc32(text):08x}{binascii.crc32(text[::-1]):08x}"


def signature(path: str) -> str:
    """Return the state of a path in one word (THE SIGNATURE).

    Complexity: one stat, and one listing for a directory.
    """
    try:
        found = os.stat(path)
    except FileNotFoundError, NotADirectoryError:
        return ABSENT
    except OSError, ValueError:
        return UNKNOWN
    if stat.S_ISREG(found.st_mode):
        return f"{FILE_SIGNATURE}{found.st_size}:{found.st_mtime_ns}:{found.st_ino}"
    if stat.S_ISDIR(found.st_mode):
        try:
            return DIRECTORY_SIGNATURE + listing_digest(path)
        except OSError:
            return UNKNOWN
    return f"{OTHER_SIGNATURE}{stat.S_IFMT(found.st_mode):o}"


def is_runtime(path: str, roots: Sequence[str]) -> bool:
    """Return True when a path is a runtime root, or a path in one (THE CLASSES OF A READ)."""
    return path in roots or path.startswith(tuple(f"{root}/" for root in roots))


def runtime_roots(environment: Mapping[str, str]) -> tuple[str, ...]:
    """Return the runtime roots of a test: RUNTIME_ROOTS, its TMPDIR and the root of the caches.

    Args:
        environment: The environment of the test
    """
    roots = list(RUNTIME_ROOTS)
    scratch = environment.get(TMPDIR, "")
    if os.path.isabs(scratch):
        roots.append(os.path.realpath(scratch).rstrip("/") or "/")
    caches = cache_root.base_root()
    if caches is not None:
        roots.append(os.path.realpath(caches).rstrip("/") or "/")
    return tuple(roots)


def classify(events: Mapping[tuple[str, str], str], roots: Sequence[str]) -> tuple[tuple[Entry, ...], bool]:
    """Return the entries of the paths of a trace, and True when each key part is settled (THE CLASSES OF A READ).

    Args:
        events: The signature at its first stop of each kind and path of the trace
        roots: The runtime roots of the test

    Returns:
        The entries, sorted by kind and path, and True when no key part changed while the test ran.  An entry with a
        path in no runtime root holds the path that os.path.realpath() gives.  Complexity: one call of
        os.path.realpath() and one signature of each path, O(n log n) for n paths because of the sort
    """
    found: dict[tuple[str, str], Entry] = {}
    is_settled = True
    for (kind, path), first in sorted(events.items()):
        if is_runtime(path, roots):
            entry = Entry(kind, FACT, first, NOT_MEASURED, path)
        else:
            resolved = os.path.realpath(path)
            end = signature(resolved)
            is_directory = end.startswith(DIRECTORY_SIGNATURE)
            if kind == READ and is_directory:
                continue
            is_fact = (
                is_runtime(resolved, roots)
                or end.startswith(OTHER_SIGNATURE)
                or (kind == LIST and not is_directory)
                or (first == ABSENT) != (end == ABSENT)
            )
            entry = Entry(kind, FACT if is_fact else KEY, first, end, resolved)
            is_settled = is_settled and (is_fact or first == end)
        found.setdefault((entry.kind, entry.path), entry)
    return tuple(found[key] for key in sorted(found)), is_settled


def escaped(text: str) -> str:
    """Return a word or a path of a read set with each backslash, tab and line break escaped (THE RECORD)."""
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def unescaped(text: str) -> str | None:
    """Return the text of an escaped word or path, or None when an escape is not one of ESCAPES.

    Complexity: linear in the length of the text.
    """
    parts: list[str] = []
    index = 0
    while index < len(text):
        char = text[index]
        if char != "\\":
            parts.append(char)
            index += 1
            continue
        meaning = ESCAPES.get(text[index + 1 : index + 2])
        if meaning is None:
            return None
        parts.append(meaning)
        index += 2
    return "".join(parts)


def record_text(read_set: ReadSet) -> str:
    """Return the text of a read set (THE RECORD)."""
    lines = [
        FORMAT,
        f"version {read_set.version}",
        f"status {read_set.status}",
        f"settled {'yes' if read_set.is_settled else 'no'}",
        "\t".join(["command", *(escaped(word) for word in read_set.command)]),
    ]
    lines += [
        "\t".join((entry.kind, entry.role, entry.first, entry.end, escaped(entry.path))) for entry in read_set.entries
    ]
    return "\n".join(lines) + "\n"


def write(path: str, read_set: ReadSet) -> None:
    """Write a read set at a path, through a scratch file and a rename (assay.core.record, THE WRITE).

    Raises:
        OSError: If the read set cannot be written
    """
    cost_record.write_text(path, record_text(read_set))


def head_value(line: str, word: str) -> str | None:
    """Return the value of a head line `WORD VALUE` of a read set, or None for a line of a different word."""
    found, separator, value = line.partition(" ")
    return value if separator and found == word else None


def entry_of(line: str) -> Entry | None:
    """Return the entry of one line of a read set, or None for a line that is not an entry (THE RECORD)."""
    cells = line.split("\t")
    if len(cells) != 5 or cells[0] not in KINDS or cells[1] not in ROLES:
        return None
    path = unescaped(cells[4])
    return Entry(cells[0], cells[1], cells[2], cells[3], path) if path else None


def read(path: str) -> ReadSet | None:
    """Return the read set at a path, or None when the file is not there or does not have the shape of THE RECORD.

    Complexity: linear in the length of the file.
    """
    try:
        with open(path, encoding="utf-8", errors="surrogateescape") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return None
    if len(lines) < 5 or lines[0] != FORMAT:
        return None
    version = head_value(lines[1], "version")
    status = head_value(lines[2], "status")
    settled = head_value(lines[3], "settled")
    command_cells = lines[4].split("\t")
    words = [unescaped(cell) for cell in command_cells[1:]]
    entries = [entry_of(line) for line in lines[5:]]
    has_heads = version is not None and settled in ("yes", "no") and command_cells[0] == "command"
    if not has_heads or status is None or not status.removeprefix("-").isdigit():
        return None
    kept_words = [word for word in words if word is not None]
    kept_entries = [entry for entry in entries if entry is not None]
    if len(kept_words) != len(words) or len(kept_entries) != len(entries):
        return None
    return ReadSet(version or "", int(status), settled == "yes", kept_words, kept_entries)


def remove(path: str) -> None:
    """Remove a read set, when the file is there."""
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass


class Plan:
    """One traced run of a test: the tracer of the run, and the read set that the launcher writes after it."""

    __slots__ = ("command", "path", "tracer", "version")

    def __init__(self, path: str, tracer: Tracer, version: str, command: Sequence[str]) -> None:
        """Hold the path of the read set, the tracer, the version of the run and its command."""
        self.path = path
        self.tracer = tracer
        self.version = version
        self.command = tuple(command)

    def save(self, status: int) -> None:
        """Write the read set of the run, or remove the read set when the trace is not complete.

        assay.loop.tracer gives the conditions of a trace that is not complete (THE END).

        Raises:
            OSError: If the read set cannot be written
        """
        if not self.tracer.is_complete:
            remove(self.path)
            return
        entries, is_settled = classify(self.tracer.events, self.tracer.roots)
        write(self.path, ReadSet(self.version, status, is_settled, self.command, entries))


def plan(build_dir: str, name: str, command: Sequence[str], version: str) -> Plan | None:
    """Return the plan of a traced run of a test, or None when its read set is current (THE RETRACE).

    Args:
        build_dir: The build directory of the test
        name: The name of the test
        command: The command of the test, after the options of the launcher
        version: The version of the run: the command, the clean environment and the files of the command

    Complexity: one read of the read set, and one signature of each of its key parts.
    """
    path = read_set_file(build_dir, name)
    earlier = read(path)
    is_current = (
        earlier is not None
        and earlier.version == version
        and earlier.command == tuple(command)
        and earlier.status == 0
        and earlier.is_settled
        and earlier.is_current()
    )
    if is_current:
        return None
    from assay.loop import tracer as call_tracer

    tracer = call_tracer.make_tracer(runtime_roots(os.environ))
    if tracer is None:
        remove(path)
        return None
    return Plan(path, tracer, version, command)
