#!/usr/bin/env python3
"""check-contract-semantic — the last contract semantic flag of each compile agrees with the ignore define.

GCC uses the last -fcontract-evaluation-semantic of a compile line.
CRUCIBLE_PRE and CRUCIBLE_POST (include/foundation/contracts/Pre.h, Post.h)
cannot read the semantic, so the define CRUCIBLE_CONTRACT_SEMANTIC_IGNORE
tells them.  With the define, each precondition gives its condition to the
optimizer as an assumption.  When the language clauses of the same compile do
their checks under the observe semantic, a handler that returns makes the
assumption of a false condition undefined behaviour.  Without the define, the
ignore semantic removes the checks and keeps no hint for the optimizer.

The configure step rejects a scope that has the flag or the define alone
(cmake/ContractSemantic.cmake).  A scope cannot see the order of the final
compile line: CMake puts the usage requirements of the linked targets after
the options of a target, and the Release build gives its semantic through a
usage requirement.  This check reads the final compile lines.

WHAT THE CHECK READS
    Each row of the compile database of a configured build
    (BUILD_DIR/compile_commands.json) whose command names the semantic flag or
    the define.  The words of the command, in order:
      * `-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE`, with or without a value, or
        `-D` and then that name as the next word, defines it.
        `-UCRUCIBLE_CONTRACT_SEMANTIC_IGNORE` in the same forms removes it.
      * The value of the last `-fcontract-evaluation-semantic=VALUE` is the
        semantic of the compile.
    A row that defines the name and whose semantic is not `ignore` is an
    error.  A row whose semantic is `ignore` and that does not define the
    name is an error.  The tree has no such row, so the check has no ledger.

Usage
    check-contract-semantic.py --build-dir BUILD_DIR [--warnings-dir DIR]
    check-contract-semantic.py --self-test

Exit 0 with no finding, 1 on a finding or an input that the check cannot
read, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import shlex
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECK = "contract-semantic"
DATABASE = "compile_commands.json"
DEFINE = "CRUCIBLE_CONTRACT_SEMANTIC_IGNORE"
SEMANTIC_FLAG = "-fcontract-evaluation-semantic="
IGNORE = "ignore"
REPAIR = ("Take a whole target off contract checks with crucible_contract_ignore_target() of "
          "cmake/ContractSemantic.cmake, and a source file with its COMPILE_OPTIONS property set to "
          "CRUCIBLE_CONTRACT_IGNORE_OPTIONS (CLAUDE.md section XII).")


def macro_name(word: str) -> str:
    """Return the macro name of the text after -D or -U, without its value."""
    return word.split("=", 1)[0]


def semantic_of(argv: list[str]) -> tuple[bool, str | None]:
    """Return whether a compile defines the ignore define, and the value of its last semantic flag.

    Complexity: linear in the number of words.

    Args:
        argv: The words of one compile command

    Returns:
        (the define is set at the end of the command line, the last semantic value or None)
    """
    is_defined = False
    semantic: str | None = None
    index = 0
    while index < len(argv):
        word = argv[index]
        if word in ("-D", "-U") and index + 1 < len(argv):
            if macro_name(argv[index + 1]) == DEFINE:
                is_defined = word == "-D"
            index += 2
            continue
        if word.startswith(("-D", "-U")) and macro_name(word[2:]) == DEFINE:
            is_defined = word.startswith("-D")
        elif word.startswith(SEMANTIC_FLAG):
            semantic = word[len(SEMANTIC_FLAG):]
        index += 1
    return is_defined, semantic


def command_words(row: dict[str, object]) -> list[str]:
    """Return the words of the compile command of one row, or no word when the row names neither item.

    The split of a command string costs most of the time of the check, so a
    command that names neither the flag nor the define is not split.

    Raises:
        KeyError: If the row has neither arguments nor a command
        ValueError: If the command string is not valid shell quoting
    """
    if "arguments" in row:
        return [str(word) for word in row["arguments"]]  # type: ignore[union-attr]
    command = str(row["command"])
    return shlex.split(command) if (DEFINE in command or SEMANTIC_FLAG in command) else []


def evaluate(build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Return one error for each compile of one build whose last semantic flag does not agree with the define.

    Complexity: linear in the size of the compile database.

    Args:
        build_dir: A configured build directory
        root: The repository root, to name a source inside the tree by its relative path

    Returns:
        The findings, or one error when the database does not exist or cannot be read
    """
    database = build_dir / DATABASE
    shown = str(database)
    if not database.is_file():
        return [check_report.Finding("error", shown, 0, CHECK, "the compile database does not exist, so the check "
                                                               "cannot see the compile commands.  Configure the "
                                                               "build first.")]
    try:
        rows = json.loads(database.read_text(encoding="utf-8"))
        if not isinstance(rows, list):
            raise ValueError("the top level is not a list of rows")
        commands = [(str(row["file"]), str(row.get("output", "")), command_words(row)) for row in rows]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        return [check_report.Finding("error", shown, 0, CHECK, f"the compile database cannot be read: {exc}")]
    findings: list[check_report.Finding] = []
    for source, output, argv in commands:
        if not argv:
            continue
        is_defined, semantic = semantic_of(argv)
        named = Path(source)
        where = named.relative_to(root).as_posix() if named.is_absolute() and named.is_relative_to(root) else source
        shown_output = f" (object {output})" if output else ""
        if is_defined and semantic != IGNORE:
            last = "no semantic flag" if semantic is None else f"{SEMANTIC_FLAG}{semantic} as its last semantic flag"
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile{shown_output} defines {DEFINE} and has {last}.  CRUCIBLE_PRE then gives its condition "
                f"to the optimizer as an assumption while the language clauses do their checks, and an assumption "
                f"of a false condition is undefined behaviour.  {REPAIR}"))
        elif semantic == IGNORE and not is_defined:
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile{shown_output} has {SEMANTIC_FLAG}{IGNORE} as its last semantic flag and does not "
                f"define {DEFINE}.  CRUCIBLE_PRE then keeps a check that does nothing and gives the optimizer no "
                f"hint.  {REPAIR}"))
    return findings


def self_test() -> int:
    """Plant each sequence of the flag and the define in a scratch compile database, and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}" if detail != "" else name)

    ignore_flag = f"{SEMANTIC_FLAG}{IGNORE}"
    observe_flag = f"{SEMANTIC_FLAG}observe"
    define = f"-D{DEFINE}=1"
    clean_rows = [
        {"directory": "/b", "file": "/r/src/plain.cpp", "command": "g++ -O3 -c /r/src/plain.cpp"},
        {"directory": "/b", "file": "/r/src/observe.cpp", "arguments": ["g++", observe_flag, "-c", "observe.cpp"]},
        {"directory": "/b", "file": "/r/test/one.cpp", "command": f"g++ {ignore_flag} {define} -c /r/test/one.cpp"},
        {"directory": "/b", "file": "/r/test/two.cpp",
         "command": f"g++ {observe_flag} -O3 {ignore_flag} -D {DEFINE} -c /r/test/two.cpp"},
        {"directory": "/b", "file": "/r/test/three.cpp",
         "arguments": ["g++", define, "-UOTHER", observe_flag, ignore_flag, "-c", "three.cpp"]},
    ]
    with tempfile.TemporaryDirectory(prefix="contract-semantic-") as work:
        build = Path(work) / "build"
        build.mkdir()
        database = build / DATABASE
        root = Path("/r")

        def run(rows: list[dict[str, object]]) -> list[check_report.Finding]:
            """Write the rows as the compile database and evaluate it."""
            database.write_text(json.dumps(rows), encoding="utf-8")
            return evaluate(build, root)

        found = run(clean_rows)
        expect("a database with no split row gives no finding: no item, observe alone, and the define with "
               "ignore last in each spelling", found == [], found)

        for row, label, needle in (
                ({"directory": "/b", "file": "/r/test/late.cpp",
                  "command": f"g++ {ignore_flag} {define} -O3 {observe_flag} -c /r/test/late.cpp"},
                 "the define with observe after ignore, the defect of a target option before a usage requirement",
                 "observe as its last semantic flag"),
                ({"directory": "/b", "file": "/r/test/enforce.cpp",
                  "arguments": ["g++", define, f"{SEMANTIC_FLAG}enforce", "-c", "enforce.cpp"]},
                 "the define with enforce", "enforce as its last semantic flag"),
                ({"directory": "/b", "file": "/r/test/none.cpp", "command": f"g++ -D {DEFINE} -c /r/test/none.cpp"},
                 "the define with no semantic flag", "no semantic flag"),
                ({"directory": "/b", "file": "/r/test/alone.cpp", "command": f"g++ {ignore_flag} -c /r/test/alone.cpp"},
                 "ignore with no define", "does not define"),
                ({"directory": "/b", "file": "/r/test/removed.cpp",
                  "command": f"g++ {ignore_flag} {define} -U{DEFINE} -c /r/test/removed.cpp"},
                 "ignore with the define removed after it", "does not define")):
            found = run(clean_rows + [row])
            expect(f"an error: {label}", len(found) == 1 and found[0].level == "error"
                   and found[0].path == str(row["file"]).removeprefix("/r/") and needle in found[0].message, found)

        database.write_text("[{]", encoding="utf-8")
        found = evaluate(build, root)
        expect("an error: a compile database that cannot be read",
               len(found) == 1 and "cannot be read" in found[0].message, found)
        database.unlink()
        found = evaluate(build, root)
        expect("an error: a missing compile database", len(found) == 1 and "does not exist" in found[0].message, found)

        late = {"directory": "/b", "file": "/r/test/late.cpp",
                "command": f"g++ {ignore_flag} {define} {observe_flag} -c /r/test/late.cpp"}
        warnings_dir = Path(work) / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(run([late]), CHECK, warnings_dir)
        lines = printed.getvalue().splitlines()
        expect("an error gives exit status 1, the line format, and no warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists() and bool(lines)
               and all(check_report.parse_line(line) is not None for line in lines), lines[:3])
    if failures:
        for failure in failures:
            print(f"check-contract-semantic --self-test: FAILED, {failure}", file=sys.stderr)
        return 2
    print("check-contract-semantic --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-contract-semantic.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, help="the configured build directory whose compile database the "
                                                       "check reads")
    parser.add_argument("--self-test", action="store_true", help="plant each case in a scratch compile database")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    findings = evaluate(arguments.build_dir.resolve(), REPO_ROOT)
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    print(f"check-contract-semantic: {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
