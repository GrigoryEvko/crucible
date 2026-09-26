#!/usr/bin/env python3
"""check-fullness-guard — a fullness test compares with `>=`, never with `==`, read from the parse tree.

A container guards its push with a fullness test.  When that test is spelled
`count == Capacity`, a count that has already passed the bound reads as not
full, and the next push writes outside the array:

    if (count == Capacity) { return false; }   // 300 == 64 is false
    entries[count] = incoming;                 // writes entries[300]

`>=` costs the same instruction and is correct for every count the type can
hold.  The release build has no _GLIBCXX_ASSERTIONS, safety::FixedArray's
operator[] has no precondition, and some TUs compile their contracts out, so
the comparison is the only check that reaches production.

WHAT COUNTS AS A FULLNESS TEST
    The guard reads the parse tree of the pinned tree-sitter kit.  A
    candidate is an `==` comparison between a counter and a bound.
      * A counter is a name, a member access chain (`state.count`,
        `hdr->n`, `this->count`) or a subscript of one, through
        parentheses and a static_cast.  A call is not a counter, because a
        container that compares its own size() asks its own invariant.
      * A bound is a name whose last segment is Capacity, capacity,
        max_<word>, MAX_<WORD> or Max<Word>, with or without a qualifier.
        A bound name can stand on either side.
      * A bound is also a call of size(), capacity() or max_size() with no
        arguments on a named object, on the right of the counter.  On the
        left, `v.size() == n` reads the other way round, and the tree
        uses that shape for a length check, not for a fullness test.
    `!=` is not read.  Every `!= <bound>` in the tree is a not-found
    sentinel, and no `!=` spells not-full.
    A comparison that runs only in constant evaluation is not read: one in
    a static_assert, a template argument, a requires clause or a consteval
    function.  An out-of-bounds write there is a compile error.
    A counter whose last segment ends in `_` is skipped.  The house
    convention names a member that a class invariant protects that way, and
    across the measured sites the convention tracks the danger exactly.
    This is a heuristic: a private counter that two methods write can still
    pass its bound, and the guard does not see it.

MACRO BODIES
    A macro body is parsed on its own (tsast.macro_bodies), with every
    fragment joined, and the guard reads the same shapes in its tree, so a
    comment inside the body does not hide a comparison and a static_assert
    in it is a constant context.  A body that the parser cannot read, such
    as one that pastes tokens with ##, is read from its preprocessing
    tokens: a counter chain right before `==` and a bound name or a bound
    call right after it, or a bound name right before `==` and a counter
    chain right after it.  The token reader does not know a constant
    context.  The files of tsast.UNPARSEABLE are not C++ and are out of
    scope.  A parse error in any other file is a violation.

EXEMPTIONS
    `// FULLNESS-OK: <reason>` on a line of the comparison, or on a line of
    the condition or statement that holds it, exempts the site.  The reason
    must not be empty.
    A row `path:text — reason` in scripts/fullness-guard-allowlist.txt
    exempts each site in that file whose first line, trimmed, equals
    `text`.  The key is the content of the line, not its number.  A row
    with no reason, a duplicate row and a row that matches no live site
    fail the check.

Exit 0 clean, 1 on a violation or a parse failure, 2 on a malformed or stale
row, a usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

ROOTS = ("include", "src", "vessel")
EXCLUDED_COMPONENTS = frozenset({"third_party", "external", "vendor"})
ALLOWLIST = "scripts/fullness-guard-allowlist.txt"
BOUND_NAME = re.compile(r"(?:[Cc]apacity|max_\w+|MAX_\w+|Max[A-Z]\w*)")
BOUND_CALLS = frozenset({"size", "capacity", "max_size"})
MARKER = re.compile(r"FULLNESS-OK:\s*\S")
MEMBER_ACCESS = (".", "->")
SEPARATOR = " — "
# The nodes whose last line still belongs to the comparison, for a marker.
STATEMENTS = ("condition_clause", "expression_statement", "return_statement", "declaration",
              "field_declaration", "init_declarator", "static_assert_declaration")


@dataclass(frozen=True)
class Site:
    """One `==` fullness test."""

    path: str
    line: int
    key: str


def final_segment(node: tsast.Node) -> tsast.Node | None:
    """Return the last name of an identifier, a qualified name or a member access, or None."""
    current: tsast.Node | None = node
    while current is not None:
        if current.type in ("identifier", "field_identifier", "type_identifier", "namespace_identifier"):
            return current
        if current.type == "qualified_identifier":
            current = current.child_by_field("name")
        elif current.type == "field_expression":
            current = current.child_by_field("field")
        else:
            return None
    return None


def unwrap(node: tsast.Node | None) -> tsast.Node | None:
    """Strip parentheses and a static_cast from an operand."""
    while node is not None:
        if node.type == "parenthesized_expression":
            inner = [child for child in node.children if child.type != "comment"]
            node = inner[0] if len(inner) == 1 else None
        elif node.type == "call_expression" and (callee := node.child_by_field("function")) is not None \
                and callee.type == "template_function" \
                and (named := callee.child_by_field("name")) is not None and tsast.spelled(named) == "static_cast":
            arguments = node.child_by_field("arguments")
            inner = [child for child in arguments.children if child.type != "comment"] if arguments else []
            node = inner[0] if len(inner) == 1 else None
        else:
            return node
    return None


def is_counter(node: tsast.Node | None) -> bool:
    """Return True for a name, a member access chain or a subscript of one, with no call in it."""
    node = unwrap(node)
    if node is None:
        return False
    if node.type == "subscript_expression":
        return is_counter(node.child_by_field("argument"))
    if node.type in ("identifier", "qualified_identifier"):
        return final_segment(node) is not None
    if node.type == "field_expression":
        base = node.child_by_field("argument")
        return base is not None and (base.type == "this" or is_counter(base)) and final_segment(node) is not None
    return False


def counter_is_protected(node: tsast.Node) -> bool:
    """Return True when the last name of a counter ends in `_`, the mark of an invariant-protected member."""
    node = unwrap(node)
    while node is not None and node.type == "subscript_expression":
        node = unwrap(node.child_by_field("argument"))
    last = final_segment(node) if node is not None else None
    return last is not None and tsast.lexeme(last).endswith("_")


def is_bound_name(node: tsast.Node | None) -> bool:
    """Return True for a name whose last segment is shaped like a capacity."""
    node = unwrap(node)
    if node is None or node.type not in ("identifier", "qualified_identifier", "field_expression"):
        return False
    last = final_segment(node)
    return last is not None and BOUND_NAME.fullmatch(tsast.lexeme(last)) is not None


def is_bound_call(node: tsast.Node | None) -> bool:
    """Return True for a size(), capacity() or max_size() call on a named object, with no arguments."""
    node = unwrap(node)
    if node is None or node.type != "call_expression":
        return False
    callee = node.child_by_field("function")
    arguments = node.child_by_field("arguments")
    if callee is None or callee.type != "field_expression" or arguments is None \
            or [child for child in arguments.children if child.type != "comment"]:
        return False
    last = final_segment(callee)
    return last is not None and tsast.spelled(last) in BOUND_CALLS and is_counter(callee.child_by_field("argument"))


def is_constant_context(node: tsast.Node) -> bool:
    """Return True when a comparison runs only in constant evaluation, where an out-of-bounds write is a
    compile error: a static_assert, a template argument, a requires clause or a consteval function."""
    if node.ancestor_of_type("static_assert_declaration", "template_argument_list", "requires_clause",
                             "requires_expression") is not None:
        return True
    function = node.ancestor_of_type("function_definition")
    while function is not None:
        if any(child.type == "type_qualifier" and child.text == "consteval" for child in function.children):
            return True
        function = function.ancestor_of_type("function_definition")
    return False


def marker_rows(node: tsast.Node) -> range:
    """Return the rows on which a marker exempts a comparison: its own, and those of its statement."""
    last = node.end[0]
    holder = node.ancestor_of_type(*STATEMENTS)
    if holder is not None:
        last = max(last, holder.end[0])
    return range(node.start[0], last + 1)


def fullness_nodes(root: tsast.Node) -> Iterator[tsast.Node]:
    """Yield each `==` fullness test under the root of a file or of a macro body, before any exemption.

    Complexity: linear in the number of nodes under the root.
    """
    for node in root.descendants("binary_expression"):
        left, right = node.child_by_field("left"), node.child_by_field("right")
        if left is None or right is None or tsast.operator_of(node) != "==":
            continue
        is_forward = is_counter(left) and not counter_is_protected(left) \
            and (is_bound_name(right) or is_bound_call(right))
        is_reversed = is_counter(right) and not counter_is_protected(right) and is_bound_name(left)
        if (is_forward or is_reversed) and not is_constant_context(node):
            yield node


def counter_before(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the first token of a counter chain that ends right before a token, or None.

    A chain is `name`, `(name)`, and then `.name` or `->name` any number of
    times.  A chain that a name, `::`, `.` or `->` precedes is part of a
    larger expression and does not count.
    """
    cursor = index - 1
    if cursor < 0 or tokens[cursor].kind != "identifier":
        return None
    while cursor >= 2 and tokens[cursor - 1].text in MEMBER_ACCESS and tokens[cursor - 2].kind == "identifier":
        cursor -= 2
    if cursor >= 3 and tokens[cursor - 1].text in MEMBER_ACCESS and tokens[cursor - 2].text == ")" \
            and tokens[cursor - 3].kind == "identifier" and cursor >= 4 and tokens[cursor - 4].text == "(":
        cursor -= 4
    before = tokens[cursor - 1] if cursor >= 1 else None
    if before is not None and (before.kind == "identifier" or before.text in ("::", *MEMBER_ACCESS)):
        return None
    return cursor


def counter_after(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the last token of a counter chain that starts at a token, or None.

    A chain that a call, `.`, `->` or a name follows is part of a larger
    expression and does not count.
    """
    cursor = index
    if cursor < len(tokens) and tokens[cursor].text == "(" and cursor + 2 < len(tokens) \
            and tokens[cursor + 1].kind == "identifier" and tokens[cursor + 2].text == ")":
        cursor += 2
    elif cursor >= len(tokens) or tokens[cursor].kind != "identifier":
        return None
    while cursor + 2 < len(tokens) and tokens[cursor + 1].text in MEMBER_ACCESS \
            and tokens[cursor + 2].kind == "identifier":
        cursor += 2
    after = tokens[cursor + 1] if cursor + 1 < len(tokens) else None
    if after is not None and (after.kind == "identifier" or after.text in ("(", *MEMBER_ACCESS)):
        return None
    return cursor


def bound_after(tokens: list[tsast.Token], index: int) -> bool:
    """Say whether a bound name or a bound call starts at a token."""
    cursor = index
    while cursor + 2 < len(tokens) and tokens[cursor].kind == "identifier" and tokens[cursor + 1].text == "::":
        cursor += 2
    if cursor < len(tokens) and tokens[cursor].kind == "identifier" \
            and BOUND_NAME.fullmatch(tsast.lexeme(tokens[cursor])) \
            and (cursor + 1 >= len(tokens) or tokens[cursor + 1].text != "("):
        return True
    cursor = index
    while cursor + 2 < len(tokens) and tokens[cursor].kind == "identifier" \
            and tokens[cursor + 1].text in MEMBER_ACCESS:
        cursor += 2
    return (cursor >= index + 2 and cursor + 2 < len(tokens) and tokens[cursor].text in BOUND_CALLS
            and tokens[cursor + 1].text == "(" and tokens[cursor + 2].text == ")")


def bound_before(tokens: list[tsast.Token], index: int) -> bool:
    """Say whether a bound name ends right before a token, with no member access before it."""
    cursor = index - 1
    if cursor < 0 or tokens[cursor].kind != "identifier" or not BOUND_NAME.fullmatch(tsast.lexeme(tokens[cursor])):
        return False
    while cursor >= 2 and tokens[cursor - 1].text == "::" and tokens[cursor - 2].kind == "identifier":
        cursor -= 2
    return cursor == 0 or tokens[cursor - 1].text not in MEMBER_ACCESS


def token_rows(tokens: list[tsast.Token]) -> Iterator[int]:
    """Yield the row of each `==` fullness test in the tokens of a macro body that did not parse.

    The row is the row of the counter.  A counter whose last name ends in
    `_` is protected, as in the tree.
    """
    for index, token in enumerate(tokens):
        if token.text != "==":
            continue
        start = counter_before(tokens, index)
        if start is not None and not tsast.lexeme(tokens[index - 1]).endswith("_") and bound_after(tokens, index + 1):
            yield tokens[start].row
            continue
        end = counter_after(tokens, index + 1)
        if end is not None and not tsast.lexeme(tokens[end]).endswith("_") and bound_before(tokens, index):
            yield tokens[index + 1].row


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan roots, sorted, without build trees and vendored code."""
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            for path in base.rglob("*"):
                rel = path.relative_to(root)
                if path.is_file() and tsast.is_in_cpp_scope(rel) \
                        and not any(part in EXCLUDED_COMPONENTS or part.startswith("build") for part in rel.parts[:-1]):
                    found.append(path)
    return sorted(found)


def scan(root: Path) -> tuple[list[Site], list[str]]:
    """Find every unexempted `==` fullness test under a scan root.

    Complexity: linear in the total size of the files in scope.

    Returns:
        The sites, and each file the parser cannot read
    """
    failures: list[str] = []
    trees: list[tsast.Tree] = []
    rows: dict[str, set[int]] = {}
    marks: dict[str, set[int]] = {}
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        trees.append(tree)
        marks[rel] = {node.start[0] for node in tree.find("comment") if MARKER.search(tsast.prose_text(node))}
        rows[rel] = {node.start[0] for node in fullness_nodes(tree.root)
                     if not any(row in marks[rel] for row in marker_rows(node))}
    for body in tsast.macro_bodies(trees):
        rel = Path(body.define.tree.path).relative_to(root).as_posix()
        # A marker on any row of the definition, to the row of its last token, exempts the tests of its body.
        if any(row in marks[rel] for row in range(body.define.start[0], body.last_row + 1)):
            continue
        if body.is_parsed:
            rows[rel].update(body.origin(node)[0] for node in fullness_nodes(body.root))
        else:
            rows[rel].update(token_rows(tsast.pp_tokens(body.text, body.first_row)))
    sites: list[Site] = []
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        sites.extend(Site(rel, row + 1, tree.line(row).strip()) for row in sorted(rows[rel]))
    return sites, failures


def read_allowlist(path: Path) -> tuple[dict[str, str], list[str]]:
    """Read the content-keyed rows of the allowlist.

    Returns:
        Each `path:text` key with its reason, and each problem with a row
    """
    rows: dict[str, str] = {}
    problems: list[str] = []
    if not path.is_file():
        return rows, problems
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, found, reason = line.partition(SEPARATOR)
        if not found or not reason.strip() or ":" not in key:
            problems.append(f"{ALLOWLIST}:{number}: a row is `path:text — reason`, and the reason must not be "
                            f"empty.")
        elif key in rows:
            problems.append(f"{ALLOWLIST}:{number}: the key {key} is on two rows.")
        else:
            rows[key] = reason.strip()
    return rows, problems


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a violation or a parse failure, 2 on a malformed or stale row
    """
    sites, failures = scan(root)
    rows, problems = read_allowlist(root / ALLOWLIST)
    live = {f"{site.path}:{site.key}" for site in sites}
    violations = [site for site in sites if f"{site.path}:{site.key}" not in rows]
    for site in violations:
        print(f"FULLNESS violation: {site.path}:{site.line} — an `==` fullness test against a capacity bound. "
              f"Write `>=`, so that a count past the bound still reads as full.  Allowlist key: "
              f"{site.path}:{site.key}", file=sys.stderr)
    for failure in failures:
        print(f"FULLNESS parse failure: {failure}", file=sys.stderr)
    stale = [key for key in rows if key not in live]
    for key in stale:
        print(f"FULLNESS stale row: {key} — no `==` fullness test has this text. Remove the row.", file=sys.stderr)
    for problem in problems:
        print(f"FULLNESS malformed row: {problem}", file=sys.stderr)
    if violations or failures:
        print("check-fullness-guard: make the counter private so that the guarded push is its only writer, "
              "or write `>=`. For a not-found sentinel, mark the line `// FULLNESS-OK: <reason>` or add the "
              "printed key with a reason.", file=sys.stderr)
        return 1
    if stale or problems:
        return 2
    print("check-fullness-guard: clean — no `==` fullness test on an unprotected counter.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each fullness shape and each exemption, then check the verdicts.

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

    # Each line of the planted header, and whether the guard reports it.
    planted: list[tuple[str, bool | None, str]] = [
        ("#pragma once", None, ""),
        ("struct S {", None, ""),
        ("    bool a() { if (count == Capacity) { return false; } return true; }", True, "a public counter"),
        ("    bool b(T& state) { return state.count == Capacity; }", True, "a member of an object"),
        ("    bool c(T* hdr) { return hdr->n == MaxPeers; }", True, "a member through a pointer"),
        ("    bool d() { return size == slots.size(); }", True, "a size() bound"),
        ("    bool e() { return Capacity == count; }", True, "the bound on the left"),
        ("    bool f() { return count == Self::Capacity; }", True, "a qualified bound"),
        ("    bool g() { return static_cast<int>(count) == MAX_SLOTS; }", True, "a static_cast counter"),
        ("    bool h() { return (count) == (max_items); }", True, "parentheses"),
        ("    bool i() { return this->count == buffer.capacity(); }", True, "this and a capacity() bound"),
        ("    bool j() { return counts[k] == Capacity; }", True, "a subscript counter"),
        ("    bool k() { return count", True, "a comparison that spans two lines"),
        ("        == Capacity; }", None, ""),
        ("    bool l() { return count == Capacity; }  // FULLNESS-OK: fixture", False, "a marked line"),
        ("    bool m() { return count == Capacity; }  // FULLNESS-OK:", True, "a marker with no reason"),
        ("    bool n() {", None, ""),
        ("        return count", False, "a marker on the last line of the statement"),
        ("            == Capacity;  // FULLNESS-OK: fixture", None, ""),
        ("    }", None, ""),
        ("    bool o() { return count_ == Capacity; }", False, "a counter that ends in _"),
        ("    bool p() { return count >= Capacity; }", False, "the correct >="),
        ("    bool q() { return count != Capacity; }", False, "a != sentinel"),
        ("    bool r() { return v.size() == v.max_size(); }", False, "a container that asks its own invariant"),
        ("    bool s() { return count == limit; }", False, "a bound of another name"),
        ("    bool t() { return count == Capacity(); }", False, "a call to a free function named Capacity"),
        ("    // if (count == Capacity) { return false; }", False, "a comment"),
        ('    const char* u = "count == Capacity";', False, "a string literal"),
        ("    bool w() { return v.size() == count; }", False, "a size() call on the left"),
        ("    bool x() { return n == std::string_view{buf}.size(); }", False, "a size() of an unnamed object"),
        ("    static_assert(count == Capacity);", False, "a static_assert"),
        ("    consteval bool y() { return count == Capacity; }", False, "a consteval function"),
        ("    bool z() { return lhs == Capacity && rhs == Capacity; }", True, "two sites on one line"),
        ("    bool v() { return allowed == Capacity; }", False, "an allowlisted site"),
        ("    bool aa(T& s) { return s.count /* n */ == Capacity; }", True, "a comment before the operator"),
        ("};", None, ""),
        ("#define FULL_IN_MACRO(s) ((s).count == Capacity)", True, "a macro body"),
        ("#define REVERSED_IN_MACRO(s) (Traits::MaxItems == (s)->n)", True, "a bound name first in a macro body"),
        ("#define PRIVATE_IN_MACRO(s) (s.count_ == Capacity)", False, "a protected counter in a macro body"),
        ("#define OWN_SIZE_IN_MACRO(v) (v.size() == v.max_size())", False, "a call counter in a macro body"),
        ("#define CALL_IN_MACRO(s) (s.n == s.slots.size())", True,
         "a size() bound in a macro body, with a marked definition on the next row"),
        ("#define MARKED_IN_MACRO(s) (s.count == Capacity)  // FULLNESS-OK: fixture", False,
         "a marked macro body"),
        ('#define TEXT_IN_MACRO "count == Capacity"', False, "a string literal in a macro body"),
        ("#define SUBSCRIPT_IN_MACRO(s) ((s).counts[i] == Capacity)", True, "a subscript counter in a macro body"),
        ("#define CAST_IN_MACRO(s) (static_cast<int>(s.count) == MAX_SLOTS)", True,
         "a static_cast counter in a macro body"),
        ("#define ASSERT_IN_MACRO(s) static_assert((s).count == Capacity)", False, "a static_assert in a macro body"),
        ("#define COMMENT_IN_MACRO(s) ((s).count /* n */ == \\", True,
         "a macro body that a comment and a line splice cut"),
        ("    Capacity)", None, ""),
        ("#define PASTE_IN_MACRO(s) (s.count##_x == Capacity)", True,
         "a macro body that pastes tokens, read from its tokens"),
        ("#define PASTE_PRIVATE(s) (s.count##_ == Capacity)", False,
         "a protected counter in a macro body that pastes tokens"),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        header = root / "include/crucible/planted/Planted.h"
        header.parent.mkdir(parents=True)
        header.write_text("\n".join(line for line, _, _ in planted) + "\n", encoding="utf-8")
        allow = root / ALLOWLIST
        allow.parent.mkdir(parents=True)
        allowed_key = "include/crucible/planted/Planted.h:bool v() { return allowed == Capacity; }"
        allow.write_text(f"# planted\n{allowed_key} — a planted not-found sentinel\n", encoding="utf-8")
        (root / "vessel").mkdir()
        (root / "vessel/Out.cpp").write_text("bool w(int count) { return count == Capacity; }\n", encoding="utf-8")
        (root / "include/vendor").mkdir(parents=True)
        (root / "include/vendor/Lib.h").write_text("bool x() { return count == Capacity; }\n", encoding="utf-8")
        sites, broken = scan(root)
        rows, _ = read_allowlist(allow)
        reported = {site.line for site in sites if site.path == "include/crucible/planted/Planted.h"
                    and f"{site.path}:{site.key}" not in rows}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        expect("caught: a site in vessel/", any(site.path == "vessel/Out.cpp" for site in sites))
        expect("not caught: vendored code", not any("vendor" in site.path for site in sites), True)
        expect("the planted tree parses", not broken, True)

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

        allowed_line = next(line for line, _, label in planted if label == "an allowlisted site")
        header.write_text("#pragma once\n\n\n" + "\n".join(line for line, _, _ in planted[:2]) + "\n"
                          + allowed_line + "\n};\n", encoding="utf-8")
        (root / "vessel/Out.cpp").unlink()
        expect("a content key survives a line shift", captured(root)[0] == 0, True)
        allow.write_text(f"{allowed_key} — a planted not-found sentinel\n"
                         f"include/crucible/planted/Planted.h:gone == Capacity — a stale row\n", encoding="utf-8")
        code, report = captured(root)
        expect("a stale row exits 2", code == 2 and "stale row" in report)
        allow.write_text(f"{allowed_key}\n", encoding="utf-8")
        code, report = captured(root)
        expect("a row with no reason admits nothing and is reported",
               code == 1 and "malformed row" in report and "Allowlist key: " + allowed_key in report)
        allow.write_text(f"{allowed_key} — one\n{allowed_key} — two\n", encoding="utf-8")
        code, report = captured(root)
        expect("a duplicate row exits 2", code == 2 and "on two rows" in report)
        allow.write_text(f"{allowed_key} — a planted not-found sentinel\n", encoding="utf-8")
        rostered = root / next(iter(tsast.UNPARSEABLE))
        rostered.parent.mkdir(parents=True, exist_ok=True)
        rostered.write_text("void f() { g(1) { } }\nbool s() { return count == Capacity; }\n", encoding="utf-8")
        sites, broken = scan(root)
        expect("a file of the UNPARSEABLE roster is out of scope",
               not broken and not any(site.path == rostered.relative_to(root).as_posix() for site in sites), True)
        soup = "include/crucible/planted/Soup.h"
        (root / soup).write_text("void f() { g(1) { } }\nbool s() { return count == Capacity; }\n",
                                 encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-fullness-guard --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-fullness-guard --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-fullness-guard.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-fullness-guard: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
