#!/usr/bin/env python3
"""check-witness-roster — every witness type has one door, and a fixture stands on it.

Seven holes were one defect: a type that attests to a fact it cannot see
(a pin, a mapping, a descriptor, a permission, a context) and whose
constructor took raw data and trusted it.  Every fix was the same rule:
the constructor either consumes the evidence or IS the operation that
produces it.  This guard makes the rule mechanical.

THE ROSTER
    scripts/witness-roster.txt is the source of truth.  One line for each
    witness type, five fields separated by `|`:

        <type> | <header> | <forging expression> | <reason> | <status>

    type        the class as a caller spells it, template arguments
                included, for example fixy::Secret<int>.  Spell the CLASS,
                not an alias: the second regex comes from the last name
                segment, and the compiler prints the class it refused.
    header      the header that declares it, relative to include/
    expression  a direct construction a forger can write, with the raw data
                handed to the constructor.  No walk can derive it, which is
                why the roster exists.  forge::lvalue<T>() and
                forge::rvalue<T>() give a T& and a T&& of any type.
    reason      how the door refuses: private | deleted | no-match.  For an
                open entry, one sentence that says why the hole stays open,
                ending with a period.
    status      closed: the door is shut, and a fixture is generated that
                attempts the expression and must be REJECTED.  open: a
                known hole, recorded so the count is honest, with no
                fixture.

    From each closed entry, --gen writes
    test/fixy/neg/neg_witness_<slug>_direct_construction.cpp, and it writes
    the manifest test/fixy/neg/witness-fixtures.json: one record for each
    fixture, with its test name and its two required regexes.  The fixtures
    and the manifest are committed, and --check compares them with what the
    roster produces, so a roster edit with no regeneration is drift.
    test/fixy/CMakeLists.txt reads the manifest with string(JSON) and
    registers one negative-compile test for each record, so the slug and the
    regexes have one implementation, here.

THE WALK
    Every class and struct in include/fixy and include/foundation that
    shows a door shape is a witness, and --check fails when its qualified
    name is absent from the roster.  The walk reads the parse tree of the
    pinned tree-sitter kit (scripts/tsast.py), so a class head over two
    lines, a brace in a block comment or a raw string, and a declaration
    over several lines each read as the compiler reads them.  The shapes:

      - it befriends a `mint_` function
      - a constructor takes a passkey first: a parameter type whose name
        ends in `_key` or `Key`
      - it IS a passkey: its own name ends in `_key` or `Key`
      - a static member function returns `std::expected<Self, ...>`, in
        the leading or the trailing return type.  That is where the
        system call went when the constructor was closed
        (OwnedMmap::map_region, OwnedFd::open_path).
      - it befriends a named function that is not an operator or swap
      - it befriends a type other than itself.  A template that befriends
        its own other specializations does not count.

    The last two count only when the class also keeps a constructor out
    of public reach, which is not a copy, a move or a deleted one.  A
    friend beside a public constructor is a shortcut, not a door.

    Friends are not members, so no reflection query can enumerate them,
    and the walk reads the source.  A class that shows a shape and is not
    a witness still needs a roster line, and a line needs a fixture that is
    refused, so the reviewer sees the claim.

MATCHING
    Every generated fixture spells its construction with braces, `T{args}`,
    and GCC renders the constructor it refused with parentheses,
    `T(args) [with ...]`.  So `\\bT\\(` is a token the compiler produces and
    the fixture source does not, and it is the second required regex of
    every fixture.  The first regex is the refusal reason of the roster.

Usage
    check-witness-roster.py --check      walk and drift, the CI gate
    check-witness-roster.py --gen        write the fixtures again from the roster
    check-witness-roster.py --walk       print each class that the walk counts
    check-witness-roster.py --self-test  plant each shape and each evasion

Exit 0 clean, 1 on an unrostered witness, a drifted fixture or a header the
parser cannot read, 2 on a malformed roster, a bad invocation or a failed
self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

REPO = tsast.REPO_ROOT
ROSTER = REPO / "scripts" / "witness-roster.txt"
FIXTURE_DIR = REPO / "test" / "fixy" / "neg"
INCLUDE = REPO / "include"
WALK_ROOTS = ("fixy", "foundation")
FIXTURE_GLOB = "neg_witness_*_direct_construction.cpp"
MANIFEST_NAME = "witness-fixtures.json"
REASONS = {
    "private": "is private within this context",
    "deleted": "use of deleted function",
    "no-match": "no matching function for call",
}
ANGLE_GROUP = re.compile(r"<[^<>]*>")
ROOT_PREFIX = re.compile(r"^(?:fixy|foundation)::")
PASSKEY_NAME = re.compile(r"(?:_key|Key)$")
PASSKEY_TYPE = re.compile(r"(?:[a-z_]*_key|[A-Za-z_]*Key)")
PREPROC_ARMS = frozenset({"preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef"})
CLASS_NODES = ("class_specifier", "struct_specifier")
FUNCTION_NODES = frozenset({"function_definition", "declaration", "field_declaration"})

# The brand a roster line can spell.  A branded type left with no brand is
# the erased identity, which scripts/check-brand-drain.py counts and does
# not let a new file add, so an expression that names one brand in two
# places needs one it can write down.  It is empty, so
# foundation::brand::IsBrand admits it.  Only a fixture that asks for it
# carries the declaration.
BRAND_DECL = """
// The brand a branded spelling in the roster line names.  It is empty,
// which is all foundation::brand::IsBrand asks of a brand.
struct brand {};
"""


class RosterError(ValueError):
    """The roster does not parse, so no verdict can stand."""


@dataclass(frozen=True)
class Entry:
    """One roster line."""

    line: int
    type_: str
    header: str
    expression: str
    reason: str
    status: str


def parse_roster(roster: Path, include: Path) -> list[Entry]:
    """Read the roster, and refuse a line that does not follow the grammar.

    Raises:
        RosterError: For a malformed line, naming the line
    """
    entries: list[Entry] = []
    for number, raw in enumerate(roster.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|")]
        if len(parts) != 5:
            raise RosterError(f"witness-roster.txt:{number}: expected 5 fields separated by '|', got {len(parts)}")
        type_, header, expression, reason, status = parts
        if status not in ("closed", "open"):
            raise RosterError(f"witness-roster.txt:{number}: status must be 'closed' or 'open', got '{status}'")
        if status == "closed" and reason not in REASONS:
            raise RosterError(f"witness-roster.txt:{number}: a closed entry's reason must be one of "
                              f"{', '.join(REASONS)}; got '{reason}'")
        if status == "open" and (reason in REASONS or not reason.endswith(".")):
            raise RosterError(f"witness-roster.txt:{number}: an open entry's reason must be one sentence that "
                              f"says why the hole stays open, ending with a period; got '{reason}'")
        if not (include / header).exists():
            raise RosterError(f"witness-roster.txt:{number}: header '{header}' does not exist under include/")
        entries.append(Entry(number, type_, header, expression, reason, status))
    return entries


def qualify(type_: str) -> str:
    """Return the type with each template argument list removed and the root namespace dropped.

    fixy::Secret<int> gives Secret, and
    fixy::concurrent::PermissionedMpscChannel<int, 8>::ProducerHandle gives
    concurrent::PermissionedMpscChannel::ProducerHandle.  The walk and the
    roster share this key.
    """
    stripped = type_
    while True:
        shorter = ANGLE_GROUP.sub("", stripped)
        if shorter == stripped:
            break
        stripped = shorter
    return ROOT_PREFIX.sub("", stripped.strip())


def base_name(type_: str) -> str:
    """Return the last name segment, which the compiler prints before `(` when it refuses the constructor."""
    return qualify(type_).rsplit("::", 1)[-1]


def slug(type_: str) -> str:
    """Return the file-name slug of a type."""
    return re.sub(r"[^a-z0-9]+", "_", qualify(type_).lower()).strip("_")


def fixture_name(type_: str) -> str:
    """Return the file name of the fixture of a type."""
    return f"neg_witness_{slug(type_)}_direct_construction.cpp"


def manifest_text(entries: list[Entry]) -> str:
    """Return the manifest that the closed entries produce, the input of test/fixy/CMakeLists.txt.

    One record for each closed entry, sorted by test name: the test name,
    the refusal reason, and the constructor as the compiler prints it.
    """
    records = sorted(({"name": fixture_name(entry.type_).removesuffix(".cpp"),
                       "reason": REASONS[entry.reason],
                       "constructor": rf"\b{re.escape(base_name(entry.type_))}\("}
                      for entry in entries if entry.status == "closed"), key=lambda record: record["name"])
    return json.dumps({"generator": "scripts/check-witness-roster.py --gen", "fixtures": records},
                      indent=2) + "\n"


def render(entry: Entry) -> str:
    """Return the fixture text that a closed entry produces."""
    brand_decl = BRAND_DECL if "forge::brand" in entry.expression else ""
    return f"""// GENERATED by scripts/check-witness-roster.py --gen from
// scripts/witness-roster.txt.  Edit the roster, not this file.
//
// {entry.type_} attests to a fact it cannot see.  The expression below is the
// raw data a forger would hand its constructor, and the fixture stands
// on the door staying shut: the construction must be refused, and the
// refusal must read "{REASONS[entry.reason]}".
//
// The second required diagnostic is the constructor as the compiler
// renders it, with parentheses.  The source spells it with braces, so
// the match cannot be satisfied by the fixture's own text.

#include <{entry.header}>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace forge {{
// Stand-ins for values a forger would have to hand over.  Neither
// returns: the fixture is compiled, never run.
template <class T> [[gnu::noinline]] T& lvalue() noexcept {{ std::abort(); }}
template <class T> [[gnu::noinline]] T&& rvalue() noexcept {{ std::abort(); }}
{brand_decl}}}  // namespace forge

int main() {{
    [[maybe_unused]] auto forged = {entry.expression};
    return 0;
}}
"""


# ── The walk ───────────────────────────────────────────────────────────


NAME_LEAVES = frozenset({"identifier", "type_identifier", "field_identifier", "namespace_identifier",
                         "operator_name", "destructor_name"})
NAME_WRAPPERS = frozenset({"qualified_identifier", "template_type", "template_function", "template_method"})


def last_name(node: tsast.Node | None) -> str:
    """Return the last name segment of a type or declarator node, with no template arguments.

    The walk follows the `name` field through each qualified name and each
    template-id, so a comment or a line break inside the spelling cannot
    change the result.  A shape with no name, such as a primitive type or
    decltype, gives the empty string.
    """
    while node is not None and node.type in NAME_WRAPPERS:
        node = node.child_by_field("name")
    if node is None or node.type not in NAME_LEAVES:
        return ""
    return "".join(node.text.split())


def returns_expected_of(member: tsast.Node, declarator: tsast.Node, name: str) -> bool:
    """Report whether a member function returns `expected<name, ...>`, in the leading or the trailing type.

    The first template argument must name the class itself.  An argument
    such as `Self::Inner` names a nested class, so it does not count.
    """
    roots = [node for node in (member.child_by_field("type"), *declarator.children_of_type("trailing_return_type"))
             if node is not None]
    for root in roots:
        for found in [root, *root.descendants("template_type")]:
            if found.type != "template_type" or last_name(found) != "expected":
                continue
            arguments = found.child_by_field("arguments")
            listed = [child for child in arguments.children if child.type != "comment"] if arguments else []
            if listed and listed[0].type == "type_descriptor" and listed[0].child_by_field("declarator") is None \
                    and last_name(listed[0].child_by_field("type")) == name:
                return True
    return False


def members(body: tsast.Node) -> list[tsast.Node]:
    """Return the members of a class body in source order, through each #if arm and each template head."""
    found: list[tsast.Node] = []
    stack = list(reversed(body.children))
    while stack:
        node = stack.pop()
        if node.type in PREPROC_ARMS:
            stack.extend(reversed(node.children))
        elif node.type == "template_declaration":
            stack.extend(reversed([child for child in node.children if child.field != "parameters"]))
        else:
            found.append(node)
    return found


def function_declarator(node: tsast.Node) -> tsast.Node | None:
    """Return the function declarator of a member declaration, through pointer and reference declarators."""
    declarator = node.child_by_field("declarator")
    while declarator is not None and declarator.type in ("pointer_declarator", "reference_declarator"):
        declarator = declarator.child_by_field("declarator")
    return declarator if declarator is not None and declarator.type == "function_declarator" else None


def declared_name(declarator: tsast.Node) -> tsast.Node | None:
    """Return the name node of a function declarator."""
    return declarator.child_by_field("declarator")


def first_parameter(declarator: tsast.Node) -> tsast.Node | None:
    """Return the first parameter of a function declarator, or None."""
    parameters = declarator.child_by_field("parameters")
    if parameters is None:
        return None
    listed = [child for child in parameters.children if child.type in ("parameter_declaration",
                                                                        "optional_parameter_declaration")]
    return listed[0] if listed else None


def is_reference(parameter: tsast.Node) -> bool:
    """Report whether a parameter is declared as a reference."""
    declarator = parameter.child_by_field("declarator")
    return declarator is not None and declarator.type in ("reference_declarator", "abstract_reference_declarator")


def is_static(member: tsast.Node) -> bool:
    """Report whether a member declaration carries `static`."""
    return any(child.type == "storage_class_specifier" and child.text == "static" for child in member.children)


def qualified_class(node: tsast.Node) -> str:
    """Return the qualified name of a class, from its namespaces and enclosing classes, the root dropped."""
    parts: list[str] = [last_name(node.child_by_field("name"))]
    owner = node.parent
    while owner is not None:
        if owner.type in CLASS_NODES and owner.child_by_field("body") is not None:
            parts.insert(0, last_name(owner.child_by_field("name")))
        elif owner.type == "namespace_definition":
            name = owner.child_by_field("name")
            if name is not None:
                spelled = [part.text for part in name.descendants("namespace_identifier")] \
                    if name.type == "nested_namespace_specifier" else [name.text]
                parts[:0] = spelled
        owner = owner.parent
    return ROOT_PREFIX.sub("", "::".join(parts))


@dataclass
class Shapes:
    """The door shapes of one class."""

    strong: int = 0
    weak: int = 0
    closed_constructor: bool = False

    def mark(self, kind: str, line: int) -> None:
        """Record a shape at a line, keeping the first line of each kind."""
        if kind == "strong" and not self.strong:
            self.strong = line
        if kind == "weak" and not self.weak:
            self.weak = line


def class_shapes(node: tsast.Node, name: str) -> Shapes:
    """Read the door shapes of one class body.

    Complexity: linear in the size of the class body.
    """
    shapes = Shapes()
    if PASSKEY_NAME.search(name):
        shapes.mark("strong", node.line)
    access = "public" if node.type == "struct_specifier" else "private"
    body = node.child_by_field("body")
    assert body is not None
    for member in members(body):
        if member.type == "access_specifier":
            access = member.text.strip()
            continue
        if member.type == "friend_declaration":
            functions = list(member.descendants("function_declarator"))
            if functions:
                named = declared_name(functions[0])
                spelled = last_name(named) if named is not None else ""
                if spelled.startswith("mint_"):
                    shapes.mark("strong", member.line)
                elif named is not None and named.type != "operator_name" and spelled != "swap" \
                        and not spelled.startswith("operator"):
                    shapes.mark("weak", member.line)
                continue
            befriended = [child for child in member.children
                          if child.type in ("type_identifier", "qualified_identifier", "template_type")]
            if befriended and last_name(befriended[-1]) != name:
                shapes.mark("weak", member.line)
            continue
        if member.type not in FUNCTION_NODES:
            continue
        declarator = function_declarator(member)
        if declarator is None:
            continue
        named = declared_name(declarator)
        spelled = last_name(named) if named is not None else ""
        if spelled == name and member.child_by_field("type") is None:
            parameter = first_parameter(declarator)
            kind = parameter.child_by_field("type") if parameter is not None else None
            parameter_type = last_name(kind)
            if parameter_type != name and PASSKEY_TYPE.fullmatch(parameter_type):
                shapes.mark("strong", member.line)
            copies = parameter is not None and parameter_type == name and is_reference(parameter)
            deleted = bool(member.children_of_type("delete_method_clause"))
            if access != "public" and not copies and not deleted:
                shapes.closed_constructor = True
        elif is_static(member) and returns_expected_of(member, declarator, name):
            shapes.mark("strong", member.line)
    return shapes


def walk_witnesses(roots: list[Path]) -> tuple[dict[str, str], list[str]]:
    """Return each class with a door shape, as qualified name to file:line, and each header the parser cannot read.

    Complexity: linear in the node count of the headers under the roots.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    def shown_path(path: Path) -> Path:
        """Return a path relative to the repository when it lies inside it."""
        return path.relative_to(REPO) if path.is_relative_to(REPO) else path

    files = sorted(path for root in roots if root.is_dir() for path in root.rglob("*.h")
                   if tsast.is_in_cpp_scope(shown_path(path)))
    found: dict[str, str] = {}
    unread: list[str] = []
    for tree in tsast.parse(files, strict=False):
        shown = shown_path(Path(tree.path))
        if tree.diagnostic is not None:
            unread.append(f"UNREADABLE header: {shown.as_posix()} does not parse, so its door shapes are unknown.\n"
                          f"  {tree.diagnostic}")
            continue
        for node in tree.find(*CLASS_NODES):
            named = node.child_by_field("name")
            if node.child_by_field("body") is None or named is None:
                continue
            name = last_name(named)
            shapes = class_shapes(node, name)
            line = shapes.strong or (shapes.weak if shapes.closed_constructor else 0)
            if line:
                found.setdefault(qualified_class(node), f"{shown}:{line}")
    return found, unread


# ── The modes ──────────────────────────────────────────────────────────


def generate(entries: list[Entry], fixture_dir: Path) -> int:
    """Write the fixture of each closed entry, and delete each fixture whose entry is gone."""
    fixture_dir.mkdir(parents=True, exist_ok=True)
    wanted = set()
    for entry in entries:
        if entry.status != "closed":
            continue
        name = fixture_name(entry.type_)
        wanted.add(name)
        (fixture_dir / name).write_text(render(entry), encoding="utf-8")
    for stale in fixture_dir.glob(FIXTURE_GLOB):
        if stale.name not in wanted:
            stale.unlink()
    (fixture_dir / MANIFEST_NAME).write_text(manifest_text(entries), encoding="utf-8")
    print(f"check-witness-roster: wrote {len(wanted)} fixture(s) and {MANIFEST_NAME} to {fixture_dir}")
    return 0


def check(entries: list[Entry], fixture_dir: Path, roots: list[Path]) -> int:
    """Walk the roots, compare the fixtures with the roster, and print each failure."""
    failures: list[str] = []
    found, unread = walk_witnesses(roots)
    failures += unread
    rostered = {qualify(entry.type_) for entry in entries}
    for name, where in sorted(found.items()):
        if name not in rostered:
            failures.append(f"UNROSTERED witness: {name} ({where}) has a door shape (a mint_, named or type "
                            f"friend, a passkey, or a static expected<{name.rsplit('::', 1)[-1]}> factory) and is "
                            f"absent from scripts/witness-roster.txt.  Add it as closed with a forging expression, "
                            f"or as open with a sentence that says why the hole stays open.")
    seen: dict[str, int] = {}
    for entry in entries:
        name = fixture_name(entry.type_)
        path = fixture_dir / name
        if name in seen:
            failures.append(f"DUPLICATE: roster lines {seen[name]} and {entry.line} both produce {name}.")
        seen[name] = entry.line
        if entry.status == "closed":
            if not path.exists():
                failures.append(f"DRIFT: roster line {entry.line} ({entry.type_}) is closed but {name} does not "
                                f"exist.  Run --gen and commit the result.")
            elif path.read_text(encoding="utf-8") != render(entry):
                failures.append(f"DRIFT: {name} differs from what roster line {entry.line} produces.  Run --gen "
                                f"and commit the result.")
        elif path.exists():
            failures.append(f"DRIFT: roster line {entry.line} ({entry.type_}) is open but {name} exists.  An open "
                            f"entry has no fixture: close the entry or run --gen.")
    for stray in sorted(fixture_dir.glob(FIXTURE_GLOB)):
        if stray.name not in seen:
            failures.append(f"STRAY: {stray.name} has no roster line.  Run --gen, which removes it, or add the "
                            f"entry.")
    manifest = fixture_dir / MANIFEST_NAME
    if not manifest.is_file() or manifest.read_text(encoding="utf-8") != manifest_text(entries):
        failures.append(f"DRIFT: {MANIFEST_NAME} is missing or differs from what the roster produces, so ctest "
                        f"registers another fixture set.  Run --gen and commit the result.")
    if failures:
        print("\n".join(failures))
        print(f"\ncheck-witness-roster: {len(failures)} failure(s).")
        return 1
    open_count = sum(entry.status == "open" for entry in entries)
    print(f"check-witness-roster: clean — {len(found)} witness class(es) walked, {len(entries)} rostered "
          f"({len(entries) - open_count} closed, {open_count} open), every closed fixture current.")
    return 0


def build_source(build_dir: Path) -> Path | None:
    """Return the source tree that a build tree was configured from, or None when its cache does not say.

    Args:
        build_dir: A configured CMake build tree

    Returns:
        The resolved CMAKE_HOME_DIRECTORY of its CMakeCache.txt, or None
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:"):
            return Path(line.split("=", 1)[1]).resolve()
    return None


def newest_build_dir(root: Path = REPO) -> Path | None:
    """Return the configured build tree of this repository whose test registration is newest, or None.

    A shared worktree carries several build trees.  A tree configured from
    another source copy passes that copy on its -I flags, so its fixture
    compile reads headers that are not this repository's, and it is skipped.
    A tree configured before the last roster entry registers a fixture set
    that no longer matches the disk.  The newest wins, and the registration
    arm names staleness when even the newest lags.

    Args:
        root: The directory that holds the build trees, the repository
            except in the self-test
    """
    best: Path | None = None
    best_stamp = -1.0
    for candidate in sorted(root.glob("build*")):
        stamp = candidate / "test" / "fixy" / "CTestTestfile.cmake"
        if (candidate / "compile_commands.json").is_file() and stamp.is_file() \
                and build_source(candidate) == REPO.resolve() and stamp.stat().st_mtime > best_stamp:
            best, best_stamp = candidate, stamp.stat().st_mtime
    return best


def compile_command(build_dir: Path) -> list[str]:
    """Return the compiler and the flags that a committed fixture builds with, from one known target."""
    for entry in json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8")):
        if entry["file"].endswith("test/fixy/test_os_time.cpp"):
            parts = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
            kept, skip = [], False
            for part in parts[1:]:
                if skip:
                    skip = False
                    continue
                if part == "-o":
                    skip = True
                    continue
                if part == "-c" or part.endswith("test_os_time.cpp"):
                    continue
                kept.append(part)
            return [parts[0], *kept]
    raise RosterError(f"{build_dir}/compile_commands.json has no entry for test/fixy/test_os_time.cpp")


def self_test() -> int:
    """Plant each door shape and each evasion of the old line walk, and prove the verdicts.

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

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its output."""
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-witness-roster --self-test")
    with tempfile.TemporaryDirectory() as work:
        trees = Path(work)
        for name, source, stamp in (("build-this", REPO, 1_000), ("build-other", Path(work) / "copy", 2_000),
                                    ("build-nocache", None, 3_000)):
            (trees / name / "test" / "fixy").mkdir(parents=True)
            (trees / name / "compile_commands.json").write_text("[]", encoding="utf-8")
            registration = trees / name / "test" / "fixy" / "CTestTestfile.cmake"
            registration.write_text("", encoding="utf-8")
            os.utime(registration, (stamp, stamp))
            if source is not None:
                (trees / name / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\n",
                                                             encoding="utf-8")
        expect("a newer build tree of another source copy, or one with no cache, is not picked",
               newest_build_dir(trees) == trees / "build-this")
    entries = parse_roster(ROSTER, INCLUDE)
    roots = [INCLUDE / root for root in WALK_ROOTS]
    code, report = captured(lambda: check(entries, FIXTURE_DIR, roots))
    expect("the real roster passes --check", code == 0)
    if code != 0:
        print(report)

    with tempfile.TemporaryDirectory() as work:
        scratch = Path(work)
        planted = scratch / "include" / "planted"
        planted.mkdir(parents=True)
        (planted / "Shapes.h").write_text(
            "#pragma once\n"
            "#define PLANTED_ABI [[gnu::abi_tag(\"planted\")]]\n"
            "namespace fixy::planted {\n"
            "// The head carries an attribute macro, and the walk reads the name after it.\n"
            "class PLANTED_ABI MintFriend {\n"
            "    constexpr explicit MintFriend(int) noexcept {}\n"
            "    template <typename T> friend constexpr MintFriend mint_friend(T) noexcept;\n"
            "public:\n"
            "    MintFriend() = default;\n"
            "};\n"
            "class\n"
            "HeadOverTwoLines {\n"
            "    /* a brace in a block comment: { */\n"
            "    friend HeadOverTwoLines mint_two_lines() noexcept;\n"
            "};\n"
            "class RawString {\n"
            "    static constexpr const char* text = R\"(}}})\";\n"
            "    friend RawString mint_raw_string() noexcept;\n"
            "};\n"
            "struct Passkey {\n"
            "    explicit Passkey(\n"
            "        detail::passkey_key) noexcept;\n"
            "};\n"
            "struct Factory {\n"
            "    static auto open_path(int) noexcept -> std::expected<Factory, int>;\n"
            "};\n"
            "struct LeadingFactory {\n"
            "    static std::expected<LeadingFactory, int> open(int) noexcept;\n"
            "};\n"
            "struct NestedResult {\n"
            "    struct Inner {};\n"
            "    static auto make() noexcept -> std::expected<NestedResult::Inner, int>;\n"
            "};\n"
            "struct plain_key {};\n"
            "class Handle {\n"
            "    Handle(int) noexcept;\n"
            "    friend class Owner;\n"
            "};\n"
            "class Opener {\n"
            "    explicit Opener(int) noexcept;\n"
            "    friend Opener open_dirfd(int) noexcept;\n"
            "};\n"
            "namespace detail { struct Outer { class Nested { friend Nested mint_nested(); }; }; }\n"
            "class ShortcutFriend {\n"
            "public:\n"
            "    explicit ShortcutFriend(int) noexcept;\n"
            "    friend class Owner;\n"
            "};\n"
            "class OwnSpecializations {\n"
            "    OwnSpecializations(int) noexcept;\n"
            "    template <class U> friend class OwnSpecializations;\n"
            "};\n"
            "class Vocabulary {\n"
            "    Vocabulary(int) noexcept;\n"
            "    friend bool operator==(Vocabulary const&, Vocabulary const&) = default;\n"
            "    friend void swap(Vocabulary&, Vocabulary&) noexcept;\n"
            "};\n"
            "class CopyOnly {\n"
            "    CopyOnly(CopyOnly const&) = default;\n"
            "    CopyOnly(int) = delete;\n"
            "    friend class Owner;\n"
            "};\n"
            "#if 0\n"
            "class DeadArm { friend DeadArm mint_dead(); };\n"
            "#endif\n"
            "// class InComment { friend InComment mint_in_comment(); };\n"
            "}  // namespace fixy::planted\n",
            encoding="utf-8",
        )
        found, unread = walk_witnesses([planted])
        expect("the planted header parses", not unread)
        for name, label in (
                ("planted::MintFriend", "a mint_ friend behind an attribute macro"),
                ("planted::HeadOverTwoLines", "a class head over two lines, with a brace in a block comment"),
                ("planted::RawString", "a class whose raw string holds braces"),
                ("planted::Passkey", "a passkey constructor over two lines"),
                ("planted::Factory", "a static factory with a trailing expected return"),
                ("planted::LeadingFactory", "a static factory with a leading expected return"),
                ("planted::plain_key", "a passkey type"),
                ("planted::Handle", "a type friend beside a private constructor"),
                ("planted::Opener", "a named friend function beside a private constructor"),
                ("planted::detail::Outer::Nested", "a nested class, qualified through its owner"),
                ("planted::DeadArm", "a class in a dead #if arm, which the kit reads")):
            expect(f"counted: {label}", name in found, True)
        for name, label in (
                ("planted::ShortcutFriend", "a type friend beside a public constructor"),
                ("planted::OwnSpecializations", "a template that befriends its own specializations"),
                ("planted::Vocabulary", "an operator and a swap friend"),
                ("planted::CopyOnly", "a type friend beside a copy and a deleted constructor"),
                ("planted::NestedResult", "a static function that returns an expected of a nested class"),
                ("planted::InComment", "a class in a comment")):
            expect(f"not counted: {label}", name not in found)

        (planted / "Broken.h").write_text("class Broken { void f() { g(1) { } } };\n", encoding="utf-8")
        code, report = captured(lambda: check([], scratch / "neg", [planted]))
        expect("a header the parser cannot read fails", code == 1 and "UNREADABLE header" in report, True)
        (planted / "Broken.h").unlink()

        code, report = captured(lambda: check(entries, FIXTURE_DIR, [*roots, planted]))
        expect("a planted witness that the roster does not name fails --check",
               code == 1 and "UNROSTERED witness: planted::MintFriend" in report, True)

        bad = scratch / "roster.txt"
        (scratch / "inc" / "planted").mkdir(parents=True)
        (scratch / "inc" / "planted" / "Open.h").write_text(
            "#pragma once\nnamespace planted {\nclass Open {\n    int v_ = 0;\npublic:\n"
            "    constexpr explicit Open(int v) noexcept : v_{v} {}\n};\n}  // namespace planted\n",
            encoding="utf-8")
        for text, label in (("planted::Open | planted/Open.h | planted::Open{42} | private\n", "four fields"),
                            ("planted::Open | planted/Open.h | planted::Open{42} | private | shut\n", "a bad status"),
                            ("planted::Open | planted/Open.h | planted::Open{42} | sealed | closed\n",
                             "a bad reason"),
                            ("planted::Open | planted/Open.h | planted::Open{42} | it stays open | open\n",
                             "an open reason with no period"),
                            ("planted::Open | planted/Gone.h | planted::Open{42} | private | closed\n",
                             "a header that does not exist")):
            bad.write_text(text, encoding="utf-8")
            try:
                parse_roster(bad, scratch / "inc")
                refused = False
            except RosterError:
                refused = True
            expect(f"a roster line with {label} is refused", refused, True)

        bad.write_text("planted::Open | planted/Open.h | planted::Open{42} | private | closed\n", encoding="utf-8")
        planted_entries = parse_roster(bad, scratch / "inc")
        neg = scratch / "neg"
        captured(lambda: generate(planted_entries, neg))
        fixture = neg / "neg_witness_planted_open_direct_construction.cpp"
        expect("--gen writes the planted fixture", fixture.is_file())
        manifest = json.loads((neg / MANIFEST_NAME).read_text(encoding="utf-8"))
        expect("--gen writes one manifest record with the name and both regexes of the fixture",
               manifest["fixtures"] == [{"name": "neg_witness_planted_open_direct_construction",
                                         "reason": "is private within this context",
                                         "constructor": r"\bOpen\("}])
        (neg / MANIFEST_NAME).write_text("{}\n", encoding="utf-8")
        code, report = captured(lambda: check(planted_entries, neg, [scratch / "inc" / "fixy"]))
        expect("an edited manifest is drift", code == 1 and f"DRIFT: {MANIFEST_NAME}" in report, True)
        captured(lambda: generate(planted_entries, neg))
        (neg / "neg_witness_gone_direct_construction.cpp").write_text("// stray\n", encoding="utf-8")
        code, report = captured(lambda: check(planted_entries, neg, [scratch / "inc" / "fixy"]))
        expect("a stray fixture fails --check", code == 1 and "STRAY: neg_witness_gone" in report, True)
        captured(lambda: generate(planted_entries, neg))
        expect("--gen removes the stray fixture", not (neg / "neg_witness_gone_direct_construction.cpp").exists())
        fixture.write_text(fixture.read_text(encoding="utf-8") + "// edited\n", encoding="utf-8")
        code, report = captured(lambda: check(planted_entries, neg, [scratch / "inc" / "fixy"]))
        expect("an edited fixture is drift", code == 1 and "DRIFT: neg_witness_planted_open" in report, True)
        captured(lambda: generate(planted_entries, neg))

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: check(planted_entries, neg, [scratch / "inc" / "fixy"]))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository",
               from_slash == captured(lambda: check(planted_entries, neg, [scratch / "inc" / "fixy"])))

        # The compiler arms.  A closed entry over a public constructor gives a
        # fixture that compiles, and that compile is the red the guard exists
        # to raise.  The mirror: a real closed fixture is refused with both
        # regexes that the roster implies.
        build_dir = Path(os.environ["WITNESS_BUILD_DIR"]) if os.environ.get("WITNESS_BUILD_DIR") \
            else newest_build_dir()
        if build_dir is None or not (build_dir / "compile_commands.json").is_file():
            expect("a configured build tree supplies the compile flags (set WITNESS_BUILD_DIR)", False)
        elif build_source(build_dir) != REPO.resolve():
            print(f"       {build_dir} was configured from {build_source(build_dir)}, not from {REPO}")
            expect("the build tree that supplies the compile flags is configured from this repository", False)
        else:
            command = compile_command(build_dir)
            opened = subprocess.run([*command, f"-I{scratch / 'inc'}", "-fdiagnostics-color=never", "-fsyntax-only",
                                     str(fixture)], cwd=REPO, capture_output=True, text=True)
            expect("a fixture over a public constructor compiles, which is the red", opened.returncode == 0, True)
            mirror = next(entry for entry in entries if entry.status == "closed")
            refused = subprocess.run([*command, "-fdiagnostics-color=never", "-fsyntax-only",
                                      str(FIXTURE_DIR / fixture_name(mirror.type_))], cwd=REPO,
                                     capture_output=True, text=True)
            output = refused.stdout + refused.stderr
            expect(f"the mirror over {mirror.type_} is refused with both regexes",
                   refused.returncode != 0 and re.search(REASONS[mirror.reason], output) is not None
                   and re.search(rf"\b{re.escape(base_name(mirror.type_))}\(", output) is not None, True)
            # ctest registers exactly the fixtures on disk.
            listed = subprocess.run(["ctest", "-N", "-R", "^neg_witness_.*_direct_construction$"], cwd=build_dir,
                                    capture_output=True, text=True).stdout
            registered = sorted(set(re.findall(r"neg_witness_[a-z0-9_]+_direct_construction", listed)))
            on_disk = sorted(path.stem for path in FIXTURE_DIR.glob(FIXTURE_GLOB))
            if registered != on_disk:
                stamp = build_dir / "test" / "fixy" / "CTestTestfile.cmake"
                if stamp.is_file() and ROSTER.stat().st_mtime > stamp.stat().st_mtime:
                    print(f"       {build_dir} was generated before the roster last changed, so its registration "
                          f"is stale rather than wrong.  Run: cmake -S {REPO} -B {build_dir}")
            expect("ctest registers exactly the fixtures on disk", registered == on_disk)

    if failures:
        print(f"check-witness-roster --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-witness-roster --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    roots = [INCLUDE / root for root in WALK_ROOTS]
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--check"]:
            return check(parse_roster(ROSTER, INCLUDE), FIXTURE_DIR, roots)
        if argv == ["--gen"]:
            return generate(parse_roster(ROSTER, INCLUDE), FIXTURE_DIR)
        if argv == ["--walk"]:
            found, unread = walk_witnesses(roots)
            for name, where in sorted(found.items()):
                print(f"{name}\t{where}")
            print("\n".join(unread), file=sys.stderr)
            return 1 if unread else 0
    except tsast.KitMissing as exc:
        print(f"check-witness-roster: {exc}", file=sys.stderr)
        return 3
    except RosterError as exc:
        print(f"check-witness-roster: {exc}", file=sys.stderr)
        return 2
    print("usage: check-witness-roster.py [--check | --gen | --walk | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
