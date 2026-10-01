#!/usr/bin/env python3
"""check-no-unity-pch — no target of the tree compiles as a unity build or with a precompiled header.

The reflection walks of this tree see each header that a translation unit
includes.  A unity build puts several source files into one translation
unit, and a precompiled header puts the headers of the header file into each
unit that uses it.  Each one changes what a walk sees, so each one can change
a result.  CLAUDE.md §XV states the rule.

WHAT THE CHECK READS
    The effect, from the compile database of a configured build
    (BUILD_DIR/compile_commands.json):
      * A row whose source is a unity source of CMake: a file
        unity_<N>_cxx.cxx or unity_<N>_c.c, or a file in a Unity directory
        of CMakeFiles.
      * A row whose command includes a precompiled header: `-include` of a
        file whose name starts with cmake_pch, `-include-pch`,
        `-Winvalid-pch`, or a word that ends in .gch or .pch.
    The cause, from each CMake file that git tracks (CMakeLists.txt and
    *.cmake), read with a CMake lexer that skips line comments, bracket
    comments and the text inside a quoted or bracket argument:
      * An invocation of target_precompile_headers.  CMake command names
        are case-insensitive.
      * An argument that is one of the property and variable names that
        turn a unity build or a precompiled header on: UNITY_BUILD and the
        UNITY_BUILD_* names, CMAKE_UNITY_BUILD and the CMAKE_UNITY_BUILD_*
        names, PRECOMPILE_HEADERS and PRECOMPILE_HEADERS_REUSE_FROM.  The
        names that turn them off (SKIP_UNITY_BUILD_INCLUSION,
        SKIP_PRECOMPILE_HEADERS, DISABLE_PRECOMPILE_HEADERS) are not
        findings.  A name inside a longer argument, such as
        ${CMAKE_UNITY_BUILD} or a message text, is not a finding.
    And from CMakePresets.json and CMakeUserPresets.json: a cache variable
    of a preset whose name starts with CMAKE_UNITY_BUILD.

    The tree has no unity build and no precompiled header, so the check has
    no ledger.  Each finding is an error.

Usage
    check-no-unity-pch.py --build-dir BUILD_DIR [--warnings-dir DIR]
    check-no-unity-pch.py --self-test

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
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402

CHECK = "no-unity-pch"
DATABASE = "compile_commands.json"
PRESETS = ("CMakePresets.json", "CMakeUserPresets.json")
PCH_COMMAND = "target_precompile_headers"
# The names that turn a unity build or a precompiled header on.  A name that
# turns one off is not in this set.
ENABLING_NAMES = frozenset({
    "UNITY_BUILD", "UNITY_BUILD_MODE", "UNITY_BUILD_BATCH_SIZE", "UNITY_BUILD_UNIQUE_ID",
    "UNITY_BUILD_CODE_BEFORE_INCLUDE", "UNITY_BUILD_CODE_AFTER_INCLUDE", "UNITY_GROUP",
    "CMAKE_UNITY_BUILD", "CMAKE_UNITY_BUILD_BATCH_SIZE", "CMAKE_UNITY_BUILD_UNIQUE_ID",
    "PRECOMPILE_HEADERS", "PRECOMPILE_HEADERS_REUSE_FROM",
})
# Each word that pch_words() keeps contains one of these texts.
PCH_MARKERS = ("cmake_pch", "-include-pch", "-Winvalid-pch", ".gch", ".pch")
WHY = ("The reflection walks of this tree see each header that a translation unit includes, so a unity build or a "
       "precompiled header can change a result.")


@dataclass(frozen=True)
class Token:
    """One token of a CMake file: an identifier that opens a command, or one argument."""

    kind: str
    text: str
    line: int


def cmake_tokens(text: str) -> Iterator[Token]:
    """Yield the command names and the arguments of one CMake file, in order.

    The lexer follows the CMake language: a line comment runs from `#` to the
    end of the line, a bracket comment is `#[=*[` up to the matching `]=*]`, a
    bracket argument is `[=*[` up to the matching `]=*]`, and a quoted
    argument is `"..."` with backslash escapes.  An unquoted argument ends at
    white space, at a parenthesis, at `#` or at a quote.  A command name is an
    identifier that a `(` follows, outside the arguments of a command.

    Complexity: linear in the length of the text.

    Args:
        text: The content of the file

    Yields:
        A token of kind "command" for each command name, "quoted", "bracket" or "unquoted" for each
        argument
    """
    index = 0
    line = 1
    depth = 0
    length = len(text)

    def bracket_end(start: int) -> int | None:
        """Return the index after the bracket that opens at start, or None when start opens none."""
        cursor = start + 1
        while cursor < length and text[cursor] == "=":
            cursor += 1
        if cursor >= length or text[cursor] != "[":
            return None
        closer = "]" + "=" * (cursor - start - 1) + "]"
        found = text.find(closer, cursor + 1)
        return length if found < 0 else found + len(closer)

    while index < length:
        char = text[index]
        if char == "\n":
            line += 1
            index += 1
        elif char in " \t\r":
            index += 1
        elif char == "#":
            end = bracket_end(index + 1) if index + 1 < length and text[index + 1] == "[" else None
            if end is None:
                newline = text.find("\n", index)
                end = length if newline < 0 else newline
            line += text.count("\n", index, end)
            index = end
        elif char == "(":
            depth += 1
            index += 1
        elif char == ")":
            depth = max(0, depth - 1)
            index += 1
        elif char == "[" and depth > 0 and bracket_end(index) is not None:
            end = bracket_end(index)
            assert end is not None
            yield Token("bracket", text[index:end], line)
            line += text.count("\n", index, end)
            index = end
        elif char == '"':
            cursor = index + 1
            value: list[str] = []
            while cursor < length and text[cursor] != '"':
                if text[cursor] == "\\" and cursor + 1 < length:
                    value.append(text[cursor:cursor + 2])
                    cursor += 2
                    continue
                value.append(text[cursor])
                cursor += 1
            yield Token("quoted", "".join(value), line)
            line += text.count("\n", index, cursor)
            index = min(cursor + 1, length)
        else:
            cursor = index
            while cursor < length and text[cursor] not in " \t\r\n()#\"":
                cursor += 2 if text[cursor] == "\\" else 1
            word = text[index:cursor]
            if depth == 0:
                after = cursor
                while after < length and text[after] in " \t":
                    after += 1
                is_command = after < length and text[after] == "(" and word.replace("_", "a").isalnum()
                yield Token("command" if is_command else "unquoted", word, line)
            else:
                yield Token("unquoted", word, line)
            index = max(cursor, index + 1)


def cmake_findings(rel: str, text: str) -> list[check_report.Finding]:
    """Return one error for each command or argument of one CMake file that turns a unity build or a PCH on.

    Args:
        rel: The file, relative to the repository root
        text: Its content

    Returns:
        The findings in file order
    """
    findings: list[check_report.Finding] = []
    for token in cmake_tokens(text):
        if token.kind == "command" and token.text.lower() == PCH_COMMAND:
            findings.append(check_report.Finding(
                "error", rel, token.line, CHECK,
                f"the file calls {PCH_COMMAND}, which gives a target a precompiled header.  {WHY}  Remove the call."))
        elif token.kind in ("unquoted", "quoted") and token.text in ENABLING_NAMES:
            findings.append(check_report.Finding(
                "error", rel, token.line, CHECK,
                f"the file names {token.text}, which turns a unity build or a precompiled header on.  {WHY}  Remove "
                f"it."))
    return findings


def preset_findings(rel: str, text: str) -> list[check_report.Finding]:
    """Return one error for each cache variable of a preset that turns a unity build on.

    Args:
        rel: The presets file, relative to the repository root
        text: Its content

    Returns:
        The findings, or one error when the file is not valid JSON
    """
    try:
        document = json.loads(text)
    except ValueError as exc:
        return [check_report.Finding("error", rel, 0, CHECK, f"the file is not valid JSON, so the check cannot read "
                                                             f"its presets: {exc}")]
    findings: list[check_report.Finding] = []
    presets = document.get("configurePresets", []) if isinstance(document, dict) else []
    for preset in presets if isinstance(presets, list) else []:
        variables = preset.get("cacheVariables", {}) if isinstance(preset, dict) else {}
        for name in variables if isinstance(variables, dict) else {}:
            if name.startswith("CMAKE_UNITY_BUILD"):
                findings.append(check_report.Finding(
                    "error", rel, 0, CHECK,
                    f"the preset {preset.get('name', '?')} sets {name}, which turns a unity build on.  {WHY}  "
                    f"Remove the cache variable."))
    return findings


def is_unity_source(source: str) -> bool:
    """Say whether a source path of the compile database is a unity source that CMake writes."""
    path = PurePosixPath(source)
    name = path.name
    stem = name.split(".", 1)[0]
    parts = stem.split("_")
    is_unity_name = len(parts) == 3 and parts[0] == "unity" and parts[1].isdigit() and parts[2] in ("c", "cxx")
    return is_unity_name or ("Unity" in path.parts and "CMakeFiles" in path.parts)


def pch_words(argv: list[str]) -> list[str]:
    """Return each word of a compile command that includes a precompiled header."""
    found: list[str] = []
    for index, word in enumerate(argv):
        if word == "-include" and index + 1 < len(argv) and PurePosixPath(argv[index + 1]).name.startswith(
                "cmake_pch"):
            found.append(f"-include {argv[index + 1]}")
        elif word.startswith("-include") and PurePosixPath(word[len("-include"):]).name.startswith("cmake_pch"):
            found.append(word)
        elif word in ("-include-pch", "-Winvalid-pch") or word.endswith((".gch", ".pch")):
            found.append(word)
    return found


def command_words(row: dict[str, object]) -> list[str]:
    """Return the words of the compile command of one row of the compile database.

    A command string that holds no marker of a precompiled header gives no
    word that pch_words() keeps, so it is not split.  The split costs most of
    the time of the check on a database of thousands of rows.

    Raises:
        KeyError: If the row has neither arguments nor a command
        ValueError: If the command string is not valid shell quoting
    """
    if "arguments" in row:
        return [str(word) for word in row["arguments"]]  # type: ignore[union-attr]
    command = str(row["command"])
    return shlex.split(command) if any(marker in command for marker in PCH_MARKERS) else []


def database_findings(build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Return one error for each row of the compile database that compiles a unity source or uses a PCH.

    Complexity: linear in the size of the database.

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
        commands = [(str(row["file"]), command_words(row)) for row in rows]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        return [check_report.Finding("error", shown, 0, CHECK, f"the compile database cannot be read: {exc}")]
    findings: list[check_report.Finding] = []
    for source, argv in commands:
        named = Path(source)
        where = named.relative_to(root).as_posix() if named.is_absolute() and named.is_relative_to(root) else source
        if is_unity_source(source):
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile database compiles the unity source {source}.  {WHY}  Turn the unity build off."))
        for word in pch_words(argv):
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile command of {source} includes a precompiled header ({word}).  {WHY}  Remove the "
                f"precompiled header."))
    return findings


def evaluate(root: Path, build_dir: Path) -> list[check_report.Finding]:
    """Read the compile database of one build and the CMake files of one tree, and return each finding.

    Args:
        root: The repository root
        build_dir: A configured build directory

    Returns:
        The findings
    """
    findings = database_findings(build_dir, root)
    for rel in tsast.tracked_files(root):
        name = PurePosixPath(rel).name
        if name != "CMakeLists.txt" and not name.endswith(".cmake") and name not in PRESETS:
            continue
        path = root / rel
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            findings.append(check_report.Finding("error", rel, 0, CHECK, f"the file cannot be read: {exc}"))
            continue
        findings.extend(preset_findings(rel, text) if name in PRESETS else cmake_findings(rel, text))
    return findings


def self_test() -> int:
    """Plant each kind of unity build and precompiled header in a scratch tree, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    clean_cmake = (
        "# target_precompile_headers(a PRIVATE x.h) in a comment\n"
        "#[[ set(CMAKE_UNITY_BUILD ON) in a bracket comment ]]\n"
        'message(STATUS "UNITY_BUILD is off here")\n'
        'message(STATUS [=[PRECOMPILE_HEADERS in a bracket argument]=])\n'
        "set_source_files_properties(a.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON)\n"
        "set_target_properties(t PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)\n"
        "if(DEFINED CMAKE_UNITY_BUILD_X)\n  message(${CMAKE_UNITY_BUILD})\nendif()\n"
        "add_library(precompile_headers_demo STATIC a.cpp)\n"
    )
    clean_presets = {"version": 6, "configurePresets": [{"name": "default", "cacheVariables": {"CMAKE_BUILD_TYPE":
                                                                                               "Debug"}}]}
    clean_rows = [{"directory": "/b", "file": "/r/src/a.cpp", "arguments": ["g++", "-c", "/r/src/a.cpp"]},
                  {"directory": "/b", "file": "/r/src/b.cpp", "command": "g++ -include config.h -c /r/src/b.cpp"}]
    with tempfile.TemporaryDirectory(prefix="no-unity-pch-") as work:
        root = Path(work) / "tree"
        build = Path(work) / "build"
        build.mkdir()
        (root / "cmake").mkdir(parents=True)
        (root / "CMakeLists.txt").write_text(clean_cmake, encoding="utf-8")
        (root / "cmake/Extra.cmake").write_text("set(SOMETHING ON)\n", encoding="utf-8")
        (root / "CMakePresets.json").write_text(json.dumps(clean_presets), encoding="utf-8")
        database = build / DATABASE
        database.write_text(json.dumps(clean_rows), encoding="utf-8")

        def run() -> list[check_report.Finding]:
            """Evaluate the scratch tree."""
            return evaluate(root, build)

        expect("a clean tree gives no finding: comments, texts, the names that turn a feature off and a forced "
               "include that is no precompiled header", run() == [])

        for text, label, line in (
                ("target_precompile_headers(t PRIVATE <vector>)\n", "a call of target_precompile_headers", 1),
                ("Target_Precompile_Headers(t PRIVATE <vector>)\n", "a call with another letter case", 1),
                ("set(CMAKE_UNITY_BUILD ON)\n", "set(CMAKE_UNITY_BUILD ON)", 1),
                ("\nset_target_properties(t PROPERTIES\n  UNITY_BUILD ON)\n", "the target property UNITY_BUILD", 3),
                ('set_property(TARGET t PROPERTY "PRECOMPILE_HEADERS_REUSE_FROM" u)\n',
                 "a quoted PRECOMPILE_HEADERS_REUSE_FROM", 1),
                ("set(CMAKE_UNITY_BUILD_BATCH_SIZE 8) # a comment\n", "a batch size of a unity build", 1)):
            (root / "cmake/Extra.cmake").write_text(text, encoding="utf-8")
            found = run()
            expect(f"an error: {label}", len(found) == 1 and found[0].level == "error"
                   and found[0].path == "cmake/Extra.cmake" and found[0].line == line)
        (root / "cmake/Extra.cmake").write_text("set(SOMETHING ON)\n", encoding="utf-8")

        presets = json.loads(json.dumps(clean_presets))
        presets["configurePresets"][0]["cacheVariables"]["CMAKE_UNITY_BUILD"] = "ON"
        (root / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
        found = run()
        expect("an error: a preset that sets CMAKE_UNITY_BUILD",
               len(found) == 1 and found[0].path == "CMakePresets.json")
        (root / "CMakePresets.json").write_text("{ not json", encoding="utf-8")
        found = run()
        expect("an error: a presets file that is not JSON", len(found) == 1 and "not valid JSON" in found[0].message)
        (root / "CMakePresets.json").write_text(json.dumps(clean_presets), encoding="utf-8")

        for row, label in (
                ({"directory": "/b", "file": "/b/CMakeFiles/t.dir/Unity/unity_0_cxx.cxx",
                  "arguments": ["g++", "-c", "unity_0_cxx.cxx"]}, "a unity source in the compile database"),
                ({"directory": "/b", "file": "/r/src/c.cpp",
                  "command": "g++ -Winvalid-pch -include /b/CMakeFiles/t.dir/cmake_pch.hxx -c /r/src/c.cpp"},
                 "the precompiled header of CMake in a command"),
                ({"directory": "/b", "file": "/r/src/d.cpp", "arguments": ["g++", "-include-pch", "x.pch", "-c", "d"]},
                 "an -include-pch option")):
            database.write_text(json.dumps(clean_rows + [row]), encoding="utf-8")
            found = run()
            expect(f"an error: {label}", bool(found) and all(item.level == "error" for item in found))
        database.write_text("[{]", encoding="utf-8")
        found = run()
        expect("an error: a compile database that cannot be read",
               len(found) == 1 and "cannot be read" in found[0].message)
        database.unlink()
        found = run()
        expect("an error: a missing compile database", len(found) == 1 and "does not exist" in found[0].message)
        database.write_text(json.dumps(clean_rows), encoding="utf-8")

        (root / "cmake/Extra.cmake").write_text("set(CMAKE_UNITY_BUILD ON)\n", encoding="utf-8")
        warnings_dir = Path(work) / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(run(), CHECK, warnings_dir)
        expect("an error gives exit status 1 and writes no warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
    if failures:
        print(f"check-no-unity-pch --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-unity-pch --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-no-unity-pch.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, help="the configured build directory whose compile database the "
                                                       "check reads")
    parser.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    findings = evaluate(tsast.REPO_ROOT, arguments.build_dir.resolve())
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    print(f"check-no-unity-pch: {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
