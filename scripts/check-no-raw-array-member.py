#!/usr/bin/env python3
"""check-no-raw-array-member.py: no C array data member in the new tree.

A subscript of a C array member is checked by no build.  The standard
library's debug assertions check std::array::operator[], and UBSan's bounds
check sees only a subscript whose bound it can read.  A write past the end of
a C array member lands in the next member of the same object, and the address
sanitizer sees valid memory there, so the overflow passes every preset.  A
std::array or a fixy::FixedArray member has the same layout and routes every
subscript through a checked operator[].

The gate parses include/foundation and include/fixy with the pinned
tree-sitter kit.  A data member is refused when the declarator nearest to
its name is an array declarator: `T name[N]` and `T* name[N]` (an array of
pointers) are refused, and `T (*name)[N]` (a pointer to an array) is not.
An alias declaration or a typedef that names an array type is refused as
well, because a member declared through it has an array type too.

Two kinds of declaration are admitted.  A static data member is not part
of the object layout, so an overflow of it reaches no neighbour member.  A
declaration inside a namespace whose name ends in `_self_test` is a probe
type that a header self-test uses to exercise the array case on purpose.

The reviewed exceptions are in scripts/raw-array-member-allowlist.txt, one
line per member: `path name reason`.  An entry that no longer matches a
member fails the gate, so the list only shrinks.

What the gate cannot see: an array type produced by a template (for example
a member of type `T` instantiated with `int[4]`), and a C array reached
through an alias declared outside the two roots.

Exit codes: 0 clean, 1 findings, 2 a stale allowlist entry or a failed
self-test, 3 the pinned tree-sitter kit is absent (ctest reports a skip).
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

ROOTS = ("include/foundation", "include/fixy")
ALLOWLIST = tsast.REPO_ROOT / "scripts" / "raw-array-member-allowlist.txt"
PROBE_SUFFIX = "_self_test"

# The declarator kinds that sit between a name and the constructor that
# decides its type.  A parenthesized declarator changes the binding only, and
# an attribute or a trailing qualifier does not change the type kind.
_TRANSPARENT = frozenset({"parenthesized_declarator", "attributed_declarator"})
_CONSTRUCTORS = frozenset({
    "array_declarator", "pointer_declarator", "reference_declarator",
    "function_declarator",
})


def _is_probe(node: tsast.Node) -> bool:
    """Return true when a namespace that encloses the node ends in _self_test.

    The segments come from tsast.namespace_path, so a comment inside a nested
    namespace name does not hide the segment.

    Args:
        node: Any node of the parsed file

    Returns:
        Whether the declaration sits in a header self-test namespace
    """
    return any(part.endswith(PROBE_SUFFIX) for part in tsast.namespace_path(node))


def _nearest_constructor(name: tsast.Node, stop: tsast.Node) -> str | None:
    """Return the type constructor nearest to a declared name.

    Args:
        name: The identifier node that names the member
        stop: The declaration node, where the walk ends

    Returns:
        The type of the nearest constructor node, or None for a plain name
    """
    owner = name.parent
    while owner is not None and owner.index != stop.index:
        if owner.type in _CONSTRUCTORS:
            return owner.type
        if owner.type not in _TRANSPARENT:
            return None
        owner = owner.parent
    return None


_ABSTRACT_TRANSPARENT = frozenset({"abstract_parenthesized_declarator"})
_ABSTRACT_CONSTRUCTORS = frozenset({
    "abstract_array_declarator", "abstract_pointer_declarator",
    "abstract_reference_declarator", "abstract_function_declarator",
})


def _abstract_kind(declarator: tsast.Node | None) -> str | None:
    """Return the constructor that decides the kind of an abstract declarator.

    An abstract declarator has no name, so the constructor that would sit
    nearest to the name is the deepest one in the chain: `int(*)[4]` is a
    pointer and `int*[4]` is an array.

    Args:
        declarator: The declarator field of a type_descriptor, or None

    Returns:
        The type of the deepest constructor node, or None for no declarator
    """
    kind: str | None = None
    node = declarator
    while node is not None:
        if node.type in _ABSTRACT_CONSTRUCTORS:
            kind = node.type
        elif node.type not in _ABSTRACT_TRANSPARENT:
            break
        node = node.child_by_field("declarator")
    return kind


def _alias_findings(tree: tsast.Tree) -> list[tuple[int, str, str]]:
    """Return each alias declaration and typedef that names an array type.

    Args:
        tree: The parsed file

    Returns:
        One (line, name, what) tuple for each refused alias
    """
    found: list[tuple[int, str, str]] = []
    for alias in tree.find("alias_declaration"):
        if _is_probe(alias):
            continue
        name = alias.child_by_field("name")
        descriptor = alias.child_by_field("type")
        if name is None or descriptor is None:
            continue
        if _abstract_kind(descriptor.child_by_field("declarator")) == "abstract_array_declarator":
            found.append((alias.line, name.text, "an alias of a C array type"))
    for typedef in tree.find("type_definition"):
        if _is_probe(typedef):
            continue
        for name in typedef.descendants("type_identifier"):
            if name.field == "type":
                continue
            if _nearest_constructor(name, typedef) == "array_declarator":
                found.append((typedef.line, name.text, "a typedef of a C array type"))
    return found


def _is_static(decl: tsast.Node) -> bool:
    """Return true when a member declaration carries static or thread_local.

    Args:
        decl: A field_declaration node

    Returns:
        Whether the member has static storage
    """
    return any(
        child.type == "storage_class_specifier" and child.text in ("static", "thread_local")
        for child in decl.children
    )


def findings(tree: tsast.Tree) -> list[tuple[int, str, str]]:
    """Return every refused declaration in one parsed file.

    Args:
        tree: The parsed file

    Returns:
        One (line, name, what) tuple for each refused declaration, in source order
    """
    found: list[tuple[int, str, str]] = []
    for decl in tree.find("field_declaration"):
        if decl.ancestor_of_type("field_declaration_list") is None or _is_static(decl) or _is_probe(decl):
            continue
        for name in decl.descendants("field_identifier"):
            if _nearest_constructor(name, decl) == "array_declarator":
                found.append((name.line, name.text, "a C array data member"))
    found.extend(_alias_findings(tree))
    return found


def load_allowlist(path: Path = ALLOWLIST) -> tuple[dict[tuple[str, str], str], list[str]]:
    """Read the reviewed exceptions, keyed by (path, member name).

    Args:
        path: The allowlist file

    Returns:
        The reason for each admitted member, and one message for each row
        that is not `path name reason`
    """
    admitted: dict[tuple[str, str], str] = {}
    malformed: list[str] = []
    if not path.is_file():
        return admitted, malformed
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(maxsplit=2)
        if len(fields) != 3:
            malformed.append(f"{path.name}:{number}: `{line}` is not `path name reason`. "
                             "Give the path, the member name and the reason.")
            continue
        member_path, name, reason = fields
        admitted[(member_path, name)] = reason
    return admitted, malformed


def scan(paths: list[Path]) -> tuple[dict[tuple[str, str], list[int]], list[str]]:
    """Parse the files and collect each refused declaration by (path, name).

    Args:
        paths: The files to parse

    Returns:
        The lines of each refused declaration, keyed by (path, name), and one
        message for each file that the parser cannot read
    """
    hits: dict[tuple[str, str], list[int]] = {}
    unread: list[str] = []
    for tree in tsast.parse(paths, strict=False):
        if tree.diagnostic is not None:
            unread.append(f"{tree.path}: the parser cannot read this file, so its members are unknown.\n"
                          f"  {tree.diagnostic}")
            continue
        for line, name, _ in findings(tree):
            hits.setdefault((str(tree.path), name), []).append(line)
    return hits, unread


def run() -> int:
    """Run the gate over the two roots and report.

    Returns:
        0 when clean, 1 on a finding or an unreadable file, 2 on a stale or
        malformed allowlist entry
    """
    hits, unread = scan(tsast.cpp_files(*ROOTS))
    admitted, malformed = load_allowlist()
    refused = {key: lines for key, lines in hits.items() if key not in admitted}
    stale = sorted(key for key in admitted if key not in hits)
    for (path, name), lines in sorted(refused.items()):
        for line in lines:
            print(f"{path}:{line}: `{name}` is a C array member. "
                  "Use std::array or fixy::FixedArray, so each subscript is checked.")
    for message in unread:
        print(message)
    for path, name in stale:
        print(f"{ALLOWLIST.relative_to(tsast.REPO_ROOT)}: stale entry `{path} {name}`: "
              "no C array member has that name there now. Delete the entry.")
    for message in malformed:
        print(message)
    if stale or malformed:
        return 2
    if refused or unread:
        return 1
    print(f"check-no-raw-array-member: clean ({len(admitted)} reviewed exceptions)")
    return 0


def self_test() -> int:
    """Plant the declarations the gate must refuse and the ones it must admit.

    Returns:
        0 when every case holds, 2 otherwise
    """
    fixture = """
namespace foundation {
struct Refused { int values[4]{}; long count = 0; };
struct PointerArray { int* slots[2]{}; };
template <class T, unsigned N> class Buffer { T data_[N]{}; };
union Shared { unsigned char bytes[8]; long word; };
using Row = int[4];
using RowOfPointers = int*[4];
typedef char Name[16];
inline void f() { struct Local { char text[8]; }; }
using PointerToRow = int(*)[4];
typedef int (*FnPointerToRow)[4];
using Plain = int;
struct Admitted {
    static constexpr int table[3] = {1, 2, 3};
    int (*to_array)[4] = nullptr;
    int scalar = 0;
    void g(int param[4]);
};
namespace detail::layout_self_test {
struct Probe { double weight[2]; };
using ProbeRow = int[2];
}
namespace probe::comment_self_test /* a comment inside the name */ ::inner {
struct CommentProbe { int cells[2]; };
}
}
"""
    must_refuse = {"values", "slots", "data_", "bytes", "Row", "RowOfPointers", "Name", "text"}
    must_admit = {"table", "to_array", "scalar", "param", "weight", "ProbeRow", "count", "word",
                  "PointerToRow", "FnPointerToRow", "Plain", "cells"}
    failures: list[str] = []
    cases: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case result and print it."""
        cases.append(name)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        path = root / "fixture.h"
        path.write_text(fixture, encoding="utf-8")
        hits, unread = scan([path])
        refused = {name for _, name in hits}
        expect("the fixture parses", not unread)
        broken = root / "broken.h"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file the parser cannot read is reported, not raised", bool(scan([broken])[1]))
        allow = root / "allow.txt"
        allow.write_text("# comment\na.h values the reason\na.h values\n", encoding="utf-8")
        admitted, malformed = load_allowlist(allow)
        expect("a row with its reason is read", admitted == {("a.h", "values"): "the reason"})
        expect("a row with no reason is malformed", len(malformed) == 1 and "a.h values" in malformed[0])
    for name in sorted(must_refuse):
        expect(f"refuses `{name}`", name in refused)
    for name in sorted(must_admit):
        expect(f"admits `{name}`", name not in refused)
    if failures:
        print(f"check-no-raw-array-member --self-test: FAILED on {', '.join(failures)}")
        return 2
    print(f"check-no-raw-array-member --self-test: {len(cases)} cases hold, {len(must_refuse)} of them refusals")
    return 0


if __name__ == "__main__":
    try:
        if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
            sys.exit(self_test())
        sys.exit(run())
    except tsast.KitMissing as exc:
        print(f"check-no-raw-array-member: {exc}", file=sys.stderr)
        sys.exit(3)
