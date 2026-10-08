"""no-ffast-math — no build flag turns on a fast-math option.

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
This guard catches what could punch through it.  It also bans the four flags
that cancel a flag of the floor, because GCC uses the last of two contrary
flags:

    -ffp-contract=on               cancels -ffp-contract=off
    -fno-trapping-math             cancels -ftrapping-math
    -fno-signaling-nans            cancels -fsignaling-nans
    -fno-rounding-math             cancels -frounding-math

The rule fast-math-override of the quarantine plugin reads each optimize
attribute and GCC optimize pragma that a unit compiles
(utils/tools/quarantine/unit.cpp).

THE ENGINES
    Build flags   The compile database of a configured build
                  (assay.readers.compdb) holds each flag as the compiler
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

SCOPE
    The compile-database entries of the files of the source root, the link
    line of each target and the configure presets.  A test or a bench links
    crucible, and a fast-math option on its link line links crtfastmath.o,
    which runs the determinism tests with flush-to-zero.  A
    file or a target in a vendored tree (third_party, external or vendor) or
    in a build tree is out of scope.

EXEMPTIONS
    A row `PATH | - | admit | FLAG | REASON` of utils/ledgers/no-ffast-math.txt
    (policy admit) admits a banned flag on the compile of the source file PATH
    in the compile database.  Such a row is for a test that compiles with the
    flag by design.  A banned flag of a preset or a link line has no
    exemption.  A row that matches no compile of a run with a build directory
    is stale.

USAGE
    guard check --rule no-ffast-math [--build-dir BUILD_DIR]
    guard check --rule no-ffast-math --self-test [--cmake CMAKE] [--cxx CXX]

Exit 0 clean, 1 on a violation or a read failure, 2 on a stale row, a usage
error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import shlex
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import proc
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.readers import cmake as cmake_reader
from assay.readers import compdb
from assay.setup import pins as cmake_pin

EXEMPT_COMPONENTS = frozenset({"third_party", "external", "vendor"})
RULE = "no-ffast-math"
LEDGER = rule_ledger.shown_path(RULE)
PRESETS = "CMakePresets.json"

# Each banned compiler flag, spelled as the driver receives it: the fast-math
# family, and the four flags that cancel a flag of the floor of
# cmake/FpStrict.cmake (-ffp-contract=off, -ftrapping-math, -fsignaling-nans
# and -frounding-math).  GCC uses the last of two contrary flags.
BANNED_FLAGS = frozenset(
    {
        "-ffast-math",
        "-funsafe-math-optimizations",
        "-fassociative-math",
        "-freciprocal-math",
        "-fno-signed-zeros",
        "-ffinite-math-only",
        "-ffp-contract=fast",
        "-Ofast",
        "-ffp-contract=on",
        "-fno-trapping-math",
        "-fno-signaling-nans",
        "-fno-rounding-math",
    }
)


@dataclass(frozen=True)
class Violation:
    """One banned flag."""

    where: str
    what: str


# ── Build flags ─────────────────────────────────────────────────────────


def is_under(path: Path, root: Path) -> bool:
    """Return True when PATH lies inside ROOT once both are resolved."""
    return path.resolve().is_relative_to(root.resolve())


def is_exempt(path: Path, root: Path) -> bool:
    """Return True when a file under ROOT lies in a vendored tree or in a build tree."""
    rel = path.resolve().relative_to(root.resolve())
    return any(part in EXEMPT_COMPONENTS or part.startswith("build") for part in rel.parts[:-1])


def build_violations(
    build_dir: Path, root: Path, admitted: frozenset[str] = frozenset()
) -> tuple[list[Violation], list[str], set[str]]:
    """Read the compile database of BUILD_DIR and return each banned flag, grouped by flag.

    A ledger row `FILE | - | admit | FLAG | REASON` admits one flag on the
    compile of one file.  Its key is FILE:FLAG, as in `admitted`.

    Complexity: linear in the size of the database.

    Returns:
        The violations, one line for each error that stops the read, and each key of `admitted` that a compile
        of the database matches
    """
    database = compdb.database_file(build_dir)
    try:
        entries = compdb.entries(build_dir)
    except compdb.CompdbError as error:
        return [], [f"{database}: cannot read the compile database: {error}"], set()
    if not entries:
        return ([], [f"{database}: the compile database holds no entry, so no flag was read"], set())
    by_flag: dict[str, list[str]] = {}
    matched: set[str] = set()
    under_root = 0
    for entry in entries:
        source = Path(entry.file)
        if not is_under(source, root):
            continue
        under_root += 1
        if is_exempt(source, root):
            continue
        rel = str(source.resolve().relative_to(root.resolve()))
        for flag in entry.argv():
            if flag not in BANNED_FLAGS:
                continue
            if f"{rel}:{flag}" in admitted:
                matched.add(f"{rel}:{flag}")
                continue
            by_flag.setdefault(flag, []).append(rel)
    if under_root == 0:
        return (
            [],
            [
                f"{database}: no entry compiles a file under {root}, so the database belongs to another "
                f"source tree and no flag of this tree was read"
            ],
            set(),
        )
    violations = []
    for flag, files in sorted(by_flag.items()):
        shown = ", ".join(sorted(set(files))[:5])
        more = len(set(files)) - 5
        suffix = f" and {more} more" if more > 0 else ""
        violations.append(Violation(f"{database}", f"{flag} on {len(set(files))} file(s): {shown}{suffix}"))
    return violations, [], matched


def words_of(text: str) -> list[str]:
    """Return the words of a flag text by the rules of a shell, or at white space for an open quotation mark."""
    try:
        return shlex.split(text)
    except ValueError:
        return text.split()


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
    try:
        model = cmake_reader.codemodel(build_dir)
    except cmake_reader.CmakeError as error:
        return [], [f"the codemodel reply of the CMake file API cannot be read, so no link flag was read: {error}"]
    source = Path(model.source)
    if not source.is_dir() or source.resolve() != root.resolve():
        return [], [
            f"{build_dir}: the file API reply belongs to the source tree {source}, not to {root}, so no "
            f"link flag of this tree was read"
        ]
    by_flag: dict[str, set[str]] = {}
    for target in model.targets:
        directory = Path(target.source_dir) / "CMakeLists.txt"
        if is_under(directory, root) and is_exempt(directory, root):
            continue
        for fragment, _ in target.link_fragments:
            for flag in words_of(fragment):
                if flag in BANNED_FLAGS:
                    by_flag.setdefault(flag, set()).add(target.name)
    where = f"{cmake_reader.api_directory(build_dir)}"
    return [
        Violation(where, f"{flag} on the link line of {len(names)} target(s): {', '.join(sorted(names))}")
        for flag, names in sorted(by_flag.items())
    ], []


def preset_violations(root: Path) -> tuple[list[Violation], list[str]]:
    """Read CMakePresets.json (assay.readers.cmake) and return each banned flag in a cache variable or the environment.

    The check reads the presets file of the tree, and not a CMakeUserPresets.json of one host.
    """
    found = next((item for item in cmake_reader.presets(root) if item.path.name == PRESETS), None)
    if found is None:
        return [], []
    if found.problem is not None:
        return [], [f"{found.path}: the presets file cannot be read: {found.problem}"]
    violations = []
    for preset in found.configure:
        values = [
            (f"cacheVariables.{variable}", text)
            for variable, text in preset.cache_variables.items()
            if text is not None
        ]
        values += [
            (f"environment.{variable}", text) for variable, text in preset.environment.items() if text is not None
        ]
        for label, text in values:
            for token in words_of(text):
                if token in BANNED_FLAGS:
                    violations.append(Violation(f"{PRESETS}", f"preset '{preset.name}' {label} holds {token}"))
    return violations, []


# ── The check ───────────────────────────────────────────────────────────


def load_ledger(root: Path) -> tuple[list[str], list[str]]:
    """Return the `path:flag` key of each row of the ledger, and the read failures.

    A ledger with no file has no row.  A row has no KIND, and its ENTITY is a banned flag.
    """
    try:
        ledger = rule_ledger.read(rule_ledger.rule_of(RULE), root)
    except (OSError, rows_grammar.RowsError, rule_ledger.LedgerError) as problem:
        return [], [f"{LEDGER}: the ledger cannot be read: {problem}"]
    keys: list[str] = []
    failures: list[str] = []
    for key, entry in sorted(ledger.base.items()):
        if key.kind or not key.path or key.entity not in BANNED_FLAGS:
            failures.append(
                f"{LEDGER}:{entry.line}: a row is 'PATH | - | admit | FLAG | REASON', with a banned flag in FLAG"
            )
            continue
        keys.append(f"{key.path}:{key.entity}")
    return keys, failures


def check(root: Path, build_dir: Path | None) -> int:
    """Run every scan and print the report.

    Returns:
        0 clean, 1 on a violation or a read failure, 2 on a stale ledger row
    """
    violations: list[Violation] = []
    failures: list[str] = []
    rows, ledger_failures = load_ledger(root)
    failures += ledger_failures
    live: set[str] = set()
    scans = [preset_violations(root)]
    if build_dir is not None:
        build_found, build_lost, matched = build_violations(build_dir, root, frozenset(rows))
        live |= matched
        scans += [(build_found, build_lost), link_violations(build_dir, root)]
    for found, lost in scans:
        violations += found
        failures += lost
    for failure in failures:
        print(f"NO-FFAST-MATH read failure: {failure}", file=sys.stderr)
    for violation in violations:
        print(f"NO-FFAST-MATH violation: {violation.where}: {violation.what}.", file=sys.stderr)
    # A row of a build flag can match only when the check reads the compile database.
    stale = [row for row in rows if row not in live and build_dir is not None]
    for row in stale:
        print(
            f"NO-FFAST-MATH stale: {row} matches no flag of a compile.  Remove its row from {LEDGER}.", file=sys.stderr
        )
    if violations or failures:
        print(
            "check-no-ffast-math: the fast-math family is banned (CLAUDE.md §V).  The floor is "
            "crucible_fp_strict (cmake/FpStrict.cmake): remove the flag.",
            file=sys.stderr,
        )
        return 1
    if stale:
        return 2
    scope = "presets, compile database and link lines" if build_dir is not None else "presets"
    print(f"check-no-ffast-math: clean, no fast-math option in the {scope}.", file=sys.stderr)
    return 0


# ── Self-test ───────────────────────────────────────────────────────────

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
add_library(planted_floor src/floor.cpp)
target_compile_options(planted_floor PRIVATE -ffp-contract=on -fno-trapping-math -fno-signaling-nans -fno-rounding-math)
add_library(planted_admitted src/admitted.cpp)
target_compile_options(planted_admitted PRIVATE -ffp-contract=on)
add_library(planted_kept src/kept.cpp)
target_compile_options(planted_kept PRIVATE -ffp-contract=off -ftrapping-math -fsignaling-nans -frounding-math)
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
        {
            "name": "strict",
            "cacheVariables": {"CMAKE_CXX_FLAGS": {"type": "STRING", "value": "-fno-fast-math"}},
            "environment": {"CXXFLAGS": "-ffp-contract=fast"},
        },
    ],
}


def self_test(cmake: str, cxx: str) -> int:
    """Plant a configured build and a presets file, then check each verdict.

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
        root.mkdir()
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True)
        ledger_head = f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n"

        (root / "CMakePresets.json").write_text(json.dumps(PLANTED_PRESETS), encoding="utf-8")
        preset_found, _ = preset_violations(root)
        shown = " ".join(violation.what for violation in preset_found)
        expect("a preset cache variable that holds -Ofast is reported", "preset 'fast'" in shown and "-Ofast" in shown)
        expect("a preset environment variable that holds -ffp-contract=fast is reported", "-ffp-contract=fast" in shown)
        expect("-fno-fast-math in a preset is not reported", "-fno-fast-math" not in shown)

        (root / "CMakeLists.txt").write_text(PLANTED_CMAKE, encoding="utf-8")
        for directory in ("src", "test", "third_party"):
            (root / directory).mkdir(exist_ok=True)
        for rel, text in (
            ("src/a.cpp", "int planted_a() { return 0; }\n"),
            ("src/b.cpp", "int planted_b() { return 0; }\n"),
            ("src/floor.cpp", "int planted_floor() { return 0; }\n"),
            ("src/admitted.cpp", "int planted_admitted() { return 0; }\n"),
            ("src/kept.cpp", "int planted_kept() { return 0; }\n"),
            ("src/main.cpp", "int main() { return 0; }\n"),
            ("test/t.cpp", "int planted_t() { return 0; }\n"),
            ("test/main.cpp", "int main() { return 0; }\n"),
            ("test/CMakeLists.txt", PLANTED_TEST_CMAKE),
            ("third_party/main.cpp", "int main() { return 0; }\n"),
            ("third_party/CMakeLists.txt", PLANTED_VENDOR_CMAKE),
        ):
            (root / rel).write_text(text, encoding="utf-8")
        build = Path(work) / "build"
        configured = proc.run(
            proc.Command(
                [
                    cmake,
                    "-S",
                    str(root),
                    "-B",
                    str(build),
                    "-DCMAKE_BUILD_TYPE=Debug",
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    f"-DCMAKE_CXX_COMPILER={cxx}",
                ],
                timeout_s=proc.TOOL_S,
            )
        )
        expect("the planted build configures", configured.exit == 0)
        if configured.exit == 0:
            build_found, build_lost, _ = build_violations(build, root)
            shown = " ".join(violation.what for violation in build_found)
            expect("the compile database reads", not build_lost)
            expect("a flag inside a generator expression is reported", "-ffast-math on 1 file(s): src/a.cpp" in shown)
            expect("-Ofast is reported", "-Ofast on 1 file(s): src/a.cpp" in shown)
            expect("a flag in a CMake bracket comment is not reported", "-fassociative-math" not in shown)
            expect("a flag on a file under test/ is reported", "-freciprocal-math on 1 file(s): test/t.cpp" in shown)
            expect("a flag on a file of a vendored tree is out of scope", "third_party/" not in shown)
            for flag in ("-fno-trapping-math", "-fno-signaling-nans", "-fno-rounding-math"):
                expect(
                    f"{flag}, which cancels a flag of the floor, is reported",
                    f"{flag} on 1 file(s): src/floor.cpp" in shown,
                )
            expect(
                "-ffp-contract=on, which cancels a flag of the floor, is reported",
                "-ffp-contract=on on 2 file(s): src/admitted.cpp, src/floor.cpp" in shown,
            )
            expect(
                "the flags of the floor itself are not reported",
                "src/kept.cpp" not in shown and "-ffp-contract=off" not in shown and "-ftrapping-math" not in shown,
            )
            key = "src/admitted.cpp:-ffp-contract=on"
            admitted_found, _, matched = build_violations(build, root, frozenset({key, "src/none.cpp:-Ofast"}))
            admitted_shown = " ".join(violation.what for violation in admitted_found)
            expect(
                "a ledger key FILE:FLAG admits that flag on that file, and only there",
                "-ffp-contract=on on 1 file(s): src/floor.cpp" in admitted_shown and matched == {key},
            )

            link_found, link_lost = link_violations(build, root)
            linked = " ".join(violation.what for violation in link_found)
            expect("the file API reply reads", not link_lost)
            expect(
                "a link option inside a generator expression is reported",
                "-funsafe-math-optimizations on the link line of 1 target(s): planted_app" in linked,
            )
            expect(
                "a flag passed through target_link_libraries is reported",
                "-ffp-contract=fast on the link line of 1 target(s): planted_shared" in linked,
            )
            expect("a link option in a CMake bracket comment is not reported", "-ffinite-math-only" not in linked)
            expect("a flag that only a variable names is not reported", "-fno-signed-zeros" not in linked)
            expect(
                "a link option of a target declared under test/ is reported",
                "-fassociative-math on the link line of 1 target(s): planted_test_app" in linked,
            )
            expect("a link option of a target in a vendored tree is out of scope", "planted_vendor_app" not in linked)
            expect("a compile option is not a link option", "-Ofast" not in linked)

            reply = cmake_reader.api_directory(build) / "reply"
            moved = Path(work) / "reply-moved"
            reply.rename(moved)
            _, missing = link_violations(build, root)
            expect(
                "a build with no file API reply fails",
                any("holds no index of the CMake file API" in line for line in missing),
            )
            moved.rename(reply)
            _, foreign = link_violations(build, root / "src")
            expect(
                "a reply of another source tree fails", any("belongs to the source tree" in line for line in foreign)
            )
            expect("the full check with the build reports the violations", check(root, build) == 1)
            ledger.write_text(
                f"{ledger_head}src/admitted.cpp | - | admit | -ffp-contract=on | a planted test\n"
                f"src/none.cpp | - | admit | -fno-rounding-math | a planted stale row\n",
                encoding="utf-8",
            )
            with contextlib.redirect_stderr(io.StringIO()) as said:
                check(root, build)
            expect(
                "a flag row admits its compile, and a flag row that matches no compile is stale",
                "src/admitted.cpp" not in said.getvalue()
                and "stale: src/none.cpp:-fno-rounding-math" in said.getvalue(),
            )
            with contextlib.redirect_stderr(io.StringIO()) as said:
                check(root, None)
            expect(
                "a run with no build directory does not call a flag row stale",
                "stale: src/none.cpp" not in said.getvalue(),
            )
        else:
            print(configured.stderr)

        expect("the full check reports the violations of the presets", check(root, None) == 1)
        (root / "CMakePresets.json").unlink()
        ledger.write_text(f"{ledger_head}src/admitted.cpp | - | admit | -ffp-contract=on | -\n", encoding="utf-8")
        expect("a row with no reason is a read failure", check(root, None) == 1)
        ledger.write_text(
            f"{ledger_head}src/admitted.cpp | marker | admit | -ffp-contract=on | a reason\n", encoding="utf-8"
        )
        expect("a row with a kind is a read failure", check(root, None) == 1)
        ledger.write_text(
            f"{ledger_head}src/admitted.cpp | - | admit | [[gnu::optimize]] | a reason\n", encoding="utf-8"
        )
        expect("a row whose entity is no banned flag is a read failure", check(root, None) == 1)
        ledger.unlink()
        expect("a clean tree exits 0", check(root, None) == 0)
    if failures:
        print(f"check-no-ffast-math --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-ffast-math --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the scan or the self-test."""
    parser = argparse.ArgumentParser(prog="guard check --rule no-ffast-math", description=__doc__.split("\n", 1)[0])
    parser.add_argument("--self-test", action="store_true", help="plant every shape and check each verdict")
    parser.add_argument("--build-dir", type=Path, help="a configured build whose compile database is read")
    parser.add_argument("--cmake", help="the cmake executable of the self-test (default: the cmake of PATH at the pin)")
    parser.add_argument("--cxx", default="c++", help="the C++ compiler of the planted build (self-test only)")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            return self_test(args.cmake or cmake_pin.pinned_program("cmake"), args.cxx)
        return check(REPO_ROOT, args.build_dir)
    except cmake_pin.PinError as error:
        print(f"check-no-ffast-math --self-test: {error}", file=sys.stderr)
        return 2
