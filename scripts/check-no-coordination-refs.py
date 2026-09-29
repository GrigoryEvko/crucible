#!/usr/bin/env python3
"""Refuse task, stage and tracker references in the prose of the new tree.

THE RULE
    A comment, a docstring, a diagnostic, a script or an allowlist states what
    the code does.  It does not name the task, the stage or the tracker entry
    that produced the code.  Such a name means nothing to a reader who has no
    access to the tracker, and it becomes false when the plan changes.

    Three families of text fail:
      1. A task reference: `#` and two to four digits (#147, task #193,
         #1519).  One digit is a list item (#1), and five or more digits is a
         colour (#374151), so neither matches.
      2. A tracker tag: the FIXY-U-, FIXY-V-, FIXY-FOUND-, FOUND-, GAPS-,
         SEPLOG-, CONTRACT-, METX-, WRAP-, BC- and PERF- families, the lowercase
         fixy-A5-016 and fix-18 forms, the short U-002 and V-073 forms, and the
         CR-05 audit form.
      3. A stage label: a dotted stage such as A13.2 or A10.x, "Stage D",
         "Stage B4", "at A9", "Phase F6" and "Agent 11".
    Three kinds of text look similar and pass by construction: a preprocessor
    directive (a letter follows `#`), a collision rule code (S004, I002: no
    hyphen) and a suppression marker (ROW-CONTAINS-OK: no digit after the
    prefix).  The guard has no allowlist.  A legitimate construct that it
    reports is a reason to narrow a pattern, not to exempt a line.

THE ENGINE
    The rule is about prose, so the scan reads prose only, from a parser where
    the language has one:
      * C++: the parse tree gives the comment, string and raw string nodes and
        the argument of each directive such as #error or #pragma.  The string
        literals inside a macro body come from the preprocessing tokens of the
        body.  Code is never read, so a member access such as `A1.x` is not a
        stage label.
      * Python: the tokenize module gives the comments and the strings,
        docstrings and f-string text included.
      * Every other file (shell, CMake, YAML, allowlists, JSON, Markdown) has
        no parser here, and its text is read line by line.

EXIT STATUS
    0  clean
    1  at least one reference
    2  bad invocation, a file that does not parse, or a self-test failure
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)
"""

from __future__ import annotations

import io
import re
import subprocess
import sys
import tempfile
import tokenize
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

# The trees that obey the rule.  The runtime under include/crucible and its
# sources still hold references, so the scan does not read them.
SCAN_DIRS = (
    "include/foundation", "include/fixy", "src/foundation", "src/fixy",
    "test/foundation", "test/fixy", "scripts", "vessel", "bench",
)

# The build files outside those trees that obey the rule.  test/CMakeLists.txt
# still holds references, so it is not here.
SCAN_FILES = ("CMakeLists.txt", ".github/workflows/ci.yml")

# This file plants references in its self-test, so the scan skips it.
SELF_PATH = "scripts/check-no-coordination-refs.py"

CPP_SUFFIXES = (".h", ".hpp", ".cpp", ".cc")

# One branch for each family.  The lookbehind on the task branch refuses an
# HTML entity (&#123;), the shell argument count ($#), a printf flag (%#08x), a
# regex bracket ([#0-9]), an escape (\#) and token pasting (##).
BRANCHES = (
    r"(?<![&\w$%{\[\\#])#[0-9]{2,4}\b",
    r"\bFIXY-(?:U|V|FOUND)-[0-9]",
    r"\bFIXY-[0-9]",
    r"\bFOUND-(?:[A-Z][0-9]*|[0-9]+)\b",
    r"\bGAPS-[0-9]",
    r"\bSEPLOG-[A-Z]?[0-9]",
    r"\bCONTRACT-[0-9]",
    r"\bMETX-[A-Z0-9]",
    r"\bWRAP-(?:\*|[A-Z][A-Za-z]*(?:-[A-Za-z]+)*-[0-9]+)",
    r"\bBC-[0-9]+\b",
    r"\bPERF-[0-9]",
    r"\bCR-[0-9]{2}\b",
    r"\bfixy-[A-Z]+[0-9]*-(?:[0-9]+\b|\*|X{3}\b)",
    r"\bfix-[0-9]+\b",
    r"(?<![\w-])[UV]-[0-9]{3}[a-z]?\b",
    r"(?<![\w.\-/])A[0-9]{1,2}(?:\.(?:[0-9]+|x))+(?![\w\-])",
    r"\bStage\s+[A-D](?:[0-9]{1,2}(?:\.[0-9]+)*)?\b",
    r"\b(?:at|task|tasks|since|until)\s+A[0-9]{1,2}\b",
    r"\bPhase\s+[A-Z][0-9]+\b",
    r"\bAgent\s+[0-9]+\b",
)
REFERENCE = re.compile("|".join(BRANCHES))

# The Python token kinds that hold prose.  FSTRING_MIDDLE and TSTRING_MIDDLE
# exist only in newer interpreters, so they are looked up by name.
PYTHON_PROSE = frozenset(
    kind for kind in (
        tokenize.COMMENT, tokenize.STRING,
        getattr(tokenize, "FSTRING_MIDDLE", None), getattr(tokenize, "TSTRING_MIDDLE", None),
    ) if kind is not None
)


class Unreadable(RuntimeError):
    """A file in scope cannot be read by its parser, so the scan cannot vouch for it."""


def scoped_files(root: Path) -> list[Path]:
    """Return every file in scope under root, repo-relative, sorted.

    In a git work tree the list comes from git, so an ignored build product is
    never read.  In a scratch tree (the self-test) it comes from the file
    system, with hidden directories skipped.

    Args:
        root: The repository root

    Returns:
        The files in scope, relative to root, without this guard itself
    """
    wanted = [d for d in SCAN_DIRS if (root / d).is_dir()] + [f for f in SCAN_FILES if (root / f).is_file()]
    if not wanted:
        return []
    listed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--", *wanted],
        cwd=root, capture_output=True, check=False,
    )
    if listed.returncode == 0 and (root / ".git").exists():
        names = {name for name in listed.stdout.decode().split("\0") if name}
    else:
        names = set()
        for entry in wanted:
            path = root / entry
            if path.is_file():
                names.add(entry)
                continue
            for found in path.rglob("*"):
                relative = found.relative_to(root)
                if found.is_file() and not any(part.startswith(".") for part in relative.parts):
                    names.add(relative.as_posix())
    return sorted(Path(n) for n in names if n != SELF_PATH and (root / n).is_file())


def cpp_prose(root: Path, files: list[Path]) -> Iterator[tuple[Path, int, str]]:
    """Yield every prose line of the C++ files, from their parse trees.

    Args:
        root: The repository root
        files: Repo-relative C++ files

    Yields:
        (file, one-based line, text) for each line of each prose span

    Raises:
        Unreadable: If a file does not parse clean
    """
    try:
        trees = list(tsast.parse([root / f for f in files]))
    except tsast.ParseError as exc:
        raise Unreadable(str(exc)) from exc
    for relative, tree in zip(files, trees):
        spans: list[tuple[int, str]] = [
            (node.line, node.text) for node in tree.find("comment", "string_literal", "raw_string_literal")
        ]
        for call in tree.find("preproc_call"):
            argument = call.child_by_field("argument")
            if argument is not None:
                spans.append((argument.line, argument.text))
        for body in tree.find("preproc_arg"):
            parent = body.parent
            if parent is None or parent.type not in ("preproc_def", "preproc_function_def"):
                continue
            for token in tsast.pp_tokens(body.text, first_row=body.start[0]):
                if token.kind == "string":
                    spans.append((token.row + 1, token.text))
        for line, text in spans:
            for offset, part in enumerate(text.split("\n")):
                yield relative, line + offset, part


def python_prose(root: Path, relative: Path) -> Iterator[tuple[Path, int, str]]:
    """Yield every prose line of one Python file, from its tokens.

    Args:
        root: The repository root
        relative: The repo-relative Python file

    Yields:
        (file, one-based line, text) for each line of each comment or string

    Raises:
        Unreadable: If the file does not tokenize
    """
    source = (root / relative).read_text(encoding="utf-8")
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
    except (tokenize.TokenError, SyntaxError) as exc:
        raise Unreadable(f"{relative}: {exc}") from exc
    for token in tokens:
        if token.type in PYTHON_PROSE:
            for offset, part in enumerate(token.string.split("\n")):
                yield relative, token.start[0] + offset, part


def text_lines(root: Path, relative: Path) -> Iterator[tuple[Path, int, str]]:
    """Yield every line of a file that has no parser here.

    A file that is not UTF-8 text is binary and holds no prose.

    Args:
        root: The repository root
        relative: The repo-relative file

    Yields:
        (file, one-based line, text) for each line
    """
    data = (root / relative).read_bytes()
    if b"\0" in data:
        return
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return
    for number, line in enumerate(text.split("\n"), start=1):
        yield relative, number, line


def scan(root: Path) -> tuple[int, list[str]]:
    """Report every coordination reference in the prose of the files in scope.

    Complexity: O(total bytes in scope) plus one parse of the C++ files.

    Args:
        root: The repository root

    Returns:
        (exit status, report lines), with the status as the module docstring states
    """
    files = scoped_files(root)
    cpp = [f for f in files if f.suffix in CPP_SUFFIXES]
    others = [f for f in files if f.suffix not in CPP_SUFFIXES]
    hits: list[tuple[Path, int, str]] = []
    try:
        for relative, line, text in cpp_prose(root, cpp):
            if REFERENCE.search(text):
                hits.append((relative, line, text))
        for relative in others:
            lines = python_prose(root, relative) if relative.suffix == ".py" else text_lines(root, relative)
            hits.extend(hit for hit in lines if REFERENCE.search(hit[2]))
    except Unreadable as exc:
        return 2, [f"check-no-coordination-refs: cannot read a file in scope: {exc}"]
    report = [f"{relative.as_posix()}:{line}: {text.strip()}" for relative, line, text in sorted(set(hits))]
    if report:
        report += [
            "",
            f"check-no-coordination-refs: {len(report)} line(s) name a task, a stage or a tracker entry.",
            "State what the code does, and remove the reference:",
            '  "Folded into one concept at #147"  ->  "Folded into one concept"',
            '  "arrives with #190"                ->  "arrives with the endpoint bridge"',
            "If the sentence says nothing without the reference, remove the sentence.",
            "A collision rule code (S004, W001) is not a reference.",
        ]
        return 1, report
    return 0, report


PLANTED_REFERENCES = (
    "Folded with its two siblings at #147.",
    "See task #193 for the fold.",
    "The forgery (#172, Door 2) is closed.",
    "Wired by Agent 11 in its second tier.",
    "Ported at A13.2.",
    "The wrappers arrive in tasks A10.x.",
    "The grant tier retired at A9.",
    "Stage D deletes the shim.",
    "Tracked as FIXY-V-264.",
    "The cache row fence of FOUND-I02.",
    "The cluster WRAP-Cipher-2.",
    "Per fixy-A5-016.",
    "Content-keyed since fix-18.",
    "Migrated at V-073.",
    "The attack pattern of CR-05.",
    "Part of Phase F6.",
    "Filed as #1519.",
    "The gate of GAPS-096.",
    "The fast path of PERF-2.",
)


def self_test() -> int:
    """Plant each reference family, the look-alikes and the scope edges, and check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, root: Path, want_status: int, named: list[str], unnamed: list[str]) -> None:
        """Run the scan and compare its status and the sites it names.

        Args:
            name: What the case proves
            root: The scratch repository root
            want_status: The exit status the scan must give
            named: Site prefixes the report must contain
            unnamed: Site prefixes the report must not contain
        """
        status, report = scan(root)
        text = "\n".join(report)
        held = status == want_status and all(n in text for n in named) and not any(n in text for n in unnamed)
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            print(f"       status {status}, want {want_status}\n       " + text.replace("\n", "\n       "))

    def write(root: Path, relative: str, text: str) -> None:
        """Write one file into the scratch tree.

        Args:
            root: The scratch repository root
            relative: The repo-relative path
            text: The file content
        """
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        write(root, "include/fixy/PlantedBreadcrumbs.h", "".join(f"// {line}\n" for line in PLANTED_REFERENCES))
        write(root, "include/foundation/PlantedClean.h",
              "#pragma once\n#include <cstdint>\n#define PLANTED_FLAG 1\n#if PLANTED_FLAG\n#endif\n"
              "// S004 refuses an async launch that carries no completion witness.\n"
              "// I002, F101, W001, P010 and V101 are collision rule codes.\n"
              "// UTF-8, SHA-256, AVX-512, FNV-1a, x86-64 and C++26 are not tags.\n"
              "// template <class B1, class B2> and A1 are template parameters.\n"
              "// ROW-CONTAINS-OK: a marker.  REFINED-PRE-OK: a marker too.\n"
              "// Headline #1 and item #2 number the items of a list.\n"
              "// &#123; is an entity, and #374151 is a color.\n")
        write(root, "scripts/planted_clean.sh", 'echo "$#" "${#arr[@]}"\nprintf \'%#08x\\n\' 255\n[[ "x" =~ [#0-9] ]]\n')
        write(root, "include/crucible/PlantedRuntime.h", "// Folded at #147.\n")
        write(root, "bench/planted_bench.cpp", "// Lifted to harness scope at GAPS-004y.\n// AVX-512 and x86-64 are not tags.\n")
        write(root, "CMakeLists.txt", "# Tracked as FIXY-V-264.\n")
        write(root, ".github/workflows/ci.yml", "  # The sibling guards (Stage A1).\n")
        write(root, "test/CMakeLists.txt", "# Filed as #1519.\n")
        planted = [f"include/fixy/PlantedBreadcrumbs.h:{n}:" for n in range(1, len(PLANTED_REFERENCES) + 1)]
        expect("each reference family is reported on its own line", root, 1,
               planted + ["CMakeLists.txt:1:", ".github/workflows/ci.yml:1:", "bench/planted_bench.cpp:1:"],
               ["PlantedClean.h", "planted_clean.sh", "PlantedRuntime.h", "test/CMakeLists.txt",
                "bench/planted_bench.cpp:2:"])

        for relative in ("include/fixy/PlantedBreadcrumbs.h", "CMakeLists.txt", ".github/workflows/ci.yml",
                         "bench/planted_bench.cpp"):
            (root / relative).unlink()
        expect("rule codes, directives and look-alikes scan clean", root, 0, [], [])

        # Positive controls: the line grep of the old shell guard got each of
        # these wrong, one way or the other.
        write(root, "include/fixy/Code.h",
              "#pragma once\nstruct P { int x; };\ninline P A1{};\n"
              "inline int member_read = A1.x;\ninline int token = 12;\n")
        write(root, "scripts/code.py", "A1 = type('A', (), {'x': 1})\nvalue = A1.x  # plain access\n")
        expect("a member access such as A1.x in C++ or Python code is not a stage label", root, 0, [], [])
        write(root, "include/fixy/Macro.h", '#pragma once\n#define NOTE "Folded at A10.x"\n#error Stage D is not here\n')
        write(root, "scripts/doc.py", '"""Ported at A13.2."""\nMESSAGE = f"see {1} and #147"\n')
        write(root, "src/fixy/Moved.cpp", "// Tracked as GAPS-096.\n")
        expect("macro strings, directives, docstrings, f-strings and src/fixy are prose in scope", root, 1,
               ["include/fixy/Macro.h:2:", "include/fixy/Macro.h:3:", "scripts/doc.py:1:",
                "scripts/doc.py:2:", "src/fixy/Moved.cpp:1:"], ["include/fixy/Code.h", "scripts/code.py"])

    if failures:
        print(f"check-no-coordination-refs --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-coordination-refs --self-test: every case holds")
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test or the scan, as the arguments ask.

    Args:
        argv: The command-line arguments after the program name

    Returns:
        The process exit status
    """
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv:
            print(__doc__, file=sys.stderr)
            return 2
        status, report = scan(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-no-coordination-refs: {exc}", file=sys.stderr)
        return 3
    for line in report:
        print(line, file=sys.stderr)
    if status == 0:
        print("check-no-coordination-refs: clean, no task, stage or tracker references in scope")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
