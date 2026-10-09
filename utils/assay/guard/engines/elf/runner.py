"""The runner of the engine elf: the facts of a build, the plants, and the run, the witness and the write of a rule.

THE BUILD
    A rule of the engine elf reads the objects, the static libraries and the
    compile database of a build.  ElfContext reads each fact of the build one
    time, on its first use:
      * The build kind of build-kind.txt (assay.core.target.kind_of)
      * The compiler of the CMake cache, CMAKE_CXX_COMPILER
      * The target tier of the build (assay.core.target.build_tier)
      * The static libraries of the root CMakeLists.txt.  Each one is a
        target of the type STATIC_LIBRARY in the code model of the CMake file
        API.  The source root is its source directory.  A library of a
        subdirectory, such as a test library, is not one of them
      * The objects of a target, from `ninja -t inputs TARGET`
      * The compile database (assay.readers.compdb)
      * A variable of the CMake cache.
    A rule that cannot read a fact of the build raises BuildError, and a rule
    that does not apply raises NotApplicable.  A test or a witness gives a
    PlantedBuild, and then no build directory is necessary.

THE PLANTS
    A plant of a rule of the engine is a tree in utils/plants/RULE/ (the
    witness of assay.guard.engines.meta.runner).  Its file plant.txt, in the
    rows grammar of assay.core.rows, gives the build of the plant.
    plant_context() copies the tree, compiles the units of the plant with the
    patched compiler (assay.core.testing.patched_compiler), and gives a
    PlantedBuild with these facts:

        kind KIND                      the build kind.  The default is planted
        tier TIER                      the target tier.  The default is planted
        flags WORD...                  the words of each compile of a library
                                       unit.  The default is FLAGS
        library NAME UNIT...           compile each unit, and put the objects
                                       into the archive libNAME.a of the build
                                       directory.  The library is a static
                                       library of the root
        unit TARGET UNIT | WORD...     a row of the compile database: the
                                       unit, an object of the target TARGET,
                                       with the compiler and the words
        cache NAME VALUE               a variable of the CMake cache

    UNIT is a path relative to the tree, and a word {root} of a unit row is
    the root of the copy.  A row with a different head word is a row of the
    module of the rule, which reads it from the PlantedBuild.  A plant that
    the rule refuses must give an error, and a control must give none.

THE RUN
    A module that judges its findings itself gives judge(rule, context),
    run(rule, context, warnings_dir) and write(rule, request).  For each other
    rule, the run of assay.guard.engines.meta.runner judges the findings of
    the function findings(context) of the module against the ledger.  A run
    exits 0 with no error, 1 with an error and 77 (SKIPPED) when the rule
    does not apply to the build.  A build that the runner cannot read gives
    one error.

`guard selftest tests.guard.engines.elf.test_runner` does a test of the module.
"""

import shlex
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import cmake, compdb, ninja

STATIC_LIBRARY = "STATIC_LIBRARY"
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
MANIFEST = "plant.txt"
PLANTED = "planted"
# The words of each compile of a library unit of a plant.  -ffunction-sections puts each function in a section of
# its own, as each build of the tree does.
FLAGS = ("-std=c++17", "-O1", "-ffunction-sections")
ROOT_WORD = "{root}"
SKIPPED = meta_runner.SKIPPED
NotApplicable = meta_runner.NotApplicable


class BuildError(Exception):
    """The runner cannot read a fact of the build, or a plant does not build."""


@dataclass(frozen=True, slots=True)
class PlantedBuild:
    """The facts of a build that a test or a plant gives (THE BUILD).

    `libraries` gives the archive of each static library of the root by its target name, `targets` gives the objects
    of each target, and `rows` holds each row of plant.txt for the module of the rule.
    """

    kind: str = PLANTED
    tier: str = PLANTED
    compiler: str = ""
    libraries: Mapping[str, Path] = field(default_factory=dict)
    entries: tuple[compdb.CompileEntry, ...] = ()
    targets: Mapping[str, frozenset[str]] = field(default_factory=dict)
    cache: Mapping[str, str] = field(default_factory=dict)
    rows: tuple[rows_grammar.Row, ...] = ()


@dataclass(frozen=True, slots=True)
class WriteRequest:
    """What one write of the ledger of a rule of the engine does.

    `mode` is the mode of assay.core.ledger, and `import_log` puts the rows of a configuration from a log into the
    ledger, with no build.  `add_configuration` writes the configuration of the build with its name (THE POLICY of
    assay.core.configuration).
    """

    root: Path
    build_dir: Path | None
    mode: rule_ledger.WriteMode = rule_ledger.WriteMode.LOWER
    reason: str | None = None
    import_log: Path | None = None
    add_configuration: bool = False


class ElfContext:
    """The build that the rules of the engine elf read: the root of the tree, the build directory and its facts.

    The context reads each fact one time, on its first use (THE BUILD).  A planted build gives each fact, and then no
    build directory is necessary.
    """

    __slots__ = (
        "_cache",
        "_compiler",
        "_entries",
        "_kind",
        "_libraries",
        "_objects",
        "_tier",
        "build_dir",
        "planted",
        "root",
    )

    def __init__(self, root: Path, build_dir: Path | None = None, planted: PlantedBuild | None = None) -> None:
        """Keep the root, the build directory, and a planted build."""
        self.root = root
        self.build_dir = build_dir
        self.planted = planted
        self._kind: str | None = None if planted is None else planted.kind
        self._compiler: str | None = None if planted is None else planted.compiler
        self._tier: str | None = None if planted is None else planted.tier
        self._libraries: dict[str, Path] | None = None if planted is None else dict(planted.libraries)
        self._entries: tuple[compdb.CompileEntry, ...] | None = None if planted is None else planted.entries
        self._objects: dict[str, frozenset[str]] = {} if planted is None else dict(planted.targets)
        self._cache: dict[str, str] | None = None if planted is None else dict(planted.cache)

    def build_shown(self) -> str:
        """Return the build directory as a message shows it, or the word planted for a planted build."""
        if self.build_dir is None:
            return PLANTED
        return build_census.shown(self.build_dir, self.root)

    def directory(self) -> Path:
        """Return the build directory.

        Raises:
            NotApplicable: If the call gives no build directory
        """
        if self.build_dir is None:
            raise NotApplicable("the engine elf reads the objects of a build, and the call gives no --build-dir DIR")
        return self.build_dir

    def kind(self) -> str:
        """Return the build kind.

        Raises:
            NotApplicable: If the call gives no build directory, or the build has no build-kind.txt
        """
        if self._kind is None:
            kind = build_target.kind_of(self.directory())
            if kind is None:
                raise NotApplicable(
                    f"{self.build_shown()} has no build-kind.txt, which cmake/BuildLauncher.cmake writes"
                )
            self._kind = kind
        return self._kind

    def cache_value(self, name: str) -> str:
        """Return the value of a variable of the CMake cache, or an empty text when the cache has no such variable.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the CMake cache cannot be read
        """
        if self._cache is None:
            try:
                entries = cmake.cache(self.directory())
            except cmake.CmakeError as problem:
                raise BuildError(f"the CMake cache of {self.build_shown()} cannot be read: {problem}") from None
            self._cache = {key: entry.value for key, entry in entries.items()}
        return self._cache.get(name, "")

    def compiler(self) -> str:
        """Return the C++ compiler of the build.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the CMake cache cannot be read or names no compiler
        """
        if self._compiler is None:
            compiler = self.cache_value(COMPILER_ENTRY)
            if not compiler:
                raise BuildError(f"the CMake cache of {self.build_shown()} names no {COMPILER_ENTRY}")
            self._compiler = compiler
        return self._compiler

    def tier(self) -> str:
        """Return the target tier of the build (assay.core.target, THE TARGET TIER).

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the CMake cache cannot be read, or the compiler does not print its target
        """
        if self._tier is None:
            try:
                self._tier = build_target.build_tier(self.directory(), self.compiler())
            except build_target.TierError as problem:
                raise BuildError(str(problem)) from None
        return self._tier

    def libraries(self) -> dict[str, Path]:
        """Return the archive of each static library of the root CMakeLists.txt, by its target name.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the code model of the CMake file API cannot be read
        """
        if self._libraries is None:
            try:
                model = cmake.codemodel(self.directory())
            except cmake.CmakeError as problem:
                raise BuildError(f"the code model of {self.build_shown()} cannot be read: {problem}") from None
            source = Path(model.source).resolve()
            self._libraries = {
                target.name: Path(target.artifacts[0])
                for target in model.targets
                if target.type_name == STATIC_LIBRARY
                and target.artifacts
                and Path(target.source_dir).resolve() == source
            }
        return self._libraries

    def entries(self) -> tuple[compdb.CompileEntry, ...]:
        """Return each compile of the compile database.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the compile database cannot be read
        """
        if self._entries is None:
            try:
                self._entries = compdb.entries(self.directory())
            except compdb.CompdbError as problem:
                raise BuildError(f"the compile database of {self.build_shown()} cannot be read: {problem}") from None
        return self._entries

    def objects_of(self, target: str) -> frozenset[str]:
        """Return the absolute path of each object of a target.

        Raises:
            NotApplicable: If the call gives no build directory, or the generator of the build is not Ninja
            BuildError: If ninja cannot give the inputs of the target
        """
        if target not in self._objects:
            directory = self.directory()
            try:
                inputs = ninja.inputs(directory, target)
            except ninja.NotNinja as reason:
                raise NotApplicable(str(reason)) from None
            except ninja.NinjaError as problem:
                raise BuildError(f"ninja does not give the inputs of the target {target}: {problem}") from None
            self._objects[target] = frozenset(
                str((directory / item).resolve()) for item in inputs if item.endswith(".o")
            )
        return self._objects[target]

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of the function findings(context) of the module of a rule.

        Raises:
            NotApplicable: If the rule does not apply to the build
            BuildError: If the build cannot be read
        """
        return list(meta_runner.rule_module(rule).findings(self))


# ── The plants ─────────────────────────────────────────────────────


def compiled(command: Sequence[str], where: Path) -> None:
    """Run one compile or one archive command of a plant.

    Raises:
        BuildError: If the command cannot start or fails
    """
    try:
        proc.require(proc.run(proc.Command(list(command), timeout_s=proc.TOOL_S, cwd=where)))
    except proc.ToolFailed as problem:
        raise BuildError(f"the plant does not build: {shlex.join(command)[:300]}: {problem}") from None


def object_of(build: Path, unit: str) -> Path:
    """Return the object of a library unit of a plant in the scratch build directory."""
    return build / f"{unit.replace('/', '_')}.o"


def refused(row: rows_grammar.Row, shape: str) -> BuildError:
    """Return the error of a row of plant.txt that does not have its shape."""
    return BuildError(str(row.error(f"a {row.words[0]} row of {MANIFEST} is '{shape}'")))


def manifest_rows(tree: Path) -> tuple[rows_grammar.Row, ...]:
    """Read the file plant.txt of a copy of a plant (THE PLANTS).

    Raises:
        BuildError: If the plant has no plant.txt, or the file does not obey the rows grammar
    """
    path = tree / MANIFEST
    try:
        return rows_grammar.read(path, MANIFEST)
    except (OSError, rows_grammar.RowsError) as problem:
        raise BuildError(f"the plant has no readable {MANIFEST}: {problem}") from None


def planted_build(tree: Path, build: Path, compiler: str) -> PlantedBuild:
    """Build the plant of a copied tree in a scratch build directory, and give its facts (THE PLANTS).

    The compiles of the library units run at the same time.  Complexity: one
    compile of each unit and one run of ar for each library.

    Raises:
        BuildError: If plant.txt does not obey its schema, or a unit does not compile
    """
    from concurrent.futures import ThreadPoolExecutor

    rows = manifest_rows(tree)
    shapes = {
        "kind": ("kind KIND", 2, 0),
        "tier": ("tier TIER", 2, 0),
        "cache": ("cache NAME VALUE", 3, 0),
        "library": ("library NAME UNIT...", 3, 0),
        "unit": ("unit TARGET UNIT | WORD...", 3, 1),
    }
    for row in rows:
        shape = shapes.get(row.words[0])
        if shape is not None and (
            len(row.words) < shape[1]
            or len(row.cells) != shape[2]
            or (row.words[0] != "library" and len(row.words) != shape[1])
        ):
            raise refused(row, shape[0])
    facts = {row.words[0]: row.words[1] for row in rows if row.words[0] in ("kind", "tier")}
    flags = next((row.words[1:] for row in rows if row.words[0] == "flags"), FLAGS)
    libraries = [row for row in rows if row.words[0] == "library"]
    build.mkdir(parents=True, exist_ok=True)
    units = sorted({unit for row in libraries for unit in row.words[2:]})

    def compile_unit(unit: str) -> None:
        """Compile one library unit of the plant into its object."""
        compiled([compiler, *flags, "-c", str(tree / unit), "-o", str(object_of(build, unit))], build)

    with ThreadPoolExecutor(max_workers=max(1, len(units))) as pool:
        list(pool.map(compile_unit, units))
    archives: dict[str, Path] = {}
    for row in libraries:
        archive = build / f"lib{row.words[1]}.a"
        compiled(["ar", "rcs", str(archive), *(str(object_of(build, unit)) for unit in row.words[2:])], build)
        archives[row.words[1]] = archive
    entries: list[compdb.CompileEntry] = []
    targets: dict[str, set[str]] = {}
    for row in (item for item in rows if item.words[0] == "unit"):
        output = str(build / row.words[1] / f"{row.words[2]}.o")
        words = [word.replace(ROOT_WORD, str(tree)) for word in row.cells[0].split()]
        command = shlex.join([compiler, *words, "-c", str(tree / row.words[2]), "-o", output])
        entries.append(compdb.CompileEntry(str(tree / row.words[2]), str(build), output, command))
        targets.setdefault(row.words[1], set()).add(output)
    cache = {row.words[1]: row.words[2] for row in rows if row.words[0] == "cache"}
    return PlantedBuild(
        facts.get("kind", PLANTED),
        facts.get("tier", PLANTED),
        compiler,
        archives,
        tuple(entries),
        {name: frozenset(paths) for name, paths in targets.items()},
        cache,
        rows,
    )


def plant_compiler() -> str | None:
    """Return the patched compiler that builds the plants, or None when no variable names a file."""
    compiler = testing.patched_compiler()
    return compiler if compiler and Path(compiler).is_file() else None


def plant_context(plant: Path, scratch: Path) -> ElfContext:
    """Copy a plant to a throwaway repository, build it in the scratch directory, and give its context (THE PLANTS).

    Raises:
        NotApplicable: If no variable names the patched compiler, or the plant does not build.  The witness counts
            each such plant as a failure
    """
    compiler = plant_compiler()
    if compiler is None:
        raise NotApplicable(
            f"{testing.TEST_COMPILER} or {testing.COMPILER_PREFIX} must name the patched compiler, "
            f"which builds the plants"
        )
    meta_runner.copy_plant(plant, scratch)
    try:
        return ElfContext(scratch, None, planted_build(scratch, scratch.parent / "build", compiler))
    except BuildError as problem:
        raise NotApplicable(str(problem)) from None


# ── The runs ───────────────────────────────────────────────────────


def run_rule(rule: rule_registry.Rule, context: ElfContext, warnings_dir: Path | None) -> int:
    """Run one rule on the build of the context, and write its findings (THE RUN).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    own_run = getattr(meta_runner.rule_module(rule), "run", None)
    try:
        if own_run is not None:
            return int(own_run(rule, context, warnings_dir))
        return meta_runner.run(rule, context, warnings_dir)
    except BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, rule.id, str(problem))], rule.id, warnings_dir
        )


def run(
    rules: Sequence[rule_registry.Rule], root: Path, build_dir: Path | None, warnings_dir: Path | None
) -> list[int]:
    """Run the rules on one context, and give the status of each.  The call then reads each fact one time."""
    context = ElfContext(root, build_dir)
    return [run_rule(rule, context, warnings_dir) for rule in rules]


def judged_plant(rule: rule_registry.Rule, context: meta_runner.RuleContext) -> list[check_report.Finding]:
    """Return the judged findings of one rule on a plant (THE RUN).

    When the facts of the plant cannot be read, the rule does not apply to it.

    Raises:
        NotApplicable: If the rule does not apply to the plant, or its build cannot be read
    """
    try:
        return meta_runner.module_judged(rule, context)
    except BuildError as problem:
        raise NotApplicable(f"the build of the plant cannot be read: {problem}") from None


def witness(rule: rule_registry.Rule, root: Path) -> int:
    """Run a rule on each of its plants and controls, with the build of each plant (THE PLANTS).

    Returns:
        0 when the witness holds, and 1 when it fails.  2 when the rule has no plant.  SKIPPED when no variable names
        the patched compiler
    """
    if plant_compiler() is None:
        check_report.write_line(
            f"{rule.id}: {testing.TEST_COMPILER} or {testing.COMPILER_PREFIX} must name the "
            f"patched compiler, which builds the plants.  The witness does not apply"
        )
        return SKIPPED
    return meta_runner.witness(rule, root, plant_context, judged_plant)


def write(rule: rule_registry.Rule, request: WriteRequest) -> int:
    """Write the ledger of one rule of the engine through the function write(rule, request) of its module.

    Returns:
        0 when the ledger is written, and 1 when the build or the ledger refuses the write.  2 when the rule writes
        no ledger.  SKIPPED when the rule does not apply
    """
    own_write = getattr(meta_runner.rule_module(rule), "write", None)
    if own_write is None:
        check_report.write_line(f"guard write: the rule {rule.id} has no ledger that a build writes", is_error=True)
        return 2
    return int(own_write(rule, request))
