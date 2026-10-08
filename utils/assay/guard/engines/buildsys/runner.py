"""The runner of the engine build: the inputs of a build that the rules of one call read, and the run of the rules.

THE CONTEXT
    A build rule reads one configured build directory.  Its inputs are the
    compile database, the replies of the CMake file API, build.ninja and the
    logs of Ninja.  They are also the records of the build launcher and of
    the tests, and the census of assay.readers.census.  BuildContext gives
    each input, and it reads each input one time for the rules of one call.
    A rule is a member of a family module of assay/guard/engines/buildsys/
    (assay.guard.engines.meta.runner, THE FAMILIES), or the function findings
    of a module of its own.  The run and the witness of
    assay.guard.engines.meta.runner judge the findings.

THE BUILD DIRECTORY
    A rule with the scope word builddir reads the build directory of
    --build-dir.  require_build() raises NotApplicable when the call gives no
    build directory, and the run then exits 77.  The rule cache-size reads
    the caches of the host and no build.  cache_root and cache_limits give
    the root of the caches and the limit of each one.

THE KIND
    kind() gives the build kind of BUILD_DIR/build-kind.txt
    (assay.core.meter).  A ledger row of a measured rule applies only to a
    build of its kind.

THE PLANTS
    A plant of a build rule holds the file build.txt, which tells what its
    planted build directory holds (assay.guard.engines.buildsys.planted).  The
    witness copies the plant to a throwaway repository and makes the planted
    build there (planted.plant_context()).

Complexity: one read of each input that a rule of the call reads.
"""

from collections.abc import Mapping, Sequence
from pathlib import Path

from assay.core import cache
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import compdb
from assay.readers import ninja as ninja_reader

# The status of a rule that does not apply to its input (the SKIP_RETURN_CODE of its ctest test).
SKIPPED = meta_runner.SKIPPED


class BuildContext:
    """The build that the build rules of one call read, and each input that a rule reads, read one time (THE CONTEXT).

    The context keeps each input after the first read, and the rules of one
    call share it.  A reader that refuses its input raises its own error, and
    the rule tells the problem in a finding.
    """

    __slots__ = (
        "_census",
        "_entries",
        "_graph",
        "_kind",
        "_model",
        "build_dir",
        "cache_limits",
        "cache_root",
        "registry",
        "root",
        "rule",
    )

    def __init__(
        self,
        root: Path,
        build_dir: Path | None,
        *,
        cache_root: Path | None = None,
        cache_limits: Mapping[str, int] | None = None,
        registry: rule_registry.Registry | None = None,
    ) -> None:
        """Keep the root, the build directory, the root and the limits of the caches, and the registry.

        Args:
            root: The root of the tree
            build_dir: The build directory, or None when the call gives none
            cache_root: The root of the caches, or None for the root of assay.core.cache
            cache_limits: The limit of each cache, or None for LIMITS of assay.core.cache
            registry: The registry that gives the thresholds of the measured rules, or None for the registry of
                this tree
        """
        self.root = root
        self.build_dir = build_dir
        self.rule: rule_registry.Rule | None = None
        self.cache_root = cache_root if cache_root is not None else cache.base_root()
        self.cache_limits: Mapping[str, int] = cache_limits if cache_limits is not None else cache.LIMITS
        self.registry = registry
        self._census: tuple[build_census.Census, bool] | None = None
        self._entries: tuple[compdb.CompileEntry, ...] | None = None
        self._model: cmake_reader.CodeModel | None = None
        self._graph: ninja_reader.Graph | None = None
        self._kind: str | None = None

    def require_build(self) -> Path:
        """Return the build directory of the call (THE BUILD DIRECTORY).

        Raises:
            NotApplicable: If the call gives no build directory
        """
        if self.build_dir is None:
            rule_id = self.rule.id if self.rule is not None else "the rule"
            raise meta_runner.NotApplicable(f"{rule_id} reads the build of --build-dir DIR, and the call gives none")
        return self.build_dir

    def shown(self, path: Path | str) -> str:
        """Return a path relative to the root when it is in the tree, else the path itself."""
        return build_census.shown(path, self.root)

    def budget(self, rule_id: str) -> check_report.Budget:
        """Return the thresholds of a measured rule of the registry of the context, for check_report.classify().

        Raises:
            NotApplicable: If the registry has no measured rule with that ID
        """
        registry = self.registry if self.registry is not None else rule_registry.load()
        self.registry = registry
        rule = registry.rules.get(rule_id)
        if rule is None or rule.threshold is None:
            raise meta_runner.NotApplicable(f"the registry has no measured rule {rule_id}")
        return check_report.Budget(
            rule.id,
            rule.threshold.warn,
            rule.threshold.error,
            check_report.unit_text(rule.threshold.unit),
            rule.refuses,
        )

    def kind(self) -> str:
        """Return the build kind of the build directory (THE KIND).

        Raises:
            NotApplicable: If the call gives no build directory, or the build has no kind
        """
        if self._kind is None:
            build = self.require_build()
            found = cost_meter.read_kind(str(build))
            if found is None:
                raise meta_runner.NotApplicable(
                    f"the build has no {self.shown(build / cost_meter.KIND_FILE)}, and the kind of the build is not "
                    f"known.  cmake/BuildLauncher.cmake writes the file at each configure"
                )
            self._kind = found
        return self._kind

    def census(self, read_files: bool = True) -> build_census.Census:
        """Return the census of the build (assay.readers.census), with the files of each unit when read_files.

        Raises:
            NotApplicable: If the call gives no build directory, or the census does not apply to the build
            build_census.CensusError: If an input of the census is there and cannot be read
        """
        if self._census is None or (read_files and not self._census[1]):
            build = self.require_build()
            try:
                self._census = (build_census.read_census(build, self.root, read_files), read_files)
            except build_census.NotApplicable as reason:
                raise meta_runner.NotApplicable(str(reason)) from None
        return self._census[0]

    def entries(self) -> tuple[compdb.CompileEntry, ...]:
        """Return the rows of the compile database of the build.

        Raises:
            NotApplicable: If the call gives no build directory
            compdb.CompdbError: If the build has no database, or the database cannot be read
        """
        if self._entries is None:
            self._entries = compdb.entries(self.require_build())
        return self._entries

    def codemodel(self) -> cmake_reader.CodeModel:
        """Return the code model of the build, from the replies of the CMake file API.

        Raises:
            NotApplicable: If the call gives no build directory
            cmake_reader.CmakeError: If the build has no reply, or a reply cannot be read
        """
        if self._model is None:
            self._model = cmake_reader.codemodel(self.require_build())
        return self._model

    def graph(self) -> ninja_reader.Graph:
        """Return the graph of build.ninja of the build.

        Raises:
            NotApplicable: If the call gives no build directory, or the build has no build.ninja because a generator
                other than Ninja made it
            ninja_reader.NinjaError: If build.ninja cannot be read
        """
        if self._graph is None:
            try:
                self._graph = ninja_reader.graph(self.require_build())
            except ninja_reader.NotNinja as reason:
                raise meta_runner.NotApplicable(str(reason)) from None
        return self._graph

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of the module of a rule.

        Raises:
            NotApplicable: If the rule does not apply to the build
        """
        self.rule = rule
        return list(meta_runner.rule_module(rule).findings(self))


def run(
    rules: Sequence[rule_registry.Rule], root: Path, build_dir: Path | None, warnings_dir: Path | None
) -> list[int]:
    """Run the rules on one context, and give the status of each one (assay.guard.engines.meta.runner.run())."""
    context = BuildContext(root, build_dir)
    return [meta_runner.run(rule, context, warnings_dir) for rule in rules]


def witness(rule: rule_registry.Rule, root: Path = REPO_ROOT) -> int:
    """Run a rule on each of its plants and controls (assay.guard.engines.meta.runner.witness())."""
    from assay.guard.engines.buildsys import planted

    return meta_runner.witness(rule, root, planted.plant_context)
