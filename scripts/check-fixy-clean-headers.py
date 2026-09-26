#!/usr/bin/env python3
"""check-fixy-clean-headers — a certified header reaches the safety substrate only through fixy.

A migration sweep moved the top-level include/crucible/*.h headers from the
`crucible::safety` namespace onto the fixy umbrella.  Each header on the
registry scripts/fixy-clean-headers.txt is certified clean, and this guard
keeps it clean.  The registry is a promise, not an exemption: it only grows.

THE RULE
    A registered header names the `safety` namespace nowhere:
      * no namespace name `safety` in a qualified name, a nested namespace
        specifier or a namespace definition (`crucible::safety::X`,
        `safety::X`, `namespace crucible::safety {`)
      * no namespace alias and no using-declaration or using-directive whose
        target names `safety` (`namespace saf = crucible::safety;`,
        `using namespace ::crucible::safety;`).  The alias counts at its
        definition, so each use of the alias needs no resolution
      * no comment and no string literal that spells `safety::`.  A certified
        header documents the fixy spelling, and prose that writes the old one
        is the first sign of a regression
    An include path such as <crucible/safety/Decide.h> is a file name, not a
    namespace, so it does not count.  A variable named `safety` is no
    namespace name either.

WHAT READS THE HEADER
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py).  The
    namespace rules read name nodes only: each namespace name, and the target
    of each namespace alias and using declaration.  The prose rule reads the
    text of comment and literal nodes, which is prose and data, not code.

Usage
    check-fixy-clean-headers.py              scan the registry
    check-fixy-clean-headers.py --list       print the registry
    check-fixy-clean-headers.py --self-test  plant each case and examine each verdict

Exit 0 clean, 1 when a registered header regressed or does not parse, 2 when a
registered path is missing, the registry is missing, or the self-test fails,
3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

REGISTRY = "scripts/fixy-clean-headers.txt"
BANNED_NAMESPACE = "safety"
BANNED_PROSE = "safety::"


@dataclass(frozen=True)
class Hit:
    """One line of a registered header that names the safety namespace."""

    path: str
    line: int
    what: str


def registered_paths(root: Path) -> list[str] | None:
    """Return the registry paths, or None when the registry is missing.

    Args:
        root: The repository root

    Returns:
        The paths in registry order
    """
    registry = root / REGISTRY
    if not registry.is_file():
        return None
    paths = [line.split("#", 1)[0].strip() for line in registry.read_text().splitlines()]
    return [path for path in paths if path]


def hits_in(tree: tsast.Tree, rel: str) -> list[Hit]:
    """Return every line of one parsed header that names the safety namespace.

    Complexity: linear in the node count of the header.

    Args:
        tree: The parsed header
        rel: Its path relative to the repository root

    Returns:
        One hit for each line and kind, in line order
    """
    found: dict[tuple[int, str], Hit] = {}

    def add(line: int, what: str) -> None:
        found.setdefault((line, what), Hit(rel, line, what))

    for node in tree.find("namespace_identifier"):
        if tsast.leaf_name(node) == BANNED_NAMESPACE:
            add(node.line, "names the safety namespace")
    # The last part of an alias or using target is an identifier node, not a
    # namespace_identifier, so the targets come from the shared readers.
    targets = [(alias.node, alias.target) for alias in tsast.namespace_aliases(tree)]
    targets += [(using.node, using.target) for using in tsast.using_names(tree)]
    for holder, target in targets:
        if BANNED_NAMESPACE in target:
            add(holder.line, "names the safety namespace")
    for node in tsast.prose_nodes(tree):
        prose = tsast.prose_text(node)
        offset = prose.find(BANNED_PROSE)
        while offset != -1:
            add(node.line + prose.count("\n", 0, offset), "spells safety:: in prose")
            offset = prose.find(BANNED_PROSE, offset + 1)
    return sorted(found.values(), key=lambda hit: (hit.line, hit.what))


def check(root: Path) -> int:
    """Check every registered header and report.

    Args:
        root: The repository root

    Returns:
        0 clean, 1 on a regression or a parse failure, 2 on a missing path or registry
    """
    paths = registered_paths(root)
    if paths is None:
        print(f"check-fixy-clean-headers: registry not found: {REGISTRY}", file=sys.stderr)
        return 2
    missing = [rel for rel in paths if not (root / rel).is_file()]
    for rel in missing:
        print(f"FIXY-CLEAN registry drift: {rel} — registered but missing (renamed or deleted?).", file=sys.stderr)
    present = [rel for rel in paths if rel not in missing]
    regressions = 0
    unreadable = 0
    for tree in tsast.parse([root / rel for rel in present], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            print(f"FIXY-CLEAN parse failure: {rel} — the parser cannot read it, so its namespaces are unknown.\n"
                  f"  {tree.diagnostic}", file=sys.stderr)
            unreadable += 1
            continue
        for hit in hits_in(tree, rel):
            print(f"FIXY-CLEAN regression: {hit.path}:{hit.line} — {hit.what} in a fixy-certified header; route "
                  f"through fixy::.", file=sys.stderr)
            regressions += 1
    if missing:
        print(f"check-fixy-clean-headers: {len(missing)} registry path(s) missing — update {REGISTRY}.",
              file=sys.stderr)
        return 2
    if regressions or unreadable:
        print(f"\ncheck-fixy-clean-headers detected {regressions} regression(s) and {unreadable} unreadable "
              f"header(s).  A certified header reached past the fixy umbrella to the safety substrate.\n"
              f"  (1) Replace the reference with its fixy re-export (safety::Tagged -> fixy::wrap::Tagged).\n"
              f"  (2) If the line is prose, describe the fixy spelling instead.\n"
              f"  (3) If the header cannot route through the umbrella, take it off {REGISTRY} with a reason.",
              file=sys.stderr)
        return 1
    print(f"check-fixy-clean-headers: clean — {len(present)} certified header(s) name no safety namespace.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each case and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case result and print it."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    def captured(root: Path) -> tuple[int, str]:
        """Run the check on a planted root and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check(root)
        return code, buffer.getvalue()

    header = "include/crucible/planted/H.h"
    clean = ("#pragma once\n#include <crucible/safety/Decide.h>\n#include <fixy/Wrap.h>\n"
             "namespace crucible::planted {\nbool safety = true;\n"
             "using CleanAlias = ::fixy::wrap::Tagged<int, int>;\n}\n")
    cases = [
        ("a header through the umbrella passes; an include path and a variable are no namespace", clean, []),
        ("a qualified spelling is caught", "using Dirty = ::crucible::safety::Tagged<int, int>;\n", [1]),
        ("a relative spelling is caught", "namespace crucible { using Dirty = safety::Tagged<int, int>; }\n", [1]),
        ("a spelling split over lines is caught", "using Dirty = ::crucible::\n    safety::Tagged<int, int>;\n", [2]),
        ("a namespace alias to safety is caught at its definition",
         "namespace saf = crucible::safety;\nusing Dirty = saf::Tagged<int, int>;\n", [1]),
        ("an alias whose target is on the next line is caught at its definition and at the name",
         "void f() {\n    namespace saf =\n        crucible::safety;\n}\n", [2, 3]),
        ("a using-directive for safety is caught", "using namespace ::crucible::safety;\n", [1]),
        ("a relative using-directive is caught", "namespace crucible { using namespace safety; }\n", [1]),
        ("a using-declaration is caught", "using ::crucible::safety::Tagged;\n", [1]),
        ("a namespace definition is caught", "namespace crucible::safety { int value; }\n", [1]),
        ("prose in a block comment is caught on its own line", "/* the old\n   safety::Tagged spelling */\n", [2]),
        ("prose in a string literal is caught", 'inline const char* text = "safety::Tagged";\n', [1]),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "scripts").mkdir()
        (root / "include/crucible/planted").mkdir(parents=True)
        (root / REGISTRY).write_text(f"# planted\n{header}\n")
        for name, text, lines in cases:
            (root / header).write_text(text)
            tree = next(tsast.parse([root / header], strict=False))
            got = sorted({hit.line for hit in hits_in(tree, header)})
            expect(name, tree.diagnostic is None and got == lines)
            if got != lines:
                print(f"       expected lines {lines}, got {got}")

        (root / header).write_text(clean)
        code, _ = captured(root)
        expect("a clean registry exits 0", code == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(root)
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == captured(root))

        (root / header).write_text("namespace saf = crucible::safety;\n")
        code, report = captured(root)
        expect("a regression exits 1 and names the line", code == 1 and f"{header}:1" in report)
        (root / header).write_text("namespace broken { void f() { g(1) { } } }\n")
        code, report = captured(root)
        expect("a header the parser cannot read exits 1", code == 1 and "parse failure" in report)
        (root / header).unlink()
        code, report = captured(root)
        expect("a missing registered header exits 2", code == 2 and "registered but missing" in report)
        (root / REGISTRY).unlink()
        expect("a missing registry exits 2", captured(root)[0] == 2)

    if failures:
        print(f"check-fixy-clean-headers --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-fixy-clean-headers --self-test: every case passes.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    try:
        if argv == []:
            return check(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--list"]:
            paths = registered_paths(tsast.REPO_ROOT) or []
            print("check-fixy-clean-headers: certified-clean registry:")
            for path in paths:
                print(f"  {path}")
            return 0
    except tsast.KitMissing as exc:
        print(f"check-fixy-clean-headers: {exc}", file=sys.stderr)
        return 3
    print("usage: check-fixy-clean-headers.py [--list | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
