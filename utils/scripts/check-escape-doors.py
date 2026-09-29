#!/usr/bin/env python3
"""check-escape-doors — every public function that hands out a raw reference or pointer is a sanctioned door.

fixy is safe by default.  A raw reference or pointer leaves a wrapper only by
an explicit door that is still type-checked and discouraged, never silently
beside the safe call.  This guard is the tree-wide net over the free functions
and the function templates.  The primary guard is
foundation/reflect/RawEscape.h with test/fixy/test_escape_reflection.cpp,
which walks the members of each wrapper through std::meta and reads the return
type that the compiler resolved.  Reflection cannot walk a free function or a
function template, so the two divide the surface.

WHAT A RAW ESCAPE IS
    A public function whose return type, const aside, is a reference (`T&`,
    `T&&`) or a raw pointer (`T*`, `void*`), in the leading or the trailing
    return type.  A private or protected member is no door.  A function that
    returns a value (a span, a wrapper, an expected, an int) hands out no
    reference.  A `decltype(auto)` return can be a reference, so it counts
    as an escape too.  A hidden friend with a body is a free function that
    argument-dependent lookup finds, so it counts.  A deleted function, a
    friend declaration with no body, an out-of-class definition of a member
    (its declaration in the class counts), and a name that ends in an
    underscore (the convention of an internal helper) are no door.

THE FOUR SANCTIONED SHAPES
    ACCESSOR  a getter whose name is in the vocabulary below, or an operator
              other than a conversion.  Its provenance is the object it is
              called on.  Admitted.
    MINT      a `mint_*` factory.  Admitted.
    HATCH     from_raw, from_raw_nonnull, into_raw, release, declassify: the
              explicit, discouraged doors of the C boundary and of the
              ownership hand-off.  Admitted, counted, and the count is pinned
              in the ledger, so a new hatch is a reviewed act.
    LEDGERED  a bare door named in utils/scripts/escape-doors.txt with a reason.
              The list only shrinks.
    Anything else, a conversion operator included, is a NEW BARE DOOR.

WHAT READS THE DECLARATIONS
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py), over the
    headers of include/foundation and include/fixy.  The line scan it
    replaces missed a trailing return type, a return type over more than four
    lines and a door behind a block comment, and it counted private members.
    The guard classifies by NAME and does not compute where the returned
    reference points: a getter named `data` that returned a dangling
    reference would pass.  The name is the claim, and review reads it.

Usage
    check-escape-doors.py              compare the tree with the ledger
    check-escape-doors.py --list       print every raw escape and its class
    check-escape-doors.py --refresh    write the hatch pin and keep each reason
    check-escape-doors.py --self-test  plant each shape and each evasion

Exit 0 clean, 1 on a new bare door, a stale ledger line, a hatch-count drift
or a header the parser cannot read, 2 on a malformed ledger, a bad invocation
or a failed self-test, 3 when the kit is not installed.
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

REPO = tsast.REPO_ROOT
ROOTS = (REPO / "include" / "foundation", REPO / "include" / "fixy")
LEDGER = REPO / "utils" / "scripts" / "escape-doors.txt"

# The conventional getter names.  A member whose provenance is the object it
# is called on carries one of these.  A new getter with a new name is added
# here in a reviewed edit rather than reaching for a bare door.
ACCESSORS = frozenset({
    "data", "data_mut", "begin", "end", "cbegin", "cend", "front", "back", "at", "get",
    "get_or_init", "get_assuming_set", "peek", "peek_mut", "value", "resource", "ctx",
    "in", "out", "input", "output", "stage", "machine", "state", "state_mut", "current",
    "carrier", "handle", "pin", "c_str", "sq_ring", "cq_ring", "sqes", "observe", "consume",
    "try_get", "raw_ptr", "graded", "cap", "span", "cspan",
})
HATCHES = frozenset({"from_raw", "from_raw_nonnull", "into_raw", "release", "declassify"})
FUNCTION_NODES = ("function_definition", "declaration", "field_declaration")
CLASS_NODES = frozenset({"class_specifier", "struct_specifier", "union_specifier"})
LEDGER_HEADER = (
    "# utils/scripts/escape-doors.txt — the escape surface the guard locks.\n"
    "# Read by utils/scripts/check-escape-doors.py.\n"
    "#\n"
    "# hatch-total pins the count of discouraged, type-checked escape\n"
    "# hatches (from_raw, from_raw_nonnull, into_raw, release,\n"
    "# declassify).  A new hatch raises the count and fails CI until the\n"
    "# pin is raised in the same commit, so a hatch is never added\n"
    "# quietly.  Removing a hatch lowers it and is refreshed here too.\n"
    "#\n"
    "# Each `path:name  reason` line admits one bare door: a raw escape\n"
    "# that is neither an accessor nor a mint nor a hatch, but is safe\n"
    "# for the stated reason.  The list only shrinks: a bare door is\n"
    "# reclassified, never added.  --refresh rewrites the pin and keeps\n"
    "# each reason.\n"
)


class LedgerError(ValueError):
    """The ledger does not parse."""


@dataclass(frozen=True)
class Escape:
    """One raw escape."""

    path: str
    line: int
    name: str
    klass: str


def function_declarator(node: tsast.Node) -> tuple[tsast.Node | None, bool]:
    """Return the function declarator of a declaration, and whether a reference or pointer wraps it."""
    declarator = node.child_by_field("declarator")
    wrapped = False
    while declarator is not None and declarator.type in ("pointer_declarator", "reference_declarator"):
        wrapped = True
        declarator = declarator.child_by_field("declarator")
    if declarator is not None and declarator.type == "function_declarator":
        return declarator, wrapped
    return None, False


def is_decltype_auto(kind: tsast.Node | None) -> bool:
    """Report whether a type node is `decltype(auto)`: a placeholder whose decltype holds the auto keyword.

    A comment inside the parentheses is a node of its own, so it changes
    nothing."""
    if kind is None or kind.type != "placeholder_type_specifier":
        return False
    return any(inner.type == "decltype" and inner.children_of_type("auto") for inner in kind.children)


def returns_raw(node: tsast.Node, declarator: tsast.Node, wrapped: bool) -> bool:
    """Report whether a function returns a reference or a raw pointer, or may through decltype(auto)."""
    if wrapped:
        return True
    for trailing in declarator.children_of_type("trailing_return_type"):
        for descriptor in trailing.children_of_type("type_descriptor"):
            outer = descriptor.child_by_field("declarator")
            if outer is not None and outer.type in ("abstract_reference_declarator", "abstract_pointer_declarator"):
                return True
            if is_decltype_auto(descriptor.child_by_field("type")):
                return True
    return is_decltype_auto(node.child_by_field("type"))


def access_of(node: tsast.Node) -> str:
    """Return the access of a member declaration, or `public` for a namespace-scope function."""
    member = node
    while member.parent is not None and member.parent.type == "template_declaration":
        member = member.parent
    body = member.parent
    if body is None or body.type != "field_declaration_list":
        return "public"
    owner = body.parent
    access = "private" if owner is not None and owner.type == "class_specifier" else "public"
    for child in body.children:
        if child.index == member.index:
            break
        if child.type == "access_specifier":
            access = tsast.lexeme(child)
    return access


def classify(name_node: tsast.Node) -> tuple[str, str] | None:
    """Return the spelled name and the class of a door, or None when the name is no public door."""
    if name_node.type == "operator_name":
        return tsast.spelled(name_node), "accessor"
    if name_node.type in ("destructor_name", "qualified_identifier"):
        return None
    # A template-id names its template in its name field; the arguments
    # after it are no part of the name.
    if name_node.type in ("template_function", "template_method"):
        name_node = name_node.child_by_field("name")
        if name_node is None:
            return None
    name = tsast.leaf_name(name_node)
    if not name or name.endswith("_"):
        return None
    if name.startswith("mint_"):
        return name, "mint"
    if name in HATCHES:
        return name, "hatch"
    if name in ACCESSORS:
        return name, "accessor"
    return name, "bare"


def escapes_of(tree: tsast.Tree, shown: str) -> list[Escape]:
    """Return every raw escape of one parsed header.

    Complexity: linear in the node count of the header.
    """
    found: list[Escape] = []
    for node in tree.find(*FUNCTION_NODES):
        cast = node.child_by_field("declarator")
        if cast is not None and cast.type == "operator_cast":
            # A conversion to a reference or a pointer is an implicit door.
            target = cast.child_by_field("declarator")
            if target is not None and target.type in ("abstract_reference_declarator", "abstract_pointer_declarator") \
                    and not node.children_of_type("delete_method_clause") and access_of(node) == "public":
                kind = cast.child_by_field("type")
                sigil = "&" if target.type == "abstract_reference_declarator" else "*"
                spelled = f"operator {tsast.spelled(kind) if kind is not None else ''}{sigil}"
                found.append(Escape(shown, cast.line, spelled, "bare"))
            continue
        declarator, wrapped = function_declarator(node)
        if declarator is None or node.children_of_type("delete_method_clause"):
            continue
        in_friend = node.ancestor_of_type("friend_declaration") is not None
        if in_friend and node.type != "function_definition":
            continue
        if not in_friend and access_of(node) != "public":
            continue
        name_node = declarator.child_by_field("declarator")
        if name_node is None or not returns_raw(node, declarator, wrapped):
            continue
        classified = classify(name_node)
        if classified is not None:
            found.append(Escape(shown, name_node.line, *classified))
    return found


def collect(roots: tuple[Path, ...] | list[Path]) -> tuple[list[Escape], list[str]]:
    """Return every raw escape under the roots, and each header the parser cannot read.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = sorted(path for root in roots if root.is_dir() for path in root.rglob("*.h"))
    escapes: list[Escape] = []
    unread: list[str] = []
    for tree in tsast.parse(files, strict=False):
        path = Path(tree.path)
        shown = path.relative_to(REPO).as_posix() if path.is_relative_to(REPO) else path.as_posix()
        if tree.diagnostic is not None:
            if shown not in tsast.UNPARSEABLE:
                unread.append(f"UNREADABLE     {shown} does not parse, so its doors are unknown.\n  {tree.diagnostic}")
            continue
        escapes += escapes_of(tree, shown)
    return escapes, unread


def read_ledger(ledger: Path) -> tuple[int | None, dict[str, str]]:
    """Return the hatch pin and each ledgered bare door with its reason.

    Raises:
        LedgerError: For a line with no reason, a second pin or a duplicate door
    """
    pin: int | None = None
    doors: dict[str, str] = {}
    if not ledger.is_file():
        return pin, doors
    for number, raw in enumerate(ledger.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        if entry.startswith("hatch-total"):
            parts = entry.split()
            if pin is not None or len(parts) != 2 or not parts[1].isdigit():
                raise LedgerError(f"{ledger.name}:{number}: one `hatch-total <N>` line is allowed")
            pin = int(parts[1])
            continue
        key, _, reason = entry.partition("  ")
        if not reason.strip() or ":" not in key:
            raise LedgerError(f"{ledger.name}:{number}: a door line is `path:name  reason`, with two spaces")
        if key in doors:
            raise LedgerError(f"{ledger.name}:{number}: {key} has a line before this one")
        doors[key.strip()] = reason.strip()
    return pin, doors


def check(roots: tuple[Path, ...] | list[Path], ledger: Path) -> int:
    """Compare the escapes with the ledger, and print each failure."""
    escapes, unread = collect(roots)
    pin, doors = read_ledger(ledger)
    failures = list(unread)
    hatches = [escape for escape in escapes if escape.klass == "hatch"]
    bare = [escape for escape in escapes if escape.klass == "bare"]
    for escape in bare:
        if f"{escape.path}:{escape.name}" not in doors:
            failures.append(f"NEW BARE DOOR  {escape.path}:{escape.line}  {escape.name} — returns a raw reference or "
                            f"pointer that is not an accessor, a mint or a marked hatch.  Make it one of those, or "
                            f"add '{escape.path}:{escape.name}  <reason>' to {ledger.name} if it is a safe exception.")
    live = {f"{escape.path}:{escape.name}" for escape in bare}
    for key in sorted(set(doors) - live):
        failures.append(f"STALE LEDGER   {key} — no longer a bare door.  Delete the line.")
    if pin is None:
        failures.append(f"NO HATCH PIN   {ledger.name} has no `hatch-total` line.  Run --refresh.")
    elif pin != len(hatches):
        verb = "rose" if len(hatches) > pin else "fell"
        failures.append(f"HATCH DRIFT    the count of discouraged hatches {verb} from {pin} to {len(hatches)}.  "
                        f"Run --refresh in the same commit.")
        failures += [f"    hatch  {escape.path}:{escape.line}  {escape.name}" for escape in hatches]
    for line in failures:
        print(line)
    counts = {klass: sum(escape.klass == klass for escape in escapes) for klass in ("accessor", "mint", "hatch", "bare")}
    print(f"check-escape-doors: {len(escapes)} raw escape(s) — {counts['accessor']} accessor, {counts['mint']} mint, "
          f"{counts['hatch']} hatch (pinned {pin}), {counts['bare']} bare.", file=sys.stderr)
    return 1 if failures else 0


def refresh(roots: tuple[Path, ...] | list[Path], ledger: Path) -> int:
    """Write the hatch pin again and keep the reason of each ledgered door that is still bare.

    A new bare door gets no line: its author writes the reason by hand, so
    the refresh never admits a door on its own.
    """
    escapes, unread = collect(roots)
    if unread:
        print("\n".join(unread))
        return 1
    _pin, doors = read_ledger(ledger)
    live = {f"{escape.path}:{escape.name}" for escape in escapes if escape.klass == "bare"}
    hatches = sum(escape.klass == "hatch" for escape in escapes)
    lines = [LEDGER_HEADER, f"hatch-total {hatches}\n"]
    lines += [f"{key}  {doors[key]}\n" for key in sorted(doors) if key in live]
    ledger.write_text("".join(lines), encoding="utf-8")
    print(f"check-escape-doors: ledger written — hatch-total {hatches}, {sum(key in live for key in doors)} "
          f"ledgered door(s).", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each door shape and each evasion of the old line scan, and examine each verdict.

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
        """Run an action and return its code and its stdout."""
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(io.StringIO()):
            code = action()
        return code, buffer.getvalue()

    print("check-escape-doors --self-test")
    planted = (
        "#pragma once\n"
        "namespace planted {\n"
        "struct Thing {\n"
        "    int* steal_the_pointer(int* p) noexcept { return p; }\n"
        "    auto trailing_steal() noexcept -> int& { return value_; }\n"
        "    int&\n"
        "    /* a block comment between the return type and the name */\n"
        "    wrapped_steal() noexcept { return value_; }\n"
        "    decltype(auto) deduced_steal() noexcept { return (value_); }\n"
        "    auto commented_deduced() noexcept -> decltype(/* deduced */ auto) { return (value_); }\n"
        "    decltype(value_) by_decltype() noexcept { return value_; }\n"
        "    operator int&() noexcept { return value_; }\n"
        "    int* data() noexcept { return &value_; }\n"
        "    int& operator*() noexcept { return value_; }\n"
        "    Thing& operator=(Thing const&) noexcept = default;\n"
        "    int* release() noexcept { return nullptr; }\n"
        "    int* internal_() noexcept { return nullptr; }\n"
        "    int* deleted_door() noexcept = delete;\n"
        "    int by_value() noexcept { return value_; }\n"
        "    friend int* hidden_friend(Thing& thing) noexcept { return &thing.value_; }\n"
        "private:\n"
        "    int* private_steal() noexcept { return &value_; }\n"
        "    int value_ = 0;\n"
        "};\n"
        "inline int* free_steal(int* p) noexcept { return p; }\n"
        "template <class T> T& template_steal(T& value) noexcept { return value; }\n"
        "inline int* Thing::out_of_class() noexcept { return nullptr; }\n"
        "// int* in_a_comment(int* p) noexcept;\n"
        "}  // namespace planted\n"
    )
    bare_expected = {"steal_the_pointer", "trailing_steal", "wrapped_steal", "deduced_steal", "commented_deduced",
                     "operator int&", "hidden_friend", "free_steal", "template_steal"}
    with tempfile.TemporaryDirectory() as work:
        root = Path(work) / "inc"
        (root / "planted").mkdir(parents=True)
        (root / "planted" / "Leak.h").write_text(planted, encoding="utf-8")
        escapes, unread = collect([root])
        by_name = {escape.name: escape.klass for escape in escapes}
        expect("the planted header parses", not unread)
        for name in sorted(bare_expected):
            expect(f"a bare door: {name}", by_name.get(name) == "bare", True)
        for name, klass in (("data", "accessor"), ("operator*", "accessor"), ("operator=", "accessor"),
                            ("release", "hatch")):
            expect(f"{name} is {klass}", by_name.get(name) == klass)
        for name in ("internal_", "deleted_door", "by_value", "by_decltype", "private_steal", "out_of_class",
                     "in_a_comment"):
            expect(f"no door: {name}", name not in by_name, True)

        ledger = Path(work) / "ledger.txt"
        shown = (root / "planted" / "Leak.h").as_posix()
        lines = "".join(f"{shown}:{name}  planted: safe for the self-test\n" for name in sorted(bare_expected))
        ledger.write_text(f"hatch-total 1\n{lines}", encoding="utf-8")
        expect("a ledgered tree passes", captured(lambda: check([root], ledger))[0] == 0)
        ledger.write_text(f"hatch-total 1\n{lines}{shown}:gone  a door that left\n", encoding="utf-8")
        code, report = captured(lambda: check([root], ledger))
        expect("a stale ledger line fails", code == 1 and "STALE LEDGER" in report, True)
        ledger.write_text("hatch-total 1\n" + "".join(line + "\n" for line in lines.splitlines()[1:]),
                          encoding="utf-8")
        code, report = captured(lambda: check([root], ledger))
        expect("a bare door with no line fails", code == 1 and "NEW BARE DOOR" in report, True)
        ledger.write_text(f"hatch-total 0\n{lines}", encoding="utf-8")
        code, report = captured(lambda: check([root], ledger))
        expect("a hatch over the pin fails", code == 1 and "HATCH DRIFT" in report, True)
        ledger.write_text(f"hatch-total 1\n{shown}:free_steal\n", encoding="utf-8")
        try:
            read_ledger(ledger)
            refused = False
        except LedgerError:
            refused = True
        expect("a door line with no reason is malformed", refused, True)
        ledger.write_text(f"hatch-total 0\n{lines}", encoding="utf-8")
        captured(lambda: refresh([root], ledger))
        expect("--refresh fixes the pin and keeps every reason",
               ledger.read_text().count("planted: safe") == len(bare_expected)
               and captured(lambda: check([root], ledger))[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: check([root], ledger))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository",
               from_slash == captured(lambda: check([root], ledger)))
        (root / "planted" / "Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = captured(lambda: check([root], ledger))
        expect("a header the parser cannot read fails", code == 1 and "UNREADABLE" in report, True)

    if failures:
        print(f"check-escape-doors --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-escape-doors --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == []:
            return check(ROOTS, LEDGER)
        if argv == ["--refresh"]:
            return refresh(ROOTS, LEDGER)
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--list"]:
            escapes, unread = collect(ROOTS)
            for escape in escapes:
                print(f"{escape.klass.upper():9} {escape.path}:{escape.line}  {escape.name}")
            print("\n".join(unread))
            return 1 if unread else 0
    except tsast.KitMissing as exc:
        print(f"check-escape-doors: {exc}", file=sys.stderr)
        return 3
    except LedgerError as exc:
        print(f"check-escape-doors: {exc}", file=sys.stderr)
        return 2
    print("usage: check-escape-doors.py [--list | --refresh | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
