#!/usr/bin/env python3
"""check-flip-list — every consumer of the old substrate is listed, and the list only shrinks.

Each consumer of the old substrate moves onto include/foundation/ and
include/fixy/.  The old substrate is the frozen tree: the prefixes of
scripts/frozen-paths.txt, the list that scripts/check-frozen-tree.py also
reads, so the two guards cannot disagree about what is old.  A consumer is a
file under the scan roots, outside every frozen prefix, that names the old
substrate.  scripts/flip-list.txt holds one path per consumer, and its header
comment gives the flip order.

THE RULE
    - A listed file that still names the old substrate passes.  It is not
      drained yet.  The guard prints how many remain.
    - A listed file that no longer names the old substrate FAILS as drained.
      Remove its entry in the same commit that drains it.  An entry for a
      drained file is a standing permission, so the file could take the old
      substrate back and the guard would stay quiet.
    - An unlisted file that names the old substrate FAILS.  A new consumer
      cannot appear: write the new substrate.
    - A listed file that does not exist FAILS.  A deletion drains a file, and
      its entry goes in the same commit.
    - A listed path under a frozen prefix, outside every scan root, listed
      twice, or out of sorted order FAILS, because it is a typo or a merge
      error, and it can hide one of the conditions above.
    - A file under the scan roots that the parser cannot read FAILS, unless
      scripts/tsast.py lists it as unparseable.

WHAT NAMES THE OLD SUBSTRATE
    N is one of safety, fixy, algebra, effects, permissions, sessions,
    bridges, handles or concurrent.  The guard reads the parse tree of each
    file (scripts/tsast.py), so white space, a line break or a comment inside
    a name changes nothing, and a comment names nothing.  Five shapes name N:
      1. a name that resolves to crucible::N: a qualified name from `::`, a
         relative name that an enclosing namespace or a using-directive in
         force makes reach crucible::N, and a name whose leading namespace
         alias resolves to it.  A relative fixy:: is left out, because an
         unqualified fixy:: names the new tree;
      2. a using-directive, a using-declaration or a namespace alias whose
         target resolves to crucible::N the same way;
      3. a namespace definition whose full name, inline namespaces left out,
         starts with crucible::N, as `namespace crucible { namespace N {`;
      4. an include of crucible/N/... or of the frozen umbrella crucible/Fixy.h;
      5. a string literal whose content spells crucible::N, or an unqualified
         N:: for an N other than fixy, such as a reflected type name in a
         golden.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A macro body is read as tokens, because a replacement list is not C++
      on its own.  A qualified name crucible::N and an unqualified N:: in the
      body count, and a name that a macro builds from pieces does not.
    - A relative name counts when one enclosing namespace makes it reach
      crucible::N.  The guard does not know which nearer namespaces exist, so
      `effects::` inside `namespace crucible::cntp` counts even when
      crucible::cntp::effects exists.
    - A file that reaches the old substrate only through a header that
      includes it, and spells none of it, is not a consumer.  It drains when
      its header drains.
    - Unqualified fixy:: inside namespace crucible finds the old
      crucible::fixy when that namespace is declared, and it is not read.
    - A BPF program is C (`.bpf.c`), and the kit reads C++, so a C file is out
      of scope.

Exit 0 when every consumer is listed and every listed file is a consumer, 1 on
an unlisted consumer, a drained or missing entry, a bad entry or a parse
failure, 2 on a usage error, a missing frozen list or a failed self-test, 3
when the parser kit is missing.
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

import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

LIST = "scripts/flip-list.txt"
PATHS_FILE = "scripts/frozen-paths.txt"
SCAN_ROOTS = ("include/crucible", "src", "vessel", "bench", "tools", "examples", "fuzz")
OLD_NAMES = ("safety", "fixy", "algebra", "effects", "permissions", "sessions", "bridges", "handles",
             "concurrent")
OLD = frozenset(OLD_NAMES)
OLD_NO_FIXY = OLD - {"fixy"}
UMBRELLA = "crucible/Fixy.h"
# The two name spellings inside literal content, which counts as a use.
IN_LITERAL = (re.compile(r"\bcrucible::(?:" + "|".join(OLD_NAMES) + r")\b"),
              re.compile(r"(?<![\w:])(?:" + "|".join(sorted(OLD_NO_FIXY)) + r")::"))
# The outermost name nodes that can spell a namespace.
NAME_NODES = ("qualified_identifier", "nested_namespace_specifier")


class Refused(Exception):
    """The guard cannot run: the frozen list is missing or empty (exit 2)."""


def frozen_prefixes(root: Path) -> list[str]:
    """Return the prefixes of scripts/frozen-paths.txt, in order.

    Raises:
        Refused: If the list is missing or empty, since then nothing is defined as old
    """
    listing = root / PATHS_FILE
    if not listing.is_file():
        raise Refused(f"{PATHS_FILE} is missing, so the old substrate is undefined.")
    prefixes = [line.split("#", 1)[0].strip() for line in listing.read_text().splitlines()]
    prefixes = [p for p in prefixes if p]
    if not prefixes:
        raise Refused(f"{PATHS_FILE} lists no prefix, so the old substrate is undefined.")
    return prefixes


def is_frozen(path: str, prefixes: list[str]) -> bool:
    """Return True when the path is under a frozen directory or is a frozen file."""
    return any(path.startswith(p) if p.endswith("/") else path == p for p in prefixes)


def is_under_scan_root(path: str) -> bool:
    """Return True when the path is under one of the scan roots."""
    return any(path.startswith(r + "/") for r in SCAN_ROOTS)


def candidate_files(root: Path, prefixes: list[str]) -> list[str]:
    """Return the source files under the scan roots that git does not ignore, outside the frozen tree.

    Complexity: linear in the number of files under the scan roots.
    """
    inside = subprocess.run(["git", "-C", str(root), "rev-parse", "--is-inside-work-tree"],
                            capture_output=True, text=True, check=False)
    if inside.returncode == 0 and inside.stdout.strip() == "true":
        listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
                                 "--exclude-standard", "--", *SCAN_ROOTS],
                                capture_output=True, check=True).stdout.decode().split("\0")
        paths = sorted({p for p in listed if p})
    else:
        paths = sorted(str(p.relative_to(root)) for r in SCAN_ROOTS for p in (root / r).rglob("*") if p.is_file())
    return [p for p in paths if tsast.is_in_cpp_scope(p) and (root / p).is_file() and not is_frozen(p, prefixes)]


def is_old_namespace(parts: tuple[str, ...]) -> bool:
    """Return True when a name from the root lies in crucible::N for an old N."""
    return len(parts) >= 2 and parts[0] == "crucible" and parts[1] in OLD


def visible_at(using: tsast.UsingDecl, at: tsast.Node) -> bool:
    """Return True when a using declaration is in force at a node: its scope holds the node and it ends first."""
    scope = using.scope
    return scope.start <= at.start and at.end <= scope.end and using.node.end <= at.start


def resolves_old(parts: tuple[str, ...], is_global: bool, at: tsast.Node, aliases: list[tsast.NamespaceAlias],
                 directives: list[tsast.UsingDecl]) -> bool:
    """Return True when a name spelled at a node can resolve to crucible::N for an old N.

    A leading namespace alias resolves first.  A relative name then gets one
    candidate for each enclosing namespace and one for each using-directive in
    force.  A relative name that starts with fixy is left out, because an
    unqualified fixy:: names the new tree.

    Complexity: O(depth + directives) for each name.
    """
    resolved = tsast.resolve_namespace(parts, at, aliases, is_global=is_global)
    if is_old_namespace(resolved):
        return True
    if is_global or not resolved or resolved[0] == "fixy":
        return False
    enclosing = tsast.namespace_path(at, skip_inline=True)
    if any(is_old_namespace(enclosing[:depth] + resolved) for depth in range(len(enclosing), 0, -1)):
        return True
    for directive in directives:
        if directive.node.index != at.index and visible_at(directive, at):
            target = tsast.resolve_namespace(directive.target, directive.node, aliases, is_global=directive.is_global)
            if is_old_namespace(target + resolved):
                return True
    return False


def include_is_old(path: tsast.Node) -> bool:
    """Return True when an include path names crucible/N/... or the frozen umbrella."""
    inner = path.text.strip()[1:-1].strip()
    return inner == UMBRELLA or any(inner.startswith(f"crucible/{name}/") for name in OLD_NAMES)


def macro_names_old(tree: tsast.Tree) -> bool:
    """Return True when a macro body spells crucible::N, an unqualified old N::, or a literal that does.

    A replacement list is not C++ on its own, so its tokens are read, not a parse.
    """
    for define in tree.find("preproc_def", "preproc_function_def"):
        values = [child for child in define.children if child.field == "value"]
        if not values:
            continue
        tokens = tsast.pp_tokens(tree.slice(values[0].start, values[-1].end), values[0].start[0])
        if any(token.kind == "string" and any(p.search(token.text) for p in IN_LITERAL) for token in tokens):
            return True
        for is_global, parts, _row in tsast.token_qualified_names(tokens):
            if is_old_namespace(parts) or (not is_global and len(parts) >= 2 and parts[0] in OLD_NO_FIXY):
                return True
    return False


def names_old(tree: tsast.Tree) -> bool:
    """Return True when a parsed file names the old substrate in one of the five shapes.

    Complexity: linear in the node count of the file, times the candidates of each name.
    """
    if any(include_is_old(path) for node in tree.find("preproc_include")
           if (path := node.child_by_field("path")) is not None):
        return True
    if any(p.search(node.text) for node in tree.find("string_content", "raw_string_content") for p in IN_LITERAL):
        return True
    for node in tree.find("namespace_definition"):
        body = node.child_by_field("body")
        if is_old_namespace(tsast.namespace_path(body if body is not None else node, skip_inline=True)):
            return True
    aliases = tsast.namespace_aliases(tree)
    usings = tsast.using_names(tree)
    directives = [using for using in usings if using.is_directive]
    targets = [(alias.target, alias.is_global, alias.node) for alias in aliases]
    targets += [(using.target, using.is_global, using.node) for using in usings]
    if any(resolves_old(target, is_global, at, aliases, directives) for target, is_global, at in targets):
        return True
    for node in tree.find(*NAME_NODES):
        if node.parent is not None and node.parent.type in NAME_NODES:
            continue
        spelled = tsast.qualified_parts(node)
        if spelled is not None and spelled[1] and resolves_old(spelled[1], spelled[0], node, aliases, directives):
            return True
    return macro_names_old(tree)


def consumers(root: Path, files: list[str]) -> tuple[list[str], list[str]]:
    """Return the files that name the old substrate, and a problem line for each file that does not parse.

    Complexity: one parse of each file, in one run of the kit.
    """
    found: list[str] = []
    problems: list[str] = []
    for tree in tsast.parse([root / p for p in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot tell what it "
                            f"names: {tree.diagnostic}")
        elif names_old(tree):
            found.append(rel)
    return sorted(found), problems


def check(root: Path, mode: str) -> int:
    """Compare the consumers in the tree with the flip list, and report.

    Returns:
        0 clean, 1 on a problem, 2 on a missing frozen list
    """
    try:
        prefixes = frozen_prefixes(root)
    except Refused as exc:
        print(f"check-flip-list: {exc}", file=sys.stderr)
        return 2
    found, problems = consumers(root, candidate_files(root, prefixes))
    if mode == "scan":
        for path in found:
            print(path)
        for line in problems:
            print(line)
        print(f"check-flip-list: {len(found)} file(s) name the old substrate.", file=sys.stderr)
        return 1 if problems else 0
    entries: list[tuple[int, str]] = []
    if (root / LIST).exists():
        for number, raw in enumerate((root / LIST).read_text().splitlines(), start=1):
            entry = raw.strip()
            if entry and not entry.startswith("#"):
                entries.append((number, entry))
    seen: set[str] = set()
    previous = ""
    for number, entry in entries:
        where = f"flip-list.txt:{number}: {entry}"
        if entry in seen:
            problems.append(f"DUPLICATE {where} — listed twice.")
        seen.add(entry)
        if entry < previous:
            problems.append(f"UNSORTED  {where} — sorts before {previous}; keep the list sorted.")
        previous = max(previous, entry)
        if is_frozen(entry, prefixes):
            problems.append(f"FROZEN    {where} — is the old substrate itself, not a consumer of it.")
        elif not is_under_scan_root(entry):
            problems.append(f"OUTSIDE   {where} — is under no scan root ({', '.join(SCAN_ROOTS)}).")
        elif not (root / entry).is_file():
            problems.append(f"MISSING   {where} — does not exist.  Remove the entry in the commit that deletes "
                            f"or moves the file.")
    consumer_set = set(found)
    for number, entry in entries:
        if (root / entry).is_file() and is_under_scan_root(entry) and not is_frozen(entry, prefixes) \
                and entry not in consumer_set:
            problems.append(f"DRAINED   flip-list.txt:{number}: {entry} — names the old substrate no longer.  "
                            f"Remove the entry in this commit.")
    for path in found:
        if path not in seen:
            problems.append(f"UNLISTED  {path} — names the old substrate and is not on the flip list.  A new "
                            f"consumer cannot appear; write include/foundation or include/fixy.  Run --scan to "
                            f"see the consumers this guard reads.")
    for line in problems:
        print(line)
    remaining = sum(1 for _, e in entries if e in consumer_set)
    print(f"check-flip-list: {remaining} listed file(s) remain on the old substrate; {len(entries)} entr(y/ies), "
          f"{len(found)} consumer(s) in the tree, {len(problems)} problem(s).", file=sys.stderr)
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
        """Run the check on the planted repository and keep both streams."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = check(root, "check")
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def expect(root: Path, code: int, needle: str, name: str, negative: bool = False) -> None:
        """Record one case: the check exits with code and prints needle."""
        nonlocal negatives
        negatives += negative
        got, report = captured(root)
        ok = got == code and needle in report
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(f"{name}: expected exit {code} and '{needle}', got exit {got}:\n{report}")

    def listing(root: Path, *entries: str) -> None:
        """Write the planted flip list."""
        write(root, LIST, "# a comment line\n\n" + "".join(f"{e}\n" for e in entries))

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        throwaway_repo.init(root)
        write(root, PATHS_FILE, "# planted\ninclude/crucible/safety/\ninclude/crucible/Fixy.h\n")
        write(root, "include/crucible/safety/Linear.h",
              "#pragma once\nnamespace crucible::safety::detail { struct Linear {}; }\n")
        write(root, "include/crucible/Fixy.h", "#pragma once\n#include <crucible/safety/Linear.h>\n")
        write(root, "test/test_linear.cpp", "#include <crucible/safety/Linear.h>\n")
        write(root, "include/fixy/Linear.h", "#pragma once\nnamespace fixy { struct Linear {}; }\n")
        write(root, "src/Clean.cpp",
              "#include <fixy/Linear.h>\n// crucible::safety::Linear was the old spelling.\n"
              "/* #include <crucible/safety/Linear.h>\n   effects::Row as well */\n"
              "namespace crucible { fixy::Linear a; foundation::effects::Row<> r; fixy::concurrent::Q q;\n"
              "namespace fixy2 { int n; } }\nnamespace fixy::concurrent { int m; }\n"
              "namespace foundation { effects::Row<> r2; }\nconcurrent::Queue* q2 = nullptr;\n")
        listing(root)
        expect(root, 0, "0 listed file(s) remain", "an empty list over a clean tree")

        write(root, "include/crucible/ByInclude.h", "#include <crucible/safety/Linear.h>\n")
        write(root, "src/ByQualified.cpp", "int x = sizeof(crucible::safety::Linear);\n")
        write(root, "bench/ByUnqualified.cpp", "namespace crucible { effects::Row<> r; }\n")
        write(root, "src/ByUmbrella.cpp", "#include <crucible/Fixy.h>\n")
        listing(root, "bench/ByUnqualified.cpp", "include/crucible/ByInclude.h", "src/ByQualified.cpp",
                "src/ByUmbrella.cpp")
        expect(root, 0, "4 listed file(s) remain", "listed consumers of each spelling pass")

        write(root, "src/ByQualified.cpp", "// once #include <crucible/safety/Linear.h>\n#include <fixy/Linear.h>\n")
        expect(root, 1, "DRAINED   flip-list.txt:5: src/ByQualified.cpp", "a listed file drained to a comment",
               True)
        listing(root, "bench/ByUnqualified.cpp", "include/crucible/ByInclude.h", "src/ByUmbrella.cpp")
        expect(root, 0, "3 listed file(s) remain", "the drained entry removed")

        planted = {
            "src/NewConsumer.cpp": "namespace crucible { concurrent::Topology* t; }\n",
            "src/MultiLine.cpp": "int a = sizeof(crucible::\n    safety::Linear);\n",
            "src/Spaced.cpp": "int b = sizeof(crucible :: safety :: Linear);\n",
            "src/Commented.cpp": "int c = sizeof(crucible::/* note */safety::Linear);\n",
            "src/NamespaceDef.cpp": "namespace crucible::safety { int d; }\n",
            "src/UsingDirective.cpp": "using namespace crucible::safety;\n",
            "src/Alias.cpp": "namespace old = crucible::safety;\n",
            "src/Reopened.cpp": "namespace crucible {\nnamespace safety {\nint e;\n}\n}\n",
            "src/ReopenedInline.cpp": "namespace crucible { inline namespace v1 { namespace effects { int f; } } }\n",
            "src/SlashInString.cpp": 'const char* s = "//"; int g = sizeof(crucible::safety::Linear);\n',
            "src/CommentInRaw.cpp": 'const char* r = R"(/*)"; int h = sizeof(crucible::safety::Linear);\n',
            "src/MacroBody.cpp": "#define USE_OLD crucible::safety::Linear\n",
            "src/QuotedInclude.cpp": '#include "crucible/effects/Row.h"\n',
            "src/AliasOfCrucible.cpp": "namespace cr = crucible;\nint x = sizeof(cr::safety::Linear);\n",
            "src/DirectiveThenRelative.cpp": "using namespace crucible;\neffects::Row<> r;\n",
            "src/RelativeDirective.cpp": "namespace crucible { using namespace safety; }\n",
            "src/OldInLiteral.cpp": 'const char* name = "crucible::safety::Linear";\n',
        }
        for rel, text in planted.items():
            write(root, rel, text)
            expect(root, 1, f"UNLISTED  {rel}", f"an unlisted consumer: {rel}", True)
            (root / rel).unlink()

        (root / "src/ByUmbrella.cpp").unlink()
        expect(root, 1, "MISSING   flip-list.txt:5: src/ByUmbrella.cpp", "a listed file that does not exist", True)
        listing(root, "bench/ByUnqualified.cpp", "include/crucible/ByInclude.h")
        expect(root, 0, "2 listed file(s) remain", "the missing entry removed")

        for entries, needle, name in (
                (("bench/ByUnqualified.cpp", "include/crucible/ByInclude.h", "include/crucible/safety/Linear.h"),
                 "FROZEN    flip-list.txt:5: include/crucible/safety/Linear.h", "a frozen entry"),
                (("bench/ByUnqualified.cpp", "include/crucible/ByInclude.h", "test/test_linear.cpp"),
                 "OUTSIDE   flip-list.txt:5: test/test_linear.cpp", "an entry outside the scan roots"),
                (("include/crucible/ByInclude.h", "bench/ByUnqualified.cpp"), "UNSORTED", "an unsorted list"),
                (("bench/ByUnqualified.cpp", "bench/ByUnqualified.cpp", "include/crucible/ByInclude.h"),
                 "DUPLICATE", "a duplicate entry")):
            listing(root, *entries)
            expect(root, 1, needle, name, True)
        listing(root, "bench/ByUnqualified.cpp", "include/crucible/ByInclude.h")

        write(root, "src/Broken.cpp", "namespace safety { int i = ; }}}\n")
        expect(root, 1, "PARSE     src/Broken.cpp", "a file the parser cannot read fails", True)
        (root / "src/Broken.cpp").unlink()

        expect(root, 0, "2 listed file(s) remain", "the tree is clean again")
        from_root, from_slash = captured(root, root), captured(root, Path("/"))
        ok = from_root == from_slash
        print(f"  {'ok  ' if ok else 'FAIL'} the report from / equals the report from the repository")
        if not ok:
            failures.append("the report depends on the working directory")

        write(root, PATHS_FILE, "include/crucible/Fixy.h\n")
        expect(root, 1, "UNLISTED  include/crucible/safety/Linear.h", "the frozen list is read, not assumed", True)
        write(root, PATHS_FILE, "# nothing\n")
        expect(root, 2, "lists no prefix", "an empty frozen list", True)
        (root / PATHS_FILE).unlink()
        expect(root, 2, "is missing", "a missing frozen list", True)
    for failure in failures:
        print(f"check-flip-list --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-flip-list --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, print the consumers, or run the self-test."""
    modes = {(): "check", ("--quiet",): "check", ("--scan",): "scan", ("--self-test",): "self-test"}
    mode = modes.get(tuple(argv))
    if mode is None:
        print("usage: check-flip-list.py [--quiet | --scan | --self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if mode == "self-test" else check(tsast.REPO_ROOT, mode)
    except tsast.KitMissing as exc:
        print(f"check-flip-list: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
