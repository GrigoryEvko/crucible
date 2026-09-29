#!/usr/bin/env python3
"""check-federation-admission — FederationAdmission is defined in one file, and nothing else defines a part of it.

foundation/permissions/Permission.h declares the class template
FederationAdmission and the key federation_admission_key, whose one friend
is that template.  The key builds a federation peer token and no other
token.  fixy/Federation.h defines the admission, and that definition is the
whole of the federation door: it builds the key only after a handshake
verifies.

C++ lets any file give the template a second definition that the key also
befriends:

  - an explicit or a partial specialization of the class;
  - a definition of the primary class in a file that does not include
    fixy/Federation.h;
  - an explicit specialization of one member for a new template argument,
    through the class name, through an alias of it, or through a decltype
    of an admission object.

A reflection check cannot refuse these.  GCC gives the location of the
primary template for an instance of a partial specialization, and a #line
directive moves any location.  This guard reads the source instead.

THE RULE
    Outside include/fixy/Federation.h, the guard refuses:

      1. a class head that names FederationAdmission with a body, with
         template arguments, or with FederationAdmission in its scope;
      2. a declaration whose declarator is a qualified name that has
         FederationAdmission in its scope, or whose last name is
         FederationAdmission or a member name of the class.

    A member explicit specialization must spell the member name, whatever
    names the class, so rule 2 refuses one made through an alias or a
    decltype too.  The member names come from the class body in
    include/fixy/Federation.h, so a member added there is guarded at once.
    A non-static data member is not a member name here: the language does
    not let a file define one outside the class.  The implicit special
    members are not names either: the language does not let a file
    specialize one.  The class may declare no operator, no conversion and
    no destructor, because the last name of such a definition does not tell
    the admission apart from another class, and the guard refuses the
    class when it does.

    A forward declaration of the primary template, a friend declaration and
    every use of the class and of its members stay legal everywhere.

WHAT READS THE SITES
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py), over
    each C++ file that git tracks and that names the class, names a member
    name, or holds a line splice, which can split a name.  An untracked file
    is out of scope, because the export of a guard run under the tree holds
    a copy of include/fixy/Federation.h at a different path.  A name is read
    from its tokens, so a comment or a splice inside it hides nothing.  A
    file that the parser cannot read fails.

    The kit keeps a macro body as raw text, so the guard parses each body
    on its own, through tsast.macro_bodies, which joins a line splice first.
    A body that does not parse is read from its preprocessing tokens: a
    class key with its attribute groups and a name that ends in
    FederationAdmission before `<`, `{`, `:` or `final`, and a
    `template <...>` that a qualified member name follows before `;`, `{`
    or `}`.

    With --compile-db, the guard also reads the macro-expanded text of each
    file of the compile database, from the store of utils/scripts/preprocessed.py.
    The expanded text shows a definition that a macro of another file
    spells, and a name that a macro forms by token pasting.  The guard
    parses each expanded text that names the class or a member name, and
    reports a site at the line of the macro use.  A unit that the
    preprocessor rejects, and an expanded text that the parser cannot read,
    fail, because the guard cannot prove them clean.

WHAT IT CANNOT SEE
    Without --compile-db, a name that a macro forms by token pasting, and a
    macro of another file that spells a qualified definition.  With it, a
    header that no unit of the compile database includes, which the tree
    pass still reads.

Usage
    check-federation-admission.py [--compile-db PATH]  scan the tree
    check-federation-admission.py --self-test           plant each forgery and each legal use

Exit 0 clean, 1 on a forged definition, a class the guard cannot read, a
file the parser cannot read or a unit the preprocessor rejects, 2 on a bad
invocation or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import preprocessed  # noqa: E402  (the path insert above has to come first)
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

CLASS = "FederationAdmission"
AUTHORED = "include/fixy/Federation.h"
CLASS_HEADS = ("class_specifier", "struct_specifier", "union_specifier")
CLASS_KEYS = frozenset({"class", "struct", "union"})
# Each declarator node that wraps the declarator id in its `declarator` field.
WRAPPERS = frozenset({"function_declarator", "pointer_declarator", "reference_declarator", "array_declarator",
                      "init_declarator", "parenthesized_declarator", "attributed_declarator"})
DECLARATIONS = ("function_definition", "declaration", "field_declaration")
SPLICES = (b"\\\n", b"\\\r\n")
# The trees whose macro bodies go to the kit together.  A chunk bounds the
# memory that the file trees of one parse of macro bodies hold.
MACRO_CHUNK = 256


class Unreadable(Exception):
    """The class body in the authored file is missing, or it declares a member the guard cannot name."""


@dataclass(frozen=True)
class Site:
    """One forged definition."""

    path: str
    line: int
    text: str
    reason: str


def name_parts(node: tsast.Node) -> tuple[str, ...]:
    """Return the names of a declarator id or of a class head name, outermost first, without template arguments.

    A name whose scope the kit cannot read, such as a decltype, gives its
    last name alone.  The kit joins a line splice inside a name.
    """
    written = tsast.qualified_parts(node)
    if written is None:
        leaf = tsast.leaf_name(node)
        return (leaf,) if leaf else ()
    return written[1]


def declarator_id(node: tsast.Node) -> tsast.Node | None:
    """Return the declarator id under a declarator, through every wrapper."""
    while node.type in WRAPPERS:
        inner = node.child_by_field("declarator")
        if inner is None:
            return None
        node = inner
    return node


def name_leaf(node: tsast.Node) -> tsast.Node:
    """Return the last name node of a qualified name, where template arguments show."""
    while node.type == "qualified_identifier" and node.child_by_field("name") is not None:
        node = node.child_by_field("name")
    return node


def admission_body(tree: tsast.Tree) -> tsast.Node:
    """Return the class body of the one definition of the admission in the authored file.

    Raises:
        Unreadable: If the file holds no definition, or more than one
    """
    bodies = []
    for head in tree.find(*CLASS_HEADS):
        named = head.child_by_field("name")
        body = head.child_by_field("body")
        if named is not None and body is not None and name_parts(named)[-1:] == (CLASS,) \
                and named.type != "template_type":
            bodies.append(body)
    if len(bodies) != 1:
        raise Unreadable(f"{AUTHORED} holds {len(bodies)} definitions of {CLASS}; it must hold one")
    return bodies[0]


def member_names(body: tsast.Node) -> set[str]:
    """Return each member name of the class body that a file could define outside it.

    Raises:
        Unreadable: If the body declares an operator, a conversion, a destructor or a shape the guard does not know
    """
    names: set[str] = set()
    pending = list(body.children)
    while pending:
        node = pending.pop()
        if node.type in ("comment", "access_specifier", "static_assert_declaration", "friend_declaration", ";",
                         "{", "}", ":"):
            continue
        if node.type == "template_declaration":
            pending += [child for child in node.children if child.type != "template_parameter_list"
                        and child.type != "requires_clause"]
            continue
        if node.type in CLASS_HEADS or node.type in ("alias_declaration", "type_definition", "enum_specifier"):
            named = node.child_by_field("name")
            if named is not None:
                names.update(name_parts(named)[-1:])
            continue
        if node.type not in DECLARATIONS:
            raise Unreadable(f"the class body of {CLASS} holds a {node.type} at line {node.line}, which the guard "
                             f"does not know how to name")
        is_static = any(child.type == "storage_class_specifier" and child.text == "static"
                        for child in node.children)
        for declarator in (child for child in node.children if child.field == "declarator"):
            is_function = declarator.type == "function_declarator" or any(
                True for _ in declarator.descendants("function_declarator"))
            if node.type == "function_definition":
                is_function = True
            ident = declarator_id(declarator)
            if ident is None:
                raise Unreadable(f"a member of {CLASS} at line {node.line} has no declarator id")
            if ident.type in ("operator_name", "operator_cast", "destructor_name"):
                raise Unreadable(f"{CLASS} declares {ident.text!r} at line {node.line}.  The last name of an "
                                 f"out-of-class definition of it does not tell the admission apart from another "
                                 f"class, so the guard cannot refuse a forged one.  Remove the member")
            if is_function or is_static:
                names.update(name_parts(ident)[-1:])
    # A constructor is named after the class, which the rules name apart.
    names.discard(CLASS)
    return names


def class_head_sites(root: tsast.Node, path: str, members: set[str], row_of) -> list[Site]:
    """Return each class head under a node that defines or specializes a part of the admission."""
    sites = []
    for head in root.descendants(*CLASS_HEADS):
        named = head.child_by_field("name")
        if named is None:
            continue
        parts = name_parts(named)
        if not parts:
            continue
        last, scopes = parts[-1], parts[:-1]
        reason = None
        if last == CLASS and head.child_by_field("body") is not None:
            reason = f"a definition of {CLASS}"
        elif last == CLASS and name_leaf(named).type == "template_type":
            reason = f"a specialization of {CLASS}"
        elif CLASS in scopes:
            reason = f"a class inside {CLASS}"
        elif scopes and last in members:
            reason = f"a qualified class head named after the member {last} of {CLASS}"
        if reason is not None:
            sites.append(Site(path, row_of(head) + 1, tsast.excerpt(head), reason))
    return sites


def declarator_sites(root: tsast.Node, path: str, members: set[str], row_of) -> list[Site]:
    """Return each declaration under a node whose qualified declarator names a part of the admission."""
    sites = []
    refused = members | {CLASS, "~" + CLASS}
    for declaration in root.descendants(*DECLARATIONS):
        for declarator in (child for child in declaration.children if child.field == "declarator"):
            ident = declarator_id(declarator)
            if ident is None or ident.type != "qualified_identifier":
                continue
            parts = name_parts(ident)
            if not parts:
                continue
            if CLASS in parts[:-1]:
                reason = f"a definition of a member of {CLASS}"
            elif parts[-1] in refused:
                reason = f"a qualified definition named after the member {parts[-1]} of {CLASS}"
            else:
                continue
            sites.append(Site(path, row_of(declaration) + 1, tsast.excerpt(declaration), reason))
    return sites


def attribute_end(tokens: list[tsast.Token], index: int) -> int:
    """Return the index after one attribute group that starts at a token, or the same index when none starts there.

    An attribute group is `[[...]]`, `alignas(...)`, `__attribute__((...))` or
    `__declspec(...)`, with its brackets balanced.
    """
    if index + 1 < len(tokens) and tokens[index].text == "[" and tokens[index + 1].text == "[":
        opening, closing = "[", "]"
    elif index + 1 < len(tokens) and tokens[index].text in ("alignas", "__attribute__", "__declspec") \
            and tokens[index + 1].text == "(":
        opening, closing = "(", ")"
        index += 1
    else:
        return index
    depth = 0
    while index < len(tokens):
        depth += {opening: 1, closing: -1}.get(tokens[index].text, 0)
        index += 1
        if depth == 0:
            return index
    return index


def qualified_name_at(tokens: list[tsast.Token], index: int) -> tuple[tuple[str, ...], int]:
    """Return (parts, index after the name) for the qualified name that starts at a token."""
    index += index < len(tokens) and tokens[index].text == "::"
    parts: list[str] = []
    while index < len(tokens) and tokens[index].kind == "identifier":
        parts.append(tokens[index].text)
        if index + 2 < len(tokens) and tokens[index + 1].text == "::" and tokens[index + 2].kind == "identifier":
            index += 2
            continue
        index += 1
        break
    return tuple(parts), index


def token_sites(tokens: list[tsast.Token], path: str, members: set[str], text: str) -> list[Site]:
    """Return the sites that the preprocessing tokens of a macro body spell, for a body the kit cannot parse."""
    sites = []
    refused = members | {CLASS}
    for index, token in enumerate(tokens):
        if token.text in CLASS_KEYS:
            after = index + 1
            while (skipped := attribute_end(tokens, after)) != after:
                after = skipped
            parts, after = qualified_name_at(tokens, after)
            if parts and after < len(tokens) and (parts[-1] == CLASS and tokens[after].text in ("<", "{", ":",
                                                                                                "final")
                                                  or CLASS in parts[:-1]):
                sites.append(Site(path, token.row + 1, text, f"a definition or a specialization of {CLASS}"))
        elif token.text == "template" and index + 1 < len(tokens) and tokens[index + 1].text == "<":
            for later in range(index + 2, len(tokens)):
                if tokens[later].text in (";", "{", "}"):
                    break
                name = later + 1 + (later + 1 < len(tokens) and tokens[later + 1].text == "~")
                if tokens[later].text == "::" and name < len(tokens) and tokens[name].text in refused:
                    sites.append(Site(path, token.row + 1, text, f"a qualified definition of a member of {CLASS}"))
                    break
    return sites


def macro_sites(root: Path, trees: list[tsast.Tree], members: set[str]) -> list[Site]:
    """Return the sites of the macro bodies of some trees.

    Complexity: one parse of every macro body of the trees, in at most three
    kit runs, plus a token pass over each body.
    """
    sites: list[Site] = []
    for body in tsast.macro_bodies(trees):
        rel = Path(body.define.tree.path).relative_to(root).as_posix()
        if body.is_parsed:
            row_of = lambda node, body=body: body.origin(node)[0]  # noqa: E731
            sites += class_head_sites(body.root, rel, members, row_of)
            sites += declarator_sites(body.root, rel, members, row_of)
        else:
            sites += token_sites(tsast.pp_tokens(body.text, body.first_row), rel, members,
                                 tsast.excerpt(body.define))
    return sites


def expanded_sites(root: Path, compile_db: Path, members: set[str], problems: list[str]) -> list[Site]:
    """Return the sites of the macro-expanded text of each file of a compile database.

    Each distinct expansion of a file is read one time.  A site is reported
    at the file line that the preprocessor gives for its text, which is the
    line of the macro use.

    Complexity: one preprocessed pass over the database, which the store
    shares with the other guards, plus one parse of each expanded text that
    names the class or a member name.
    """
    store = preprocessed.Store(compile_db, root)
    needles = tuple(sorted(members | {CLASS}))
    pending: list[tuple[str, str, list[tuple[int, int]]]] = []
    seen: set[tuple[str, str]] = set()
    for unit in store.units():
        if unit.failure is not None:
            problems.append(f"federation_admission: {unit.file} does not preprocess, so what its macros define is "
                            f"unknown.\n  {unit.failure}")
            continue
        for path, (key, chunks) in preprocessed.files_of(unit).items():
            if path == AUTHORED or (path, key) in seen:
                continue
            seen.add((path, key))
            texts = [store.text(chunk.digest) for chunk in chunks]
            if not any(needle in text for text in texts for needle in needles):
                continue
            starts: list[tuple[int, int]] = []
            row = 0
            for chunk, text in zip(chunks, texts, strict=True):
                starts.append((row, chunk.line))
                row += text.count("\n") + 1
            pending.append((path, "\n".join(texts), starts))
    sites: list[Site] = []
    trees = tsast.parse_texts([(path, text) for path, text, _starts in pending])
    for (path, _text, starts), tree in zip(pending, trees, strict=True):
        if tree.diagnostic is not None:
            problems.append(f"federation_admission: the macro expansion of {path} does not parse, so what it "
                            f"defines is unknown.\n  {tree.diagnostic}")
            continue

        def row_of(node: tsast.Node, starts: list[tuple[int, int]] = starts) -> int:
            """Map a row of the expanded text to the zero-based row of the file line it came from."""
            begin, line = max((start for start in starts if start[0] <= node.start[0]), default=(0, 1))
            return line - 1 + node.start[0] - begin

        sites += class_head_sites(tree.root, path, members, row_of)
        sites += declarator_sites(tree.root, path, members, row_of)
    return sites


def listed_files(root: Path, needles: tuple[bytes, ...]) -> list[str]:
    """Return the tracked C++ files under the root that hold a needle or a line splice, relative to the root.

    tsast.tracked_files gives the files.  An untracked file is out of scope,
    because the export of a guard run under the tree holds a copy of the
    authored file at a different path.
    """
    found = []
    for path in tsast.tracked_files(root):
        full = root / path
        if tsast.is_in_cpp_scope(path) and full.is_file():
            data = full.read_bytes()
            if any(needle in data for needle in needles + SPLICES):
                found.append(path)
    return found


def scan(root: Path, compile_db: Path | None = None) -> tuple[list[Site], list[str]]:
    """Return each forged site, and each problem that stops the scan.

    Complexity: linear in the size of the files that name the class or a
    member name or hold a splice, plus the preprocessed pass with a compile
    database.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    authored = root / AUTHORED
    if not authored.is_file():
        return [], [f"federation_admission: {AUTHORED} is missing, so the members to guard are unknown"]
    problems: list[str] = []
    members: set[str] = set()
    for tree in tsast.parse([authored], strict=False):
        if tree.diagnostic is not None:
            return [], [f"federation_admission: {AUTHORED} does not parse.\n  {tree.diagnostic}"]
        try:
            members = member_names(admission_body(tree))
        except Unreadable as exc:
            return [], [f"federation_admission: {exc}"]
    needles = tuple(name.encode() for name in sorted(members | {CLASS}))
    sites: list[Site] = []
    chunk: list[tsast.Tree] = []
    files = [path for path in listed_files(root, needles) if path != AUTHORED]
    for tree in tsast.parse([root / path for path in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            problems.append(f"federation_admission: {rel} does not parse, so what it defines is unknown."
                            f"\n  {tree.diagnostic}")
            continue
        sites += class_head_sites(tree.root, rel, members, lambda node: node.start[0])
        sites += declarator_sites(tree.root, rel, members, lambda node: node.start[0])
        if next(tree.find("preproc_def", "preproc_function_def"), None) is not None:
            chunk.append(tree)
        if len(chunk) >= MACRO_CHUNK:
            sites += macro_sites(root, chunk, members)
            chunk = []
    sites += macro_sites(root, chunk, members)
    if compile_db is not None:
        sites += expanded_sites(root, compile_db, members, problems)
    unique = sorted({(site.path, site.line, site.reason): site for site in sites}.values(),
                    key=lambda site: (site.path, site.line, site.reason))
    return unique, problems


def run(root: Path, compile_db: Path | None = None) -> int:
    """Scan, print each finding, and return the exit code."""
    forged, problems = scan(root, compile_db)
    for site in forged:
        print(f"federation_admission: {site.reason} at {site.path}:{site.line}", file=sys.stderr)
        print(f"federation_admission: {site.text}", file=sys.stderr)
    for line in problems:
        print(line, file=sys.stderr)
    if forged or problems:
        print(f"federation_admission: {AUTHORED} alone defines {CLASS}.  A definition of any part of it elsewhere "
              f"gets the friendship of federation_admission_key and can mint a federation peer token with no "
              f"handshake.  Use the class and its members; do not define them.", file=sys.stderr)
        return 1
    print(f"check-federation-admission: clean — {CLASS} is defined only in {AUTHORED}.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each forgery and each legal use, and examine each verdict.

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

    authored = (
        "template <typename Org>\n"
        "class foundation::permissions::FederationAdmission final : Base {\n"
        "public:\n"
        "    template <typename Policy, typename Ctx>\n"
        "    static constexpr FederationAdmission mint_federation_admission(Ctx const&) noexcept { return {}; }\n"
        "    template <typename... Explicit>\n"
        "    constexpr int mint_federation_admittance(H const& h) noexcept { return 0; }\n"
        "private:\n"
        "    constexpr FederationAdmission() noexcept {}\n"
        "    Key admission_key_;\n"
        "};\n")
    planted = {
        "src/planted/explicit.cpp": (
            "template <> class foundation::permissions::FederationAdmission<Evil> { public: int grant(); };\n"),
        "src/planted/partial.cpp": (
            "template <class T> requires true class foundation::permissions::FederationAdmission<T*> {};\n"),
        "src/planted/primary.cpp": (
            "namespace foundation::permissions {\n"
            "template <class Org> class FederationAdmission { public: static int grant(); };\n"
            "}\n"),
        "src/planted/member.cpp": (
            "template <> template <> constexpr int\n"
            "foundation::permissions::FederationAdmission<Evil>::mint_federation_admittance<>(H const&) noexcept"
            " { return 1; }\n"),
        "src/planted/alias.cpp": (
            "using E = foundation::permissions::FederationAdmission<Evil>;\n"
            "template <> template <> constexpr int E::mint_federation_admittance<>(H const&) noexcept { return 1; }\n"
            "template <> E::FederationAdmission() noexcept {}\n"),
        "src/planted/decltype.cpp": (
            "using D = std::remove_cvref_t<decltype(admission)>;\n"
            "template <> template <class P, class C> D D::mint_federation_admission(C const&) noexcept"
            " { return {}; }\n"),
        "src/planted/nested.cpp": "struct E::mint_federation_admission {};\n",
        "src/planted/splice.cpp": (
            "template <> class foundation::permissions::Federation\\\nAdmission<Evil> {};\n"),
        "src/planted/comment.cpp": (
            "template <> class foundation::permissions:: /* c */ FederationAdmission<Evil> {};\n"),
        "src/planted/macro.cpp": (
            "#define FORGE template <> class foundation::permissions::FederationAdmission<Evil> {}\n"
            "#define FORGE_MEMBER template <> template <> int E::mint_federation_admittance<>(H const&)\n"
            "#define MENTION FederationAdmission is named here, and nothing is defined\n"
            "#define FORGE_SPLICE template <> class foundation::permissions:: \\\n"
            "    FederationAdmission<Evil> {}\n"
            "#define FORGE_ALIGNAS class alignas(8) foundation::permissions::FederationAdmission {}\n"
            "#define FORGE_NAME template <> class foundation::permissions::Federation\\\nAdmission<Evil> {}\n"
            "#define FORGE_PASTE(x) template <> class foundation::permissions::FederationAdmission<x##Evil> {}\n"),
    }
    expected = {
        ("src/planted/explicit.cpp", 1),
        ("src/planted/partial.cpp", 1),
        ("src/planted/primary.cpp", 2),
        ("src/planted/member.cpp", 1),
        ("src/planted/alias.cpp", 2),
        ("src/planted/alias.cpp", 3),
        ("src/planted/decltype.cpp", 2),
        ("src/planted/nested.cpp", 1),
        ("src/planted/splice.cpp", 1),
        ("src/planted/comment.cpp", 1),
        ("src/planted/macro.cpp", 1),
        ("src/planted/macro.cpp", 2),
        ("src/planted/macro.cpp", 4),
        ("src/planted/macro.cpp", 6),
        ("src/planted/macro.cpp", 7),
        ("src/planted/macro.cpp", 9),
    }
    legal = {
        "src/legal/use.cpp": (
            "#include <fixy/Federation.h>\n"
            "template <class Org> class foundation::permissions::FederationAdmission;\n"
            "namespace foundation::permissions { template <class Org> class FederationAdmission; }\n"
            "struct Friendly { template <class O> friend class foundation::permissions::FederationAdmission; };\n"
            "void admit(foundation::permissions::FederationAdmission<Org>& admission, H const& h) {\n"
            "    auto again = foundation::permissions::FederationAdmission<Org>::mint_federation_admission<P>(ctx);\n"
            "    auto token = admission.mint_federation_admittance(h);\n"
            "}\n"
            "// template <> class FederationAdmission<Evil> {}; is prose.\n"
            "const char* text = \"template <> class FederationAdmission<Evil> {};\";\n"
            "R& Other::grant() { static R r; return r; }\n"
            "#define LEGAL_USE(x) foundation::permissions::FederationAdmission<x>::mint_federation_admission<P>(c)\n"
            "#define LEGAL_FRIEND template <class O> friend class foundation::permissions::FederationAdmission;\n"),
    }

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-federation-admission --self-test")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in {AUTHORED: authored, **planted, **legal}.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")

        forged, problems = scan(root)
        found = {(site.path, site.line) for site in forged}
        expect("every file parses and the class body is read", not problems)
        for path, line in sorted(expected):
            expect(f"caught: {path}:{line}", (path, line) in found, True)
        expect("nothing else is reported", found == expected, True)
        expect("a use, a declaration, a friend, prose and another class's member are legal",
               not any(site.path.startswith("src/legal/") for site in forged))
        expect("the macro that only names the class is legal", ("src/planted/macro.cpp", 3) not in found)
        expect("the authored file is not scanned", not any(site.path == AUTHORED for site in forged))

        code, report = captured(lambda: run(root))
        expect("a forged tree exits 1", code == 1 and "src/planted/alias.cpp:2" in report, True)

        # A macro of another file, with the class name pasted from its
        # argument, shows only in the expanded text.
        compiler = os.environ.get("CXX") or next(
            (c for c in ("c++", "g++") if any(os.path.isfile(os.path.join(d, c))
                                              for d in os.environ.get("PATH", "").split(os.pathsep))), None)
        if compiler is None:
            expect("a C++ compiler runs the preprocessed pass", False)
        else:
            (root / "src" / "expand").mkdir(parents=True)
            (root / "src" / "expand" / "forge.h").write_text(
                "#pragma once\n#define FORGE_FROM(N) template <> class foundation::permissions::N<Evil> {}\n",
                encoding="utf-8")
            (root / "src" / "expand" / "use.cpp").write_text(
                '#include "forge.h"\nstruct Evil;\nFORGE_FROM(FederationAdmission);\n', encoding="utf-8")
            (root / "src" / "expand" / "quiet.cpp").write_text('#include "forge.h"\nint quiet;\n',
                                                               encoding="utf-8")
            database = root / "build" / "compile_commands.json"
            database.parent.mkdir()
            database.write_text(json.dumps([{"directory": str(root), "file": name,
                                             "command": f"{compiler} -std=c++20 -c {name} -o {name}.o"}
                                            for name in ("src/expand/use.cpp", "src/expand/quiet.cpp")]))
            without = {(site.path, site.line) for site in scan(root)[0]}
            expect("the tree pass cannot see a pasted name from another file's macro",
                   ("src/expand/use.cpp", 3) not in without)
            forged, problems = scan(root, database)
            found = {(site.path, site.line) for site in forged}
            expect("the expanded text shows a pasted name at the line of the macro use",
                   ("src/expand/use.cpp", 3) in found and not problems, True)
            expect("a unit that expands no forgery adds nothing", not any(site.path == "src/expand/quiet.cpp"
                                                                          for site in forged))
            database.write_text(json.dumps([{"directory": str(root), "file": "src/expand/broken.cpp",
                                             "command": f"{compiler} -std=c++20 -c src/expand/broken.cpp -o b.o"}]))
            (root / "src" / "expand" / "broken.cpp").write_text('#include "missing.h"\n', encoding="utf-8")
            code, report = captured(lambda: run(root, database))
            expect("a unit the preprocessor rejects fails", code == 1 and "does not preprocess" in report, True)
            for rel in ("forge.h", "use.cpp", "quiet.cpp", "broken.cpp"):
                (root / "src" / "expand" / rel).unlink()

        (root / AUTHORED).write_text(authored.replace("    Key admission_key_;\n",
                                                      "    Key admission_key_;\n"
                                                      "    bool operator==(const FederationAdmission&) const;\n"),
                                     encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("an operator member makes the class unreadable", code == 1 and "operator==" in report, True)
        (root / AUTHORED).unlink()
        code, report = captured(lambda: run(root))
        expect("a missing authored file fails closed", code == 1 and "is missing" in report, True)
        (root / AUTHORED).write_text(authored, encoding="utf-8")

        (root / "src" / "planted" / "broken.cpp").write_text("void f() { g(1) { } } // FederationAdmission\n",
                                                            encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("a file the parser cannot read fails", code == 1 and "src/planted/broken.cpp does not parse" in report,
               True)
        for rel in [*planted, "src/planted/broken.cpp"]:
            (root / rel).unlink()
        expect("a tree with only legal uses exits 0", captured(lambda: run(root))[0] == 0)

    # In a work tree, an untracked file is out of scope: a guard-run export
    # holds a copy of the authored file at another path.
    with tempfile.TemporaryDirectory() as work:
        root = Path(work).resolve()
        throwaway_repo.init(root)
        (root / AUTHORED).parent.mkdir(parents=True)
        (root / AUTHORED).write_text(authored, encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        copy = root / "grun" / "change" / AUTHORED
        copy.parent.mkdir(parents=True)
        copy.write_text(authored, encoding="utf-8")
        expect("an untracked copy of the authored file is out of scope", captured(lambda: run(root))[0] == 0)
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        code, report = captured(lambda: run(root))
        expect("the same copy fails once git tracks it", code == 1 and f"grun/change/{AUTHORED}:2" in report, True)

    if failures:
        print(f"check-federation-admission --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-federation-admission --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == []:
            return run(tsast.REPO_ROOT)
        if len(argv) == 2 and argv[0] == "--compile-db":
            database = Path(argv[1]).resolve()
            if not database.is_file():
                print(f"check-federation-admission: the compile database {database} does not exist", file=sys.stderr)
                return 2
            return run(tsast.REPO_ROOT, database)
    except tsast.KitMissing as exc:
        print(f"check-federation-admission: {exc}", file=sys.stderr)
        return 3
    print("usage: check-federation-admission.py [--compile-db PATH | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
