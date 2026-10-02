#!/usr/bin/env python3
"""Run one negative-compile fixture, and keep the result of its compile in a store.

The driver reads the compile command of the fixture from compile_commands.json.
It runs that command, and it puts the object file and the dependency file in a
scratch directory of the fixture.  It does not call Ninja.  CTest runs many
fixtures at the same time, and each `cmake --build` call writes to the same
Ninja log and to the same build graph.

The parse of the compile database takes about 20 ms, a fifth of a run that
uses the store.  So the driver keeps the command of the fixture in
`NAME.command` in the scratch directory, with the device, the inode, the size
and the two change times of the database.  It uses that file only while the
five fields are the same.  It writes the file only when the database did not
change in the settle period of the store, so a change in the same clock tick
cannot hide.

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
The store is the "neg" cache of utils/scripts/cache_dir.py, so
CRUCIBLE_CACHE_DIR moves it with every other cache, and the value "off" turns
it off.  When the store holds a
correct entry for the compile, the driver uses the stored exit code and output,
and it does not compile.  Then it does the same checks as after a compile: the
removal of the caret display, the regexes and the independence report.  The
store holds the output of the compiler, and not the decision to pass or fail.

One entry holds the results of at most four compiles of one key, newest
first.  Each result has its own inputs (items 2 to 4 below).  The driver uses
the first result whose inputs are all the same.  So the undo of an edit, or a
change to a different branch and back, finds the result of the earlier
compile.  A new result goes first.  A result with the same inputs as the new
result goes, and the oldest result goes when the entry holds more than four.
The entry keeps each dependency path one time, for all its results.

CRUCIBLE_NEG_CACHE=0 turns the store off.  CRUCIBLE_NEG_CACHE_MAX_MB sets the
size limit of the store, and the preset value is 1024 MB.  The store keeps each
entry in one of 256 buckets, by the first two digits of its key.  Each bucket
gets an equal part of the limit.  When a write makes a bucket larger than its
part, the driver removes the entries of that bucket that it did not use for
the longest time.  So a write reads one bucket and not the full store.  A
damaged entry, an entry that the driver cannot read and an entry in a
different format are not correct entries.

The compile environment
-----------------------
Each compile, and each query of the compiler, runs with LC_MESSAGES=C, and the
value of LC_ALL goes to LC_CTYPE.  GCC then reads no message catalog, and its
diagnostics are in English, the language of the regexes.  Without this, a host
with LANGUAGE=de gets German diagnostics from the catalogs of the compiler, and
each fixture fails.  LC_CTYPE keeps the codeset, which sets the quote
characters of a diagnostic.

Why a stored result is the result of the compile
------------------------------------------------
A compile gives the same exit code and the same output when all its inputs are
the same.  The inputs are the compiler and its environment, the bytes of each
file that the compile reads, and the answer of each lookup that the compile
makes.  The store makes sure of each input:

1. The key is a hash of these items: the compiler identity, the full command,
   the working directory, each environment variable that GCC or the loader
   reads, and the bytes of this file.  The compiler identity is the path and
   the bytes of the compiler driver, of cc1plus and of the assembler, of each
   shared object that the loader maps into them, and of the specs file, and
   the codeset of LC_CTYPE.  The key also holds the bytes of each file that the
   command names, for example a plugin.  When the command contains
   `-march=native`, the key holds the model and the flags of the host CPU.
2. Each result lists each file that GCC read, from the dependency file of the
   compile, with a hash of its bytes.  The driver reads each file again and
   compares the hashes.  After a fatal error, GCC writes no dependency file.
   Then the driver preprocesses the source again with `-M -MG`, which writes
   the dependency rule and continues after a missing header.  That pass reads
   each file that the compile read before its fatal error, and it can read
   more.  It also names each missing header.
3. Each result lists the search roots of the compile, with the kind and the
   real path of each root.  The roots are each directory that GCC names in its
   search list (the output of `-E -v` on an empty file, with each directory
   that GCC ignores), and the directory of each dependency.  For each root
   that is a directory, the result holds a hash of the names under it: the name
   and the kind of each entry, the target of each symbolic link, and the names
   under each directory that a link names.  A root inside a different root
   gets no hash of its own.
4. Each result lists the state of each exact path that an include name with a
   `..` component, or an absolute name, can reach (item 5), and of its
   precompiled name `NAME.gch`.

Why these inputs are sufficient
-------------------------------
5. GCC finds a name N of an `#include`, `#include_next`, `#embed` or
   `__has_include` at the first path D/N in its search sequence that is a
   file.  D is a directory of the search list or the directory of the current
   file.  Item 3 holds the directory of the current file, because the current
   file is a dependency.  The command, the environment and the compiler in the
   key, and the directories that GCC names in the list, set the search list.  When N has no `..`
   component and is not absolute, each path D/N is under the root D, so the
   hash of the names under D sets the answer at D.  The bytes of the file that
   GCC finds are the bytes of a dependency, which item 2 holds.  A name with a
   `..` component can reach a path outside each root.  When GCC finds such a
   name, the dependency file spells the path D/N, and item 4 holds the path
   that N reaches from each root.  When a fatal error stops at such a name, the
   `-M -MG` pass spells it, and item 4 holds its paths.  An absolute name has
   one path, which is a dependency or the missing header of a fatal error.
   A `__has_include` probe that finds no file leaves no trace.  When its name
   has a `..` component or is absolute, the store does not see that probe.  No
   file of the tree has such a probe.
6. The compiler makes three other kinds of lookup, and the key sets each one.
   The driver of GCC finds cc1plus, the assembler and the specs file: the
   store asks the driver for each path (`-print-prog-name`, `-print-file-name`)
   in each run.  The loader finds the shared objects: the loader lists them
   (`--list`), and with LD_DEBUG=libs it names each path that it tries.  A
   memo keeps the list while each tried path has the same state, and each
   object, the cache of the loader and its preload list have the same size
   and times.  GCC reads its message catalogs only for a language other than
   C, and the compile environment sets LC_MESSAGES=C.  The driver computes
   the key again after the compile, and it stores no result when the key
   changed.
7. A change in a root or of a dependency after the compile changes the hash or
   the state in the result.  A change while GCC reads a file can hide.  The
   driver does not store a result when a dependency, a directory under a root,
   a root, a probed path or the target of a symbolic link changed in the second
   before the compile started or after that.  For a path that does not exist,
   the driver reads the time of the nearest directory above it.  A directory
   gets a new change time when a process adds, removes or renames a name in
   it.  A memo keeps the hash of the names under a root, with the inode and the
   two change times of each directory under the root and the target of each
   link.  The memo gives the hash while each one is the same.
   CRUCIBLE_NEG_NOW_NS gives the driver a time earlier than the clock, for the
   tests of the driver.  An earlier time only makes the driver refuse more
   results.

A new file in a root changes the key of each compile that searches that root,
also when no lookup names the new file.  A new header in `include/` then
compiles each fixture again, and a new fixture file compiles each fixture of
its directory again.

The driver stores no result in these conditions:

- The exit code is not 0 or 1.  A signal or an internal error of the compiler
  can give a different output on the next run.
- An input changed in the settle period of item 7, or the key changed during
  the compile (item 6).
- The driver cannot read a root or a probed path.
- A search directory holds a precompiled header (a name that ends in `.gch`),
  or a probed path of item 4 is one.  GCC reads its bytes, and the dependency
  file does not name it.
- A root holds more than 200,000 names.
- The `-M -MG` pass after a fatal error writes no dependency rule.
- The command gives a plugin an output directory (`-fplugin-arg-NAME-out=`).
  The compile writes a file there, and a stored result does not write it.
- The command reads a profile (`-fprofile-use`, `-fauto-profile`).
- The command names a response file (`@FILE`), a `-B` prefix, a plugin by a
  name that is not a path, or a file for `-include` or `-imacros` by a
  relative name.  GCC looks for such a file in the working directory or under
  the prefix, and no root holds those lookups.

The cost of a compile
---------------------
The driver measures the user and the system CPU time of each compile from the
resource use of its child processes.  It runs one compile at a time, so the
CPU time that its waited children gain during the call is the time of the
compiler driver, cc1plus and the assembler.  It also counts the user
instructions of the compile, when the host gives an exact count
(utils/scripts/cost_meter.py, THE INSTRUCTION COUNT).  The CPU budget applies to
the user time.  The finding gives the two times.  The entry of the store keeps
the times and the count, and a result from the store gives the cost of the
compile that made it.

The rows fixture-cpu and fixture-instructions of utils/scripts/budgets.txt give
the thresholds.  A measure above the warning threshold of a row prints a warning
in the format of utils/scripts/check_report.py, and writes it to the warnings
directory, in a file of this fixture (`fixture-cpu.NAME.txt`,
`fixture-instructions.NAME.txt`).  The error level belongs to the instruction
count when the compile has one, because the CPU time rises with the load of the
host and the count does not.  A CPU time above its error threshold then gives a
warning only.  With no count, the error level belongs to the CPU time.  A
measure above the error threshold of the row that holds the error level fails
the test, and the driver does not store that result, so the next run measures
again.  utils/scripts/fixture-cpu-ledger.txt and
utils/scripts/fixture-instructions-ledger.txt name the fixtures above the error
threshold of their row at this time, `fixture | note`.  A listed fixture gives
a warning, and a listed fixture at or below the error threshold fails, so that
its row goes.  On a GitHub runner, each error of the CPU time is a warning that
says that it was demoted (utils/scripts/cost_meter.py, A CI RUNNER), and an
error of the instruction count stays an error.  A budget that cannot be read
stays an error.  CRUCIBLE_NEG_BUDGETS, CRUCIBLE_NEG_CPU_LEDGER and
CRUCIBLE_NEG_INSTRUCTIONS_LEDGER name a different table and ledgers, for the
tests of the driver.

The record of a run
-------------------
After each run, from a compile or from the store, the driver writes
`NAME.inputs` in the scratch directory of the fixture: the result, the CPU
times, the instruction count, and each file that the compile read.  The list is
the dependency file of GCC, the list of the `-M -MG` pass after a fatal error,
or the list of the stored result.  utils/scripts/check-parse-cost.py adds
the bytes of these files for each fixture.

test/neg_compile_driver_test.py holds the tests of the store, of the cost
budget and of the record.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import locale
import os
import re
import resource
import secrets
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import time
import zlib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType

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
    """The exit code, the combined standard output and error, the cost and the inputs of one compile.

    `user_s` and `system_s` are the user and the system CPU time of the
    compile in seconds.  `instructions` is the exact count of its user
    instructions, or None when the host gives no exact count.  `inputs` is
    each file that the compile read, by absolute path, or None when the driver
    does not know them.  A result from the store gives the cost and the inputs
    of the compile that made it.
    """

    returncode: int
    output: str
    user_s: float
    system_s: float
    instructions: int | None
    inputs: tuple[str, ...] | None = None


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


def find_compile_command(build_dir: Path, source: Path, memo: Path) -> tuple[list[str], Path] | None:
    """Return the command and the working directory of `source` in the compile database.

    `memo` keeps the answer with the stat fields of the database, and the
    function uses it while the fields are the same (the module text).  The
    search compares the file names as text first, and it resolves each name
    only when no name is equal.  The cost is O(n) in the rows of the database,
    and O(1) with a correct memo.
    """
    database = build_dir / "compile_commands.json"
    fields = _stat_fields(os.stat(database))
    text = str(source)
    try:
        recorded = json.loads(memo.read_text())
        if (recorded["database"] == fields and recorded["source"] == text
                and all(isinstance(arg, str) for arg in recorded["command"]) and recorded["command"]):
            return list(recorded["command"]), Path(recorded["directory"])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    rows = json.loads(database.read_text())
    match = next((row for row in rows if row["file"] == text), None)
    if match is None:
        match = next((row for row in rows if Path(row["file"]).resolve() == source), None)
    if match is None:
        return None
    command = list(match.get("arguments") or shlex.split(match["command"]))
    directory = Path(match.get("directory", build_dir))
    after = os.stat(database)
    if _stat_fields(after) == fields and after.st_ctime_ns < _now_ns() - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({"database": fields, "source": text, "command": command,
                                            "directory": str(directory)}).encode("ascii"))
        except OSError:
            pass
    return command, directory


def compile_environment(base: Mapping[str, str]) -> dict[str, str]:
    """Return the environment of each compile and of each query of the compiler.

    With LC_MESSAGES=C, GCC reads no message catalog.  LC_ALL takes precedence
    over LC_MESSAGES, so its value goes to LC_CTYPE, which keeps the codeset.
    GCC reads no other locale category.
    """
    env = dict(base)
    every = env.pop("LC_ALL", "")
    if every:
        env["LC_CTYPE"] = every
    env["LC_MESSAGES"] = "C"
    return env


def locale_codeset(env: Mapping[str, str]) -> str:
    """Return the codeset that setlocale(LC_CTYPE, "") gives under `env`.

    GCC uses its quote characters and its text art for UTF-8 only.  A locale
    that the host does not hold leaves GCC in the C locale, and the function
    then gives the codeset of C.
    """
    saved = locale.setlocale(locale.LC_CTYPE)
    try:
        try:
            locale.setlocale(locale.LC_CTYPE, env.get("LC_CTYPE") or env.get("LANG") or "C")
        except locale.Error:
            locale.setlocale(locale.LC_CTYPE, "C")
        return locale.nl_langinfo(locale.CODESET)
    finally:
        locale.setlocale(locale.LC_CTYPE, saved)


def run_compile(argv: list[str], directory: Path, env: Mapping[str, str]) -> CompileResult:
    """Compile with `argv` in `directory` under `env`, and return the exit code, the output and the cost.

    The driver runs one child process at a time, so the change of the resource
    use of its waited children over the call is the use of this compile and of
    each process that it waited for.  The instruction counter counts the
    compile and each process that it starts, and not the driver.
    """
    meter = _cost_meter_module()
    counter = meter.open_instruction_counter()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    try:
        proc = subprocess.run(
            argv,
            cwd=directory,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except BaseException:
        if counter is not None:
            os.close(counter)
        raise
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    instructions = meter.read_instruction_counter(counter) if counter is not None else None
    return CompileResult(proc.returncode, proc.stdout + proc.stderr, after.ru_utime - before.ru_utime,
                         after.ru_stime - before.ru_stime, instructions)


# ── The result store ───────────────────────────────────────────────

_STORE_MAGIC = b"crucible-neg-store 6\n"
# An entry holds at most this number of results of one key (the module text).
_RESULTS_PER_ENTRY = 4
# A change in this period before the compile started can be a change that the
# compiler did not see.  The period is longer than one tick of the clock that
# sets file times.
_SETTLE_NS = 1_000_000_000
_DEFAULT_LIMIT_MB = 1024
# The name of a bucket is the first two hexadecimal digits of a key.  A key is
# a SHA-256 hash, so each bucket gets an equal part of the keys.
_BUCKET_DIGITS = 2
_BUCKETS = 16**_BUCKET_DIGITS
# A bucket removes entries until it uses at most this part of its share of the limit.
_EVICT_TO = 0.9
_DEPFILE_PLACEHOLDER = "<dependency file>"
# The environment variables that GCC reads.  Each one can change the files
# that GCC finds or the text of a diagnostic.  The variables of the loader
# (LD_*) can change the shared objects of the compiler.
_GCC_ENVIRONMENT = frozenset({
    "COLUMNS", "COMPILER_PATH", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH",
    "DEPENDENCIES_OUTPUT", "LANG", "LANGUAGE", "LIBRARY_PATH", "OBJC_INCLUDE_PATH",
    "SOURCE_DATE_EPOCH", "SUNPRO_DEPENDENCIES", "TEMP", "TERM", "TERM_URLS", "TMP", "TMPDIR",
})
_GCC_ENVIRONMENT_PREFIXES = ("GCC_", "LC_", "LD_")
_PLUGIN_OUTPUT = re.compile(r"-fplugin-arg-[^=]*-out=")
_PROFILE_INPUT = ("-fprofile-use", "-fauto-profile")
# The options that name a file which GCC looks for in the working directory
# first, before the search list.
_WORKING_DIRECTORY_INCLUDES = ("-include", "-imacros", "--include", "--imacros")
# The loader of the C library reads this list of objects in each process.
_PRELOAD_LIST = "/etc/ld.so.preload"
# A root with more names than this is not a search directory of a fixture.
_LISTING_LIMIT = 200_000

_driver_digest_value: str | None = None


def _now_ns() -> int:
    """Return the time of the clock, in nanoseconds since the epoch.

    CRUCIBLE_NEG_NOW_NS gives an earlier time, for the tests of the driver.  The
    function then returns the earlier of that time and the clock.  Each use of
    the time compares it with the time of a file, and an earlier time only makes
    the driver refuse more results, write fewer memos and remove fewer temporary
    files.  So the variable cannot make the store take a result that the clock
    makes it refuse.
    """
    now = time.time_ns()
    text = os.environ.get("CRUCIBLE_NEG_NOW_NS", "")
    return min(now, int(text)) if text.isdigit() else now


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
    memo = memo_dir / f"file-{_short_digest(os.fsencode(os.path.realpath(path)))}.json"
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
    if _stat_fields(after) == fields and after.st_ctime_ns < _now_ns() - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({"stat": fields, "digest": digest}).encode("ascii"))
        except OSError:
            pass
    return digest


def _ask_driver(driver: str, option: str, directory: Path, env: Mapping[str, str]) -> str:
    """Return what the compiler driver prints for one `-print-*` option, or an empty text.

    The driver prints the answer of the last such option only, so each
    question is one run.
    """
    proc = subprocess.run([driver, option], cwd=directory, env=env, text=True, capture_output=True, check=False)
    return proc.stdout.strip() if proc.returncode == 0 else ""


def _program_path(driver: str, program: str, directory: Path, env: Mapping[str, str]) -> str | None:
    """Return the path of the program that `driver` runs as `program`, or None."""
    name = _ask_driver(driver, f"-print-prog-name={program}", directory, env)
    # GCC prints the bare name of a program outside its own directories, and it
    # then finds the program through PATH.
    if name and not os.path.isabs(name):
        name = shutil.which(name, path=env.get("PATH")) or ""
    return name if name and os.path.isfile(name) else None


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
    return [name for name in names if name]


def _key_refusal(argv: list[str], directory: Path) -> str:
    """Return the reason that the store cannot key the command `argv`, or an empty text.

    The store refuses a command whose compile writes an output that a stored
    result does not give, or makes a lookup that no search root holds.
    """
    for index, arg in enumerate(argv[1:], start=1):
        if arg.startswith(_PROFILE_INPUT):
            return "the command reads a profile"
        if _PLUGIN_OUTPUT.match(arg):
            return "a plugin of the command writes an output file"
        if arg.startswith("@"):
            return "the command names a response file"
        if arg.startswith("-B"):
            return "the command names a -B prefix"
        if arg.startswith("-fplugin="):
            plugin = arg.split("=", 1)[1]
            # GCC looks for a plugin name with no directory part in its plugin
            # directory or in the search path of the loader.
            if os.sep not in plugin or not os.path.isfile(os.path.join(directory, plugin)):
                return "the command names a plugin by a name that is not a path"
        for option in _WORKING_DIRECTORY_INCLUDES:
            if arg == option:
                name = argv[index + 1] if index + 1 < len(argv) else ""
            elif arg.startswith(option):
                name = arg[len(option) :].removeprefix("=")
            else:
                continue
            if not os.path.isabs(name):
                return f"the command names a file for {option} by a relative name"
    return ""


def compile_key(argv: list[str], directory: Path, protected: set[str], memo_dir: Path,
                env: Mapping[str, str]) -> tuple[str | None, str]:
    """Return the store key of the compile, or None and the reason for no key.

    `argv` ends with `-MF` and the dependency file, and `env` is the compile
    environment.  `protected` holds the resolved paths of the source, the
    object file and the dependency file, which are not inputs of the key.  The
    key holds the inputs of items 1 and 6 of the module text.
    """
    refusal = _key_refusal(argv, directory)
    if refusal:
        return None, refusal
    driver = argv[0] if os.sep in argv[0] else shutil.which(argv[0], path=env.get("PATH"))
    if not driver:
        return None, f"the compiler driver {argv[0]} is not in PATH"
    driver = os.path.join(directory, driver)
    try:
        programs = [driver]
        for program in ("cc1plus", "as"):
            path = _program_path(driver, program, directory, env)
            if path is None:
                return None, f"the compiler driver finds no {program}"
            programs.append(path)
        identity: list[object] = [[path, file_digest(path, memo_dir)] for path in programs]
        specs = _ask_driver(driver, "-print-file-name=specs", directory, env)
        has_specs = os.path.isabs(specs) and os.path.isfile(specs)
        identity.append(["specs", specs, file_digest(specs, memo_dir) if has_specs else None])
        plugins = [os.path.join(directory, arg.split("=", 1)[1]) for arg in argv if arg.startswith("-fplugin=")]
        for path in [*programs, *plugins]:
            # cc1plus loads a plugin, so its loader maps the objects of the plugin.
            interpreter = elf_interpreter(path) or (elf_interpreter(programs[1]) if path in plugins else None)
            if interpreter is None:
                identity.append([path, "static"])
                continue
            objects, reason = shared_objects(path, interpreter, directory, env, memo_dir)
            if objects is None:
                return None, reason
            identity.append([path, interpreter, [[name, file_digest(name, memo_dir)] for name in objects]])
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
        for name, value in env.items()
        if name in _GCC_ENVIRONMENT or name.startswith(_GCC_ENVIRONMENT_PREFIXES)
    )
    is_native = any(arg.startswith("-m") and arg.endswith("=native") for arg in argv)
    material = json.dumps(
        {
            "argv": argv[:-1] + [_DEPFILE_PLACEHOLDER],
            "codeset": locale_codeset(env),
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


# ── The compiler identity: the loader ──────────────────────────────


def elf_interpreter(path: str) -> str | None:
    """Return the program interpreter (PT_INTERP) of the ELF file `path`, or None.

    A static program has no interpreter, and a file that is not ELF has none.
    The function reads the file header and the program headers only.
    """
    with open(path, "rb") as stream:
        head = stream.read(64)
        if len(head) < 64 or head[:4] != b"\x7fELF" or head[4] not in (1, 2):
            return None
        is_64 = head[4] == 2
        order = "<" if head[5] == 1 else ">"
        if is_64:
            (table_offset,) = struct.unpack_from(order + "Q", head, 32)
            entry_size, entry_count = struct.unpack_from(order + "HH", head, 54)
        else:
            (table_offset,) = struct.unpack_from(order + "I", head, 28)
            entry_size, entry_count = struct.unpack_from(order + "HH", head, 42)
        stream.seek(table_offset)
        table = stream.read(entry_size * entry_count)
        for index in range(entry_count):
            entry = table[index * entry_size : (index + 1) * entry_size]
            # p_type 3 is PT_INTERP.  p_offset and p_filesz are at 8 and 32 in
            # a 64-bit header, and at 4 and 16 in a 32-bit header.
            if len(entry) < (56 if is_64 else 32) or struct.unpack_from(order + "I", entry, 0)[0] != 3:
                continue
            word = order + ("Q" if is_64 else "I")
            (offset,) = struct.unpack_from(word, entry, 8 if is_64 else 4)
            (size,) = struct.unpack_from(word, entry, 32 if is_64 else 16)
            stream.seek(offset)
            return os.fsdecode(stream.read(size).rstrip(b"\0"))
    return None


def parse_loader_list(stdout: str, stderr: str, directory: Path) -> tuple[list[str], list[str], list[str]] | None:
    """Return the objects, the tried paths and the caches of one `--list` run of the loader with LD_DEBUG=libs.

    stdout names one object on each line: `name => path (address)`, `path
    (address)`, or an object of the kernel such as `linux-vdso.so.1
    (address)`, which is not a file.  stderr names each path that the loader
    tried (`trying file=PATH`) and each cache that it read (`search
    cache=PATH`).  An empty entry of LD_LIBRARY_PATH gives a tried path
    relative to the working directory.  The function returns None when the
    loader finds no object for a name (`=> not found`).
    """
    objects: list[str] = []
    for line in stdout.splitlines():
        text = line.strip()
        if "=>" in text:
            text = text.split("=>", 1)[1].strip()
            if text.startswith("not found"):
                return None
        if text.startswith("/"):
            objects.append(text.rsplit(" (", 1)[0])
    tried: list[str] = []
    caches: list[str] = []
    for line in stderr.splitlines():
        _, is_try, rest = line.partition("trying file=")
        if is_try:
            tried.append(os.path.join(directory, rest))
            continue
        _, is_cache, rest = line.partition("search cache=")
        if is_cache:
            caches.append(rest.strip())
    return list(dict.fromkeys(objects)), list(dict.fromkeys(tried)), list(dict.fromkeys(caches))


def _exists(path: str) -> bool:
    """Return True when a file has the name `path` after each symbolic link."""
    try:
        os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return False
    return True


def _entry_ctime(path: str) -> int:
    """Return the change time of the file that `path` names, or of the nearest directory above it when none exists.

    A new file at `path` gives that directory a new change time, so a time
    older than a moment shows that the state of `path` did not change after
    that moment.
    """
    current = os.path.realpath(path)
    while True:
        try:
            return os.stat(current).st_ctime_ns
        except OSError:
            parent = os.path.dirname(current)
            if parent == current:
                return 0
            current = parent


def shared_objects(program: str, interpreter: str, directory: Path, env: Mapping[str, str],
                   memo_dir: Path) -> tuple[list[str] | None, str]:
    """Return the shared objects that the loader maps for `program`, or None and the reason.

    The loader `interpreter` lists them as it maps them in a run, and it names
    each path that it tries.  A memo keeps the list.  The memo gives the list
    while each tried path has the same state, and each object, each cache of
    the loader and the preload list have the same device, inode, size and
    times.  The function writes no memo when a file that exists changed in the
    settle period, because the run of the loader can then miss it.  A tried
    path that does not exist needs no such check: when a file appears there,
    the memo check fails.  The driver computes the key again after the
    compile, so a change between this run and the compile stores no result.
    """
    status = os.stat(program)
    loader_variables = sorted([name, value] for name, value in env.items() if name.startswith("LD_"))
    material = json.dumps([program, interpreter, _stat_fields(status), str(directory), loader_variables])
    memo = memo_dir / f"loader-{_short_digest(material.encode('ascii'))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if all(_stat_fields(os.stat(path)) == fields for path, fields in recorded["files"]) and all(
            _exists(path) == exists for path, exists in recorded["tried"]
        ):
            return list(recorded["objects"]), ""
    except (OSError, ValueError, KeyError, TypeError):
        pass
    loader_env = {name: value for name, value in env.items() if name != "LD_DEBUG_OUTPUT"}
    loader_env["LD_DEBUG"] = "libs"
    started_ns = _now_ns()
    proc = subprocess.run([interpreter, "--list", program], cwd=directory, env=loader_env, capture_output=True,
                          text=True, errors="surrogateescape", check=False)
    parsed = parse_loader_list(proc.stdout, proc.stderr, directory) if proc.returncode == 0 else None
    if parsed is None:
        return None, f"the loader {interpreter} cannot list the shared objects of {program}"
    objects, tried, caches = parsed
    files = list(dict.fromkeys([*objects, *caches, *([_PRELOAD_LIST] if _exists(_PRELOAD_LIST) else [])]))
    file_rows = [(path, _stat_fields(os.stat(path))) for path in files]
    tried_rows = [(path, _exists(path)) for path in [*tried, _PRELOAD_LIST]]
    settled_ns = started_ns - _SETTLE_NS
    is_settled = all(fields[4] < settled_ns for _, fields in file_rows) and all(
        os.stat(path).st_ctime_ns < settled_ns for path, exists in tried_rows if exists
    )
    if is_settled:
        payload = json.dumps({"objects": objects, "files": file_rows, "tried": tried_rows})
        try:
            _write_atomic(memo, payload.encode("ascii"))
        except OSError:
            pass
    return objects, ""


# ── The search roots ───────────────────────────────────────────────


def _command_without_output(argv: list[str]) -> list[str]:
    """Return `argv` without `-c`, the object file and the dependency options.

    The search query and the dependency pass give their own mode and output.
    """
    result: list[str] = []
    skip_next = False
    for arg in _strip_dependency_flags(argv):
        if skip_next:
            skip_next = False
            continue
        if arg == "-o":
            skip_next = True
            continue
        if arg == "-c" or (arg.startswith("-o") and len(arg) > 2):
            continue
        result.append(arg)
    return result


def parse_search_list(text: str) -> list[str] | None:
    """Return each directory that the output of `-E -v` names, or None when the output has no full search list.

    GCC prints each directory of the quote list and of the bracket list on a
    line that starts with a space, between `search starts here:` and `End of
    search list.`.  Before the lists, it prints in quotes each directory that
    it ignores: one that does not exist, and a duplicate of an earlier one.
    These texts are English, which the compile environment gives.
    """
    named: list[str] = []
    is_inside = False
    for line in text.splitlines():
        if line.startswith("ignoring ") and line.count('"') >= 2:
            named.append(line[line.index('"') + 1 : line.rindex('"')])
        elif line.startswith("#include ") and line.endswith("search starts here:"):
            is_inside = True
        elif line == "End of search list.":
            return named
        elif is_inside and line.startswith(" "):
            named.append(line[1:].removesuffix(" (framework directory)"))
    return None


def search_directories(argv: list[str], directory: Path, source: Path, scratch: Path,
                       env: Mapping[str, str]) -> tuple[list[str] | None, str]:
    """Return each directory that GCC names in the search list of the compile, or None and the reason.

    The query preprocesses an empty file with the suffix of the source, with
    the options of the compile, so GCC gives the same search list.
    """
    empty = scratch / f"search-query{source.suffix}"
    command = [arg for arg in _command_without_output(argv)
               if arg.startswith("-") or os.path.realpath(os.path.join(directory, arg)) != str(source)]
    try:
        empty.write_bytes(b"")
        proc = subprocess.run([*command, "-E", "-v", str(empty), "-o", os.devnull], cwd=directory, env=env,
                              capture_output=True, text=True, errors="surrogateescape", check=False)
    except OSError as error:
        return None, f"the search query cannot run: {error}"
    finally:
        empty.unlink(missing_ok=True)
    named = parse_search_list(proc.stderr)
    if named is None:
        return None, f"the compiler prints no search list (exit code {proc.returncode})"
    return [os.path.join(directory, name) for name in named], ""


def dependency_pass(argv: list[str], directory: Path, scratch: Path,
                    env: Mapping[str, str]) -> tuple[list[str] | None, list[str], str]:
    """Return the dependencies and the missing headers of the source, from a preprocess with `-M -MG`.

    The function returns None and the reason when the preprocess writes no
    dependency rule.  A name in the rule that does not name a file is a header
    that the preprocess did not find, as the source spells it.
    """
    depfile = scratch / "dependency-pass.d"
    try:
        proc = subprocess.run([*_command_without_output(argv), "-M", "-MG", "-MF", str(depfile)], cwd=directory,
                              env=env, capture_output=True, text=True, errors="surrogateescape", check=False)
        parsed = parse_dependency_file(depfile.read_text(errors="surrogateescape")) if depfile.exists() else None
    except OSError as error:
        return None, [], f"the dependency pass cannot run: {error}"
    finally:
        depfile.unlink(missing_ok=True)
    if parsed is None:
        return None, [], f"the preprocess with -M -MG writes no dependency rule (exit code {proc.returncode})"
    present: list[str] = []
    missing: list[str] = []
    for name in dict.fromkeys(parsed):
        full = os.path.join(directory, name)
        if os.path.isfile(full):
            present.append(full)
        else:
            missing.append(name)
    return present, missing, ""


def root_state(path: str) -> list[str] | None:
    """Return the state of a search root or of a probed path.

    The state is None when no file has the name, else the kind of the file
    (_kind) and its real path.  A path that the driver cannot read has the
    state of an error.
    """
    try:
        status = os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return None
    except OSError as error:
        return [f"error {error.errno}", path]
    return [_kind(status), os.path.realpath(path)]


def search_roots(named: list[str], dependencies: list[str], missing: list[str]) -> tuple[list[str], list[str]]:
    """Return the search roots and the probed paths of a compile (items 3 and 4 of the module text).

    The roots are the named directories and the directory of each dependency.
    A dependency path whose part after a root has a `..` component was found
    by a name with that component.  The probes are the path that such a name
    reaches from each root, the path of each missing header with an absolute
    name or a `..` component, and the precompiled name of each one.  The cost
    is O(r * (d + m)) for r roots, d dependencies and m missing headers.
    """
    roots = list(dict.fromkeys([*named, *(os.path.dirname(path) for path in dependencies)]))
    names: list[str] = []
    for path in dependencies:
        if ".." not in path.split(os.sep):
            continue
        for root in roots:
            prefix = root.rstrip(os.sep) + os.sep
            if path.startswith(prefix) and ".." in path[len(prefix) :].split(os.sep):
                names.append(path[len(prefix) :])
    names.extend(name for name in missing if not os.path.isabs(name) and ".." in name.split(os.sep))
    reached = [os.path.join(root, name) for name in dict.fromkeys(names) for root in roots]
    reached.extend(name for name in missing if os.path.isabs(name))
    probes = [path for reach in reached for path in (reach, reach + ".gch")]
    return roots, list(dict.fromkeys(probes))


def listing_roots(states: Mapping[str, list[str] | None]) -> list[str]:
    """Return the real path of each directory root that no other directory root holds.

    The hash of the names under a root covers each directory inside it.  The
    cost is O(r^2) for r roots.
    """
    reals = sorted({state[1] for state in states.values() if state is not None and state[0] == "dir"})
    kept: list[str] = []
    for real in reals:
        if not any(real == outer or real.startswith(outer.rstrip(os.sep) + os.sep) for outer in kept):
            kept.append(real)
    return kept


@dataclass(frozen=True, slots=True)
class Listing:
    """The hash of the names under one root, and what the store reads from them.

    `newest_ctime_ns` is the newest change time of a directory under the root
    or of the target of a link.  `precompiled` names the first precompiled
    header under the root, or is empty.  `entries` counts the names.
    """

    digest: str
    newest_ctime_ns: int
    precompiled: str
    entries: int


def _walk(root: str) -> tuple[Listing, list[list[object]], list[list[object]]] | None:
    """Walk the tree under the real directory `root`, and return the hash of its names, its directories and its links.

    Each directory is a row (path, inode, mtime, ctime), and each symbolic link
    is a row (path, target, kind of the file that it names, real path of a
    named directory).  The walk follows each link to a directory, and it walks
    each real directory one time.  The function returns None for a tree of more
    than _LISTING_LIMIT names.  The cost is O(n log n) for n names, because
    the names of each directory are sorted.
    """
    hasher = hashlib.sha256()
    directories: list[list[object]] = []
    links: list[list[object]] = []
    seen: set[str] = set()
    pending = [root]
    entries = 0
    newest = 0
    precompiled = ""
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        status = os.stat(current)
        directories.append([current, status.st_ino, status.st_mtime_ns, status.st_ctime_ns])
        newest = max(newest, status.st_ctime_ns)
        with os.scandir(current) as iterator:
            items = sorted(iterator, key=lambda item: item.name)
        hasher.update(b"D" + os.fsencode(current) + b"\0")
        for item in items:
            entries += 1
            name = os.fsencode(item.name)
            if item.name.endswith(".gch") and not precompiled:
                precompiled = item.path
            if item.is_symlink():
                target = os.readlink(item.path)
                try:
                    kind: str | None = _kind(os.stat(item.path))
                except (FileNotFoundError, NotADirectoryError):
                    kind = None
                real = os.path.realpath(item.path) if kind == "dir" else None
                links.append([item.path, target, kind, real])
                newest = max(newest, _entry_ctime(item.path))
                hasher.update(b"l" + name + b"\0" + os.fsencode(target) + b"\0" + str(kind).encode() + b"\0")
                if real is not None:
                    pending.append(real)
            elif item.is_dir(follow_symlinks=False):
                hasher.update(b"d" + name + b"\0")
                pending.append(item.path)
            else:
                hasher.update((b"f" if item.is_file(follow_symlinks=False) else b"o") + name + b"\0")
        if entries > _LISTING_LIMIT:
            return None
    return Listing(hasher.hexdigest()[:32], newest, precompiled, entries), directories, links


def _stamps_agree(directories: list[list[object]]) -> bool:
    """Return True when each directory has the same inode and the same two change times."""
    for path, inode, mtime_ns, ctime_ns in directories:
        try:
            status = os.stat(str(path))
        except OSError:
            return False
        if (status.st_ino, status.st_mtime_ns, status.st_ctime_ns) != (inode, mtime_ns, ctime_ns):
            return False
    return True


def _links_agree(links: list[list[object]]) -> bool:
    """Return True when each symbolic link has the same target, the same kind of named file and the same real path."""
    for path, target, kind, real in links:
        try:
            if os.readlink(str(path)) != target:
                return False
            now_kind: str | None = _kind(os.stat(str(path)))
        except (FileNotFoundError, NotADirectoryError):
            now_kind = None
        except OSError:
            return False
        if now_kind != kind or (real is not None and os.path.realpath(str(path)) != real):
            return False
    return True


def tree_listing(root: str, memo_dir: Path) -> tuple[Listing | None, str]:
    """Return the hash of the names under the real directory `root`, or None and the reason.

    A memo keeps the hash with the stamp of each directory and each link
    (item 7 of the module text).  The function writes the memo only when no
    directory changed in the settle period before the walk.  A check of a
    memo costs one stat for each directory and two for each link.  A walk of
    /usr/include, with 1,600 directories, costs about 40 ms, and the check of
    its memo about 5 ms.
    """
    memo = memo_dir / f"listing-{_short_digest(os.fsencode(root))}.json"
    try:
        recorded = json.loads(memo.read_text())
        if recorded["root"] == root and _stamps_agree(recorded["directories"]) and _links_agree(recorded["links"]):
            return Listing(*recorded["listing"]), ""
    except (OSError, ValueError, KeyError, TypeError):
        pass
    started_ns = _now_ns()
    try:
        walked = _walk(root)
    except OSError as error:
        return None, f"the search directory {root} cannot be read: {error}"
    if walked is None:
        return None, f"the search directory {root} holds more than {_LISTING_LIMIT} names"
    listing, directories, links = walked
    if not (_stamps_agree(directories) and _links_agree(links)):
        return None, f"the search directory {root} changed during its walk"
    if listing.newest_ctime_ns < started_ns - _SETTLE_NS:
        try:
            _write_atomic(memo, json.dumps({
                "root": root,
                "listing": [listing.digest, listing.newest_ctime_ns, listing.precompiled, listing.entries],
                "directories": directories,
                "links": links,
            }).encode("ascii"))
        except OSError:
            pass
    return listing, ""


def _kind(status: os.stat_result) -> str:
    """Return the kind of a file: "file" for a regular file, "dir" for a directory, or "other"."""
    if stat.S_ISREG(status.st_mode):
        return "file"
    return "dir" if stat.S_ISDIR(status.st_mode) else "other"


def encode_entry(entry: dict[str, object]) -> bytes:
    """Return the bytes of a store entry: a format line, a checksum and the packed entry."""
    payload = zlib.compress(json.dumps(entry, separators=(",", ":")).encode("ascii"), 6)
    return _STORE_MAGIC + hashlib.sha256(payload).hexdigest().encode("ascii") + b"\n" + payload


def _is_text_pairs(value: object) -> bool:
    """Return True when `value` is a list of pairs of texts."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and all(isinstance(part, str) for part in item) for item in value
    )


def _is_states(value: object) -> bool:
    """Return True when `value` is a list of pairs of a path and a `root_state` result."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and isinstance(item[0], str)
        and (item[1] is None or _is_text_pairs([item[1]]))
        for item in value
    )


def _is_index_pairs(value: object, count: int) -> bool:
    """Return True when `value` is a list of pairs of an index below `count` and a text."""
    return isinstance(value, list) and all(
        isinstance(item, list) and len(item) == 2 and isinstance(item[0], int) and not isinstance(item[0], bool)
        and 0 <= item[0] < count and isinstance(item[1], str)
        for item in value
    )


def _is_result(value: object, path_count: int) -> bool:
    """Return True when `value` is a correct result of an entry with `path_count` dependency paths."""
    if not isinstance(value, dict):
        return False
    returncode = value.get("returncode")
    times = (value.get("user_s"), value.get("system_s"))
    instructions = value.get("instructions", False)
    return (
        _is_index_pairs(value.get("dependencies"), path_count)
        and _is_states(value.get("roots"))
        and _is_states(value.get("probes"))
        and _is_text_pairs(value.get("listings"))
        and isinstance(returncode, int)
        and not isinstance(returncode, bool)
        and isinstance(value.get("output"), str)
        and all(
            isinstance(time_s, (int, float)) and not isinstance(time_s, bool) and 0 <= time_s < float("inf")
            for time_s in times
        )
        and (instructions is None or (isinstance(instructions, int) and not isinstance(instructions, bool)
                                      and instructions >= 0))
    )


def decode_entry(blob: bytes) -> dict[str, object] | None:
    """Return the entry in `blob`, or None when the bytes are not a correct entry.

    An entry holds `paths`, the dependency paths of all its results, and
    `results`, from one result to _RESULTS_PER_ENTRY results, newest first.  A
    dependency of a result is a pair of an index in `paths` and a hash.
    """
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
    paths = entry.get("paths")
    results = entry.get("results")
    is_well_formed = (
        isinstance(paths, list)
        and all(isinstance(name, str) for name in paths)
        and isinstance(results, list)
        and 1 <= len(results) <= _RESULTS_PER_ENTRY
        and all(_is_result(result, len(paths)) for result in results)
    )
    return entry if is_well_formed else None


def pack_entry(results: list[dict[str, object]]) -> dict[str, object]:
    """Return the entry of `results`, whose dependencies are pairs of a path and a hash.

    The entry keeps each path one time, in `paths`, and each result names a
    path by its index there.  The cost is O(r * d) for r results of d
    dependencies.
    """
    indices: dict[str, int] = {}
    packed: list[dict[str, object]] = []
    for result in results:
        dependencies = [[indices.setdefault(name, len(indices)), digest]
                        for name, digest in result["dependencies"]]  # type: ignore[attr-defined]
        packed.append({**result, "dependencies": dependencies})
    return {"paths": list(indices), "results": packed}


def unpack_results(entry: dict[str, object]) -> list[dict[str, object]]:
    """Return the results of a correct entry, with each dependency as a pair of a path and a hash."""
    paths: list[str] = entry["paths"]  # type: ignore[assignment]
    return [{**result, "dependencies": [[paths[index], digest] for index, digest in result["dependencies"]]}
            for result in entry["results"]]  # type: ignore[attr-defined]


def _result_inputs(result: Mapping[str, object]) -> tuple[object, ...]:
    """Return the inputs of a result (items 2 to 4 of the module text), for a comparison."""
    return result["dependencies"], result["roots"], result["probes"], result["listings"]


def merge_results(new_result: dict[str, object], earlier: list[dict[str, object]]) -> list[dict[str, object]]:
    """Return the results of an entry after the write of `new_result` into an entry with the results `earlier`.

    The new result goes first.  Each earlier result with the same inputs goes,
    and the list keeps at most _RESULTS_PER_ENTRY results, newest first.
    """
    inputs = _result_inputs(new_result)
    kept = [old for old in earlier if _result_inputs(old) != inputs]
    return [new_result, *kept][:_RESULTS_PER_ENTRY]


class InputCheck:
    """The check of the inputs of the results of one entry.

    The check finds the hash of each dependency, the state of each root and
    probed path, and the listing of each root one time, and it compares each
    result with these answers.  The answers come from the same moment, so a
    result matches when its inputs are the inputs at that moment.  The cost is
    O(n + d + r * k): n bytes of dependencies, d directories under the roots,
    and k inputs in each of r results.
    """

    def __init__(self, memo_dir: Path) -> None:
        """Make a check that keeps the memos of the root listings in `memo_dir`."""
        self.memo_dir = memo_dir
        self.digests: dict[str, str | None] = {}
        self.states: dict[str, list[str] | None] = {}
        self.listings: dict[str, tuple[Listing | None, str]] = {}

    def digest(self, name: str) -> str | None:
        """Return the hash of the bytes of the file `name`, or None when the file cannot be read."""
        if name not in self.digests:
            try:
                self.digests[name] = _short_digest(Path(name).read_bytes())
            except OSError:
                self.digests[name] = None
        return self.digests[name]

    def state(self, name: str) -> list[str] | None:
        """Return the `root_state` of the path `name`."""
        if name not in self.states:
            self.states[name] = root_state(name)
        return self.states[name]

    def listing(self, root: str) -> tuple[Listing | None, str]:
        """Return the `tree_listing` of the root `root`."""
        if root not in self.listings:
            self.listings[root] = tree_listing(root, self.memo_dir)
        return self.listings[root]

    def reason(self, result: Mapping[str, object]) -> str:
        """Return the reason that `result` does not match, or an empty text when each input is the same.

        The dependencies come first.  An edit changes a dependency more
        frequently than a new file changes a root, so an edit stops the check
        before the walk of a root.
        """
        for name, digest in result["dependencies"]:  # type: ignore[attr-defined]
            now = self.digest(name)
            if now is None:
                return f"a dependency cannot be read: {name}"
            if now != digest:
                return f"a dependency changed: {name}"
        for name, state in result["roots"]:  # type: ignore[attr-defined]
            if self.state(name) != state:
                return f"a search root changed: {name}"
        for name, state in result["probes"]:  # type: ignore[attr-defined]
            if self.state(name) != state:
                return f"a probed path changed: {name}"
        for root, digest in result["listings"]:  # type: ignore[attr-defined]
            listing, why = self.listing(root)
            if listing is None or listing.digest != digest:
                return why or f"a search directory changed: {root}"
        return ""


class ResultStore:
    """The store of compile results in one directory.

    `entries/` holds one file for each key, and `memo/` holds the memos of
    `file_digest`, `shared_objects` and `tree_listing`.  Many fixtures read and
    write the store at the same time.  Each write goes through a temporary file
    and a rename, and a reader that loses a file to a removal counts a miss.
    """

    def __init__(self, root: Path, limit_bytes: int) -> None:
        """Use the store in `root`, with a size limit of `limit_bytes`."""
        self.root = root
        self.entries = root / "entries"
        self.memo = root / "memo"
        self.limit_bytes = limit_bytes

    def entry_path(self, key: str) -> Path:
        """Return the path of the entry for `key`, in the bucket of the key."""
        return self.entries / key[:_BUCKET_DIGITS] / key

    def lookup(self, key: str) -> tuple[CompileResult | None, str]:
        """Return the stored result for `key`, or None and the reason for a miss.

        The function returns the first result of the entry whose inputs are
        the same (InputCheck): each dependency has the same bytes, each search
        root and each probed path has the same state, and the names under each
        root are the same.  The reason for a miss is the reason of the newest
        result.  The cost is the cost of one InputCheck.
        """
        path = self.entry_path(key)
        try:
            blob = path.read_bytes()
        except OSError:
            return None, "no entry"
        entry = decode_entry(blob)
        if entry is None:
            return None, "the entry is damaged"
        check = InputCheck(self.memo)
        newest_reason = ""
        for result in unpack_results(entry):
            reason = check.reason(result)
            if reason:
                newest_reason = newest_reason or reason
                continue
            try:
                os.utime(path)
            except OSError:
                pass
            inputs = tuple(name for name, _ in result["dependencies"])  # type: ignore[attr-defined]
            return CompileResult(result["returncode"], result["output"],  # type: ignore[arg-type]
                                 float(result["user_s"]), float(result["system_s"]),  # type: ignore[arg-type]
                                 result["instructions"], inputs), ""  # type: ignore[arg-type]
        return None, newest_reason

    def record(
        self,
        key: str,
        result: CompileResult,
        dependencies: list[str],
        missing: list[str],
        named: list[str],
        started_ns: int,
    ) -> tuple[bool, str]:
        """Store `result` for `key`, and return True, or False and the reason.

        `dependencies` holds each file that GCC read, `missing` each header that
        a fatal error did not find, and `named` each directory of the search
        list.  The function refuses a result whose inputs changed in the settle
        period before `started_ns`, and a result that a precompiled header or a
        root that it cannot read can change.  The new result goes first in the
        entry.  The entry drops each earlier result with the same inputs, and
        keeps at most _RESULTS_PER_ENTRY results.  Two writers of one entry at
        the same time can lose one result, which costs one compile later.  The
        cost is O(n + d) for n bytes of dependencies and d directories under the
        roots, plus a walk of each root that has no memo.
        """
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
        roots, probes = search_roots(named, dependencies, missing)
        states = {path: root_state(path) for path in roots}
        probe_states = {path: root_state(path) for path in probes}
        for path, state in [*states.items(), *probe_states.items()]:
            if state is not None and state[0].startswith("error"):
                return False, f"a search root cannot be read: {path}"
            if state is not None and path in probe_states and path.endswith(".gch"):
                return False, f"a precompiled header is on a probed path: {path}"
        listed = [(root, *tree_listing(root, self.memo)) for root in listing_roots(states)]
        for root, listing, reason in listed:
            if listing is None:
                return False, reason
            if listing.precompiled:
                return False, f"a precompiled header is in a search directory: {listing.precompiled}"
        for root, listing, _ in listed:
            if listing is not None and listing.newest_ctime_ns >= settled_ns:
                return False, f"a search directory changed less than one second before the compile: {root}"
        changed = next((path for path in [*states, *probe_states] if _entry_ctime(path) >= settled_ns), None)
        if changed is not None:
            return False, f"a search root changed less than one second before the compile: {changed}"
        listings = [[root, listing.digest] for root, listing, _ in listed if listing is not None]
        new_result: dict[str, object] = {
            "dependencies": recorded,
            "roots": [[path, state] for path, state in states.items()],
            "probes": [[path, state] for path, state in probe_states.items()],
            "listings": listings,
            "output": result.output,
            "returncode": result.returncode,
            "user_s": round(result.user_s, 3),
            "system_s": round(result.system_s, 3),
            "instructions": result.instructions,
        }
        path = self.entry_path(key)
        try:
            earlier = decode_entry(path.read_bytes())
        except OSError:
            earlier = None
        results = merge_results(new_result, unpack_results(earlier) if earlier is not None else [])
        try:
            _write_atomic(path, encode_entry(pack_entry(results)))
        except OSError as error:
            return False, f"the entry cannot be written: {error}"
        self.evict(path)
        return True, ""

    def evict(self, written: Path) -> None:
        """Remove the entries of the bucket of `written` that the store did not use for the longest time.

        Each bucket gets an equal share of the limit.  When the bucket uses more
        than its share, the function removes entries until the bucket uses at
        most 90 percent of its share.  It keeps `written`, the entry that the
        caller wrote, also when that entry alone is larger than the share.  It
        also removes each temporary file of the bucket older than one hour, the
        remains of a writer that stopped.  The cost is O(m log m) in the m
        entries of one bucket, which hold about 1/256 of the store.
        """
        files: list[tuple[int, int, str]] = []
        expired_ns = _now_ns() - 3600 * 1_000_000_000
        try:
            with os.scandir(written.parent) as iterator:
                names = list(iterator)
        except OSError:
            return
        for item in names:
            try:
                status = item.stat(follow_symlinks=False)
            except OSError:
                continue
            if item.name.endswith(".tmp"):
                if status.st_mtime_ns < expired_ns:
                    Path(item.path).unlink(missing_ok=True)
                continue
            if item.name != written.name:
                files.append((status.st_mtime_ns, status.st_size, item.path))
        share = self.limit_bytes // _BUCKETS
        try:
            total = sum(size for _, size, _ in files) + written.stat().st_size
        except OSError:
            return
        if total <= share:
            return
        goal = int(share * _EVICT_TO)
        for _, size, name in sorted(files):
            if total <= goal:
                break
            Path(name).unlink(missing_ok=True)
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
    if str(_SCRIPTS) not in sys.path:
        sys.path.insert(0, str(_SCRIPTS))
    import cache_dir

    try:
        root = cache_dir.cache_root("neg")
        if root is None:
            return None, f"{cache_dir.ROOT_VARIABLE} turns every cache off"
        (root / "entries").mkdir(parents=True, exist_ok=True)
        (root / "memo").mkdir(parents=True, exist_ok=True)
    except OSError as error:
        return None, f"the store directory cannot be made: {error}"
    return ResultStore(root, _store_limit_bytes()), ""


def _note(fixture_name: str, text: str) -> None:
    """Print one line about the store for the fixture."""
    print(f"neg-compile {fixture_name}: {text}", file=sys.stderr)


# ── The cost budget of a compile ───────────────────────────────────

_REPO_ROOT = Path(__file__).resolve().parents[1]
_SCRIPTS = _REPO_ROOT / "utils" / "scripts"
_CPU_CHECK = "fixture-cpu"
_INSTRUCTIONS_CHECK = "fixture-instructions"
# The variable that names a different ledger for each check, and the ledger of the tree.
_LEDGERS = {
    _CPU_CHECK: ("CRUCIBLE_NEG_CPU_LEDGER", _SCRIPTS / "fixture-cpu-ledger.txt"),
    _INSTRUCTIONS_CHECK: ("CRUCIBLE_NEG_INSTRUCTIONS_LEDGER", _SCRIPTS / "fixture-instructions-ledger.txt"),
}
_GIGA = 1e9
_ADVICE = "Include only the header that the fixture attacks, or make the tables that it does not read lazy."


@dataclass(frozen=True, slots=True)
class BudgetRow:
    """The thresholds of one budget row of a fixture compile, and the rows of its ledger (`fixture | note`)."""

    check: str
    warn: float
    error: float
    ledger: dict[str, str]
    ledger_name: str


@dataclass(frozen=True, slots=True)
class CostBudget:
    """The rows fixture-cpu and fixture-instructions, with their ledgers.

    `problems` gives, for each check whose row or ledger cannot be read, the
    reason.  A budget with a problem fails the test.
    """

    rows: dict[str, BudgetRow]
    problems: dict[str, str]

    def is_over_error(self, fixture_name: str, result: CompileResult) -> bool:
        """Return True when the measure of the row that holds the error level fails the test, with no ledger row.

        The instruction count holds the error level when the compile has one,
        and the CPU time holds it when the compile has no count.
        """
        if self.problems:
            return False
        if result.instructions is not None:
            row = self.rows[_INSTRUCTIONS_CHECK]
            return result.instructions / _GIGA > row.error and fixture_name not in row.ledger
        row = self.rows[_CPU_CHECK]
        return result.user_s > row.error and fixture_name not in row.ledger

    @staticmethod
    def excess_text(result: CompileResult) -> str:
        """Return the words that tell which measure of a compile holds the error level, for a note."""
        return "ran more user instructions" if result.instructions is not None else "took more CPU time"


def _report_module() -> ModuleType:
    """Return the module utils/scripts/check_report.py."""
    if str(_SCRIPTS) not in sys.path:
        sys.path.insert(0, str(_SCRIPTS))
    import check_report

    return check_report


def _cost_meter_module() -> ModuleType:
    """Return the module utils/scripts/cost_meter.py."""
    _report_module()
    import cost_meter

    return cost_meter


def _read_fixture_ledger(path: Path) -> tuple[dict[str, str], str]:
    """Read one ledger of fixtures, `fixture | note`, and return its rows, or no rows and the reason."""
    ledger: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            continue
        cells = [cell.strip() for cell in text.split("|")]
        if len(cells) != 2 or not all(cells) or cells[0] in ledger:
            return {}, (f"{path}:{number}: a row is `fixture | note`, with two cells that are not empty, and one row "
                        f"for each fixture")
        ledger[cells[0]] = cells[1]
    return ledger, ""


def read_cost_budget() -> CostBudget:
    """Read the rows fixture-cpu and fixture-instructions of the budget table, and their ledgers.

    CRUCIBLE_NEG_BUDGETS, CRUCIBLE_NEG_CPU_LEDGER and
    CRUCIBLE_NEG_INSTRUCTIONS_LEDGER name a different table and ledgers.
    """
    table = Path(os.environ.get("CRUCIBLE_NEG_BUDGETS") or _SCRIPTS / "budgets.txt")
    rows: dict[str, BudgetRow] = {}
    problems: dict[str, str] = {}
    try:
        budgets = _report_module().read_budgets(table)
    except (OSError, ValueError) as error:
        return CostBudget({}, {check: f"the budget of {check} cannot be read: {error}" for check in _LEDGERS})
    for check, (variable, default) in _LEDGERS.items():
        row = budgets.get(check)
        if row is None:
            problems[check] = f"the budget table {table} has no row {check}"
            continue
        path = Path(os.environ.get(variable) or default)
        try:
            ledger, problem = _read_fixture_ledger(path)
        except OSError as error:
            ledger, problem = {}, f"the ledger of {check} cannot be read: {error}"
        if problem:
            problems[check] = problem
            continue
        rows[check] = BudgetRow(check, row.warn, row.error, ledger, path.name)
    return CostBudget(rows, problems)


def _judge_row(report: ModuleType, row: BudgetRow, fixture_name: str, place: str, value: float, text: str,
               unit: str, holds_error: bool) -> list[object]:
    """Judge one measure of a fixture compile against one budget row and its ledger.

    `holds_error` is False for the CPU time of a compile that has an
    instruction count, which then gives a warning in place of an error.
    """
    if fixture_name in row.ledger and value <= row.error:
        return [report.judged("error", place, 0, row.check,
                              f"{text}, no more than the error threshold {row.error:g} {unit}.  Remove its row from "
                              f"utils/scripts/{row.ledger_name}.")]
    if fixture_name in row.ledger:
        return [report.judged("warning", place, 0, row.check,
                              f"{text}, more than the error threshold {row.error:g} {unit}.  It has a row in the "
                              f"ledger: {row.ledger[fixture_name]}")]
    if value > row.error and not holds_error:
        return [report.Finding("warning", place, 0, row.check,
                               f"{text}, more than the error threshold {row.error:g} {unit}.  The compile has an "
                               f"instruction count, so the row {_INSTRUCTIONS_CHECK} holds the error level, and the "
                               f"CPU time, which rises with the load of the host, gives a warning only.")]
    if value > row.error:
        return [report.judged("error", place, 0, row.check,
                              f"{text}, more than the error threshold {row.error:g} {unit}.  {_ADVICE}")]
    if value > row.warn:
        return [report.judged("warning", place, 0, row.check,
                              f"{text}, more than the warning threshold {row.warn:g} {unit}.")]
    return []


def report_cost(fixture_name: str, source: Path, result: CompileResult, is_stored: bool, budget: CostBudget,
                warnings_dir: Path | None) -> int:
    """Report the CPU time and the instruction count of the compile against the budget, and return 1 when it fails.

    The warning lines of each check go to the file of this fixture in the
    warnings directory, so the many fixtures that run at the same time do not
    write one file.
    """
    report = _report_module()
    try:
        place = str(source.relative_to(_REPO_ROOT))
    except ValueError:
        place = str(source)
    measured = "the stored compile" if is_stored else "the compile"
    has_count = result.instructions is not None
    findings: dict[str, list[object]] = {_CPU_CHECK: [], _INSTRUCTIONS_CHECK: []}
    for check, problem in budget.problems.items():
        findings[check].append(report.Finding("error", place, 0, check, f"{problem}."))
    if _CPU_CHECK in budget.rows:
        text = (f"{measured} of the fixture {fixture_name} took {result.user_s:.2f} s of user CPU time (and "
                f"{result.system_s:.2f} s of system time)")
        findings[_CPU_CHECK] += _judge_row(report, budget.rows[_CPU_CHECK], fixture_name, place, result.user_s, text,
                                           "s", not has_count)
    if _INSTRUCTIONS_CHECK in budget.rows and result.instructions is not None:
        count = result.instructions / _GIGA
        text = f"{measured} of the fixture {fixture_name} ran {count:.2f} G user instructions"
        findings[_INSTRUCTIONS_CHECK] += _judge_row(report, budget.rows[_INSTRUCTIONS_CHECK], fixture_name, place,
                                                    count, text, "G", True)
    status = 0
    for check, found in findings.items():
        status |= report.emit(found, check, warnings_dir, fixture_name)
    return status


def _record_compile(store: ResultStore, key: str, result: CompileResult, missing: list[str], argv: list[str],
                    directory: Path, source: Path, env: Mapping[str, str], started_ns: int) -> tuple[bool, str]:
    """Find the search list of a compile, and store its result.

    `result.inputs` is the list of the dependency file of GCC, or after a
    fatal error the list of the `-M -MG` pass, and `missing` holds each header
    that the pass did not find.  The search query gives the directories of the
    search list, and it uses the scratch directory of the dependency file.
    """
    if result.inputs is None:
        return False, "the driver does not know the files that the compile read"
    named, reason = search_directories(argv, directory, source, Path(argv[-1]).parent, env)
    if named is None:
        return False, reason
    return store.record(key, result, list(result.inputs), missing, named, started_ns)


_INPUTS_FORMAT = 1
_INPUTS_SUFFIX = ".inputs"
_COMMAND_SUFFIX = ".command"


def write_inputs_record(scratch: Path, fixture_name: str, result: CompileResult, is_stored: bool) -> None:
    """Write the record of a run of the fixture: the cost and each file that its compile read.

    The record is `scratch/NAME.inputs`.  Its first line is a JSON object,
    and each line after it is one absolute path.  utils/scripts/check-parse-cost.py
    reads it.  A path with a line break, which no file of the tree has, gives a
    record that says that the driver does not know the inputs.  A failure to
    write changes no verdict.
    """
    inputs = result.inputs
    if inputs is not None and any("\n" in path for path in inputs):
        inputs = None
    header = {"format": _INPUTS_FORMAT, "fixture": fixture_name, "result": "stored" if is_stored else "compiled",
              "user_s": round(result.user_s, 3), "system_s": round(result.system_s, 3),
              "instructions": result.instructions, "has_inputs": inputs is not None}
    text = json.dumps(header) + "\n" + "".join(f"{path}\n" for path in inputs or ())
    try:
        _write_atomic(scratch / f"{fixture_name}{_INPUTS_SUFFIX}", text.encode("utf-8", "surrogateescape"))
    except OSError:
        pass


def obtain_result(fixture_name: str, argv: list[str], directory: Path, source: Path, output: Path,
                  budget: CostBudget) -> tuple[CompileResult, bool]:
    """Return the result of the compile, from the store or from a compile, and True when it comes from the store.

    `argv` ends with `-MF` and the dependency file of this process.  After a
    compile, the dependency file goes to the name of the fixture in the scratch
    directory.  The compile runs in the compile environment, with or without
    the store.  A stored result whose compile is over the error threshold of
    the row that holds the error level is a miss, so the driver measures again.
    """
    depfile = Path(argv[-1])
    env = compile_environment(os.environ)
    protected = {os.path.realpath(path) for path in (source, output, depfile)}
    store, reason = open_store()
    key: str | None = None
    if store is not None:
        key, reason = compile_key(argv, directory, protected, store.memo, env)
    if store is not None and key is not None:
        stored, reason = store.lookup(key)
        if stored is not None and budget.is_over_error(fixture_name, stored):
            stored, reason = None, f"the stored compile {budget.excess_text(stored)} than the error threshold"
        if stored is not None:
            _note(fixture_name, f"the result comes from the store (entry {key})")
            return stored, True
    _note(fixture_name, f"compiled ({reason})")
    depfile.unlink(missing_ok=True)
    started_ns = _now_ns()
    result = run_compile(argv, directory, env)
    dependencies: list[str] | None = None
    missing: list[str] = []
    dependency_reason = "the dependency file of GCC does not hold exactly one rule"
    try:
        text = depfile.read_text(errors="surrogateescape")
        os.replace(depfile, depfile.with_name(f"{fixture_name}.d"))
    except OSError:
        text = None
    if text is not None:
        parsed = parse_dependency_file(text)
        if parsed is not None:
            dependencies = list(dict.fromkeys(os.path.join(directory, name) for name in parsed))
    else:
        # GCC writes no dependency file after a fatal error.  The `-M -MG`
        # pass reads each file that the compile read before it stopped.
        dependencies, missing, dependency_reason = dependency_pass(argv, directory, depfile.parent, env)
    result = dataclasses.replace(result, inputs=None if dependencies is None else tuple(dependencies))
    if store is not None and key is not None:
        if dependencies is None:
            is_stored, why = False, dependency_reason
        elif result.returncode not in (0, 1):
            # A signal or an internal error of the compiler can give a different
            # output on the next run.
            is_stored, why = False, f"the exit code is {result.returncode}"
        elif budget.is_over_error(fixture_name, result):
            is_stored, why = False, f"the compile {budget.excess_text(result)} than the error threshold"
        elif compile_key(argv, directory, protected, store.memo, env)[0] != key:
            # The compiler, a shared object, a path that the loader tried or a
            # file of the command changed between the key and the compile.
            is_stored, why = False, "the compiler identity changed during the compile"
        else:
            is_stored, why = _record_compile(store, key, result, missing, argv, directory, source, env, started_ns)
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

    scratch = build_dir / "neg-compile" / fixture_name
    scratch.mkdir(parents=True, exist_ok=True)
    found = find_compile_command(build_dir, source, scratch / f"{fixture_name}{_COMMAND_SUFFIX}")
    if found is None:
        print(f"no compile command for {source}", file=sys.stderr)
        return 2
    command, directory = found
    output = scratch / f"{fixture_name}.o"
    depfile = scratch / f"{fixture_name}.{os.getpid()}.d"
    argv = compile_argv(command, output, depfile)
    budget = read_cost_budget()
    result, is_stored = obtain_result(fixture_name, argv, directory, source, output, budget)
    write_inputs_record(scratch, fixture_name, result, is_stored)
    verdict = evaluate(fixture_name, source, expected_regexes, result)
    sys.stdout.flush()
    over_budget = report_cost(fixture_name, source, result, is_stored, budget, warnings_dir)
    return verdict or over_budget


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
