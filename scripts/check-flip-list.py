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
    bridges, handles or concurrent.  The guard reads the tokens of each file
    with its comments blanked by scripts/cxx_lex.py, so white space, a line
    break or a comment between two tokens changes nothing, and a `//` inside a
    string or a raw string is not a comment.  Four shapes name N:
      1. a qualified name crucible::N, which also covers a namespace
         definition `namespace crucible::N`, a using-directive and a
         namespace alias;
      2. an include of crucible/N/... or of the frozen umbrella crucible/Fixy.h;
      3. an unqualified N:: with no identifier character and no `:` before
         it, which code inside `namespace crucible {` uses to reach N.  This
         shape leaves out fixy, because an unqualified fixy:: names the new
         tree;
      4. a namespace N that the parse shows is opened inside namespace
         crucible, as `namespace crucible { namespace N {`.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A macro body is raw text: a name spelled in a macro counts, and a name
      that a macro builds from pieces does not.
    - A string literal is not blanked.  A literal that spells an old name,
      such as a reflected type name in a golden, counts as a use.
    - A file that reaches the old substrate only through a header that
      includes it, and spells none of it, is not a consumer.  It drains when
      its header drains.
    - Unqualified fixy:: inside namespace crucible finds the old
      crucible::fixy when that namespace is declared, and it is not read.

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

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

LIST = "scripts/flip-list.txt"
PATHS_FILE = "scripts/frozen-paths.txt"
SCAN_ROOTS = ("include/crucible", "src", "vessel", "bench", "tools", "examples", "fuzz")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".H", ".C", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp",
                      ".tpp"})
OLD_NAMES = ("safety", "fixy", "algebra", "effects", "permissions", "sessions", "bridges", "handles",
             "concurrent")
OLD = "|".join(OLD_NAMES)
OLD_NO_FIXY = "|".join(name for name in OLD_NAMES if name != "fixy")
INCLUDE = re.compile(r'#\s*include\s*[<"]\s*crucible/(?:(?:' + OLD + r')/|Fixy\.h\s*[>"])')
# The two name spellings inside a literal, which counts as a use.
IN_LITERAL = (re.compile(r"\bcrucible::(?:" + OLD + r")\b"), re.compile(r"(?<![\w:])(?:" + OLD_NO_FIXY + r")::"))
PUNCTUATION = re.compile(r"::|\S")
LITERALS = frozenset({"string", "raw", "char"})
# A namespace definition that can reopen an old namespace.  Only a file with
# one is parsed, because only the parse can say what encloses it.
REOPEN_HINT = re.compile(r"\bnamespace\s+(?:inline\s+)?(?:" + OLD + r")\b")


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
    return [p for p in paths if Path(p).suffix in SUFFIXES and (root / p).is_file() and not is_frozen(p, prefixes)]


def names_old(text: str) -> bool:
    """Return True when a comment-blanked text names the old substrate by an include or a name.

    The names are read from tokens, so white space, a line break or a blanked
    comment between crucible, :: and N changes nothing.  A literal is one
    token, and its text is searched for the same names, because a literal that
    spells an old name counts as a use.  Complexity: linear in the length of
    the text.
    """
    if INCLUDE.search(text):
        return True
    tokens: list[str] = []
    cursor = 0
    for match in cxx_lex.LEXER.finditer(text):
        tokens += PUNCTUATION.findall(text[cursor:match.start()])
        if match.lastgroup in LITERALS and any(p.search(match.group()) for p in IN_LITERAL):
            return True
        tokens.append(match.group())
        cursor = match.end()
    tokens += PUNCTUATION.findall(text[cursor:])
    for i, token in enumerate(tokens[:-1]):
        if tokens[i + 1] != "::":
            continue
        if token == "crucible" and i + 2 < len(tokens) and tokens[i + 2] in OLD_NAMES:
            return True
        if token in OLD_NAMES and token != "fixy" and (i == 0 or tokens[i - 1] != "::"):
            return True
    return False


def enclosing_names(node: tsast.Node) -> list[str]:
    """Return the name components of a namespace definition and of every namespace around it.

    An inline namespace is transparent: its members are members of the
    namespace around it, so its name is left out.
    """
    parts: list[str] = []
    current: tsast.Node | None = node
    while current is not None:
        if current.type == "namespace_definition" and not current.text.lstrip().startswith("inline"):
            name = current.child_by_field("name")
            if name is not None:
                parts[:0] = re.sub(r"\s+", "", name.text).split("::")
        current = current.parent
    return parts


def reopens_old(tree: tsast.Tree) -> bool:
    """Return True when the parse opens crucible::N for an old N, at any nesting."""
    for node in tree.find("namespace_definition"):
        parts = [part for part in enclosing_names(node) if part]
        for i in range(len(parts) - 1):
            if parts[i] == "crucible" and parts[i + 1] in OLD_NAMES:
                return True
    return False


def consumers(root: Path, files: list[str]) -> tuple[list[str], list[str]]:
    """Return the files that name the old substrate, and a problem line for each file that does not parse.

    Complexity: one lexical pass over each file, plus one parse of each file
    that opens a namespace with an old name.
    """
    found: set[str] = set()
    to_parse: list[str] = []
    for path in files:
        text, _ = cxx_lex.blank(cxx_lex.splice((root / path).read_text(errors="replace"))[0])
        if names_old(text):
            found.add(path)
        elif REOPEN_HINT.search(text):
            to_parse.append(path)
    problems: list[str] = []
    for tree in tsast.parse([root / p for p in to_parse], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None and rel not in tsast.UNPARSEABLE:
            problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot tell what "
                            f"its namespaces open: {tree.diagnostic}")
        elif reopens_old(tree):
            found.add(rel)
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
        subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
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
              "namespace fixy2 { int n; } }\nnamespace fixy::concurrent { int m; }\n")
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
