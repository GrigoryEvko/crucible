"""The runner of the engine meta: the context of a rule, the run of a rule against its ledger, and the witness of its plants.

THE CONTEXT
    A meta rule reads a tree: the registry, the ledgers, the modules of the
    package, the plants, the tracked files and the git history.  MetaContext
    gives the root of the tree, the build directory when the call gives one,
    and the registry, which it reads one time.  A rule is the function
    findings(context) of its module, assay/guard/engines/meta/ID.py with
    underscores in place of the hyphens.  Its errors are its violations, and
    assay.core.ledger judges them against the ledger of the rule.

    The run and the witness of this module also serve the other engines.  A
    context of each engine (RuleContext) gives its root, its build directory
    and findings_of(rule), the findings of one rule on its tree.  A rule of
    the policy max or baseline judges its ledger itself.  The run then gives
    its findings unchanged.

THE WITNESS
    The plants of a rule are in utils/plants/RULE/.  Each directory whose name
    starts with "plant-" is a tree that the rule must refuse: the rule gives a
    minimum of one error there.  Each directory whose name starts with
    "control-" is a tree that the rule must accept: the rule gives no error
    there.  The witness copies each one to a throwaway repository, puts each
    file in its index, and runs the rule there.  It judges the findings
    against the ledger of the copy (judged()).  A plant can then hold a ledger
    of the rule.  A copy with no ledger has the empty ledger.  A module can
    give plant_context(plant, scratch) when a plant needs more than a copy,
    for example a git history or a configured CMake project.  An engine gives
    the copy of its own context to witness(), and the judge of a rule that it
    judges in its own way.

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
from dataclasses import replace
from pathlib import Path
from types import ModuleType
from typing import Protocol

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

PLANTS = "utils/plants"
PLANT_PREFIX = "plant-"
CONTROL_PREFIX = "control-"
# The suffix of a file of a plant that no rule of the tree reads.  The copy of the plant removes it (copy_plant()).
PLANTED_SUFFIX = ".planted"
SKIPPED = 77
# The guide of the tree.  Only the root of the repository holds a file with this name (the rule layout).
GUIDE = "CLAUDE.md"


class NotApplicable(Exception):
    """The rule does not apply to the tree.  The message tells why."""


class RuleContext(Protocol):
    """The context of a rule of each engine: the root of its tree, the build directory, and the run of one rule."""

    @property
    def root(self) -> Path:
        """The root of the tree."""

    @property
    def build_dir(self) -> Path | None:
        """The build directory, when the call gives one."""

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of one rule on the tree, before the ledger judges them.

        Raises:
            NotApplicable: If the rule does not apply to the tree
        """


class MetaContext:
    """The tree that a meta rule reads: its root, its build directory, its guide and its registry.

    The guide is the path of the guide of the tree relative to its root:
    CLAUDE.md, or a planted guide of a plant.  The context reads the registry
    and the tracked files one time, on the first use.
    """

    __slots__ = ("root", "build_dir", "guide", "_registry", "_problems", "_tracked")

    def __init__(self, root: Path, build_dir: Path | None = None, guide: str = GUIDE) -> None:
        """Keep the root, the build directory and the path of the guide."""
        self.root = root
        self.build_dir = build_dir
        self.guide = guide
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

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of the function findings(context) of the module of a rule.

        Raises:
            NotApplicable: If the rule does not apply to the tree
        """
        return list(rule_module(rule).findings(self))


def pinned_program(tool: str) -> Path:
    """Return what `setup install --print-bin TOOL` prints for a tool of assay.setup.install.

    Raises:
        NotApplicable: If the tool is not installed at its pin
    """
    from assay.setup import install

    try:
        return install.program(tool)
    except install.InstallError as problem:
        raise NotApplicable(str(problem)) from None


def module_name(rule_id: str, engine: str = "meta") -> str:
    """Return the dotted name of the module of a rule."""
    return f"assay.guard.engines.{engine}.{rule_id.replace('-', '_')}"


def rule_module(rule: rule_registry.Rule) -> ModuleType:
    """Import the module of a rule.

    Raises:
        ModuleNotFoundError: If the rule has no module
    """
    return importlib.import_module(module_name(rule.id, rule.engine.value))


def judged(rule: rule_registry.Rule, root: Path, raw: Sequence[check_report.Finding],
           kept: Callable[[rule_ledger.Key], bool] | None = None) -> list[check_report.Finding]:
    """Judge the findings of a rule against its ledger in a tree.

    A ledger that does not obey its schema gives one error, and the findings
    of the rule stay errors.  A rule of the policy max or baseline judges its
    ledger itself, so its findings stay as they are.

    Args:
        rule: The rule
        root: The root of the tree of the ledger
        raw: The findings of the rule
        kept: The rows of the ledger that the run judges, when the run reads only a part of the tree.  None
            keeps each row
    """
    if rule.policy in (rule_registry.Policy.MAX, rule_registry.Policy.BASELINE):
        return list(raw)
    try:
        ledger = rule_ledger.read(rule, root)
    except (rows_grammar.RowsError, OSError) as problem:
        return [*raw, check_report.Finding("error", rule_ledger.shown_path(rule.id), getattr(problem, "line", 0),
                                           rule.id, f"the ledger cannot be read: {problem}")]
    if kept is not None:
        ledger = replace(ledger, base={key: entry for key, entry in ledger.base.items() if kept(key)},
                         deltas={name: {key: entry for key, entry in table.items() if kept(key)}
                                 for name, table in ledger.deltas.items()})
    return rule_ledger.compare(rule, ledger, raw)


def module_judged(rule: rule_registry.Rule, context: RuleContext) -> list[check_report.Finding]:
    """Return the findings of a rule judged against its ledger in the tree of a context.

    A module that judges its rule in its own way gives a function judge(rule,
    context).  Each other rule is judged against its ledger (judged()).
    """
    own_judge = getattr(rule_module(rule), "judge", None)
    if own_judge is not None:
        return list(own_judge(rule, context))
    return judged(rule, context.root, context.findings_of(rule))


def run(rule: rule_registry.Rule, context: RuleContext, warnings_dir: Path | None,
        kept: Callable[[rule_ledger.Key], bool] | None = None) -> int:
    """Run one rule on the tree of the context, judge its findings against its ledger and print them.

    Args:
        rule: The rule
        context: The context of the engine of the rule
        warnings_dir: The directory of the warnings of a build, or None
        kept: The rows of the ledger that the run judges (judged()), or None for each row

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        raw = context.findings_of(rule)
    except NotApplicable as reason:
        print(f"{rule.id}: {reason}.  The rule does not apply")
        return SKIPPED
    findings = judged(rule, context.root, raw, kept)
    status = check_report.emit(findings, rule.id, warnings_dir)
    errors = sum(item.level == "error" for item in findings)
    print(f"{rule.id}: {errors} error(s), {len(findings) - errors} warning(s)")
    return status


def copy_plant(plant: Path, scratch: Path) -> None:
    """Copy a plant to a throwaway repository, give each file NAME.planted its name NAME, and put each file in the index.

    A file of a plant with the suffix .planted is data of the plant, which no
    rule of the tree reads: a guide CLAUDE.md, or the C++ of a plant of a
    tsast rule.  Complexity: linear in the files of the plant.
    """
    shutil.copytree(plant, scratch, dirs_exist_ok=True)
    for planted in sorted(scratch.rglob(f"*{PLANTED_SUFFIX}")):
        planted.rename(planted.with_name(planted.name.removesuffix(PLANTED_SUFFIX)))
    git.init(scratch)
    git.run(scratch, "add", "-A", "--", ".")


def copy_tree(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository (copy_plant()), and give its context."""
    copy_plant(plant, scratch)
    return MetaContext(scratch)


def plants_of(rule_id: str, root: Path = REPO_ROOT) -> list[Path]:
    """Return the plant and control directories of a rule, sorted."""
    directory = root / PLANTS / rule_id
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.iterdir() if path.is_dir()
                  and path.name.startswith((PLANT_PREFIX, CONTROL_PREFIX)))


def witness(rule: rule_registry.Rule, root: Path = REPO_ROOT,
            copy: Callable[[Path, Path], RuleContext] = copy_tree,
            judge: Callable[[rule_registry.Rule, RuleContext], list[check_report.Finding]] | None = None) -> int:
    """Run a rule on each of its plants and controls (THE WITNESS), and print each verdict.

    Args:
        rule: The rule
        root: The root of the tree of the plants
        copy: The copy of a plant and the context of the engine, when the module of the rule gives no
            plant_context
        judge: The judged findings of the rule on the context of a plant, when the engine judges a rule in its own
            way.  None judges the findings of the rule against the ledger of the copy (judged())

    Returns:
        0 when each plant is refused and each control is accepted, 1 otherwise, 2 when the rule has no plant
    """
    module = rule_module(rule)
    prepare: Callable[[Path, Path], RuleContext] = getattr(module, "plant_context", copy)
    plants = plants_of(rule.id, root)
    if not plants:
        print(f"{rule.id}: no plant in {PLANTS}/{rule.id}/", file=sys.stderr)
        return 2
    failures: list[str] = []
    for plant in plants:
        with tempfile.TemporaryDirectory(prefix=f"witness-{rule.id}-") as scratch:
            try:
                context = prepare(plant, Path(scratch) / "tree")
                found = judge(rule, context) if judge is not None else judged(rule, context.root,
                                                                              context.findings_of(rule))
                errors = [item for item in found if item.level == "error"]
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
