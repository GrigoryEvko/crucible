#!/usr/bin/env python3
"""check-detail-namespace — a detail namespace of foundation or fixy is used only inside its layer.

A function in a namespace named detail does the work of a door with no
gate of its own: the gate is the public door that calls it.  A file that
names the detail function directly skips the gate.  So a use of
::foundation::...::detail or ::fixy::...::detail outside the two layers is
refused.  The layers are include/foundation, include/fixy and their sources,
src/foundation and src/fixy.  Every other C++ file under the scan roots is
in scope: include/crucible, src, vessel, bench, tools, examples, fuzz and
test.

WHAT COUNTS AS A USE
    The guard reads each file from the parse of scripts/tsast.py, and it
    resolves each name the way C++ lookup can resolve it.  A name counts
    when one of its readings passes through a detail namespace of a layer:
      - A qualified name, with or without the leading ::, anywhere in the
        file: a type, a call, a template argument, a base class, a default
        template argument, a decltype.  An unqualified start is read in the
        global namespace, in each enclosing namespace and in each namespace
        that a using-directive opens from where it stands to the end of its
        block, so `detail::key` inside `namespace foundation::effects` and
        after `using namespace foundation::effects;` both count.  A reading
        through an enclosing namespace or a directive counts only when the
        name itself spells detail and the layers declare that namespace, so
        `crucible::detail::x` after a directive of a layer is not a use.
      - A name that starts with a namespace alias is read through the
        target of the alias, chains of aliases included.  An alias of the
        file itself wins when it is visible where the name stands.  An alias
        at namespace scope in a file that the file includes, directly or
        through other includes, applies when the name stands inside the
        namespace of the alias.  So an alias that a header defines still
        marks a use in a file that includes it, and so does an alias of the
        detail namespace itself.
      - A namespace alias: its target counts as a use.
      - A using-declaration and a using-directive of a detail namespace.
      - A namespace definition inside a detail namespace of a layer.
      - A name in a macro body, read from its preprocessing tokens, because
        the parse keeps a macro body as raw text.  The tokens come from the
        whole replacement list, so a comment inside it hides nothing.
    A comment, a string literal and a raw string are not uses.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A call that argument-dependent lookup resolves to a detail function
      when no name in the file names a detail namespace: the argument type
      reaches the file through a public API.  The layer must keep a detail
      function out of the reach of that lookup, for example as a hidden
      friend or a function object.  A file that names the argument type
      through the detail namespace is refused as usual.
    - A name that a macro builds from pieces with ##.
    - An alias in a header that the include is not resolved to: a system
      header, a computed include, or a path outside the including
      directory, include/ and test/.
    - A negative-compile fixture, a file under a test directory named neg
      or *_neg.  It must fail to compile, so it builds nothing.

THE ALLOWLIST
    scripts/detail-namespace-allowlist.txt admits the reviewed uses in test
    files.  A row is `PATH NAMESPACE xN — REASON`, where NAMESPACE is the
    detail namespace, for example ::foundation::effects::detail, and N is
    the number of uses.  The key is the content, not a line.  Only a file
    under test/ may take a row.  More uses than a row admits fail, and a
    row that admits more uses than the file has is stale, so the list only
    shrinks.

The guard parses every C++ file of the scan roots and of the layers once,
because an alias in any header of an include closure can open a detail
namespace.  A file in scope that the parser cannot read fails the guard.

Exit 0 clean, 1 on an unlisted use, a surplus, a stale row or a parse
failure, 2 on a usage error, a bad allowlist or a failed self-test, 3 when
the parser kit is missing.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

ALLOWLIST = "scripts/detail-namespace-allowlist.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
LAYER_ROOTS = ("include/foundation/", "include/fixy/", "src/foundation/", "src/fixy/")
LAYERS = frozenset({"foundation", "fixy"})
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")
MACROS = ("preproc_def", "preproc_function_def")
ROW = re.compile(r"^(?P<path>\S+)\s+(?P<namespace>::\S+)(?:\s+x(?P<count>[1-9][0-9]*))?$")

# A namespace path, outermost name first.
NsPath = tuple[str, ...]
Position = tuple[int, int]


class Refused(Exception):
    """The allowlist is missing or malformed (exit 2)."""


class Name(NamedTuple):
    """One name of a file that can reach a detail namespace, as the parse gives it.

    at is the position where lookup happens, or None for a macro body,
    which can expand anywhere, so every directive of the file applies.
    """

    line: int
    is_global: bool
    parts: NsPath
    enclosing: NsPath
    at: Position | None


class Directive(NamedTuple):
    """One using-directive: its target, where it stands, and the end of the block that holds it.

    is_shared says that the directive is at namespace scope, so it applies
    in each later block of its namespace, not only to the end of its own.
    """

    start: Position
    end: Position
    is_global: bool
    target: NsPath
    enclosing: NsPath
    is_shared: bool


class AliasDef(NamedTuple):
    """One namespace alias of a file.

    scope_start and scope_end bound the block that the alias is visible in,
    and at is where the definition stands.  is_shared says that the alias is
    at namespace scope, so a file that includes this one sees it.
    """

    name: str
    is_global: bool
    target: NsPath
    enclosing: NsPath
    scope_start: Position
    scope_end: Position
    at: Position
    is_shared: bool


class FileFacts(NamedTuple):
    """What the scan keeps of one parsed file once its tree is gone.

    names is empty for a layer file: the scan reads a layer file only for
    its includes, its aliases and its directives.
    """

    rel: str
    includes: list[str]
    aliases: list[AliasDef]
    directives: list[Directive]
    names: list[Name]


# The scopes where a namespace alias is a namespace member, so that a file
# which includes its file can name it.
NAMESPACE_SCOPES = frozenset({"translation_unit", "declaration_list"})
# The directories a bracket include resolves against, after the directory of
# the including file for a quoted include.
INCLUDE_DIRS = ("include", "test")


def detail_namespace(path: NsPath) -> str | None:
    """Return the detail namespace that a path of a layer passes through, as ::a::b::detail, or None."""
    if not path or path[0] not in LAYERS or "detail" not in path[1:]:
        return None
    return "::" + "::".join(path[:path.index("detail", 1) + 1])


def joined(parts: tuple[str, ...]) -> NsPath:
    """Return name parts with each line splice inside them removed, as translation phase 2 does."""
    return tuple("".join(token.text for token in tsast.pp_tokens(part)) if "\\" in part else part for part in parts)


def enclosing_of(node: tsast.Node) -> NsPath:
    """Return the namespaces around a node, outermost first, an inline namespace left out."""
    return joined(tsast.namespace_path(node, skip_inline=True))


def prefixes(path: NsPath) -> list[NsPath]:
    """Return each enclosing namespace of a path, innermost first, for the readings of an unqualified name."""
    return [path[:depth] for depth in range(len(path), 0, -1)]


def is_within(enclosing: NsPath, scope: NsPath) -> bool:
    """Say whether a name that stands in one namespace sees a member of another namespace.

    An unnamed namespace is transparent, because its enclosing namespace
    reads its members through an implicit using-directive.
    """
    named = tuple(part for part in scope if part)
    return tuple(part for part in enclosing if part)[:len(named)] == named


def chosen(paths: list[NsPath]) -> tuple[NsPath, ...]:
    """Return the readings that an alias or a directive records: every layer reading, or else the first one."""
    layer = [path for path in paths if path and path[0] in LAYERS]
    return tuple(dict.fromkeys(layer)) if layer else tuple(paths[:1])


# One alias of the scan: the file that defines it and its index there.
AliasKey = tuple[str, int]


class Resolver:
    """Resolve the names of the scan the way C++ lookup can resolve them.

    An unqualified start reads through the aliases of its file, then the
    aliases at namespace scope of its include closure, then the global
    namespace, each enclosing namespace and each open using-directive.  A
    reading through an enclosing namespace or a directive counts only when
    the name itself spells detail and the layers declare that detail
    namespace.  Without that rule every name inside a reopened namespace
    would count, and a name such as crucible::detail::x after a directive of
    a layer namespace would read as a namespace that no layer declares.
    """

    def __init__(self, facts: dict[str, FileFacts], declared: frozenset[str]) -> None:
        """Index the aliases of every parsed file."""
        self.facts = facts
        self.declared = declared
        self.shared_names = frozenset(alias.name for fact in facts.values() for alias in fact.aliases
                                      if alias.is_shared)
        self._closures: dict[str, tuple[str, ...]] = {}
        self._bases: dict[AliasKey, tuple[NsPath, ...]] = {}
        self._targets: dict[tuple[str, int], tuple[NsPath, ...]] = {}

    def closure(self, rel: str) -> tuple[str, ...]:
        """Return every file that one file includes, directly or through other includes, sorted.

        Complexity: linear in the size of the include graph under the file, once for each file.
        """
        if rel not in self._closures:
            seen: set[str] = set()
            pending = list(self.facts[rel].includes) if rel in self.facts else []
            while pending:
                included = pending.pop()
                if included in seen or included == rel:
                    continue
                seen.add(included)
                pending.extend(self.facts[included].includes if included in self.facts else ())
            self._closures[rel] = tuple(sorted(seen))
        return self._closures[rel]

    def local_aliases(self, rel: str, name: str, enclosing: NsPath, at: Position | None) -> list[AliasKey]:
        """Return the alias of the file that a name starts with, where the name stands.

        An alias at namespace scope is visible after its definition in each
        block of its namespace, because a namespace block that closes and
        opens again is still the same namespace.  An alias in a function is
        visible to the end of its block.  The innermost visible alias wins.
        A macro body can expand anywhere, so every alias of the file with
        that name applies to it.
        """
        aliases = self.facts[rel].aliases
        named = [index for index, alias in enumerate(aliases) if alias.name == name]
        if at is None:
            return [(rel, index) for index in named]
        visible = [index for index in named if aliases[index].at < at and (
            is_within(enclosing, aliases[index].enclosing) if aliases[index].is_shared
            else aliases[index].scope_start <= at < aliases[index].scope_end)]
        if not visible:
            return []
        return [(rel, max(visible, key=lambda index: (not aliases[index].is_shared, len(aliases[index].enclosing),
                                                      aliases[index].scope_start, aliases[index].at)))]

    def shared_aliases(self, rel: str, name: str, enclosing: NsPath, at: Position | None) -> list[AliasKey]:
        """Return each alias of the include closure that a name can start with, where the name stands."""
        if name not in self.shared_names:
            return []
        found: list[AliasKey] = []
        for included in self.closure(rel):
            for index, alias in enumerate(self.facts[included].aliases):
                if alias.is_shared and alias.name == name and (
                        at is None or is_within(enclosing, alias.enclosing)):
                    found.append((included, index))
        return found

    def bases(self, key: AliasKey, visiting: frozenset[AliasKey]) -> tuple[NsPath, ...]:
        """Return the namespaces that one alias stands for, read at its definition; a cycle stops the walk."""
        if key in self._bases:
            return self._bases[key]
        if key in visiting:
            return ()
        rel, index = key
        alias = self.facts[rel].aliases[index]
        readings = self.readings(rel, alias.is_global, alias.target, alias.enclosing, alias.at, visiting | {key})
        result = chosen([path for path, _ in readings])
        self._bases[key] = result
        return result

    def opened(self, rel: str, enclosing: NsPath, at: Position | None,
               visiting: frozenset[AliasKey]) -> list[NsPath]:
        """Return each namespace that a using-directive of the file opens where a name stands.

        A directive at namespace scope applies after it in each block of its
        namespace.  A directive in a function applies to the end of its block.
        """
        found: list[NsPath] = []
        for index, directive in enumerate(self.facts[rel].directives):
            if at is not None and not (directive.start < at and (
                    is_within(enclosing, directive.enclosing) if directive.is_shared
                    else at < directive.end)):
                continue
            if (rel, index) not in self._targets:
                readings = self.readings(rel, directive.is_global, directive.target, directive.enclosing,
                                         directive.start, visiting)
                self._targets[(rel, index)] = chosen([path for path, _ in readings])
            found += self._targets[(rel, index)]
        return found

    def readings(self, rel: str, is_global: bool, parts: NsPath, enclosing: NsPath, at: Position | None,
                 visiting: frozenset[AliasKey] = frozenset()) -> list[tuple[NsPath, bool]]:
        """Return each (path, through a prefix) that a name of one file can resolve to."""
        if not parts:
            return []
        if is_global:
            return [(parts, False)]
        local = self.local_aliases(rel, parts[0], enclosing, at)
        if local:
            return [(base + parts[1:], False) for key in local for base in self.bases(key, visiting)]
        found = [(parts, False)]
        for key in self.shared_aliases(rel, parts[0], enclosing, at):
            found += [(base + parts[1:], False) for base in self.bases(key, visiting)]
        opened = self.opened(rel, enclosing, at, visiting)
        return found + [(prefix + parts, True) for prefix in prefixes(enclosing) + opened]

    def reaches(self, rel: str, name: Name) -> str | None:
        """Return the detail namespace that a name of one file reaches, or None."""
        for path, through_prefix in self.readings(rel, name.is_global, name.parts, name.enclosing, name.at):
            namespace = detail_namespace(path)
            if namespace and (not through_prefix or ("detail" in name.parts and namespace in self.declared)):
                return namespace
        return None


def outermost_qualified(tree: tsast.Tree) -> list[tsast.Node]:
    """Return each qualified_identifier that no other qualified_identifier holds as its name."""
    return [node for node in tree.find("qualified_identifier")
            if not (node.parent is not None and node.parent.type == "qualified_identifier" and node.field == "name")]


def resolved_includes(root: Path, rel: str, tree: tsast.Tree) -> list[str]:
    """Return each file of the scan that one file includes, relative to the root.

    A quoted include resolves against the directory of the file first, then
    every include resolves against INCLUDE_DIRS.  A system header and a
    computed include resolve to nothing.
    """
    found: list[str] = []
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        if path is None or path.type not in ("string_literal", "system_lib_string"):
            continue
        body = path.text[1:-1].strip()
        candidates = [(root / rel).parent / body] if path.type == "string_literal" else []
        candidates += [root / directory / body for directory in INCLUDE_DIRS]
        for candidate in candidates:
            if candidate.is_file():
                found.append(os.path.relpath(candidate.resolve(), root.resolve()))
                break
    return found


def file_facts(root: Path, rel: str, tree: tsast.Tree, *, with_names: bool) -> FileFacts:
    """Return the includes, aliases, directives and names of one parsed file.

    with_names false keeps no name: a layer file serves only its includes,
    its aliases and its directives.

    Complexity: linear in the number of nodes, plus the length of the macro bodies.
    """
    names: list[Name] = []
    aliases: list[AliasDef] = []
    directives: list[Directive] = []
    directive_nodes: set[Position] = set()
    for alias in tsast.namespace_aliases(tree):
        enclosing, target = enclosing_of(alias.node), joined(alias.target)
        aliases.append(AliasDef(joined((alias.name,))[0], alias.is_global, target, enclosing, alias.scope.start,
                                alias.scope.end, alias.node.start, alias.scope.type in NAMESPACE_SCOPES))
        names.append(Name(alias.node.line, alias.is_global, target, enclosing, alias.node.start))
    for using in tsast.using_names(tree):
        if using.is_directive:
            directive_nodes.add(using.node.start)
            enclosing, target = enclosing_of(using.node), joined(using.target)
            names.append(Name(using.node.line, using.is_global, target, enclosing, using.node.start))
            directives.append(Directive(using.node.start, using.scope.end, using.is_global, target, enclosing,
                                        using.scope.type in NAMESPACE_SCOPES))
    includes = resolved_includes(root, rel, tree)
    if not with_names:
        return FileFacts(rel, includes, aliases, directives, [])
    for node in outermost_qualified(tree):
        parent = node.parent
        if parent is not None and parent.type == "using_declaration" and parent.start in directive_nodes:
            continue
        parts = tsast.qualified_parts(node)
        if parts is not None:
            names.append(Name(node.line, parts[0], joined(parts[1]), enclosing_of(node), node.start))
    for definition in tree.find("namespace_definition"):
        body = definition.child_by_field("body")
        if body is not None and detail_namespace(enclosing_of(body)):
            names.append(Name(definition.line, True, enclosing_of(body), (), definition.start))
    for define in tree.find(*MACROS):
        values = [child for child in define.children if child.field == "value"]
        if not values:
            continue
        tokens = tsast.pp_tokens(tree.slice(values[0].start, values[-1].end), values[0].start[0])
        for is_global, parts, row in tsast.token_qualified_names(tokens):
            if len(parts) > 1 or (is_global and parts):
                names.append(Name(row + 1, is_global, parts, (), None))
    return FileFacts(rel, includes, aliases, directives, names)


def listed_files(root: Path, roots: tuple[str, ...]) -> list[str]:
    """Return the C++ files under the given roots that git does not ignore, relative to the root, sorted."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *roots], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = {p for p in listed.stdout.decode().split("\0") if p}
    else:
        paths = {str(p.relative_to(root)) for r in roots for p in (root / r).rglob("*") if p.is_file()}
    return sorted(p for p in paths if tsast.is_in_cpp_scope(p) and (root / p).is_file())


def scan(root: Path) -> tuple[Counter, dict[tuple[str, str], list[int]], list[str]]:
    """Return the count of uses for each (path, namespace), their lines, and each parse failure.

    Complexity: one parse of each C++ file under the scan roots and the layers.
    """
    layer = listed_files(root, LAYER_ROOTS)
    scope = [rel for rel in listed_files(root, SCAN_ROOTS)
             if not rel.startswith(LAYER_ROOTS) and not NEG_FIXTURE.search(rel)]
    in_scope = set(scope)
    declared: set[str] = set()
    facts: dict[str, FileFacts] = {}
    problems: list[str] = []
    for tree in tsast.parse([root / rel for rel in layer + scope], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if rel not in in_scope:
            declared |= {namespace for definition in tree.find("namespace_definition")
                         if (body := definition.child_by_field("body")) is not None
                         and (namespace := detail_namespace(enclosing_of(body)))}
        elif tree.diagnostic is not None:
            problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its uses of a "
                            f"detail namespace: {tree.diagnostic}")
            continue
        facts[rel] = file_facts(root, rel, tree, with_names=rel in in_scope)
    resolver = Resolver(facts, frozenset(declared))
    counts: Counter = Counter()
    lines: dict[tuple[str, str], list[int]] = {}
    for rel in scope:
        for name in facts[rel].names if rel in facts else ():
            namespace = resolver.reaches(rel, name)
            if namespace:
                counts[(rel, namespace)] += 1
                lines.setdefault((rel, namespace), []).append(name.line)
    return counts, lines, problems


def read_allowlist(root: Path) -> dict[tuple[str, str], tuple[int, int]]:
    """Return (admitted count, line) for each (path, namespace) row.

    Raises:
        Refused: If the allowlist is missing, a row is malformed, or a row names a file outside test/
    """
    listing = root / ALLOWLIST
    if not listing.is_file():
        raise Refused(f"{ALLOWLIST} is missing, so no test file may use a detail namespace.")
    rows: dict[tuple[str, str], tuple[int, int]] = {}
    for number, line in enumerate(listing.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        key, separator, reason = line.partition(" — ")
        match = ROW.match(key.strip())
        if not separator or not reason.strip() or match is None:
            raise Refused(f"{ALLOWLIST}:{number} is not PATH NAMESPACE xN — REASON.")
        if not match.group("path").startswith("test/"):
            raise Refused(f"{ALLOWLIST}:{number} names {match.group('path')}, and only a test file may take a row.")
        rows[(match.group("path"), match.group("namespace"))] = (int(match.group("count") or 1), number)
    return rows


def check(root: Path) -> int:
    """Compare the uses in the tree with the allowlist, and report to stderr.

    Returns:
        0 clean, 1 on a finding, 2 on a bad allowlist
    """
    try:
        rows = read_allowlist(root)
    except Refused as exc:
        print(f"check-detail-namespace: {exc}", file=sys.stderr)
        return 2
    counts, lines, problems = scan(root)
    for key in sorted(counts):
        rel, namespace = key
        admitted = rows.get(key, (0, 0))[0]
        if counts[key] > admitted:
            where = ", ".join(map(str, sorted(lines[key])))
            problems.append(f"REFUSED   {rel}:{where} — {counts[key]} use(s) of {namespace}, {admitted} admitted.  A "
                            f"detail namespace does the work of a door with no gate of its own.  Call the public "
                            f"door of the layer.")
    for key, (admitted, number) in sorted(rows.items(), key=lambda item: item[1][1]):
        if counts.get(key, 0) < admitted:
            problems.append(f"STALE     {ALLOWLIST}:{number} admits {admitted} use(s) of {key[1]} in {key[0]}, and "
                            f"the file has {counts.get(key, 0)}.  Lower the count or remove the row.")
    for line in problems:
        print(f"check-detail-namespace: {line}", file=sys.stderr)
    if not problems:
        print(f"check-detail-namespace: {sum(counts.values())} use(s) of a detail namespace outside the layers, "
              f"each on its reviewed row.")
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
        write(root, "include/foundation/effects/Key.h",
              "#pragma once\nnamespace foundation::effects {\nnamespace detail { struct Key {}; void helper(Key); }\n"
              "inline void frob(detail::Key key) { detail::helper(key); }\n}\n")
        write(root, "include/fixy/session/Core.h",
              "#pragma once\nnamespace fixy::session { namespace detail { struct Core {}; }\n"
              "using core_type = ::foundation::effects::detail::Key;\n}\n")
        write(root, "test/fixy/test_probe.cpp",
              "auto first = ::foundation::effects::detail::Key{};\nauto second = foundation::effects::detail::Key{};\n")
        write(root, "test/fixy/neg/neg_reach.cpp", "auto forged = ::foundation::effects::detail::Key{};\n")
        write(root, "src/Clean.cpp",
              "// ::foundation::effects::detail::Key is refused outside the layers.\n"
              'const char* s = "::foundation::effects::detail::Key";\n'
              'const char* r = R"(fixy::session::detail::Core)";\n'
              "namespace crucible::fixy::sched::detail { struct Pin {}; }\n"
              "auto pin = ::crucible::fixy::sched::detail::Pin{};\n"
              "namespace crucible { namespace detail { int own; } int n = detail::own; }\n"
              "void call() { foundation::effects::frob({}); }\n"
              "using namespace foundation::effects;\nauto other = crucible::detail::own;\n"
              '#define TEXT "::foundation::effects::detail::Key"\n'
              "#define OWN ::crucible::detail::Own\n"
              "using foundation::effects::frob;\n")
        # One directive into a detail namespace is one use, however many
        # names come after it.
        write(root, "test/fixy/test_directive.cpp",
              "void probe() {\n    using namespace ::foundation::effects::detail;\n    std::size_t a = 0;\n"
              "    std::vector<int> b;\n    helper(Key{});\n}\n")
        write(root, ALLOWLIST, "# planted\ntest/fixy/test_probe.cpp ::foundation::effects::detail x2 — a probe\n"
                               "test/fixy/test_directive.cpp ::foundation::effects::detail x1 — one directive\n")
        expect(root, 0, "3 use(s) of a detail namespace", "a use inside the layers, reviewed tests, a fixture, a "
                                                            "comment, a string, the old tree, a public door, a "
                                                            "macro string and a crucible detail after a layer "
                                                            "directive pass", True)
        (root / "test/fixy/test_directive.cpp").unlink()
        write(root, ALLOWLIST, "# planted\ntest/fixy/test_probe.cpp ::foundation::effects::detail x2 — a probe\n")

        forgeries = {
            "src/Qualified.cpp": "auto key = ::foundation::effects::detail::Key{};\n",
            "src/Unrooted.cpp": "auto key = foundation::effects::detail::Key{};\n",
            "src/UsingDeclaration.cpp": "using foundation::effects::detail::Key;\nvoid f() { frob(Key{}); }\n",
            "src/Alias.cpp": "namespace fe = ::foundation::effects;\nvoid f() { frob(fe::detail::Key{}); }\n",
            "src/AliasOfDetail.cpp": "namespace fd = foundation::effects::detail;\n",
            "src/AliasComment.cpp": "namespace fe = foundation:: /*x*/ effects;\nvoid f(fe::detail::Key);\n",
            "src/Directive.cpp": "using namespace fixy::session::detail;\n",
            "src/DirectiveOfParent.cpp": "using namespace foundation::effects;\nauto key = detail::Key{};\n",
            "src/DirectiveComment.cpp": "using namespace foundation:: /*c*/ effects;\nauto k = detail::Key{};\n",
            "src/Reopened.cpp": "namespace foundation::effects::detail { int forged; }\n",
            "src/ReopenComment.cpp": "namespace foundation:: /*c*/ effects::detail { int forged; }\n",
            "src/ReopenedParent.cpp": "namespace foundation { namespace effects {\nauto key = detail::Key{};\n} }\n",
            "src/Adl.cpp": "namespace fe = foundation::effects;\nvoid call(fe::detail::Key key) { helper(key); }\n",
            "src/ReopenedAlias.cpp": "namespace { namespace fe = foundation::effects; }\n"
                                     "namespace { auto key = fe::detail::Key{}; }\n",
            "src/ReopenedDirective.cpp": "namespace probe { using namespace foundation::effects; }\n"
                                         "namespace probe { auto key = detail::Key{}; }\n",
            "src/UnnamedAlias.cpp": "namespace { namespace fe = foundation::effects; }\nauto key = fe::detail::Key{};\n",
            "src/Macro.cpp": "#define REACH ::fixy::session::detail::Core\nREACH core;\n",
            "src/SplitMacro.cpp": "#define REACH ::fixy::session:: /* c */ \\\n    detail::Core\nREACH core;\n",
            "src/MultiLine.cpp": "auto key = foundation /* hidden */ ::\n    effects::detail::Key{};\n",
            "src/SplicedPart.cpp": "auto key = foundation::effects::det\\\nail::Key{};\n",
            "src/SplicedAlias.cpp": "namespace fe = foundation::eff\\\nects;\nvoid f(fe::detail::Key);\n",
            "src/SplicedReopen.cpp": "namespace foundation::effects::det\\\nail { int forged; }\n",
            "src/TemplateArgument.cpp": "std::vector<::fixy::session::detail::Core> cores;\n",
            "src/ParenArgument.cpp": "auto k = ::foundation::effects::detail::Key<(1 > 0)>{};\n",
            "include/crucible/Base.h": "struct Derived : fixy::session::detail::Core {};\n",
            "vessel/Decltype.cpp": "decltype(::foundation::effects::detail::Key{}) key;\n",
        }
        for rel, text in forgeries.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"a use outside the layers: {rel}", True)
            (root / rel).unlink()

        # An alias that another file defines opens the namespace too, and
        # an alias of the detail namespace itself names it with no word
        # detail in the using file.
        cross = {
            "include/crucible/Alias.h": "#pragma once\nnamespace fe2 = ::foundation::effects;\n",
            "include/foundation/effects/Alias.h": "#pragma once\nnamespace fd2 = ::foundation::effects::detail;\n",
        }
        for rel, text in cross.items():
            write(root, rel, text)
        for rel, text, name in (
                ("src/UseAlias.cpp", "#include <crucible/Alias.h>\nauto k = fe2::detail::Key{};\n",
                 "a use through an alias of an included header"),
                ("src/UseLayerAlias.cpp", "#include <foundation/effects/Alias.h>\nauto k = fd2::Key{};\n",
                 "a use through an alias of the detail namespace that a layer header defines"),
                ("src/UseChained.cpp", '#include "Chain.h"\nauto k = fe4::detail::Key{};\n',
                 "a use through an alias of a header that another header includes")):
            write(root, "src/Chain.h", "#pragma once\n#include <crucible/Alias.h>\nnamespace fe4 = fe2;\n")
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", name, True)
            (root / rel).unlink()
        (root / "src/Chain.h").unlink()
        # An alias counts only where C++ can see it: in a file that includes
        # its header, unless the file defines an alias of the same name.
        write(root, "src/Unincluded.cpp", "auto k = fe2::detail::Key{};\n")
        write(root, "src/Shadowed.cpp", "#include <crucible/Alias.h>\nnamespace fe2 = ::crucible::effects;\n"
                                        "auto k = fe2::detail::Key{};\n")
        write(root, "src/OtherScope.cpp", "#include <crucible/Scoped.h>\nauto k = fs::detail::Key{};\n")
        write(root, "include/crucible/Scoped.h", "#pragma once\nnamespace crucible { namespace fs = "
                                                 "::foundation::effects; }\n")
        expect(root, 0, "2 use(s) of a detail namespace", "an alias from a header the file does not include, an "
                                                          "alias the file shadows and an alias of another "
                                                          "namespace scope pass", True)
        for rel in ("src/Unincluded.cpp", "src/Shadowed.cpp", "src/OtherScope.cpp", "include/crucible/Scoped.h"):
            (root / rel).unlink()
        for rel in cross:
            (root / rel).unlink()

        write(root, "test/fixy/test_probe.cpp",
              "auto first = ::foundation::effects::detail::Key{};\nauto second = foundation::effects::detail::Key{};\n"
              "auto third = ::foundation::effects::detail::Key{};\n")
        expect(root, 1, "REFUSED   test/fixy/test_probe.cpp:1, 2, 3 — 3 use(s)", "a use above the reviewed count",
               True)
        write(root, "test/fixy/test_probe.cpp", "auto first = ::foundation::effects::detail::Key{};\n")
        expect(root, 1, "STALE     scripts/detail-namespace-allowlist.txt:2", "a row above the count", True)
        write(root, "test/fixy/test_probe.cpp",
              "auto first = ::foundation::effects::detail::Key{};\nauto second = foundation::effects::detail::Key{};\n")
        write(root, ALLOWLIST, "# planted\nsrc/Clean.cpp ::foundation::effects::detail x1 — not a test\n")
        expect(root, 2, "only a test file may take a row", "a row outside test/", True)
        write(root, ALLOWLIST, "test/fixy/test_probe.cpp ::foundation::effects::detail x2\n")
        expect(root, 2, "is not PATH NAMESPACE xN — REASON", "a row with no reason", True)
        write(root, ALLOWLIST, "# planted\ntest/fixy/test_probe.cpp ::foundation::effects::detail x2 — a probe\n")
        write(root, "src/Broken.cpp", "namespace detail { struct X { int = ; }}}\n")
        expect(root, 1, "PARSE     src/Broken.cpp", "a file the parser cannot read fails", True)
        (root / "src/Broken.cpp").unlink()
        same = captured(root, root) == captured(root, Path("/"))
        print(f"  {'ok  ' if same else 'FAIL'} the report from / equals the report from the repository")
        if not same:
            failures.append("the report depends on the working directory")
        (root / ALLOWLIST).unlink()
        expect(root, 2, "is missing", "a missing allowlist", True)
    for failure in failures:
        print(f"check-detail-namespace --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-detail-namespace --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, or run the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-detail-namespace.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-detail-namespace: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
