#!/usr/bin/env python3
"""check-layer-boundary — the three-layer rule, read from the parse tree.

The tree has three layers, and each layer may name only the project roots at
or below it:

    include/foundation/  src/foundation/  names  foundation
    include/fixy/        src/fixy/os/     names  foundation, fixy
    include/crucible/    the rest of src/ names  every project root

A project root is a directory directly under include/, so a fourth root is a
violation in the two lower layers until the table below admits it.  std and
every system header are outside the rule.  The crucible layer may name every
root, so only the two lower layers are read.

WHAT COUNTS AS NAMING A ROOT
    The guard reads the parse tree of the pinned tree-sitter kit.
      * An include.  An angle include names the first component of its path.
        A quoted include first resolves against the directory of the file,
        as the compiler does, so `"../../crucible/Arena.h"` names crucible.
        A computed include must resolve through an object-like macro of the
        same file, or it is a violation, because the guard cannot see where
        it goes.
      * The first segment of a qualified name, with or without a leading
        `::`, in any position: a type, an expression, a using-declaration,
        a friend, a template argument, a base class.
      * A namespace definition at global scope, because it adds names to
        the root.
      * A using-directive and a namespace alias whose target is a root,
        because after them a name reaches the root with no qualifier.
    A comment and a string literal name nothing.

WHAT THE PARSER CANNOT READ
    A macro body is raw text.  The guard blanks its comments and literals
    with scripts/cxx_lex.py and reports each whole word that is a root
    above the layer, because a macro can paste that word into a name.  A
    file in tsast.UNPARSEABLE gets the same scan over its whole text.  A
    parse error in any other file is a violation.

There is no allowlist.  A file that needs a name from a higher layer is in
the wrong layer.

Exit 0 clean, 1 on a violation or a parse failure, 2 on a usage error, on a
file of an unknown kind in a lower layer, or on a failed self-test, 3 when
the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from cxx_lex import blank, line_of, splice  # noqa: E402

# The layer of each lower-layer directory, and the roots that layer may name.
LAYERS: tuple[tuple[str, str], ...] = (
    ("include/foundation/", "foundation"), ("src/foundation/", "foundation"),
    ("include/fixy/", "fixy"), ("src/fixy/os/", "fixy"),
)
ALLOWED: dict[str, frozenset[str]] = {
    "foundation": frozenset({"foundation"}),
    "fixy": frozenset({"foundation", "fixy"}),
    "crucible": frozenset(),
}
CPP_SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".inl", ".ipp", ".tpp", ".cxx", ".hxx")
PROSE_SUFFIXES = (".md", ".txt")


def project_roots(root: Path) -> frozenset[str]:
    """Return the name of each directory directly under include/."""
    base = root / "include"
    return frozenset(path.name for path in base.iterdir() if path.is_dir()) if base.is_dir() else frozenset()


def layer_of(rel: str) -> str:
    """Return the layer of a repo-relative path.

    Args:
        rel: A path relative to the scan root, in POSIX form

    Returns:
        foundation, fixy or crucible
    """
    for prefix, layer in LAYERS:
        if rel.startswith(prefix):
            return layer
    return "crucible"


def scope_files(root: Path) -> tuple[list[Path], list[str]]:
    """Return the C++ files of the two lower layers, sorted, and each file of an unknown kind there."""
    found: list[Path] = []
    unknown: list[str] = []
    for prefix, _ in LAYERS:
        base = root / prefix
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix in CPP_SUFFIXES:
                found.append(path)
            elif path.suffix not in PROSE_SUFFIXES:
                unknown.append(path.relative_to(root).as_posix())
    return sorted(found), sorted(unknown)


def include_target(root: Path, rel: str, spelled: str) -> str | None:
    """Return the layer or root that one include names, or None for a system header.

    Args:
        root: The scan root
        rel: The including file, relative to the scan root
        spelled: The include path with its delimiters, `<a/b.h>` or `"a/b.h"`

    Returns:
        The project root the include reaches, or None
    """
    body = spelled[1:-1].strip()
    if spelled.startswith('"'):
        beside = (root / rel).parent / body
        if beside.is_file():
            target = os.path.relpath(beside.resolve(), root.resolve())
            if target.startswith("include/"):
                return target.split("/")[1]
            return layer_of(target)
    return body.split("/")[0] if "/" in body else None


def chain_head(node: tsast.Node) -> str | None:
    """Return the first segment of a qualified name, through a leading `::`.

    Args:
        node: A qualified_identifier that no other qualified_identifier holds as its name

    Returns:
        The text of the first scope, or None for a bare `::name`
    """
    current: tsast.Node | None = node
    while current is not None and current.type == "qualified_identifier":
        scope = current.child_by_field("scope")
        if scope is not None:
            if scope.type == "template_type":
                scope = scope.child_by_field("name")
            return scope.text if scope is not None else None
        current = current.child_by_field("name")
    return None


def is_chain_root(node: tsast.Node) -> bool:
    """Return True when a qualified_identifier is not the name part of another one."""
    parent = node.parent
    return not (parent is not None and parent.type == "qualified_identifier" and node.field == "name")


def first_segment(text: str) -> str:
    """Return the first segment of a namespace path written as text."""
    return text.replace(" ", "").removeprefix("::").split("::")[0]


def named_roots(root: Path, rel: str, tree: tsast.Tree, roots: frozenset[str]) -> Iterator[tuple[int, str, str]]:
    """Yield each root that a parsed file names, as (row, root, how).

    Complexity: linear in the number of nodes of the file.

    Args:
        root: The scan root
        rel: The file, relative to the scan root
        tree: The parse tree of the file
        roots: The project roots

    Yields:
        The zero-based row, the root, and a short description of the form
    """
    macros = {node.child_by_field("name").text: node.child_by_field("value").text.strip()
              for node in tree.find("preproc_def")
              if node.child_by_field("name") is not None and node.child_by_field("value") is not None}
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        spelled = path.text.strip() if path is not None else ""
        if path is not None and path.type == "identifier":
            spelled = macros.get(path.text, "")
            if not spelled.startswith(("<", '"')):
                yield node.start[0], "?", "a computed include that no object-like macro of this file resolves"
                continue
        target = include_target(root, rel, spelled)
        if target in roots:
            yield node.start[0], target, "an include"
    for node in tree.find("qualified_identifier"):
        if is_chain_root(node):
            head = chain_head(node)
            if head in roots:
                yield node.start[0], head, "a qualified name"
    for node in tree.find("namespace_definition"):
        named = node.child_by_field("name")
        if named is not None and node.ancestor_of_type("namespace_definition") is None:
            head = first_segment(named.text)
            if head in roots:
                yield node.start[0], head, "a namespace definition at global scope"
    for node in tree.find("using_declaration"):
        for child in node.children:
            if child.type in ("identifier", "namespace_identifier") and child.text in roots:
                yield node.start[0], child.text, "a using-directive"
    for node in tree.find("namespace_alias_definition"):
        for child in node.children:
            if child.field != "name" and child.type != "comment":
                head = first_segment(child.text)
                if head in roots:
                    yield node.start[0], head, "a namespace alias"


def lexical_roots(text: str, roots: frozenset[str]) -> Iterator[tuple[int, str]]:
    """Yield each whole word of raw text that is a root, as (row, root), after the lexer blanks it."""
    joined, joins = splice(text)
    code, _ = blank(joined, blank_literals=True)
    pattern = re.compile(r"\b(" + "|".join(sorted(roots)) + r")\b") if roots else None
    if pattern is None:
        return
    for match in pattern.finditer(code):
        yield line_of(joined, joins, match.start()) - 1, match.group(1)


def scan(root: Path) -> tuple[list[str], list[str]]:
    """Find each name of a root above its layer in the two lower layers.

    Complexity: linear in the total size of the files in scope.

    Args:
        root: The scan root

    Returns:
        Each violation, and each file of an unknown kind
    """
    roots = project_roots(root)
    files, unknown = scope_files(root)
    violations: list[str] = []
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        layer = layer_of(rel)
        above = roots - ALLOWED[layer]
        source = tree.source.decode("utf-8", "replace")
        if tree.diagnostic is not None and rel not in tsast.UNPARSEABLE:
            violations.append(f"{rel}: the parser cannot read this file, so the rule cannot see it. "
                              f"{tree.diagnostic.strip()}")
            continue
        hits: set[tuple[int, str, str]] = set()
        if tree.diagnostic is not None:
            hits.update((row, name, "a word in a file the parser cannot read")
                        for row, name in lexical_roots(source, above))
        else:
            hits.update(hit for hit in named_roots(root, rel, tree, roots) if hit[1] in above or hit[1] == "?")
            for body in tree.find("preproc_arg"):
                hits.update((body.start[0] + row, name, "a word in a macro body")
                            for row, name in lexical_roots(body.text, above))
        for row, name, how in sorted(hits):
            if name == "?":
                violations.append(f"{rel}:{row + 1} — {how}, so the rule cannot see where it goes.")
            else:
                violations.append(f"{rel}:{row + 1} — a {layer} file names {name} through {how}, "
                                  f"which is above its layer.")
    return violations, unknown


def check(root: Path) -> int:
    """Run the scan and report.

    Args:
        root: The scan root

    Returns:
        0 clean, 1 on a violation, 2 on a file of an unknown kind
    """
    violations, unknown = scan(root)
    for violation in violations:
        print(f"LAYER violation: {violation}", file=sys.stderr)
    for rel in unknown:
        print(f"LAYER unread: {rel} is in a lower layer and is not a C++ file the guard reads. Give it a C++ "
              f"suffix, or move it out of the layer.", file=sys.stderr)
    if violations:
        print("check-layer-boundary: a file names a layer above its own. The order is foundation < fixy < "
              "crucible. Move the file up, or move the thing it names down. There is no allowlist.",
              file=sys.stderr)
        return 1
    if unknown:
        return 2
    print("check-layer-boundary: clean — every file names only its own layer and the layers below it.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each way to name a higher root, and each shape that names nothing, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    # Each line of the planted foundation header, and whether the guard flags it.
    planted: list[tuple[str, bool | None, str]] = [
        ("#pragma once", None, ""),
        ("#include <crucible/Arena.h>", True, "an angle include of a higher root"),
        ('#include "crucible/Arena.h"', True, "a quoted include that the -I path resolves"),
        ('#include "../../crucible/Arena.h"', True, "a quoted include that resolves beside the file"),
        ("#  include <fixy/Linear.h>", True, "an include with a space after the hash"),
        ("#define HEADER <crucible/Arena.h>", True, "a macro body that names a higher root"),
        ("#include HEADER", True, "a computed include through a macro of the file"),
        ("#include OTHER_HEADER", True, "a computed include that the file cannot resolve"),
        ("#include <foundation/Platform.h>", False, "an include of the own layer"),
        ("#include <vector>", False, "a system header"),
        ('#include "Sibling.h"', False, "a quoted include of a sibling"),
        ("// Prose may cite crucible::algebra and <crucible/Arena.h> freely.", False, "a line comment"),
        ("/* a block comment", None, ""),
        ("   that names crucible::Arena */", False, "a block comment"),
        ("namespace foundation::algebra {", None, ""),
        ("using Bad = ::crucible::Arena;", True, "a name qualified from ::"),
        ("using Relative = crucible::Arena;", True, "a relative qualified name"),
        ("using Nested = crucible::safety::Linear<int>::value_type;", True, "a long qualified name"),
        ("using ::fixy::Linear;", True, "a using-declaration"),
        ("using namespace crucible;", True, "a using-directive"),
        ("namespace up = ::crucible::safety;", True, "a namespace alias"),
        ("struct Derived : crucible::Base {};", True, "a base class"),
        ("inline int argument = sizeof(Holder<fixy::Tag>);", True, "a template argument"),
        ('inline const char* text = "crucible::Arena";', False, "a string literal"),
        ("inline int fine = 1;  // fixy::fn in a trailing comment", False, "a trailing comment"),
        ("namespace my_fixy { struct X {}; }", False, "a namespace whose name only contains a root"),
        ("inline my_fixy::X lookalike;", False, "an identifier that ends in a root name"),
        ("namespace crucible { struct Nested {}; }", False, "a nested namespace with the name of a root"),
        ("inline foundation::algebra::Bad own{};", False, "a name of the own layer"),
        ("}", None, ""),
        ("namespace crucible { struct Opened {}; }", True, "a namespace definition at global scope"),
        ("namespace crucible::safety { struct Deeper {}; }", True, "a nested namespace definition at global scope"),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("include/foundation/algebra/Planted.h", "\n".join(line for line, _, _ in planted) + "\n"),
            ("include/foundation/algebra/Sibling.h", "#pragma once\n"),
            ("include/crucible/Arena.h", "#pragma once\n"),
            ("include/fixy/Linear.h", "#pragma once\n"),
            ("include/fixy/Good.h", "#pragma once\n#include <foundation/algebra/Planted.h>\n"
                                    "namespace fixy { using Ok = ::foundation::algebra::Bad; }\n"),
            ("include/fixy/Up.h", "#pragma once\nnamespace fixy { using Up = ::crucible::Arena; }\n"),
            ("include/crucible/Top.h", "#pragma once\n#include <fixy/Good.h>\n"
                                       "namespace crucible { using A = ::fixy::Ok; }\n"),
            ("src/foundation/Planted.cpp", "static int planted_bad = fixy::value;\n"),
            ("src/fixy/os/Os.cpp", "#include <crucible/Arena.h>\n"),
            ("src/fixy/Old.cpp", "#include <crucible/Arena.h>\n"),
            ("include/newroot/Thing.h", "#pragma once\n"),
            ("include/fixy/Fourth.h", "#pragma once\n#include <newroot/Thing.h>\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        violations, unknown = scan(root)
        flagged = {int(violation.split(":", 2)[1].split(" ", 1)[0]) for violation in violations
                   if violation.startswith("include/foundation/algebra/Planted.h:")}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in flagged)
            elif caught is False:
                expect(f"not caught: {label}", line not in flagged, True)
        expect("nothing else in the planted header is reported",
               flagged <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        reported = {violation.split(":", 1)[0] for violation in violations}
        expect("caught: a fixy file that names crucible", "include/fixy/Up.h" in reported)
        expect("caught: a foundation source that names fixy", "src/foundation/Planted.cpp" in reported)
        expect("caught: an include of a higher root in src/fixy/os", "src/fixy/os/Os.cpp" in reported)
        expect("caught: a fourth root that the table does not admit", "include/fixy/Fourth.h" in reported)
        expect("not caught: a fixy file that names foundation", "include/fixy/Good.h" not in reported, True)
        expect("not caught: a file of the crucible layer", "include/crucible/Top.h" not in reported, True)
        expect("not caught: src/fixy outside os/ is the crucible layer", "src/fixy/Old.cpp" not in reported, True)
        expect("the planted tree has no file of an unknown kind", not unknown, True)

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

        code, _ = captured(root)
        expect("the planted tree fails the check", code == 1)
        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        (root / "include/fixy/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file the parser cannot read is a violation",
               any(violation.startswith("include/fixy/Broken.h") for violation in scan(root)[0]))
        for rel in ("include/fixy/Up.h", "include/foundation/algebra/Planted.h", "src/foundation/Planted.cpp",
                    "src/fixy/os/Os.cpp", "include/fixy/Fourth.h", "include/fixy/Broken.h"):
            (root / rel).unlink()
        (root / "include/fixy/Table.def").write_text("X(one)\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            code = check(root)
        expect("a file of an unknown kind in a lower layer exits 2", code == 2)
        (root / "include/fixy/Table.def").unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            code = check(root)
        expect("a clean tree passes", code == 0, True)
    if failures:
        print(f"check-layer-boundary --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-layer-boundary --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-layer-boundary.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-layer-boundary: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
