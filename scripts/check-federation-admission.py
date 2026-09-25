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
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py), over
    each C and C++ file that git lists and that names the class or a member
    name.  A macro body is one preproc_arg of raw text, and a file in
    tsast.UNPARSEABLE has no tree, so the guard reads those with patterns
    over text with the comments and the literals blanked.  A file that the
    parser cannot read fails.

WHAT IT CANNOT SEE
    A name that a macro forms by token pasting, and a macro of another
    file that spells a qualified definition.  The kit does not expand
    macros.

Usage
    check-federation-admission.py              scan the tree
    check-federation-admission.py --self-test  plant each forgery and each legal use

Exit 0 clean, 1 on a forged definition, a class the guard cannot read or a
file the parser cannot read, 2 on a bad invocation or a failed self-test,
3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

CLASS = "FederationAdmission"
AUTHORED = "include/fixy/Federation.h"
SOURCE_SUFFIXES = frozenset({".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".inl", ".ipp", ".tpp",
                             ".tcc", ".inc", ".ixx", ".cppm"})
EXCLUDED_DIRS = ("build", "cmake-build-", "third_party", "external", "vendor", ".git", ".tools")
CLASS_HEADS = ("class_specifier", "struct_specifier", "union_specifier")
# Each declarator node that wraps the declarator id in its `declarator` field.
WRAPPERS = frozenset({"function_declarator", "pointer_declarator", "reference_declarator", "array_declarator",
                      "init_declarator", "parenthesized_declarator", "attributed_declarator"})
DECLARATIONS = ("function_definition", "declaration", "field_declaration")


class Unreadable(Exception):
    """The class body in the authored file is missing, or it declares a member the guard cannot name."""


@dataclass(frozen=True)
class Site:
    """One forged definition."""

    path: str
    line: int
    text: str
    reason: str


def final_name(node: tsast.Node) -> str:
    """Return the last name of a declarator id or of a class head name, without template arguments."""
    while node.type == "qualified_identifier":
        inner = node.child_by_field("name")
        if inner is None:
            return ""
        node = inner
    if node.type in ("template_type", "template_function", "template_method"):
        named = node.child_by_field("name")
        return named.text if named is not None else ""
    if node.type == "destructor_name":
        return "~" + node.text.lstrip("~").strip()
    return node.text.strip()


def scope_names(node: tsast.Node) -> list[str]:
    """Return the name of each scope of a qualified name, outermost first, without template arguments."""
    names = []
    while node.type == "qualified_identifier":
        scope = node.child_by_field("scope")
        if scope is not None:
            named = scope.child_by_field("name") if scope.type == "template_type" else None
            names.append((named.text if named is not None else scope.text).strip())
        inner = node.child_by_field("name")
        if inner is None:
            break
        node = inner
    return names


def declarator_id(node: tsast.Node) -> tsast.Node | None:
    """Return the declarator id under a declarator, through every wrapper."""
    while node.type in WRAPPERS:
        inner = node.child_by_field("declarator")
        if inner is None:
            return None
        node = inner
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
        if named is not None and body is not None and final_name(named) == CLASS and named.type != "template_type":
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
                names.add(final_name(named))
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
                names.add(final_name(ident))
    # A constructor is named after the class, which the rules name apart.
    names.discard(CLASS)
    return names


def class_head_sites(tree: tsast.Tree, path: str, members: set[str]) -> list[Site]:
    """Return each class head outside the authored file that defines or specializes a part of the admission."""
    sites = []
    for head in tree.find(*CLASS_HEADS):
        named = head.child_by_field("name")
        if named is None:
            continue
        last = final_name(named)
        leaf = named
        while leaf.type == "qualified_identifier" and leaf.child_by_field("name") is not None:
            leaf = leaf.child_by_field("name")
        scopes = scope_names(named)
        reason = None
        if last == CLASS and head.child_by_field("body") is not None:
            reason = f"a definition of {CLASS}"
        elif last == CLASS and leaf.type == "template_type":
            reason = f"a specialization of {CLASS}"
        elif CLASS in scopes:
            reason = f"a class inside {CLASS}"
        elif scopes and last in members:
            reason = f"a qualified class head named after the member {last} of {CLASS}"
        if reason is not None:
            sites.append(Site(path, head.line, head.text.splitlines()[0].strip(), reason))
    return sites


def declarator_sites(tree: tsast.Tree, path: str, members: set[str]) -> list[Site]:
    """Return each declaration outside the authored file whose qualified declarator names a part of the admission."""
    sites = []
    refused = members | {CLASS, "~" + CLASS}
    for declaration in tree.find(*DECLARATIONS):
        for declarator in (child for child in declaration.children if child.field == "declarator"):
            ident = declarator_id(declarator)
            if ident is None or ident.type != "qualified_identifier":
                continue
            last = final_name(ident)
            if CLASS in scope_names(ident):
                reason = f"a definition of a member of {CLASS}"
            elif last in refused:
                reason = f"a qualified definition named after the member {last} of {CLASS}"
            else:
                continue
            sites.append(Site(path, declaration.line, declaration.text.splitlines()[0].strip(), reason))
    return sites


def text_patterns(members: set[str]) -> list[tuple[str, re.Pattern[str]]]:
    """Return the patterns for text with no tree: a macro body or a file that the kit cannot parse."""
    names = "|".join(sorted(re.escape(name) for name in members | {CLASS}))
    return [
        (f"a definition or a specialization of {CLASS}",
         re.compile(rf"\b(?:class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)*(?:::)?(?:\w+::)*{CLASS}\s*(?:<|\{{|:(?!:)|final\b)")),
        (f"a qualified definition of a member of {CLASS}",
         re.compile(rf"\btemplate\s*<[^;{{}}]*>[^;{{}}]*::\s*~?(?:{names})\b")),
    ]


def text_sites(text: str, path: str, first_line: int, patterns: list[tuple[str, re.Pattern[str]]]) -> list[Site]:
    """Return the sites of text that has no tree."""
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    sites = []
    for reason, pattern in patterns:
        for match in pattern.finditer(blanked):
            line = first_line + blanked.count("\n", 0, match.start())
            sites.append(Site(path, line, match.group(0).split("\n")[0].strip(), reason))
    return sites


def listed_files(root: Path, needles: tuple[bytes, ...]) -> list[str]:
    """Return the C and C++ files under the root that hold a needle, relative to the root."""
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             check=True, capture_output=True).stdout.decode(errors="replace")
        candidates = [path for path in out.split("\0") if path]
    except (OSError, subprocess.CalledProcessError):
        candidates = []
        for directory, dirs, names in os.walk(root):
            dirs[:] = sorted(d for d in dirs if not d.startswith(EXCLUDED_DIRS))
            candidates += [str((Path(directory) / name).relative_to(root)) for name in names]
    found = []
    for path in sorted(candidates):
        full = root / path
        if Path(path).suffix in SOURCE_SUFFIXES and full.is_file():
            data = full.read_bytes()
            if any(needle in data for needle in needles):
                found.append(path)
    return found


def scan(root: Path) -> tuple[list[Site], list[str]]:
    """Return each forged site, and each problem that stops the scan.

    Complexity: linear in the size of the files that name the class or a member name.

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
    patterns = text_patterns(members)
    sites: list[Site] = []
    files = [path for path in listed_files(root, needles) if path != AUTHORED]
    for tree in tsast.parse([root / path for path in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel in tsast.UNPARSEABLE:
                sites += text_sites(tree.source.decode("utf-8", "replace"), rel, 1, patterns)
            else:
                problems.append(f"federation_admission: {rel} does not parse, so what it defines is unknown."
                                f"\n  {tree.diagnostic}")
            continue
        sites += class_head_sites(tree, rel, members)
        sites += declarator_sites(tree, rel, members)
        for body in tree.find("preproc_arg"):
            sites += text_sites(body.text, rel, body.line, patterns)
    unique = sorted({(site.path, site.line, site.reason): site for site in sites}.values(),
                    key=lambda site: (site.path, site.line, site.reason))
    return unique, problems


def run(root: Path) -> int:
    """Scan, print each finding, and return the exit code."""
    forged, problems = scan(root)
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
        "src/planted/macro.cpp": (
            "#define FORGE template <> class foundation::permissions::FederationAdmission<Evil> {}\n"
            "#define FORGE_MEMBER template <> template <> int E::mint_federation_admittance<>(H const&)\n"
            "#define MENTION FederationAdmission is named here, and nothing is defined\n"),
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
        ("src/planted/macro.cpp", 1),
        ("src/planted/macro.cpp", 2),
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
            "R& Other::grant() { static R r; return r; }\n"),
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

    if failures:
        print(f"check-federation-admission --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-federation-admission --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == []:
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-federation-admission: {exc}", file=sys.stderr)
        return 3
    print("usage: check-federation-admission.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
