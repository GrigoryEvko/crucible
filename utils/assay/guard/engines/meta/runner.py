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

THE FAMILIES
    A family module holds more than one rule.  Its table RULES maps the ID of
    each of its rules to a Member.  A Member gives the function of the
    findings of the rule.  For a rule of the policy max or baseline, it also
    gives the function that writes the ledger.  For a rule of the policy none,
    it can give the function that writes the data file of the rule.

    A rule with a module of its own is in the file ID.py of its engine
    directory.  module_name_of() gives that file when the file is there.  Else
    it gives the module that names the rule in its table.  rule_module() gives
    a FamilyRule for a rule of a family.  The runners call the functions
    findings, written and refresh of a FamilyRule, as they call the functions
    of a module.  Each other attribute, such as plant_context, comes from the
    family module.

THE REFUSED KINDS
    A rule module can name the kinds of the findings that no ledger row
    admits, in its constant REFUSED_KINDS.  Such a finding tells that the
    rule cannot read an input, such as a file that does not parse.  The rule
    then cannot count the items of that input.  judged() keeps each error of
    such a kind, and the ledger does not see it.  `guard write` refuses a
    finding of such a kind in each mode.

THE WITNESS
    The plants of a rule are in utils/plants/RULE/.  The prefix of the name of
    each directory tells what the rule must give on its tree:
      plant-    a minimum of one error: the rule refuses the tree
      control-  no error: the rule accepts the tree
      warning-  a minimum of one warning and no error, for example a time
                error that a CI runner demotes (assay.core.meter, A CI RUNNER)
      skip-     no finding, because the rule does not apply to the tree: it
                raises NotApplicable, and `guard check` exits with SKIPPED
    A plant can hold the file environment.txt, with one row `NAME | VALUE` for
    each variable that the run of the plant sets.  The witness removes each
    variable of PLANTED_VARIABLES from the environment for the run of a plant
    that does not set it.  The verdict of a plant then does not change with
    the host: GITHUB_ACTIONS of a CI runner demotes a time error.  The witness
    puts the environment back after each plant.  The witness copies each
    plant to a throwaway repository, puts each
    file in its index, and runs the rule there.  It judges the findings
    against the ledger of the copy (judged()).  A plant can then hold a ledger
    of the rule.  A copy with no ledger has the empty ledger.  A module can
    give plant_context(plant, scratch) when a plant needs more than a copy,
    for example a git history or a configured CMake project.  A rule that
    runs a tool of the host gives applies(), which raises NotApplicable when
    the host has no such tool.  The witness then exits with SKIPPED and runs
    no plant.  An engine gives the copy of its own context to witness(), and
    the judge of a rule that it judges in its own way.  A measured rule with a
    holder has no module of its own.  Its witness gets the module and the
    judge of its holder (assay.guard.engines.buildsys.held), and it counts
    each finding of a plant, also a warning.

THE STATUS
    A run exits 0 with no error, 1 with an error, and 77 (SKIPPED, the
    SKIP_RETURN_CODE of its ctest test) when the rule does not apply to the
    tree, for example a history that is shallow.
"""

import contextlib
import importlib
import os
import shutil
import sys
import tempfile
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass, replace
from pathlib import Path
from types import ModuleType
from typing import Any, Protocol

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

PLANTS = "utils/plants"
PLANT_PREFIX = "plant-"
CONTROL_PREFIX = "control-"
WARNING_PREFIX = "warning-"
SKIP_PREFIX = "skip-"
# The prefixes of the directories of the plants of a rule (THE WITNESS).
PREFIXES = (PLANT_PREFIX, CONTROL_PREFIX, WARNING_PREFIX, SKIP_PREFIX)
# The file of a plant with the variables of its run, and the variables that a plant sets only through that file.
ENVIRONMENT_FILE = "environment.txt"
PLANTED_VARIABLES = ("GITHUB_ACTIONS",)
# The suffix of a file of a plant that no rule of the tree reads.  The copy of the plant removes it (copy_plant()).
PLANTED_SUFFIX = ".planted"
# The suffixes of a file of a plant that adds its bytes to a file of the base control (copy_overlay()).
APPEND_SUFFIX = ".append"
PREPEND_SUFFIX = ".prepend"
SKIPPED = 77
# The guide of the tree.  Only the root of the repository holds a file with this name (the rule layout).
GUIDE = "CLAUDE.md"
# The directory of the modules of the engines, relative to the root, and the table of a family module (THE FAMILIES).
ENGINES = "utils/assay/guard/engines"
FAMILY_TABLE = "RULES"
# The constant of a rule module that names the kinds of the findings that no ledger row admits (THE REFUSED KINDS).
REFUSED_KINDS = "REFUSED_KINDS"


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

    __slots__ = ("_problems", "_registry", "_tracked", "build_dir", "guide", "root")

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
    """Return the dotted name of the module of a rule with a module of its own.

    Args:
        rule_id: The ID of the rule
        engine: The directory of the engine (assay.core.registry.Engine.directory)
    """
    return f"assay.guard.engines.{engine}.{rule_id.replace('-', '_')}"


def module_file(root: Path, module: str) -> Path | None:
    """Return the file of a dotted module of the package in a tree, such as assay.core.rows, or None.

    The file is utils/MODULE.py, or utils/MODULE/__init__.py for a package, where a slash replaces each point.
    """
    base = root / "utils" / Path(*module.split("."))
    for candidate in (base.with_suffix(".py"), base / "__init__.py"):
        if candidate.is_file():
            return candidate
    return None


def declared_rules(path: Path) -> tuple[bool, list[str]]:
    """Return True when a module holds the function findings, and the string keys of its table RULES (THE FAMILIES).

    The function reads the module with the Python ast module, and it imports none.
    """
    import ast

    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), str(path))
    except SyntaxError:
        return False, []
    has_function = any(isinstance(node, ast.FunctionDef) and node.name == "findings" for node in tree.body)
    keys: list[str] = []
    for node in tree.body:
        targets = (
            node.targets if isinstance(node, ast.Assign) else [node.target] if isinstance(node, ast.AnnAssign) else []
        )
        value = node.value if isinstance(node, (ast.Assign, ast.AnnAssign)) else None
        if any(isinstance(target, ast.Name) and target.id == FAMILY_TABLE for target in targets) and isinstance(
            value, ast.Dict
        ):
            keys += [key.value for key in value.keys if isinstance(key, ast.Constant) and isinstance(key.value, str)]
    return has_function, keys


def module_name_of(rule: rule_registry.Rule, root: Path = REPO_ROOT) -> str:
    """Return the dotted name of the module of a rule: its own module, else the family module that names it.

    When the engine directory holds no such module, the function gives the
    name of the own module of the rule.  Then the import fails with the name
    of the missing file.
    Complexity: one parse of each module of the engine directory, for a rule of a family.
    """
    own = module_name(rule.id, rule.engine.directory)
    directory = root / ENGINES / rule.engine.directory
    if (directory / f"{rule.id.replace('-', '_')}.py").is_file():
        return own
    for path in sorted(directory.glob("*.py")):
        if rule.id in declared_rules(path)[1]:
            return f"assay.guard.engines.{rule.engine.directory}.{path.stem}"
    return own


@dataclass(frozen=True, slots=True)
class Member:
    """One rule of a family module (THE FAMILIES).

    `findings` gives the findings of the rule on the context of its engine.
    `written` gives the new ledger of a rule of the policy max or baseline,
    from the context, the ledger and the reason of the call, and it is None
    for each other rule.  `refresh` writes the data file of a rule of the
    policy none again from the context, and it returns the line that tells
    the result.  The list cmake/compile-first.txt of the rule compile-first is
    such a file.  `refresh` is None for each other rule.  `configured` is
    True for a rule with the rows of each configuration of a build
    (assay.core.configuration, THE POLICY).  `guard write
    --add-configuration` applies only to such a member.
    """

    findings: Callable[[Any], list[check_report.Finding]]
    written: Callable[[Any, rule_ledger.Ledger, str | None], rule_ledger.Ledger] | None = None
    refresh: Callable[[Any], str] | None = None
    configured: bool = False


class FamilyRule:
    """One rule of a family module, with the functions that the runners call on the module of a rule."""

    __slots__ = ("member", "module")

    def __init__(self, module: ModuleType, member: Member) -> None:
        """Keep the family module and the member of the rule."""
        self.module = module
        self.member = member

    def findings(self, context: Any) -> list[check_report.Finding]:
        """Return the findings of the rule on the context of its engine."""
        return self.member.findings(context)

    def written(self, context: Any, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
        """Return the new ledger of the rule.

        Raises:
            rule_ledger.LedgerError: If the member of the rule has no function that writes its ledger
        """
        if self.member.written is None:
            raise rule_ledger.LedgerError(
                f"the family module {self.module.__name__} gives no write of the ledger of this rule"
            )
        return self.member.written(context, ledger, reason)

    @property
    def refresh(self) -> Callable[[Any], str]:
        """The write of the data file of the rule (Member).

        Raises:
            AttributeError: If the member of the rule writes no data file.  getattr() then takes its default
        """
        if self.member.refresh is None:
            raise AttributeError(f"the family module {self.module.__name__} writes no data file of this rule")
        return self.member.refresh

    def __getattr__(self, name: str) -> Any:
        """Give each other attribute of the family module, such as plant_context or judge, as a module gives it.

        Raises:
            AttributeError: If the family module has no such attribute, or the class gives the name itself.
                getattr() then takes its default
        """
        if name in FamilyRule.__slots__ or hasattr(FamilyRule, name):
            raise AttributeError(name)
        return getattr(self.module, name)


def rule_module(rule: rule_registry.Rule) -> ModuleType | FamilyRule:
    """Import the module of a rule, and give a FamilyRule for a rule of a family module (THE FAMILIES).

    Raises:
        ModuleNotFoundError: If the rule has no module
    """
    name = module_name_of(rule)
    module = importlib.import_module(name)
    table = getattr(module, FAMILY_TABLE, None)
    if (
        name != module_name(rule.id, rule.engine.directory)
        and isinstance(table, dict)
        and isinstance(table.get(rule.id), Member)
    ):
        return FamilyRule(module, table[rule.id])
    return module


def is_configured(rule: rule_registry.Rule) -> bool:
    """Return True when a rule of a family module holds the rows of each configuration of a build (Member)."""
    module = rule_module(rule)
    return isinstance(module, FamilyRule) and module.member.configured


def judged(
    rule: rule_registry.Rule,
    root: Path,
    raw: Sequence[check_report.Finding],
    kept: Callable[[rule_ledger.Key], bool] | None = None,
    configuration: str | None = None,
) -> list[check_report.Finding]:
    """Judge the findings of a rule against its ledger in a tree.

    A ledger that does not obey its schema gives one error, and the findings
    of the rule stay errors.  A rule of the policy max or baseline judges its
    ledger itself, so its findings stay as they are.  An error of a refused
    kind of the rule stays an error, and the ledger does not see it (THE
    REFUSED KINDS).  A row of such a kind then admits no finding, and the
    ledger gives its error.

    Args:
        rule: The rule
        root: The root of the tree of the ledger
        raw: The findings of the rule
        kept: The rows of the ledger that the run judges, when the run reads only a part of the tree.  None
            keeps each row
        configuration: The configuration of the ledger with the rows that judge the findings (assay.core.ledger,
            THE CONFIGURATIONS).  None judges them against the base rows
    """
    if rule.policy in (rule_registry.Policy.MAX, rule_registry.Policy.BASELINE):
        return list(raw)
    refused = refused_kinds(rule)
    fixed = [item for item in raw if item.level == "error" and item.kind in refused]
    counted = [item for item in raw if item.level != "error" or item.kind not in refused]
    try:
        ledger = rule_ledger.read(rule, root)
    except (rows_grammar.RowsError, OSError) as problem:
        return [
            *raw,
            check_report.Finding(
                "error",
                rule_ledger.shown_path(rule.id),
                getattr(problem, "line", 0),
                rule.id,
                f"the ledger cannot be read: {problem}",
            ),
        ]
    if kept is not None:
        ledger = replace(
            ledger,
            base={key: entry for key, entry in ledger.base.items() if kept(key)},
            deltas={
                name: {key: entry for key, entry in table.items() if kept(key)} for name, table in ledger.deltas.items()
            },
        )
    return [*rule_ledger.compare(rule, ledger, counted, configuration), *fixed]


def refused_kinds(rule: rule_registry.Rule) -> frozenset[str]:
    """Return the kinds of the findings of a rule that no ledger row admits (THE REFUSED KINDS).

    The module of the rule names them in its constant REFUSED_KINDS.  A rule with no such constant has none.
    """
    return frozenset(getattr(rule_module(rule), REFUSED_KINDS, frozenset()))


def module_judged(rule: rule_registry.Rule, context: RuleContext) -> list[check_report.Finding]:
    """Return the findings of a rule judged against its ledger in the tree of a context.

    A module that judges its rule in its own way gives a function judge(rule,
    context).  Each other rule is judged against its ledger (judged()).
    """
    own_judge = getattr(rule_module(rule), "judge", None)
    if own_judge is not None:
        return list(own_judge(rule, context))
    return judged(rule, context.root, context.findings_of(rule))


def run(
    rule: rule_registry.Rule,
    context: RuleContext,
    warnings_dir: Path | None,
    kept: Callable[[rule_ledger.Key], bool] | None = None,
    key: str | None = None,
) -> int:
    """Run one rule on the tree of the context, judge its findings against its ledger and print them.

    Args:
        rule: The rule
        context: The context of the engine of the rule
        warnings_dir: The directory of the warnings of a build, or None
        kept: The rows of the ledger that the run judges (judged()), or None for each row
        key: The writer key of the warnings file (assay.core.report, THE WARNINGS DIRECTORY), when the parts of
            one rule run at the same time, or None

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        raw = context.findings_of(rule)
    except NotApplicable as reason:
        print(f"{rule.id}: {reason}.  The rule does not apply")
        return SKIPPED
    findings = judged(rule, context.root, raw, kept)
    status = check_report.emit(findings, rule.id, warnings_dir, key)
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


def copy_overlay(plant: Path, scratch: Path, base: str) -> MetaContext:
    """Copy the tree of the base control of a plant, and then the files of the plant over it, to a throwaway repository.

    BASE names a control in the directory of the plant, and its tree is the
    first tree of each plant.  A file of the plant then replaces its file in the tree.  A
    file NAME.append adds its bytes at the end of NAME, and a file
    NAME.prepend adds them at the start.  Complexity: linear in the bytes of
    the files of the base and of the plant.
    """
    copy_plant(plant.parent / base, scratch)
    if plant.name == base:
        return MetaContext(scratch)
    for source in sorted(plant.rglob("*")):
        if not source.is_file():
            continue
        name = source.relative_to(plant).as_posix()
        mode = (
            APPEND_SUFFIX if name.endswith(APPEND_SUFFIX) else PREPEND_SUFFIX if name.endswith(PREPEND_SUFFIX) else ""
        )
        name = name.removesuffix(mode).removesuffix(PLANTED_SUFFIX)
        target = scratch / name
        target.parent.mkdir(parents=True, exist_ok=True)
        old = target.read_bytes() if mode and target.is_file() else b""
        added = source.read_bytes()
        target.write_bytes(old + added if mode == APPEND_SUFFIX else added + old)
    git.run(scratch, "add", "-A", "--", ".")
    return MetaContext(scratch)


def plants_of(rule_id: str, root: Path = REPO_ROOT) -> list[Path]:
    """Return the plant and control directories of a rule, sorted."""
    directory = root / PLANTS / rule_id
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.iterdir() if path.is_dir() and path.name.startswith(PREFIXES))


def plant_variables(plant: Path) -> tuple[tuple[str, str], ...]:
    """Return the name and the value of each variable that the file environment.txt of a plant sets (THE WITNESS).

    Raises:
        ValueError: If the file does not read, does not obey the rows grammar, or a row is not `NAME | VALUE`
    """
    path = plant / ENVIRONMENT_FILE
    try:
        rows = rows_grammar.read(path, path.name) if path.is_file() else ()
    except (rows_grammar.RowsError, OSError) as problem:
        raise ValueError(f"the file {ENVIRONMENT_FILE} of the plant does not read: {problem}") from None
    for row in rows:
        if len(row.words) != 1 or len(row.cells) != 1:
            raise ValueError(f"{ENVIRONMENT_FILE}:{row.line}: a row is `NAME | VALUE`")
    return tuple((row.words[0], row.cells[0]) for row in rows)


@contextlib.contextmanager
def planted_environment(variables: Sequence[tuple[str, str]]) -> Iterator[None]:
    """Run the body with the variables of a plant, with no other variable of PLANTED_VARIABLES, and put them back."""
    names = {*PLANTED_VARIABLES, *(name for name, _ in variables)}
    earlier = {name: os.environ.get(name) for name in names}
    for name in names:
        os.environ.pop(name, None)
    os.environ.update(variables)
    try:
        yield
    finally:
        for name, value in earlier.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def plant_verdict(name: str, found: Sequence[check_report.Finding], counts_warnings: bool) -> tuple[bool, str, str]:
    """Return if the findings on one plant hold its prefix, the text of its verdict, and the text of a failure.

    `counts_warnings` makes each warning a finding of a plant and of a control too (THE WITNESS).
    """
    errors = [item for item in found if item.level == "error"]
    warnings = [item for item in found if item.level != "error"]
    if name.startswith(WARNING_PREFIX):
        shown = f"{len(errors)} error(s) and {len(warnings)} warning(s)"
        first = f"; {(errors or warnings)[0].text()}" if found else ""
        return bool(warnings) and not errors, f"{shown}{first}", shown
    if name.startswith(SKIP_PREFIX):
        shown = f"the rule applies to it, with {len(found)} finding(s)"
        return False, f"{shown}{f'; {found[0].text()}' if found else ''}", shown
    noun = "finding" if counts_warnings else "error"
    counted = list(found) if counts_warnings else errors
    is_plant = name.startswith(PLANT_PREFIX)
    text = f"{len(counted)} {noun}(s); {counted[0].text() if counted else f'no {noun}'}"
    return bool(counted) if is_plant else not counted, text, f"no {noun}" if is_plant else f"{len(counted)} {noun}(s)"


def witness(
    rule: rule_registry.Rule,
    root: Path = REPO_ROOT,
    copy: Callable[[Path, Path], RuleContext] = copy_tree,
    judge: Callable[[rule_registry.Rule, RuleContext], list[check_report.Finding]] | None = None,
    *,
    module: ModuleType | FamilyRule | None = None,
    counts_warnings: bool = False,
) -> int:
    """Run a rule on each of its plants and controls (THE WITNESS), and print each verdict.

    Args:
        rule: The rule
        root: The root of the tree of the plants
        copy: The copy of a plant and the context of the engine, when the module of the rule gives no
            plant_context
        judge: The judged findings of the rule on the context of a plant, when the engine judges a rule in its own
            way.  None judges the findings of the rule against the ledger of the copy (judged())
        module: The module that gives plant_context and applies(), or None for the module of the rule
            (rule_module()).  A rule with a holder has no module of its own
        counts_warnings: True when a warning counts as a finding of a plant too.  A measured rule with two equal
            thresholds gives a warning only

    Returns:
        0 when each plant is refused and each control is accepted, 1 otherwise, 2 when the rule has no plant.
        SKIPPED when the function applies() of the module raises NotApplicable
    """
    module = rule_module(rule) if module is None else module
    prepare: Callable[[Path, Path], RuleContext] = getattr(module, "plant_context", copy)
    plants = plants_of(rule.id, root)
    if not plants:
        print(f"{rule.id}: no plant in {PLANTS}/{rule.id}/", file=sys.stderr)
        return 2
    applies: Callable[[], None] | None = getattr(module, "applies", None)
    if applies is not None:
        try:
            applies()
        except NotApplicable as reason:
            check_report.write_line(f"{rule.id}: {reason}.  The witness does not apply on this host")
            return SKIPPED
    failures: list[str] = []
    for plant in plants:
        try:
            variables = plant_variables(plant)
        except ValueError as problem:
            failures.append(f"{plant.name}: {problem}")
            continue
        with tempfile.TemporaryDirectory(prefix=f"witness-{rule.id}-") as scratch, planted_environment(variables):
            try:
                context = prepare(plant, Path(scratch) / "tree")
                found = (
                    judge(rule, context) if judge is not None else judged(rule, context.root, context.findings_of(rule))
                )
                holds, text, failure = plant_verdict(plant.name, found, counts_warnings)
            except NotApplicable as reason:
                holds = plant.name.startswith(SKIP_PREFIX)
                text = failure = f"the rule does not apply to it: {reason}"
        print(f"  {'ok  ' if holds else 'FAIL'} {plant.name}: {text}")
        if not holds:
            failures.append(f"{plant.name}: {failure}")
    if failures:
        print(f"{rule.id}: the witness fails: {'; '.join(failures)}", file=sys.stderr)
        return 1
    print(f"{rule.id}: the witness holds for {len(plants)} plants and controls")
    return 0
