#!/usr/bin/env python3
"""check-header-checks — a header does not check itself again in each translation unit that includes it.

A self-test namespace and a static_assert at namespace scope run again in
each translation unit that includes their header.  On the tree of
2026-10-01 they cost about half of the front-end time of a file.  Such a
check lives in the check file of its header, which one translation unit
compiles one time.  A static_assert inside a class or a template stays in
the header, because it applies to each instantiation.

THE CHECK FILE
    The check file of include/<layer>/<path>.h is
    test/layer/checks/<layer>/<path>.cpp.  Its first line of code
    includes its own header, so the file also shows that the header
    compiles alone.  It holds the self-test namespaces and the
    namespace-scope static_asserts of the header, unchanged and inside the
    same enclosing namespaces, so that each name resolves as before.
    test/layer/CMakeLists.txt compiles the check file in place of the
    one-line sentinel of its header, against the include root of its
    layer, in the default build.

WHAT THE GUARD READS
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py),
    for each header under include/ and each file under test/layer/checks/.
      * A self-test namespace is a namespace definition whose name has a
        segment with the word `test` in it, when the segment is split at
        `_`: x_self_test, self_test and fn_test are self-test namespaces.
        `testing` is a different word, so foundation::effects::testing is
        not one.  A self-test namespace inside another one counts with
        the outer one.
      * A namespace-scope static_assert is a static_assert declaration
        that no class, function, lambda or other block holds.  A namespace
        body, an extern "C++" body and an arm of a preprocessor
        conditional are namespace scope.  A static_assert inside a
        self-test namespace counts with that namespace, not alone.

THE LEDGER
    utils/scripts/header-checks-ledger.txt holds two kinds of row.
      path | self-test namespaces | namespace-scope static_asserts
          The checks that the header still holds.  A count above its row
          fails: move the new check to the check file.  A count below its
          row also fails: regenerate the row with --write in the same
          commit.  A header with no check has no row.
      keep | path | namespace or static_assert | key | reason
          One check that stays in its header, because its result depends
          on the translation unit that includes the header.  The key of a
          namespace is its full name.  The key of a static_assert is the
          spelling of its condition, as --list prints it.  A keep row that
          names no check fails, and a keep row with no reason fails.  A
          reason does not contain ` | `.

THE CHECK FILES
    Each file under test/layer/checks/ is a .cpp file, its header exists,
    and its first line of code includes that header.

THE HEADERS THAT CANNOT COMPILE ALONE
    test/layer/crucible-not-standalone.txt names each crucible header that
    has no sentinel, one row `crucible/<path>.h | reason` each.  With
    --standalone BUILD_DIR, the guard compiles each listed header alone,
    with the command of the crucible sentinels from the compile database of
    BUILD_DIR, and it fails for each header that compiles.  A header that
    compiles alone gets its sentinel back, so the list can only become
    shorter.

WHAT THE GUARD CANNOT SEE
    A static_assert in the body of a macro has no scope until the macro
    expands, so the guard does not count it.  Every arm of an #if counts,
    because the kit does not preprocess.

Usage
    check-header-checks.py                         compare the tree with the ledger
    check-header-checks.py --list                  print each check that a header holds
    check-header-checks.py --write                 write the count rows of the ledger again from the tree
    check-header-checks.py --standalone BUILD_DIR  compile each listed header alone and refuse one that compiles
    check-header-checks.py --self-test             plant each kind of check in a scratch tree and examine each verdict

Exit 0 clean, 1 on a new check, a bad check file, a listed header that
compiles alone or a parse failure, 2 on a stale or malformed row, a
missing compile command, a usage error or a failed self-test, 3 when the
kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

SCRIPT = "utils/scripts/check-header-checks.py"
LEDGER = "utils/scripts/header-checks-ledger.txt"
INCLUDE = "include"
CHECKS = "test/layer/checks"
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
SENTINEL_TARGET = "layer_sentinel_crucible"
HEADER_SUFFIXES = (".h", ".hpp")
NAMESPACE = "namespace"
ASSERT = "static_assert"
KINDS = (NAMESPACE, ASSERT)
SEPARATOR = " | "
# The node types whose body is namespace scope.  A static_assert with an
# ancestor of any other type sits in a class, a function, a lambda or a
# block.
NAMESPACE_SCOPE = frozenset({
    "translation_unit", "declaration_list", "namespace_definition", "linkage_specification",
    "preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef",
})
LEDGER_HEADER = (
    "# utils/scripts/header-checks-ledger.txt — the compile-time checks that each header under include/\n"
    "# still holds.  utils/scripts/check-header-checks.py reads this ledger.\n"
    "#\n"
    "# A header holds no self-test namespace and no static_assert at namespace scope.  Such a check\n"
    "# runs again in each translation unit that includes the header.  It belongs in the check file of\n"
    "# the header, which one translation unit compiles one time: test/layer/checks/<layer>/<path>.cpp\n"
    "# for include/<layer>/<path>.h.  The first line of code of a check file includes its header.\n"
    "#\n"
    "# A count row:  path | self-test namespaces | namespace-scope static_asserts\n"
    "#   The counts can only decrease.  A header with no check has no row.  When you move a check, run\n"
    "#   python3 utils/scripts/check-header-checks.py --write in the same commit.\n"
    "#\n"
    "# A keep row:   keep | path | namespace or static_assert | key | reason\n"
    "#   One check that stays in its header, because its result depends on the translation unit that\n"
    "#   includes the header.  The key of a namespace is its full name.  The key of a static_assert is\n"
    "#   the spelling of its condition, as --list prints it.  The reason is mandatory.\n"
)


@dataclass(frozen=True)
class Check:
    """One self-test namespace or one namespace-scope static_assert of a header."""

    path: str
    row: int
    kind: str
    key: str


@dataclass(frozen=True)
class Keep:
    """One keep row of the ledger."""

    path: str
    kind: str
    key: str
    reason: str
    line: int


@dataclass
class Ledger:
    """The rows of the ledger, and one message for each malformed row.

    bad_keeps holds the messages of the malformed keep rows only, because
    --write keeps each keep row and cannot keep one that it cannot read.
    """

    counts: dict[str, tuple[int, int]]
    keeps: list[Keep]
    malformed: list[str]
    bad_keeps: list[str]


@dataclass
class Scan:
    """What the guard reads from one tree."""

    checks: list[Check]
    headers: frozenset[str]
    bad_check_files: list[str]
    failures: list[str]


def is_self_test_segment(segment: str) -> bool:
    """Say whether one segment of a namespace name names a self-test namespace.

    Args:
        segment: One identifier of a namespace name

    Returns:
        True when `test` is one of the words of the segment, split at `_`
    """
    return "test" in segment.split("_")


def is_at_namespace_scope(node: tsast.Node) -> bool:
    """Say whether no class, function, lambda or block holds a node.

    Complexity: linear in the depth of the node.

    Args:
        node: A static_assert_declaration

    Returns:
        True when each ancestor is a namespace body, a linkage body, an arm
        of a preprocessor conditional or the translation unit
    """
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE:
            return False
        owner = owner.parent
    return True


def check_file_of(header: str) -> str:
    """Return the check file of a header.

    Args:
        header: A repo-relative path under include/

    Returns:
        test/layer/checks/<layer>/<path>.cpp for include/<layer>/<path>.h
    """
    inner = Path(header).relative_to(INCLUDE)
    return (Path(CHECKS) / inner.with_suffix(".cpp")).as_posix()


def header_of_check(root: Path, check_file: str) -> str | None:
    """Return the header of a check file, or None when no header of that name exists.

    Args:
        root: The repository root
        check_file: A repo-relative path under test/layer/checks/

    Returns:
        The repo-relative header path
    """
    inner = Path(check_file).relative_to(CHECKS)
    for suffix in HEADER_SUFFIXES:
        header = (Path(INCLUDE) / inner.with_suffix(suffix)).as_posix()
        if (root / header).is_file():
            return header
    return None


def header_checks(tree: tsast.Tree, rel: str) -> tuple[list[Check], list[str]]:
    """Return each self-test namespace and each namespace-scope static_assert of one header.

    Complexity: linear in the number of nodes of the file.

    Args:
        tree: The parse tree of the header
        rel: The header, relative to the repository root

    Returns:
        The checks in source order, and one message for each namespace
        whose name the guard cannot read
    """
    checks: list[Check] = []
    unread: list[str] = []
    for node in tree.find("namespace_definition"):
        name = node.child_by_field("name")
        if name is None:
            continue
        parts = tsast.qualified_parts(name)
        if parts is None:
            unread.append(f"{rel}:{node.line}: the guard cannot read the name of this namespace, so it cannot "
                          f"tell whether it is a self-test namespace.")
            continue
        enclosing = tsast.namespace_path(node)
        if any(is_self_test_segment(segment) for segment in enclosing):
            continue
        if any(is_self_test_segment(segment) for segment in parts[1]):
            checks.append(Check(rel, node.line, NAMESPACE, "::".join((*enclosing, *parts[1]))))
    for node in tree.find("static_assert_declaration"):
        if not is_at_namespace_scope(node):
            continue
        if any(is_self_test_segment(segment) for segment in tsast.namespace_path(node)):
            continue
        condition = node.child_by_field("condition")
        checks.append(Check(rel, node.line, ASSERT, tsast.spelled(condition if condition is not None else node)))
    checks.sort(key=lambda check: (check.row, check.kind))
    return checks, unread


def first_include(tree: tsast.Tree) -> str | None:
    """Return the path that the first line of code of a file includes, or None.

    Args:
        tree: The parse tree of a check file

    Returns:
        The include path without its delimiters, or None when the first
        item that is not a comment is not an include of a written path
    """
    items = tsast.non_comment_children(tree.root)
    if not items or items[0].type != "preproc_include":
        return None
    target = items[0].child_by_field("path")
    if target is None or target.type not in ("system_lib_string", "string_literal"):
        return None
    return tsast.prose_text(target).strip()[1:-1].strip()


def scan(root: Path) -> Scan:
    """Read every header under include/ and every file under test/layer/checks/.

    Complexity: linear in the total size of the files read.

    Args:
        root: The repository root

    Returns:
        The checks, the set of headers, each bad check file and each parse failure

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    base = root / INCLUDE
    headers = sorted(path for path in base.rglob("*") if path.is_file() and path.suffix in HEADER_SUFFIXES
                     and tsast.is_in_cpp_scope(path.relative_to(root))) if base.is_dir() else []
    checks: list[Check] = []
    failures: list[str] = []
    for tree in tsast.parse(headers, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file, so the guard cannot count its checks.  "
                            f"{tree.diagnostic.strip()}")
            continue
        found, unread = header_checks(tree, rel)
        checks.extend(found)
        failures.extend(unread)

    bad: list[str] = []
    check_root = root / CHECKS
    sources: list[Path] = []
    expected: dict[str, str] = {}
    for path in sorted(check_root.rglob("*")) if check_root.is_dir() else []:
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if path.suffix != ".cpp":
            bad.append(f"{rel}: a check file is a .cpp file: {CHECKS}/<layer>/<path>.cpp for "
                       f"include/<layer>/<path>.h.  Rename the file, or move it out of {CHECKS}/.")
            continue
        header = header_of_check(root, rel)
        if header is None:
            bad.append(f"{rel}: no header include/{Path(rel).relative_to(CHECKS).with_suffix('.h').as_posix()} "
                       f"exists.  A check file belongs to one header.  Move it with its header, or delete it.")
            continue
        sources.append(path)
        expected[rel] = Path(header).relative_to(INCLUDE).as_posix()
    for tree in tsast.parse(sources, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this check file.  {tree.diagnostic.strip()}")
            continue
        included = first_include(tree)
        if included != expected[rel]:
            shown = f"<{included}>" if included is not None else "no written include path"
            bad.append(f"{rel}: the first line of code includes {shown}, not <{expected[rel]}>.  The first line "
                       f"of code of a check file includes its own header, so the file shows that the header "
                       f"compiles alone.")
    return Scan(checks, frozenset(Path(path).relative_to(root).as_posix() for path in headers), bad, failures)


def read_ledger(path: Path) -> Ledger:
    """Read the ledger.

    Args:
        path: The ledger file

    Returns:
        The count rows, the keep rows and one message for each malformed row
    """
    ledger = Ledger({}, [], [], [])
    if not path.is_file():
        return ledger
    shape = (f"A count row is `path{SEPARATOR}self-test namespaces{SEPARATOR}static_asserts`, and a keep row is "
             f"`keep{SEPARATOR}path{SEPARATOR}namespace or static_assert{SEPARATOR}key{SEPARATOR}reason`.")
    keys: set[tuple[str, str, str]] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        where = f"{LEDGER}:{number}"
        # The space after the entry keeps an empty last cell, so that a
        # keep row that ends in its separator has an empty reason.
        cells = [cell.strip() for cell in (entry + " ").split(SEPARATOR)]
        if cells[0] == "keep":
            problem = ""
            if len(cells) < 5 or cells[2] not in KINDS or not cells[1].startswith(f"{INCLUDE}/"):
                problem = f"MALFORMED {where}: {entry}  {shape}"
            elif not cells[-1]:
                problem = (f"MALFORMED {where}: the keep row gives no reason.  Say why the check depends on the "
                           f"translation unit that includes the header.")
            elif (cells[1], cells[2], SEPARATOR.join(cells[3:-1])) in keys:
                problem = f"DUPLICATE {where}: a keep row for this check comes before this one."
            if problem:
                ledger.malformed.append(problem)
                ledger.bad_keeps.append(problem)
                continue
            keep = Keep(cells[1], cells[2], SEPARATOR.join(cells[3:-1]), cells[-1], number)
            keys.add((keep.path, keep.kind, keep.key))
            ledger.keeps.append(keep)
            continue
        if len(cells) != 3 or not cells[1].isdigit() or not cells[2].isdigit():
            ledger.malformed.append(f"MALFORMED {where}: {entry}  {shape}")
            continue
        if cells[0] in ledger.counts:
            ledger.malformed.append(f"DUPLICATE {where}: {cells[0]} has a count row before this one.")
            continue
        namespaces, asserts = int(cells[1]), int(cells[2])
        if namespaces == 0 and asserts == 0:
            ledger.malformed.append(f"MALFORMED {where}: the row counts no check.  A header with no check has no "
                                    f"row.  Run: python3 {SCRIPT} --write")
            continue
        ledger.counts[cells[0]] = (namespaces, asserts)
    return ledger


def apply_keeps(checks: list[Check], keeps: list[Keep]) -> tuple[list[Check], list[Keep]]:
    """Remove each kept check.

    Args:
        checks: Every check of the tree
        keeps: The keep rows

    Returns:
        The checks that no keep row names, and each keep row that names no check
    """
    wanted = {(keep.path, keep.kind, keep.key) for keep in keeps}
    matched: set[tuple[str, str, str]] = set()
    remaining: list[Check] = []
    for check in checks:
        identity = (check.path, check.kind, check.key)
        if identity in wanted:
            matched.add(identity)
        else:
            remaining.append(check)
    stale = [keep for keep in keeps if (keep.path, keep.kind, keep.key) not in matched]
    return remaining, stale


def per_header(checks: list[Check]) -> dict[str, tuple[int, int]]:
    """Count the self-test namespaces and the namespace-scope static_asserts of each header."""
    counts: dict[str, list[int]] = {}
    for check in checks:
        slot = counts.setdefault(check.path, [0, 0])
        slot[0 if check.kind == NAMESPACE else 1] += 1
    return {path: (pair[0], pair[1]) for path, pair in counts.items()}


def describe(check: Check) -> str:
    """Return one line that names a check and its place."""
    if check.kind == NAMESPACE:
        return f"{check.path}:{check.row}: the self-test namespace {check.key}"
    return f"{check.path}:{check.row}: a namespace-scope static_assert({check.key})"


def check(root: Path, ledger_path: Path) -> int:
    """Compare the tree with the ledger and print the report.

    Args:
        root: The repository root
        ledger_path: The ledger file

    Returns:
        0 clean, 1 on a new check, a bad check file or a parse failure, 2 on a stale or malformed row
    """
    found = scan(root)
    ledger = read_ledger(ledger_path)
    checks, stale_keeps = apply_keeps(found.checks, ledger.keeps)
    counts = per_header(checks)
    over = 0
    stale = 0
    for path in sorted(set(counts) | set(ledger.counts)):
        have = counts.get(path, (0, 0))
        allowed = ledger.counts.get(path, (0, 0))
        if path in ledger.counts and path not in found.headers:
            stale += 1
            print(f"STALE     {LEDGER}: the row names {path}, which is not a header under include/.  Run: "
                  f"python3 {SCRIPT} --write", file=sys.stderr)
            continue
        if have[0] > allowed[0] or have[1] > allowed[1]:
            over += 1
            print(f"NEW CHECK {path} holds {have[0]} self-test namespace(s) and {have[1]} namespace-scope "
                  f"static_assert(s), and the ledger permits {allowed[0]} and {allowed[1]}.  Move each check, "
                  f"unchanged and inside the same enclosing namespaces, to the check file {check_file_of(path)}, "
                  f"whose first line of code includes <{Path(path).relative_to(INCLUDE).as_posix()}>.  A "
                  f"static_assert inside a class or a template stays in the header.  A check whose result depends "
                  f"on the translation unit that includes the header can stay, with a keep row and its reason.",
                  file=sys.stderr)
            for item in checks:
                if item.path == path and (item.kind == NAMESPACE and have[0] > allowed[0]
                                          or item.kind == ASSERT and have[1] > allowed[1]):
                    print(f"            {describe(item)}", file=sys.stderr)
        elif have != allowed:
            stale += 1
            print(f"STALE     {path} holds {have[0]} self-test namespace(s) and {have[1]} namespace-scope "
                  f"static_assert(s), and its row says {allowed[0]} and {allowed[1]}.  Regenerate this row in the "
                  f"same commit: python3 {SCRIPT} --write", file=sys.stderr)
    for keep in stale_keeps:
        stale += 1
        print(f"STALE     {LEDGER}:{keep.line}: the keep row names no {keep.kind} {keep.key} in {keep.path}.  "
              f"Remove the row, or correct its key: python3 {SCRIPT} --list prints each key.", file=sys.stderr)
    for line in found.bad_check_files:
        print(f"CHECK FILE {line}", file=sys.stderr)
    for line in found.failures + ledger.malformed:
        print(line, file=sys.stderr)
    total = per_header(found.checks)
    print(f"check-header-checks: {len(total)} header(s) hold {sum(pair[0] for pair in total.values())} self-test "
          f"namespace(s) and {sum(pair[1] for pair in total.values())} namespace-scope static_assert(s), "
          f"{len(found.checks) - len(checks)} of them kept.  The ledger permits "
          f"{sum(pair[0] for pair in ledger.counts.values())} and {sum(pair[1] for pair in ledger.counts.values())} "
          f"in {len(ledger.counts)} header(s).  {over} over, {stale} stale, {len(found.bad_check_files)} bad check "
          f"file(s).", file=sys.stderr)
    if over or found.bad_check_files or found.failures:
        return 1
    return 2 if stale or ledger.malformed else 0


def write(root: Path, ledger_path: Path) -> int:
    """Write the count rows of the ledger again from the tree, and keep each keep row.

    Args:
        root: The repository root
        ledger_path: The ledger file

    Returns:
        0 when the ledger is written, 1 when a parse failure or a malformed keep row stops the write
    """
    found = scan(root)
    ledger = read_ledger(ledger_path)
    if found.failures or ledger.bad_keeps:
        for line in found.failures + ledger.bad_keeps:
            print(line, file=sys.stderr)
        print("check-header-checks: --write does not write the ledger while a file does not parse or a keep row "
              "is malformed.", file=sys.stderr)
        return 1
    checks, _stale_keeps = apply_keeps(found.checks, ledger.keeps)
    counts = per_header(checks)
    rows = [f"{path}{SEPARATOR}{counts[path][0]}{SEPARATOR}{counts[path][1]}\n" for path in sorted(counts)]
    keeps = [f"keep{SEPARATOR}{keep.path}{SEPARATOR}{keep.kind}{SEPARATOR}{keep.key}{SEPARATOR}{keep.reason}\n"
             for keep in sorted(ledger.keeps, key=lambda keep: (keep.path, keep.kind, keep.key))]
    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    ledger_path.write_text(LEDGER_HEADER + "".join(rows) + ("\n" + "".join(keeps) if keeps else ""),
                           encoding="utf-8")
    print(f"check-header-checks: ledger written with {len(rows)} count row(s) and {len(keeps)} keep row(s).",
          file=sys.stderr)
    return 0


def list_checks(root: Path, ledger_path: Path) -> int:
    """Print each check that a header holds, with the key that a keep row names.

    Returns:
        0, or 1 on a parse failure
    """
    found = scan(root)
    kept = {(keep.path, keep.kind, keep.key) for keep in read_ledger(ledger_path).keeps}
    for item in found.checks:
        marker = "KEPT" if (item.path, item.kind, item.key) in kept else "HELD"
        print(f"{marker}  {item.path}:{item.row}  {item.kind}  {item.key}")
    for line in found.failures:
        print(line)
    return 1 if found.failures else 0


def read_not_standalone(root: Path) -> tuple[list[tuple[str, int]], list[str]]:
    """Read the list of the crucible headers that cannot compile alone.

    Args:
        root: The repository root

    Returns:
        (header, line) for each row, with the header relative to include/,
        and one message for each malformed row
    """
    rows: list[tuple[str, int]] = []
    problems: list[str] = []
    path = root / NOT_STANDALONE
    if not path.is_file():
        return rows, [f"MALFORMED {NOT_STANDALONE}: the list does not exist."]
    seen: set[str] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        header, separator, reason = entry.partition(SEPARATOR)
        header = header.strip()
        where = f"{NOT_STANDALONE}:{number}"
        if not separator or not reason.strip():
            problems.append(f"MALFORMED {where}: {entry}  A row is `crucible/<path>.h{SEPARATOR}reason`, and the "
                            f"reason is mandatory.")
        elif not header.startswith("crucible/") or not (root / INCLUDE / header).is_file():
            problems.append(f"STALE     {where}: {header} is not a header under include/crucible.  Remove the row.")
        elif header in seen:
            problems.append(f"DUPLICATE {where}: {header} has a row before this one.")
        else:
            seen.add(header)
            rows.append((header, number))
    return rows, problems


def sentinel_command(build_dir: Path) -> tuple[list[str], str, str] | None:
    """Return the compile command of one crucible sentinel from the compile database.

    Args:
        build_dir: A configured build directory

    Returns:
        (argv, source, directory), or None when the database holds no unit
        of the crucible sentinels
    """
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return None
    for row in json.loads(database.read_text(encoding="utf-8")):
        if f"/CMakeFiles/{SENTINEL_TARGET}.dir/" in row.get("output", ""):
            argv = row.get("arguments") or shlex.split(row["command"])
            return argv, row["file"], row["directory"]
    return None


def syntax_argv(argv: list[str], source: str, replacement: Path) -> list[str]:
    """Return a compile command that only checks the syntax of another source.

    The output and dependency-file flags go, so the run writes nothing.

    Args:
        argv: The compile command of one unit
        source: The source path that the command names
        replacement: The source to compile in its place

    Returns:
        The command with -fsyntax-only
    """
    out: list[str] = []
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
            continue
        if flag in ("-c", "-MD", "-MMD", "-MP"):
            index += 1
            continue
        out.append(str(replacement) if flag == source else flag)
        index += 1
    return out + ["-fsyntax-only"]


def standalone(root: Path, build_dir: Path) -> int:
    """Compile each listed header alone, and refuse each one that compiles.

    Complexity: one compiler run for each row, run in parallel.

    Args:
        root: The repository root
        build_dir: A configured build directory

    Returns:
        0 when each listed header still fails, 1 when one compiles, 2 on a
        malformed row or a missing compile command
    """
    rows, problems = read_not_standalone(root)
    for line in problems:
        print(line, file=sys.stderr)
    found = sentinel_command(build_dir)
    if found is None:
        print(f"check-header-checks: {build_dir}/compile_commands.json holds no unit of {SENTINEL_TARGET}.  "
              f"Configure the build first.", file=sys.stderr)
        return 2
    argv, source, directory = found
    with tempfile.TemporaryDirectory(prefix="header-checks-") as work:

        def compiles(item: tuple[int, tuple[str, int]]) -> bool:
            """Say whether one listed header compiles alone."""
            index, (header, _line) = item
            unit = Path(work) / f"alone_{index}.cpp"
            unit.write_text(f"#include <{header}>\n", encoding="utf-8")
            done = subprocess.run(syntax_argv(argv, source, unit), cwd=directory, capture_output=True, check=False)
            return done.returncode == 0

        with ThreadPoolExecutor(max_workers=min(16, max(1, len(rows)))) as pool:
            verdicts = list(pool.map(compiles, enumerate(rows)))
    alone = [row for row, verdict in zip(rows, verdicts) if verdict]
    for header, line in alone:
        print(f"STANDALONE {NOT_STANDALONE}:{line}: include/{header} compiles alone.  Remove its row in this commit, "
              f"so that the header gets its sentinel.", file=sys.stderr)
    print(f"check-header-checks: {len(rows)} listed header(s), {len(alone)} of them compile alone.",
          file=sys.stderr)
    if alone:
        return 1
    return 2 if problems else 0


def self_test() -> int:
    """Plant each kind of check in a scratch tree and make sure that each verdict is correct.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, holds: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    # Each line of the planted header, and how the guard reads it: a
    # namespace, a static_assert, or None for a line that counts nothing.
    planted: list[tuple[str, str | None, str]] = [
        ("#pragma once", None, ""),
        ("namespace foundation {", None, ""),
        ("static_assert(sizeof(int) == 4);", ASSERT, "a static_assert in a namespace body"),
        ("struct Holder { static_assert(sizeof(Holder*) == 8); };", None, "a static_assert in a class"),
        ("template <class T> struct Box { static_assert(sizeof(T) > 0); };", None, "a static_assert in a template"),
        ("inline void run() { static_assert(1 == 1); }", None, "a static_assert in a function body"),
        ("inline constexpr int probe = [] { static_assert(2 == 2); return 0; }();", None,
         "a static_assert in a lambda body"),
        ("// static_assert(false); in a comment", None, "a static_assert in a comment"),
        ('inline const char* text = "static_assert(false);";', None, "a static_assert in a string"),
        ("#if defined(PLANTED)", None, ""),
        ("static_assert(3 == 3);", ASSERT, "a static_assert in an arm of a preprocessor conditional"),
        ("#endif", None, ""),
        ('extern "C++" { static_assert(4 == 4); }', ASSERT, "a static_assert in an extern \"C++\" body"),
        ("namespace detail::planted_self_test {", NAMESPACE, "a namespace whose name ends in _self_test"),
        ("static_assert(5 == 5);", None, "a static_assert inside a self-test namespace"),
        ("namespace inner_self_test { static_assert(6 == 6); }", None,
         "a self-test namespace inside a self-test namespace"),
        ("}  // namespace detail::planted_self_test", None, ""),
        ("namespace self_test { struct Probe {}; }", NAMESPACE, "a namespace named self_test"),
        ("namespace fn_test { inline void f0() {} }", NAMESPACE, "a namespace whose name ends in _test"),
        ("namespace testing { struct Witness; }", None, "a namespace named testing"),
        ("namespace contest { struct Entry {}; }", None, "a namespace whose name only contains test"),
        ("}  // namespace foundation", None, ""),
        ('static_assert(7 == 7, "global scope");', ASSERT, "a static_assert at global scope"),
        ("#define PLANTED_CHECK static_assert(8 == 8)", None, "a static_assert in a macro body"),
    ]
    kept_header = "#pragma once\nnamespace crucible {\nstatic_assert(sizeof(long) == 8);\n" \
                  "static_assert(sizeof(char) == 1);\n}  // namespace crucible\n"
    keep_row = "keep | include/crucible/Kept.h | static_assert | sizeof(long)==8 | the planted reason"
    matching = ("include/crucible/Kept.h | 0 | 1\ninclude/foundation/Planted.h | 3 | 4\n" + keep_row + "\n")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        files = {
            "include/foundation/Planted.h": "\n".join(line for line, _, _ in planted) + "\n",
            "include/fixy/Clean.h": "#pragma once\nnamespace fixy { struct Clean {}; }\n",
            "include/crucible/Kept.h": kept_header,
            f"{CHECKS}/fixy/Clean.cpp": "// The checks of fixy/Clean.h.\n#include <fixy/Clean.h>\n\n"
                                        "namespace fixy { static_assert(sizeof(Clean) == 1); }\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True, exist_ok=True)

        def captured(action) -> tuple[int, str]:
            """Run one action and keep its report."""
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = action()
            return code, buffer.getvalue()

        found = scan(root)
        rows = {(item.row, item.kind) for item in found.checks if item.path == "include/foundation/Planted.h"}
        for line, (_, kind, label) in enumerate(planted, start=1):
            if not label:
                continue
            if kind is None:
                expect(f"not counted: {label}", not any(row == line for row, _ in rows), True)
            else:
                expect(f"counted: {label}", (line, kind) in rows)
        expect("the keys name each namespace in full and each condition by its spelling",
               {item.key for item in found.checks if item.path == "include/foundation/Planted.h"} == {
                   "foundation::detail::planted_self_test", "foundation::self_test", "foundation::fn_test",
                   "sizeof(int)==4", "3==3", "4==4", "7==7"})
        expect("the planted tree has no parse failure and no bad check file",
               not found.failures and not found.bad_check_files, True)

        ledger.write_text(matching, encoding="utf-8")
        expect("a ledger that agrees with the tree passes", captured(lambda: check(root, ledger))[0] == 0, True)
        clean_planted = (root / "include/fixy/Clean.h").read_text(encoding="utf-8")
        (root / "include/fixy/Clean.h").write_text(clean_planted + "static_assert(sizeof(int) == 4);\n",
                                                   encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a planted namespace-scope static_assert in a clean header fails",
               code == 1 and "NEW CHECK include/fixy/Clean.h" in report
               and f"{CHECKS}/fixy/Clean.cpp" in report and "include/fixy/Clean.h:3:" in report)
        (root / "include/fixy/Clean.h").write_text(
            clean_planted + "namespace fixy::detail::clean_self_test { struct Probe {}; }\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a planted self-test namespace in a clean header fails",
               code == 1 and "the self-test namespace fixy::detail::clean_self_test" in report)
        (root / "include/fixy/Clean.h").write_text(clean_planted, encoding="utf-8")
        for text, label in (("include/foundation/Planted.h | 3 | 3", "static_assert"),
                            ("include/foundation/Planted.h | 2 | 4", "self-test namespace")):
            ledger.write_text(matching.replace("include/foundation/Planted.h | 3 | 4", text), encoding="utf-8")
            code, report = captured(lambda: check(root, ledger))
            expect(f"one more {label} than the row permits fails",
                   code == 1 and "NEW CHECK include/foundation/Planted.h" in report)
        ledger.write_text(matching.replace("include/foundation/Planted.h | 3 | 4\n", ""), encoding="utf-8")
        expect("a header with checks and no row fails", captured(lambda: check(root, ledger))[0] == 1)
        ledger.write_text(matching.replace("include/foundation/Planted.h | 3 | 4", "include/foundation/Planted.h | 3 | 5"),
                          encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a row above the tree fails as stale and asks for a regenerate in the same commit",
               code == 2 and "Regenerate this row in the same commit" in report, True)
        ledger.write_text(matching + "include/foundation/Gone.h | 1 | 1\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a row that names no header fails as stale", code == 2 and "include/foundation/Gone.h" in report, True)
        ledger.write_text(matching + "include/fixy/Clean.h | 0 | 0\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a row that counts no check is malformed", code == 2 and "counts no check" in report, True)
        ledger.write_text(matching + "include/foundation/Planted.h | 9 | 9\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a second row for one header fails", code == 2 and "DUPLICATE" in report, True)
        ledger.write_text(matching + "include/fixy/Clean.h 1 1\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a row without its separators is malformed", code == 2 and "MALFORMED" in report, True)
        ledger.write_text(matching.replace(" | the planted reason", " | "), encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a keep row with no reason is malformed", code != 0 and "gives no reason" in report, True)
        ledger.write_text(matching.replace("sizeof(long)==8", "sizeof(long)==4"), encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a keep row that names no check fails", code != 0 and "the keep row names no static_assert" in report,
               True)
        ledger.write_text(matching.replace(keep_row + "\n", ""), encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("without its keep row, the kept static_assert counts", code == 1 and "include/crucible/Kept.h" in report,
               True)
        ledger.write_text("include/foundation/Planted.h | 1 | 1\n" + keep_row + "\n", encoding="utf-8")
        code, _ = captured(lambda: write(root, ledger))
        written = ledger.read_text(encoding="utf-8")
        expect("--write gives a ledger that passes, with the header and the keep row",
               code == 0 and captured(lambda: check(root, ledger))[0] == 0 and written.startswith(LEDGER_HEADER)
               and keep_row in written and "include/foundation/Planted.h | 3 | 4" in written)

        clean_check = (root / f"{CHECKS}/fixy/Clean.cpp").read_text(encoding="utf-8")
        for text, label in ((clean_check.replace("<fixy/Clean.h>", "<foundation/Planted.h>"),
                             "a check file whose first include is not its header"),
                            ("#define CLEAN_ALTERED 1\n" + clean_check, "a check file with a line of code before the "
                                                                         "include of its header"),
                            ("namespace fixy {}\n" + clean_check, "a check file whose first line of code is not an "
                                                                  "include")):
            (root / f"{CHECKS}/fixy/Clean.cpp").write_text(text, encoding="utf-8")
            code, report = captured(lambda: check(root, ledger))
            expect(f"{label} fails", code == 1 and f"CHECK FILE {CHECKS}/fixy/Clean.cpp" in report)
        (root / f"{CHECKS}/fixy/Clean.cpp").write_text(clean_check.replace("<fixy/Clean.h>", '"fixy/Clean.h"'),
                                                       encoding="utf-8")
        expect("a check file that includes its header in quotes passes", captured(lambda: check(root, ledger))[0] == 0,
               True)
        (root / f"{CHECKS}/fixy/Clean.cpp").write_text(clean_check, encoding="utf-8")
        (root / f"{CHECKS}/fixy/Gone.cpp").write_text("#include <fixy/Gone.h>\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a check file with no header fails", code == 1 and "no header include/fixy/Gone.h" in report)
        (root / f"{CHECKS}/fixy/Gone.cpp").unlink()
        (root / f"{CHECKS}/fixy/Notes.txt").write_text("notes\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a file under the check root that is not a .cpp file fails", code == 1 and "Notes.txt" in report)
        (root / f"{CHECKS}/fixy/Notes.txt").unlink()
        (root / "include/fixy/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a header the parser cannot read fails", code == 1 and "include/fixy/Broken.h" in report)
        expect("--write refuses while a header does not parse", captured(lambda: write(root, ledger))[0] == 1, True)
        (root / "include/fixy/Broken.h").unlink()

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: check(root, ledger))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository",
               from_slash == captured(lambda: check(root, ledger)), True)

        for rel in ("include/foundation/Planted.h", "include/crucible/Kept.h"):
            (root / rel).unlink()
        ledger.write_text("", encoding="utf-8")
        expect("a tree with no check in a header and an empty ledger passes",
               captured(lambda: check(root, ledger))[0] == 0, True)

        # The list of the headers that cannot compile alone, against a
        # compile database whose crucible sentinel runs the host compiler.
        compiler = shutil.which("c++") or shutil.which("g++")
        if compiler is None:
            expect("a host C++ compiler exists for the cases of --standalone", False)
        else:
            build = root / "build"
            build.mkdir()
            sentinel = build / "sentinel.cpp"
            sentinel.write_text("#include <crucible/Alone.h>\n", encoding="utf-8")
            (build / "compile_commands.json").write_text(json.dumps([{
                "directory": str(build), "file": str(sentinel),
                "output": f"{build}/test/layer/CMakeFiles/{SENTINEL_TARGET}.dir/sentinel.cpp.o",
                "arguments": [compiler, "-I", str(root / INCLUDE), "-MD", "-MF", "sentinel.d", "-o", "sentinel.o",
                              "-c", str(sentinel)],
            }]), encoding="utf-8")
            (root / "include/crucible/Alone.h").write_text("#pragma once\ninline int alone = 1;\n", encoding="utf-8")
            (root / "include/crucible/Needs.h").write_text("#pragma once\n#include <header_checks_absent.h>\n",
                                                           encoding="utf-8")
            listing = root / NOT_STANDALONE
            listing.parent.mkdir(parents=True, exist_ok=True)
            needs_row = "crucible/Needs.h | it includes a header that is not on the include path\n"
            listing.write_text(needs_row, encoding="utf-8")
            expect("a listed header that still fails passes", captured(lambda: standalone(root, build))[0] == 0, True)
            listing.write_text(needs_row + "crucible/Alone.h | it compiles alone\n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a listed header that compiles alone fails",
                   code == 1 and f"STANDALONE {NOT_STANDALONE}:2: include/crucible/Alone.h" in report)
            expect("the run writes no object and no dependency file",
                   not (build / "sentinel.o").exists() and not (build / "sentinel.d").exists(), True)
            listing.write_text(needs_row + "crucible/Needs.h |  \n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a row with no reason is malformed", code == 2 and "MALFORMED" in report, True)
            listing.write_text(needs_row + "crucible/Gone.h | it is gone\n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a row that names no header is stale", code == 2 and "crucible/Gone.h" in report, True)
            listing.write_text(needs_row, encoding="utf-8")
            (build / "compile_commands.json").write_text("[]", encoding="utf-8")
            expect("a compile database with no crucible sentinel fails",
                   captured(lambda: standalone(root, build))[0] == 2, True)
    if failures:
        print(f"check-header-checks --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-header-checks --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    root = tsast.REPO_ROOT
    ledger = root / LEDGER
    modes = {(): lambda: check(root, ledger), ("--self-test",): self_test,
             ("--write",): lambda: write(root, ledger), ("--list",): lambda: list_checks(root, ledger)}
    action = modes.get(tuple(argv))
    if action is None and len(argv) == 2 and argv[0] == "--standalone":
        return standalone(root, Path(argv[1]).resolve())
    if action is None:
        print("usage: check-header-checks.py [--list | --write | --standalone BUILD_DIR | --self-test]",
              file=sys.stderr)
        return 2
    try:
        return action()
    except tsast.KitMissing as exc:
        print(f"check-header-checks: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
