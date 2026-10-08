"""check-header-checks --standalone — no crucible header of the list compiles alone.

THE HEADERS THAT CANNOT COMPILE ALONE
    test/layer/crucible-not-standalone.txt names each crucible header that
    has no sentinel, one row `crucible/<path>.h | reason` each.  The guard
    compiles each listed header alone, with the command of the crucible
    sentinels from the compile database of BUILD_DIR, and it fails for each
    header that compiles.  A header that compiles alone gets its sentinel
    back, so the list can only become shorter.

    The rule header-checks of the engine tsast
    (utils/assay/guard/engines/tsast/header_checks.py) holds the checks of the
    headers.  This module holds only the mode --standalone of the legacy
    guard, under the rule header-standalone.

Usage
    guard check --rule header-standalone --standalone BUILD_DIR  compile each listed header alone and refuse one
                                                                 that compiles
    guard check --rule header-standalone --self-test             examine each verdict on a scratch tree

Exit 0 when no listed header compiles alone, 1 when a listed header
compiles alone, 2 on a malformed row, a missing compile command, a usage
error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import shutil
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from assay.core import paths, proc
from assay.readers import compdb

INCLUDE = "include"
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
SENTINEL_TARGET = "layer_sentinel_crucible"
SEPARATOR = " | "


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
            problems.append(
                f"MALFORMED {where}: {entry}  A row is `crucible/<path>.h{SEPARATOR}reason`, and the "
                f"reason is mandatory."
            )
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
    try:
        compiles = compdb.entries(build_dir)
    except compdb.MissingDatabase:
        return None
    for row in compiles:
        if f"/CMakeFiles/{SENTINEL_TARGET}.dir/" in (row.output or ""):
            return list(row.argv()), row.file, row.directory
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
        print(
            f"check-header-checks: the compile database of {build_dir} holds no unit of {SENTINEL_TARGET}.  "
            f"Configure the build first.",
            file=sys.stderr,
        )
        return 2
    argv, source, directory = found
    with tempfile.TemporaryDirectory(prefix="header-checks-") as work:

        def compiles(item: tuple[int, tuple[str, int]]) -> bool:
            """Say whether one listed header compiles alone."""
            index, (header, _line) = item
            unit = Path(work) / f"alone_{index}.cpp"
            unit.write_text(f"#include <{header}>\n", encoding="utf-8")
            done = proc.run(proc.Command(syntax_argv(argv, source, unit), timeout_s=proc.TOOL_S, cwd=directory))
            return done.exit == 0

        with ThreadPoolExecutor(max_workers=min(16, max(1, len(rows)))) as pool:
            verdicts = list(pool.map(compiles, enumerate(rows)))
    alone = [row for row, verdict in zip(rows, verdicts) if verdict]
    for header, line in alone:
        print(
            f"STANDALONE {NOT_STANDALONE}:{line}: include/{header} compiles alone.  Remove its row in this commit, "
            f"so that the header gets its sentinel.",
            file=sys.stderr,
        )
    print(f"check-header-checks: {len(rows)} listed header(s), {len(alone)} of them compile alone.", file=sys.stderr)
    if alone:
        return 1
    return 2 if problems else 0


def self_test() -> int:
    """Examine each verdict of the mode --standalone on a scratch tree.

    The compile database of the scratch tree runs the host compiler as its
    crucible sentinel.

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

    def captured(root: Path, build: Path) -> tuple[int, str]:
        """Run the mode --standalone and keep its report."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer), contextlib.redirect_stdout(buffer):
            code = standalone(root, build)
        return code, buffer.getvalue()

    compiler = shutil.which("c++") or shutil.which("g++")
    if compiler is None:
        expect("a host C++ compiler exists for the cases of --standalone", False)
    else:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "include/crucible").mkdir(parents=True)
            build = root / "build"
            build.mkdir()
            sentinel = build / "sentinel.cpp"
            sentinel.write_text("#include <crucible/Alone.h>\n", encoding="utf-8")
            compdb.plant(
                build,
                [
                    {
                        "directory": str(build),
                        "file": str(sentinel),
                        "output": f"{build}/test/layer/CMakeFiles/{SENTINEL_TARGET}.dir/sentinel.cpp.o",
                        "arguments": [
                            compiler,
                            "-I",
                            str(root / INCLUDE),
                            "-MD",
                            "-MF",
                            "sentinel.d",
                            "-o",
                            "sentinel.o",
                            "-c",
                            str(sentinel),
                        ],
                    }
                ],
            )
            (root / "include/crucible/Alone.h").write_text("#pragma once\ninline int alone = 1;\n", encoding="utf-8")
            (root / "include/crucible/Needs.h").write_text(
                "#pragma once\n#include <header_checks_absent.h>\n", encoding="utf-8"
            )
            listing = root / NOT_STANDALONE
            listing.parent.mkdir(parents=True, exist_ok=True)
            needs_row = "crucible/Needs.h | it includes a header that is not on the include path\n"
            listing.write_text(needs_row, encoding="utf-8")
            expect("a listed header that still fails passes", captured(root, build)[0] == 0, True)
            listing.write_text(needs_row + "crucible/Alone.h | it compiles alone\n", encoding="utf-8")
            code, report = captured(root, build)
            expect(
                "a listed header that compiles alone fails",
                code == 1 and f"STANDALONE {NOT_STANDALONE}:2: include/crucible/Alone.h" in report,
            )
            expect(
                "the run writes no object and no dependency file",
                not (build / "sentinel.o").exists() and not (build / "sentinel.d").exists(),
                True,
            )
            listing.write_text(needs_row + "crucible/Needs.h |  \n", encoding="utf-8")
            code, report = captured(root, build)
            expect("a row with no reason is malformed", code == 2 and "MALFORMED" in report, True)
            listing.write_text(needs_row + "crucible/Gone.h | it is gone\n", encoding="utf-8")
            code, report = captured(root, build)
            expect("a row that names no header is stale", code == 2 and "crucible/Gone.h" in report, True)
            listing.write_text(needs_row, encoding="utf-8")
            compdb.plant(build, [])
            expect("a compile database with no crucible sentinel fails", captured(root, build)[0] == 2, True)
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
    parser = argparse.ArgumentParser(prog="guard check --rule header-standalone", add_help=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument(
        "--standalone",
        metavar="BUILD_DIR",
        type=Path,
        help="compile each listed header alone and refuse one that compiles",
    )
    modes.add_argument("--self-test", action="store_true", help="examine each verdict on a scratch tree")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    if options.self_test:
        return self_test()
    return standalone(paths.REPO_ROOT, options.standalone.resolve())
