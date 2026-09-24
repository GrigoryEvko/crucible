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

WHAT COUNTS AS THE DOOR
    The guard reads the tokens of each file with its comments and literals
    blanked by scripts/cxx_lex.py, so white space, a line break or a comment
    between two tokens changes nothing, and a mention in a comment or a string
    is not a use.
      1. The name unchecked after ::, . or ->.  Every qualifier counts, so a
         namespace alias, a using-directive, a spliced class and a call
         through an object reach the same member.
      2. A using-declaration or an alias of access_context: a `using` or
         `typedef` statement that names access_context.
      3. A reflection of access_context, of std::meta or of std.  A walk of
         the members of one of them reaches unchecked by reflection, with no
         name to read.
      4. A splice that names a member of an object: `.[:`, `->[:` and the
         member pointer `&[:`.  This is the write itself, so it closes every
         route to the reflection, such as a walk of
         ^^decltype(access_context::current()) that finds unchecked by a
         string compare.

WHAT COUNTS AS AN OPEN TARGET
    A bare splice of a static data member, `[:m:] = v`, has no access check
    either, and no token before the `[:` marks it.  The tree holds many value
    splices, and a lexer cannot tell an enumerator from a static data member.
    So the guard refuses the target, not the spelling:
      5. A writable static data member that is private or protected, in a
         class in include/foundation, include/fixy or include/crucible, less
         the frozen prefixes in scripts/frozen-paths.txt.  Writable means that
         the member itself is not const and not constexpr: a pointer to const
         is writable, a const pointer is not, and a reference is writable when
         its referent is.  The guard reads the declarations from the parse of
         scripts/tsast.py, with the access of each #if arm joined, so an
         access label inside one arm does not open the members after the
         block.  A macro body is raw text to the parser, so the guard reads a
         static declaration in a macro body from its tokens, and refuses one
         that is not constexpr and not const without a pointer or a
         reference.  No allowlist admits a member: make it const or
         constexpr, or move it into the object.
    A file in these roots that the parser cannot read fails the guard, unless
    scripts/tsast.py lists it as not C++, and then the token rule reads it.

REVIEW RULE FOR THE ALLOWED FILES
    An allowed file may not return or publish a reflection of a nonstatic
    data member to its caller.  A public constexpr verdict whose field names
    a private member reopens the door, because `obj.[:verdict.field:]` needs
    no unchecked().  The guard does not check this rule.  The reviewer of each
    row does.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A macro that builds the name from pieces with ##.
    - A reflection of an access context that the guard does not name, such
      as parent_of of a std::meta type, or ^^T in a template that deduces T
      from a call to access_context::current().  Such a file walks private
      members with no door token.  Rules 4 and 5 still refuse the write to a
      nonstatic member and the target of a static one.
    - A const static member whose class type holds a mutable member, and the
      object that a const pointer member points to.
    - A static member that a macro declares when the macro is defined outside
      the three roots, and a member of the frozen tree, which is deleted, not
      edited.

A stale row, one whose file does not exist or does not open the door, fails
the guard, so the allowlist only shrinks.

Exit 0 clean, 1 on a door outside the allowlist, a stale row, an open target
or a file the parser cannot read, 2 on a usage error, a bad allowlist or a
failed self-test, 3 when the parser kit is missing.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = "scripts/unchecked-access-allowlist.txt"
FROZEN_PATHS = "scripts/frozen-paths.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
MEMBER_ROOTS = ("include/foundation/", "include/fixy/", "include/crucible/")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp", ".tpp"})
PUNCTUATION = re.compile(r"\^\^|::|->|\S")
STATEMENT_END = frozenset({";", "{", "}"})
# Every door names unchecked or access_context, reflects std, or splices.  A
# backslash line splice can split a word, so a text with one is always read.
DOOR_WORDS = re.compile(r"unchecked|access_context|\^\^|\[\s*:|\\\r?\n")
# Only a text that says static can declare a static member, so only such a
# file of the member roots is parsed.
STATIC_WORD = re.compile(r"\bstatic\b|\\\r?\n")
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
MACROS = ("preproc_def", "preproc_function_def")
CONDITIONALS = frozenset({"preproc_if", "preproc_ifdef", "preproc_elif", "preproc_elifdef"})
DECLARATIONS = frozenset({"field_declaration", "declaration", "template_declaration"})
NAMES = frozenset({"field_identifier", "identifier", "qualified_identifier", "operator_name", "destructor_name",
                   "template_function"})
# Declarators that change neither the object that a name declares nor its
# constness: an array has the constness of its element.
TRANSPARENT = frozenset({"init_declarator", "parenthesized_declarator", "array_declarator", "attributed_declarator"})


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
    return sorted(p for p in paths if Path(p).suffix in SUFFIXES and (root / p).is_file())


def tokens(text: str) -> list[tuple[str, int]]:
    """Return the tokens of a text with its comments and literals blanked, each with its line.

    Complexity: linear in the length of the text.
    """
    joined, joins = cxx_lex.splice(text)
    blanked, _ = cxx_lex.blank(joined, blank_literals=True)
    return [(m.group(), cxx_lex.line_of(blanked, joins, m.start()))
            for m in re.finditer(r"[A-Za-z_]\w*|" + PUNCTUATION.pattern, blanked)]


def doors(text: str) -> list[tuple[int, str]]:
    """Return (line, shape) for each way the text opens the unchecked access context.

    A text that holds none of the words a door needs is not tokenized, which
    keeps the scan of the whole tree fast.
    """
    if not DOOR_WORDS.search(text):
        return []
    found: list[tuple[int, str]] = []
    toks = tokens(text)
    for i, (token, line) in enumerate(toks):
        before = toks[i - 1][0] if i > 0 else ""
        if token == "unchecked" and before in ("::", ".", "->"):
            found.append((line, "a use of unchecked"))
        after = [t for t, _ in toks[i + 1:i + 3]]
        if token in (".", "->", "&") and after == ["[", ":"]:
            found.append((line, "a splice that names a member of an object"))
        if token == "^^":
            name = []
            k = i + 1
            while k < len(toks) and (re.fullmatch(r"[A-Za-z_]\w*", toks[k][0]) or toks[k][0] == "::"):
                name.append(toks[k][0])
                k += 1
            spelled = "".join(name).lstrip(":")
            if spelled in ("std", "std::meta") or spelled.endswith("access_context"):
                found.append((line, f"a reflection of {spelled}"))
    start = 0
    for i, (token, _) in enumerate(toks + [(";", 0)]):
        if token not in STATEMENT_END:
            continue
        statement = [t for t, _ in toks[start:i]]
        if ("using" in statement or "typedef" in statement) and "access_context" in statement \
                and "namespace" not in statement:
            found.append((toks[start][1], "a using-declaration or an alias of access_context"))
        start = i + 1
    return found


def spelled(text: str) -> str:
    """Return a piece of source as its tokens alone: comments and white space removed."""
    return re.sub(r"\s+", "", cxx_lex.blank(text)[0])


def is_const_qualified(node: tsast.Node) -> bool:
    """Return whether a pointer declarator carries const on the pointer itself."""
    return any(child.type == "type_qualifier" and spelled(child.text) == "const" for child in node.children)


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
            return spelled(declarator.text), True
        node = inner
    name = spelled(node.text)
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


def open_members(declaration: tsast.Node) -> list[tuple[int, str]]:
    """Return (line, name) for each writable static data member that one member declaration declares."""
    while declaration.type == "template_declaration":
        inner = [c for c in declaration.children if c.type in DECLARATIONS]
        if not inner:
            return []
        declaration = inner[-1]
    words = {spelled(c.text) for c in declaration.children
             if c.type in ("storage_class_specifier", "type_qualifier")}
    if "static" not in words or "constexpr" in words:
        return []
    found = []
    for child in declaration.children:
        if child.field != "declarator":
            continue
        verdict = declarator_verdict(child, "const" in words)
        if verdict is not None and verdict[1]:
            found.append((child.line, verdict[0]))
    return found


def walk_members(nodes: list[tsast.Node], accesses: frozenset[str],
                 found: list[tuple[int, str, str]]) -> frozenset[str]:
    """Walk the members of one class body in order, and return the accesses that hold after them.

    An #if block joins the access at the end of each arm, and the access
    before it when no arm must be taken, so a label inside one arm does not
    open the members after the block.
    """
    for node in nodes:
        if node.type == "access_specifier":
            accesses = frozenset({spelled(node.text).rstrip(":")})
        elif node.type in CONDITIONALS:
            accesses = walk_conditional(node, accesses, found)
        elif node.type in DECLARATIONS and accesses - {"public"}:
            access = "private" if "private" in accesses else "protected"
            found += [(line, access, name) for line, name in open_members(node)]
    return accesses


def walk_conditional(node: tsast.Node, accesses: frozenset[str],
                     found: list[tuple[int, str, str]]) -> frozenset[str]:
    """Walk one #if, #ifdef or #elif block, and return the join of the accesses at the end of its arms."""
    body = [c for c in node.children if c.field not in ("condition", "name", "alternative")]
    after = walk_members(body, accesses, found)
    alternative = node.child_by_field("alternative")
    if alternative is None:
        return after | accesses
    if alternative.type in CONDITIONALS:
        return after | walk_conditional(alternative, accesses, found)
    return after | walk_members(alternative.children, accesses, found)


def macro_members(text: str, first_line: int) -> list[tuple[int, str]]:
    """Return (line, declaration) for each static declaration in a macro body that may be writable.

    The parse keeps a macro body as raw text, so the tokens decide.  A
    declaration passes only when it is constexpr, or const with no pointer
    and no reference.  A declaration whose name is followed by a parenthesis
    that does not open a pointer or a reference declares a function.

    Complexity: linear in the length of the text.
    """
    found = []
    toks = tokens(text)
    start = 0
    for i, (token, _) in enumerate(toks + [(";", 0)]):
        if token not in STATEMENT_END:
            continue
        statement = toks[start:i]
        start = i + 1
        words = [t for t, _ in statement]
        if "static" not in words or "constexpr" in words:
            continue
        head = words[words.index("static"):]
        if "=" in head:
            head = head[:head.index("=")]
        if "(" in head:
            opening = head.index("(")
            if opening + 1 >= len(head) or head[opening + 1] not in ("*", "&", "&&"):
                continue
        if "const" in head and "*" not in head and "&" not in head and "&&" not in head:
            continue
        line = next(line for t, line in statement if t == "static")
        found.append((first_line + line - 1, " ".join(head)))
    return found


def frozen_prefixes(root: Path) -> tuple[str, ...]:
    """Return the path prefixes of the frozen tree, or none when the list is absent."""
    listing = root / FROZEN_PATHS
    if not listing.is_file():
        return ()
    return tuple(line.strip() for line in listing.read_text().splitlines()
                 if line.strip() and not line.lstrip().startswith("#"))


def open_targets(root: Path, files: list[str]) -> tuple[list[tuple[str, int, str, str]], list[str], int]:
    """Return each writable private or protected static data member, each parse failure, and the files read.

    Complexity: one lexical pass over each file of the member roots, and one
    parse of each file that says static.
    """
    frozen = frozen_prefixes(root)
    hinted = []
    for rel in files:
        if not rel.startswith(MEMBER_ROOTS) or rel.startswith(frozen):
            continue
        if STATIC_WORD.search(cxx_lex.blank((root / rel).read_text(errors="replace"))[0]):
            hinted.append(rel)
    found: list[tuple[str, int, str, str]] = []
    problems: list[str] = []
    for tree in tsast.parse([root / rel for rel in hinted], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its static "
                                f"data members: {tree.diagnostic}")
                continue
            found += [(rel, line, "unparsed", text)
                      for line, text in macro_members((root / rel).read_text(errors="replace"), 1)]
            continue
        members: list[tuple[int, str, str]] = []
        for node in tree.find(*CLASSES):
            body = node.child_by_field("body")
            if body is not None:
                default = "private" if node.type == "class_specifier" else "public"
                walk_members(body.children, frozenset({default}), members)
        found += [(rel, line, access, name) for line, access, name in members]
        for node in tree.find(*MACROS):
            value = node.child_by_field("value")
            if value is not None:
                found += [(rel, line, "macro", text) for line, text in macro_members(value.text, value.line)]
    return sorted(found), problems, len(hinted)


def check(root: Path) -> int:
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
    opened: set[str] = set()
    problems: list[str] = []
    files = scope_files(root)
    targets, problems, parsed = open_targets(root, files)
    for rel, line, access, name in targets:
        where = {"macro": "a static declaration in a macro body, which may be writable",
                 "unparsed": "a static declaration in a file the parser cannot read, which may be writable"}
        shape = where.get(access, f"a writable {access} static data member")
        problems.append(f"REFUSED   {rel}:{line} — {shape}: {name}.  A splice of a static data member has no access "
                        f"check, so any file writes it.  Make it const or constexpr, or move it into the object.")
    for rel in files:
        found = doors((root / rel).read_text(errors="replace"))
        if found:
            opened.add(rel)
        if found and rel not in admitted:
            problems += [f"REFUSED   {rel}:{line} — {shape}.  A reflection of a private member lets a splice "
                         f"write a proof's private state.  Walk with access_context::current(), or add a "
                         f"reviewed row to {ALLOWLIST}." for line, shape in found]
    for number, path in rows:
        if path not in opened:
            problems.append(f"STALE     {ALLOWLIST}:{number}: {path} — does not open the door.  Remove the row.")
    for line in problems:
        print(f"check-no-unchecked-access: {line}", file=sys.stderr)
    if not problems:
        print(f"check-no-unchecked-access: {len(opened)} file(s) open the unchecked access context, "
              f"each on its reviewed row, and {parsed} parsed file(s) of the member roots hold no writable "
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

    def captured(root: Path, cwd: Path | None = None) -> tuple[int, str]:
        """Run the check on the planted repository and keep its report."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = check(root)
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def expect(root: Path, code: int, needle: str, name: str, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        got, report = captured(root)
        ok = got == code and needle in report
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(f"{name}: expected exit {code} and '{needle}', got exit {got}:\n{report}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
        write(root, "include/foundation/Walk.h",
              "auto m = std::meta::members_of(^^T, std::meta::access_context::unchecked());\n")
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n")
        write(root, "src/Clean.cpp",
              "// std::meta::access_context::unchecked() is refused outside the allowlist.\n"
              'const char* s = "access_context::unchecked()";\n'
              "auto c = std::meta::access_context::current();\nbool unchecked = true;\nint n = unchecked;\n")
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
        expect(root, 0, "1 file(s) open", "a listed walk, a comment, a string, a plain name, a closed member, "
                                          "a frozen file and a file outside the member roots pass")

        forgeries = {
            "src/Qualified.cpp": "auto c = std::meta::access_context::unchecked();\n",
            "src/Alias.cpp": "namespace mm = std::meta;\nauto c = mm::access_context::unchecked();\n",
            "src/UsingDeclaration.cpp": "using std::meta::access_context;\nauto c = 1;\n",
            "src/TypeAlias.cpp": "using Door = std::meta::access_context;\n",
            "src/Typedef.cpp": "typedef std::meta::access_context Door;\n",
            "src/MultiLine.cpp": "auto c = std::meta::access_context\n    ::  /* door */\n    unchecked();\n",
            "src/Spliced.cpp": "auto c = [: ^^std::meta::access_context :]::unchecked();\n",
            "src/Member.cpp": "auto c = std::meta::access_context::current().unchecked();\n",
            "src/Address.cpp": "auto f = &std::meta::access_context::unchecked;\n",
            "src/ReflectClass.cpp": "constexpr auto r = ^^std::meta::access_context;\n",
            "src/ReflectNamespace.cpp": "constexpr auto r = ^^std::meta;\n",
            "src/Macro.cpp": "#define DOOR std::meta::access_context::unchecked()\n",
            "src/DecltypeWalk.cpp": "auto m = std::meta::members_of(^^decltype(std::meta::access_context::current()),"
                                    " std::meta::access_context::current());\nsealed.[:field:] = 42;\n",
            "src/TypeOfWalk.cpp": "constexpr auto ctx = std::meta::access_context::current();\n"
                                  "auto t = std::meta::type_of(^^ctx);\nsealed->[:field:] = 42;\n",
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
            "include/foundation/Macro.h": "#define DECLARE_COUNTER static int counter_;\nclass M { DECLARE_COUNTER };\n",
            "include/foundation/MacroPointer.h": "#define DECLARE_SLOT static const int* slot_;\n",
            "include/crucible/perf/bpf/vmlinux.h": "struct s { int operator; };\nstatic int counter;\n",
        }
        for rel, text in targets.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"an open static member: {rel}", True)
            (root / rel).unlink()
        write(root, "include/foundation/Broken.h", "class B { static int = ; }}}\n")
        expect(root, 1, "PARSE     include/foundation/Broken.h", "a member-root file the parser cannot read", True)
        (root / "include/foundation/Broken.h").unlink()

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
    if argv not in ([], ["--self-test"]):
        print("usage: check-no-unchecked-access.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-no-unchecked-access: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
