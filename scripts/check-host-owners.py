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
      definition.  The guard reads the enclosing namespaces and the qualifier
      of the class name from the parse of scripts/tsast.py, so a comment, a
      line splice or an attribute cannot hide one.  `struct fe::host::X {}`
      after a namespace alias, `struct host::X {}` after a using-directive
      and `namespace foundation::effects::host { struct X {}; }` are all
      owner definitions.  An inline namespace is transparent.  A declaration
      without a body, such as a friend declaration, is not a definition.
    - A qualifier resolves through each namespace alias of the whole scan,
      a chain of aliases included, and not only through the aliases of its
      own file.  An alias name with two targets keeps both, so a spelling can
      only be found more often.
    - A class definition inside a macro body counts.  The guard parses each
      macro body on its own, through tsast.macro_bodies.  A body that does
      not parse, or whose parse a line splice inside a name has split, is
      read from its preprocessing tokens: a class key, its attributes and a
      qualified name before `{`, `:` or `final`.  A body of that kind that
      opens a namespace named host fails, because the guard cannot read what
      its expansion defines.
    - Each owner definition must match a roster row by name and file.
    - Each roster row must match an owner definition.  A row whose file does
      not define the owner fails, so the roster cannot go stale.
    - A file in scope that the parser cannot read fails.  A file that
      tsast.UNPARSEABLE lists is not C++ and is out of scope.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A namespace or a class whose name a macro supplies at the use, for
      example `namespace foundation::effects::NS {` where NS expands to host.
    - A class that a macro builds from pieces with ##.

Negative-compile fixtures (a test directory named neg or *_neg) are out of
scope: a fixture that defines an owner to prove that the build refuses it is
the point of the fixture.

The guard parses every C++ file under the scan roots, because an alias that
any of them defines can reach the namespace host.

Exit 0 clean, 1 on an unlisted owner definition, a stale row, a macro that
opens host or a parse failure, 2 on a usage error, a bad roster or a failed
self-test, 3 when the parser kit is missing.
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
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

ROSTER = "scripts/host-owner-roster.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
CLASS_KEYS = frozenset({"class", "struct", "union"})
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")
# The trees whose macro bodies go to the kit together.  A chunk bounds the
# memory that the file trees of one parse of macro bodies hold.
MACRO_CHUNK = 256

# A namespace path, outermost name first.
NsPath = tuple[str, ...]


class Refused(Exception):
    """The roster is missing or malformed (exit 2)."""


class Head(NamedTuple):
    """One class definition that can be an owner: its name, where it stands, and each path it can read as."""

    owner: str
    rel: str
    line: int
    paths: tuple[NsPath, ...]


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
    return sorted(p for p in paths if tsast.is_in_cpp_scope(p) and not NEG_FIXTURE.search(p) and (root / p).is_file())


def joined(parts: tuple[str, ...]) -> NsPath:
    """Return name parts with each line splice inside them removed, as translation phase 2 does."""
    return tuple("".join(token.text for token in tsast.pp_tokens(part)) if "\\" in part else part for part in parts)


class Aliases:
    """The namespace aliases of the whole scan, by alias name.

    An alias name that two files define with two targets keeps both, so a
    spelling can only resolve to more paths, never to fewer.
    """

    def __init__(self) -> None:
        """Start with no alias."""
        self.targets: dict[str, set[NsPath]] = {}

    def add(self, tree: tsast.Tree) -> None:
        """Record every alias of one parsed file."""
        for alias in tsast.namespace_aliases(tree):
            self.targets.setdefault(joined((alias.name,))[0], set()).add(joined(alias.target))

    def expand(self, path: NsPath, depth: int = 0) -> list[NsPath]:
        """Return the path and each path that its first name reaches through an alias.

        Complexity: bounded by the depth limit of 8 on a chain of aliases,
        which also stops a cycle.
        """
        found = [path]
        if depth < 8 and path and path[0] in self.targets:
            for target in sorted(self.targets[path[0]]):
                found += self.expand(target + path[1:], depth + 1)
        return found


def names_host(path: NsPath, aliases: Aliases) -> bool:
    """Return True when a class path, through any alias, puts the class directly in a namespace named host."""
    return any(len(candidate) >= 2 and candidate[-2] == "host" for candidate in aliases.expand(path))


def parsed_heads(rel: str, root: tsast.Node, row_of: callable) -> list[Head]:
    """Return each class definition under a node, with every path its name can read as.

    A relative qualifier reads two ways: from the enclosing namespaces and
    from the global namespace.  Either way can reach an alias, and a match
    through any of them counts, so a spelling can only be found more often.
    """
    found: list[Head] = []
    for node in root.descendants(*CLASSES):
        name, body = node.child_by_field("name"), node.child_by_field("body")
        written = None if name is None or body is None else tsast.qualified_parts(name)
        if written is None:
            continue
        is_global, qualifier = written[0], joined(written[1])
        enclosing = joined(tsast.namespace_path(node, skip_inline=True))
        paths = (qualifier,) if is_global else (enclosing + qualifier, qualifier)
        found.append(Head(qualifier[-1], rel, row_of(node) + 1, paths))
    return found


def attribute_end(tokens: list[tsast.Token], index: int) -> int:
    """Return the index after one attribute group that starts at a token, or the same index when none starts there.

    An attribute group is `[[...]]`, `alignas(...)`, `__attribute__((...))` or
    `__declspec(...)`, with its brackets balanced.
    """
    if index + 1 < len(tokens) and tokens[index].text == "[" and tokens[index + 1].text == "[":
        opening, closing = "[", "]"
    elif tokens[index].text in ("alignas", "__attribute__", "__declspec") and index + 1 < len(tokens) \
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


def qualified_name_at(tokens: list[tsast.Token], index: int) -> tuple[bool, NsPath, int]:
    """Return (is_global, parts, index after the name) for the qualified name that starts at a token."""
    is_global = index < len(tokens) and tokens[index].text == "::"
    index += is_global
    parts: list[str] = []
    while index < len(tokens) and tokens[index].kind == "identifier":
        parts.append(tokens[index].text)
        if index + 2 < len(tokens) and tokens[index + 1].text == "::" and tokens[index + 2].kind == "identifier":
            index += 2
            continue
        index += 1
        break
    return is_global, tuple(parts), index


def token_heads(rel: str, tokens: list[tsast.Token]) -> tuple[list[Head], list[int]]:
    """Return each class definition head in a token list, and the row of each namespace named host it opens.

    A head is a class key, its attribute groups and a qualified name, then
    `final`, `{` or `:`.  The body has no enclosing namespace, because a
    macro can expand anywhere, so a relative qualifier reads only as written.
    """
    heads: list[Head] = []
    opened: list[int] = []
    for index, token in enumerate(tokens):
        if token.text not in CLASS_KEYS and token.text != "namespace":
            continue
        after = index + 1
        while (skipped := attribute_end(tokens, after)) != after:
            after = skipped
        _, parts, after = qualified_name_at(tokens, after)
        if not parts or after >= len(tokens):
            continue
        if token.text == "namespace":
            if tokens[after].text == "{" and parts[-1] == "host":
                opened.append(token.row)
        elif tokens[after].text in ("final", "{", ":"):
            heads.append(Head(parts[-1], rel, token.row + 1, (parts,)))
    return heads, opened


def macro_heads(root: Path, trees: list[tsast.Tree]) -> tuple[list[Head], list[str]]:
    """Return each class definition in the macro bodies of some trees, and a line for each body that opens host.

    Complexity: one parse of every macro body of the trees, in at most three
    kit runs, plus a token pass over each body.
    """
    heads: list[Head] = []
    problems: list[str] = []
    for body in tsast.macro_bodies(trees):
        rel = str(Path(body.define.tree.path).relative_to(root))
        values = [child for child in body.define.children if child.field == "value"]
        raw = tsast.pp_tokens(body.define.tree.slice(values[0].start, values[-1].end), body.first_row)
        is_split = [token.text for token in raw] != [token.text for token in tsast.pp_tokens(body.text)]
        if body.is_parsed and not is_split:
            heads += parsed_heads(rel, body.root, lambda node, body=body: body.origin(node)[0])
            continue
        found, opened = token_heads(rel, raw)
        heads += found
        problems += [f"MACRO     {rel}:{row + 1} — the macro {body.name} opens a namespace named host, and the guard "
                     f"cannot read what its expansion defines.  Define the owner in its roster file without a "
                     f"macro." for row in opened]
    return heads, problems


def scan(root: Path) -> tuple[list[tuple[str, str, int]], list[str]]:
    """Return every owner definition as (owner, path, line), and a line for each problem.

    Complexity: one parse of each file under the scan roots, and one parse
    of each macro body.
    """
    aliases = Aliases()
    heads: list[Head] = []
    problems: list[str] = []
    chunk: list[tsast.Tree] = []
    for tree in tsast.parse([root / rel for rel in scope_files(root)], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its owner "
                            f"definitions: {tree.diagnostic}")
            continue
        aliases.add(tree)
        heads += parsed_heads(rel, tree.root, lambda node: node.start[0])
        if next(tree.find("preproc_def", "preproc_function_def"), None) is not None:
            chunk.append(tree)
        if len(chunk) >= MACRO_CHUNK:
            found, failed = macro_heads(root, chunk)
            heads, problems, chunk = heads + found, problems + failed, []
    found, failed = macro_heads(root, chunk)
    heads, problems = heads + found, problems + failed
    owners = [(head.owner, head.rel, head.line) for head in heads
              if any(names_host(path, aliases) for path in head.paths)]
    return sorted(owners, key=lambda owner: (owner[1], owner[2], owner[0])), problems


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
        write(root, "src/MacroText.cpp", '#define TEXT "struct foundation::effects::host::InitOwner {}"\n'
                                         "#define FRIEND friend struct ::foundation::effects::host::InitOwner;\n"
                                         "#define OTHER struct foundation::effects::Other {}\n")
        expect(root, 0, "2 owner definition(s)",
               "each owner in its roster file, uses, a friend, a class through an alias of another namespace, a "
               "macro string, a macro friend and a macro class elsewhere pass", True)

        forgeries = {
            "src/Qualified.cpp": "struct foundation::effects::host::InitOwner { static int key(); };\n",
            "src/Reopened.cpp": "namespace foundation::effects::host { struct BackgroundOwner {}; }\n",
            "src/Nested.cpp": "namespace foundation { namespace effects { inline namespace v1 {} namespace host {\n"
                              "class ForegroundOwner\n{\n};\n} } }\n",
            "src/InlineHost.cpp": "namespace foundation::effects { inline namespace v2 { namespace host {\n"
                                  "struct InitOwner {}; } } }\n",
            "src/Alias.cpp": "namespace fe = foundation::effects;\nstruct fe::host::InitOwner {};\n",
            "src/AliasComment.cpp": "namespace fe = foundation:: /*c*/ effects;\nstruct fe::host::InitOwner {};\n",
            "src/Using.cpp": "using namespace foundation::effects;\nstruct host::ForegroundOwner {};\n",
            "src/AttributeHead.cpp": "struct [[nodiscard]] foundation::effects::host::InitOwner {};\n",
            "src/Commented.cpp": "struct foundation::effects::/* hidden */host::\n    InitOwner { };\n",
            "src/SplicedName.cpp": "struct foundation::effects::host::Init\\\nOwner { };\n",
            "src/SplicedHost.cpp": "struct foundation::effects::ho\\\nst::InitOwner { };\n",
            "src/SplicedNamespace.cpp": "namespace foundation::effects::ho\\\nst { struct InitOwner {}; }\n",
            "src/SplicedAlias.cpp": "namespace hs = foundation::effects::ho\\\nst;\nstruct hs::InitOwner {};\n",
            "src/HostAlias.cpp": "namespace h = foundation::effects::host;\nstruct h::InitOwner {};\n",
            "src/ChainAlias.cpp": "namespace e = ::foundation::effects;\nnamespace h2 = e::host;\n"
                                  "namespace h3 = h2;\nstruct h3::ForegroundOwner {};\n",
            "src/Macro.cpp": "#define FORGE struct foundation::effects::host::InitOwner {}\nFORGE;\n",
            "src/MacroAlias.cpp": "namespace hm = foundation::effects::host;\n"
                                  "#define FORGE_ALIAS struct hm::BackgroundOwner {}\nFORGE_ALIAS;\n",
            "src/MacroNamespace.cpp": "#define FORGE namespace foundation::effects::host { struct InitOwner {}; }\n",
            "src/MacroAttribute.cpp": "#define FORGE struct [[nodiscard]] foundation::effects::host::InitOwner {}\n",
            "src/MacroSplice.cpp": "#define FORGE struct \\\n    foundation::effects::host::InitOwner {}\n",
            "src/MacroSplicedName.cpp": "#define FORGE struct foundation::effects::host::Init\\\nOwner {}\n",
            "src/MacroComment.cpp": "#define FORGE struct foundation:: /* c */ \\\n    effects::host::InitOwner {}\n",
            "src/MacroPasted.cpp": "#define FORGE(x) struct foundation::effects::host::InitOwner { int x##_; }\n",
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
        write(root, "src/MacroOpens.cpp", "#define OPEN_HOST namespace foundation::effects::host {\n")
        expect(root, 1, "MACRO     src/MacroOpens.cpp:1 — the macro OPEN_HOST opens a namespace named host",
               "a macro that opens host fails", True)
        (root / "src/MacroOpens.cpp").unlink()

        write(root, "test/foundation/neg/neg_forge_owner.cpp",
              "struct foundation::effects::host::InitOwner {};\n")
        write(root, "src/Declares.cpp", "namespace foundation::effects::host { struct InitOwner; }\n"
                                        "// struct foundation::effects::host::InitOwner {};\n"
                                        'const char* s = "struct host::InitOwner {}";\n')
        expect(root, 0, "2 owner definition(s)", "a negative fixture, a declaration, a comment and a string pass",
               True)

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
