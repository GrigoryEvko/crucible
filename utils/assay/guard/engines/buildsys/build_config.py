"""build-config — a unity build, a precompiled header, and a build flag that turns on a fast-math option.

The rules of this family read the result of the configure of a build.  They
read the compile database (assay.readers.compdb), the replies of the CMake
file API (assay.readers.cmake) and the presets of the tree.  They read no
text of a CMake file.  A name in a CMake branch that no configure takes gives no
finding.  Each CI leg runs the rules on the build of its own preset.

THE RULE no-unity-pch
    The reflection walks of this tree see each header that a translation unit
    includes.  A unity build puts some source files into one translation
    unit.  A precompiled header puts the headers of the header file into each
    unit that uses it.  Each one changes what a walk sees, and it can change a
    result.  CLAUDE.md §XV states the rule.  The rule gives:
      `unity-source`  a row of the compile database that compiles a unity
                      source of CMake: a file unity_<N>_cxx.cxx or
                      unity_<N>_c.c, or a file in a Unity directory of
                      CMakeFiles
      `pch-flag`      a row with a command that includes a precompiled header:
                      `-include` of a file with a name that starts with
                      cmake_pch, also in the joined shape `-includeFILE`,
                      `-include-pch`, `-Winvalid-pch`, or a word that ends in
                      .gch or .pch
      `pch-header`    a precompiled header of a compile group of a target, at
                      the line of the call that adds it
      `unity-target`  a target that compiles a unity source, at the line of
                      the command that makes the target
      `borrowed-pch`  a target that uses the precompiled header of a
                      different target: a source cmake_pch.hxx and no header of
                      its own
      `unity-cache`   an entry of the CMake cache with a name that starts with
                      CMAKE_UNITY_BUILD
      `unity-preset`  a cache variable of a preset of CMakePresets.json or
                      CMakeUserPresets.json with a name that starts with
                      CMAKE_UNITY_BUILD
      `input`         the compile database, a reply or a presets file cannot
                      be read
    The entity is the source, the word, the header, the target, the entry or
    the variable.  The rule has no ledger, and each finding is an error.

THE RULE no-ffast-math
    CLAUDE.md §V bans the fast-math family, because each option breaks one of
    the numerical invariants of replay: `-ffast-math`,
    `-funsafe-math-optimizations`, `-fassociative-math`, `-freciprocal-math`,
    `-fno-signed-zeros`, `-ffinite-math-only`, `-ffp-contract=fast` and
    `-Ofast`.  The floor is the INTERFACE library crucible_fp_strict
    (cmake/FpStrict.cmake).  The rule also bans the four flags that cancel a
    flag of the floor: `-ffp-contract=on`, `-fno-trapping-math`,
    `-fno-signaling-nans` and `-fno-rounding-math`.  GCC uses the last of two
    contrary flags.  The rule fast-math-override of the quarantine plugin
    reads each optimize attribute and GCC optimize pragma of a unit.  The rule
    gives:
      no kind        a banned flag of a compile of the compile database.  The
                     path is the source, and the entity is the flag
      `link-flag`    a banned flag of the link line of a target, from the
                     reply codemodel-v2, at the command that makes the target.
                     A fast-math option at link time links crtfastmath.o,
                     which sets the flush-to-zero bits of the process
      `preset-flag`  a banned flag in a cache variable or the environment of a
                     preset of CMakePresets.json.  A preset that no leg
                     configures also gets a verdict
      `ledger-row`   a row of the ledger that does not have the shape
                     `PATH | - | admit | FLAG | REASON` with a banned flag
      `input`        the compile database, the reply or the presets file
                     cannot be read, or the database compiles no file of the
                     tree
    The entity of each finding is the flag, or the file that cannot be read.
    A compile of a source, and a target, in a vendored tree (third_party,
    external or vendor) or in a build tree is out of scope.  A row of the
    ledger admits a banned flag on the compile of one source, for a test that
    compiles with the flag by design.  A link flag and a preset flag have no
    such row, because a row with a kind is a `ledger-row` finding.

THE PLANTS
    plant_context() makes the planted build of a plant
    (assay.guard.engines.buildsys.planted).  Then it writes each file of the
    directory planted-overlay/ of the plant into the build directory, at the
    same relative path.  It replaces the placeholders {root} and {build} of
    THE FILE of that module.  An overlay holds a file that the rows of
    build.txt cannot give, such as a compile database with the "command"
    strings of CMake.  A file NAME.removed of the overlay removes the file
    NAME of the build directory.

Complexity: one read of the compile database, of the replies and of the presets for each rule.
"""

import shlex
from pathlib import Path, PurePosixPath

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import listing
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import compdb

UNITY_RULE = "no-unity-pch"
FAST_MATH_RULE = "no-ffast-math"
UNITY_CACHE_PREFIX = "CMAKE_UNITY_BUILD"
# The file that CMake writes for a precompiled header starts with this name: cmake_pch.hxx and cmake_pch.hxx.cxx.
PCH_FILE_PREFIX = "cmake_pch"
INCLUDE = "-include"
# Each word that pch_words() keeps holds one of these texts.  The rule does not divide a command string with none.
PCH_MARKERS = (PCH_FILE_PREFIX, "-include-pch", "-Winvalid-pch", ".gch", ".pch")
PCH_WORDS = frozenset({"-include-pch", "-Winvalid-pch"})
PCH_SUFFIXES = (".gch", ".pch")
WHY = (
    "The reflection walks of this tree see each header that a translation unit includes, and a unity build or a "
    "precompiled header can change a result"
)
# The directories of a vendored tree, which no rule of this family reads.  A directory with a name that starts with
# "build" is a build tree, and no rule of this family reads it.
VENDORED = frozenset({"third_party", "external", "vendor"})
BUILD_TREE_PREFIX = "build"
PRESETS = "CMakePresets.json"
CMAKE_FILE = "CMakeLists.txt"
# Each banned flag, as the driver receives it (THE RULE no-ffast-math).
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
FLOOR = "Keep the floor crucible_fp_strict of cmake/FpStrict.cmake, and remove the flag"
OVERLAY = "planted-overlay"
REMOVED_SUFFIX = ".removed"


# ── no-unity-pch ───────────────────────────────────────────────────────────


def unity_error(path: str, line: int, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule no-unity-pch."""
    return check_report.Finding("error", path, line, UNITY_RULE, message, kind, entity)


def is_unity_source(source: str) -> bool:
    """Tell if a source path is a unity source that CMake writes."""
    path = PurePosixPath(source)
    parts = path.name.split(".", 1)[0].split("_")
    is_unity_name = len(parts) == 3 and parts[0] == "unity" and parts[1].isdigit() and parts[2] in ("c", "cxx")
    return is_unity_name or ("Unity" in path.parts and "CMakeFiles" in path.parts)


def is_pch_file(path: str) -> bool:
    """Tell if a path names a file that CMake writes for a precompiled header."""
    return PurePosixPath(path).name.startswith(PCH_FILE_PREFIX)


def pch_words(words: tuple[str, ...]) -> list[str]:
    """Return each word of a compile command that includes a precompiled header.

    Complexity: linear in the words.
    """
    found: list[str] = []
    for index, word in enumerate(words):
        is_joined = word.startswith(INCLUDE) and word != INCLUDE and is_pch_file(word.removeprefix(INCLUDE))
        if word == INCLUDE and index + 1 < len(words) and is_pch_file(words[index + 1]):
            found.append(f"{INCLUDE} {words[index + 1]}")
        elif is_joined or word in PCH_WORDS or word.endswith(PCH_SUFFIXES):
            found.append(word)
    return found


def command_words(row: compdb.CompileEntry) -> tuple[str, ...]:
    """Return the words of the compile command of a row, or none when its command string holds no marker.

    To divide each command string costs most of the time of the rule on a
    database of thousands of rows.  A string with no marker of PCH_MARKERS
    gives no word that pch_words() keeps, and the rule does not divide it.

    Raises:
        ValueError: If the command string does not obey the quotation marks of a shell
    """
    if row.arguments is not None:
        return row.arguments
    return row.argv() if any(marker in row.command for marker in PCH_MARKERS) else ()


def database_unity_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each row of the compile database that compiles a unity source or uses a precompiled header.

    Complexity: linear in the bytes of the database.
    """
    shown = context.shown(compdb.database_file(context.require_build()))
    try:
        commands = [(row.file, command_words(row)) for row in context.entries()]
    except compdb.MissingDatabase:
        return [
            unity_error(
                shown,
                0,
                "the compile database is not there, and the rule cannot see the compile "
                "commands.  Configure the build first",
                "input",
                compdb.DATABASE_NAME,
            )
        ]
    except (compdb.CompdbError, ValueError) as problem:
        return [unity_error(shown, 0, f"the compile database cannot be read: {problem}", "input", compdb.DATABASE_NAME)]
    found: list[check_report.Finding] = []
    for source, words in commands:
        where = context.shown(source)
        if is_unity_source(source):
            found.append(
                unity_error(
                    where,
                    0,
                    f"the compile database compiles the unity source {source}.  {WHY}.  Turn the unity build off",
                    "unity-source",
                    where,
                )
            )
        for word in pch_words(words):
            found.append(
                unity_error(
                    where,
                    0,
                    f"the compile command of {source} includes a precompiled header "
                    f"({word}).  {WHY}.  Remove the precompiled header",
                    "pch-flag",
                    word,
                )
            )
    return found


def origin_of(context: BuildContext, origin: cmake_reader.Origin | None) -> tuple[str, int]:
    """Return the file and the line of a position in a CMake file, or the build directory when the reply gives none."""
    if origin is None:
        return context.shown(context.require_build()), 0
    return context.shown(origin.file), origin.line


def target_unity_findings(context: BuildContext, model: cmake_reader.CodeModel) -> list[check_report.Finding]:
    """Return one error for each precompiled header, each unity source and each borrowed header of a target.

    Complexity: linear in the targets and their sources.
    """
    found: list[check_report.Finding] = []
    for target in model.targets:
        # Each compile group of a target names its headers, and one header can be in more than one group.
        unique = {(header.header, header.origin or target.origin): header for header in target.precompile_headers}
        for header in unique.values():
            path, line = origin_of(context, header.origin or target.origin)
            found.append(
                unity_error(
                    path,
                    line,
                    f"the target {target.name} gets the precompiled header "
                    f"{header.header}.  {WHY}.  Remove the call that adds it",
                    "pch-header",
                    header.header,
                )
            )
        path, line = origin_of(context, target.origin)
        unity = sorted({source.path for source in target.sources if is_unity_source(source.path)})
        if unity:
            found.append(
                unity_error(
                    path,
                    line,
                    f"the target {target.name} compiles the unity source {unity[0]} of a "
                    f"unity build.  {WHY}.  Turn the unity build of the target off",
                    "unity-target",
                    target.name,
                )
            )
        borrowed = sorted({source.path for source in target.sources if is_pch_file(source.path)})
        if borrowed and not target.precompile_headers:
            found.append(
                unity_error(
                    path,
                    line,
                    f"the target {target.name} uses the precompiled header {borrowed[0]} "
                    f"of a different target.  {WHY}.  Remove the precompiled header",
                    "borrowed-pch",
                    target.name,
                )
            )
    return found


def reply_unity_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return the errors of the code model and the cache of the build, or one error when a reply cannot be read."""
    build = context.require_build()
    build_shown = context.shown(build)
    try:
        model = context.codemodel()
        entries = cmake_reader.cache(build)
    except cmake_reader.CmakeError as problem:
        return [unity_error(build_shown, 0, f"the rule cannot read what CMake decided: {problem}", "input", "reply")]
    found = target_unity_findings(context, model)
    for name in sorted(entries):
        if name.startswith(UNITY_CACHE_PREFIX):
            found.append(
                unity_error(
                    build_shown,
                    0,
                    f"the CMake cache of the build sets {name}={entries[name].value}, "
                    f"which turns a unity build on.  {WHY}.  Remove the cache entry, "
                    f"and configure the build again",
                    "unity-cache",
                    name,
                )
            )
    return found


def preset_unity_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each cache variable of a preset that turns a unity build on, and for each unread file."""
    found: list[check_report.Finding] = []
    for presets in cmake_reader.presets(context.root):
        shown = context.shown(presets.path)
        if presets.problem is not None:
            found.append(
                unity_error(
                    shown, 0, f"the rule cannot read the presets: {presets.problem}", "input", presets.path.name
                )
            )
            continue
        for preset in presets.configure:
            for name in preset.cache_variables:
                if name.startswith(UNITY_CACHE_PREFIX):
                    found.append(
                        unity_error(
                            shown,
                            0,
                            f"the preset {preset.name} sets {name}, which turns a unity "
                            f"build on.  {WHY}.  Remove the cache variable",
                            "unity-preset",
                            name,
                        )
                    )
    return found


def unity_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each unity build and each precompiled header of the build and of the presets (THE RULE no-unity-pch).

    Raises:
        NotApplicable: If the call gives no build directory
    """
    context.require_build()
    return [*database_unity_findings(context), *reply_unity_findings(context), *preset_unity_findings(context)]


# ── no-ffast-math ──────────────────────────────────────────────────────────


def fast_math_error(path: str, line: int, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule no-ffast-math."""
    return check_report.Finding("error", path, line, FAST_MATH_RULE, message, kind, entity)


def is_under(path: Path, root: Path) -> bool:
    """Tell if a path is in the root, after the two resolve."""
    return path.resolve().is_relative_to(root.resolve())


def is_out_of_scope(path: Path, root: Path) -> bool:
    """Tell if a file of the root is in a vendored tree or in a build tree."""
    relative = path.resolve().relative_to(root.resolve())
    return any(part in VENDORED or part.startswith(BUILD_TREE_PREFIX) for part in relative.parts[:-1])


def words_of(text: str) -> list[str]:
    """Return the words of a flag text by the rules of a shell, or at white space for a quotation mark with no end."""
    try:
        return shlex.split(text)
    except ValueError:
        return text.split()


def ledger_row_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each row of the ledger that does not admit a banned flag on the compile of one source.

    A ledger that cannot be read gives no finding here, because the judge of
    the ledger (assay.guard.engines.meta.runner.judged) gives that error.
    """
    rule = context.rule if context.rule is not None else rule_ledger.rule_of(FAST_MATH_RULE)
    try:
        ledger = rule_ledger.read(rule, context.root)
    except OSError, rows_grammar.RowsError, rule_ledger.LedgerError:
        return []
    shown = rule_ledger.shown_path(FAST_MATH_RULE)
    return [
        fast_math_error(
            shown,
            entry.line,
            "a row of the ledger is 'PATH | - | admit | FLAG | REASON', with the path "
            "of a source and a banned flag in FLAG.  A link flag and a preset flag "
            "have no row.  Correct the row, or remove it",
            "ledger-row",
            key.entity or key.kind or key.path,
        )
        for key, entry in sorted(ledger.base.items())
        if key.kind or not key.path or key.entity not in BANNED_FLAGS
    ]


def compile_flag_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each banned flag of the compile of a source of the tree, from the compile database.

    The key of each finding is the source and the flag, with no kind, as a
    row of the ledger names it.  Complexity: linear in the bytes of the
    database, with one resolve of each source.
    """
    shown = context.shown(compdb.database_file(context.require_build()))
    try:
        entries = context.entries()
    except compdb.CompdbError as problem:
        return [
            fast_math_error(shown, 0, f"the compile database cannot be read: {problem}", "input", compdb.DATABASE_NAME)
        ]
    if not entries:
        return [
            fast_math_error(
                shown,
                0,
                "the compile database holds no entry, and the rule read no flag",
                "input",
                compdb.DATABASE_NAME,
            )
        ]
    root = context.root
    found: dict[tuple[str, str], check_report.Finding] = {}
    under_root = 0
    for entry in entries:
        source = Path(entry.file)
        if not is_under(source, root):
            continue
        under_root += 1
        if is_out_of_scope(source, root):
            continue
        relative = source.resolve().relative_to(root.resolve()).as_posix()
        try:
            words = entry.argv()
        except ValueError as problem:
            found[(relative, "")] = fast_math_error(
                relative,
                0,
                f"the rule cannot divide the compile command of {relative} into words: {problem}",
                "input",
                compdb.DATABASE_NAME,
            )
            continue
        for flag in words:
            if flag in BANNED_FLAGS and (relative, flag) not in found:
                found[(relative, flag)] = fast_math_error(
                    relative,
                    0,
                    f"the compile of {relative} has the banned flag {flag}.  {FLOOR}.  A test that "
                    f"compiles with the flag by design gets a row of the ledger",
                    "",
                    flag,
                )
    if under_root == 0:
        return [
            fast_math_error(
                shown,
                0,
                f"no entry of the compile database compiles a file of {root}.  The "
                f"database is of a different source tree, and the rule read no flag of "
                f"this tree",
                "input",
                compdb.DATABASE_NAME,
            )
        ]
    return [found[key] for key in sorted(found)]


def link_flag_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each banned flag of the link line of a target, from the reply codemodel-v2.

    Complexity: linear in the bytes of the reply.
    """
    build = context.require_build()
    shown = context.shown(cmake_reader.api_directory(build))
    try:
        model = context.codemodel()
    except cmake_reader.CmakeError as problem:
        return [
            fast_math_error(
                shown,
                0,
                f"the reply codemodel-v2 of the CMake file API cannot be read, and the rule "
                f"read no link flag: {problem}",
                "input",
                "codemodel-v2",
            )
        ]
    root = context.root
    source = Path(model.source)
    if not source.is_dir() or source.resolve() != root.resolve():
        return [
            fast_math_error(
                shown,
                0,
                f"the reply is of the source tree {source} and not of {root}, and the rule "
                f"read no link flag of this tree",
                "input",
                "codemodel-v2",
            )
        ]
    found: dict[tuple[str, str], check_report.Finding] = {}
    for target in model.targets:
        cmake_file = Path(target.source_dir) / CMAKE_FILE
        if is_under(cmake_file, root) and is_out_of_scope(cmake_file, root):
            continue
        path, line = origin_of(context, target.origin)
        for fragment, _ in target.link_fragments:
            for flag in words_of(fragment):
                if flag in BANNED_FLAGS and (target.name, flag) not in found:
                    found[(target.name, flag)] = fast_math_error(
                        path,
                        line,
                        f"the link line of the target {target.name} has the banned flag {flag}, which "
                        f"links crtfastmath.o and sets the flush-to-zero bits of the process.  {FLOOR}",
                        "link-flag",
                        flag,
                    )
    return [found[key] for key in sorted(found)]


def preset_flag_findings(context: BuildContext) -> list[check_report.Finding]:
    """Return one error for each banned flag in a cache variable or the environment of a preset of CMakePresets.json.

    The rule reads the presets file of the tree, and not a
    CMakeUserPresets.json of one host.
    """
    presets = next((item for item in cmake_reader.presets(context.root) if item.path.name == PRESETS), None)
    if presets is None:
        return []
    shown = context.shown(presets.path)
    if presets.problem is not None:
        return [fast_math_error(shown, 0, f"the presets file cannot be read: {presets.problem}", "input", PRESETS)]
    found: list[check_report.Finding] = []
    for preset in presets.configure:
        values = [(f"cacheVariables.{name}", text) for name, text in preset.cache_variables.items()]
        values += [(f"environment.{name}", text) for name, text in preset.environment.items()]
        for label, text in values:
            for flag in words_of(text or ""):
                if flag in BANNED_FLAGS:
                    found.append(
                        fast_math_error(
                            shown,
                            0,
                            f"the preset {preset.name} holds the banned flag {flag} in {label}.  {FLOOR}",
                            "preset-flag",
                            flag,
                        )
                    )
    return found


def fast_math_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each banned flag of a compile, of a link line and of a preset, and each ledger row of an incorrect shape.

    THE RULE no-ffast-math of the module text tells the rule.  The rule reads
    a build, because a row of the ledger admits a flag of a compile of the
    build.  With no build, the judge can call each row stale.

    Raises:
        NotApplicable: If the call gives no build directory
    """
    context.require_build()
    return [
        *ledger_row_findings(context),
        *compile_flag_findings(context),
        *link_flag_findings(context),
        *preset_flag_findings(context),
    ]


# ── The plants ─────────────────────────────────────────────────────────────


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Make the planted build of a plant, put the files of its overlay into the build directory, and give the context.

    THE PLANTS of the module text tells the overlay.  Complexity: linear in
    the files of the plant and of its overlay.

    Raises:
        NotApplicable: If build.txt of the plant does not obey its rows
    """
    context = planted.plant_context(plant, scratch)
    overlay = scratch / OVERLAY
    build = context.build_dir
    if build is None or not overlay.is_dir():
        return context
    for path in listing(overlay, "**/*"):
        if not path.is_file():
            continue
        target = build / path.relative_to(overlay)
        if path.name.endswith(REMOVED_SUFFIX):
            target.with_name(path.name.removesuffix(REMOVED_SUFFIX)).unlink(missing_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(planted.expanded(path.read_text(encoding="utf-8"), scratch, build), encoding="utf-8")
    return context


RULES = {
    "no-unity-pch": meta_runner.Member(unity_findings),
    "no-ffast-math": meta_runner.Member(fast_math_findings),
}
