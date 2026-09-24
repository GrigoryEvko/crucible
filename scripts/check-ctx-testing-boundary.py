#!/usr/bin/env python3
"""check-ctx-testing-boundary — the testing door stays out of the code that ships.

The testing door hands out a context with no key.  Both trees have one:
foundation::effects::testing in the new tree and crucible::effects::testing in
the old tree.  bg(), init(), test() and foreground() each mint a context, and
TestWitness and ForegroundWitness are the friends that reach the keys.  A
context is what every ctx-bound mint checks for, so a use of the door in code
that ships hands out authority that no mint gave.

WHAT COUNTS AS A USE
    The guard reads the code of each file after scripts/cxx_lex.py blanks its
    comments and literals, so a comment or a literal that names the door is
    not a use.  A use is:
      * a factory or a friend named through a namespace called testing, with
        any qualifier before it;
      * a friend by its own name;
      * a using-directive and a namespace alias whose target is a namespace
        called testing;
      * a factory or a friend named through an alias of a testing namespace,
        and a using-directive of such an alias.  The aliases come from every
        file of the scan, chains of aliases included, so an alias that one
        header defines still marks a use in another file.

SCOPE
    include/foundation, include/fixy, include/crucible, src, vessel, tools and
    examples.  test/, bench/ and fuzz/ are not read, because taking the test
    path is what they are for.

THE ALLOWLIST
    scripts/ctx-testing-boundary-allowlist.txt admits the uses of a file:
    `path xN  — reason`, where N is the number of uses the file has (1 when
    absent).  A new use in a listed file exceeds its count and fails, and an
    entry above the count of its file is stale and fails, so the list drains
    with the code.

WHAT THE GUARD CANNOT SEE
    A name of the door that a macro builds with `##`.

Exit 0 clean, 1 on a use that no entry admits or a stale entry, 2 on a usage
error or a failed self-test.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cxx_lex import blank, line_of, splice  # noqa: E402

SCAN_DIRS = ("include/foundation", "include/fixy", "include/crucible", "src", "vessel", "tools", "examples")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".c", ".cc", ".cpp", ".cxx", ".cppm"})
ALLOWLIST = "scripts/ctx-testing-boundary-allowlist.txt"
MEMBERS = r"(?:bg|init|test|foreground|TestWitness|ForegroundWitness)"
ALIAS = re.compile(r"\bnamespace\s+(\w+)\s*=\s*((?:::\s*)?\w+(?:\s*::\s*\w+)*)\s*;")
ENTRY = re.compile(r"^(?P<path>\S+?)(?: x(?P<count>[1-9][0-9]*))?\s+—\s+\S")


def code_of(path: Path) -> tuple[str, str, list[int]]:
    """Return the joined text of a file, its code with comments and literals blanked, and its line joins."""
    joined, joins = splice(path.read_text(errors="replace"))
    return joined, blank(joined, blank_literals=True)[0], joins


def door_names(codes: dict[str, str]) -> set[str]:
    """Return `testing` and each alias of the scan that reaches a namespace called testing.

    Complexity: linear in the size of the code, plus the aliases times the
    length of their longest chain.
    """
    targets: dict[str, set[tuple[str, ...]]] = defaultdict(set)
    for code in codes.values():
        for match in ALIAS.finditer(code):
            targets[match.group(1)].add(tuple(part for part in re.sub(r"\s+", "", match.group(2)).split("::")
                                              if part))
    names = {"testing"}
    changed = True
    while changed:
        changed = False
        for alias, paths in targets.items():
            if alias not in names and any(path and path[-1] in names for path in paths):
                names.add(alias)
                changed = True
    return names


def door_pattern(names: set[str]) -> re.Pattern[str]:
    """Return the pattern of one use of the door, for the names that reach a testing namespace."""
    spelled = "|".join(sorted(names))
    return re.compile(rf"(?<!\w)(?:{spelled})\s*::\s*{MEMBERS}\b"
                      rf"|\b(?:Test|Foreground)Witness\b"
                      rf"|\busing\s+namespace\s+(?:::\s*)?(?:\w+\s*::\s*)*(?:{spelled})\b"
                      rf"|\bnamespace\s+\w+\s*=\s*(?:::\s*)?(?:\w+\s*::\s*)*(?:{spelled})\s*;")


def scan(root: Path) -> dict[str, list[int]]:
    """Return the lines of each use of the door, by file.

    Complexity: linear in the total size of the files in scope.
    """
    texts: dict[str, tuple[str, str, list[int]]] = {}
    for directory in SCAN_DIRS:
        base = root / directory
        if base.is_dir():
            for path in sorted(base.rglob("*")):
                if path.suffix in SUFFIXES and path.is_file():
                    texts[path.relative_to(root).as_posix()] = code_of(path)
    pattern = door_pattern(door_names({rel: code for rel, (_, code, _) in texts.items()}))
    found: dict[str, list[int]] = {}
    for rel, (joined, code, joins) in texts.items():
        lines = [line_of(joined, joins, match.start()) for match in pattern.finditer(code)]
        if lines:
            found[rel] = lines
    return found


def read_allowlist(path: Path) -> tuple[dict[str, tuple[int, int]], list[str]]:
    """Return each listed path with the number of uses it admits and its line, and each malformed row."""
    entries: dict[str, tuple[int, int]] = {}
    problems: list[str] = []
    for number, raw in enumerate(path.read_text().splitlines() if path.is_file() else [], 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if match is None:
            problems.append(f"{ALLOWLIST}:{number} has no path, count and reason.")
        elif match.group("path") in entries:
            problems.append(f"{ALLOWLIST}:{number} lists {match.group('path')} a second time.")
        else:
            entries[match.group("path")] = (int(match.group("count") or 1), number)
    return entries, problems


def check(root: Path) -> int:
    """Compare the uses of the door with the allowlist and report.

    Returns:
        0 clean, 1 on a use that no entry admits, a stale entry or a malformed row
    """
    found = scan(root)
    entries, problems = read_allowlist(root / ALLOWLIST)
    for path, lines in sorted(found.items()):
        admitted = entries.get(path, (0, 0))[0]
        if len(lines) > admitted:
            problems.append(f"{path} uses the testing door {len(lines)} time(s) at lines "
                            f"{', '.join(map(str, lines))}, and its entry admits {admitted}.  The door mints a "
                            f"context with no key.  Code that ships takes its context from a real mint.  A "
                            f"self-test in a header may use the door: give its file an entry with the count and a "
                            f"sentence saying why.")
    for path, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        if len(found.get(path, [])) < admitted:
            problems.append(f"{ALLOWLIST}:{number} admits {admitted} use(s) in {path}, and the file has "
                            f"{len(found.get(path, []))}.  Lower or remove the entry.")
    for problem in problems:
        print(f"check-ctx-testing-boundary: {problem}", file=sys.stderr)
    total = sum(len(lines) for lines in found.values())
    print(f"check-ctx-testing-boundary: {total} use(s) of the testing door in {len(found)} file(s), "
          f"{'refused' if problems else 'each one admitted'}.", file=sys.stderr)
    return 1 if problems else 0


def self_test() -> int:
    """Plant uses in both trees and through aliases, and each shape that is not a use, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = {
        "include/foundation/effects/Planted.h":
            "#pragma once\ninline auto forged() noexcept { return ::foundation::effects::testing::init(); }\n"
            "inline auto forged_foreground() noexcept { return ::foundation::effects::testing::foreground(); }\n",
        "src/planted.cpp":
            "inline int old_tree() { using namespace crucible::effects; return testing::bg(); }\n"
            "namespace eff = ::crucible::effects;\n"
            "inline int through_alias() { return eff::testing::init(); }\n"
            "inline int in_scope() { using namespace crucible::effects::testing; return bg(); }\n",
        "include/crucible/effects/Door.h": "#pragma once\nnamespace door = ::crucible::effects::testing;\n",
        "src/cross.cpp": "#include <crucible/effects/Door.h>\nnamespace d2 = door;\n"
                         "inline int far() { return d2::bg(); }\n",
        "src/directive.cpp": "#include <crucible/effects/Door.h>\n"
                             "inline int near() { using namespace door; return 0; }\n",
        "include/crucible/effects/Clean.h":
            "#pragma once\n// effects::testing::bg() hands out a context, so this header never calls it.\n"
            "inline const char* note = \"testing::bg() and TestWitness\";\n"
            "namespace quiet = ::crucible::effects;\ninline int unrelated() { return quiet::other(); }\n",
        "include/crucible/effects/Listed.h":
            "#pragma once\ninline void self_test() { (void)::crucible::effects::testing::bg(); "
            "(void)::crucible::effects::testing::test(); }\n",
        "test/t.cpp": "inline auto t() { return effects::testing::test(); }\n",
        "bench/b.cpp": "inline auto b() { return effects::testing::bg(); }\n",
        ALLOWLIST: "include/crucible/effects/Listed.h x2  — a planted self-test\n"
                   "include/crucible/effects/Door.h x1  — the planted door alias\n",
    }
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in planted.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        found = scan(root)
        expect("caught: two uses in the new tree", len(found.get("include/foundation/effects/Planted.h", [])) == 2)
        expect("caught: three uses in the old tree, through a namespace alias and a using-directive",
               len(found.get("src/planted.cpp", [])) == 3)
        expect("caught: an alias of a testing namespace", len(found.get("include/crucible/effects/Door.h", [])) == 1)
        expect("caught: a use through an alias of another file, by a chain of aliases",
               len(found.get("src/cross.cpp", [])) == 2)
        expect("caught: a using-directive of an alias of another file", len(found.get("src/directive.cpp", [])) == 1)
        expect("not caught: a comment, a literal and an alias of another namespace",
               "include/crucible/effects/Clean.h" not in found)
        expect("not caught: test and bench code", not any(rel.startswith(("test/", "bench/")) for rel in found))

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        code, report = captured(root)
        expect("unlisted uses fail, and the listed files pass",
               code == 1 and "Planted.h uses the testing door 2 time(s)" in report and "Listed.h uses" not in report)
        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        for rel in ("include/foundation/effects/Planted.h", "src/planted.cpp", "src/cross.cpp", "src/directive.cpp"):
            (root / rel).unlink()
        with (root / "include/crucible/effects/Listed.h").open("a", encoding="utf-8") as listed:
            listed.write("inline void more() { (void)::crucible::effects::testing::init(); }\n")
        code, report = captured(root)
        expect("a new use in a listed file fails", code == 1 and "Listed.h uses the testing door 3 time(s)" in report)
        (root / ALLOWLIST).write_text("include/crucible/effects/Listed.h x4  — a planted self-test\n"
                                      "include/crucible/effects/Door.h x1  — the planted door alias\n"
                                      "include/crucible/effects/Absent.h  — never existed\n", encoding="utf-8")
        code, report = captured(root)
        expect("an entry above the count of its file, and an entry for a file with no use, are stale",
               code == 1 and "admits 4 use(s) in include/crucible/effects/Listed.h, and the file has 3" in report
               and "admits 1 use(s) in include/crucible/effects/Absent.h, and the file has 0" in report)
        (root / ALLOWLIST).write_text("include/crucible/effects/Listed.h x3  — a planted self-test\n"
                                      "include/crucible/effects/Door.h x1  — the planted door alias\n"
                                      "include/crucible/effects/Door.h x1  — twice\n", encoding="utf-8")
        code, report = captured(root)
        expect("a path listed twice fails", code == 1 and "a second time" in report)
        (root / ALLOWLIST).write_text("include/crucible/effects/Listed.h x3  — a planted self-test\n"
                                      "include/crucible/effects/Door.h x1  — the planted door alias\n",
                                      encoding="utf-8")
        expect("a satisfied list passes", captured(root)[0] == 0)
    if failures:
        print(f"check-ctx-testing-boundary --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-ctx-testing-boundary --self-test: every case passes.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-ctx-testing-boundary.py [--self-test]", file=sys.stderr)
        return 2
    return self_test() if argv else check(Path(__file__).resolve().parent.parent)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
