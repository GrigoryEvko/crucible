#!/usr/bin/env python3
"""check-no-ffast-math — no build flag and no per-TU override turns on a fast-math option.

CLAUDE.md §V bans the fast-math family, because each option breaks one of
the numerical invariants that replay depends on:

    -ffast-math                    the umbrella of the family
    -funsafe-math-optimizations    the blanket of the unsafe rewrites
    -fassociative-math             reorders floating-point additions
    -freciprocal-math              rewrites x/y as x*(1/y)
    -fno-signed-zeros              loses the sign of zero
    -ffinite-math-only             folds std::isnan to false
    -ffp-contract=fast             contracts across statements
    -Ofast                         -O3 plus -ffast-math

The floor is the INTERFACE library crucible_fp_strict (cmake/FpStrict.cmake).
This guard catches what could punch through it.

THE ENGINES
    Build flags   The compile database of a configured build
                  (compile_commands.json) holds each flag as the compiler
                  receives it, after CMake has expanded every generator
                  expression, variable and preset.  The guard reads it with
                  json, so a flag inside `$<$<CONFIG:Release>:...>` and a flag
                  in a CMake bracket comment each get the correct verdict.
                  The compile database holds no link line, and a fast-math
                  option at link time links crtfastmath.o, which sets the
                  flush-to-zero bits of the process.  So the guard also reads
                  the link fragments of each target from the codemodel reply
                  of the CMake file API, which the root CMakeLists.txt asks
                  for.  A build with no reply fails the guard.
                  Each CI build leg runs the guard on its own configuration.
                  CMakePresets.json is read with json as well, so a preset
                  that no leg configures still has its flag variables read.
    Overrides     A per-TU override is an `optimize` attribute (`[[gnu::...]]`
                  or `__attribute__((...))`), a `#pragma GCC optimize`, or a
                  `_Pragma` call.  The guard reads each one from the parse
                  tree of the pinned tree-sitter kit (utils/scripts/tsast.py),
                  including those inside a macro body, and it reads the
                  options from the string literals of the node.  A comment or
                  a string that only names an option holds no such node.

SCOPE
    Sources under include/, src/, vessel/, utils/tools/, test/, bench/ and
    examples/, the compile-database entries of those files, and the link
    line of each target.  A test or a bench links crucible, and a fast-math
    option on its link line links crtfastmath.o, which runs the determinism
    tests under flush-to-zero.  A file or a target in a vendored tree
    (third_party, external or vendor) or in a build tree is out of scope.

EXEMPTIONS
    `// NO-FFAST-MATH-OK: <reason>` on the row of an override exempts it.
    utils/scripts/no-ffast-math-allowlist.txt holds `path:key` rows, where the key
    is the text of the row without its comments, so an edit above the site
    does not move it.  A row that matches no live site is stale.  A banned
    build flag has no exemption.

Exit 0 clean, 1 on a violation or a parse failure, 2 on a stale row, a usage
error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from preprocessed import split_command  # noqa: E402

REPO_ROOT = tsast.REPO_ROOT
SOURCE_ROOTS = ("include", "src", "vessel", "utils/tools", "test", "bench", "examples")
EXEMPT_COMPONENTS = frozenset({"third_party", "external", "vendor"})
ALLOWLIST = "utils/scripts/no-ffast-math-allowlist.txt"
MARKER = "NO-FFAST-MATH-OK"
PRESETS = "CMakePresets.json"

# Each banned compiler flag, spelled as the driver receives it.
BANNED_FLAGS = frozenset({
    "-ffast-math", "-funsafe-math-optimizations", "-fassociative-math", "-freciprocal-math",
    "-fno-signed-zeros", "-ffinite-math-only", "-ffp-contract=fast", "-Ofast",
})
# The same options as the optimize attribute and pragma spell them.  GCC
# accepts an option with or without its leading `-f` or `-`.
BANNED_OPTIONS = frozenset({
    "fast-math", "unsafe-math-optimizations", "associative-math", "reciprocal-math",
    "no-signed-zeros", "finite-math-only", "fp-contract=fast", "Ofast",
})


@dataclass(frozen=True)
class Violation:
    """One banned flag or override."""

    where: str
    what: str
    key: str | None = None


# ── Build flags ─────────────────────────────────────────────────────────


def is_under(path: Path, root: Path) -> bool:
    """Return True when PATH lies inside ROOT once both are resolved."""
    return path.resolve().is_relative_to(root.resolve())


def is_exempt(path: Path, root: Path) -> bool:
    """Return True when a file under ROOT lies in a vendored tree or in a build tree."""
    rel = path.resolve().relative_to(root.resolve())
    return any(part in EXEMPT_COMPONENTS or part.startswith("build") for part in rel.parts[:-1])


def compile_flags(entry: dict) -> list[str]:
    """Return the argument vector of one compile-database entry."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return split_command(entry.get("command", ""))


def build_violations(build_dir: Path, root: Path) -> tuple[list[Violation], list[str]]:
    """Read the compile database of BUILD_DIR and return each banned flag, grouped by flag.

    Complexity: linear in the size of the database.

    Returns:
        The violations, and one line for each error that stops the read
    """
    database = build_dir / "compile_commands.json"
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [], [f"{database}: cannot read the compile database: {error}"]
    if not isinstance(entries, list) or not entries:
        return [], [f"{database}: the compile database holds no entry, so no flag was read"]
    by_flag: dict[str, list[str]] = {}
    under_root = 0
    for entry in entries:
        source = Path(entry.get("directory", "."), entry.get("file", ""))
        if not is_under(source, root):
            continue
        under_root += 1
        if is_exempt(source, root):
            continue
        for flag in compile_flags(entry):
            if flag in BANNED_FLAGS:
                by_flag.setdefault(flag, []).append(str(source.resolve().relative_to(root.resolve())))
    if under_root == 0:
        return [], [f"{database}: no entry compiles a file under {root}, so the database belongs to another "
                    f"source tree and no flag of this tree was read"]
    violations = []
    for flag, files in sorted(by_flag.items()):
        shown = ", ".join(sorted(set(files))[:5])
        more = len(set(files)) - 5
        suffix = f" and {more} more" if more > 0 else ""
        violations.append(Violation(f"{database}", f"{flag} on {len(set(files))} file(s): {shown}{suffix}"))
    return violations, []


def codemodel_targets(build_dir: Path) -> tuple[Path, list[Path]] | str:
    """Return the source directory and the target files of the codemodel reply of the CMake file API.

    CMake writes a new index file at each configure, and the name of the
    index holds its time, so the last name in sort order is the current one.

    Returns:
        The source directory that the codemodel names and the path of each
        target file, or one line that says why the reply cannot be read
    """
    reply = build_dir / ".cmake" / "api" / "v1" / "reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        return (f"{reply}: the build has no reply of the CMake file API, so no link flag was read. The root "
                f"CMakeLists.txt asks for the codemodel with cmake_file_api. Configure the build again")
    try:
        index = json.loads(indexes[-1].read_text(encoding="utf-8"))
        codemodel_file = next(item["jsonFile"] for item in index.get("objects", [])
                              if item.get("kind") == "codemodel" and item.get("version", {}).get("major") == 2)
        codemodel = json.loads((reply / codemodel_file).read_text(encoding="utf-8"))
    except StopIteration:
        return f"{indexes[-1]}: the reply holds no codemodel of version 2, so no link flag was read"
    except (OSError, KeyError, json.JSONDecodeError) as error:
        return f"{indexes[-1]}: cannot read the codemodel reply: {error}"
    targets = [reply / target["jsonFile"] for configuration in codemodel.get("configurations", [])
               for target in configuration.get("targets", [])]
    return Path(codemodel.get("paths", {}).get("source", "")), targets


def link_violations(build_dir: Path, root: Path) -> tuple[list[Violation], list[str]]:
    """Read the link line of each target from the CMake file API and return each banned flag, grouped by flag.

    A fast-math option on the link line makes GCC link crtfastmath.o, which
    sets the flush-to-zero and denormals-are-zero bits of the process at
    startup.  The compile database holds no link line, so only this read
    sees such an option.  A target in an exempt directory is out of scope,
    as its compile entries are.

    Complexity: linear in the size of the reply.

    Returns:
        The violations, and one line for each error that stops the read
    """
    found = codemodel_targets(build_dir)
    if isinstance(found, str):
        return [], [found]
    source, targets = found
    if not source.is_dir() or source.resolve() != root.resolve():
        return [], [f"{build_dir}: the file API reply belongs to the source tree {source}, not to {root}, so no "
                    f"link flag of this tree was read"]
    by_flag: dict[str, set[str]] = {}
    for target_file in targets:
        try:
            target = json.loads(target_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            return [], [f"{target_file}: cannot read the target reply: {error}"]
        directory = root / target.get("paths", {}).get("source", ".") / "CMakeLists.txt"
        if is_under(directory, root) and is_exempt(directory, root):
            continue
        for fragment in target.get("link", {}).get("commandFragments", []):
            try:
                tokens = shlex.split(fragment.get("fragment", ""))
            except ValueError:
                tokens = fragment.get("fragment", "").split()
            for flag in tokens:
                if flag in BANNED_FLAGS:
                    by_flag.setdefault(flag, set()).add(target.get("name", target_file.name))
    where = f"{build_dir / '.cmake' / 'api' / 'v1'}"
    return [Violation(where, f"{flag} on the link line of {len(names)} target(s): {', '.join(sorted(names))}")
            for flag, names in sorted(by_flag.items())], []


def preset_violations(root: Path) -> tuple[list[Violation], list[str]]:
    """Read CMakePresets.json with json and return each banned flag in a cache variable or the environment."""
    presets = root / PRESETS
    if not presets.is_file():
        return [], []
    try:
        document = json.loads(presets.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        return [], [f"{presets}: the presets file is not JSON: {error}"]
    violations = []
    for preset in document.get("configurePresets", []):
        name = preset.get("name", "<unnamed>")
        values: list[tuple[str, str]] = []
        for variable, value in preset.get("cacheVariables", {}).items():
            text = value.get("value", "") if isinstance(value, dict) else value
            if isinstance(text, str):
                values.append((f"cacheVariables.{variable}", text))
        for variable, value in preset.get("environment", {}).items():
            if isinstance(value, str):
                values.append((f"environment.{variable}", value))
        for label, text in values:
            try:
                tokens = shlex.split(text)
            except ValueError:
                tokens = text.split()
            for token in tokens:
                if token in BANNED_FLAGS:
                    violations.append(Violation(f"{PRESETS}", f"preset '{name}' {label} holds {token}"))
    return violations, []


# ── Per-TU overrides ────────────────────────────────────────────────────


def is_banned_option(option: str) -> bool:
    """Return True when one option of an optimize attribute or pragma is in the fast-math family.

    GCC reads `fast-math`, `-ffast-math`, `Ofast` and `-Ofast` alike, so the
    leading `-f` or `-` is dropped before the lookup.
    """
    spelled = option.strip()
    if spelled.startswith("-f"):
        spelled = spelled[2:]
    elif spelled.startswith("-"):
        spelled = spelled[1:]
    return spelled in BANNED_OPTIONS


def string_token_value(token: str) -> str:
    """Return the characters of a string-literal preprocessing token, without its prefix and quotes.

    The value is an option list of GCC, not C++ source.  A raw string gives
    its body, and an ordinary string gives its body with each backslash escape
    kept as written, which no option name contains.
    """
    opening = token.index('"')
    if opening > 0 and token[opening - 1] == "R":
        delimiter = token[opening + 1:token.index("(", opening)]
        return token[token.index("(", opening) + 1:len(token) - len(delimiter) - 2]
    return token[opening + 1:-1]


def banned_in_strings(values: list[str]) -> list[str]:
    """Return each banned option in a list of option strings, where one string can hold several options."""
    return [option.strip() for value in values for option in value.split(",") if is_banned_option(option)]


def attribute_strings(attribute: tsast.Attribute) -> list[str]:
    """Return the value of each string-literal argument of an attribute, read from its string_content nodes."""
    return ["".join(part.text for part in argument.descendants("string_content"))
            for argument in attribute.arguments]


def pragma_strings(text: str) -> list[str] | None:
    """Return the option strings of a `GCC optimize` pragma text, or None for another pragma.

    The text is lexed into preprocessing tokens: `GCC optimize ("a", "b")`
    and `GCC optimize "a"` give ["a", "b"] and ["a"].
    """
    tokens = tsast.pp_tokens(text)
    if len(tokens) < 2 or tokens[0].text != "GCC" or tokens[1].text != "optimize":
        return None
    return [string_token_value(token.text) for token in tokens[2:] if token.kind == "string"]


def tree_overrides(tree: tsast.Tree) -> Iterator[tuple[tsast.Node, list[str]]]:
    """Yield each optimize attribute, GCC optimize pragma and _Pragma of a tree that names a banned option."""
    for attribute in tsast.attributes(tree):
        if attribute.name == "optimize" and attribute.namespace in (None, "gnu"):
            banned = banned_in_strings(attribute_strings(attribute))
            if banned:
                yield attribute.node, banned
    for node, text in tsast.pragmas(tree):
        options = pragma_strings(text)
        if options is not None:
            banned = banned_in_strings(options)
            if banned:
                yield node, banned


def token_overrides(tokens: list[tsast.Token]) -> list[str]:
    """Return each banned option that a macro body names through `optimize(...)` or `_Pragma(...)`.

    The body did not parse as C++, so its preprocessing tokens decide.  An
    `optimize` or a `_Pragma` identifier takes every string token up to the
    closing parenthesis of its argument list.
    """
    banned: list[str] = []
    for index, token in enumerate(tokens):
        if token.kind != "identifier" or token.text not in ("optimize", "_Pragma"):
            continue
        depth, strings = 0, []
        for inner in tokens[index + 1:]:
            if inner.text == "(":
                depth += 1
            elif inner.text == ")":
                depth -= 1
                if depth == 0:
                    break
            elif inner.kind == "string":
                strings.append(string_token_value(inner.text))
        if token.text == "_Pragma":
            strings = [option for value in strings for option in (pragma_strings(value) or [])]
        banned += banned_in_strings(strings)
    return banned


def marked(tree: tsast.Tree, row: int) -> bool:
    """Return True when a comment on the row carries the marker and a reason."""
    return any(tsast.prose_text(comment).partition(f"{MARKER}:")[2].strip()
               for comment in tsast.comments_by_row(tree).get(row, ()))


def source_violations(root: Path) -> tuple[list[Violation], list[str]]:
    """Read every source under the roots and return each unmarked override, keyed by its content.

    Complexity: linear in the size of the sources.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = sorted(path for top in SOURCE_ROOTS if (root / top).is_dir()
                   for path in (root / top).rglob("*")
                   if path.is_file() and tsast.is_in_cpp_scope(path.relative_to(root))
                   and not any(part in EXEMPT_COMPONENTS or part.startswith("build")
                               for part in path.relative_to(root).parts[:-1]))
    trees = list(tsast.parse(files, strict=False))
    violations: list[Violation] = []
    failures: list[str] = []
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file, so its overrides are unknown. "
                            f"{tree.diagnostic.strip()}")
            continue
        for node, banned in tree_overrides(tree):
            row = node.start[0]
            if not marked(tree, row):
                violations.append(Violation(f"{rel}:{row + 1}", f"override names {', '.join(banned)}",
                                            f"{rel}:{tsast.site_key(tree, row)}"))
    for body in tsast.macro_bodies([tree for tree in trees if tree.diagnostic is None]):
        host = body.define.tree
        rel = Path(host.path).relative_to(root).as_posix()
        if body.is_parsed:
            found = [(body.origin(node)[0], banned) for node, banned in tree_overrides(body.tree)]
        else:
            banned = token_overrides(tsast.pp_tokens(body.text, body.first_row))
            found = [(body.define.start[0], banned)] if banned else []
        for row, banned in found:
            if not marked(host, row):
                violations.append(Violation(f"{rel}:{row + 1}", f"override in the macro {body.name} names "
                                            f"{', '.join(banned)}", f"{rel}:{tsast.site_key(host, row)}"))
    return violations, failures


# ── The check ───────────────────────────────────────────────────────────


def load_allowlist(path: Path) -> list[str]:
    """Return the `path:key` rows of the allowlist, without comments or blank lines."""
    if not path.is_file():
        return []
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.strip().startswith("#")]


def check(root: Path, build_dir: Path | None) -> int:
    """Run every scan and print the report.

    Returns:
        0 clean, 1 on a violation or a read failure, 2 on a stale allowlist row
    """
    violations: list[Violation] = []
    failures: list[str] = []
    scans = [preset_violations(root), source_violations(root)]
    if build_dir is not None:
        scans += [build_violations(build_dir, root), link_violations(build_dir, root)]
    for found, lost in scans:
        violations += found
        failures += lost
    rows = load_allowlist(root / ALLOWLIST)
    admitted = set(rows)
    live = {violation.key for violation in violations if violation.key is not None}
    reported = [violation for violation in violations if violation.key not in admitted]
    for failure in failures:
        print(f"NO-FFAST-MATH read failure: {failure}", file=sys.stderr)
    for violation in reported:
        suffix = f"  Allowlist key: {violation.key}" if violation.key else ""
        print(f"NO-FFAST-MATH violation: {violation.where}: {violation.what}.{suffix}", file=sys.stderr)
    stale = [row for row in rows if row not in live]
    for row in stale:
        print(f"NO-FFAST-MATH stale: {row} matches no override.  Remove it from {ALLOWLIST}.", file=sys.stderr)
    if reported or failures:
        print("check-no-ffast-math: the fast-math family is banned (CLAUDE.md §V).  The floor is "
              "crucible_fp_strict (cmake/FpStrict.cmake): remove the flag, or remove the override or mark its "
              f"row with `// {MARKER}: <reason>`.", file=sys.stderr)
        return 1
    if stale:
        return 2
    scope = ("presets, sources, compile database and link lines" if build_dir is not None
             else "presets and sources")
    print(f"check-no-ffast-math: clean, no fast-math option in the {scope}.", file=sys.stderr)
    return 0


# ── Self-test ───────────────────────────────────────────────────────────

PLANTED_SOURCE = """\
#pragma once
[[gnu::optimize("fast-math")]] inline double gnu_attribute(double a) { return a; }
__attribute__((hot, optimize("fast-math"))) inline double second_attribute(double a) { return a; }
__attribute__((optimize("-Ofast"))) inline double ofast_option(double a) { return a; }
#pragma GCC optimize("fast-math")
_Pragma("GCC optimize(\\"associative-math\\")")
/* a block comment first */ #pragma GCC optimize ("O2,reciprocal-math")
#define FAST_ATTRIBUTE [[gnu::optimize("finite-math-only")]]
#define FAST_PRAGMA _Pragma("GCC optimize(\\"no-signed-zeros\\")")
#pragma GCC optimize("O2")
__attribute__((optimize("O3"))) inline double plain_level(double a) { return a; }
// #pragma GCC optimize("fast-math")
inline const char* text = "__attribute__((optimize(\\"fast-math\\")))";
[[gnu::optimize("fast-math")]] inline double marked(double a) { return a; }  // NO-FFAST-MATH-OK: fixture
[[gnu::optimize("fast-math")]] inline double admitted(double a) { return a; }
[[gnu::optimize("fast-math")]] inline double bare_marker(double a) { return a; }  // NO-FFAST-MATH-OK:
"""

# (row of the planted source, the scan must report it)
PLANTED_ROWS = (
    (2, True), (3, True), (4, True), (5, True), (6, True), (7, True), (8, True), (9, True),
    (10, False), (11, False), (12, False), (13, False), (14, False), (15, False), (16, True),
)

PLANTED_CMAKE = """\
cmake_minimum_required(VERSION 3.28)
project(planted CXX)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
add_library(planted_lib src/a.cpp)
target_compile_options(planted_lib PRIVATE $<$<CONFIG:Debug>:-ffast-math>)
target_compile_options(planted_lib PRIVATE -Ofast)
#[[ a bracket comment
  add_compile_options(-fassociative-math)
  target_link_options(planted_app PRIVATE -ffinite-math-only)
]]
set(NAMED_BUT_UNUSED "-fno-signed-zeros")
add_library(planted_test test/t.cpp)
target_compile_options(planted_test PRIVATE -freciprocal-math)
add_executable(planted_app src/main.cpp)
target_link_options(planted_app PRIVATE $<$<CONFIG:Debug>:-funsafe-math-optimizations>)
add_library(planted_shared SHARED src/b.cpp)
target_link_libraries(planted_shared PRIVATE -ffp-contract=fast)
add_subdirectory(test)
add_subdirectory(third_party)
"""

PLANTED_TEST_CMAKE = """\
add_executable(planted_test_app main.cpp)
target_link_options(planted_test_app PRIVATE -fassociative-math)
"""

PLANTED_VENDOR_CMAKE = """\
add_executable(planted_vendor_app main.cpp)
target_compile_options(planted_vendor_app PRIVATE -fno-signed-zeros)
target_link_options(planted_vendor_app PRIVATE -fno-signed-zeros)
"""

PLANTED_PRESETS = {
    "version": 6,
    "configurePresets": [
        {"name": "fast", "cacheVariables": {"CMAKE_CXX_FLAGS": "-O2 -Ofast"}},
        {"name": "strict", "cacheVariables": {"CMAKE_CXX_FLAGS": {"type": "STRING", "value": "-fno-fast-math"}},
         "environment": {"CXXFLAGS": "-ffp-contract=fast"}},
    ],
}


def self_test(cmake: str, cxx: str) -> int:
    """Plant every override shape, a configured build and a presets file, then check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        """Record one case and print it."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    print("check-no-ffast-math --self-test")
    with tempfile.TemporaryDirectory(prefix="no-ffast-math-") as work:
        root = Path(work) / "src"
        planted = root / "include" / "crucible" / "planted" / "Fast.h"
        planted.parent.mkdir(parents=True)
        planted.write_text(PLANTED_SOURCE, encoding="utf-8")
        (root / "include" / "crucible" / "third_party").mkdir()
        (root / "include" / "crucible" / "third_party" / "Out.h").write_text(
            '[[gnu::optimize("fast-math")]] inline double out_of_scope(double a) { return a; }\n', encoding="utf-8")
        in_test = root / "test" / "fixture" / "Planted.h"
        in_test.parent.mkdir(parents=True)
        in_test.write_text('[[gnu::optimize("fast-math")]] inline double in_test(double a) { return a; }\n',
                           encoding="utf-8")
        (root / "utils" / "scripts").mkdir(parents=True)
        (root / ALLOWLIST).write_text(
            "include/crucible/planted/Fast.h:[[gnu::optimize(\"fast-math\")]] inline double admitted(double a) "
            "{ return a; }\n", encoding="utf-8")
        found, lost = source_violations(root)
        rows = {int(violation.where.rsplit(":", 1)[1]) for violation in found
                if violation.where.startswith("include/crucible/planted/Fast.h:")}
        expect("the planted sources parse", not lost)
        for row, must_report in PLANTED_ROWS:
            if row == 15:
                expect("row 15: an allowlisted override is live, so the allowlist can admit it", row in rows)
                continue
            expect(f"row {row}: {'reported' if must_report else 'not reported'}", (row in rows) == must_report)
        expect("an override in a vendored tree is out of scope",
               not any("third_party/Out.h" in violation.where for violation in found))
        expect("an override under test/ is reported",
               any(violation.where == "test/fixture/Planted.h:1" for violation in found))
        in_test.unlink()

        (root / "CMakePresets.json").write_text(json.dumps(PLANTED_PRESETS), encoding="utf-8")
        preset_found, _ = preset_violations(root)
        shown = " ".join(violation.what for violation in preset_found)
        expect("a preset cache variable that holds -Ofast is reported", "preset 'fast'" in shown and "-Ofast" in shown)
        expect("a preset environment variable that holds -ffp-contract=fast is reported", "-ffp-contract=fast" in shown)
        expect("-fno-fast-math in a preset is not reported", "-fno-fast-math" not in shown)

        (root / "CMakeLists.txt").write_text(PLANTED_CMAKE, encoding="utf-8")
        for directory in ("src", "test", "third_party"):
            (root / directory).mkdir(exist_ok=True)
        for rel, text in (("src/a.cpp", "int planted_a() { return 0; }\n"),
                          ("src/b.cpp", "int planted_b() { return 0; }\n"),
                          ("src/main.cpp", "int main() { return 0; }\n"),
                          ("test/t.cpp", "int planted_t() { return 0; }\n"),
                          ("test/main.cpp", "int main() { return 0; }\n"),
                          ("test/CMakeLists.txt", PLANTED_TEST_CMAKE),
                          ("third_party/main.cpp", "int main() { return 0; }\n"),
                          ("third_party/CMakeLists.txt", PLANTED_VENDOR_CMAKE)):
            (root / rel).write_text(text, encoding="utf-8")
        build = Path(work) / "build"
        configured = subprocess.run([cmake, "-S", str(root), "-B", str(build), "-DCMAKE_BUILD_TYPE=Debug",
                                     "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", f"-DCMAKE_CXX_COMPILER={cxx}"],
                                    capture_output=True, text=True)
        expect("the planted build configures", configured.returncode == 0)
        if configured.returncode == 0:
            build_found, build_lost = build_violations(build, root)
            shown = " ".join(violation.what for violation in build_found)
            expect("the compile database reads", not build_lost)
            expect("a flag inside a generator expression is reported", "-ffast-math on 1 file(s): src/a.cpp" in shown)
            expect("-Ofast is reported", "-Ofast on 1 file(s): src/a.cpp" in shown)
            expect("a flag in a CMake bracket comment is not reported", "-fassociative-math" not in shown)
            expect("a flag on a file under test/ is reported", "-freciprocal-math on 1 file(s): test/t.cpp" in shown)
            expect("a flag on a file of a vendored tree is out of scope", "third_party/" not in shown)

            link_found, link_lost = link_violations(build, root)
            linked = " ".join(violation.what for violation in link_found)
            expect("the file API reply reads", not link_lost)
            expect("a link option inside a generator expression is reported",
                   "-funsafe-math-optimizations on the link line of 1 target(s): planted_app" in linked)
            expect("a flag passed through target_link_libraries is reported",
                   "-ffp-contract=fast on the link line of 1 target(s): planted_shared" in linked)
            expect("a link option in a CMake bracket comment is not reported", "-ffinite-math-only" not in linked)
            expect("a flag that only a variable names is not reported", "-fno-signed-zeros" not in linked)
            expect("a link option of a target declared under test/ is reported",
                   "-fassociative-math on the link line of 1 target(s): planted_test_app" in linked)
            expect("a link option of a target in a vendored tree is out of scope", "planted_vendor_app" not in linked)
            expect("a compile option is not a link option", "-Ofast" not in linked)

            reply = build / ".cmake" / "api" / "v1" / "reply"
            moved = Path(work) / "reply-moved"
            reply.rename(moved)
            _, missing = link_violations(build, root)
            expect("a build with no file API reply fails", any("no reply of the CMake file API" in line
                                                               for line in missing))
            moved.rename(reply)
            _, foreign = link_violations(build, root / "src")
            expect("a reply of another source tree fails", any("belongs to the source tree" in line
                                                               for line in foreign))
            expect("the full check with the build reports the violations", check(root, build) == 1)
        else:
            print(configured.stderr)

        expect("the full check reports the violations", check(root, None) == 1)
        planted.write_text("#pragma once\ninline double clean(double a) { return a; }\n", encoding="utf-8")
        (root / "CMakePresets.json").unlink()
        expect("a stale allowlist row exits 2", check(root, None) == 2)
        (root / ALLOWLIST).unlink()
        expect("a clean tree exits 0", check(root, None) == 0)
        planted.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file the parser cannot read fails", check(root, None) == 1)
    if failures:
        print(f"check-no-ffast-math --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-ffast-math --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the scan or the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--self-test", action="store_true", help="plant every shape and check each verdict")
    parser.add_argument("--build-dir", type=Path, help="a configured build whose compile database is read")
    parser.add_argument("--cmake", default="cmake", help="the cmake executable (self-test only)")
    parser.add_argument("--cxx", default="c++", help="the C++ compiler of the planted build (self-test only)")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            return self_test(args.cmake, args.cxx)
        return check(REPO_ROOT, args.build_dir)
    except tsast.KitMissing as error:
        print(f"check-no-ffast-math: {error}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
