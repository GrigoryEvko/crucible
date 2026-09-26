#!/usr/bin/env python3
"""Every public trait and alias of safety::extract is re-exported through fixy::is.

THE PARITY
    include/crucible/fixy/Is.h re-exports the public surface of each
    include/crucible/safety/*Is*.h header with a using-declaration.  A new
    trait that is not re-exported compiles cleanly, and a consumer of the fixy
    umbrella never sees it.  This guard names each such trait.

WHAT IS DEMANDED
    The scan reads the parse tree of each *Is*.h header (the glob keeps a
    header marked superseded with a leading underscore in the set, because it
    ships through fixy/Is.h until the old tree is deleted).  A declaration is
    demanded when it sits directly in namespace crucible::safety::extract, at
    namespace scope, and it is one of these:
      * a bool variable template whose name ends in `_v` (the trait tier)
      * an alias declaration whose name ends in `_t` (the slot-extractor tier)
      * a using-declaration whose name is lower case and ends in `_v` or `_t`
    A trait of another type (an enum tier such as join_policy_tier_v) is not
    demanded, and neither is an upper-case `X_v` using-declaration, which
    re-exports an enum type and not a trait.  fixy/Is.h is an old-tree shim that
    the flip of its consumers deletes, so the guard does not widen the surface it
    carries.  A declaration in a nested namespace such as extract::detail is not
    public surface, and the namespace path decides that, with no name list.

WHAT COUNTS AS A RE-EXPORT
    A using-declaration directly in namespace crucible::fixy::is whose target is
    crucible::safety::extract::NAME, with or without the leading `::`.

EXIT STATUS
    0  every demanded name is re-exported
    1  at least one demanded name is not
    2  bad invocation, no *Is*.h header, no fixy/Is.h, or a file that does not parse
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

EXTRACT = ("crucible", "safety", "extract")
FIXY_IS = ("crucible", "fixy", "is")
SAFETY_DIR = "include/crucible/safety"
FIXY_IS_HEADER = "include/crucible/fixy/Is.h"
LOWER_TRAIT = re.compile(r"[a-z][a-z0-9_]*_[vt]")


def at_namespace_scope(node: tsast.Node) -> bool:
    """Say whether a declaration sits outside every class body and function body.

    Args:
        node: A declaration node

    Returns:
        True at namespace scope
    """
    return node.ancestor_of_type("field_declaration_list", "compound_statement", "lambda_expression") is None


def demanded(tree: tsast.Tree) -> set[str]:
    """Return the names a *Is*.h header declares that fixy/Is.h must re-export.

    Args:
        tree: One parsed header

    Returns:
        The demanded names
    """
    names: set[str] = set()
    for node in tree.find("alias_declaration"):
        name = node.child_by_field("name")
        if name is not None and name.text.endswith("_t") and at_namespace_scope(node) \
                and tsast.namespace_path(node) == EXTRACT:
            names.add(name.text)
    for node in tree.find("declaration"):
        declared = node.child_by_field("type")
        if declared is None or declared.type != "primitive_type" or declared.text != "bool":
            continue
        if not at_namespace_scope(node) or tsast.namespace_path(node) != EXTRACT:
            continue
        for init in node.children_of_type("init_declarator"):
            target = init.child_by_field("declarator")
            if target is not None and target.type == "identifier" and target.text.endswith("_v"):
                names.add(target.text)
    for using in tsast.using_names(tree):
        if using.is_directive or not using.target:
            continue
        if at_namespace_scope(using.node) and tsast.namespace_path(using.node) == EXTRACT \
                and LOWER_TRAIT.fullmatch(using.target[-1]):
            names.add(using.target[-1])
    return names


def reexported(tree: tsast.Tree) -> set[str]:
    """Return the extract names that fixy/Is.h re-exports into fixy::is.

    Args:
        tree: The parsed fixy/Is.h

    Returns:
        The re-exported names
    """
    names: set[str] = set()
    for using in tsast.using_names(tree):
        if using.is_directive or len(using.target) != len(EXTRACT) + 1:
            continue
        if tuple(using.target[:-1]) == EXTRACT and at_namespace_scope(using.node) \
                and tsast.namespace_path(using.node) == FIXY_IS:
            names.add(using.target[-1])
    return names


def scan(root: Path) -> tuple[int, list[str]]:
    """Compare the demanded names with the re-exports.

    Complexity: O(total nodes) over the scanned headers.

    Args:
        root: The repository root

    Returns:
        (exit status, report lines), with the status as the module docstring states
    """
    headers = sorted((root / SAFETY_DIR).glob("*Is*.h"))
    fixy = root / FIXY_IS_HEADER
    if not headers:
        return 2, [f"check-isx-parity: no *Is*.h header under {SAFETY_DIR}, so nothing is measured."]
    if not fixy.is_file():
        return 2, [f"check-isx-parity: {FIXY_IS_HEADER} does not exist."]
    try:
        trees = list(tsast.parse([*headers, fixy]))
    except tsast.ParseError as exc:
        return 2, [f"check-isx-parity: {exc}"]
    demand: set[str] = set()
    for tree in trees[:-1]:
        demand |= demanded(tree)
    missing = sorted(demand - reexported(trees[-1]))
    if missing:
        return 1, ["check-isx-parity: FAIL, public safety::extract names not re-exported in fixy/Is.h:"] \
            + [f"  {name}" for name in missing] \
            + ["", "Add `using ::crucible::safety::extract::<name>;` to namespace crucible::fixy::is for each."]
    return 0, [f"check-isx-parity: PASS, {len(demand)} public names mirrored."]


def self_test() -> int:
    """Plant mirrored, drifted and look-alike trees and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, root: Path, want_status: int, missing: list[str]) -> None:
        """Run the scan and compare the status and the exact missing set.

        Args:
            name: What the case proves
            root: The scratch repository root
            want_status: The exit status the scan must give
            missing: The names the report must list as missing, and no others
        """
        status, report = scan(root)
        listed = sorted(line.strip() for line in report if line.startswith("  "))
        held = status == want_status and listed == sorted(missing)
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            print(f"       status {status}, want {want_status}; listed {listed}, want {sorted(missing)}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        expect("no input header is a refusal, not a pass", root, 2, [])
        safety = root / SAFETY_DIR
        safety.mkdir(parents=True)
        (root / "include/crucible/fixy").mkdir(parents=True)

        def write(relative: str, text: str) -> None:
            """Write one file into the scratch tree.

            Args:
                relative: The repo-relative path
                text: The file content
            """
            (root / relative).write_text("#pragma once\n" + text, encoding="utf-8")

        write(f"{SAFETY_DIR}/IsTest.h",
              "namespace crucible::safety::extract {\ntemplate <typename T>\ninline constexpr bool is_test_drift_v = true;\n"
              "template <typename T>\nusing test_drift_value_t = int;\n"
              "template <typename T>\ninline constexpr int test_tier_v = 3;\n"
              "using ::crucible::safety::JoinPolicy_v;\n"
              "namespace detail { template <typename T> using session_base_probe_t = int; }\n}\n")
        mirrored = ("namespace crucible::fixy::is {\nusing ::crucible::safety::extract::is_test_drift_v;\n"
                    "using ::crucible::safety::extract::test_drift_value_t;\n}\n")
        write(FIXY_IS_HEADER, mirrored)
        expect("a mirrored tree passes; a non-bool trait, an enum re-export and a detail probe are not demanded",
               root, 0, [])
        write(FIXY_IS_HEADER, "namespace crucible::fixy::is {\n// using ::crucible::safety::extract::is_test_drift_v;\n}\n")
        expect("a drift names both names, and a commented re-export is not one", root, 1,
               ["is_test_drift_v", "test_drift_value_t"])
        write(FIXY_IS_HEADER, mirrored)
        write(f"{SAFETY_DIR}/_IsSuperseded.h",
              "namespace crucible::safety::extract {\ntemplate <typename T>\ninline constexpr bool is_superseded_drift_v = true;\n}\n")
        expect("a superseded _Is header is still demanded", root, 1, ["is_superseded_drift_v"])
        (safety / "_IsSuperseded.h").unlink()

        # Positive controls: the line regex of the old shell guard got each of
        # these wrong.
        write(f"{SAFETY_DIR}/IsShapes.h",
              "namespace crucible::safety {\ntemplate <typename T> inline constexpr bool is_owned_v = true;\n"
              "namespace extract {\ntemplate <typename T> inline constexpr bool is_one_line_v = true;\n"
              "using ::crucible::safety::is_owned_v;\n}\n}\n")
        expect("a one-line variable template and a using-declaration of a trait are demanded", root, 1,
               ["is_one_line_v", "is_owned_v"])
        write(FIXY_IS_HEADER, mirrored.replace("}\n", "using crucible::safety::extract::is_one_line_v;\n"
                                                      "    using ::crucible::safety::extract::is_owned_v;\n}\n"))
        expect("a re-export with no leading :: or with indentation counts", root, 0, [])

    if failures:
        print(f"check-isx-parity --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-isx-parity --self-test: every case holds")
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
        print(f"check-isx-parity: {exc}", file=sys.stderr)
        return 3
    for line in report:
        print(line, file=sys.stderr if status else sys.stdout)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
