#!/usr/bin/env python3
"""check-syscall-grant-coverage — derive the hardening grant set from the call sites, on the parse tree.

include/crucible/warden/Hardening.h publishes hardening_syscall_atoms, a
type-level tuple of fixy/atoms/Syscall.h atoms that names every privileged
system call the apply path issues.  The tuple is the auditable claim about
what this code does to the kernel.  A tuple_size static_assert and a test
compare the list with a second copy of the same list, so neither reads the
call sites.  This guard does: it derives the set of system calls that the
header issues and fails when that set and the tuple disagree in either
direction.  A smaller tuple understates the claim, and a larger one
overstates it.

WHAT COUNTS AS ISSUING A SYSTEM CALL
    The guard reads the parse tree of the pinned tree-sitter kit.  A system
    call is a name that the SyscallId enum of include/fixy/atoms/Syscall.h
    declares, so the catalog is the one list of names.
      * A reference to that name, bare or qualified from `::`: a call, an
        address and a function pointer all count.  A name qualified by
        anything else, such as SyscallId::mlock, is not a call.
      * A call of a helper named `<name>_sys`, which stands for the system
        call it wraps.
      * A `syscall(SYS_<name>, ...)` outside a `_sys` helper.  Inside a
        helper, the SYS_ number must name the helper's own system call, or
        the helper is a violation.
      * The same shapes in a macro body.  The body is parsed on its own
        (tsast.macro_bodies), with every fragment joined, so a block comment
        inside it cannot cut a member call away from its object.  A body
        that the parser cannot read is read from its preprocessing tokens:
        a name that `.`, `->` or `X::` does not precede counts.
    A comment and a string literal name nothing.  A name is compared as the
    lexer spells it, after the line splices of phase 2.

WHAT COUNTS AS A GRANT
    Each name of the catalog in the type of the alias
    hardening_syscall_atoms.

WHAT THE GUARD CANNOT SEE
    A call reached through a function of another header, through a template
    parameter or through a name that a macro builds with `##`, and a
    syscall number written as a literal.

Exit 0 when the sets agree, 1 when they disagree or a helper is wrong, 2
when the header, the catalog or either set cannot be read, on a usage error
or on a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Callable, Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

HEADER = "include/crucible/warden/Hardening.h"
CATALOG = "include/fixy/atoms/Syscall.h"
TUPLE = "hardening_syscall_atoms"
HELPER_SUFFIX = "_sys"


class Unreadable(Exception):
    """An input that the guard needs is missing or has no entry it can read."""


def parse_one(path: Path) -> tsast.Tree:
    """Parse one file, or raise Unreadable."""
    if not path.is_file():
        raise Unreadable(f"{path} does not exist")
    tree = next(tsast.parse([path], strict=False))
    if tree.diagnostic is not None:
        raise Unreadable(f"the parser cannot read {path}. {tree.diagnostic.strip()}")
    return tree


def catalog_names(tree: tsast.Tree) -> frozenset[str]:
    """Return the enumerators of the SyscallId enum."""
    for node in tree.find("enum_specifier"):
        named = node.child_by_field("name")
        body = node.child_by_field("body")
        if named is not None and named.text == "SyscallId" and body is not None:
            return frozenset(child.child_by_field("name").text for child in body.children
                             if child.type == "enumerator" and child.child_by_field("name") is not None)
    raise Unreadable(f"{CATALOG} has no enum SyscallId")


def scopes_of(node: tsast.Node) -> list[str] | None:
    """Return the scope names above a name, outermost first, or None when a scope is not a plain name.

    A name qualified from `::` alone has no scope names.
    """
    scopes: list[str] = []
    current = node
    while current.field == "name" and current.parent is not None and current.parent.type == "qualified_identifier":
        scope = current.parent.child_by_field("scope")
        if scope is not None:
            scopes.insert(0, scope.text)
        current = current.parent
    return scopes


def function_name(function: tsast.Node) -> str | None:
    """Return the name that a function definition declares, through its nested declarators."""
    current = function.child_by_field("declarator")
    while current is not None and current.type not in ("identifier", "field_identifier", "qualified_identifier"):
        current = current.child_by_field("declarator")
    return current.text if current is not None else None


def helper_of(node: tsast.Node) -> str | None:
    """Return the name of the `_sys` helper whose body holds a node, or None."""
    function = node.ancestor_of_type("function_definition")
    while function is not None:
        name = function_name(function)
        if name is not None and name.endswith(HELPER_SUFFIX):
            return name
        function = function.ancestor_of_type("function_definition")
    return None


def call_of(word: str, names: frozenset[str]) -> str | None:
    """Return the system call that a name stands for: itself, a `_sys` helper, or a SYS_ number, or None."""
    if word in names:
        return word
    if word.endswith(HELPER_SUFFIX) and word[: -len(HELPER_SUFFIX)] in names:
        return word[: -len(HELPER_SUFFIX)]
    if word.startswith("SYS_") and word[4:] in names:
        return word[4:]
    return None


def root_issued(root: tsast.Node, names: frozenset[str], problems: list[str],
                line_of: Callable[[tsast.Node], int]) -> set[str]:
    """Derive the system calls that the identifiers under the root of a file or a macro body issue.

    Complexity: linear in the number of nodes under the root.

    Args:
        root: The root node
        names: The catalog names
        problems: Each wrong helper is appended here
        line_of: The one-based file line of a node under the root

    Returns:
        The system call names
    """
    found: set[str] = set()
    for node in root.descendants("identifier"):
        if node.field == "declarator" or (node.parent is not None and node.parent.type == "enumerator"):
            continue
        if scopes_of(node):
            continue
        text = tsast.spelled(node)
        call = call_of(text, names)
        if call is None:
            continue
        helper = helper_of(node) if text.startswith("SYS_") else None
        if helper is None:
            found.add(call)
        elif helper[: -len(HELPER_SUFFIX)] != call:
            problems.append(f"{HEADER}:{line_of(node)}: the helper {helper} issues {text}, which is not its own "
                            f"system call.")
    return found


def token_issued(tokens: list[tsast.Token], names: frozenset[str]) -> Iterator[str]:
    """Yield each system call that the tokens of a macro body that did not parse name.

    A name after `.` or `->` is a member, and a name after `X::` is
    qualified by something other than the global namespace, so neither
    counts.
    """
    for index, token in enumerate(tokens):
        if token.kind != "identifier":
            continue
        before = tokens[index - 1].text if index >= 1 else ""
        if before in (".", "->") or (before == "::" and index >= 2 and tokens[index - 2].kind == "identifier"):
            continue
        call = call_of(token.text, names)
        if call is not None:
            yield call


def issued(tree: tsast.Tree, names: frozenset[str]) -> tuple[set[str], list[str]]:
    """Derive the system calls that one parsed header issues, in its code and in its macro bodies.

    Complexity: linear in the number of nodes of the header and of its macro bodies.

    Returns:
        The system call names, and each wrong helper
    """
    problems: list[str] = []
    found = root_issued(tree.root, names, problems, lambda node: node.line)
    for body in tsast.macro_bodies([tree]):
        if body.is_parsed:
            found |= root_issued(body.root, names, problems, lambda node, body=body: body.origin(node)[0] + 1)
        else:
            found.update(token_issued(tsast.pp_tokens(body.text, body.first_row), names))
    return found, problems


def granted(tree: tsast.Tree, names: frozenset[str]) -> set[str]:
    """Return each catalog name in the type of the tuple alias."""
    for node in tree.find("alias_declaration"):
        named = node.child_by_field("name")
        if named is not None and named.text == TUPLE:
            return {child.text for child in node.descendants("identifier", "type_identifier")
                    if child.text in names and (scopes_of(child) or [""])[-1] == "SyscallId"}
    return set()


def check(root: Path) -> int:
    """Compare the issued set with the granted set and report.

    Returns:
        0 when they agree, 1 when they disagree, 2 when an input cannot be read
    """
    try:
        names = catalog_names(parse_one(root / CATALOG))
        header = parse_one(root / HEADER)
    except Unreadable as exc:
        print(f"check-syscall-grant-coverage: {exc}", file=sys.stderr)
        return 2
    calls, problems = issued(header, names)
    grants = granted(header, names)
    if not grants or not calls:
        print(f"check-syscall-grant-coverage: {HEADER} has no {'grant' if not grants else 'call'} the guard can "
              f"read.  Update the guard to the new shape rather than leave it reading nothing.", file=sys.stderr)
        return 2
    missing, extra = sorted(calls - grants), sorted(grants - calls)
    if missing:
        print(f"SYSCALL-GRANT issued but not granted: {' '.join(missing)}", file=sys.stderr)
    if extra:
        print(f"SYSCALL-GRANT granted but never issued: {' '.join(extra)}", file=sys.stderr)
    for problem in problems:
        print(f"SYSCALL-GRANT wrong helper: {problem}", file=sys.stderr)
    if missing or extra or problems:
        print(f"check-syscall-grant-coverage: {TUPLE} in {HEADER} disagrees with the call sites.  For a new "
              f"call, add per<SyscallId::<name>> to the tuple, raise the tuple_size assert and extend "
              f"test/test_hardening_syscall_atoms.cpp.  For a grant with no call, remove the grant, or say in a "
              f"comment beside it which header issues the call.", file=sys.stderr)
        return 1
    print(f"check-syscall-grant-coverage: clean — {TUPLE} names exactly the system calls issued in {HEADER}.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each call shape and each drift direction, then check the verdicts.

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

    catalog = ("#pragma once\nnamespace fixy::atom::syscall {\nenum class SyscallId : unsigned short {\n"
               "    sched_setaffinity = 0, sched_getaffinity = 1, madvise = 2, mlock = 3, mlock2 = 4,\n"
               "    munlock = 5, prctl = 6, ptrace = 7, close = 8,\n};\n}\n")

    def header(calls: str = "", grants: str = "", helper: str = "SYS_mlock2") -> str:
        """Return a planted Hardening.h with extra calls and extra grants."""
        return ("#pragma once\n"
                "// A prose mention of ::prctl( and \"::munlock(\" names nothing.\n"
                "namespace crucible::warden {\n"
                "inline int mlock2_sys(const void* a, unsigned long l) noexcept {\n"
                f"    return ::syscall({helper}, a, l);\n"
                "}\n"
                "inline void apply() noexcept {\n"
                "    (void)::sched_setaffinity(0, 0, nullptr);\n"
                "    (void)madvise(nullptr, 0, 0);\n"
                "    (void)mlock2_sys(nullptr, 0);\n"
                "    (void)SyscallId::close;\n"
                "    const char* text = \"::close(\";\n"
                f"    {calls}\n"
                "}\n"
                "using hardening_syscall_atoms =\n"
                "    std::tuple<per<::fixy::atom::syscall::SyscallId::sched_setaffinity>,\n"
                "               per<SyscallId::madvise>,\n"
                f"               per<sc::SyscallId::mlock2>{grants}>;\n"
                "}\n")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / CATALOG).parent.mkdir(parents=True)
        (root / CATALOG).write_text(catalog, encoding="utf-8")
        (root / HEADER).parent.mkdir(parents=True)

        def run(text: str, cwd: Path | None = None) -> tuple[int, str]:
            """Plant a header, run the check from one working directory, and keep its report."""
            (root / HEADER).write_text(text, encoding="utf-8")
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd or root)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        expect("agreeing sets pass, with prose, a string and a qualified enumerator ignored", run(header())[0] == 0,
               True)
        expect("the report from / equals the report from the scan root",
               run(header(), Path("/")) == run(header()))
        for calls, name, label in (
            ("(void)::sched_getaffinity(0, 0, nullptr);", "sched_getaffinity", "a call qualified from ::"),
            ("(void)sched_getaffinity(0, 0, nullptr);", "sched_getaffinity", "a bare call"),
            ("auto p = &::munlock;", "munlock", "an address"),
            ("auto p = ::munlock; (void)p(nullptr, 0);", "munlock", "a function pointer"),
            ("(void)::syscall(SYS_ptrace, 0);", "ptrace", "a direct syscall number"),
            ("(void)ptrace_sys(0);", "ptrace", "a helper call"),
            ("#define LOCK(a) ::mlock(a, 1)\n", "mlock", "a macro body"),
            ("#define PASTE(a) ::mlock(a##_p, 1)\n", "mlock", "a macro body that pastes tokens, read from its tokens"),
            ("(void)::sched_\\\ngetaffinity(0, 0, nullptr);", "sched_getaffinity",
             "a name that a line splice cuts in two"),
        ):
            code, report = run(header(calls))
            expect(f"caught, issued but not granted: {label}",
                   code == 1 and re.search(rf"issued but not granted:.*\b{name}\b", report) is not None)
        for calls, label in (("file.close();", "a member call"), ("pipe->close();", "a member call through ->"),
                             ("(void)other::close(0);", "a name qualified by another namespace"),
                             ("#define SHUT(f) (f).close()\n", "a member call in a macro body"),
                             ("#define SHUT2(f) (f)-> /* c */ \\\n    close()\n",
                              "a member call in a macro body that a block comment splits"),
                             ("int close = 0;", "the name of a declaration")):
            expect(f"not counted: {label}", run(header(calls))[0] == 0, True)
        expect("counted: a use of a local with a catalog name, which fails loud rather than quiet",
               run(header("int close = 0; (void)close;"))[0] == 1)
        code, report = run(header(grants=",\n               per<SyscallId::ptrace>"))
        expect("caught: a grant with no call", code == 1 and "granted but never issued: ptrace" in report)
        code, report = run(header(helper="SYS_mlock"))
        expect("caught: a helper that issues another system call", code == 1 and "wrong helper" in report)
        code, report = run(header().replace(TUPLE, "renamed_atoms"))
        expect("a tuple the guard cannot find exits 2", code == 2)
        (root / CATALOG).unlink()
        expect("a missing catalog exits 2", run(header())[0] == 2)
    if failures:
        print(f"check-syscall-grant-coverage --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-syscall-grant-coverage --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check, the listing or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"], ["--list"]):
        print("usage: check-syscall-grant-coverage.py [--self-test | --list]", file=sys.stderr)
        return 2
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--list"]:
            names = catalog_names(parse_one(tsast.REPO_ROOT / CATALOG))
            tree = parse_one(tsast.REPO_ROOT / HEADER)
            print("issued:  " + " ".join(sorted(issued(tree, names)[0])))
            print("granted: " + " ".join(sorted(granted(tree, names))))
            return 0
        return check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-syscall-grant-coverage: {exc}", file=sys.stderr)
        return 3
    except Unreadable as exc:
        print(f"check-syscall-grant-coverage: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
