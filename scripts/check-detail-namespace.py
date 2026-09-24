#!/usr/bin/env python3
"""check-detail-namespace — a detail namespace of foundation or fixy is used only inside its layer.

A function in a namespace named detail does the work of a door with no
gate of its own: the gate is the public door that calls it.  A file that
names the detail function directly skips the gate.  So a use of
::foundation::...::detail or ::fixy::...::detail outside the two layers is
refused.  The layers are include/foundation, include/fixy and their sources,
src/foundation and src/fixy.  Every other C or C++ file under the scan roots
is in scope: include/crucible, src, vessel, bench, tools, examples, fuzz and
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
      - A namespace alias: its target counts as a use, and a name that
        starts with the alias is read through the target.
      - A using-declaration and a using-directive of a detail namespace.
      - A namespace definition inside a detail namespace of a layer.
      - A name in a macro body, read from its tokens, because the parse
        keeps a macro body as raw text.
    A comment, a string literal and a raw string are not uses.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A call that argument-dependent lookup resolves to a detail function
      when no name in the file names a detail namespace: the argument type
      reaches the file through a public API.  The layer must keep a detail
      function out of the reach of that lookup, for example as a hidden
      friend or a function object.  A file that names the argument type
      through the detail namespace is refused as usual.
    - A name that a macro builds from pieces with ##.
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

A file in scope that the parser cannot read fails the guard, unless
scripts/tsast.py lists it as not C++, and then the token rule reads it.

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

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

ALLOWLIST = "scripts/detail-namespace-allowlist.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
LAYER_ROOTS = ("include/foundation/", "include/fixy/", "src/foundation/", "src/fixy/")
LAYERS = frozenset({"foundation", "fixy"})
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp", ".tpp"})
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")
# Only a text that names detail can use a detail namespace, so only such a
# file is parsed.  A backslash line splice can split the word.
DETAIL_WORD = re.compile(r"\bdetail\b|\\\r?\n")
MACROS = ("preproc_def", "preproc_function_def")
ROW = re.compile(r"^(?P<path>\S+)\s+(?P<namespace>::\S+)(?:\s+x(?P<count>[1-9][0-9]*))?$")
# A qualified name in a token stream: an optional leading ::, then names
# joined by ::.
TOKEN = re.compile(r"::|[A-Za-z_]\w*|\S")


class Refused(Exception):
    """The allowlist is missing or malformed (exit 2)."""


def spelled(text: str) -> str:
    """Return a piece of source as its tokens alone: comments and white space removed."""
    return re.sub(r"\s+", "", cxx_lex.blank(text)[0])


def without_template_arguments(text: str) -> str:
    """Return a spelled name with each balanced <...> removed."""
    out: list[str] = []
    depth = 0
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">" and depth > 0:
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def parts_of(text: str) -> tuple[bool, list[str]]:
    """Return (absolute, components) of a spelled qualified name."""
    name = without_template_arguments(spelled(text))
    absolute = name.startswith("::")
    return absolute, [part for part in name.split("::") if part]


def detail_namespace(path: list[str]) -> str | None:
    """Return the detail namespace that a path of a layer passes through, as ::a::b::detail, or None."""
    if not path or path[0] not in LAYERS or "detail" not in path[1:]:
        return None
    return "::" + "::".join(path[:path.index("detail", 1) + 1])


class Scope:
    """What a name in one file can resolve through: the aliases and the using-directives of the file.

    A reading through an enclosing namespace or a using-directive counts
    only when the name itself spells detail and the layers declare that
    detail namespace.  Without that rule every name inside a reopened
    namespace would count, and a name such as crucible::detail::x after a
    directive of a layer namespace would read as a namespace that no layer
    declares.
    """

    def __init__(self, declared: frozenset[str]) -> None:
        self.aliases: dict[str, list[str]] = {}
        # (start, end, target): a directive opens its target from where it
        # stands to the end of the block that holds it.
        self.directives: list[tuple[tuple[int, int], tuple[int, int], list[str]]] = []
        self.declared = declared

    def readings(self, absolute: bool, parts: list[str], enclosing: list[list[str]],
                 at: tuple[int, int] | None) -> list[tuple[list[str], bool]]:
        """Return each (path, through a prefix) that a name at a position can resolve to.

        An unqualified start has several readings.  A position of None, for
        a macro body that expands elsewhere, takes every directive of the
        file.
        """
        if not parts:
            return []
        if absolute:
            return [(parts, False)]
        if parts[0] in self.aliases:
            return [(self.aliases[parts[0]] + parts[1:], False)]
        opened = [target for start, end, target in self.directives if at is None or start <= at < end]
        return [(parts, False)] + [(prefix + parts, True) for prefix in enclosing + opened]

    def reaches(self, absolute: bool, parts: list[str], enclosing: list[list[str]],
                at: tuple[int, int] | None) -> str | None:
        """Return the detail namespace that a name reaches, or None."""
        for path, through_prefix in self.readings(absolute, parts, enclosing, at):
            namespace = detail_namespace(path)
            if namespace and (not through_prefix or ("detail" in parts and namespace in self.declared)):
                return namespace
        return None

    def resolved(self, absolute: bool, parts: list[str], enclosing: list[list[str]],
                 at: tuple[int, int] | None) -> list[str]:
        """Return the one reading that an alias or a directive records: a layer reading when one exists."""
        readings = [path for path, _ in self.readings(absolute, parts, enclosing, at)]
        return next((r for r in readings if r and r[0] in LAYERS), readings[0] if readings else [])


def namespace_path(node: tsast.Node) -> list[str]:
    """Return the path of the namespaces around a node, outermost first."""
    path: list[str] = []
    current = node.parent
    while current is not None:
        if current.type == "namespace_definition":
            name = current.child_by_field("name")
            if name is not None:
                path[:0] = parts_of(name.text)[1]
        current = current.parent
    return path


def prefixes(path: list[str]) -> list[list[str]]:
    """Return each enclosing namespace of a path, innermost first, for the readings of an unqualified name."""
    return [path[:i] for i in range(len(path), 0, -1)]


def macro_uses(text: str, scope: Scope, first_line: int) -> list[tuple[int, str]]:
    """Return (line, detail namespace) for each qualified name in a macro body that reaches one.

    Complexity: linear in the length of the text.
    """
    joined, joins = cxx_lex.splice(text)
    blanked, _ = cxx_lex.blank(joined, blank_literals=True)
    tokens = [(m.group(), m.start()) for m in TOKEN.finditer(blanked)]
    found = []
    i = 0
    while i < len(tokens):
        start = i
        absolute = tokens[i][0] == "::"
        if absolute:
            i += 1
        parts: list[str] = []
        while i < len(tokens) and re.fullmatch(r"[A-Za-z_]\w*", tokens[i][0]):
            parts.append(tokens[i][0])
            if i + 1 < len(tokens) and tokens[i + 1][0] == "::":
                i += 2
            else:
                i += 1
                break
        if len(parts) > 1 or (absolute and parts):
            namespace = scope.reaches(absolute, parts, [], None)
            if namespace:
                found.append((first_line + cxx_lex.line_of(blanked, joins, tokens[start][1]) - 1, namespace))
        if i == start:
            i += 1
    return found


def tree_uses(tree: tsast.Tree, declared: frozenset[str]) -> list[tuple[int, str]]:
    """Return (line, detail namespace) for each use in one parsed file.

    Complexity: linear in the number of nodes, times the depth of the
    namespaces for each qualified name.
    """
    scope = Scope(declared)
    found: list[tuple[int, str]] = []

    def record(node: tsast.Node, absolute: bool, parts: list[str], enclosing: list[list[str]]) -> None:
        """Record one use when the name reaches a detail namespace of a layer."""
        namespace = scope.reaches(absolute, parts, enclosing, node.start)
        if namespace:
            found.append((node.line, namespace))

    # Aliases and directives first, in order: a later name reads through them.
    for node in tree.find("namespace_alias_definition", "using_declaration"):
        enclosing = prefixes(namespace_path(node))
        if node.type == "namespace_alias_definition":
            name = node.child_by_field("name")
            target = next((c for c in node.children if c.type in ("nested_namespace_specifier",
                                                                    "namespace_identifier", "qualified_identifier")
                           and c.field != "name"), None)
            if name is None or target is None:
                continue
            absolute, parts = parts_of(target.text)
            record(node, absolute, parts, enclosing)
            scope.aliases[spelled(name.text)] = scope.resolved(absolute, parts, enclosing, node.start)
        elif spelled(node.text).startswith("usingnamespace"):
            absolute, parts = parts_of(re.sub(r"^\s*using\s+namespace\b", "", cxx_lex.blank(node.text)[0]).rstrip(";"))
            record(node, absolute, parts, enclosing)
            block_end = node.parent.end if node.parent is not None else node.end
            scope.directives.append((node.start, block_end, scope.resolved(absolute, parts, enclosing, node.start)))
    for node in tree.find("qualified_identifier"):
        if node.parent is not None and node.parent.type == "qualified_identifier":
            continue
        if node.parent is not None and node.parent.type == "using_declaration" \
                and spelled(node.parent.text).startswith("usingnamespace"):
            continue
        absolute, parts = parts_of(node.text)
        record(node, absolute, parts, prefixes(namespace_path(node)))
    for node in tree.find("namespace_definition"):
        name = node.child_by_field("name")
        if name is not None:
            path = namespace_path(node) + parts_of(name.text)[1]
            namespace = detail_namespace(path)
            if namespace:
                found.append((node.line, namespace))
    for node in tree.find(*MACROS):
        value = node.child_by_field("value")
        if value is not None:
            found += macro_uses(value.text, scope, value.line)
    return found


def scope_files(root: Path) -> list[str]:
    """Return the C and C++ files in scope: under the scan roots, outside the layers and the fixtures."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = {p for p in listed.stdout.decode().split("\0") if p}
    else:
        paths = {str(p.relative_to(root)) for r in SCAN_ROOTS for p in (root / r).rglob("*") if p.is_file()}
    return sorted(p for p in paths if Path(p).suffix in SUFFIXES and not p.startswith(LAYER_ROOTS)
                  and not NEG_FIXTURE.search(p) and (root / p).is_file())


def names_detail(root: Path, rel: str) -> bool:
    """Return whether a file names detail outside its comments and literals."""
    return DETAIL_WORD.search(cxx_lex.blank((root / rel).read_text(errors="replace"))[0]) is not None


def declared_detail_namespaces(root: Path) -> frozenset[str]:
    """Return each detail namespace that the layers declare, as ::a::b::detail.

    Complexity: one parse of each layer header that names detail.
    """
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *LAYER_ROOTS], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = [p for p in listed.stdout.decode().split("\0") if p]
    else:
        paths = [str(p.relative_to(root)) for r in LAYER_ROOTS for p in (root / r).rglob("*") if p.is_file()]
    layer_files = [p for p in sorted(paths) if Path(p).suffix in SUFFIXES and (root / p).is_file()
                   and names_detail(root, p)]
    declared = set()
    for tree in tsast.parse([root / rel for rel in layer_files], strict=False):
        for node in tree.find("namespace_definition"):
            name = node.child_by_field("name")
            if name is not None:
                namespace = detail_namespace(namespace_path(node) + parts_of(name.text)[1])
                if namespace:
                    declared.add(namespace)
    return frozenset(declared)


def scan(root: Path) -> tuple[Counter, dict[tuple[str, str], list[int]], list[str]]:
    """Return the count of uses for each (path, namespace), their lines, and each parse failure.

    Complexity: one lexical pass over each file in scope, and one parse of
    each file that names detail, in scope and in the layers.
    """
    declared = declared_detail_namespaces(root)
    hinted = [rel for rel in scope_files(root) if names_detail(root, rel)]
    counts: Counter = Counter()
    lines: dict[tuple[str, str], list[int]] = {}
    problems: list[str] = []
    for tree in tsast.parse([root / rel for rel in hinted], strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                problems.append(f"PARSE     {rel} — the parser cannot read it, so the guard cannot see its uses of "
                                f"a detail namespace: {tree.diagnostic}")
                continue
            uses = macro_uses((root / rel).read_text(errors="replace"), Scope(declared), 1)
        else:
            uses = tree_uses(tree, declared)
        for line, namespace in uses:
            counts[(rel, namespace)] += 1
            lines.setdefault((rel, namespace), []).append(line)
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
            where = ", ".join(map(str, lines[key]))
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
        subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
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
              "using namespace foundation::effects;\nauto other = crucible::detail::own;\n")
        # One directive into a detail namespace is one use, however many
        # names come after it.
        write(root, "test/fixy/test_directive.cpp",
              "void probe() {\n    using namespace ::foundation::effects::detail;\n    std::size_t a = 0;\n"
              "    std::vector<int> b;\n    helper(Key{});\n}\n")
        write(root, ALLOWLIST, "# planted\ntest/fixy/test_probe.cpp ::foundation::effects::detail x2 — a probe\n"
                               "test/fixy/test_directive.cpp ::foundation::effects::detail x1 — one directive\n")
        expect(root, 0, "3 use(s) of a detail namespace", "a use inside the layers, reviewed tests, a fixture, a "
                                                            "comment, a string, the old tree, a public door and a "
                                                            "crucible detail after a layer directive pass")
        (root / "test/fixy/test_directive.cpp").unlink()
        write(root, ALLOWLIST, "# planted\ntest/fixy/test_probe.cpp ::foundation::effects::detail x2 — a probe\n")

        forgeries = {
            "src/Qualified.cpp": "auto key = ::foundation::effects::detail::Key{};\n",
            "src/Unrooted.cpp": "auto key = foundation::effects::detail::Key{};\n",
            "src/UsingDeclaration.cpp": "using foundation::effects::detail::Key;\nvoid f() { frob(Key{}); }\n",
            "src/Alias.cpp": "namespace fe = ::foundation::effects;\nvoid f() { frob(fe::detail::Key{}); }\n",
            "src/AliasOfDetail.cpp": "namespace fd = foundation::effects::detail;\n",
            "src/Directive.cpp": "using namespace fixy::session::detail;\n",
            "src/DirectiveOfParent.cpp": "using namespace foundation::effects;\nauto key = detail::Key{};\n",
            "src/Reopened.cpp": "namespace foundation::effects::detail { int forged; }\n",
            "src/ReopenedParent.cpp": "namespace foundation { namespace effects {\nauto key = detail::Key{};\n} }\n",
            "src/Adl.cpp": "namespace fe = foundation::effects;\nvoid call(fe::detail::Key key) { helper(key); }\n",
            "src/Macro.cpp": "#define REACH ::fixy::session::detail::Core\nREACH core;\n",
            "src/MultiLine.cpp": "auto key = foundation /* hidden */ ::\n    effects::detail::Key{};\n",
            "src/TemplateArgument.cpp": "std::vector<::fixy::session::detail::Core> cores;\n",
            "include/crucible/Base.h": "struct Derived : fixy::session::detail::Core {};\n",
            "vessel/Decltype.cpp": "decltype(::foundation::effects::detail::Key{}) key;\n",
        }
        for rel, text in forgeries.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"a use outside the layers: {rel}", True)
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
