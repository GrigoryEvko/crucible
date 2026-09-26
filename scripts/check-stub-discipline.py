#!/usr/bin/env python3
"""Pair every stub honesty marker with a CRUCIBLE_STUB deprecation in its header.

THE DISCIPLINE
    An honesty marker is a constexpr bool whose name ends in `_implemented`,
    `_attached` or `_ready` and whose value is `false`, at namespace scope or as
    a static member of a class.  It is a compile-time statement that a surface
    does not ship its live behaviour: a caller sees a sentinel return code at
    runtime.

    A header that declares such a marker must also carry at least one
    `deprecated("CRUCIBLE_STUB: ...")` attribute on a declaration, so that each
    call site of the stub gets a -Wdeprecated-declarations warning at compile
    time.  Authorised callers (tests, forwarding implementations) suppress the
    warning with `#pragma GCC diagnostic`.

    The pair holds in both directions, over every header under include/:
        the header declares a marker  <=>  the header carries a CRUCIBLE_STUB deprecation
    A marker with no deprecation is a silent stub: it looks live and fails only
    at runtime.  A deprecation with no marker gives code no constant to test
    the stub by.

THE ENGINE
    The scan reads the parse tree of each header.  A marker is a `declaration`
    or a `field_declaration` node with a `constexpr` qualifier, the type `bool`
    and the value `false`.  A deprecation is an `attribute` node named
    `deprecated`, or an `__attribute__((deprecated(...)))` call, whose first
    string argument starts with `CRUCIBLE_STUB:`.  A comment or a string
    literal that holds the same text is neither.

EXIT STATUS
    0  every marker is paired and every deprecation is marked
    1  at least one pair violation
    2  bad invocation, or a header that does not parse
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)
"""

from __future__ import annotations

import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

MARKER_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*_(?:implemented|attached|ready)")
STUB_PREFIX = "CRUCIBLE_STUB:"
SCAN_ROOT = "include"


def string_value(node: tsast.Node) -> str | None:
    """Return the text of a string literal or of adjacent concatenated literals.

    Args:
        node: A `string_literal` or `concatenated_string` node

    Returns:
        The joined `string_content` text, or None for any other node
    """
    if node.type == "string_literal":
        return "".join(part.text for part in node.children_of_type("string_content"))
    if node.type == "concatenated_string":
        parts = [string_value(piece) for piece in node.children_of_type("string_literal")]
        return "".join(part for part in parts if part is not None)
    return None


def first_string_argument(arguments: tsast.Node | None) -> str | None:
    """Return the first string argument of an argument list.

    Args:
        arguments: An `argument_list` node, or None

    Returns:
        The text of the first string argument, or None when there is none
    """
    if arguments is None:
        return None
    for child in arguments.children:
        value = string_value(child)
        if value is not None:
            return value
    return None


def stub_deprecations(tree: tsast.Tree) -> Iterator[tsast.Node]:
    """Yield each deprecation attribute whose reason starts with the stub prefix.

    Both spellings count: `[[deprecated("...")]]`, where the kit gives an
    `attribute` node, and `__attribute__((deprecated("...")))`, where it gives
    a call inside an `attribute_specifier`.

    Args:
        tree: One parsed header

    Yields:
        The attribute node, or the call node, of each stub deprecation
    """
    for attribute in tree.find("attribute"):
        name = attribute.child_by_field("name")
        if name is None or name.text != "deprecated":
            continue
        reason = first_string_argument(next(iter(attribute.children_of_type("argument_list")), None))
        if reason is not None and reason.startswith(STUB_PREFIX):
            yield attribute
    for specifier in tree.find("attribute_specifier"):
        for call in specifier.descendants("call_expression"):
            function = call.child_by_field("function")
            if function is None or function.type != "identifier" or function.text != "deprecated":
                continue
            reason = first_string_argument(call.child_by_field("arguments"))
            if reason is not None and reason.startswith(STUB_PREFIX):
                yield call


def honesty_markers(tree: tsast.Tree) -> Iterator[tuple[str, tsast.Node]]:
    """Yield each constexpr bool marker that is false, with its name.

    Args:
        tree: One parsed header

    Yields:
        (marker name, declaration node) for each marker
    """
    for declaration in tree.find("declaration", "field_declaration"):
        qualifiers = {q.text for q in declaration.children_of_type("type_qualifier")}
        if "constexpr" not in qualifiers:
            continue
        declared_type = declaration.child_by_field("type")
        if declared_type is None or declared_type.type != "primitive_type" or declared_type.text != "bool":
            continue
        if declaration.type == "field_declaration":
            name = declaration.child_by_field("declarator")
            value = declaration.child_by_field("default_value")
        else:
            init = declaration.child_by_field("declarator")
            if init is None or init.type != "init_declarator":
                continue
            name = init.child_by_field("declarator")
            value = init.child_by_field("value")
        if name is None or value is None or value.type != "false":
            continue
        if name.type not in ("identifier", "field_identifier") or not MARKER_NAME.fullmatch(name.text):
            continue
        yield name.text, declaration


def scan(root: Path) -> tuple[int, list[str]]:
    """Check the marker and deprecation pair of every header under root/include.

    Complexity: O(total nodes) over the scanned headers.

    Args:
        root: The repository root to scan

    Returns:
        (exit status, report lines).  The status is 0, 1 or 2 as the module
        docstring states.
    """
    base = root / SCAN_ROOT
    headers = sorted(
        path for suffix in (".h", ".hpp") for path in base.rglob(f"*{suffix}")
        if path.relative_to(root).as_posix() not in tsast.UNPARSEABLE
    )
    report: list[str] = []
    status = 0
    try:
        trees = list(tsast.parse(headers))
    except tsast.ParseError as exc:
        return 2, [f"check-stub-discipline: {exc}"]
    for tree in trees:
        relative = Path(tree.path).relative_to(root).as_posix()
        markers = list(honesty_markers(tree))
        deprecations = list(stub_deprecations(tree))
        if markers and not deprecations:
            name, node = markers[0]
            report.append(
                f"{relative}:{node.line}: pair violation: the honesty marker {name} has no "
                f'[[deprecated("{STUB_PREFIX} ...")]] attribute in this header. Put one on '
                "each stub entry point, so that a caller sees the stub at compile time."
            )
            status = 1
        if deprecations and not markers:
            report.append(
                f"{relative}:{deprecations[0].line}: pair violation: a {STUB_PREFIX} deprecation "
                "has no honesty marker (a constexpr bool *_implemented, *_attached or *_ready "
                "that is false) in this header. Declare one, so that code can test the stub."
            )
            status = 1
    return status, report


def self_test() -> int:
    """Plant each case in a scratch tree and check the verdict of the scan.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, root: Path, want_status: int, named: list[str], unnamed: list[str]) -> None:
        """Run the scan and compare its status and the headers it names.

        Args:
            name: What the case proves
            root: The scratch repository root
            want_status: The exit status the scan must give
            named: Header names the report must contain
            unnamed: Header names the report must not contain
        """
        status, report = scan(root)
        text = "\n".join(report)
        held = status == want_status and all(n in text for n in named) and not any(n in text for n in unnamed)
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            print(f"       status {status}, want {want_status}\n       " + text.replace("\n", "\n       "))

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        headers = root / SCAN_ROOT / "crucible" / "test"
        headers.mkdir(parents=True)

        def plant(name: str, text: str) -> Path:
            """Write one header into the scratch tree.

            Args:
                name: The header file name
                text: The header source

            Returns:
                The written path
            """
            path = headers / name
            path.write_text("#pragma once\n" + text, encoding="utf-8")
            return path

        plant("Paired.h", "namespace t {\ninline constexpr bool data_plane_implemented = false;\n"
                          '[[deprecated("CRUCIBLE_STUB: not wired")]] void connect() noexcept;\n}\n')
        plant("NoMarker.h", "namespace t {\ninline constexpr bool unrelated_flag = true;\n"
                            "struct S { bool backend_ready = false; };\nvoid ordinary() noexcept;\n}\n")
        expect("a paired marker and a header with no constexpr marker pass", root, 0, [], ["Paired.h", "NoMarker.h"])

        stub = plant("Stub.h", "namespace t {\ninline constexpr bool data_plane_implemented = false;\n"
                               "void connect_stub() noexcept;\n}\n")
        expect("a marker with no deprecation is caught", root, 1, ["Stub.h"], ["Paired.h"])
        stub.unlink()

        unmarked = plant("Unmarked.h", 'namespace t {\n[[nodiscard, deprecated("CRUCIBLE_STUB: " "split")]]\n'
                                       "int connect_unmarked() noexcept;\n}\n")
        expect("a split-literal deprecation with no marker is caught", root, 1, ["Unmarked.h"], ["Paired.h"])
        unmarked.unlink()

        # Positive controls: each of these the line regex of the old shell
        # guard got wrong.
        member = plant("Member.h", "namespace t {\nstruct Swapper {\n"
                                   "    static constexpr bool data_migration_implemented = false;\n};\n}\n")
        expect("a static member marker with no deprecation is caught", root, 1, ["Member.h"], [])
        member.unlink()

        comment_pair = plant("CommentPair.h", "namespace t {\ninline constexpr bool data_plane_implemented = false;\n"
                                              '// [[deprecated("CRUCIBLE_STUB: only in a comment")]]\n'
                                              'inline constexpr char note[] = "deprecated(\\"CRUCIBLE_STUB: text\\")";\n}\n')
        expect("a deprecation text in a comment or a string does not pair a marker", root, 1, ["CommentPair.h"], [])
        comment_pair.unlink()

        comment_marker = plant("CommentMarker.h", "namespace t {\n// inline constexpr bool x_implemented = false;\n"
                                                  "struct Probe { bool stats_attached = false; };\n}\n")
        expect("a marker in a comment and a runtime bool field are not markers", root, 0, [], ["CommentMarker.h"])
        comment_marker.unlink()

        gnu = plant("Gnu.h", "namespace t {\ninline constexpr bool backend_attached = false;\n"
                             '__attribute__((deprecated("CRUCIBLE_STUB: gnu spelling"))) void attach() noexcept;\n}\n')
        expect("the __attribute__ spelling pairs a marker", root, 0, [], ["Gnu.h"])
        gnu.unlink()

        moved = root / SCAN_ROOT / "fixy"
        moved.mkdir()
        (moved / "Moved.h").write_text("#pragma once\nnamespace t {\ninline constexpr bool backend_implemented = false;\n}\n",
                                       encoding="utf-8")
        expect("an unpaired marker under include/fixy is caught", root, 1, ["include/fixy/Moved.h"], ["Paired.h"])

    if failures:
        print(f"check-stub-discipline --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-stub-discipline --self-test: every case holds")
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
        print(f"check-stub-discipline: {exc}", file=sys.stderr)
        return 3
    for line in report:
        print(line, file=sys.stderr)
    if status == 0:
        print("check-stub-discipline: PASS, every honesty marker is paired with a CRUCIBLE_STUB deprecation")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
