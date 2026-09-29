#!/usr/bin/env python3
"""check-permission-storage — a Permission held as a class member is a handle's `[[no_unique_address]]` member.

A Permission<Tag> token is linear: its copy is deleted, it is
`[[nodiscard]]`, and its size is one byte.  A class member that holds one is
correct when the class IS a handle that proves single ownership through its
lifetime (Pinned, move-only, nested in a channel or a session class).  The
handle shape is:

    class FooHandle {
        FooState& state_;
        [[no_unique_address]] Permission<tag> perm_;
    };

`[[no_unique_address]]` is the visible mark that the storage is deliberate:
it folds the token to zero bytes, and it announces the handle pattern.  A
member with no such mark is the pattern that the header comment of
include/foundation/permissions/Permission.h forbids: the holder can be
aliased across threads, and the type system does not see it.

THE RULE
    Each class or struct member whose declared type names Permission<...>,
    through a template argument too (std::optional<Permission<T>>), or
    through a type alias of the same file, carries `[[no_unique_address]]`.
    The mark is the standard attribute with no namespace.  GCC ignores
    `[[msvc::no_unique_address]]`, so that spelling marks nothing.  A static
    member never qualifies.  A reference or a pointer member holds no token
    of its own and is out of scope.  The one exemption is a
    `// PERMISSION-STORAGE-OK: <reason>` comment on a line of the member
    declaration.  A marker that exempts nothing fails, so a dead marker
    cannot come to hide a later member.

WHAT READS THE MEMBERS
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py), over
    each file of include/ and src/ that tsast.is_in_cpp_scope admits: a C++
    suffix, and not a file of tsast.UNPARSEABLE.  A member inside an arm of
    an `#if` in a class body is a member of that class.  The substrate
    headers that define Permission are exempt.  Any other file that the
    parser cannot read fails.

Usage
    check-permission-storage.py              scan the tree
    check-permission-storage.py --self-test  plant each shape and each marker

Exit 0 clean, 1 on an unmarked member or a file the parser cannot read, 2 on
a dead marker, a bad invocation or a failed self-test, 3 when the kit is not
installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

ROOTS = ("include", "src")
SUBSTRATE = frozenset({"include/foundation/permissions/Permission.h"})
MARKER = "PERMISSION-STORAGE-OK"
MARKER_WITH_REASON = re.compile(rf"{MARKER}:\s*\S")
TEMPLATE = "Permission"
PREPROC_ARMS = frozenset({"preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef"})


@dataclass(frozen=True)
class Finding:
    """One result of the guard."""

    kind: str
    path: str
    line: int
    text: str


def names_permission(node: tsast.Node, aliases: set[str]) -> bool:
    """Report whether a type node names Permission<...> anywhere, or an alias of it."""
    for found in [node, *node.descendants("template_type", "type_identifier")]:
        if found.type == "template_type":
            named = found.child_by_field("name")
            if named is not None and named.text == TEMPLATE:
                return True
        elif found.type == "type_identifier" and found.text in aliases:
            return True
    return False


def permission_aliases(tree: tsast.Tree) -> set[str]:
    """Return the names of the file whose alias or typedef names Permission<...>, to a fixed point."""
    declarations = []
    for node in tree.find("alias_declaration", "type_definition"):
        body = node.child_by_field("type")
        if body is None:
            continue
        if node.type == "alias_declaration":
            named = node.child_by_field("name")
            names = [named.text] if named is not None else []
        else:
            names = [leaf.text for child in node.children if child.field == "declarator"
                     for leaf in ([child] if child.type == "type_identifier" else child.descendants("type_identifier"))]
        declarations.append((names, body))
    aliases: set[str] = set()
    grew = True
    while grew:
        grew = False
        for names, body in declarations:
            if any(name not in aliases for name in names) and names_permission(body, aliases):
                aliases.update(names)
                grew = True
    return aliases


def in_class_body(member: tsast.Node) -> bool:
    """Report whether a field declaration is a member of a class body, through each arm of an `#if` there."""
    owner = member.parent
    while owner is not None and owner.type in PREPROC_ARMS:
        owner = owner.parent
    return owner is not None and owner.type == "field_declaration_list"


def standard_attributes(member: tsast.Node) -> set[str]:
    """Return the names of the `[[...]]` attributes of a declaration that carry no namespace.

    `[[msvc::no_unique_address]]` names the attribute of another compiler, which
    GCC ignores, so a name with a namespace is left out.
    """
    return {named.text for declaration in member.children_of_type("attribute_declaration")
            for attribute in declaration.children_of_type("attribute")
            if attribute.child_by_field("prefix") is None and (named := attribute.child_by_field("name")) is not None}


def check_tree(tree: tsast.Tree, rel: str) -> list[Finding]:
    """Return the unmarked members and the dead markers of one file."""
    findings: list[Finding] = []
    aliases = permission_aliases(tree)
    markers = {}
    for comment in tree.find("comment"):
        if MARKER in tsast.prose_text(comment):
            markers[comment.start[0]] = comment
    used: set[int] = set()
    for member in tree.find("field_declaration"):
        if not in_class_body(member):
            continue
        kind = member.child_by_field("type")
        # A nested class definition is a member declaration whose type is the
        # class, and its own members are judged on their own.
        if kind is None or kind.child_by_field("body") is not None or not names_permission(kind, aliases):
            continue
        # A reference or a pointer member holds no token of its own.
        declarators = [child for child in member.children if child.field == "declarator"]
        if declarators and all(child.type in ("reference_declarator", "pointer_declarator") for child in declarators):
            continue
        marked = "no_unique_address" in standard_attributes(member)
        is_static = any(child.type == "storage_class_specifier" and child.text == "static"
                        for child in member.children)
        rows = range(member.start[0], member.end[0] + 1)
        suppressions = [markers[row] for row in rows if row in markers]
        if suppressions:
            used.update(comment.start[0] for comment in suppressions)
            if not all(MARKER_WITH_REASON.search(tsast.prose_text(comment)) for comment in suppressions):
                findings.append(Finding("marker", rel, member.line,
                                        f"PERMISSION-STORAGE marker without a reason: {rel}:{member.line} — write "
                                        f"`// {MARKER}: <reason>`."))
            continue
        if marked and not is_static:
            continue
        what = "a static Permission member" if is_static else "a Permission member without [[no_unique_address]]"
        findings.append(Finding("violation", rel, member.line,
                                f"PERMISSION-STORAGE violation: {rel}:{member.line} — {what}: "
                                f"{tsast.excerpt(member)}"))
    for row, comment in sorted(markers.items()):
        if row not in used:
            findings.append(Finding("dead-marker", rel, comment.line,
                                    f"PERMISSION-STORAGE dead marker: {rel}:{comment.line} — the marker sits on no "
                                    f"Permission member that needs it.  Delete it."))
    return findings


def scan(root: Path) -> list[Finding]:
    """Return every finding under include/ and src/ of the root.

    Complexity: linear in the size of the files in scope.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            files += [path for path in sorted(base.rglob("*")) if path.is_file()
                      and tsast.is_in_cpp_scope(path.relative_to(root))
                      and path.relative_to(root).as_posix() not in SUBSTRATE]
    findings: list[Finding] = []
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            findings.append(Finding("parse", rel, 0, f"PERMISSION-STORAGE parse failure: {rel} — the parser "
                                                     f"cannot read it, so its members are unknown.\n"
                                                     f"  {tree.diagnostic}"))
            continue
        findings += check_tree(tree, rel)
    return findings


def run(root: Path) -> int:
    """Scan, print each finding, and return the exit code."""
    findings = scan(root)
    for finding in findings:
        print(finding.text, file=sys.stderr)
    failing = [finding for finding in findings if finding.kind in ("violation", "parse")]
    if failing:
        print("\ncheck-permission-storage: a Permission member is the [[no_unique_address]] member of a handle "
              "(Pinned, move-only, nested in its channel or session).  Wrap the storage in such a handle, or write "
              f"`// {MARKER}: <reason>` on the declaration when the design is deliberate.", file=sys.stderr)
        return 1
    if findings:
        return 2
    print("check-permission-storage: clean — every Permission member is a [[no_unique_address]] handle member.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each member shape and each marker, and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = (
        "#pragma once\n"                                                                        # 1
        "namespace planted {\n"                                                                 # 2
        "struct tag {};\n"                                                                      # 3
        "using Held = ::foundation::permissions::Permission<tag>;\n"                            # 4
        "struct Bare { ::foundation::permissions::Permission<tag> bare_; };\n"                  # 5
        "struct Initialized { Permission<tag> initialized_ = mint<tag>(); };\n"                 # 6
        "struct TwoDeclarators { Permission<tag> first_, second_; };\n"                         # 7
        "struct OverTwoLines {\n"                                                               # 8
        "    Permission<tag>\n"                                                                 # 9
        "        wrapped_;\n"                                                                   # 10
        "};\n"                                                                                  # 11
        "struct ArrayMember { Permission<tag> many_[2]; };\n"                                   # 12
        "struct ThroughAlias { Held aliased_; };\n"                                             # 13
        "struct Wrapped { std::optional<Permission<tag>> optional_; };\n"                       # 14
        "struct Static { [[no_unique_address]] static inline Permission<tag> shared_{}; };\n"   # 15
        "struct Handle { [[no_unique_address]] Permission<tag> perm_; };\n"                     # 16
        "struct WrappedAttribute {\n"                                                           # 17
        "    [[no_unique_address]]\n"                                                           # 18
        "    Permission<tag> wrapped_attr_;\n"                                                  # 19
        "};\n"                                                                                  # 20
        "struct Marked { Permission<tag> marked_;  // PERMISSION-STORAGE-OK: a planted witness\n"  # 21
        "};\n"                                                                                  # 22
        "struct NoReason { Permission<tag> no_reason_;  // PERMISSION-STORAGE-OK\n"            # 23
        "};\n"                                                                                  # 24
        "// PERMISSION-STORAGE-OK: a marker on no member\n"                                     # 25
        "struct Shared { SharedPermission<tag> not_the_template_; };\n"                         # 26
        "void function(Permission<tag> parameter) { Permission<tag> local = parameter; }\n"      # 27
        "// struct Commented { Permission<tag> in_comment_; };\n"                               # 28
        "struct Outer { struct Nested { Permission<tag> nested_bare_; } nested_; };\n"          # 29
        "struct Borrowing { Permission<tag>& borrowed_; Permission<tag>* pointed_; };\n"        # 30
        "struct MsvcMark { [[msvc::no_unique_address]] Permission<tag> msvc_; };\n"             # 31
        "struct InsideIf {\n"                                                                   # 32
        "#if PLANTED_ARM\n"                                                                     # 33
        "    Permission<tag> in_arm_;\n"                                                        # 34
        "#endif\n"                                                                              # 35
        "};\n"                                                                                  # 36
        "struct BothMarks { [[gnu::aligned(8), no_unique_address]] Permission<tag> both_; };\n"  # 37
        "}\n"                                                                                   # 38
    )

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-permission-storage --self-test")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        header = root / "include" / "planted" / "Members.h"
        header.parent.mkdir(parents=True)
        header.write_text(planted, encoding="utf-8")
        findings = scan(root)
        by_line = {(finding.kind, finding.line) for finding in findings}
        for line, label in ((5, "a bare member"), (6, "a member with an initializer"),
                            (7, "a member with two declarators"), (9, "a member over two lines"),
                            (12, "an array member"), (13, "a member through an alias"),
                            (14, "a Permission inside a template argument"),
                            (15, "a static member, marked or not"),
                            (29, "a bare member of a nested class"),
                            (31, "a member marked with another compiler's attribute"),
                            (34, "a member inside an #if arm of a class body")):
            expect(f"flagged: {label}", ("violation", line) in by_line, True)
        expect("a nested class definition is judged once, by its own members",
               sum(finding.line == 29 for finding in findings) == 1)
        for line, label in ((16, "a [[no_unique_address]] member"), (18, "an attribute on the line above"),
                            (21, "a member with a marker and its reason"), (26, "a SharedPermission member"),
                            (27, "a parameter and a local"), (28, "a member in a comment"),
                            (30, "a reference member and a pointer member"),
                            (37, "the standard mark beside an attribute with a namespace")):
            expect(f"not flagged: {label}", not any(line == finding.line and finding.kind == "violation"
                                                    for finding in findings))
        expect("a marker with no reason fails", ("marker", 23) in by_line, True)
        expect("a marker on no member is dead", ("dead-marker", 25) in by_line, True)
        code, report = captured(lambda: run(root))
        expect("an unmarked member exits 1", code == 1 and "Members.h:5" in report, True)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: run(root))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == (code, report))
        header.write_text("#pragma once\nstruct Handle { [[no_unique_address]] Permission<tag> perm_; };\n"
                          "// PERMISSION-STORAGE-OK: dead\n", encoding="utf-8")
        expect("a dead marker alone exits 2", captured(lambda: run(root))[0] == 2, True)
        header.write_text("#pragma once\nstruct Handle { [[no_unique_address]] Permission<tag> perm_; };\n",
                          encoding="utf-8")
        expect("a clean tree exits 0", captured(lambda: run(root))[0] == 0)
        rostered = root / next(iter(tsast.UNPARSEABLE))
        rostered.parent.mkdir(parents=True, exist_ok=True)
        rostered.write_text("struct Out { Permission<tag> out_; void f() { g(1) { } } };\n", encoding="utf-8")
        expect("a file of the UNPARSEABLE roster is out of scope", captured(lambda: run(root))[0] == 0, True)
        (root / "src").mkdir()
        (root / "src" / "broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("a file the parser cannot read fails", code == 1 and "parse failure: src/broken.cpp" in report, True)

    if failures:
        print(f"check-permission-storage --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-permission-storage --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == []:
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-permission-storage: {exc}", file=sys.stderr)
        return 3
    print("usage: check-permission-storage.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
