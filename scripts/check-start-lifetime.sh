#!/usr/bin/env bash
# check-start-lifetime.sh — std::start_lifetime_as only where no proof can reach it.
#
# The rule
# --------
# A use of std::start_lifetime_as or std::start_lifetime_as_array is
# admitted in three places only:
#
#   the checked start    include/foundation/Lifetime.h, whose
#                        start_as_array refuses at compile time a type with
#                        a subobject that is not an implicit-lifetime type
#   a frozen file        a path that scripts/check-frozen-tree.sh freezes,
#                        so no new use can appear there
#   a negative fixture   a file under a test directory named neg or *_neg,
#                        which must fail to compile
#
# Each admitted use also needs an entry in
# scripts/start-lifetime-allowlist.txt, and each entry states how many
# uses it admits.  A use anywhere else is refused, also when the
# allowlist names it, and an entry for such a path is refused too.  New
# code calls foundation::lifetime::start_as_array.  A use is each
# occurrence of the name in code: a call, a using-declaration, a macro
# body, or the address of the function.
#
# Why
# ---
# A proof type (a mint key, a context, a capability, a permission, a read
# proof) is neither trivially copyable nor an implicit-lifetime type, so
# std::bit_cast and std::start_lifetime_as<P> refuse it.  Three routes of
# the same library family still give a pointer to a proof object whose
# lifetime never started:
#
#   std::start_lifetime_as<P[1]>(buf)            an array type is an
#                                                implicit-lifetime type
#                                                for any element
#   std::start_lifetime_as_array<P>(buf, 1)      libstdc++ puts no mandate
#                                                on the element type
#   std::start_lifetime_as<Holder>(buf)          an aggregate that holds a
#                                                P, or std::array<P, 1>
#
# A read through that pointer is undefined behavior, and no property of a
# type can refuse the three routes.  The checked start refuses all three
# at compile time, whatever an alias in its template argument names.  So
# this guard keeps the library names out of every other place, and
# test/fixy/test_forgeable_proofs.cpp states the routes that stay open.
#
# The key
# -------
# An allowlist entry is `path:key`, or `path:key xN` for N uses.  The key
# is the name and its template argument list, with the white space
# reduced, for example
#
#   include/crucible/concurrent/AtomicSnapshot.h:start_lifetime_as<T> x2
#
# The key survives a line shift, a rename of the variable that holds the
# result, and a change of the qualification.  A use with no template
# argument list has the bare name as its key.  The count makes each new
# use a change to the allowlist.  Each entry needs a comment above it
# that gives the reason.
#
# Two passes
# ----------
# The lexical pass reads each source file.  It joins each backslash-newline
# pair first, as translation phase 2 does, and then a lexer reads comments,
# string literals, raw string literals, character literals, numbers with
# digit separators, and identifiers.  A use is an identifier token with
# one of the two names.  The pass also follows each #include whose operand
# is a literal, whatever the suffix of the named file.  Its scope is every
# C and C++ source file that git tracks, every untracked file that
# .gitignore does not exclude, and with --compile-db each source that the
# compile database names.
#
# The preprocessed pass runs with --compile-db.  It runs the compiler of
# each database entry with -E and the flags of the build, and it reads
# the output.  Preprocessing expands token pasting and macro bodies,
# follows an #include whose operand is a macro, and finds each header
# through the -I flags of the build.  A line marker of the output names
# the file and the line of each token, so a use in a header counts once,
# however many translation units include it.  A name inside a literal is
# not a use.  For each file and key, the count is the larger count of
# the two passes, so an arm that this host does not compile still counts.
#
# The preprocessed pass keeps its results in start-lifetime-cache/ beside
# the compile database.  An entry is valid while the command, the
# compiler binary and the content of each file under the root that the
# translation unit read stay the same.  A preprocessor failure refuses
# the run, because a translation unit the guard cannot read can hold a
# use.  START_LIFETIME_JOBS sets the number of parallel preprocessor
# runs, and the default is the number of processors, at most 16.
#
# What this guard does not see, stated rather than implied
# --------------------------------------------------------
#   - a file that no entry of the compile database reads.  The lexical
#     pass alone scans it, so a name built by token pasting there, or an
#     #include with a macro operand, is not seen.  A negative fixture is
#     such a file, and it must fail to compile anyway.
#   - a proof pointer from a void pointer, from std::malloc, from an
#     allocator, or from the inactive member of a union.  These routes do
#     not name std::start_lifetime_as, and a lexer cannot know that the
#     target type of a cast is a proof type.  The forgeability ledger in
#     test/fixy/test_forgeable_proofs.cpp pins them.
#   - a frozen site whose template argument a later change can make a
#     proof type.  The only such site is SwissTableBuffer<SlotPtr> in
#     include/crucible/safety, whose SlotPtr no constraint holds.  Its
#     allowlist entry names it, and the frozen tree admits no new site.
#
# Exit codes
#   0 — each use is admitted, and each entry admits exactly its uses
#   1 — a use outside the three places, a use with no entry, or a key with
#       more uses than its entry admits
#   2 — an entry above its count or outside the three places, a
#       preprocessor failure, a bad invocation, or a failed self-test
#
# Usage
#   check-start-lifetime.sh [--compile-db PATH]         check the tree
#   check-start-lifetime.sh [--compile-db PATH] --list  print each use
#   check-start-lifetime.sh --self-test                 plant each route
#                                                       and each known
#                                                       bypass, and prove
#                                                       the verdicts

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="${START_LIFETIME_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/start-lifetime-allowlist.txt"
compile_db=""

usage() {
    printf 'usage: %s [--compile-db PATH] [--list | --self-test | -h]\n' "${BASH_SOURCE[0]}" >&2
}

run_scan() {
    python3 - "$1" "$scan_root" "$allowlist" "$compile_db" "$(dirname "${BASH_SOURCE[0]}")" <<'PY'
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

mode = sys.argv[1]
root = Path(sys.argv[2]).resolve()
allowlist_path = Path(sys.argv[3])
compile_db = Path(sys.argv[4]).resolve() if sys.argv[4] else None
sys.path.insert(0, sys.argv[5])
from cxx_lex import blank, line_of, splice  # noqa: E402

NAMES = frozenset({"start_lifetime_as", "start_lifetime_as_array"})
SOURCE_SUFFIXES = frozenset({".c", ".C", ".h", ".H", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".c++", ".h++",
                             ".cp", ".CPP", ".ixx", ".cppm", ".mpp", ".inl", ".ipp", ".tpp", ".tcc", ".txx",
                             ".icc", ".inc", ".ii"})
SKIPPED_DIRS = frozenset({".git", "third_party", "external", "vendor", "__pycache__", "node_modules"})
# The directories that an include path is resolved against, after the
# directory of the file that names it.
INCLUDE_ROOTS = ("include", "", "src", "test", "vessel", "bench", "tools", "fuzz", "examples")
# An include directive in text whose comments are blanked, so a comment
# inside the directive is white space.  %: is the digraph of #.
INCLUDE = re.compile(r'^[ \t]*(?:#|%:)[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.M)
# The one file whose use of the library start is checked at compile time.
CHECKED_START = "include/foundation/Lifetime.h"
# A negative-compile fixture: a file under a test directory named neg or *_neg.
FIXTURE = re.compile(r"^test/(?:[^/]+/)*(?:neg|[^/]+_neg)/[^/]+$")
# Bumped when the preprocessed pass reads its output differently, so an
# older cache entry is not reused.
SCAN_VERSION = 1

ENTRY = re.compile(r"^(?P<key>.*?)(?: x(?P<count>[1-9][0-9]*))?$")
# A line marker of preprocessed output: # line "file" flags.
MARKER = re.compile(rb'# ([0-9]+) "((?:[^"\\]|\\.)*)"[^\n]*')
NAME_BYTES = b"start_lifetime_as"


def is_source(path: Path) -> bool:
    """True when the suffix names a C or C++ source, also under a .in template."""
    suffix = path.suffix
    if suffix == ".in":
        suffix = Path(path.stem).suffix
    return suffix in SOURCE_SUFFIXES


def listed_files() -> list[Path]:
    """The files git lists under the root, or every file under it outside a work tree.

    Complexity: linear in the number of files under the root."""
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             check=True, capture_output=True).stdout
        return [root / p for p in out.decode(errors="replace").split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        found = []
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames
                           if d not in SKIPPED_DIRS and not d.startswith("build") and not d.startswith("cmake-build")]
            found.extend(Path(dirpath) / name for name in filenames)
        return found


def database_entries() -> list[dict]:
    """The entries of the compile database, or none without one."""
    if compile_db is None:
        return []
    return json.loads(compile_db.read_text())


def entry_source(command: dict) -> Path:
    """The source file of a compile database entry, as an absolute path."""
    source = Path(command["file"])
    return source if source.is_absolute() else Path(command["directory"]) / source


def frozen_prefixes() -> list[str]:
    """The frozen prefixes that scripts/check-frozen-tree.sh declares.

    An unreadable declaration freezes nothing, so the rule fails closed."""
    script = root / "scripts" / "check-frozen-tree.sh"
    if not script.is_file():
        return []
    declared = re.search(r"^FROZEN_PATHS=\(\n(.*?)^\)", script.read_text(), re.M | re.S)
    if declared is None:
        return []
    return [line.strip() for line in declared.group(1).splitlines()
            if line.strip() and not line.strip().startswith("#")]


FROZEN = frozen_prefixes()


def is_admitted_path(path: str) -> bool:
    """True when the path is the checked start, a frozen file or a negative fixture."""
    if path == CHECKED_START or FIXTURE.match(path):
        return True
    return any(path.startswith(prefix) if prefix.endswith("/") else path == prefix for prefix in FROZEN)


def resolve_include(including: Path, delimiter: str, name: str) -> Path | None:
    """The file under the root that an include directive names, if one exists."""
    candidates = [including.parent / name] if delimiter == '"' else []
    candidates += [root / base / name for base in INCLUDE_ROOTS]
    for candidate in candidates:
        if candidate.is_file():
            resolved = candidate.resolve()
            if resolved.is_relative_to(root):
                return resolved
    return None


def shown(path: Path) -> str:
    """The path relative to the root when it is under the root."""
    return path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()


def normalized(fragment: str) -> str:
    """Reduce white space: one space between two word characters, none elsewhere."""
    squeezed = re.sub(r"\s+", " ", fragment.strip())
    return re.sub(r" (?=\W)|(?<=\W) ", "", squeezed)


def key_at(blanked: str, offset: int) -> str:
    """The name at the offset and its template argument list, if it has one."""
    name_match = re.compile(r"[A-Za-z_0-9]+").match(blanked, offset)
    name = name_match.group(0)
    cursor = name_match.end()
    while cursor < len(blanked) and blanked[cursor].isspace():
        cursor += 1
    if cursor >= len(blanked) or blanked[cursor] != "<":
        return name
    depth = 0
    for index in range(cursor, min(len(blanked), cursor + 1024)):
        ch = blanked[index]
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return name + normalized(blanked[cursor:index + 1])
        elif ch in ";{}":
            break
    return name + "<unbalanced>"


def read_allowlist() -> dict[str, tuple[int, int]]:
    """Each entry key, the number of uses it admits, and its line in the allowlist."""
    entries: dict[str, tuple[int, int]] = {}
    if not allowlist_path.is_file():
        return entries
    for number, raw in enumerate(allowlist_path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        entries[match.group("key")] = (int(match.group("count") or 1), number)
    return entries


def lexical_scan(uses: dict[str, list[int]]) -> None:
    """Record each use in scope, and follow each include directive to the file it names.

    Each file is lexed once.  Complexity: linear in the total size of the
    files in scope."""
    pending = [p.resolve() for p in listed_files() if is_source(p) and p.is_file()]
    pending += [entry_source(e).resolve() for e in database_entries() if entry_source(e).is_file()]
    seen: set[Path] = set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        joined, joins = splice(path.read_bytes().decode(errors="replace"))
        blanked, hits = blank(joined, NAMES)
        for offset in hits:
            uses[f"{shown(path)}:{key_at(blanked, offset)}"].append(line_of(joined, joins, offset))
        for delimiter, name in INCLUDE.findall(blanked):
            target = resolve_include(path, delimiter, name)
            if target is not None and target not in seen:
                pending.append(target)


def preprocess_argv(argv: list[str]) -> list[str]:
    """The compile command with -E, and without the output and dependency-file flags."""
    out: list[str] = []
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag in ("-c", "-MD", "-MMD", "-MP"):
            index += 1
        elif flag in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
        elif flag.startswith(("-o", "-MF", "-MT", "-MQ")):
            index += 1
        else:
            out.append(flag)
            index += 1
    return out + ["-E"]


class ContentHashes:
    """The SHA-256 of each file under the root, computed once per run."""

    def __init__(self) -> None:
        self.known: dict[str, str | None] = {}

    def of(self, relative: str) -> str | None:
        """The hash of the file, or None when it no longer exists."""
        if relative not in self.known:
            path = root / relative
            self.known[relative] = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        return self.known[relative]


def under_root(name: bytes, directory: str, memo: dict) -> str | None:
    """The root-relative path of a line-marker file name, or None outside the root."""
    cached = memo.get((name, directory))
    if cached is not None or (name, directory) in memo:
        return cached
    text = name.decode(errors="replace").replace('\\"', '"').replace("\\\\", "\\")
    result = None
    if not text.startswith("<"):
        absolute = os.path.normpath(os.path.join(directory, text))
        prefix = str(root) + os.sep
        if absolute.startswith(prefix):
            result = absolute[len(prefix):]
    memo[(name, directory)] = result
    return result


def marker_before(data: bytes, offset: int):
    """The last line marker of the preprocessed output that starts before the offset."""
    search = offset
    while search > 0:
        newline = data.rfind(b"\n# ", 0, search)
        start = newline + 1 if newline >= 0 else 0
        marker = MARKER.match(data, start)
        if marker is not None:
            return marker
        if newline < 0:
            return None
        search = newline
    return None


def is_identifier_byte(byte: bytes) -> bool:
    """True when the byte can continue an identifier."""
    return byte.isalnum() or byte == b"_"


def read_preprocessed(data: bytes, directory: str) -> list[list]:
    """The uses in the preprocessed output that fall in files under the root.

    Each use is [path, line, ordinal on the line, key].  The search runs in
    the byte-string routines of the interpreter, and a line marker is read
    only for a hit.  Complexity: linear in the length of the output."""
    memo: dict = {}
    hits: list[list] = []
    ordinals: dict[tuple[str, int], int] = defaultdict(int)
    position = 0
    while (start := data.find(NAME_BYTES, position)) >= 0:
        end = start + len(NAME_BYTES)
        if data.startswith(b"_array", end):
            end += len(b"_array")
        position = end
        if is_identifier_byte(data[start - 1:start]) or is_identifier_byte(data[end:end + 1]):
            continue
        marker = marker_before(data, start)
        if marker is None:
            continue
        path = under_root(marker.group(2), directory, memo)
        if path is None:
            continue
        line = int(marker.group(1)) + data.count(b"\n", marker.end() + 1, start)
        line_start = data.rfind(b"\n", 0, start) + 1
        window = data[line_start:line_start + 4096].decode(errors="replace")
        column = len(data[line_start:start].decode(errors="replace"))
        blanked, idents = blank(window, NAMES)
        if column not in idents:
            continue
        ordinal = ordinals[(path, line)]
        ordinals[(path, line)] += 1
        hits.append([path, line, ordinal, key_at(blanked, column)])
    return hits


def read_dependencies(text: str, directory: str) -> set[str]:
    """The files under the root that a make-style dependency file names."""
    memo: dict = {}
    body = text.replace("\\\n", " ").split(":", 1)[-1]
    found: set[str] = set()
    for name in re.split(r"(?<!\\)\s+", body):
        if name and (path := under_root(name.replace("\\ ", " ").encode(), directory, memo)) is not None:
            found.add(path)
    return found


def compiler_identity(argv: list[str], directory: str) -> list:
    """The size and modification time of the compiler binary, when it can be found."""
    binary = argv[0] if os.path.isabs(argv[0]) else (
        os.path.join(directory, argv[0]) if os.sep in argv[0] else next(
            (os.path.join(d, argv[0]) for d in os.environ.get("PATH", "").split(os.pathsep)
             if os.path.isfile(os.path.join(d, argv[0]))), argv[0]))
    try:
        info = os.stat(os.path.realpath(binary))
        return [os.path.realpath(binary), info.st_size, info.st_mtime_ns]
    except OSError:
        return [binary]


def preprocessed_scan(uses: dict[str, set[tuple[int, int]]], failures: list[str]) -> tuple[int, int, float]:
    """Run each database entry through the preprocessor, or read its valid cache entry.

    Complexity: linear in the total size of the preprocessed output; the
    runs are parallel."""
    entries = database_entries()
    cache_dir = compile_db.parent / "start-lifetime-cache"
    cache_dir.mkdir(exist_ok=True)
    hashes = ContentHashes()
    jobs = int(os.environ.get("START_LIFETIME_JOBS", "0") or 0) or min(16, os.cpu_count() or 1)

    def one(entry: dict) -> tuple[list[list], str | None, bool]:
        argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        directory = entry["directory"]
        run_argv = preprocess_argv(argv)
        identity = json.dumps([SCAN_VERSION, run_argv, directory, compiler_identity(argv, directory)])
        cache_file = cache_dir / (hashlib.sha256(identity.encode()).hexdigest() + ".json")
        if cache_file.is_file():
            try:
                cached = json.loads(cache_file.read_text())
                if all(hashes.of(path) == digest for path, digest in cached["dependencies"].items()):
                    return cached["hits"], None, True
            except (OSError, ValueError, KeyError):
                pass
        descriptor, depfile = tempfile.mkstemp(prefix="start-lifetime-", suffix=".d")
        os.close(descriptor)
        try:
            result = subprocess.run(run_argv + ["-MD", "-MF", depfile], cwd=directory, capture_output=True)
            dependencies = read_dependencies(Path(depfile).read_text(errors="replace"), directory)
        finally:
            os.unlink(depfile)
        if result.returncode != 0:
            first = result.stderr.decode(errors="replace").strip().splitlines()[:1]
            return [], f"{entry['file']}: {first[0] if first else 'exit ' + str(result.returncode)}", False
        hits = read_preprocessed(result.stdout, directory)
        record = {"hits": hits, "dependencies": {path: hashes.of(path) for path in sorted(dependencies)}}
        staging = cache_file.with_suffix(f".{os.getpid()}.{id(entry)}.tmp")
        staging.write_text(json.dumps(record))
        os.replace(staging, cache_file)
        return hits, None, False

    begin = time.monotonic()
    cached = 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for hits, failure, from_cache in pool.map(one, entries):
            cached += from_cache
            if failure is not None:
                failures.append(failure)
            for path, line, ordinal, key in hits:
                uses[f"{path}:{key}"].add((line, ordinal))
    return len(entries), cached, time.monotonic() - begin


def main() -> int:
    entries = read_allowlist()
    lexical: dict[str, list[int]] = defaultdict(list)
    lexical_scan(lexical)
    preprocessed: dict[str, set[tuple[int, int]]] = defaultdict(set)
    failures: list[str] = []
    if compile_db is not None:
        units, cached, seconds = preprocessed_scan(preprocessed, failures)
        print(f"check-start-lifetime: preprocessed {units} translation unit(s) in {seconds:.1f} s, "
              f"{cached} from the cache.", file=sys.stderr)

    unreviewed = 0
    total = 0
    for key in sorted(set(lexical) | set(preprocessed)):
        path = key.split(":", 1)[0]
        lines = sorted(set(lexical.get(key, [])) | {line for line, _ in preprocessed.get(key, set())})
        count = max(len(lexical.get(key, [])), len(preprocessed.get(key, set())))
        total += count
        admitted = entries.get(key, (0, 0))[0]
        if not is_admitted_path(path):
            for line in lines:
                unreviewed += 1
                print(f"START-LIFETIME violation: {path}:{line} starts a lifetime outside the checked start, a "
                      f"frozen file and a negative fixture.  Use foundation::lifetime::start_as_array.  key: {key}",
                      file=sys.stderr)
        elif admitted == 0:
            for line in lines:
                unreviewed += 1
                print(f"START-LIFETIME violation: {path}:{line} has no reviewed entry.  Allowlist key: {key}",
                      file=sys.stderr)
        elif count > admitted:
            unreviewed += 1
            print(f"START-LIFETIME violation: {path} has {count} uses of this key at lines "
                  f"{', '.join(map(str, lines))}, and its entry admits {admitted}.  Allowlist key: "
                  f"{key} x{count}", file=sys.stderr)
        elif mode == "list":
            print(f"REVIEWED  {key}  ({count} use(s), lines {', '.join(map(str, lines))})")
    stale = 0
    for key, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        path = key.split(":", 1)[0]
        found = max(len(lexical.get(key, [])), len(preprocessed.get(key, set())))
        if not is_admitted_path(path):
            stale += 1
            print(f"START-LIFETIME refused entry: {allowlist_path.name}:{number} names {path}, which is not the "
                  f"checked start, a frozen file or a negative fixture.  An entry cannot admit it.", file=sys.stderr)
        elif not unreviewed and found < admitted:
            stale += 1
            print(f"START-LIFETIME stale: {allowlist_path.name}:{number} admits {admitted} use(s) of {key}, "
                  f"and the tree has {found}.", file=sys.stderr)
    for failure in failures:
        print(f"START-LIFETIME preprocessor failure: {failure}.  A translation unit the guard cannot read can "
              f"hold a use, so the run is refused.", file=sys.stderr)
    print(f"check-start-lifetime: {total} use(s) under {len(set(lexical) | set(preprocessed))} key(s), "
          f"{unreviewed} unreviewed, {stale} refused or stale entr(y/ies), {len(failures)} preprocessor "
          f"failure(s).", file=sys.stderr)
    if unreviewed:
        print("\nEach use of std::start_lifetime_as or std::start_lifetime_as_array outside the checked start,\n"
              "a frozen file and a negative fixture is refused.\n"
              "  (1) Prefer a construction, std::bit_cast, or a typed arena to the lifetime start.\n"
              "  (2) Use foundation::lifetime::start_as_array from <foundation/Lifetime.h>.  It refuses at\n"
              "      compile time a type with a subobject that is not an implicit-lifetime type.  A single\n"
              "      object is a span of one.\n"
              "  (3) A negative fixture adds the printed key to scripts/start-lifetime-allowlist.txt, with a\n"
              "      comment above it that names the element type.",
              file=sys.stderr)
        return 1
    return 2 if stale or failures else 0


sys.exit(main())
PY
}

# True when a line of the file ends with the text.
report_ends_with() {
    local line
    while IFS= read -r line; do
        [[ "$line" == *"$1" ]] && return 0
    done <"$2"
    return 1
}

# Runs the guard over the planted root, and keeps its exit code in rc.
run_planted() {
    set +e
    START_LIFETIME_TEST_ROOT="$tmp" bash "${BASH_SOURCE[0]}" "$@" >"$out" 2>&1
    rc=$?
    set -e
}

# Prints the guard output and fails the self-test.
self_test_fail() {
    printf 'check-start-lifetime --self-test: FAIL — %s\n' "$1" >&2
    rg -N '' "$out" >&2 || true
    return 2
}

# Counts the reports whose key the self-test expects, and fails on a
# missing, an extra or a forbidden one.
expect_reports() {
    local forbidden="$1"
    shift
    local key failed=0
    for key in "$@"; do
        if ! report_ends_with "key: $key" "$out"; then
            printf 'check-start-lifetime --self-test: FAIL — no report for %s.\n' "$key" >&2
            failed=1
        fi
    done
    if [[ "$(rg -c 'key: ' "$out" || printf 0)" != "$#" ]]; then
        printf 'check-start-lifetime --self-test: FAIL — %s reports, not %s.\n' \
            "$(rg -c 'key: ' "$out" || printf 0)" "$#" >&2
        failed=1
    fi
    if rg -q "$forbidden" "$out"; then
        printf 'check-start-lifetime --self-test: FAIL — a report matches %s, which the guard must not report.\n' \
            "$forbidden" >&2
        failed=1
    fi
    return "$failed"
}

self_test() {
    local tmp rc out cxx
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    out="$tmp/out.txt"
    cxx="${START_LIFETIME_CXX:-c++}"
    mkdir -p "$tmp/scripts" "$tmp/include/planted/frozen" "$tmp/lib" "$tmp/misc" "$tmp/src" "$tmp/test/neg" \
        "$tmp/build-planted/gen"

    # A planted freeze.  The guard reads the frozen prefixes from it.
    cat >"$tmp/scripts/check-frozen-tree.sh" <<'EOF'
FROZEN_PATHS=(
    include/planted/frozen/
)
EOF
    # Each route of the ledger and each bypass the guard must see.  Each
    # use has a distinct template argument, so each key names exactly one
    # planted line.  The <Listed> use has an allowlist entry, and the path
    # is none of the three places, so the entry cannot admit it.
    cat >"$tmp/include/planted/Routes.h" <<'EOF'
#pragma once
#include <array>
#include <memory>
%:include "../../misc/Hidden.txt"
# /* a comment */ include "../../misc/Other.txt"
struct Proof;
template <class P> struct Holder { P proof; };
struct Listed { int value; };
inline void routes(unsigned char* buf) {
    (void)std::start_lifetime_as<Proof[1]>(buf);
    (void)std::start_lifetime_as_array<Proof>(buf, 1);
    (void)std::start_lifetime_as<Holder<Proof>>(buf);
    (void)std::start_lifetime_as<std::array<Proof, 1>>(buf);
    (void)std :: start_lifetime_as < Proof [2] > (buf);
    (void)std::start_lifetime_as /* a comment */ <Proof[3]>(buf);
    (void)std::start_life\
time_as<Proof[4]>(buf);
    (void)std::start_lifetime_as_array<Listed>(buf, 4);
}
using std::start_lifetime_as;
#define PLANTED_LIFETIME std::start_lifetime_as_array
template <class T> T* forward_start(unsigned char* buf) { return std::start_lifetime_as_array<T>(buf, 1); }
EOF
    # Two files that a header includes, each with a suffix that is not a
    # source suffix: one through the digraph of #, and one through a
    # directive with a comment inside it.
    cat >"$tmp/misc/Hidden.txt" <<'EOF'
inline void* hidden(unsigned char* buf) { return std::start_lifetime_as<Proof[9]>(buf); }
EOF
    cat >"$tmp/misc/Other.txt" <<'EOF'
inline void* other(unsigned char* buf) { return std::start_lifetime_as<Proof[12]>(buf); }
EOF
    # A module interface unit, a suffix of its own.
    cat >"$tmp/include/planted/Unit.cppm" <<'EOF'
export module planted;
export inline void* unit(unsigned char* buf) { return std::start_lifetime_as<Proof[10]>(buf); }
EOF
    # Text that names the function but uses it nowhere.  None of it may be
    # reported.
    cat >"$tmp/include/planted/Quiet.h" <<'EOF'
#pragma once
// std::start_lifetime_as<Proof[5]>(buf) in a line comment
/* std::start_lifetime_as_array<Proof>(buf, 5) in a block comment */
inline const char* quiet_string = "std::start_lifetime_as<Proof[6]>(buf)";
inline const char* quiet_raw = R"delim(std::start_lifetime_as<Proof[7]>(buf) ")" )delim";
inline constexpr long quiet_digits = 1'000'000;
inline constexpr char quiet_quote = '\'';
inline int my_start_lifetime_as_helper() { return 0; }
EOF
    # A header outside the usual source trees is in scope too.
    cat >"$tmp/lib/Stray.h" <<'EOF'
#pragma once
inline void* stray(unsigned char* buf) { return std::start_lifetime_as<Proof[8]>(buf); }
EOF
    # A macro that pastes the name from two halves.  The lexer sees two
    # identifiers, and only the preprocessed pass sees the expansion.
    cat >"$tmp/include/planted/Pasted.h" <<'EOF'
#pragma once
#define PASTED_LIFETIME(T, p) std::start_life ## time_as<T>(p)
EOF
    # The three admitted places: a frozen file, a negative fixture and the
    # checked start.  Each has a reviewed entry.
    cat >"$tmp/include/planted/frozen/Reviewed.h" <<'EOF'
#pragma once
struct Event { int value; };
inline Event* reviewed(unsigned char* storage) {
    auto* moved = std::start_lifetime_as_array<Event>(storage, 4);
    return moved;
}
EOF
    # A frozen header whose one macro body expands twice: the lexical pass
    # sees one use, and the preprocessed pass sees two.  It also has an arm
    # that this host does not compile: the lexical pass sees two uses, and
    # the preprocessed pass sees one.  Each count is the larger one.
    cat >"$tmp/include/planted/frozen/Twice.h" <<'EOF'
#pragma once
#define TWICE_LIFETIME(p) std::start_lifetime_as<Proof[18]>(p)
inline void* twice_first(unsigned char* buf) { return TWICE_LIFETIME(buf); }
inline void* twice_second(unsigned char* buf) { return TWICE_LIFETIME(buf); }
#if 0
inline void* other_host(unsigned char* buf) { return std::start_lifetime_as<Proof[19]>(buf); }
#endif
inline void* this_host(unsigned char* buf) { return std::start_lifetime_as<Proof[19]>(buf); }
EOF
    cat >"$tmp/test/neg/fixture.cpp" <<'EOF'
struct Proof;
inline void* fixture(unsigned char* buf) { return std::start_lifetime_as<Proof>(buf); }
EOF
    # A generated source in a build directory, with no source suffix.
    # Only the compile database brings it into scope.
    cat >"$tmp/build-planted/generated.gen" <<'EOF'
inline void* generated(unsigned char* buf) { return std::start_lifetime_as<Proof[11]>(buf); }
EOF
    cat >"$tmp/scripts/start-lifetime-allowlist.txt" <<'EOF'
# The planted reviewed site in a frozen file.
include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>
# The planted negative fixture.
test/neg/fixture.cpp:start_lifetime_as<Proof>
# The frozen macro header, each key reviewed for one use.
include/planted/frozen/Twice.h:start_lifetime_as<Proof[18]>
include/planted/frozen/Twice.h:start_lifetime_as<Proof[19]>
# An entry for a path that is none of the three places.  It cannot admit.
include/planted/Routes.h:start_lifetime_as_array<Listed>
EOF

    local lexical_keys=(
        'include/planted/Routes.h:start_lifetime_as<Proof[1]>'
        'include/planted/Routes.h:start_lifetime_as_array<Proof>'
        'include/planted/Routes.h:start_lifetime_as<Holder<Proof>>'
        'include/planted/Routes.h:start_lifetime_as<std::array<Proof,1>>'
        'include/planted/Routes.h:start_lifetime_as<Proof[2]>'
        'include/planted/Routes.h:start_lifetime_as<Proof[3]>'
        'include/planted/Routes.h:start_lifetime_as<Proof[4]>'
        'include/planted/Routes.h:start_lifetime_as_array<Listed>'
        'include/planted/Routes.h:start_lifetime_as'
        'include/planted/Routes.h:start_lifetime_as_array'
        'include/planted/Routes.h:start_lifetime_as_array<T>'
        'misc/Hidden.txt:start_lifetime_as<Proof[9]>'
        'misc/Other.txt:start_lifetime_as<Proof[12]>'
        'include/planted/Unit.cppm:start_lifetime_as<Proof[10]>'
        'lib/Stray.h:start_lifetime_as<Proof[8]>'
        'include/planted/frozen/Twice.h:start_lifetime_as<Proof[19]> x2'
    )
    run_planted
    [[ $rc -eq 1 ]] || { self_test_fail "the planted routes gave exit $rc, not 1."; return 2; }
    expect_reports 'Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|generated\.gen' "${lexical_keys[@]}" \
        || { rg -N '' "$out" >&2 || true; return 2; }
    rg -q -F 'refused entry: start-lifetime-allowlist.txt:9 names include/planted/Routes.h' "$out" \
        || { self_test_fail 'the entry for a path outside the three places was not refused.'; return 2; }
    printf 'check-start-lifetime --self-test: each route, the spacing, comment, continuation, alias, macro and template forms, an included file with any suffix, a module unit and a header outside the source trees are reported.  Comments, literals, the frozen site and the fixture are not, and an entry cannot admit a path outside the three places.\n'

    # The preprocessed pass.  A translation unit expands the pasted name,
    # includes a file through a macro operand and a header that only its -I
    # flag finds, and names the function inside a literal.  The lexical
    # pass sees none of the three uses.
    cat >"$tmp/src/planted.cpp" <<'EOF'
#include "../include/planted/Pasted.h"
#include "../include/planted/frozen/Twice.h"
#define PLANTED_HEADER "../misc/MacroIncluded.txt"
#include PLANTED_HEADER
#include <Generated.h>
inline void* pasted(unsigned char* buf) { return PASTED_LIFETIME(Proof[13], buf); }
inline const char* mention = "std::start_lifetime_as<Proof[16]>(buf)";
EOF
    cat >"$tmp/misc/MacroIncluded.txt" <<'EOF'
inline void* macro_included(unsigned char* buf) { return std::start_lifetime_as<Proof[14]>(buf); }
EOF
    cat >"$tmp/build-planted/gen/Generated.h" <<'EOF'
inline void* generated_header(unsigned char* buf) { return std::start_lifetime_as<Proof[15]>(buf); }
EOF
    printf '[{"directory": "%s", "file": "build-planted/generated.gen", "command": "%s -x c++ -c build-planted/generated.gen -o build-planted/generated.o"},\n {"directory": "%s", "file": "src/planted.cpp", "command": "%s -std=c++20 -Ibuild-planted/gen -MD -MF build-planted/planted.d -c src/planted.cpp -o build-planted/planted.o"}]\n' \
        "$tmp" "$cxx" "$tmp" "$cxx" >"$tmp/build-planted/compile_commands.json"
    local preprocessed_keys=(
        "${lexical_keys[@]}"
        'build-planted/generated.gen:start_lifetime_as<Proof[11]>'
        'src/planted.cpp:start_lifetime_as<Proof[13]>'
        'misc/MacroIncluded.txt:start_lifetime_as<Proof[14]>'
        'build-planted/gen/Generated.h:start_lifetime_as<Proof[15]>'
        'include/planted/frozen/Twice.h:start_lifetime_as<Proof[18]> x2'
    )
    run_planted --compile-db "$tmp/build-planted/compile_commands.json"
    [[ $rc -eq 1 ]] || { self_test_fail "the preprocessed run gave exit $rc, not 1."; return 2; }
    expect_reports 'Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|Proof\[16\]' "${preprocessed_keys[@]}" \
        || { rg -N '' "$out" >&2 || true; return 2; }
    rg -q 'preprocessed 2 translation unit\(s\) in [0-9.]+ s, 0 from the cache' "$out" \
        || { self_test_fail 'the first preprocessed run did not run both translation units.'; return 2; }
    printf 'check-start-lifetime --self-test: the preprocessed pass reports the pasted name, the macro include, the -I header, a generated source and each expansion of a macro body, and not a name inside a literal.\n'

    # The cache gives the same verdict, and a change to a file that a
    # translation unit read makes its entry stale.
    run_planted --compile-db "$tmp/build-planted/compile_commands.json"
    rg -q 'preprocessed 2 translation unit\(s\) in [0-9.]+ s, 2 from the cache' "$out" \
        || { self_test_fail 'the second preprocessed run did not read the cache.'; return 2; }
    expect_reports 'Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|Proof\[16\]' "${preprocessed_keys[@]}" \
        || { rg -N '' "$out" >&2 || true; return 2; }
    printf 'inline void* later(unsigned char* buf) { return std::start_lifetime_as<Proof[17]>(buf); }\n' \
        >>"$tmp/misc/MacroIncluded.txt"
    run_planted --compile-db "$tmp/build-planted/compile_commands.json"
    rg -q 'preprocessed 2 translation unit\(s\) in [0-9.]+ s, 1 from the cache' "$out" \
        && report_ends_with 'key: misc/MacroIncluded.txt:start_lifetime_as<Proof[17]>' "$out" \
        || { self_test_fail 'a changed dependency did not make its cache entry stale.'; return 2; }
    printf 'check-start-lifetime --self-test: the cache gives the same verdict, and a changed dependency makes its entry stale.\n'

    # A translation unit the preprocessor cannot read refuses the run.
    printf '#include "absent.h"\n' >"$tmp/src/broken.cpp"
    printf '[{"directory": "%s", "file": "src/broken.cpp", "command": "%s -c src/broken.cpp -o broken.o"}]\n' \
        "$tmp" "$cxx" >"$tmp/build-planted/broken_commands.json"
    rm -f "$tmp/include/planted/frozen/Twice.h" "$tmp/include/planted/Routes.h" "$tmp/lib/Stray.h" "$tmp/misc/Hidden.txt" "$tmp/misc/Other.txt" \
        "$tmp/include/planted/Unit.cppm" "$tmp/src/planted.cpp"
    printf '%s\n' '# The planted reviewed site.' 'include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>' \
        '# The planted fixture.' 'test/neg/fixture.cpp:start_lifetime_as<Proof>' \
        >"$tmp/scripts/start-lifetime-allowlist.txt"
    run_planted --compile-db "$tmp/build-planted/broken_commands.json"
    [[ $rc -eq 2 ]] && rg -q -F 'preprocessor failure: src/broken.cpp' "$out" \
        || { self_test_fail "a preprocessor failure gave exit $rc without its report."; return 2; }
    printf 'check-start-lifetime --self-test: a translation unit the preprocessor cannot read refuses the run, as expected.\n'
    rm -f "$tmp/src/broken.cpp"

    # The reviewed key survives a line shift and a new variable name.
    cat >"$tmp/include/planted/frozen/Reviewed.h" <<'EOF'


#pragma once
struct Event { int value; };
inline Event* reviewed(unsigned char* storage) {
    auto* shifted = std::start_lifetime_as_array<Event>(storage, 4);
    return shifted;
}
EOF
    run_planted
    [[ $rc -eq 0 ]] || { self_test_fail "the reviewed key did not survive a line shift (exit $rc)."; return 2; }
    printf 'check-start-lifetime --self-test: the reviewed key survives a line shift and a rename, as expected.\n'

    # A second use under the reviewed key needs its own review.
    cat >>"$tmp/include/planted/frozen/Reviewed.h" <<'EOF'
inline Event* second(unsigned char* storage) { return std::start_lifetime_as_array<Event>(storage, 1); }
EOF
    run_planted
    [[ $rc -eq 1 ]] && rg -q -F 'and its entry admits 1.  Allowlist key: include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event> x2' "$out" \
        || { self_test_fail "a second use under a reviewed key gave exit $rc without its report."; return 2; }
    printf 'check-start-lifetime --self-test: a second use under a reviewed key is reported, as expected.\n'

    # An entry that admits more uses than the tree has is stale, and so is
    # an entry that names no use.
    printf '%s\n' '# The planted reviewed sites, one count too high.' \
        'include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event> x3' \
        '# The planted fixture.' 'test/neg/fixture.cpp:start_lifetime_as<Proof>' \
        '# A planted stale entry.' 'include/planted/frozen/Reviewed.h:start_lifetime_as<Gone>' \
        >"$tmp/scripts/start-lifetime-allowlist.txt"
    run_planted
    [[ $rc -eq 2 ]] && rg -q -F 'admits 3 use(s) of include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>, and the tree has 2.' "$out" \
        && rg -q -F 'admits 1 use(s) of include/planted/frozen/Reviewed.h:start_lifetime_as<Gone>, and the tree has 0.' "$out" \
        || { self_test_fail "stale entries gave exit $rc without their reports."; return 2; }
    printf 'check-start-lifetime --self-test: an entry above its count and an entry with no use are reported, as expected.\n'

    # With no frozen declaration, nothing is frozen, so the frozen site is
    # refused.  The rule fails closed.
    rm -f "$tmp/scripts/check-frozen-tree.sh"
    run_planted
    [[ $rc -eq 1 ]] && rg -q -F 'include/planted/frozen/Reviewed.h:' "$out" \
        || { self_test_fail "a missing frozen declaration did not refuse the frozen site (exit $rc)."; return 2; }
    printf 'check-start-lifetime --self-test: a missing frozen declaration freezes nothing, as expected.\n'
    printf 'check-start-lifetime --self-test: PASS.\n'
}

mode=check
while [[ $# -gt 0 ]]; do
    case "$1" in
        --compile-db) [[ $# -ge 2 ]] || { usage; exit 2; }; compile_db="$2"; shift 2 ;;
        --list)       mode=list; shift ;;
        --self-test)  mode=self-test; shift ;;
        -h|--help)    usage; exit 0 ;;
        *)            usage; exit 2 ;;
    esac
done
if [[ -n "$compile_db" && ! -f "$compile_db" ]]; then
    printf 'check-start-lifetime: the compile database %s does not exist.\n' "$compile_db" >&2
    exit 2
fi
case "$mode" in
    check)     run_scan check ;;
    list)      run_scan list ;;
    self-test) self_test ;;
esac
