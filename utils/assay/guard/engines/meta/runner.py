"""The runner of the engine meta: the context of a rule, the run of a rule against its ledger, and the witness of its plants.

THE CONTEXT
    A meta rule reads a tree: the registry, the ledgers, the modules of the
    package, the plants, the tracked files and the git history.  MetaContext
    gives the root of the tree, the build directory when the call gives one,
    and the registry, which it reads one time.  A rule is the function
    findings(context) of its module, assay/guard/engines/meta/ID.py with
    underscores in place of the hyphens.  Its errors are its violations, and
    assay.core.ledger judges them against the ledger of the rule.

THE WITNESS
    The plants of a rule are in utils/plants/RULE/.  Each directory whose name
    starts with "plant-" is a tree that the rule must refuse: the rule gives a
    minimum of one error there.  Each directory whose name starts with
    "control-" is a tree that the rule must accept: the rule gives no error
    there.  The witness copies each one to a throwaway repository, puts each
    file in its index, and runs the rule there with no ledger.  A module can
    give plant_context(plant, scratch) when a plant needs more than a copy,
    for example a git history or a configured CMake project.

THE STATUS
    A run exits 0 with no error, 1 with an error, and 77 (SKIPPED, the
    SKIP_RETURN_CODE of its ctest test) when the rule does not apply to the
    tree, for example a history that is shallow.
"""

import importlib
import shutil
import sys
import tempfile
from collections.abc import Callable, Sequence
from pathlib import Path
from types import ModuleType

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

PLANTS = "utils/plants"
PLANT_PREFIX = "plant-"
CONTROL_PREFIX = "control-"
SKIPPED = 77


class NotApplicable(Exception):
    """The rule does not apply to the tree.  The message tells why."""


class MetaContext:
    """The tree that a meta rule reads: its root, its build directory and its registry.

    The context reads the registry and the tracked files one time, on the
    first use.
    """

    __slots__ = ("root", "build_dir", "_registry", "_problems", "_tracked")

    def __init__(self, root: Path, build_dir: Path | None = None) -> None:
        """Keep the root and the build directory."""
        self.root = root
        self.build_dir = build_dir
        self._registry: rule_registry.Registry | None = None
        self._problems: list[rows_grammar.RowsError] = []
        self._tracked: tuple[str, ...] | None = None

    def registry_text(self) -> str:
        """Return the text of the registry of the tree, or an empty text when the tree has none."""
        path = self.root / rule_registry.REGISTRY_PATH
        return path.read_text(encoding="utf-8") if path.is_file() else ""

    def _examine(self) -> None:
        """Read the registry one time."""
        if self._registry is None:
            self._registry, self._problems = rule_registry.examine(self.registry_text())

    @property
    def registry(self) -> rule_registry.Registry:
        """The rows of the registry that read.  The rule registry-shape gives the problems of the others."""
        self._examine()
        assert self._registry is not None
        return self._registry

    @property
    def problems(self) -> list[rows_grammar.RowsError]:
        """The problems of the rows of the registry."""
        self._examine()
        return self._problems

    def tracked(self) -> tuple[str, ...]:
        """Return each tracked file of the tree and each new file that .gitignore does not match, sorted.

        Raises:
            NotApplicable: If the root is not a git work tree
        """
        if self._tracked is None:
            try:
                self._tracked = git.tracked_files(self.root, with_new=True)
            except git.GitError as problem:
                raise NotApplicable(f"the tree {self.root} is not a git work tree: {problem}") from None
        return self._tracked


def module_name(rule_id: str, engine: str = "meta") -> str:
    """Return the dotted name of the module of a rule."""
    return f"assay.guard.engines.{engine}.{rule_id.replace('-', '_')}"


def rule_module(rule: rule_registry.Rule) -> ModuleType:
    """Import the module of a rule.

    Raises:
        ModuleNotFoundError: If the rule has no module
    """
    return importlib.import_module(module_name(rule.id, rule.engine.value))


def judged(rule: rule_registry.Rule, context: MetaContext,
           raw: Sequence[check_report.Finding]) -> list[check_report.Finding]:
    """Judge the findings of a rule against its ledger in the tree of the context.

    A ledger that does not obey its schema gives one error, and the findings of the rule stay errors.
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
    except (rows_grammar.RowsError, OSError) as problem:
        return [*raw, check_report.Finding("error", rule_ledger.shown_path(rule.id), getattr(problem, "line", 0),
                                           rule.id, f"the ledger cannot be read: {problem}")]
    return rule_ledger.compare(rule, ledger, raw)


def run(rule: rule_registry.Rule, context: MetaContext, warnings_dir: Path | None) -> int:
    """Run one rule on the tree of the context, judge its findings against its ledger and print them.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        raw = rule_module(rule).findings(context)
    except NotApplicable as reason:
        print(f"{rule.id}: {reason}.  The rule does not apply")
        return SKIPPED
    findings = judged(rule, context, raw)
    status = check_report.emit(findings, rule.id, warnings_dir)
    errors = sum(item.level == "error" for item in findings)
    print(f"{rule.id}: {errors} error(s), {len(findings) - errors} warning(s)")
    return status


def copy_tree(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository, put each file in its index, and give its context."""
    shutil.copytree(plant, scratch, dirs_exist_ok=True)
    git.init(scratch)
    git.run(scratch, "add", "-A", "--", ".")
    return MetaContext(scratch)


def plants_of(rule_id: str, root: Path = REPO_ROOT) -> list[Path]:
    """Return the plant and control directories of a rule, sorted."""
    directory = root / PLANTS / rule_id
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.iterdir() if path.is_dir()
                  and path.name.startswith((PLANT_PREFIX, CONTROL_PREFIX)))


def witness(rule: rule_registry.Rule, root: Path = REPO_ROOT) -> int:
    """Run a rule on each of its plants and controls (THE WITNESS), and print each verdict.

    Returns:
        0 when each plant is refused and each control is accepted, 1 otherwise, 2 when the rule has no plant
    """
    module = rule_module(rule)
    prepare: Callable[[Path, Path], MetaContext] = getattr(module, "plant_context", copy_tree)
    plants = plants_of(rule.id, root)
    if not plants:
        print(f"{rule.id}: no plant in {PLANTS}/{rule.id}/", file=sys.stderr)
        return 2
    failures: list[str] = []
    for plant in plants:
        with tempfile.TemporaryDirectory(prefix=f"witness-{rule.id}-") as scratch:
            try:
                context = prepare(plant, Path(scratch) / "tree")
                errors = [item for item in module.findings(context) if item.level == "error"]
            except NotApplicable as reason:
                failures.append(f"{plant.name}: the rule does not apply to it: {reason}")
                continue
        is_plant = plant.name.startswith(PLANT_PREFIX)
        holds = bool(errors) if is_plant else not errors
        shown = errors[0].text() if errors else "no error"
        print(f"  {'ok  ' if holds else 'FAIL'} {plant.name}: {len(errors)} error(s); {shown}")
        if not holds:
            failures.append(f"{plant.name}: {'no error' if is_plant else f'{len(errors)} error(s)'}")
    if failures:
        print(f"{rule.id}: the witness fails: {'; '.join(failures)}", file=sys.stderr)
        return 1
    print(f"{rule.id}: the witness holds for {len(plants)} plants and controls")
    return 0
