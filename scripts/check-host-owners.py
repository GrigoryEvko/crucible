#!/usr/bin/env python3
"""check-host-owners — each context owner is defined in exactly one reviewed file.

foundation::effects::host declares the owners that mint the execution
contexts: InitOwner, BackgroundOwner and ForegroundOwner.  Each context names
its owner as a friend, so the owner's key() is the one door to a context.  A
translation unit that defines an owner itself gets that door, and mints any
context it wants.  So each owner is defined once, in the file that
scripts/host-owner-roster.txt names, and nowhere else.

THE RULE
    - A class defined in a namespace named host, by any spelling, is an owner
      definition.  The guard reads the enclosing namespaces from the parse and
      the qualifier from the class name, so `struct fe::host::X {}` after a
      namespace alias, `struct host::X {}` after a using-directive and
      `namespace foundation::effects::host { struct X {}; }` are all owner
      definitions.  A declaration without a body, such as a friend
      declaration, is not.
    - A qualifier resolves through each namespace alias of the whole scan,
      a chain of aliases included, and not only through the aliases of its
      own file.  So `struct h::X {}` is an owner definition when any file
      defines `namespace h = foundation::effects::host;`, and a file that
      spells only the alias is parsed too.  An alias name with two targets
      keeps both, so a spelling can only be found more often.
    - A class definition inside a macro body counts, read from the macro
      text, because the parse keeps a macro body as raw text.
    - Each owner definition must match a roster row by name and file.
    - Each roster row must match an owner definition.  A row whose file does
      not define the owner fails, so the roster cannot go stale.
    - A file under the scan roots that the parser cannot read fails, unless
      scripts/tsast.py lists it as unparseable, and then its text is read.

Negative-compile fixtures (a test directory named neg or *_neg) are out of
scope: a fixture that defines an owner to prove that the build refuses it is
the point of the fixture.

Exit 0 clean, 1 on an unlisted owner definition, a stale row or a parse
failure, 2 on a usage error, a bad roster or a failed self-test, 3 when the
parser kit is missing.
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
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

ROSTER = "scripts/host-owner-roster.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp", ".tpp"})
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
MACROS = ("preproc_def", "preproc_function_def")
# A namespace alias definition, read as text so that one lexical pass over
# every file builds the alias table before any parse.
ALIAS = re.compile(r"\bnamespace\s+(\w+)\s*=\s*((?:::\s*)?\w+(?:\s*::\s*\w+)*)\s*;")
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")


class Refused(Exception):
    """The roster is missing or malformed (exit 2)."""


def read_roster(root: Path) -> list[tuple[str, str]]:
    """Return (owner, path) for each row of the roster.

    Raises:
        Refused: If the roster is missing or a row is malformed
    """
    roster = root / ROSTER
    if not roster.is_file():
        raise Refused(f"{ROSTER} is missing, so no owner has a reviewed home.")
    rows = []
    for number, line in enumerate(roster.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = [field.strip() for field in line.split(" — ")]
        if len(fields) != 3 or not all(fields):
            raise Refused(f"{ROSTER}:{number} has {len(fields)} fields, and a row is OWNER — PATH — REASON.")
        rows.append((fields[0], fields[1]))
    return rows


def scope_files(root: Path) -> list[str]:
    """Return the C++ files under the scan roots that git does not ignore, without the negative fixtures."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = {p for p in listed.stdout.decode().split("\0") if p}
    else:
        paths = {str(p.relative_to(root)) for r in SCAN_ROOTS for p in (root / r).rglob("*") if p.is_file()}
    return sorted(p for p in paths if Path(p).suffix in SUFFIXES and not NEG_FIXTURE.search(p)
                  and (root / p).is_file())


def spelled(text: str) -> str:
    """Return a name as its tokens alone: comments and white space removed."""
    return re.sub(r"\s+", "", cxx_lex.blank(text)[0])


def segments(text: str) -> list[str]:
    """Return the names of a qualified spelling, without template arguments."""
    return [re.sub(r"<.*", "", part) for part in spelled(text).split("::") if part]


class Aliases:
    """The namespace aliases of the whole scan, by alias name.

    An alias name that two files define with two targets keeps both, so a
    spelling can only resolve to more paths, never to fewer.
    """

    def __init__(self) -> None:
        """Start with no alias."""
        self.targets: dict[str, set[tuple[str, ...]]] = {}

    def add(self, name: str, target: str) -> None:
        """Record one alias definition."""
        self.targets.setdefault(name, set()).add(tuple(segments(target)))

    def expand(self, path: list[str], depth: int = 0) -> list[list[str]]:
        """Return the path and each path that its first name reaches through an alias.

        Complexity: bounded by the depth limit of 8 on a chain of aliases,
        which also stops a cycle.
        """
        found = [path]
        if depth < 8 and path and path[0] in self.targets:
            for target in self.targets[path[0]]:
                found += self.expand(list(target) + path[1:], depth + 1)
        return found

    def host_names(self) -> set[str]:
        """Return each alias name that reaches a namespace named host."""
        return {name for name in self.targets if any(p and p[-1] == "host" for p in self.expand([name]))}


def names_host(path: list[str], aliases: Aliases) -> bool:
    """Return True when a class path, through any alias, puts the class directly in a namespace named host."""
    return any(len(candidate) >= 2 and candidate[-2] == "host" for candidate in aliases.expand(path))


def text_definitions(text: str, aliases: Aliases) -> list[tuple[str, int]]:
    """Return (owner, line) for each class head in raw text whose qualifier reaches a namespace host."""
    found = []
    for match in TEXT_HEAD.finditer(text):
        path = segments(match.group(1)) + [match.group(2)]
        if names_host(path, aliases):
            found.append((match.group(2), text.count("\n", 0, match.start()) + 1))
    return found


# A qualified class head, inside a macro body or an unparseable file.
TEXT_HEAD = re.compile(r"\b(?:class|struct|union)\s+((?:::\s*)?(?:\w+\s*::\s*)+)(\w+)\s*(?:final\s*)?[{:]")


def scope_of(node: tsast.Node) -> list[str]:
    """Return the names of the namespaces around a node, outermost first.  An inline namespace is transparent."""
    parts: list[str] = []
    current = node.parent
    while current is not None:
        if current.type == "namespace_definition" and not current.text.lstrip().startswith("inline"):
            name = current.child_by_field("name")
            if name is not None:
                parts[:0] = [p for p in spelled(name.text).split("::") if p]
        current = current.parent
    return parts


def definitions(tree: tsast.Tree, aliases: Aliases) -> list[tuple[str, int]]:
    """Return (owner name, line) for each class that the parse defines in a namespace named host.

    A relative qualifier is read two ways: from the enclosing namespaces and
    from the global namespace.  Either way can reach an alias, and a match
    through any of them counts, so a spelling can only be found more often.
    """
    found = []
    for node in tree.find(*CLASSES):
        name, body = node.child_by_field("name"), node.child_by_field("body")
        if name is None or body is None:
            continue
        written = spelled(name.text)
        qualifier = segments(written)
        paths = [qualifier] if written.startswith("::") else [scope_of(node) + qualifier, qualifier]
        if any(names_host(path, aliases) for path in paths):
            found.append((qualifier[-1], node.line))
    for node in tree.find(*MACROS):
        found += [(owner, node.line + line - 1) for owner, line in text_definitions(node.text, aliases)]
    return found


def scan(root: Path) -> tuple[list[tuple[str, str, int]], list[str]]:
    """Return every owner definition as (owner, path, line), and a line for each file that does not parse.

    Complexity: one lexical pass over each file, and one parse of each file
    that names a namespace host or an alias that reaches one.
    """
    texts = {}
    aliases = Aliases()
    for rel in scope_files(root):
        text, _ = cxx_lex.blank(cxx_lex.splice((root / rel).read_text(errors="replace"))[0], blank_literals=True)
        texts[rel] = text
        for match in ALIAS.finditer(text):
            aliases.add(match.group(1), match.group(2))
    hint = re.compile(r"\b(?:" + "|".join(sorted({"host"} | aliases.host_names())) + r")\b")
    hinted = [rel for rel, text in texts.items() if hint.search(text)]
    found: list[tuple[str, str, int]] = []
    problems: list[str] = []
    for tree in tsast.parse([root / rel for rel in hinted], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its "
                                f"owner definitions: {tree.diagnostic}")
                continue
            found += [(owner, rel, line) for owner, line in text_definitions(texts[rel], aliases)]
            continue
        found += [(owner, rel, line) for owner, line in definitions(tree, aliases)]
    return found, problems


def check(root: Path) -> int:
    """Compare the owner definitions in the tree with the roster, and report to stderr.

    Returns:
        0 clean, 1 on a finding, 2 on a bad roster
    """
    try:
        rows = read_roster(root)
    except Refused as exc:
        print(f"check-host-owners: {exc}", file=sys.stderr)
        return 2
    found, problems = scan(root)
    admitted = set(rows)
    for owner, rel, line in found:
        if (owner, rel) not in admitted:
            problems.append(f"UNLISTED  {rel}:{line} — defines host::{owner}, and {ROSTER} names no such home.  "
                            f"A definition outside the owner's reviewed file mints any context.  Delete it.")
    defined = {(owner, rel) for owner, rel, _ in found}
    for owner, rel in rows:
        if (owner, rel) not in defined:
            problems.append(f"STALE     {ROSTER}: {owner} — {rel} does not define host::{owner}.  Move the row "
                            f"to the file that defines it, or remove it.")
    for line in problems:
        print(f"check-host-owners: {line}", file=sys.stderr)
    if not problems:
        print(f"check-host-owners: {len(found)} owner definition(s), each in the file its roster row names.")
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
        throwaway_repo.init(root)
        write(root, "include/foundation/effects/Effect.h",
              "#pragma once\nnamespace foundation::effects {\nnamespace host {\n"
              "struct InitOwner { static int key(); };\nstruct BackgroundOwner;\n}\n"
              "struct Bg { friend struct ::foundation::effects::host::BackgroundOwner; };\n}\n")
        write(root, "include/foundation/effects/Owners.h",
              "#pragma once\nstruct foundation::effects::host::BackgroundOwner final { static int key(); };\n")
        write(root, ROSTER, "# planted\n"
              "InitOwner — include/foundation/effects/Effect.h — the init context\n"
              "BackgroundOwner — include/foundation/effects/Owners.h — the background context\n")
        write(root, "src/Uses.cpp", "int n = foundation::effects::host::InitOwner::key();\n")
        write(root, "src/OtherAlias.cpp", "namespace q = foundation::effects;\nstruct q::Unrelated {};\n"
                                          "namespace host_like = foundation::effects;\nstruct host_like::Other {};\n")
        expect(root, 0, "2 owner definition(s)",
               "each owner in its roster file, uses, a friend and a class through an alias of another namespace pass")

        forgeries = {
            "src/Qualified.cpp": "struct foundation::effects::host::InitOwner { static int key(); };\n",
            "src/Reopened.cpp": "namespace foundation::effects::host { struct BackgroundOwner {}; }\n",
            "src/Nested.cpp": "namespace foundation { namespace effects { inline namespace v1 {} namespace host {\n"
                              "class ForegroundOwner\n{\n};\n} } }\n",
            "src/Alias.cpp": "namespace fe = foundation::effects;\nstruct fe::host::InitOwner {};\n",
            "src/Using.cpp": "using namespace foundation::effects;\nstruct host::ForegroundOwner {};\n",
            "src/Macro.cpp": "#define FORGE struct foundation::effects::host::InitOwner {}\nFORGE;\n",
            "src/Commented.cpp": "struct foundation::effects::/* hidden */host::\n    InitOwner { };\n",
            "src/HostAlias.cpp": "namespace h = foundation::effects::host;\nstruct h::InitOwner {};\n",
            "src/ChainAlias.cpp": "namespace e = ::foundation::effects;\nnamespace h2 = e::host;\n"
                                  "namespace h3 = h2;\nstruct h3::ForegroundOwner {};\n",
            "src/MacroAlias.cpp": "namespace hm = foundation::effects::host;\n"
                                  "#define FORGE_ALIAS struct hm::BackgroundOwner {}\nFORGE_ALIAS;\n",
        }
        for rel, text in forgeries.items():
            write(root, rel, text)
            expect(root, 1, f"UNLISTED  {rel}", f"an owner defined outside the roster: {rel}", True)
            (root / rel).unlink()
        write(root, "include/foundation/effects/Alias.h", "#pragma once\nnamespace fh = foundation::effects::host;\n")
        write(root, "src/CrossFile.cpp", "#include <foundation/effects/Alias.h>\nstruct fh::InitOwner {};\n")
        expect(root, 1, "UNLISTED  src/CrossFile.cpp", "an owner defined through an alias that another file defines",
               True)
        (root / "include/foundation/effects/Alias.h").unlink()
        (root / "src/CrossFile.cpp").unlink()

        write(root, "test/foundation/neg/neg_forge_owner.cpp",
              "struct foundation::effects::host::InitOwner {};\n")
        write(root, "src/Declares.cpp", "namespace foundation::effects::host { struct InitOwner; }\n"
                                        "// struct foundation::effects::host::InitOwner {};\n"
                                        'const char* s = "struct host::InitOwner {}";\n')
        expect(root, 0, "2 owner definition(s)", "a negative fixture, a declaration, a comment and a string pass")

        write(root, ROSTER, "# planted\n"
              "InitOwner — include/foundation/effects/Effect.h — the init context\n"
              "BackgroundOwner — include/foundation/effects/Owners.h — the background context\n"
              "ForegroundOwner — include/foundation/effects/Owners.h — the foreground context\n")
        expect(root, 1, "STALE     scripts/host-owner-roster.txt: ForegroundOwner", "a row whose file lacks the "
               "definition", True)
        write(root, ROSTER, "# planted\n"
              "InitOwner — include/foundation/effects/Effect.h — the init context\n")
        expect(root, 1, "UNLISTED  include/foundation/effects/Owners.h", "an owner that no row names", True)
        write(root, ROSTER, "InitOwner — include/foundation/effects/Effect.h\n")
        expect(root, 2, "OWNER — PATH — REASON", "a malformed row", True)
        write(root, ROSTER, "# planted\n"
              "InitOwner — include/foundation/effects/Effect.h — the init context\n"
              "BackgroundOwner — include/foundation/effects/Owners.h — the background context\n")
        write(root, "src/Broken.cpp", "namespace host { struct X { int = ; }}}\n")
        expect(root, 1, "PARSE     src/Broken.cpp", "a file the parser cannot read fails", True)
        (root / "src/Broken.cpp").unlink()
        same = captured(root, root) == captured(root, Path("/"))
        print(f"  {'ok  ' if same else 'FAIL'} the report from / equals the report from the repository")
        if not same:
            failures.append("the report depends on the working directory")
        (root / ROSTER).unlink()
        expect(root, 2, "is missing", "a missing roster", True)
    for failure in failures:
        print(f"check-host-owners --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-host-owners --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, or run the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-host-owners.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-host-owners: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
