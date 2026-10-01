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
   compares the hashes.
3. A new file can change the file that an `#include` finds, and GCC did not
   read that new file.  GCC looks for the file in each directory of its search
   list, in sequence, and it uses the first file that it finds.  It also uses
   a precompiled header (`NAME.gch`) that it finds immediately before that
   file.  For each `#include`, `#embed` and `__has_include` probe, the entry
   records each name that GCC looks for, up to the file that it finds, and
   the `NAME.gch` of each.  For each name, the entry records whether a
   regular file has that name.  The driver makes sure that each condition is
   the same.  A new file with one of these names, or a removed one, changes
   its condition, and other changes in a search directory change no
   condition.
4. The driver finds these directives in the lines of each file that GCC read.
   It reads each line, also the lines of a skipped `#if` branch, of a comment
   and of a string, so it can find more directives than GCC used.  It ignores
   a line where a punctuation mark follows `#include`, because GCC finds no
   file for such a directive.  It cannot find a probe whose name a macro
   makes with `##`.
5. A file can change while GCC reads it.  The driver does not store a result
   when a file that GCC read, or a path that the entry records, changed in the
   second before the compile started or after that.

The driver stores no result in these conditions:

- GCC wrote no dependency file.  This occurs after a fatal error, for example
  a missing header.
- The exit code is not 0 or 1.  A signal or an internal error of the compiler
  can give a different output on the next run.
- A directive or a probe names its file with a macro, or a macro is the name
  of a probe.  Then the driver cannot find the directories that GCC searched.
- A precompiled header is in the search list.  GCC reads its bytes, and the
  dependency file does not name it.
- The command gives a plugin an output directory (`-fplugin-arg-NAME-out=`).
  The compile writes a file there, and a stored result does not write it.
- The command reads a profile (`-fprofile-use`, `-fauto-profile`).

test/neg_compile_driver_test.py holds the tests of the store.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
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
    """The exit code and the combined standard output and error of one compile."""

    returncode: int
    output: str


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
    """Compile with `argv` in `directory`, and return the exit code and the output."""
    proc = subprocess.run(
        argv,
        cwd=directory,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return CompileResult(proc.returncode, proc.stdout + proc.stderr)


# ── The result store ───────────────────────────────────────────────

_STORE_MAGIC = b"crucible-neg-store 1\n"
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

# The scan below reads the preprocessor directives that find a file.  It runs
# after the splice of each line that ends in a backslash, as phase 2 of the
# translation does.  A block comment can come before the `#` (also the end of
# a comment that started on a line before), and between the `#`, the name and
# the file name.  `%:` is the digraph of `#`.
_COMMENTS = rb"(?:[ \t]|/\*.*?\*/)*"
_DIRECTIVE_TAIL = (
    _COMMENTS + rb"(?:#|%:)" + _COMMENTS + rb"(include_next|include|import|embed)\b" + _COMMENTS + rb"(.*)$"
)
_FILE_DIRECTIVE = re.compile(rb"^" + _DIRECTIVE_TAIL, re.M)
_FILE_DIRECTIVE_AFTER_COMMENT = re.compile(rb"\*/" + _DIRECTIVE_TAIL, re.M)
# A probe for a file in a preprocessor condition.  A probe that has no `(`
# after its name is `defined(__has_include)`, `#ifdef __has_include` or a
# comment, and it finds no file.
_FILE_PROBE = re.compile(rb"__has_(include_next|include|embed)\b[ \t]*(\(?)[ \t]*(.*)$", re.M)
# The start of an identifier, which can be a macro that names the file.
_IDENTIFIER_START = re.compile(rb"[A-Za-z_]")
# A macro whose body is the name of a probe, with no `(`.  A use of such a
# macro probes a file that the scan cannot see.
_PROBE_ALIAS = re.compile(
    rb"^[ \t]*(?:#|%:)[ \t]*define[ \t]+\w+(?:\([^)\n]*\))?[ \t]+__has_(?:include_next|include|embed)\b[ \t]*(?:$|[^ \t(])",
    re.M,
)

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


@dataclass(frozen=True, slots=True)
class SearchDirectories:
    """The include search list of one compile, as `-v` prints it.

    `quote` holds the directories that only `#include "..."` searches, and
    `angle` holds the directories that both forms search, in their sequence.
    `missing` holds the directories that GCC ignores because they are missing.
    """

    quote: tuple[str, ...]
    angle: tuple[str, ...]
    missing: tuple[str, ...]


def search_directories(argv: list[str], directory: Path, source: str) -> SearchDirectories | None:
    """Return the include search list of the compile, or None if GCC does not give it.

    The function runs the compile command with `-E -v` on an empty input.
    """
    probe: list[str] = []
    skip_next = False
    for arg in argv:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-o", "-MF"):
            skip_next = True
            continue
        if arg in ("-c", "-MD") or os.path.realpath(os.path.join(directory, arg)) == source:
            continue
        probe.append(arg)
    proc = subprocess.run(
        [*probe, "-E", "-v", "-x", "c++", os.devnull, "-o", os.devnull],
        cwd=directory,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        return None
    lists: dict[str, list[str]] = {"quote": [], "angle": []}
    missing: list[str] = []
    section: str | None = None
    is_complete = False
    for line in proc.stderr.splitlines():
        if line.startswith('ignoring nonexistent directory "') and line.endswith('"'):
            missing.append(line[len('ignoring nonexistent directory "') : -1])
        elif line.startswith('#include "..." search starts here:'):
            section = "quote"
        elif line.startswith("#include <...> search starts here:"):
            section = "angle"
        elif line.startswith("End of search list."):
            is_complete = True
            break
        elif section is not None and line.startswith(" "):
            lists[section].append(line[1:])
    if not is_complete:
        return None
    return SearchDirectories(tuple(lists["quote"]), tuple(lists["angle"]), tuple(missing))


def _directive_name(rest: bytes) -> tuple[str, str] | None:
    """Return the form (`"` or `<`) and the file name at the start of `rest`, or None."""
    closer = {b'"': b'"', b"<": b">"}.get(rest[:1])
    if closer is None:
        return None
    end = rest.find(closer, 1)
    if end < 0:
        return None
    return rest[:1].decode("ascii"), rest[1:end].decode("utf-8", "surrogateescape")


def scan_file_directives(data: bytes) -> list[tuple[str, str, str]] | None:
    """Return each directive and probe in `data` that finds a file.

    Each item is the kind (`include`, `include_next`, `import`, `embed`, or
    `has_include`, `has_include_next`, `has_embed` for a probe), the form
    (`"` or `<`) and the file name.  The function returns None when the file of
    a directive or a probe can be a macro (an identifier follows the name), or
    when a macro is the name of a probe.  It reads every line, also the lines
    of a skipped `#if` branch, of a comment and of a string, so it can give
    more items than GCC used.  It ignores a line where other text follows the
    name, such as the prose `#include's`, because GCC finds no file for such a
    directive.  The cost is O(n) in the length of `data`.
    """
    spliced = data.replace(b"\\\r\n", b"").replace(b"\\\n", b"")
    found: list[tuple[str, str, str]] = []
    for line in _lines_with(spliced, (b"include", b"import", b"embed")):
        for pattern in (_FILE_DIRECTIVE, _FILE_DIRECTIVE_AFTER_COMMENT):
            match = pattern.search(line)
            if match is None:
                continue
            named = _directive_name(match.group(2))
            if named is None and _IDENTIFIER_START.match(match.group(2)):
                return None
            if named is not None:
                found.append((match.group(1).decode("ascii"), *named))
    for line in _lines_with(spliced, (b"__has_",)):
        if _PROBE_ALIAS.search(line):
            return None
        for match in _FILE_PROBE.finditer(line):
            if not match.group(2):
                continue
            named = _directive_name(match.group(3))
            if named is None and _IDENTIFIER_START.match(match.group(3)):
                return None
            if named is not None:
                found.append((f"has_{match.group(1).decode('ascii')}", *named))
    return list(dict.fromkeys(found))


def _lines_with(data: bytes, words: tuple[bytes, ...]) -> list[bytes]:
    """Return each line of `data` that holds one of `words`, one time each, in sequence.

    A search for a word costs less than a regex at each line start.  The cost
    is O(n + m log m) for n bytes and m lines that hold a word.
    """
    starts: set[int] = set()
    for word in words:
        position = data.find(word)
        while position >= 0:
            starts.add(data.rfind(b"\n", 0, position) + 1)
            position = data.find(word, position + len(word))
    lines: list[bytes] = []
    for start in sorted(starts):
        end = data.find(b"\n", start)
        lines.append(data[start:] if end < 0 else data[start:end])
    return lines


def path_state(path: str) -> tuple[list[int] | str | None, int | None]:
    """Return the condition of a search path, and its change time when it is present.

    The condition is `"file"` for a regular file, None when no file has the
    name, an error text when the driver cannot read the name, and the inode and
    the two change times for another kind of file.  A regular file is a
    dependency, whose bytes the entry holds, or a file that a probe found, of
    which only the presence matters.  So an edit of the file changes no
    condition.
    """
    try:
        status = os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return None, None
    except OSError as error:
        return f"error {error.errno}", None
    if stat.S_ISREG(status.st_mode):
        return "file", status.st_ctime_ns
    return [status.st_ino, status.st_mtime_ns, status.st_ctime_ns], status.st_ctime_ns


def search_paths(
    dependencies: list[str],
    directives: dict[str, list[tuple[str, str, str]]],
    search: SearchDirectories,
    directory: Path,
    argv: list[str],
) -> list[str]:
    """Return each path whose condition decides the file that a directive finds.

    For a file name `a/b.h` and each search directory `D`, the paths are
    `D/a/b.h` and `D/a/b.h.gch`, the precompiled header that GCC uses in place
    of the file when it is there.  The search stops at the first directory
    that holds `a/b.h`, as GCC does, and a search that finds no file gives the
    two paths for each directory.

    An `#include_next` starts after the first search directory that holds the
    file of the directive, or at the start of the list when no search
    directory holds that file.  It does not stop, because the function cannot
    know the directory that GCC started after when one search directory holds
    another.  A probe can be in the body of a macro.  Then GCC searches from
    the file that uses the macro.  The function starts a `__has_include("...")`
    probe in the directory of each file that GCC read, and a
    `__has_include_next` probe reads the full list.  The missing directories go
    first in each list.  The cost is O(d * s) for d different file names and s
    search directories.
    """
    quote_chain = [*search.missing, *search.quote, *search.angle]
    angle_chain = [*search.missing, *search.angle]
    next_chain = [*search.quote, *search.angle]
    candidates: dict[str, None] = {}
    probed: set[tuple[str, ...]] = set()

    def probe(chain: list[str], name: str, should_stop: bool) -> None:
        """Add the paths that a search for `name` through `chain` reads."""
        key = (str(should_stop), name, *chain)
        if key in probed:
            return
        probed.add(key)
        for base in chain:
            target = os.path.join(base, name)
            candidates[f"{target}.gch"] = None
            candidates[target] = None
            if should_stop and base not in search.missing and os.path.isfile(target):
                return

    # GCC reads stdc-predef.h with no directive.  `-include` and `-imacros`
    # search the working directory first.
    probe(angle_chain, "stdc-predef.h", True)
    for index, arg in enumerate(argv[:-1]):
        if arg in ("-include", "-imacros"):
            probe([str(directory), *quote_chain], argv[index + 1], True)
    for path in dependencies:
        for kind, form, name in directives.get(path, []):
            if kind == "has_include_next":
                probe([*search.missing, *next_chain], name, False)
            elif kind.startswith("has_") and form == '"':
                for user in dependencies:
                    probe([os.path.dirname(user), *quote_chain], name, True)
            elif kind.endswith("_next"):
                normalized = os.path.normpath(path)
                start = next(
                    (
                        position + 1
                        for position, base in enumerate(next_chain)
                        if normalized.startswith(os.path.normpath(base) + os.sep)
                    ),
                    0,
                )
                probe([*search.missing, *next_chain[start:]], name, False)
            elif form == '"':
                probe([os.path.dirname(path), *quote_chain], name, True)
            else:
                probe(angle_chain, name, True)
    return list(candidates)


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
        and all(isinstance(field, int) and not isinstance(field, bool) for field in value)
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
            if path_state(name)[0] != state:
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
        return CompileResult(entry["returncode"], entry["output"]), ""  # type: ignore[arg-type]

    def record(
        self,
        key: str,
        argv: list[str],
        directory: Path,
        source: str,
        result: CompileResult,
        dependencies: list[str] | None,
        started_ns: int,
    ) -> tuple[bool, str]:
        """Store `result` for `key`, and return True, or False and the reason.

        The function refuses a result whose inputs it cannot record in full, or
        whose inputs changed in the settle period before `started_ns`.
        """
        if result.returncode not in (0, 1):
            return False, f"the exit code is {result.returncode}"
        if dependencies is None:
            return False, "GCC wrote no dependency file"
        settled_ns = started_ns - _SETTLE_NS
        recorded: list[list[str]] = []
        directives: dict[str, list[tuple[str, str, str]]] = {}
        for name in dependencies:
            try:
                before = os.stat(name)
                data = Path(name).read_bytes()
                after = os.stat(name)
            except OSError:
                return False, f"a dependency cannot be read: {name}"
            if _stat_fields(before) != _stat_fields(after) or after.st_ctime_ns >= settled_ns:
                return False, f"a dependency changed less than one second before the compile: {name}"
            found = scan_file_directives(data)
            if found is None:
                return False, f"a directive or a probe names its file with a macro: {name}"
            directives[name] = found
            recorded.append([name, _short_digest(data)])
        search = search_directories(argv, directory, source)
        if search is None:
            return False, "GCC does not give its include search list"
        states: list[list[object]] = []
        for name in search_paths(dependencies, directives, search, directory, argv):
            state, changed_ns = path_state(name)
            if changed_ns is not None and changed_ns >= settled_ns:
                return False, f"a search path changed less than one second before the compile: {name}"
            if state is not None and name.endswith(".gch"):
                return False, f"a precompiled header is in the search list: {name}"
            states.append([name, state])
        entry = {
            "dependencies": recorded,
            "searched": states,
            "output": result.output,
            "returncode": result.returncode,
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


def obtain_result(fixture_name: str, argv: list[str], directory: Path, source: Path, output: Path) -> CompileResult:
    """Return the result of the compile, from the store or from a compile.

    `argv` ends with `-MF` and the dependency file of this process.  After a
    compile, the dependency file goes to the name of the fixture in the scratch
    directory.
    """
    depfile = Path(argv[-1])
    store, reason = open_store()
    key: str | None = None
    if store is not None:
        protected = {os.path.realpath(path) for path in (source, output, depfile)}
        key, reason = compile_key(argv, directory, protected, store.identity)
    if store is not None and key is not None:
        stored, reason = store.lookup(key)
        if stored is not None:
            _note(fixture_name, f"the result comes from the store (entry {key})")
            return stored
    _note(fixture_name, f"compiled ({reason})")
    depfile.unlink(missing_ok=True)
    started_ns = time.time_ns()
    result = run_compile(argv, directory)
    dependencies: list[str] | None = None
    try:
        text = depfile.read_text(errors="surrogateescape")
        os.replace(depfile, depfile.with_name(f"{fixture_name}.d"))
    except OSError:
        text = None
    if text is not None:
        parsed = parse_dependency_file(text)
        if parsed is not None:
            dependencies = list(dict.fromkeys(os.path.join(directory, name) for name in parsed))
    if store is not None and key is not None:
        is_stored, why = store.record(
            key, argv, directory, os.path.realpath(source), result, dependencies, started_ns
        )
        _note(fixture_name, f"stored (entry {key})" if is_stored else f"not stored ({why})")
    return result


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
    if len(arguments) < 5:
        print(
            "usage: neg_compile_driver.py <build-dir> <source> "
            "<fixture-name> <expected-regex> [<expected-regex> ...]",
            file=sys.stderr,
        )
        return 2

    build_dir = Path(arguments[1]).resolve()
    source = Path(arguments[2]).resolve()
    fixture_name = arguments[3]
    expected_regexes = arguments[4:]

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
    result = obtain_result(fixture_name, argv, directory, source, output)
    return evaluate(fixture_name, source, expected_regexes, result)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
