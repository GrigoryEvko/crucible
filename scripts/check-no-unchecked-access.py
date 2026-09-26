#!/usr/bin/env python3
"""check-no-unchecked-access — access_context::unchecked() only in reviewed reflection walks.

A splice of a nonstatic data member has no access check in GCC 16: given the
reflection of a private member m, `obj.[:m:] = v` writes it.  The reflection
of a private member comes only from a walk under
std::meta::access_context::unchecked(), so that call is the one door to the
private state of every proof type: a count, a stamp, a grade, a Refined value
or a Secret payload.  scripts/unchecked-access-allowlist.txt names each file
that may open the door, with the reason, and the guard refuses the door
anywhere else.

HOW THE GUARD READS A FILE
    The guard reads the parse tree of scripts/tsast.py, so white space, a
    line break or a comment between two tokens changes nothing, and a mention
    in a comment or a string is not a use.  The body of each #define is
    parsed on its own by tsast.macro_bodies.  A body that is not C++ on its
    own, because it uses # or ##, is read from its preprocessing tokens.

WHAT COUNTS AS THE DOOR
      1. A member named unchecked: the name of a qualified name, or the field
         of a member access.  Every qualifier counts, so a namespace alias, a
         using-directive, a spliced class and a call through an object reach
         the same member.  A plain variable named unchecked is not a door.
      2. A using-declaration, an alias declaration or a typedef that names
         access_context.
      3. A reflection of access_context, of std::meta or of std.  A walk of
         the members of one of them reaches unchecked by reflection, with no
         name to read.
      4. A splice that names a member of an object: the field of a member
         access, `obj.[:m:]` and `p->[:m:]`, or the operand of a unary `&`.
         This is the write itself, so it closes every route to the
         reflection, such as a walk of ^^decltype(access_context::current())
         that finds unchecked by a string compare.  A binary `&` before a
         splice, as in `mask & [:field:]`, is not a door.
      5. A use of a macro whose body opens the door, directly or through
         another macro.  The use opens the door where it expands.

WHAT COUNTS AS AN OPEN TARGET
    A bare splice of a static data member, `[:m:] = v`, has no access check
    either, and no token before the `[:` marks it.  The tree holds many value
    splices, and the parse cannot tell an enumerator from a static data
    member.  So the guard refuses the target, not the spelling:
      6. A writable static data member that is private or protected, in a
         class in include/foundation, include/fixy or include/crucible, less
         the frozen prefixes in scripts/frozen-paths.txt.  Writable means that
         the member itself is not const and not constexpr: a pointer to const
         is writable, a const pointer is not, and a reference is writable when
         its referent is.  The access of each #if arm is joined, so an access
         label inside one arm does not open the members after the block.  No
         allowlist admits a member: make it const or constexpr, or move it
         into the object.

THE EXPANDED RUN
    With --compile-db the guard also reads the macro-expanded text of each
    file from the preprocessor store of scripts/preprocessed.py, one parse
    for each distinct expansion.  That run sees a door that ## builds from
    pieces, and a static data member that a macro declares, with the access
    of the class where the macro expands.  ctest runs the guard this way in
    the build legs.  A door that only an expansion shows needs a row that the
    run without the database calls stale, so spell the door, or do not open
    it.  A unit that the preprocessor rejects, and an expansion that the
    parser cannot read, fail the run.

REVIEW RULE FOR THE ALLOWED FILES
    An allowed file may not return or publish a reflection of a nonstatic
    data member to its caller.  A public constexpr verdict whose field names
    a private member reopens the door, because `obj.[:verdict.field:]` needs
    no unchecked().  The guard does not check this rule.  The reviewer of each
    row does.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A reflection of an access context that the guard does not name, such
      as parent_of of a std::meta type, or ^^T in a template that deduces T
      from a call to access_context::current().  Such a file walks private
      members with no door token.  Rules 4 and 6 still refuse the write to a
      nonstatic member and the target of a static one.
    - A const static member whose class type holds a mutable member, and the
      object that a const pointer member points to.
    - A door that ## builds, and a static data member that a macro
      declares, in a file that no unit of the compile database reads.
      Without --compile-db, the guard sees neither in any file.
    - A file that scripts/tsast.py lists as not C++, and a member of the
      frozen tree, which is deleted, not edited.

A stale row, one whose file does not exist or does not open the door, fails
the guard, so the allowlist only shrinks.

Exit 0 clean, 1 on a door outside the allowlist, a stale row, an open target
or an input the guard cannot read, 2 on a usage error, a bad allowlist or a
failed self-test, 3 when the parser kit is missing.
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable, Iterable, Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402
import tsast  # noqa: E402
from preprocessed import Store, files_of  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = "scripts/unchecked-access-allowlist.txt"
FROZEN_PATHS = "scripts/frozen-paths.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
MEMBER_ROOTS = ("include/foundation/", "include/fixy/", "include/crucible/")
# Bumped when the rules change, so the expanded run does not reuse an old result.
SCAN_VERSION = 1
# The leaves that spell one part of a name.
NAME_LEAVES = ("identifier", "field_identifier", "type_identifier", "namespace_identifier")
# The declarations that give access_context a second name.
ALIASES = ("using_declaration", "alias_declaration", "type_definition")
# The directives whose name node mentions a macro without a use of it.
MACRO_MENTIONS = frozenset({"preproc_def", "preproc_function_def", "preproc_ifdef", "preproc_defined"})
# The tokens that end a statement of a macro body that does not parse.
STATEMENT_END = frozenset({";", "{", "}"})
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
CONDITIONALS = frozenset({"preproc_if", "preproc_ifdef", "preproc_elif", "preproc_elifdef"})
DECLARATIONS = frozenset({"field_declaration", "declaration", "template_declaration"})
NAMES = frozenset({"field_identifier", "identifier", "qualified_identifier", "operator_name", "destructor_name",
                   "template_function"})
# Declarators that change neither the object that a name declares nor its
# constness: an array has the constness of its element.
TRANSPARENT = frozenset({"init_declarator", "parenthesized_declarator", "array_declarator", "attributed_declarator"})

Door = tuple[int, str]
Target = tuple[str, int, str, str]


class Refused(Exception):
    """The allowlist is missing or malformed (exit 2)."""


def read_allowlist(root: Path) -> list[tuple[int, str]]:
    """Return (line, path) for each row of the allowlist.

    Raises:
        Refused: If the allowlist is missing or a row has no reason
    """
    listing = root / ALLOWLIST
    if not listing.is_file():
        raise Refused(f"{ALLOWLIST} is missing, so no file may open the door.")
    rows = []
    for number, line in enumerate(listing.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        path, separator, reason = line.partition(" — ")
        if not separator or not path.strip() or not reason.strip():
            raise Refused(f"{ALLOWLIST}:{number} is not PATH — REASON.")
        rows.append((number, path.strip()))
    return rows


def scope_files(root: Path) -> list[str]:
    """Return the C++ files under the scan roots that git does not ignore."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = {p for p in listed.stdout.decode().split("\0") if p}
    else:
        paths = {str(p.relative_to(root)) for r in SCAN_ROOTS for p in (root / r).rglob("*") if p.is_file()}
    return sorted(p for p in paths if tsast.is_in_cpp_scope(p) and (root / p).is_file())


def name_parts(node: tsast.Node) -> tuple[str, ...] | None:
    """Return the parts of a name or of a type descriptor that is a name, or None for any other shape.

    A leading `::` adds no part.  A scope that is not a name, such as a
    splice or a decltype, gives None.
    """
    if node.type == "type_descriptor":
        inner = node.child_by_field("type")
        return None if inner is None else name_parts(inner)
    found = tsast.qualified_parts(node)
    return None if found is None else found[1]


def names_member(leaf: tsast.Node) -> bool:
    """Return whether a name leaf is the member that a qualified name or a member access names.

    `A::unchecked`, `a.unchecked`, `p->unchecked` and their template forms,
    with or without the `template` keyword, qualify.  A bare `unchecked` is a
    plain variable, and does not.
    """
    node = leaf
    if node.parent is not None and node.parent.type in ("template_function", "template_method") \
            and node.field == "name":
        node = node.parent
    if node.parent is not None and node.parent.type == "dependent_name":
        node = node.parent
    parent = node.parent
    if parent is None:
        return False
    return ((parent.type == "qualified_identifier" and node.field == "name")
            or (parent.type == "field_expression" and node.field == "field"))


def leading_operator(node: tsast.Node) -> str:
    """Return the operator token of a unary pointer expression: `&` or `*`."""
    tokens = node.gap_tokens()
    return tokens[0] if tokens else ""


def is_macro_use(leaf: tsast.Node) -> bool:
    """Return whether a name leaf can be the use of a macro: not the name that a directive defines or tests."""
    parent = leaf.parent
    return parent is None or parent.type not in MACRO_MENTIONS


def node_doors(root: tsast.Node, line_of: Callable[[tsast.Node], int],
               door_macros: frozenset[str] = frozenset()) -> list[Door]:
    """Return (line, shape) for each way the nodes below root open the unchecked access context.

    Args:
        root: The root of the walk
        line_of: The one-based line in the file of a node
        door_macros: The macros whose body opens the door

    Complexity: linear in the number of nodes.
    """
    found: list[Door] = []
    for leaf in root.descendants(*NAME_LEAVES):
        if leaf.text == "unchecked" and names_member(leaf):
            found.append((line_of(leaf), "a use of unchecked"))
        elif leaf.text == "access_context" and leaf.ancestor_of_type(*ALIASES) is not None:
            found.append((line_of(leaf), "a using-declaration or an alias of access_context"))
        elif leaf.text in door_macros and is_macro_use(leaf):
            found.append((line_of(leaf), f"a use of the macro {leaf.text}, which opens the door"))
    for node in root.descendants("reflect_expression"):
        operand = next((c for c in node.children if c.type != "comment"), None)
        parts = None if operand is None else name_parts(operand)
        if parts and (parts in (("std",), ("std", "meta")) or parts[-1] == "access_context"):
            found.append((line_of(node), f"a reflection of {'::'.join(parts)}"))
    for node in root.descendants("splice_expression"):
        parent = node.parent
        if parent is None:
            continue
        if parent.type == "field_expression" and node.field == "field":
            found.append((line_of(node), "a splice that names a member of an object"))
        elif parent.type == "pointer_expression" and node.field == "argument" and leading_operator(parent) == "&":
            found.append((line_of(node), "a splice that names a member of an object"))
    return found


def name_after(tokens: Sequence[tsast.Token], index: int) -> tuple[str, ...]:
    """Return the parts of the qualified name that starts at tokens[index], or () when none starts there."""
    if index < len(tokens) and tokens[index].text == "::":
        index += 1
    parts: list[str] = []
    while index < len(tokens) and tokens[index].kind == "identifier":
        parts.append(tokens[index].text)
        if index + 2 < len(tokens) and tokens[index + 1].text == "::" and tokens[index + 2].kind == "identifier":
            index += 2
        else:
            break
    return tuple(parts)


def token_doors(tokens: Sequence[tsast.Token], door_macros: frozenset[str] = frozenset()) -> list[Door]:
    """Return (line, shape) for each door in the tokens of a macro body that does not parse as C++.

    The tokens cannot tell a unary `&` from a binary one, so `&` before a
    splice counts, and the rule fails closed.

    Complexity: linear in the number of tokens.
    """
    found: list[Door] = []
    statement: list[tsast.Token] = []
    for index, token in enumerate(tokens):
        before = index - 1
        if before > 0 and tokens[before].text == "template":
            before -= 1
        previous = tokens[before].text if before >= 0 else ""
        if token.kind == "identifier" and token.text == "unchecked" and previous in ("::", ".", "->"):
            found.append((token.row + 1, "a use of unchecked"))
        elif token.kind == "identifier" and token.text in door_macros:
            found.append((token.row + 1, f"a use of the macro {token.text}, which opens the door"))
        elif token.text == "[:" and previous in (".", "->", "&"):
            found.append((token.row + 1, "a splice that names a member of an object"))
        elif token.text == "^^":
            parts = name_after(tokens, index + 1)
            if parts and (parts in (("std",), ("std", "meta")) or parts[-1] == "access_context"):
                found.append((token.row + 1, f"a reflection of {'::'.join(parts)}"))
        if token.text in STATEMENT_END:
            statement = []
            continue
        statement.append(token)
        if token.text == "access_context" and any(t.text in ("using", "typedef") for t in statement):
            found.append((token.row + 1, "a using-declaration or an alias of access_context"))
    return found


def body_names(body: tsast.MacroBody) -> set[str]:
    """Return the identifiers that a macro body names, from its parse or from its tokens."""
    if body.is_parsed:
        return {leaf.text for leaf in body.root.descendants(*NAME_LEAVES)}
    return {token.text for token in tsast.pp_tokens(body.text) if token.kind == "identifier"}


def body_doors(body: tsast.MacroBody, door_macros: frozenset[str] = frozenset()) -> list[Door]:
    """Return (line, shape) for each door of one macro body, with lines of the file that defines it."""
    if body.is_parsed:
        return node_doors(body.root, lambda node: body.origin(node)[0] + 1, door_macros)
    return token_doors(tsast.pp_tokens(body.text, body.first_row), door_macros)


def door_macro_names(bodies: Sequence[tsast.MacroBody]) -> frozenset[str]:
    """Return the names of the macros whose body opens the door, directly or through another macro.

    A macro that names a door macro opens the door when it expands, so the
    set grows until no body adds a name.

    Complexity: O(B * R) for B bodies and R rounds, and R is at most the
    depth of the deepest chain of macros.
    """
    names = {body.name for body in bodies if body_doors(body)}
    mentions = [(body.name, body_names(body)) for body in bodies]
    grown = True
    while grown:
        grown = False
        for name, mentioned in mentions:
            if name not in names and mentioned & names:
                names.add(name)
                grown = True
    return frozenset(names)


def display(node: tsast.Node) -> str:
    """Return the name that a report shows for a declarator: its parts joined by ::, comments left out."""
    parts = name_parts(node)
    return "::".join(parts) if parts else tsast.spelled(node)


def is_const_qualified(node: tsast.Node) -> bool:
    """Return whether a pointer declarator carries const on the pointer itself."""
    return any(child.type == "type_qualifier" and child.text == "const" for child in node.children)


def declarator_verdict(declarator: tsast.Node, const_specifier: bool) -> tuple[str, bool] | None:
    """Return (name, writable) for the data member that a declarator declares, or None for a member function.

    The declarator nearest the name decides what the name is.  A shape the
    function does not know counts as writable, so an unknown shape fails
    closed.
    """
    chain: list[tsast.Node] = []
    node = declarator
    while node.type not in NAMES:
        chain.append(node)
        if node.type == "attributed_declarator":
            inner = next((c for c in node.children if c.type != "attribute_declaration"), None)
        else:
            inner = node.child_by_field("declarator")
        if inner is None:
            return display(declarator), True
        node = inner
    name = display(node)
    shaped = [n for n in chain if n.type not in TRANSPARENT]
    if not shaped:
        return name, not const_specifier
    nearest = shaped[-1]
    if nearest.type == "function_declarator":
        return None
    if nearest.type == "pointer_declarator":
        return name, not is_const_qualified(nearest)
    if nearest.type == "reference_declarator":
        referent = shaped[-2] if len(shaped) > 1 else None
        if referent is None:
            return name, not const_specifier
        if referent.type == "pointer_declarator":
            return name, not is_const_qualified(referent)
        return name, referent.type != "function_declarator"
    return name, True


def open_members(declaration: tsast.Node) -> list[tuple[tsast.Node, str]]:
    """Return (declarator, name) for each writable static data member that one member declaration declares."""
    while declaration.type == "template_declaration":
        inner = [c for c in declaration.children if c.type in DECLARATIONS]
        if not inner:
            return []
        declaration = inner[-1]
    words = {c.text for c in declaration.children if c.type in ("storage_class_specifier", "type_qualifier")}
    if "static" not in words or "constexpr" in words:
        return []
    found = []
    for child in declaration.children:
        if child.field != "declarator":
            continue
        verdict = declarator_verdict(child, "const" in words)
        if verdict is not None and verdict[1]:
            found.append((child, verdict[0]))
    return found


def walk_members(nodes: list[tsast.Node], accesses: frozenset[str],
                 found: list[tuple[tsast.Node, str, str]]) -> frozenset[str]:
    """Walk the members of one class body in order, and return the accesses that hold after them.

    An #if block joins the access at the end of each arm, and the access
    before it when no arm must be taken, so a label inside one arm does not
    open the members after the block.
    """
    for node in nodes:
        if node.type == "access_specifier":
            accesses = frozenset({node.text.strip().rstrip(":").strip()})
        elif node.type in CONDITIONALS:
            accesses = walk_conditional(node, accesses, found)
        elif node.type in DECLARATIONS and accesses - {"public"}:
            access = "private" if "private" in accesses else "protected"
            found += [(declarator, access, name) for declarator, name in open_members(node)]
    return accesses


def walk_conditional(node: tsast.Node, accesses: frozenset[str],
                     found: list[tuple[tsast.Node, str, str]]) -> frozenset[str]:
    """Walk one #if, #ifdef or #elif block, and return the join of the accesses at the end of its arms."""
    body = [c for c in node.children if c.field not in ("condition", "name", "alternative")]
    after = walk_members(body, accesses, found)
    alternative = node.child_by_field("alternative")
    if alternative is None:
        return after | accesses
    if alternative.type in CONDITIONALS:
        return after | walk_conditional(alternative, accesses, found)
    return after | walk_members(alternative.children, accesses, found)


def class_members(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return (declarator, access, name) for each writable private or protected static data member of one parse."""
    members: list[tuple[tsast.Node, str, str]] = []
    for node in tree.find(*CLASSES):
        body = node.child_by_field("body")
        if body is not None:
            default = "private" if node.type == "class_specifier" else "public"
            walk_members(body.children, frozenset({default}), members)
    return members


def frozen_prefixes(root: Path) -> tuple[str, ...]:
    """Return the path prefixes of the frozen tree, or none when the list is absent."""
    listing = root / FROZEN_PATHS
    if not listing.is_file():
        return ()
    return tuple(line.strip() for line in listing.read_text().splitlines()
                 if line.strip() and not line.lstrip().startswith("#"))


def reads_members(rel: str, frozen: tuple[str, ...]) -> bool:
    """Return whether the guard reads the static data members of a file."""
    return rel.startswith(MEMBER_ROOTS) and not (frozen and rel.startswith(frozen))


def with_defines(trees: Iterable[tsast.Tree]) -> Iterable[tsast.Tree]:
    """Yield the trees that hold a #define, the only trees whose macro bodies there are to parse."""
    for tree in trees:
        if any(True for _ in tree.find("preproc_def", "preproc_function_def")):
            yield tree


def lexical(root: Path, files: list[str]) -> tuple[dict[str, list[Door]], list[Target], list[str], int]:
    """Parse each file once, and return its doors, the open targets, the unread files and the member files read.

    Every file gives its doors.  A file of the member roots, less the frozen
    tree, also gives its open static data members.  A file that the parser
    cannot read fails.

    Complexity: one parse of each file and of each macro body, linear in
    their nodes, plus the closure of door_macro_names.
    """
    frozen = frozen_prefixes(root)
    parsed: dict[str, tsast.Tree] = {}
    targets: list[Target] = []
    problems: list[str] = []
    member_files = 0
    for tree in tsast.parse([root / rel for rel in files], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its doors "
                            f"and its static data members: {tree.diagnostic}")
            continue
        parsed[rel] = tree
        if reads_members(rel, frozen):
            member_files += 1
            targets += [(rel, node.line, access, name) for node, access, name in class_members(tree)]
    bodies = tsast.macro_bodies(with_defines(parsed.values()))
    door_macros = door_macro_names(bodies)
    doors = {rel: node_doors(tree.root, lambda node: node.line, door_macros) for rel, tree in parsed.items()}
    for body in bodies:
        rel = str(Path(body.define.tree.path).relative_to(root))
        doors[rel] += body_doors(body, door_macros)
    return doors, targets, problems, member_files


def expansion_rows(texts: Sequence[str]) -> tuple[str, list[int]]:
    """Join the chunk texts of one file as preprocessed.joined does, and return the row where each chunk starts."""
    starts: list[int] = []
    row = 0
    for text in texts:
        starts.append(row)
        row += text.count("\n") + 1
    return "\n".join(texts), starts


def expanded(root: Path, compile_db: Path, scope: frozenset[str],
             problems: list[str]) -> tuple[dict[str, list[Door]], list[Target]]:
    """Read the doors and the open targets of each distinct expansion of each file in scope.

    The result of each expansion is cached beside the compile database under
    the store key of the expansion, with rows of the joined text.  The lines
    of the chunks map a row to a line of the file.  The text is expanded, so
    rule 5 has no macro left to find.

    Complexity: linear in the preprocessed output of the units, plus one
    parse for each expansion that is not in the cache.
    """
    frozen = frozen_prefixes(root)
    store = Store(compile_db, root)
    cache = compile_db.parent / "unchecked-access-cache"
    cache.mkdir(exist_ok=True)
    results: dict[str, dict] = {}
    pending: dict[str, tuple[str, list[str]]] = {}
    seen: list[tuple[str, str, list[int]]] = []
    for unit in store.units():
        if unit.failure is not None:
            problems.append(f"PREPROC   {unit.failure} — the guard cannot read the expansions of this unit.")
            continue
        for path, (key, chunks) in files_of(unit).items():
            if path not in scope:
                continue
            seen.append((path, key, [chunk.line for chunk in chunks]))
            if key in results or key in pending:
                continue
            try:
                results[key] = json.loads((cache / f"{SCAN_VERSION}-{key}.json").read_text())
            except (OSError, ValueError):
                pending[key] = (path, [store.text(chunk.digest) for chunk in chunks])
    keys = list(pending)
    joined = [expansion_rows(pending[key][1]) for key in keys]
    items = [(f"{pending[key][0]} (expansion {key[:12]})", text) for key, (text, _) in zip(keys, joined)]
    for key, (_, starts), tree in zip(keys, joined, tsast.parse_texts(items)):
        if tree.diagnostic is not None:
            result = {"unread": tree.diagnostic}
        else:
            result = {"starts": starts,
                      "doors": [[line - 1, shape] for line, shape in node_doors(tree.root, lambda n: n.line)],
                      "members": [[node.start[0], access, name] for node, access, name in class_members(tree)]}
        staging = cache / f"{SCAN_VERSION}-{key}.{os.getpid()}.tmp"
        staging.write_text(json.dumps(result))
        os.replace(staging, cache / f"{SCAN_VERSION}-{key}.json")
        results[key] = result
    doors: dict[str, list[Door]] = {}
    targets: list[Target] = []
    unread: set[str] = set()
    for path, key, lines in seen:
        result = results[key]
        if "unread" in result:
            if path not in unread:
                unread.add(path)
                problems.append(f"PARSE     {path} (a macro expansion) — the parser cannot read it, so the guard "
                                f"cannot see its doors and its static data members: {result['unread']}")
            continue
        starts = result["starts"]

        def line_of(row: int) -> int:
            """Map a row of the joined text to a line of the file."""
            chunk = bisect.bisect_right(starts, row) - 1
            return lines[chunk] + row - starts[chunk]

        doors.setdefault(path, []).extend((line_of(row), shape) for row, shape in result["doors"])
        if reads_members(path, frozen):
            targets += [(path, line_of(row), access, name) for row, access, name in result["members"]]
    return doors, targets


def check(root: Path, compile_db: Path | None = None) -> int:
    """Compare the doors in the tree with the allowlist, and report to stderr.

    Returns:
        0 clean, 1 on a finding, 2 on a bad allowlist
    """
    try:
        rows = read_allowlist(root)
    except Refused as exc:
        print(f"check-no-unchecked-access: {exc}", file=sys.stderr)
        return 2
    admitted = {path for _, path in rows}
    files = scope_files(root)
    doors, targets, problems, parsed = lexical(root, files)
    if compile_db is not None:
        more_doors, more_targets = expanded(root, compile_db, frozenset(files), problems)
        for rel, found in more_doors.items():
            known = {shape for _, shape in doors.get(rel, [])}
            doors.setdefault(rel, []).extend(
                (line, f"{shape} (in a macro expansion)") for line, shape in found if shape not in known)
        named = {(rel, access, name) for rel, _, access, name in targets}
        targets += [target for target in more_targets if (target[0], target[2], target[3]) not in named]
    opened: set[str] = set()
    for rel, line, access, name in sorted(set(targets)):
        problems.append(f"REFUSED   {rel}:{line} — a writable {access} static data member: {name}.  A splice of "
                        f"a static data member has no access check, so any file writes it.  Make it const or "
                        f"constexpr, or move it into the object.")
    for rel, found in sorted(doors.items()):
        if found:
            opened.add(rel)
        if found and rel not in admitted:
            problems += [f"REFUSED   {rel}:{line} — {shape}.  A reflection of a private member lets a splice "
                         f"write a proof's private state.  Walk with access_context::current(), or add a "
                         f"reviewed row to {ALLOWLIST}." for line, shape in sorted(set(found))]
    for number, path in rows:
        if path not in opened:
            problems.append(f"STALE     {ALLOWLIST}:{number}: {path} — does not open the door.  Remove the row.")
    for line in problems:
        print(f"check-no-unchecked-access: {line}", file=sys.stderr)
    if not problems:
        read = "the parse and the macro expansions" if compile_db is not None else "the parse"
        print(f"check-no-unchecked-access: {len(opened)} file(s) open the unchecked access context, "
              f"each on its reviewed row, and in {read} {parsed} file(s) of the member roots hold no writable "
              f"private or protected static data member.")
    return 1 if problems else 0


def self_test() -> int:
    """Plant a repository, prove each verdict, and prove the verdict does not depend on the working directory.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def write(root: Path, rel: str, text: str) -> None:
        """Write one planted file."""
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")

    def captured(root: Path, cwd: Path | None = None, compile_db: Path | None = None) -> tuple[int, str]:
        """Run the check on the planted repository and keep its report."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = check(root, compile_db)
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def expect(root: Path, code: int, needle: str, name: str, negative: bool = False,
               compile_db: Path | None = None, absent: str | None = None) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        got, report = captured(root, compile_db=compile_db)
        ok = got == code and needle in report and (absent is None or absent not in report)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(f"{name}: expected exit {code} and '{needle}', got exit {got}:\n{report}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        throwaway_repo.init(root)
        write(root, "include/foundation/Walk.h",
              "auto m = std::meta::members_of(^^T, std::meta::access_context::unchecked());\n"
              "#define WALK_DOOR std::meta::access_context::unchecked()\n"
              "#define WALK_OUTER WALK_DOOR\n")
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n")
        write(root, "src/Clean.cpp",
              "// std::meta::access_context::unchecked() is refused outside the allowlist.\n"
              'const char* s = "access_context::unchecked()";\n'
              "auto c = std::meta::access_context::current();\nbool unchecked = true;\nint n = unchecked;\n"
              "constexpr auto mask = flags & [:field:];\nconstexpr auto r = ^^my_access_context;\n"
              "#ifdef WALK_DOOR\n#endif\n")
        write(root, FROZEN_PATHS, "# planted\ninclude/crucible/safety/\n")
        write(root, "include/foundation/Closed.h",
              "class Closed {\n    static constexpr int a_ = 1;\n    static const int b_ = 2;\n"
              "    static int* const c_;\n    static const int& d_;\n    static int (&e_)(int);\n"
              "    static int f_();\n    template <class T> static constexpr T g_{};\n"
              "    // static int commented_;\n"
              '    static constexpr const char* s_ = R"(static int quoted_;)";\n'
              "public:\n    static int open_;\n};\nstruct Public { static int counter; };\n"
              "#define NAMED static constexpr int named_ = 3;\n")
        write(root, "include/crucible/safety/Frozen.h", "class Frozen { static int n_; };\n")
        write(root, "src/Local.cpp", "class Local { static int n_; };\n")
        expect(root, 0, "1 file(s) open", "a listed walk, a comment, a string, a plain name, a binary & before a "
                                          "splice, a reflection of another name, a test of a door macro, a closed "
                                          "member, a frozen file and a file outside the member roots pass")

        forgeries = {
            "src/Qualified.cpp": "auto c = std::meta::access_context::unchecked();\n",
            "src/Alias.cpp": "namespace mm = std::meta;\nauto c = mm::access_context::unchecked();\n",
            "src/UsingDeclaration.cpp": "using std::meta::access_context;\nauto c = 1;\n",
            "src/TypeAlias.cpp": "using Door = std::meta::access_context;\n",
            "src/Typedef.cpp": "typedef std::meta::access_context Door;\n",
            "src/MultiLine.cpp": "auto c = std::meta::access_context\n    ::  /* door */\n    unchecked();\n",
            "src/Spliced.cpp": "auto c = [: ^^std::meta::access_context :]::unchecked();\n",
            "src/Member.cpp": "auto c = std::meta::access_context::current().unchecked();\n",
            "src/TemplateMember.cpp": "auto c = holder.template unchecked<int>();\n",
            "src/Address.cpp": "auto f = &std::meta::access_context::unchecked;\n",
            "src/ReflectClass.cpp": "constexpr auto r = ^^std::meta::access_context;\n",
            "src/ReflectNamespace.cpp": "constexpr auto r = ^^std::meta;\n",
            "src/ReflectStd.cpp": "constexpr auto r = ^^::std;\n",
            "src/Macro.cpp": "#define DOOR std::meta::access_context::unchecked()\n",
            "src/PastedMacro.cpp": "#define DOOR(x) std::meta::access_context::x##_unused() . [:x:]\n",
            "src/MacroAlias.cpp": "#define ALIAS(n) using n = std::meta::access_context; n##_tail\n",
            "src/UseMacro.cpp": "auto c = WALK_DOOR;\n",
            "src/UseNestedMacro.cpp": "auto c = WALK_OUTER;\n",
            "src/DefineThroughMacro.cpp": "#define MINE WALK_DOOR\n",
            "src/DecltypeWalk.cpp": "auto m = std::meta::members_of(^^decltype(std::meta::access_context::current()),"
                                    " std::meta::access_context::current());\nsealed.[:field:] = 42;\n",
            "src/TypeOfWalk.cpp": "constexpr auto ctx = std::meta::access_context::current();\n"
                                  "auto t = std::meta::type_of(^^ctx);\nsealed->[:field:] = 42;\n",
            "src/TemplateSplice.cpp": "void w(S& s) { s.template [:m:] = 1; }\n",
            "src/TemplateWalk.cpp": "template <class C> consteval auto f() { return std::meta::members_of(^^C, c); }\n"
                                    "auto g = f<decltype(std::meta::access_context::current())>();\nsealed.[:g:] = 1;\n",
            "src/ParameterWalk.cpp": "consteval auto f(auto c) { return std::meta::members_of(^^decltype(c), c); }\n"
                                     "void w(S& s) { s.[: f(1)[0] :] = 1; }\n",
            "src/MemberPointer.cpp": "constexpr auto m = &[:field:];\nvoid w(S& s) { s.*m = 42; }\n",
        }
        for rel, text in forgeries.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"a door outside the allowlist: {rel}", True)
            (root / rel).unlink()

        # The write through a bare splice takes no door token, so the guard
        # refuses the member it writes.
        write(root, "include/foundation/Splice.h",
              "class Source {\npublic:\n    static int issued() { return next_; }\nprivate:\n"
              "    static inline int next_ = 0;\n};\n"
              "consteval std::meta::info next_field() { return std::meta::members_of(^^Source, open())[1]; }\n"
              "void forge() { [:next_field():] = 42; }\n")
        expect(root, 1, "REFUSED   include/foundation/Splice.h:5 — a writable private static data member: next_",
               "a private static member written through a splice", True)
        (root / "include/foundation/Splice.h").unlink()
        targets = {
            "include/fixy/Protected.h": "class P {\nprotected:\n    static int count_;\n};\n",
            "include/crucible/MultiLine.h": "struct S {\nprivate:\n    static /* hidden */ int\n        count_;\n};\n",
            "include/foundation/ThreadLocal.h": "class T { static inline thread_local const T* held_ = nullptr; };\n",
            "include/foundation/Reference.h": "class R { static int& ref_; };\n",
            "include/foundation/FunctionPointer.h": "class F { static inline void (*hook_)() = nullptr; };\n",
            "include/foundation/Variable.h": "class V { template <class X> static inline X slot_{}; };\n",
            "include/foundation/Array.h": "class A { static int* slots_[4]; };\n",
            "include/foundation/Nested.h": "namespace n { struct Outer { class Inner { static int n_; }; }; }\n",
            "include/foundation/Conditional.h": "class C {\n#if A\npublic:\n#endif\n    static int n_;\n};\n",
            "include/foundation/Constinit.h": "class K { constinit static int n_; };\n",
        }
        for rel, text in targets.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"an open static member: {rel}", True)
            (root / rel).unlink()
        write(root, "include/foundation/Broken.h", "class B { static int = ; }}}\n")
        expect(root, 1, "PARSE     include/foundation/Broken.h", "a file the parser cannot read", True)
        (root / "include/foundation/Broken.h").unlink()

        # The expanded run reads what only the preprocessor shows: a door
        # that ## builds, and a static member that a macro declares, with the
        # access of the class where the macro expands.
        compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
        if compiler is None:
            failures.append("no C++ compiler to run the expanded pass")
        else:
            database = root / "build" / "compile_commands.json"
            write(root, "include/foundation/Counter.h",
                  "#define COUNTER static int counter_;\nstruct Open { COUNTER };\nclass Shut { COUNTER };\n")
            write(root, "src/Glue.cpp",
                  '#include "include/foundation/Counter.h"\n#define GLUE(a, b) a##b\n'
                  "auto c = std::meta::access_context::GLUE(unch, ecked)();\n")
            write(root, "build/compile_commands.json", json.dumps([{
                "directory": str(root), "file": "src/Glue.cpp",
                "command": f"{compiler} -std=c++20 -I. -c src/Glue.cpp -o Glue.o"}]))
            expect(root, 0, "1 file(s) open", "the run without the database cannot see a pasted door or a macro "
                                              "member")
            for attempt in ("cold", "cached"):
                expect(root, 1, "REFUSED   src/Glue.cpp:3 — a use of unchecked (in a macro expansion)",
                       f"the {attempt} expanded run refuses a door that ## builds", True, database)
                expect(root, 1, "REFUSED   include/foundation/Counter.h:3 — a writable private static data member: "
                                "counter_", f"the {attempt} expanded run refuses a macro member in a class",
                       True, database, absent="Counter.h:2")
            if not any((root / "build" / "unchecked-access-cache").glob("*.json")):
                failures.append("the expanded pass wrote no cache entry")
            write(root, "src/Glue.cpp", '#include "include/foundation/Counter.h"\n#include "missing.h"\n')
            expect(root, 1, "PREPROC", "a unit that the preprocessor rejects fails the run", True, database)
            (root / "src/Glue.cpp").unlink()
            (root / "include/foundation/Counter.h").unlink()
            shutil.rmtree(root / "build")

        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n"
                               "src/Clean.cpp — opens nothing\n")
        expect(root, 1, "STALE     scripts/unchecked-access-allowlist.txt:3: src/Clean.cpp", "a stale row", True)
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\nsrc/Gone.cpp — gone\n")
        expect(root, 1, "STALE     scripts/unchecked-access-allowlist.txt:3: src/Gone.cpp", "a row for a missing file",
               True)
        write(root, ALLOWLIST, "include/foundation/Walk.h\n")
        expect(root, 2, "is not PATH — REASON", "a row with no reason", True)
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n")
        same = captured(root, root) == captured(root, Path("/"))
        print(f"  {'ok  ' if same else 'FAIL'} the report from / equals the report from the repository")
        if not same:
            failures.append("the report depends on the working directory")
        (root / ALLOWLIST).unlink()
        expect(root, 2, "is missing", "a missing allowlist", True)
    for failure in failures:
        print(f"check-no-unchecked-access --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-no-unchecked-access --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, or run the self-test."""
    parser = argparse.ArgumentParser(description="Refuse access_context::unchecked() outside the reviewed walks.")
    parser.add_argument("--self-test", action="store_true", help="run the planted cases")
    parser.add_argument("--compile-db", type=Path, help="also read the macro expansions of this compile database")
    try:
        args = parser.parse_args(argv)
    except SystemExit:
        return 2
    try:
        if args.self_test:
            return self_test()
        if args.compile_db is not None and not args.compile_db.is_file():
            print(f"check-no-unchecked-access: {args.compile_db} does not exist.  Configure the build first.",
                  file=sys.stderr)
            return 2
        return check(REPO_ROOT, args.compile_db)
    except tsast.KitMissing as exc:
        print(f"check-no-unchecked-access: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
