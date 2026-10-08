"""check-witness-roster — every witness type has one door, and a fixture stands on it.

Seven holes were one defect: a type that attests to a fact it cannot see
(a pin, a mapping, a descriptor, a permission, a context) and whose
constructor took raw data and trusted it.  Every fix was the same rule:
the constructor either consumes the evidence or IS the operation that
produces it.  This guard makes the rule mechanical.

THE ROWS
    The witness rows of utils/rule-table.txt (assay.core.ruletable) name each
    witness class, and `loop derive witness-fixtures` writes the negative
    fixture of each row (assay.loop.derive.witness_fixtures).  The key of a
    row is the full name of its class with no root namespace fixy or
    foundation.

THE WALK
    Every class and struct in include/fixy and include/foundation that
    shows a door shape is a witness, and --check fails when its qualified
    name is absent from the roster.  The walk reads the parse tree of the
    pinned tree-sitter kit (utils/assay/readers/tsast.py), so a class head over two
    lines, a brace in a block comment or a raw string, and a declaration
    over several lines each read as the compiler reads them.  The shapes:

      - it befriends a `mint_` function
      - a constructor takes a passkey first: a parameter type whose name
        ends in `_key` or `Key`
      - it IS a passkey: its own name ends in `_key` or `Key`
      - a static member function returns `std::expected<Self, ...>`, in
        the leading or the trailing return type.  That is where the
        system call went when the constructor was closed
        (OwnedMmap::mint_region).
      - it befriends a named function that is not an operator or swap
      - it befriends a type other than itself.  A template that befriends
        its own other specializations does not count.

    The last two count only when the class also keeps a constructor out
    of public reach, which is not a copy, a move or a deleted one.  A
    friend beside a public constructor is a shortcut, not a door.

    Friends are not members, so no reflection query can enumerate them,
    and the walk reads the source.  A class that shows a shape and is not
    a witness still needs a roster line, and a line needs a fixture that is
    refused, so the reviewer sees the claim.

Usage
    guard check --rule witness-roster --check      the walk, the CI gate
    guard check --rule witness-roster --walk       print each class that the walk counts
    guard check --rule witness-roster --self-test  plant each shape and each evasion

Exit 0 when each class with a door shape has a row, 1 on a class with no row
or a header that the parser cannot read, 2 on a rule table that does not obey
its format, a usage error or a self-test that fails, and 3 when the kit is
not installed.
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

from assay.core import ruletable
from assay.readers import tsast

REPO = tsast.REPO_ROOT
TABLE = ruletable.TABLE
INCLUDE = REPO / "include"
WALK_ROOTS = ("fixy", "foundation")
ROOT_PREFIX = re.compile(r"^(?:fixy|foundation)::")
PASSKEY_NAME = re.compile(r"(?:_key|Key)$")
PASSKEY_TYPE = re.compile(r"(?:[a-z_]*_key|[A-Za-z_]*Key)")
PREPROC_ARMS = frozenset({"preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef"})
CLASS_NODES = ("class_specifier", "struct_specifier")
FUNCTION_NODES = frozenset({"function_definition", "declaration", "field_declaration"})


def rostered_keys(table: Path) -> set[str]:
    """Return the key of each witness row of a rule table (THE ROWS).

    Raises:
        ruletable.TableError: When the table does not obey its format
    """
    return {ROOT_PREFIX.sub("", witness.name) for witness in ruletable.load(table).witnesses}


# ── The walk ───────────────────────────────────────────────────────────


NAME_LEAVES = frozenset(
    {"identifier", "type_identifier", "field_identifier", "namespace_identifier", "operator_name", "destructor_name"}
)
NAME_WRAPPERS = frozenset({"qualified_identifier", "template_type", "template_function", "template_method"})


def last_name(node: tsast.Node | None) -> str:
    """Return the last name segment of a type or declarator node, with no template arguments.

    The walk follows the `name` field through each qualified name and each
    template-id, so a comment or a line break inside the spelling cannot
    change the result.  A shape with no name, such as a primitive type or
    decltype, gives the empty string.
    """
    while node is not None and node.type in NAME_WRAPPERS:
        node = node.child_by_field("name")
    if node is None or node.type not in NAME_LEAVES:
        return ""
    return tsast.leaf_name(node) or ""


def returns_expected_of(member: tsast.Node, declarator: tsast.Node, name: str) -> bool:
    """Report whether a member function returns `expected<name, ...>`, in the leading or the trailing type.

    The first template argument must name the class itself.  An argument
    such as `Self::Inner` names a nested class, so it does not count.
    """
    roots = [
        node
        for node in (member.child_by_field("type"), *declarator.children_of_type("trailing_return_type"))
        if node is not None
    ]
    for root in roots:
        for found in [root, *root.descendants("template_type")]:
            if found.type != "template_type" or last_name(found) != "expected":
                continue
            arguments = found.child_by_field("arguments")
            listed = [child for child in arguments.children if child.type != "comment"] if arguments else []
            if (
                listed
                and listed[0].type == "type_descriptor"
                and listed[0].child_by_field("declarator") is None
                and last_name(listed[0].child_by_field("type")) == name
            ):
                return True
    return False


def members(body: tsast.Node) -> list[tsast.Node]:
    """Return the members of a class body in source order, through each #if arm and each template head."""
    found: list[tsast.Node] = []
    stack = list(reversed(body.children))
    while stack:
        node = stack.pop()
        if node.type in PREPROC_ARMS:
            stack.extend(reversed(node.children))
        elif node.type == "template_declaration":
            stack.extend(reversed([child for child in node.children if child.field != "parameters"]))
        else:
            found.append(node)
    return found


def function_declarator(node: tsast.Node) -> tsast.Node | None:
    """Return the function declarator of a member declaration, through pointer and reference declarators."""
    declarator = node.child_by_field("declarator")
    while declarator is not None and declarator.type in ("pointer_declarator", "reference_declarator"):
        declarator = declarator.child_by_field("declarator")
    return declarator if declarator is not None and declarator.type == "function_declarator" else None


def declared_name(declarator: tsast.Node) -> tsast.Node | None:
    """Return the name node of a function declarator."""
    return declarator.child_by_field("declarator")


def first_parameter(declarator: tsast.Node) -> tsast.Node | None:
    """Return the first parameter of a function declarator, or None."""
    parameters = declarator.child_by_field("parameters")
    if parameters is None:
        return None
    listed = [
        child
        for child in parameters.children
        if child.type in ("parameter_declaration", "optional_parameter_declaration")
    ]
    return listed[0] if listed else None


def is_reference(parameter: tsast.Node) -> bool:
    """Report whether a parameter is declared as a reference."""
    declarator = parameter.child_by_field("declarator")
    return declarator is not None and declarator.type in ("reference_declarator", "abstract_reference_declarator")


def is_static(member: tsast.Node) -> bool:
    """Report whether a member declaration carries `static`."""
    return any(child.type == "storage_class_specifier" and child.text == "static" for child in member.children)


def qualified_class(node: tsast.Node) -> str:
    """Return the qualified name of a class, from its namespaces and enclosing classes, the root dropped."""
    parts: list[str] = [last_name(node.child_by_field("name"))]
    owner = node.parent
    while owner is not None:
        if owner.type in CLASS_NODES and owner.child_by_field("body") is not None:
            parts.insert(0, last_name(owner.child_by_field("name")))
        elif owner.type == "namespace_definition":
            name = owner.child_by_field("name")
            if name is not None:
                spelled = (
                    [tsast.leaf_name(part) or "" for part in name.descendants("namespace_identifier")]
                    if name.type == "nested_namespace_specifier"
                    else [tsast.leaf_name(name) or ""]
                )
                parts[:0] = spelled
        owner = owner.parent
    return ROOT_PREFIX.sub("", "::".join(parts))


@dataclass
class Shapes:
    """The door shapes of one class."""

    strong: int = 0
    weak: int = 0
    closed_constructor: bool = False

    def mark(self, kind: str, line: int) -> None:
        """Record a shape at a line, keeping the first line of each kind."""
        if kind == "strong" and not self.strong:
            self.strong = line
        if kind == "weak" and not self.weak:
            self.weak = line


def class_shapes(node: tsast.Node, name: str) -> Shapes:
    """Read the door shapes of one class body.

    Complexity: linear in the size of the class body.
    """
    shapes = Shapes()
    if PASSKEY_NAME.search(name):
        shapes.mark("strong", node.line)
    access = "public" if node.type == "struct_specifier" else "private"
    body = node.child_by_field("body")
    assert body is not None
    for member in members(body):
        if member.type == "access_specifier":
            access = tsast.lexeme(member)
            continue
        if member.type == "friend_declaration":
            functions = list(member.descendants("function_declarator"))
            if functions:
                named = declared_name(functions[0])
                spelled = last_name(named) if named is not None else ""
                if spelled.startswith("mint_"):
                    shapes.mark("strong", member.line)
                elif (
                    named is not None
                    and named.type != "operator_name"
                    and spelled != "swap"
                    and not spelled.startswith("operator")
                ):
                    shapes.mark("weak", member.line)
                continue
            befriended = [
                child
                for child in member.children
                if child.type in ("type_identifier", "qualified_identifier", "template_type")
            ]
            if befriended and last_name(befriended[-1]) != name:
                shapes.mark("weak", member.line)
            continue
        if member.type not in FUNCTION_NODES:
            continue
        declarator = function_declarator(member)
        if declarator is None:
            continue
        named = declared_name(declarator)
        spelled = last_name(named) if named is not None else ""
        if spelled == name and member.child_by_field("type") is None:
            parameter = first_parameter(declarator)
            kind = parameter.child_by_field("type") if parameter is not None else None
            parameter_type = last_name(kind)
            if parameter_type != name and PASSKEY_TYPE.fullmatch(parameter_type):
                shapes.mark("strong", member.line)
            copies = parameter is not None and parameter_type == name and is_reference(parameter)
            deleted = bool(member.children_of_type("delete_method_clause"))
            if access != "public" and not copies and not deleted:
                shapes.closed_constructor = True
        elif is_static(member) and returns_expected_of(member, declarator, name):
            shapes.mark("strong", member.line)
    return shapes


def walk_witnesses(roots: list[Path]) -> tuple[dict[str, str], list[str]]:
    """Return each class with a door shape, as qualified name to file:line, and each header the parser cannot read.

    Complexity: linear in the node count of the headers under the roots.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """

    def shown_path(path: Path) -> Path:
        """Return a path relative to the repository when it lies inside it."""
        return path.relative_to(REPO) if path.is_relative_to(REPO) else path

    files = sorted(
        path for root in roots if root.is_dir() for path in root.rglob("*.h") if tsast.is_in_cpp_scope(shown_path(path))
    )
    found: dict[str, str] = {}
    unread: list[str] = []
    for tree in tsast.parse(files, strict=False):
        shown = shown_path(Path(tree.path))
        if tree.diagnostic is not None:
            unread.append(
                f"UNREADABLE header: {shown.as_posix()} does not parse, so its door shapes are unknown.\n"
                f"  {tree.diagnostic}"
            )
            continue
        for node in tree.find(*CLASS_NODES):
            named = node.child_by_field("name")
            if node.child_by_field("body") is None or named is None:
                continue
            name = last_name(named)
            shapes = class_shapes(node, name)
            line = shapes.strong or (shapes.weak if shapes.closed_constructor else 0)
            if line:
                found.setdefault(qualified_class(node), f"{shown}:{line}")
    return found, unread


# ── The modes ──────────────────────────────────────────────────────────


def check(rostered: set[str], roots: list[Path]) -> int:
    """Walk the roots, and print each class with a door shape and no witness row (THE WALK)."""
    failures: list[str] = []
    found, unread = walk_witnesses(roots)
    failures += unread
    for name, where in sorted(found.items()):
        if name not in rostered:
            failures.append(
                f"UNROSTERED witness: {name} ({where}) has a door shape (a mint_, named or type "
                f"friend, a passkey, or a static expected<{name.rsplit('::', 1)[-1]}> factory), and "
                f"utils/rule-table.txt has no witness row of it.  Add the row with the expression of a "
                f"forger, and write the fixtures with `loop derive witness-fixtures --write`."
            )
    if failures:
        print("\n".join(failures))
        print(f"\ncheck-witness-roster: {len(failures)} failure(s).")
        return 1
    print(f"check-witness-roster: clean — {len(found)} witness class(es) walked, {len(rostered)} witness row(s).")
    return 0


def self_test() -> int:
    """Plant each door shape and each evasion of the old line walk, and prove the verdicts.

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

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its output."""
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-witness-roster --self-test")
    rostered = rostered_keys(TABLE)
    roots = [INCLUDE / root for root in WALK_ROOTS]
    code, report = captured(lambda: check(rostered, roots))
    expect("the real witness rows pass --check", code == 0)
    if code != 0:
        print(report)

    with tempfile.TemporaryDirectory() as work:
        scratch = Path(work)
        planted = scratch / "include" / "planted"
        planted.mkdir(parents=True)
        (planted / "Shapes.h").write_text(
            "#pragma once\n"
            '#define PLANTED_ABI [[gnu::abi_tag("planted")]]\n'
            "namespace fixy::planted {\n"
            "// The head carries an attribute macro, and the walk reads the name after it.\n"
            "class PLANTED_ABI MintFriend {\n"
            "    constexpr explicit MintFriend(int) noexcept {}\n"
            "    template <typename T> friend constexpr MintFriend mint_friend(T) noexcept;\n"
            "public:\n"
            "    MintFriend() = default;\n"
            "};\n"
            "class\n"
            "HeadOverTwoLines {\n"
            "    /* a brace in a block comment: { */\n"
            "    friend HeadOverTwoLines mint_two_lines() noexcept;\n"
            "};\n"
            "class RawString {\n"
            '    static constexpr const char* text = R"(}}})";\n'
            "    friend RawString mint_raw_string() noexcept;\n"
            "};\n"
            "struct Passkey {\n"
            "    explicit Passkey(\n"
            "        detail::passkey_key) noexcept;\n"
            "};\n"
            "struct Factory {\n"
            "    static auto open_path(int) noexcept -> std::expected<Factory, int>;\n"
            "};\n"
            "struct LeadingFactory {\n"
            "    static std::expected<LeadingFactory, int> open(int) noexcept;\n"
            "};\n"
            "struct NestedResult {\n"
            "    struct Inner {};\n"
            "    static auto make() noexcept -> std::expected<NestedResult::Inner, int>;\n"
            "};\n"
            "struct plain_key {};\n"
            "class Handle {\n"
            "    Handle(int) noexcept;\n"
            "    friend class Owner;\n"
            "};\n"
            "class Opener {\n"
            "    explicit Opener(int) noexcept;\n"
            "    friend Opener open_dirfd(int) noexcept;\n"
            "};\n"
            "namespace detail { struct Outer { class Nested { friend Nested mint_nested(); }; }; }\n"
            "class ShortcutFriend {\n"
            "public:\n"
            "    explicit ShortcutFriend(int) noexcept;\n"
            "    friend class Owner;\n"
            "};\n"
            "class OwnSpecializations {\n"
            "    OwnSpecializations(int) noexcept;\n"
            "    template <class U> friend class OwnSpecializations;\n"
            "};\n"
            "class Vocabulary {\n"
            "    Vocabulary(int) noexcept;\n"
            "    friend bool operator==(Vocabulary const&, Vocabulary const&) = default;\n"
            "    friend void swap(Vocabulary&, Vocabulary&) noexcept;\n"
            "};\n"
            "class CopyOnly {\n"
            "    CopyOnly(CopyOnly const&) = default;\n"
            "    CopyOnly(int) = delete;\n"
            "    friend class Owner;\n"
            "};\n"
            "#if 0\n"
            "class DeadArm { friend DeadArm mint_dead(); };\n"
            "#endif\n"
            "// class InComment { friend InComment mint_in_comment(); };\n"
            "}  // namespace fixy::planted\n",
            encoding="utf-8",
        )
        found, unread = walk_witnesses([planted])
        expect("the planted header parses", not unread)
        for name, label in (
            ("planted::MintFriend", "a mint_ friend behind an attribute macro"),
            ("planted::HeadOverTwoLines", "a class head over two lines, with a brace in a block comment"),
            ("planted::RawString", "a class whose raw string holds braces"),
            ("planted::Passkey", "a passkey constructor over two lines"),
            ("planted::Factory", "a static factory with a trailing expected return"),
            ("planted::LeadingFactory", "a static factory with a leading expected return"),
            ("planted::plain_key", "a passkey type"),
            ("planted::Handle", "a type friend beside a private constructor"),
            ("planted::Opener", "a named friend function beside a private constructor"),
            ("planted::detail::Outer::Nested", "a nested class, qualified through its owner"),
            ("planted::DeadArm", "a class in a dead #if arm, which the kit reads"),
        ):
            expect(f"counted: {label}", name in found, True)
        for name, label in (
            ("planted::ShortcutFriend", "a type friend beside a public constructor"),
            ("planted::OwnSpecializations", "a template that befriends its own specializations"),
            ("planted::Vocabulary", "an operator and a swap friend"),
            ("planted::CopyOnly", "a type friend beside a copy and a deleted constructor"),
            ("planted::NestedResult", "a static function that returns an expected of a nested class"),
            ("planted::InComment", "a class in a comment"),
        ):
            expect(f"not counted: {label}", name not in found)

        (planted / "Broken.h").write_text("class Broken { void f() { g(1) { } } };\n", encoding="utf-8")
        code, report = captured(lambda: check(set(), [planted]))
        expect("a header the parser cannot read fails", code == 1 and "UNREADABLE header" in report, True)
        (planted / "Broken.h").unlink()

        code, report = captured(lambda: check(rostered, [*roots, planted]))
        expect(
            "a planted witness that no witness row names fails --check",
            code == 1 and "UNROSTERED witness: planted::MintFriend" in report,
            True,
        )
        planted_table = scratch / "rule-table.txt"
        planted_table.write_text(
            "witness <planted/Shapes.h> private | fixy::planted::MintFriend<int> | fixy::planted::MintFriend<int>{0}\n",
            encoding="utf-8",
        )
        expect(
            "the key of a witness row drops the root namespace and the template arguments",
            rostered_keys(planted_table) == {"planted::MintFriend"},
        )

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: check(rostered, [planted]))
        finally:
            os.chdir(previous)
        expect(
            "the report from / equals the report from the repository",
            from_slash == captured(lambda: check(rostered, [planted])),
        )

    if failures:
        print(f"check-witness-roster --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-witness-roster --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    roots = [INCLUDE / root for root in WALK_ROOTS]
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--check"]:
            return check(rostered_keys(TABLE), roots)
        if argv == ["--walk"]:
            found, unread = walk_witnesses(roots)
            for name, where in sorted(found.items()):
                print(f"{name}\t{where}")
            print("\n".join(unread), file=sys.stderr)
            return 1 if unread else 0
    except tsast.KitMissing as exc:
        print(f"check-witness-roster: {exc}", file=sys.stderr)
        return 3
    except ruletable.TableError as exc:
        print(f"check-witness-roster: {exc}", file=sys.stderr)
        return 2
    print("usage: guard check --rule witness-roster [--check | --walk | --self-test]", file=sys.stderr)
    return 2
