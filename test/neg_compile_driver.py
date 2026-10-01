#!/usr/bin/env python3
"""Run one negative-compile fixture, and keep the result of its compile in a store.

The driver reads the compile command of the fixture from compile_commands.json.
It runs that command, and it puts the object file and the dependency file in a
scratch directory of the fixture.  It does not call Ninja.  CTest runs many
fixtures at the same time, and each `cmake --build` call writes to the same
Ninja log and to the same build graph.

How the regexes apply
---------------------
GCC shows the source line of a diagnostic again, below the message.  A regex
can find its text in that line, which is the text of the fixture and not a
message of the compiler.  Then the test of a fixture can pass although the
compiler does not reject the fixture for the documented reason.

`strip_source_echo` removes the caret display before the driver applies the
regexes.  The text that stays is the text that the compiler wrote: the message
lines, the `note:` lines, the `In file included from` chain, the bullet lines of
GCC 16 and the headers of each instantiation context.  These lines can contain
a type name, a concept name or a list of template arguments, and a regex must
find its text there.  The driver prints the full output for the reader, and it
applies the regexes to the output without the caret display.

The result store
----------------
Without the store, CTest compiles each fixture again on each run, and these
compiles are most of the cost of the run.  The driver keeps the exit code and
the combined output of each compile in a store in `~/.cache/crucible/neg/`.
CRUCIBLE_NEG_CACHE_DIR sets a different directory.  When the store holds a
correct entry for the compile, the driver uses the stored exit code and output,
and it does not compile.  Then it does the same checks as after a compile: the
removal of the caret display, the regexes and the independence report.  The
store holds the output of the compiler, and not the decision to pass or fail.

CRUCIBLE_NEG_CACHE=0 turns the store off.  CRUCIBLE_NEG_CACHE_MAX_MB sets the
size limit of the store, and the preset value is 1024 MB.  When the store is
larger than its limit, the driver removes the entries that it did not use for
the longest time.  A damaged entry, an entry that the driver cannot read and an
entry in a different format are not correct entries.

Why a stored result is the result of the compile
------------------------------------------------
A compile gives the same exit code and the same output when all its inputs are
the same.  The store makes sure of each input:

1. The key is a hash of these items: the bytes of the compiler driver, of
   cc1plus and of the assembler, the bytes of each file that the command
   names (for example a plugin and its data file), the full command, the
   working directory, each environment variable that GCC reads, and the bytes
   of this file.  When the command contains `-march=native`, the key also
   holds the model and the flags of the host CPU.
2. The entry lists each file that GCC read, from the dependency file of the
   compile, with a hash of its bytes.  The driver reads each file again and
   compares the hashes.  After a fatal error, for example a missing header,
   GCC writes no dependency file.  Then the list comes from the record of
   item 3: libcpp opens each source file with O_NOCTTY, and the list holds
   each file that the compiler opened in that mode.  When GCC writes a
   dependency file, the driver makes sure that the record shows each file of
   that list.  Each compile is then a test of the O_NOCTTY rule.
3. A new file can change the file that an `#include` finds, and GCC did not
   read that new file.  The driver runs the compile under strace, which
   records each file system call of the compiler.  The record holds each
   path that the compile looked up: each name of each `#include`,
   `#include_next`, `#embed` and `__has_include` in each search directory,
   each precompiled header `NAME.gch`, and each program and library that the
   compiler driver looked for.  The compiler makes each lookup itself.  A
   name that a macro gives is in the record, and a probe in a skipped branch
   is not.  A path that the compile writes, such as the object file, and a
   path under /proc, /sys or /dev are not lookups.  No part of the driver
   reads the text of a file that GCC read.
4. The entry holds the condition of each path in the record: no file, a
   regular file, a directory, another kind of file, or a symbolic link with
   its target and the condition of that target.  The driver makes sure that
   each condition is the same.  A new file at one of these paths, or a
   removed one, changes its condition.  A change in a search directory at a
   path that the compile did not look up changes no condition.
5. A file can change while GCC reads it.  The driver does not store a result
   when a file that GCC read, or a path in the record that is not a
   directory, changed in the second before the compile started or after
   that.  It also does not store a result when the condition of a path does
   not agree with the result that GCC got for it.  A directory changes with
   each file in it.  Only the kind of a directory counts.

The driver stores no result in these conditions:

- strace is not in PATH, it did not record the full compile, or it wrote a
  message into the output.  In the last two conditions, the driver compiles
  again without strace, and it uses the output of that compile.
- The exit code is not 0 or 1.  A signal or an internal error of the compiler
  can give a different output on the next run.
- The record shows a call that the driver does not know, a path relative to
  a directory descriptor, a change of the working directory, or an error
  other than "no such file" for a path.
- A precompiled header is in the search list.  GCC reads its bytes, and the
  dependency file does not name it.
- The command gives a plugin an output directory (`-fplugin-arg-NAME-out=`).
  The compile writes a file there, and a stored result does not write it.
- The command reads a profile (`-fprofile-use`, `-fauto-profile`).

The CPU time of a compile
-------------------------
The driver measures the user and the system CPU time of each compile from the
resource use of its child processes.  It runs one compile at a time, so the
CPU time that its waited children gain during the call is the time of the
compiler driver, cc1plus and the assembler.  The budget applies to the user
time.  A compile under strace also counts the system time of the stops of
strace, which was 0.2 s to 1.8 s for one fixture of the tree on the shared
host, and the compile alone had 0.1 s to 0.3 s.  The user time of one fixture
changed by less than 10% with and without strace.  The finding gives the two
times.  The entry of the store keeps them, and a result from the store gives
the times of the compile that made it.

The row fixture-cpu of utils/scripts/budgets.txt gives the thresholds.  A
measure above the warning threshold prints a warning in the format of
utils/scripts/check_report.py, and writes it to the warnings directory, in a
file of this fixture (`fixture-cpu.NAME.txt`).  A measure above the error
threshold fails the test, and the driver does not store that result, so the
next run measures again.  utils/scripts/fixture-cpu-ledger.txt names the
fixtures above the error threshold at this time, `fixture | note`.  A listed
fixture gives a warning, and a listed fixture at or below the error threshold
fails, so that its row goes.  CRUCIBLE_NEG_BUDGETS and CRUCIBLE_NEG_CPU_LEDGER
name a different table and ledger, for the tests of the driver.

test/neg_compile_driver_test.py holds the tests of the store and of the CPU
budget.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import resource
import secrets
import shlex
import shutil
import stat
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

# ── Diagnostic-text normalisation ──────────────────────────────────
#
# The replayed command carries `-fdiagnostics-color=always` (see the
# top-level CMakeLists), so every highlighted token is wrapped in SGR
# escapes and each line carries an EL (`ESC [ K`) sequence.  A regex
# such as `concept GradedWrapper` would not match `concept
# <SGR>GradedWrapper<SGR>`, so escapes are removed before matching.
# Removing them can only make matching see MORE of what the compiler
# actually said; it never invents text.
_ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")

# A GCC caret-display line.  With `-fdiagnostics-show-line-numbers`
# (default since GCC 9, and appended below so the shape is guaranteed
# rather than assumed) every such line carries a left gutter that ends
# in `|`:
#
#     `   19 |     static_assert(GradedWrapper<int>);`   source echo
#     `      |                   ~~~~~^~~~~~~~~~~~~`     caret / range
#     `  +++ |+    #include <foo>`                       fix-it insert
#     `      |                   replacement`            fix-it replace
#
# The gutter is never colourised, and no GCC MESSAGE line begins with
# an optional line number followed by `|` -- messages begin with a
# path, with `In file included from`, with `In function`, with the
# GCC 16 bullet, or with `from`/`required from` continuations.
#
# One known over-reach: `-fanalyzer`'s inline-events path art also draws
# a `|` gutter, so an analyzer path would be removed along with the
# source it quotes.  Neg-compile fixtures assert compile-time rejections
# and never gate on analyzer path text, and `-fanalyzer` is confined to
# its own preset, so nothing in the corpus depends on it.
_GUTTER = re.compile(r"^[ \t]*(?:\d+|\+\+\+)?[ \t]*\|")

# GCC's line-elision marker between two non-adjacent quoted lines.
# Carries no diagnostic text of its own.
_ELISION = re.compile(r"^[ \t]*\.{3,}[ \t]*$")

# A primary diagnostic header.  GCC 16 renders sub-reasons as indented
# `•` bullets belonging to the preceding header, so counting headers
# counts distinct diagnostics rather than lines.  The `:line:col` part is
# optional inside the location, which is how `cc1plus: error: ...` and
# `<built-in>: error: ...` are counted; both carry no fixture line and so
# contribute to the total without being attributed to the fixture.
_ERROR_HEADER = re.compile(
    r"^(?:(?P<file>[^\s:][^:]*):(?:(?P<line>\d+):(?:\d+:)?)?[ \t]*)?"
    r"(?:error|fatal error):[ \t]",
)


def strip_source_echo(text: str) -> str:
    """Return `text` with ANSI escapes and GCC's caret display removed.

    Only the caret display is discarded -- the quoted source line, its
    caret/range underline, and fix-it hint lines.  Every line the
    compiler wrote as prose survives, including the ones that quote a
    type name, a concept name or a template argument list.
    """
    kept: list[str] = []
    for line in text.splitlines():
        plain = _ANSI.sub("", line)
        if _GUTTER.match(plain) or _ELISION.match(plain):
            continue
        kept.append(plain)
    return "\n".join(kept)


def summarise_errors(stripped: str, source: Path) -> tuple[int, list[int]]:
    """Count primary error diagnostics and locate the fixture-local ones.

    Returns the total number of error headers and the sorted distinct
    `line` numbers at which the FIXTURE'S OWN source produced one.  A
    fixture that rejects for exactly one reason reports a single
    fixture-local line; more than one means the file triggers more than
    one independent rejection, and the registered regex may be matching
    a rejection other than the one the fixture documents.
    """
    total = 0
    own: set[int] = set()
    resolved = str(source)
    for line in stripped.splitlines():
        match = _ERROR_HEADER.match(line)
        if match is None:
            continue
        total += 1
        where = match.group("file")
        lineno = match.group("line")
        if where is None or lineno is None:
            continue
        try:
            if Path(where).resolve() == source:
                own.add(int(lineno))
        except OSError:  # pragma: no cover - defensive
            if where == resolved:
                own.add(int(lineno))
    return total, sorted(own)


# ── The compile command ────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class CompileResult:
    """The exit code, the combined standard output and error, and the CPU time of one compile.

    `user_s` and `system_s` are the user and the system CPU time of the
    compile in seconds.  A result from the store gives the times of the
    compile that made it.
    """

    returncode: int
    output: str
    user_s: float
    system_s: float


def _replace_output(argv: list[str], output: Path) -> list[str]:
    """Return `argv` with the object file of the compile set to `output`."""
    result = list(argv)
    for index, arg in enumerate(result):
        if arg == "-o" and index + 1 < len(result):
            result[index + 1] = str(output)
            return result
        if arg.startswith("-o") and len(arg) > 2:
            result[index] = f"-o{output}"
            return result
    return result + ["-o", str(output)]


def _strip_dependency_flags(argv: list[str]) -> list[str]:
    """Return `argv` without the options that write a dependency file.

    The driver adds its own dependency options, and the store reads the file
    that they name.
    """
    result: list[str] = []
    skip_next = False
    for arg in argv:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-MD", "-MMD", "-MP", "-MG"):
            continue
        if arg in ("-MF", "-MT", "-MQ"):
            skip_next = True
            continue
        if arg.startswith(("-MF", "-MT", "-MQ")):
            continue
        result.append(arg)
    return result


def compile_argv(command: list[str], output: Path, depfile: Path) -> list[str]:
    """Return the command that compiles the fixture.

    The object file goes to `output`, and GCC writes the list of the files that
    it read to `depfile`.  The last two arguments are `-MF` and `depfile`.
    """
    argv = _replace_output(_strip_dependency_flags(command), output)

    # A negative-compile fixture asserts a COMPILE-TIME rejection — a
    # property that only manifests under the `enforce` contract
    # evaluation semantic.  Presets that relax contracts (release =>
    # observe; the `ignore` hot-path TUs) would otherwise let the
    # fixture compile clean, inverting its WILL_FAIL / expected-
    # diagnostic gate.  GCC honors the LAST -fcontract-evaluation-
    # semantic flag, so append `enforce` to override whatever the
    # replayed preset command carried.  No-op under default/tsan
    # (already enforce); fixes the release preset.  The flag does not
    # depend on the fixes in utils/toolchain/gcc/patches.  Only appended when
    # contracts are already enabled on the replayed command.
    if any(arg.startswith("-fcontract") for arg in argv):
        argv.append("-fcontract-evaluation-semantic=enforce")

    # A fixture that refuses a target other than the host gets the flags of
    # that target from CRUCIBLE_NEG_EXTRA_FLAGS, which the test sets.  The
    # flags do not go on the fixture target, because they can make the
    # preprocessor fail.  Each guard that preprocesses every unit of the
    # compile database refuses a unit that it cannot read.  The database
    # entry of the fixture then stays a unit of the host.
    argv.extend(shlex.split(os.environ.get("CRUCIBLE_NEG_EXTRA_FLAGS", "")))

    # `strip_source_echo` recognises the caret display by its left
    # gutter, which exists only while line numbers are shown.  That is
    # GCC's default, but a preset (or a future default change) could
    # turn it off and silently reopen the echo hole, so pin it here.
    # GCC honours the LAST occurrence.
    argv.append("-fdiagnostics-show-line-numbers")

    # GCC writes the dependency file also for a compile that fails with an
    # error.  It writes none after a fatal error.
    argv.extend(["-MD", "-MF", str(depfile)])
    return argv


def find_compile_command(build_dir: Path, source: Path) -> tuple[list[str], Path] | None:
    """Return the command and the working directory of `source` in the compile database.

    The search compares the file names as text first, and it resolves each name
    only when no name is equal.  The cost is O(n) in the rows of the database.
    """
    rows = json.loads((build_dir / "compile_commands.json").read_text())
    text = str(source)
    match = next((row for row in rows if row["file"] == text), None)
    if match is None:
        match = next((row for row in rows if Path(row["file"]).resolve() == source), None)
    if match is None:
        return None
    command = match.get("arguments") or shlex.split(match["command"])
    return list(command), Path(match.get("directory", build_dir))


def run_compile(argv: list[str], directory: Path) -> CompileResult:
    """Compile with `argv` in `directory`, and return the exit code, the output and the CPU time.

    The driver runs one child process at a time, so the change of the resource
    use of its waited children over the call is the use of this compile and of
    each process that it waited for.
    """
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    proc = subprocess.run(
        argv,
        cwd=directory,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return CompileResult(proc.returncode, proc.stdout + proc.stderr, after.ru_utime - before.ru_utime,
                         after.ru_stime - before.ru_stime)


# ── The result store ───────────────────────────────────────────────

_STORE_MAGIC = b"crucible-neg-store 3\n"
# A change in this period before the compile started can be a change that the
# compiler did not see.  The period is longer than one tick of the clock that
# sets file times.
_SETTLE_NS = 1_000_000_000
_DEFAULT_LIMIT_MB = 1024
# The store removes entries until it uses at most this part of its limit.
_EVICT_TO = 0.9
_DEPFILE_PLACEHOLDER = "<dependency file>"
# The environment variables that GCC reads.  Each one can change the files
# that GCC finds or the text of a diagnostic.
_GCC_ENVIRONMENT = frozenset({
    "COLUMNS", "COMPILER_PATH", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH",
    "DEPENDENCIES_OUTPUT", "LANG", "LANGUAGE", "LIBRARY_PATH", "OBJC_INCLUDE_PATH",
    "SOURCE_DATE_EPOCH", "SUNPRO_DEPENDENCIES", "TERM", "TERM_URLS",
})
_GCC_ENVIRONMENT_PREFIXES = ("GCC_", "LC_")
_PLUGIN_OUTPUT = re.compile(r"-fplugin-arg-[^=]*-out=")
_PROFILE_INPUT = ("-fprofile-use", "-fauto-profile")

# The options of strace for a compile.  -ff writes one record for each
# process, and the lines of two processes then do not mix.  --seccomp-bpf
# stops a process only at a call that the record holds, and the trace then
# adds little time.  -q keeps the exit line of each process, which shows that
# the record is full.  -x writes a name that holds a byte above 127 in
# hexadecimal, and -s keeps each name whole.
_TRACE_OPTIONS = ("-ff", "-q", "--seccomp-bpf", "-e", "trace=%file", "-x", "-s", "65535")
# One call in a record: the name, the arguments, the result and the error.
_TRACE_CALL = re.compile(
    r"^(?P<call>[a-z_0-9]+)\((?P<args>.*)\)\s+=\s+(?P<result>-?\d+|\?)(?:\s+(?P<errno>E[A-Z0-9]+)\b.*)?$"
)
_TRACE_STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
_TRACE_ESCAPE = re.compile(r"\\(x[0-9a-fA-F]{2}|[0-7]{1,3}|.)")
_TRACE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "v": "\v", "f": "\f", "a": "\a", "b": "\b"}
_TRACE_EXIT = ("+++ exited with ", "+++ killed by ")
# An integer directory descriptor before a name, in the arguments of a call
# after each string is made empty: the name is relative to that directory and
# not to the working directory.
_TRACE_DIRFD = re.compile(r'(?:^|, )\d+, ""')
_WRITE_FLAGS = re.compile(r"\bO_(?:WRONLY|RDWR|CREAT|TRUNC|APPEND|TMPFILE)\b")
# The calls that look up a path, by name.  The value tells whether the call
# follows a symbolic link at the end of the path.
_LOOKUP_CALLS = {
    "open": True, "openat": True, "openat2": True, "stat": True, "stat64": True, "newfstatat": True,
    "fstatat64": True, "statx": True, "access": True, "faccessat": True, "faccessat2": True, "statfs": True,
    "statfs64": True, "execve": True, "execveat": True, "lstat": False, "lstat64": False, "readlink": False,
    "readlinkat": False, "getxattr": True, "lgetxattr": False, "listxattr": True, "llistxattr": False,
}
# The calls that write a path.  The compile makes each path that one of them
# names, and no such path is an input of the compile.
_WRITE_CALLS = frozenset({
    "creat", "unlink", "unlinkat", "rename", "renameat", "renameat2", "mkdir", "mkdirat", "rmdir", "link",
    "linkat", "symlink", "symlinkat", "chmod", "fchmodat", "fchmodat2", "chown", "lchown", "fchownat",
    "truncate", "truncate64", "utime", "utimes", "utimensat", "futimesat", "mknod", "mknodat", "setxattr",
    "lsetxattr", "removexattr", "lremovexattr",
})
# The calls that name no input of the compile.  getcwd gives the working
# directory, which the key holds.
_IGNORED_CALLS = frozenset({"getcwd"})
# The kernel makes the files of these trees, and their contents depend on the
# process that reads them.
_KERNEL_TREES = ("/proc", "/sys", "/dev")

_driver_digest_value: str | None = None


def _driver_digest() -> str:
    """Return the hash of the bytes of this file."""
    global _driver_digest_value
    if _driver_digest_value is None:
        _driver_digest_value = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    return _driver_digest_value


def _short_digest(data: bytes) -> str:
    """Return the first 128 bits of the SHA-256 hash of `data`, as hexadecimal text."""
    return hashlib.sha256(data).hexdigest()[:32]


def _stat_fields(status: os.stat_result) -> list[int]:
    """Return the fields of `status` that change when the file changes."""
    return [status.st_dev, status.st_ino, status.st_size, status.st_mtime_ns, status.st_ctime_ns]


def _write_atomic(path: Path, data: bytes) -> None:
    """Write `data` to `path` through a temporary file and a rename.

    A reader sees the previous file or the full new file, and never a part of
    one.  An OSError goes to the caller.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.{os.getpid()}.{secrets.token_hex(4)}.tmp"
    try:
        temporary.write_bytes(data)
        os.replace(temporary, path)
    except OSError:
        temporary.unlink(missing_ok=True)
        raise


def file_digest(path: str, memo_dir: Path) -> str:
    """Return the hash of the bytes of the file `path`.

    The hash of cc1plus (about 400 MB) costs more than the rest of a lookup.  A
    memo keeps each hash with the device, the inode, the size and the two change
    times of the file.  The memo gives the hash only while the five fields are
    the same.  The function writes a memo only for a file that did not change
    in the settle period, so a change in the same clock tick cannot hide.
    """
    status = os.stat(path)
    fields = _stat_fields(status)
    memo = memo_dir / f"{_short_digest(os.fsencode(os.path.realpath(path)))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if recorded.get("stat") == fields and isinstance(recorded.get("digest"), str):
            return recorded["digest"]
    except (OSError, ValueError, AttributeError):
        pass
    hasher = hashlib.sha256()
    with open(path, "rb") as stream:
        while chunk := stream.read(1 << 20):
            hasher.update(chunk)
    digest = hasher.hexdigest()[:32]
    after = os.stat(path)
    if _stat_fields(after) == fields and after.st_ctime_ns < time.time_ns() - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({"stat": fields, "digest": digest}).encode("ascii"))
        except OSError:
            pass
    return digest


def _program_path(driver: str, program: str, prefix_args: list[str], directory: Path) -> str | None:
    """Return the path of the program that `driver` runs as `program`, or None."""
    proc = subprocess.run(
        [driver, *prefix_args, f"-print-prog-name={program}"],
        cwd=directory,
        text=True,
        capture_output=True,
        check=False,
    )
    name = proc.stdout.strip()
    if proc.returncode != 0 or not name:
        return None
    # GCC prints the bare name of a program outside its own directories, and it
    # then finds the program through PATH.
    if not os.path.isabs(name):
        name = shutil.which(name) or ""
    return name if os.path.isfile(name) else None


def _host_cpu() -> str:
    """Return the lines of /proc/cpuinfo that tell the model and the flags of the first CPU.

    `-march=native` gives the compiler the features of the host CPU, and no file
    that GCC reads holds them.
    """
    try:
        first = Path("/proc/cpuinfo").read_text().split("\n\n", 1)[0]
    except OSError:
        return ""
    fields = {
        "vendor_id", "cpu family", "model", "flags",
        "CPU implementer", "CPU architecture", "CPU variant", "CPU part", "Features",
    }
    return "\n".join(line for line in first.splitlines() if line.split(":", 1)[0].strip() in fields)


def _named_files(arg: str) -> list[str]:
    """Return the texts in one argument that can name a file."""
    names = [arg]
    if "=" in arg:
        names.append(arg.split("=", 1)[1])
    if arg.startswith("@"):
        names.append(arg[1:])
    return [name for name in names if name]


def compile_key(argv: list[str], directory: Path, protected: set[str], memo_dir: Path) -> tuple[str | None, str]:
    """Return the store key of the compile, or None and the reason for no key.

    `argv` ends with `-MF` and the dependency file.  `protected` holds the
    resolved paths of the source, the object file and the dependency file, which
    are not inputs of the key.
    """
    for arg in argv:
        if arg.startswith(_PROFILE_INPUT):
            return None, "the command reads a profile"
        if _PLUGIN_OUTPUT.match(arg):
            return None, "a plugin of the command writes an output file"
    driver = argv[0] if os.sep in argv[0] else shutil.which(argv[0])
    if not driver:
        return None, f"the compiler driver {argv[0]} is not in PATH"
    driver = os.path.join(directory, driver)
    prefix_args = [arg for index, arg in enumerate(argv) if arg.startswith("-B") or argv[index - 1] == "-B"]
    try:
        identity: list[list[str | None]] = [["driver", file_digest(driver, memo_dir)]]
        for program in ("cc1plus", "as"):
            path = _program_path(driver, program, prefix_args, directory)
            identity.append([program, path, file_digest(path, memo_dir) if path else None])
        files: list[list[int | str]] = []
        for index, arg in enumerate(argv[1:], start=1):
            for name in _named_files(arg):
                full = os.path.join(directory, name)
                if os.path.isfile(full) and os.path.realpath(full) not in protected:
                    files.append([index, name, file_digest(full, memo_dir)])
    except OSError as error:
        return None, f"an input of the key cannot be read: {error}"
    environment = sorted(
        [name, value]
        for name, value in os.environ.items()
        if name in _GCC_ENVIRONMENT or name.startswith(_GCC_ENVIRONMENT_PREFIXES)
    )
    is_native = any(arg.startswith("-m") and arg.endswith("=native") for arg in argv)
    material = json.dumps(
        {
            "argv": argv[:-1] + [_DEPFILE_PLACEHOLDER],
            "directory": str(directory),
            "driver": _driver_digest(),
            "environment": environment,
            "files": files,
            "format": _STORE_MAGIC.decode("ascii"),
            "host": _host_cpu() if is_native else "",
            "identity": identity,
        },
        sort_keys=True,
    )
    return hashlib.sha256(material.encode("ascii")).hexdigest(), ""


def parse_dependency_file(text: str) -> list[str] | None:
    """Return the prerequisites of the one rule in a dependency file of GCC.

    GCC puts a backslash before a space, a tab and a `#` in a file name, and it
    writes a `$` two times.  The function returns None for a file with no rule
    or with more than one rule.  The cost is O(n) in the length of the text.
    """
    words: list[str] = []
    current: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        follower = text[index + 1] if index + 1 < length else ""
        if char == "\\" and follower in ("\n", " ", "\t", "#"):
            if follower == "\n":
                if current:
                    words.append("".join(current))
                    current = []
            else:
                current.append(follower)
            index += 2
            continue
        if char == "$" and follower == "$":
            current.append("$")
            index += 2
            continue
        if char in " \t\r\n":
            if current:
                words.append("".join(current))
                current = []
            index += 1
            continue
        current.append(char)
        index += 1
    if current:
        words.append("".join(current))
    targets = [position for position, word in enumerate(words) if word.endswith(":")]
    if len(targets) != 1:
        return None
    return words[targets[0] + 1 :]


@dataclass(slots=True)
class TraceRecord:
    """What the strace record of one compile shows.

    `lookups` maps each path that the compile looked up to the set of the
    results that it got: (does the call follow a symbolic link at the end,
    does the path exist).  `sources` holds each regular file that libcpp
    opened (with O_NOCTTY), in the sequence of the record.  `refusal` is the
    reason that the record cannot describe the inputs of the compile, or an
    empty text.
    """

    lookups: dict[str, set[tuple[bool, bool]]]
    sources: list[str]
    refusal: str


def _trace_string(text: str) -> str:
    """Return the name that strace writes as `text`, a quoted string without its quotes.

    strace writes a backslash before a quote and before a backslash, a C
    escape for a control character, and two hexadecimal digits for each byte
    of a name that holds a byte above 127.  The cost is O(n) in the length.
    """
    def unescape(match: re.Match[str]) -> str:
        """Return the character of one escape."""
        code = match.group(1)
        if code[0] == "x":
            return chr(int(code[1:], 16))
        if code.isdigit():
            return chr(int(code, 8))
        return _TRACE_ESCAPES.get(code, code)

    raw = _TRACE_ESCAPE.sub(unescape, text)
    return os.fsdecode(raw.encode("latin-1"))


def _trace_calls(path: Path) -> tuple[list[re.Match[str]], bool, str]:
    """Return the calls in the record of one process, True when the record is full, and the first unread line.

    A full record ends with the exit line of the process.  A call that a
    signal interrupts takes two lines, and the function joins them.  A line
    of a signal or of the process is not a call.  The function returns each
    other line that it cannot read as a call, because a call that it cannot
    read can be a lookup.
    """
    calls: list[re.Match[str]] = []
    pending = ""
    unread = ""
    is_full = False
    for line in path.read_text(encoding="latin-1").splitlines():
        if line.startswith(_TRACE_EXIT):
            is_full = True
            continue
        if line.startswith(("--- ", "+++ ")) or not line:
            continue
        if line.endswith(" <unfinished ...>"):
            pending = line[: -len(" <unfinished ...>")]
            continue
        if line.startswith("<... ") and " resumed>" in line and pending:
            line = pending + line.split(" resumed>", 1)[1]
            pending = ""
        match = _TRACE_CALL.match(line)
        if match is None:
            unread = unread or line
            continue
        calls.append(match)
    return calls, is_full, unread or pending


def read_trace(prefix: Path, argv: list[str], directory: Path) -> tuple[TraceRecord | None, str]:
    """Return what the records `prefix.PID` of a traced compile show, or None and the reason.

    The records are full when each one ends with its exit line, and when one
    process ran `argv[0]`.  A path that a call writes is an output of the
    compile, and a path in a tree of the kernel is not a file.  Neither one is
    a lookup.  The cost is O(n) in the length of the records.
    """
    paths = sorted(prefix.parent.glob(prefix.name + ".*"))
    driver = argv[0] if os.sep in argv[0] else (shutil.which(argv[0]) or argv[0])
    driver = os.path.realpath(os.path.join(directory, driver))
    lookups: dict[str, set[tuple[bool, bool]]] = {}
    written: set[str] = set()
    sources: list[str] = []
    refusal = ""
    has_driver = False
    for record_path in paths:
        calls, is_full, unread = _trace_calls(record_path)
        if not is_full:
            return None, f"the strace record {record_path.name} has no exit line"
        if unread:
            refusal = refusal or f"the strace record {record_path.name} holds a line that is not a call: {unread}"
        for index, call in enumerate(calls):
            name, args, result, errno = call.group("call", "args", "result", "errno")
            strings = [_trace_string(text) for text in _TRACE_STRING.findall(args)]
            if name == "execve" and index == 0 and result == "0" and strings:
                has_driver = has_driver or os.path.realpath(os.path.join(directory, strings[0])) == driver
            if name in _IGNORED_CALLS:
                continue
            if name == "chdir":
                refusal = refusal or "the compile changes its working directory"
                continue
            is_open = name in ("open", "openat", "openat2")
            is_write = name in _WRITE_CALLS or (is_open and bool(_WRITE_FLAGS.search(args)))
            # A lookup names one path.  The other strings of execve are its arguments.
            named = strings if is_write else strings[:1]
            if any(text and not os.path.isabs(text) for text in named) and _TRACE_DIRFD.search(
                _TRACE_STRING.sub('""', args)
            ):
                refusal = refusal or f"a call names a path relative to a directory descriptor: {name}"
                continue
            if is_write:
                written.update(os.path.join(directory, text) for text in named if text)
                continue
            if name not in _LOOKUP_CALLS:
                refusal = refusal or f"the compile makes a file system call that the driver does not know: {name}"
                continue
            if not strings or not strings[0]:
                continue
            target = os.path.join(directory, strings[0])
            follows = _LOOKUP_CALLS[name] and "AT_SYMLINK_NOFOLLOW" not in args and "O_NOFOLLOW" not in args
            if result != "-1" or (errno == "EINVAL" and name in ("readlink", "readlinkat")):
                exists = True
            elif errno in ("ENOENT", "ENOTDIR"):
                exists = False
            else:
                refusal = refusal or f"the compile got {errno} for {target}"
                continue
            lookups.setdefault(target, set()).add((follows, exists))
            if is_open and exists and "O_NOCTTY" in args:
                sources.append(target)
    if not has_driver:
        return None, f"no strace record shows the start of {argv[0]}"

    def is_input(path: str) -> bool:
        """Return True when `path` is not an output of the compile and not in a tree of the kernel."""
        return path not in written and not any(path == tree or path.startswith(tree + "/") for tree in _KERNEL_TREES)

    kept = {path: results for path, results in lookups.items() if is_input(path)}
    return TraceRecord(kept, list(dict.fromkeys(filter(is_input, sources))), refusal), ""


def traced_compile(argv: list[str], directory: Path, prefix: Path) -> tuple[CompileResult, TraceRecord | None, str]:
    """Compile under strace, and return the result, the record and the reason for no record.

    When strace is not in PATH, the function compiles without it.  When the
    records are not full, or strace wrote a message into the output, the
    output of the compile is not the output of the compiler alone.  Then the
    function compiles again without strace.  It removes the records.
    """
    tracer = shutil.which("strace")
    if tracer is None:
        return run_compile(argv, directory), None, "strace is not in PATH"
    try:
        result = run_compile([tracer, *_TRACE_OPTIONS, "-o", str(prefix), "--", *argv], directory)
        record, reason = read_trace(prefix, argv, directory)
        has_message = any(line.startswith("strace: ") for line in result.output.splitlines())
        if record is None or has_message:
            Path(argv[-1]).unlink(missing_ok=True)
            return run_compile(argv, directory), None, reason or "strace wrote a message into the output"
        return result, record, ""
    finally:
        for path in prefix.parent.glob(prefix.name + ".*"):
            path.unlink(missing_ok=True)


def _kind(status: os.stat_result) -> str:
    """Return the kind of a file: "file" for a regular file, "dir" for a directory, or "other"."""
    if stat.S_ISREG(status.st_mode):
        return "file"
    return "dir" if stat.S_ISDIR(status.st_mode) else "other"


def path_state(path: str) -> str | list[str | None] | None:
    """Return the condition of a path that the compile looked up.

    The condition is None when no file has the name, the kind of the file
    (_kind), an error text when the driver cannot read the name, or
    ["link", the target, the condition of the file that the link names] for a
    symbolic link.  A regular file is a dependency, whose bytes the entry
    holds, or a file of which only the presence decides what the compile
    does.  An edit of a file then changes no condition.
    """
    try:
        status = os.lstat(path)
        if not stat.S_ISLNK(status.st_mode):
            return _kind(status)
        target = os.readlink(path)
    except (FileNotFoundError, NotADirectoryError):
        return None
    except OSError as error:
        return f"error {error.errno}"
    try:
        named: str | None = _kind(os.stat(path))
    except (FileNotFoundError, NotADirectoryError):
        named = None
    except OSError as error:
        named = f"error {error.errno}"
    return ["link", target, named]


def _changed_since(path: str, moment_ns: int) -> bool:
    """Return True when the entry of `path` itself changed at or after `moment_ns`, or cannot be read."""
    try:
        return os.lstat(path).st_ctime_ns >= moment_ns
    except OSError:
        return True


def _agrees(state: str | list[str | None] | None, follows: bool, exists: bool) -> bool:
    """Return True when `state` gives the result that the compile got for its path.

    A call that follows a link sees the file that the link names.  An error
    state agrees with no result.
    """
    if isinstance(state, list) and follows:
        state = state[2]
    if isinstance(state, str) and state.startswith("error"):
        return False
    return (state is not None) == exists


def encode_entry(entry: dict[str, object]) -> bytes:
    """Return the bytes of a store entry: a format line, a checksum and the packed entry."""
    payload = zlib.compress(json.dumps(entry, separators=(",", ":")).encode("ascii"), 6)
    return _STORE_MAGIC + hashlib.sha256(payload).hexdigest().encode("ascii") + b"\n" + payload


def _is_state(value: object) -> bool:
    """Return True when `value` has the shape of a `path_state` result."""
    if value is None or isinstance(value, str):
        return True
    return (
        isinstance(value, list)
        and len(value) == 3
        and value[0] == "link"
        and isinstance(value[1], str)
        and (value[2] is None or isinstance(value[2], str))
    )


def decode_entry(blob: bytes) -> dict[str, object] | None:
    """Return the entry in `blob`, or None when the bytes are not a correct entry."""
    if not blob.startswith(_STORE_MAGIC):
        return None
    checksum, newline, payload = blob[len(_STORE_MAGIC) :].partition(b"\n")
    if not newline or hashlib.sha256(payload).hexdigest().encode("ascii") != checksum:
        return None
    try:
        entry = json.loads(zlib.decompress(payload))
    except (zlib.error, ValueError):
        return None
    if not isinstance(entry, dict):
        return None
    dependencies = entry.get("dependencies")
    searched = entry.get("searched")
    returncode = entry.get("returncode")
    times = (entry.get("user_s"), entry.get("system_s"))
    is_well_formed = (
        isinstance(dependencies, list)
        and all(
            isinstance(item, list) and len(item) == 2 and all(isinstance(part, str) for part in item)
            for item in dependencies
        )
        and isinstance(searched, list)
        and all(
            isinstance(item, list) and len(item) == 2 and isinstance(item[0], str) and _is_state(item[1])
            for item in searched
        )
        and isinstance(returncode, int)
        and not isinstance(returncode, bool)
        and isinstance(entry.get("output"), str)
        and all(
            isinstance(value, (int, float)) and not isinstance(value, bool) and 0 <= value < float("inf")
            for value in times
        )
    )
    return entry if is_well_formed else None


class ResultStore:
    """The store of compile results in one directory.

    `entries/` holds one file for each key, and `identity/` holds the memo of
    `file_digest`.  Many fixtures read and write the store at the same time.
    Each write goes through a temporary file and a rename, and a reader that
    loses a file to a removal counts a miss.
    """

    def __init__(self, root: Path, limit_bytes: int) -> None:
        """Use the store in `root`, with a size limit of `limit_bytes`."""
        self.root = root
        self.entries = root / "entries"
        self.identity = root / "identity"
        self.limit_bytes = limit_bytes

    def entry_path(self, key: str) -> Path:
        """Return the path of the entry for `key`."""
        return self.entries / key[:2] / key

    def lookup(self, key: str) -> tuple[CompileResult | None, str]:
        """Return the stored result for `key`, or None and the reason for a miss.

        The function makes sure that each search path has the same condition
        and that each dependency has the same bytes.  The cost is O(n) in the
        bytes of the dependencies.
        """
        path = self.entry_path(key)
        try:
            blob = path.read_bytes()
        except OSError:
            return None, "no entry"
        entry = decode_entry(blob)
        if entry is None:
            return None, "the entry is damaged"
        for name, state in entry["searched"]:  # type: ignore[union-attr]
            if path_state(name) != state:
                return None, f"a search path changed: {name}"
        for name, digest in entry["dependencies"]:  # type: ignore[union-attr]
            try:
                data = Path(name).read_bytes()
            except OSError:
                return None, f"a dependency cannot be read: {name}"
            if _short_digest(data) != digest:
                return None, f"a dependency changed: {name}"
        try:
            os.utime(path)
        except OSError:
            pass
        return CompileResult(entry["returncode"], entry["output"], float(entry["user_s"]),  # type: ignore[arg-type]
                             float(entry["system_s"])), ""  # type: ignore[arg-type]

    def record(
        self,
        key: str,
        result: CompileResult,
        dependencies: list[str] | None,
        trace: TraceRecord,
        started_ns: int,
    ) -> tuple[bool, str]:
        """Store `result` for `key`, and return True, or False and the reason.

        `dependencies` is the list of the dependency file, or None when GCC
        wrote none.  Then the list is the source files of the record.  The
        function refuses a result whose inputs it cannot record in full, or
        whose inputs changed in the settle period before `started_ns`.  The
        cost is O(n + p) for n bytes of dependencies and p paths of the record.
        """
        if result.returncode not in (0, 1):
            return False, f"the exit code is {result.returncode}"
        if trace.refusal:
            return False, trace.refusal
        if dependencies is None:
            dependencies = [name for name in trace.sources if os.path.isfile(name)]
        else:
            opened = {os.path.realpath(name) for name in trace.sources}
            unseen = next((name for name in dependencies if os.path.realpath(name) not in opened), None)
            if unseen is not None:
                return False, f"the strace record shows no open of the dependency {unseen}"
        settled_ns = started_ns - _SETTLE_NS
        recorded: list[list[str]] = []
        for name in dependencies:
            try:
                before = os.stat(name)
                data = Path(name).read_bytes()
                after = os.stat(name)
            except OSError:
                return False, f"a dependency cannot be read: {name}"
            if _stat_fields(before) != _stat_fields(after) or after.st_ctime_ns >= settled_ns:
                return False, f"a dependency changed less than one second before the compile: {name}"
            recorded.append([name, _short_digest(data)])
        states: list[list[object]] = []
        for name, results in trace.lookups.items():
            state = path_state(name)
            if not all(_agrees(state, follows, exists) for follows, exists in results):
                return False, f"a search path changed while GCC read it: {name}"
            if state is not None and name.endswith(".gch"):
                return False, f"a precompiled header is in the search list: {name}"
            # A file can be replaced by another kind of file with the same result
            # of the call.  A directory changes with each file in it, and its kind
            # alone decides a lookup.
            if state is not None and state != "dir" and _changed_since(name, settled_ns):
                return False, f"a search path changed less than one second before the compile: {name}"
            states.append([name, state])
        entry = {
            "dependencies": recorded,
            "searched": states,
            "output": result.output,
            "returncode": result.returncode,
            "user_s": round(result.user_s, 3),
            "system_s": round(result.system_s, 3),
        }
        try:
            _write_atomic(self.entry_path(key), encode_entry(entry))
        except OSError as error:
            return False, f"the entry cannot be written: {error}"
        self.evict()
        return True, ""

    def evict(self) -> None:
        """Remove the entries that the store did not use for the longest time.

        The function removes entries until the store uses at most 90 percent of
        its limit.  It also removes each temporary file older than one hour, the
        remains of a writer that stopped.  The cost is O(n log n) in the number
        of entries.
        """
        files: list[tuple[int, int, Path]] = []
        expired_ns = time.time_ns() - 3600 * 1_000_000_000
        try:
            buckets = list(self.entries.iterdir())
        except OSError:
            return
        for bucket in buckets:
            try:
                names = list(os.scandir(bucket))
            except OSError:
                continue
            for item in names:
                try:
                    status = item.stat()
                except OSError:
                    continue
                if item.name.endswith(".tmp"):
                    if status.st_mtime_ns < expired_ns:
                        Path(item.path).unlink(missing_ok=True)
                    continue
                files.append((status.st_mtime_ns, status.st_size, Path(item.path)))
        total = sum(size for _, size, _ in files)
        if total <= self.limit_bytes:
            return
        goal = int(self.limit_bytes * _EVICT_TO)
        for _, size, path in sorted(files, key=lambda item: item[0]):
            if total <= goal:
                break
            path.unlink(missing_ok=True)
            total -= size


def _store_limit_bytes() -> int:
    """Return the size limit of the store from CRUCIBLE_NEG_CACHE_MAX_MB."""
    text = os.environ.get("CRUCIBLE_NEG_CACHE_MAX_MB", "")
    if not text:
        return _DEFAULT_LIMIT_MB << 20
    megabytes = int(text) if text.isdigit() else 0
    if megabytes <= 0:
        print(
            f"CRUCIBLE_NEG_CACHE_MAX_MB is '{text}'.  Set it to a positive number of megabytes.  "
            f"The store uses {_DEFAULT_LIMIT_MB} MB.",
            file=sys.stderr,
        )
        return _DEFAULT_LIMIT_MB << 20
    return megabytes << 20


def open_store() -> tuple[ResultStore | None, str]:
    """Return the result store, or None and the reason when the store is off."""
    if os.environ.get("CRUCIBLE_NEG_CACHE") == "0":
        return None, "the store is off"
    configured = os.environ.get("CRUCIBLE_NEG_CACHE_DIR")
    root = Path(configured) if configured else Path.home() / ".cache" / "crucible" / "neg"
    try:
        (root / "entries").mkdir(parents=True, exist_ok=True)
        (root / "identity").mkdir(parents=True, exist_ok=True)
    except OSError as error:
        return None, f"the store directory cannot be made: {error}"
    return ResultStore(root, _store_limit_bytes()), ""


def _note(fixture_name: str, text: str) -> None:
    """Print one line about the store for the fixture."""
    print(f"neg-compile {fixture_name}: {text}", file=sys.stderr)


# ── The CPU budget of a compile ────────────────────────────────────

_REPO_ROOT = Path(__file__).resolve().parents[1]
_SCRIPTS = _REPO_ROOT / "utils" / "scripts"
_CPU_CHECK = "fixture-cpu"
_CPU_LEDGER = _SCRIPTS / "fixture-cpu-ledger.txt"


@dataclass(frozen=True, slots=True)
class CpuBudget:
    """The thresholds of the row fixture-cpu, and the rows of the ledger.

    `problem` is the reason that the table or the ledger cannot be read, or
    an empty text.  A budget with a problem fails the test.
    """

    warn: float
    error: float
    ledger: dict[str, str]
    problem: str

    def is_over_error(self, fixture_name: str, user_s: float) -> bool:
        """Return True when a measure fails the test because of the error threshold alone."""
        return user_s > self.error and fixture_name not in self.ledger


def _report_module():
    """Return the module utils/scripts/check_report.py."""
    if str(_SCRIPTS) not in sys.path:
        sys.path.insert(0, str(_SCRIPTS))
    import check_report

    return check_report


def read_cpu_budget() -> CpuBudget:
    """Read the row fixture-cpu of the budget table and the rows of the CPU ledger.

    A ledger row is `fixture | note`.  CRUCIBLE_NEG_BUDGETS and
    CRUCIBLE_NEG_CPU_LEDGER name a different table and ledger.
    """
    table = Path(os.environ.get("CRUCIBLE_NEG_BUDGETS") or _SCRIPTS / "budgets.txt")
    ledger_path = Path(os.environ.get("CRUCIBLE_NEG_CPU_LEDGER") or _CPU_LEDGER)
    try:
        row = _report_module().read_budgets(table).get(_CPU_CHECK)
        if row is None:
            return CpuBudget(0.0, 0.0, {}, f"the budget table {table} has no row {_CPU_CHECK}")
        ledger: dict[str, str] = {}
        for number, raw in enumerate(ledger_path.read_text(encoding="utf-8").splitlines(), start=1):
            text = raw.strip()
            if not text or text.startswith("#"):
                continue
            cells = [cell.strip() for cell in text.split("|")]
            if len(cells) != 2 or not all(cells) or cells[0] in ledger:
                return CpuBudget(0.0, 0.0, {}, f"{ledger_path}:{number}: a row is `fixture | note`, with two "
                                               f"cells that are not empty, and one row for each fixture")
            ledger[cells[0]] = cells[1]
    except (OSError, ValueError) as error:
        return CpuBudget(0.0, 0.0, {}, f"the budget of {_CPU_CHECK} cannot be read: {error}")
    return CpuBudget(row.warn, row.error, ledger, "")


def report_cpu(fixture_name: str, source: Path, result: CompileResult, is_stored: bool, budget: CpuBudget,
               warnings_dir: Path | None) -> int:
    """Report the CPU time of the compile against the budget, and return 1 when it fails the test.

    The warning line goes to the file of this fixture in the warnings
    directory, so the many fixtures that run at the same time do not write
    one file.
    """
    report = _report_module()
    try:
        place = str(source.relative_to(_REPO_ROOT))
    except ValueError:
        place = str(source)
    findings = []
    measured = "the stored compile" if is_stored else "the compile"
    text = (f"{measured} of the fixture {fixture_name} took {result.user_s:.2f} s of user CPU time (and "
            f"{result.system_s:.2f} s of system time)")
    if budget.problem:
        findings.append(report.Finding("error", place, 0, _CPU_CHECK, f"{budget.problem}."))
    elif fixture_name in budget.ledger and result.user_s <= budget.error:
        findings.append(report.Finding("error", place, 0, _CPU_CHECK,
                                       f"{text}, no more than the error threshold {budget.error:g} s.  Remove its "
                                       f"row from utils/scripts/fixture-cpu-ledger.txt."))
    elif fixture_name in budget.ledger:
        findings.append(report.Finding("warning", place, 0, _CPU_CHECK,
                                       f"{text}, more than the error threshold {budget.error:g} s.  It has a row in "
                                       f"the ledger: {budget.ledger[fixture_name]}"))
    elif result.user_s > budget.error:
        findings.append(report.Finding("error", place, 0, _CPU_CHECK,
                                       f"{text}, more than the error threshold {budget.error:g} s.  Include only the "
                                       f"header that the fixture attacks, or make the tables that it does not read "
                                       f"lazy."))
    elif result.user_s > budget.warn:
        findings.append(report.Finding("warning", place, 0, _CPU_CHECK,
                                       f"{text}, more than the warning threshold {budget.warn:g} s."))
    return report.emit(findings, _CPU_CHECK, warnings_dir, fixture_name)


def obtain_result(fixture_name: str, argv: list[str], directory: Path, source: Path, output: Path,
                  budget: CpuBudget) -> tuple[CompileResult, bool]:
    """Return the result of the compile, from the store or from a compile, and True when it comes from the store.

    `argv` ends with `-MF` and the dependency file of this process.  After a
    compile, the dependency file goes to the name of the fixture in the scratch
    directory.  When the store can take the result, the compile runs under
    strace (traced_compile).  A stored result whose compile took more CPU time
    than the error threshold is a miss, so the driver measures again.
    """
    depfile = Path(argv[-1])
    store, reason = open_store()
    key: str | None = None
    if store is not None:
        protected = {os.path.realpath(path) for path in (source, output, depfile)}
        key, reason = compile_key(argv, directory, protected, store.identity)
    if store is not None and key is not None:
        stored, reason = store.lookup(key)
        if stored is not None and budget.is_over_error(fixture_name, stored.user_s):
            stored, reason = None, "the stored compile took more CPU time than the error threshold"
        if stored is not None:
            _note(fixture_name, f"the result comes from the store (entry {key})")
            return stored, True
    _note(fixture_name, f"compiled ({reason})")
    depfile.unlink(missing_ok=True)
    started_ns = time.time_ns()
    trace: TraceRecord | None = None
    trace_reason = ""
    if store is not None and key is not None:
        prefix = depfile.with_name(f"{depfile.stem}.trace")
        result, trace, trace_reason = traced_compile(argv, directory, prefix)
    else:
        result = run_compile(argv, directory)
    dependencies: list[str] | None = None
    is_readable = True
    try:
        text = depfile.read_text(errors="surrogateescape")
        os.replace(depfile, depfile.with_name(f"{fixture_name}.d"))
    except OSError:
        text = None
    if text is not None:
        parsed = parse_dependency_file(text)
        is_readable = parsed is not None
        if parsed is not None:
            dependencies = list(dict.fromkeys(os.path.join(directory, name) for name in parsed))
    if store is not None and key is not None:
        if trace is None:
            is_stored, why = False, trace_reason
        elif not is_readable:
            is_stored, why = False, "the dependency file of GCC does not hold exactly one rule"
        elif budget.is_over_error(fixture_name, result.user_s):
            is_stored, why = False, "the compile took more CPU time than the error threshold"
        else:
            is_stored, why = store.record(key, result, dependencies, trace, started_ns)
        _note(fixture_name, f"stored (entry {key})" if is_stored else f"not stored ({why})")
    return result, False


# ── The test result ────────────────────────────────────────────────


def evaluate(fixture_name: str, source: Path, expected_regexes: list[str], result: CompileResult) -> int:
    """Print the output of the compile, apply the regexes, and return the exit code of the test.

    The function gives the same exit code and the same messages for a stored
    result and for a fresh compile.
    """
    combined = result.output
    # The HUMAN sees the compiler's output verbatim, carets and all.
    sys.stdout.write(combined)
    # Matching sees only what the compiler said, never what the fixture
    # wrote.  See the module docstring.
    matchable = strip_source_echo(combined)

    if result.returncode == 0:
        print(
            f"negative fixture {fixture_name} compiled successfully",
            file=sys.stderr,
        )
        return 1

    # Independence report.  A fixture documents ONE rejection; if its
    # own source produces error headers at several distinct lines it is
    # rejecting for several reasons at once, and the registered regex
    # may be witnessing a different one than the file claims.  Reported
    # unconditionally so the count shows up in CI logs; promoted to a
    # failure under CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 so the discipline
    # can be swept without editing 2,000+ registrations.
    total_errors, own_lines = summarise_errors(matchable, source)
    strict = os.environ.get("CRUCIBLE_NEG_STRICT_SINGLE_ERROR") == "1"
    not_independent = len(own_lines) > 1
    if not_independent:
        detail = ", ".join(str(n) for n in own_lines)
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local errors at {len(own_lines)} distinct lines "
            f"({detail}) — the fixture rejects for more than one reason",
            file=sys.stderr,
        )
    else:
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local error lines {own_lines}",
            file=sys.stderr,
        )

    # EVERY required regex must appear.  For a single-regex fixture this
    # is identical to the original `re.search` gate.  For a multi-cell
    # fixture, a missing regex means one cell stopped failing (discipline
    # slipped on that cell) OR its diagnostic text drifted — either way
    # the merged TU no longer proves what it claims, and we fail loudly
    # naming the specific cell that went silent.
    missing = [
        pattern
        for pattern in expected_regexes
        if not re.search(pattern, matchable, flags=re.MULTILINE)
    ]
    if missing:
        for pattern in missing:
            print(
                f"expected diagnostic not found for {fixture_name}: "
                f"{pattern}",
                file=sys.stderr,
            )
            # The most common cause, by a wide margin, is a regex that
            # was only ever satisfied by GCC quoting the fixture's own
            # source back at it.  Say so instead of leaving the author
            # to rediscover it.
            if re.search(pattern, _ANSI.sub("", combined), flags=re.MULTILINE):
                print(
                    f"  note: {fixture_name} matched this pattern ONLY in "
                    f"the quoted source line, not in any diagnostic the "
                    f"compiler produced — the fixture is asserting its own "
                    f"text, not a rejection",
                    file=sys.stderr,
                )
        return 1
    # The regex verdict is the more informative one, so it is reported
    # first; the strict-mode verdict is applied only after it.
    return 1 if (strict and not_independent) else 0


def main(arguments: list[str]) -> int:
    """Run one fixture, and return the exit code of its test."""
    # arguments[4..] are one or more required-diagnostic regexes.  The
    # harnesses in the CMake files pass two or more: the first shows that
    # a rejection occurred, and the next ones show which gate rejected.
    # EVERY listed regex must appear in the compile output.  AND semantics
    # are mandatory: a fixture whose generic regex matched but whose
    # specific regex went silent would otherwise pass.  See the two-regex
    # floor above `crucible_neg_compile_test` in test/CMakeLists.txt.
    # `--warnings-dir DIR` before the other arguments names the warnings
    # directory of utils/scripts/check_report.py.
    warnings_dir: Path | None = None
    if len(arguments) >= 3 and arguments[1] == "--warnings-dir":
        warnings_dir = Path(arguments[2])
        arguments = [arguments[0], *arguments[3:]]
    if len(arguments) < 5:
        print(
            "usage: neg_compile_driver.py [--warnings-dir DIR] <build-dir> <source> "
            "<fixture-name> <expected-regex> [<expected-regex> ...]",
            file=sys.stderr,
        )
        return 2

    build_dir = Path(arguments[1]).resolve()
    source = Path(arguments[2]).resolve()
    fixture_name = arguments[3]
    expected_regexes = arguments[4:]
    if not _report_module().WRITER_KEY.fullmatch(fixture_name):
        print(f"the fixture name {fixture_name!r} is not a word of letters, digits, '_' and '-'", file=sys.stderr)
        return 2

    found = find_compile_command(build_dir, source)
    if found is None:
        print(f"no compile command for {source}", file=sys.stderr)
        return 2
    command, directory = found

    scratch = build_dir / "neg-compile" / fixture_name
    scratch.mkdir(parents=True, exist_ok=True)
    output = scratch / f"{fixture_name}.o"
    depfile = scratch / f"{fixture_name}.{os.getpid()}.d"
    argv = compile_argv(command, output, depfile)
    budget = read_cpu_budget()
    result, is_stored = obtain_result(fixture_name, argv, directory, source, output, budget)
    verdict = evaluate(fixture_name, source, expected_regexes, result)
    sys.stdout.flush()
    over_budget = report_cpu(fixture_name, source, result, is_stored, budget, warnings_dir)
    return verdict or over_budget


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
