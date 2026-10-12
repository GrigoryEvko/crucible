"""The runner of the engine elf: the facts of a build, the plants, and the run, the witness and the write of a rule.

THE BUILD
    A rule of the engine elf reads the objects, the static libraries and the
    compile database of a build.  ElfContext reads each fact of the build one
    time, on its first use:
      * The build kind of build-kind.txt (assay.core.target.kind_of)
      * The compiler of the CMake cache, CMAKE_CXX_COMPILER.  The compiler
        PREFIX/usr/bin/COMPILER, or PREFIX/bin/COMPILER, has the prefix
        PREFIX (toolchain_prefix())
      * The target tier of the build (assay.core.target.build_tier)
      * The static libraries of the root CMakeLists.txt.  Each one is a
        target of the type STATIC_LIBRARY in the code model of the CMake file
        API.  The source root is its source directory.  A library of a
        subdirectory, such as a test library, is not one of them
      * The libraries of the link lines that are not files of the build
        directory.  The code model gives each fragment of the role libraries
        of an executable, a shared library or a module.  A word -lNAME stays
        as it is, a path becomes absolute, and each other flag goes
      * The linked artifacts of the target all: each executable, shared
        library and module with its file in the inputs of all.  The code
        model gives each one, and `ninja -t inputs all` gives the inputs.
        Each one comes as a LinkedArtifact with the type of its target.  It
        also holds the first directory of its source directory, or an empty
        text for the source root
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
        archive NAME UNIT...           compile each unit into the archive
                                       libNAME.a of a link line.  The library
                                       is not a library of the root
        shared NAME UNIT...            compile each unit with -fPIC, and link
                                       the shared library libNAME.so of a link
                                       line
        program DIRECTORY NAME UNIT... [| WORD...]
                                       compile each unit, and link the program
                                       DIRECTORY/NAME of the target all with
                                       its map (cmake/LinkMap.cmake).  Its
                                       source directory is DIRECTORY.  The
                                       words of the cell go at the end of the
                                       link line
        module DIRECTORY NAME UNIT... [| WORD...]
                                       compile each unit with -fPIC, and link
                                       the shared library DIRECTORY/NAME of
                                       the target all with -shared and its
                                       map, as a program row does
        unit TARGET UNIT | WORD...     a row of the compile database: the
                                       unit, an object of the target TARGET,
                                       with the compiler and the words
        cache NAME VALUE               a variable of the CMake cache

    UNIT is a path relative to the tree.  A word {root} of a unit row, a
    program row or a module row is the root of the copy.  A word {cfi} of a
    flags row is the flag of the control-flow protection of the host
    (CFI_FLAGS): -fcf-protection=full on x86_64, and
    -mbranch-protection=standard on aarch64.  A row with a different head
    word is a row of the module of the rule, which reads it from the
    PlantedBuild.  A plant that the rule refuses must give an error, and a
    control must give none.

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

import os
import platform
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
# The targets with a link line, and the role of a library fragment of that line.
EXECUTABLE = "EXECUTABLE"
MODULE_LIBRARY = "MODULE_LIBRARY"
LINKED_TYPES = frozenset({EXECUTABLE, "SHARED_LIBRARY", MODULE_LIBRARY})
LIBRARY_ROLE = "libraries"
LIBRARY_FLAG = "-l"
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
MANIFEST = "plant.txt"
PLANTED = "planted"
# The words of each compile of a library unit of a plant.  -ffunction-sections puts each function in a section of
# its own, as each build of the tree does.
FLAGS = ("-std=c++17", "-O1", "-ffunction-sections")
ROOT_WORD = "{root}"
# The word of a flags row for the control-flow protection, and the flag of each machine of the host (THE PLANTS).
CFI_WORD = "{cfi}"
CFI_FLAGS = {"x86_64": "-fcf-protection=full", "aarch64": "-mbranch-protection=standard"}
# The rows of plant.txt that compile units into a library, and the rows of a linked artifact of all (THE PLANTS).
LIBRARY_ROWS = frozenset({"library", "archive", "shared"})
PROGRAM_ROW = "program"
MODULE_ROW = "module"
LINKED_ROWS = frozenset({PROGRAM_ROW, MODULE_ROW})
SKIPPED = meta_runner.SKIPPED
NotApplicable = meta_runner.NotApplicable


class BuildError(Exception):
    """The runner cannot read a fact of the build, or a plant does not build."""


def toolchain_prefix(compiler: str) -> Path:
    """Return the prefix of a compiler: PREFIX of PREFIX/usr/bin/COMPILER or of PREFIX/bin/COMPILER (THE BUILD)."""
    binaries = Path(compiler).parent
    return binaries.parent.parent if binaries.parent.name == "usr" else binaries.parent


@dataclass(frozen=True, slots=True)
class LinkedArtifact:
    """One linked artifact of the target all (THE BUILD).

    `directory` is the first directory of the source directory of its target, or an empty text for the source root.
    `path` is its file, and `target_type` is the type of its target in the code model: EXECUTABLE, SHARED_LIBRARY or
    MODULE_LIBRARY.
    """

    directory: str
    path: Path
    target_type: str

    @property
    def is_executable(self) -> bool:
        """True for the artifact of a target of the type EXECUTABLE."""
        return self.target_type == EXECUTABLE


@dataclass(frozen=True, slots=True)
class PlantedBuild:
    """The facts of a build that a test or a plant gives (THE BUILD).

    `libraries` gives the archive of each static library of the root by its target name, `targets` gives the objects
    of each target, and `rows` holds each row of plant.txt for the module of the rule.  `link_libraries` gives each
    library of a link line that is not a file of the build directory, and `linked` each linked artifact of the target
    all.
    """

    kind: str = PLANTED
    tier: str = PLANTED
    compiler: str = ""
    libraries: Mapping[str, Path] = field(default_factory=dict)
    entries: tuple[compdb.CompileEntry, ...] = ()
    targets: Mapping[str, frozenset[str]] = field(default_factory=dict)
    cache: Mapping[str, str] = field(default_factory=dict)
    rows: tuple[rows_grammar.Row, ...] = ()
    link_libraries: tuple[str, ...] = ()
    linked: tuple[LinkedArtifact, ...] = ()


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
        "_linked",
        "_links",
        "_model",
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
        self._model: cmake.CodeModel | None = None
        self._kind: str | None = None if planted is None else planted.kind
        self._compiler: str | None = None if planted is None else planted.compiler
        self._tier: str | None = None if planted is None else planted.tier
        self._libraries: dict[str, Path] | None = None if planted is None else dict(planted.libraries)
        self._links: tuple[str, ...] | None = None if planted is None else planted.link_libraries
        self._linked: tuple[LinkedArtifact, ...] | None = None if planted is None else planted.linked
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
            model = self.code_model()
            source = Path(model.source).resolve()
            self._libraries = {
                target.name: Path(target.artifacts[0])
                for target in model.targets
                if target.type_name == STATIC_LIBRARY
                and target.artifacts
                and Path(target.source_dir).resolve() == source
            }
        return self._libraries

    def code_model(self) -> cmake.CodeModel:
        """Return the code model of the CMake file API of the build.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the code model cannot be read
        """
        if self._model is None:
            try:
                self._model = cmake.codemodel(self.directory())
            except cmake.CmakeError as problem:
                raise BuildError(f"the code model of {self.build_shown()} cannot be read: {problem}") from None
        return self._model

    def link_libraries(self) -> tuple[str, ...]:
        """Return each library of a link line that is not a file of the build directory (THE BUILD).

        Each item is a word -lNAME or an absolute path, in the sequence of the
        targets of the code model, with no second copy.  Complexity: linear in
        the link fragments of the targets.

        Raises:
            NotApplicable: If the call gives no build directory
            BuildError: If the code model cannot be read
        """
        if self._links is None:
            directory = self.directory().resolve()
            found: dict[str, None] = {}
            for target in self.code_model().targets:
                if target.type_name not in LINKED_TYPES:
                    continue
                for fragment, role in target.link_fragments:
                    if role != LIBRARY_ROLE:
                        continue
                    for word in shlex.split(fragment):
                        if word.startswith(LIBRARY_FLAG):
                            found.setdefault(word, None)
                        elif not word.startswith("-"):
                            path = Path(os.path.normpath(directory / word))
                            if not path.is_relative_to(directory):
                                found.setdefault(str(path), None)
            self._links = tuple(found)
        return self._links

    def linked_artifacts(self) -> tuple[LinkedArtifact, ...]:
        """Return each linked artifact of the target all (THE BUILD).

        The items come in the sequence of their paths.  Complexity: one run of
        `ninja -t inputs all`, and linear in the targets of the code model.

        Raises:
            NotApplicable: If the call gives no build directory, or the generator of the build is not Ninja
            BuildError: If the code model or the inputs of all cannot be read, or a target has no source directory in
                the source root
        """
        if self._linked is None:
            directory = self.directory()
            try:
                inputs = {os.path.normpath(directory / name) for name in ninja.inputs(directory, "all")}
            except ninja.NotNinja as reason:
                raise NotApplicable(str(reason)) from None
            except ninja.NinjaError as problem:
                raise BuildError(f"ninja does not give the inputs of the target all: {problem}") from None
            model = self.code_model()
            source = Path(model.source).resolve()
            found: list[LinkedArtifact] = []
            for target in model.targets:
                if target.type_name not in LINKED_TYPES or not target.artifacts:
                    continue
                if os.path.normpath(target.artifacts[0]) not in inputs:
                    continue
                source_dir = Path(target.source_dir).resolve()
                if not source_dir.is_relative_to(source):
                    raise BuildError(f"the target {target.name} has the source directory {source_dir}, not in {source}")
                parts = source_dir.relative_to(source).parts
                found.append(LinkedArtifact(parts[0] if parts else "", Path(target.artifacts[0]), target.type_name))
            self._linked = tuple(sorted(found, key=lambda item: item.path))
        return self._linked

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


def object_of(build: Path, unit: str, is_shared: bool = False) -> Path:
    """Return the object of a library unit of a plant in the scratch build directory, with -fPIC for a shared one."""
    return build / f"{unit.replace('/', '_')}{'.pic' if is_shared else ''}.o"


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


def host_flags(words: Sequence[str]) -> list[str]:
    """Return the words of a flags row with the flag of CFI_FLAGS for the machine of the host in place of {cfi}.

    Raises:
        BuildError: If a word is {cfi} and CFI_FLAGS has no flag for the machine of the host
    """
    machine = platform.machine()
    if CFI_WORD in words and machine not in CFI_FLAGS:
        raise BuildError(f"the plant gives {CFI_WORD}, and the runner has no control-flow flag for {machine}")
    return [CFI_FLAGS[machine] if word == CFI_WORD else word for word in words]


def linked_artifact(row: rows_grammar.Row, tree: Path, build: Path, compiler: str) -> LinkedArtifact:
    """Link the program or the shared library of a program row or a module row of plant.txt, with its map.

    Raises:
        BuildError: If the link fails
    """
    is_module = row.words[0] == MODULE_ROW
    artifact = build / row.words[1] / row.words[2]
    artifact.parent.mkdir(parents=True, exist_ok=True)
    objects = [str(object_of(build, unit, is_module)) for unit in row.words[3:]]
    words = [word.replace(ROOT_WORD, str(tree)) for cell in row.cells for word in rows_grammar.head_words(cell)]
    shared = ["-shared"] if is_module else []
    compiled([compiler, *shared, *objects, "-o", str(artifact), f"-Wl,-Map={artifact}.map", *words], build)
    return LinkedArtifact(row.words[1], artifact, MODULE_LIBRARY if is_module else EXECUTABLE)


def planted_build(tree: Path, build: Path, compiler: str) -> PlantedBuild:
    """Build the plant of a copied tree in a scratch build directory, and give its facts (THE PLANTS).

    The compiles of the units run at the same time.  Complexity: one compile
    of each unit, and one run of ar or of the link for each library, each
    program and each module.

    Raises:
        BuildError: If plant.txt does not obey its schema, or a unit does not compile or link
    """
    from concurrent.futures import ThreadPoolExecutor

    rows = manifest_rows(tree)
    # The shape of each row, its number of words, and the numbers of cells that it can have (THE PLANTS).
    shapes = {
        "kind": ("kind KIND", 2, (0,)),
        "tier": ("tier TIER", 2, (0,)),
        "cache": ("cache NAME VALUE", 3, (0,)),
        "library": ("library NAME UNIT...", 3, (0,)),
        "archive": ("archive NAME UNIT...", 3, (0,)),
        "shared": ("shared NAME UNIT...", 3, (0,)),
        PROGRAM_ROW: ("program DIRECTORY NAME UNIT... [| WORD...]", 4, (0, 1)),
        MODULE_ROW: ("module DIRECTORY NAME UNIT... [| WORD...]", 4, (0, 1)),
        "unit": ("unit TARGET UNIT | WORD...", 3, (1,)),
    }
    for row in rows:
        shape = shapes.get(row.words[0])
        if shape is not None and (
            len(row.words) < shape[1]
            or len(row.cells) not in shape[2]
            or (row.words[0] not in (*LIBRARY_ROWS, *LINKED_ROWS) and len(row.words) != shape[1])
        ):
            raise refused(row, shape[0])
    facts = {row.words[0]: row.words[1] for row in rows if row.words[0] in ("kind", "tier")}
    flags = host_flags(next((row.words[1:] for row in rows if row.words[0] == "flags"), FLAGS))
    built = [row for row in rows if row.words[0] in LIBRARY_ROWS]
    artifacts = [row for row in rows if row.words[0] in LINKED_ROWS]
    build.mkdir(parents=True, exist_ok=True)
    units = sorted(
        {(unit, row.words[0] == "shared") for row in built for unit in row.words[2:]}
        | {(unit, row.words[0] == MODULE_ROW) for row in artifacts for unit in row.words[3:]}
    )

    def compile_unit(item: tuple[str, bool]) -> None:
        """Compile one library unit of the plant into its object, with -fPIC for a shared library."""
        unit, is_shared = item
        position = ["-fPIC"] if is_shared else []
        compiled(
            [compiler, *flags, *position, "-c", str(tree / unit), "-o", str(object_of(build, unit, is_shared))], build
        )

    with ThreadPoolExecutor(max_workers=min(proc.DEFAULT_JOBS, max(1, len(units)))) as pool:
        list(pool.map(compile_unit, units))
    archives: dict[str, Path] = {}
    links: list[str] = []
    for row in built:
        name, is_shared = row.words[1], row.words[0] == "shared"
        objects = [str(object_of(build, unit, is_shared)) for unit in row.words[2:]]
        if is_shared:
            library = build / "links" / f"lib{name}.so"
            library.parent.mkdir(exist_ok=True)
            compiled([compiler, "-shared", *objects, "-o", str(library)], build)
        else:
            library = (build if row.words[0] == "library" else build / "links") / f"lib{name}.a"
            library.parent.mkdir(exist_ok=True)
            compiled(["ar", "rcs", str(library), *objects], build)
        if row.words[0] == "library":
            archives[name] = library
        else:
            links.append(str(library))
    linked = [linked_artifact(row, tree, build, compiler) for row in artifacts]
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
        tuple(links),
        tuple(linked),
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
