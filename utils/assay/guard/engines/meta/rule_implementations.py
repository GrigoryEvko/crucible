"""rule-implementations — each rule of the registry has one function, and each function has one rule.

THE RULE
    A rule with no legacy module and no holder has the module
    assay/guard/engines/DIRECTORY/ID.py with the function findings.  In the
    name of the file, an underscore replaces each hyphen of the ID.  DIRECTORY
    is the directory of the engine of the rule
    (assay.core.registry.Engine.directory).  A family module holds a table
    RULES.  Each key of the table is the ID of a rule that the module gives
    findings for.  The rule refuses:
      no-function     a rule with no such module and no family table that names it
      orphan          a module that has findings or RULES for an ID that is not a rule of the registry
      engine          a module in the directory of an engine that is not the engine of its rule
      legacy          a rule that has only a legacy module.  During the migration of the utils system, the
                      ledger admits each one.  The change that replaces the legacy module removes its row
      missing-legacy  a legacy module that the tree does not hold
    The rule reads each module with the Python ast module, and it imports
    none.  A module with no function findings and no table RULES is a helper,
    such as the runner of an engine.

THE HOLDERS
    A rule with held-by has no module of its own.  Its holder judges it, and
    its field holder names the function of the holder that judges it
    (assay.core.registry).  The witness of the rule runs that function
    (assay.guard.engines.buildsys.held).  The rule refuses:
      no-holder       a rule with held-by and no field holder
      missing-holder  a field holder that names a module that the tree does
                      not hold, or a module that does not parse.  The kind
                      also refuses a function that is not at the top level of
                      its module
      unrun-holder    a function that the witness does not run
    When held-by names a command, the judge of that command in the table
    HOLDERS of held.py must call the function.  The import table of held.py
    gives the name of each callee (assay.readers.python).  When held-by names
    a rule, the function must be in the module that holds that rule, because
    the witness runs that module.  No ledger row admits a finding of these
    three kinds (REFUSED_KINDS).

Complexity: linear in the bytes of the modules of assay/guard/engines/, and one parse of each module that a field
holder names.
"""

import ast
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import ENGINES, FAMILY_TABLE, MetaContext, declared_rules, module_file
from assay.readers import python as python_reader

RULE = "rule-implementations"
# The module of the witness of a held rule, relative to the directory of the engines, and its table of judges.
HELD_MODULE = "buildsys/held.py"
HOLDERS_TABLE = "HOLDERS"
# The kinds of THE HOLDERS.  No ledger row admits them.
NO_HOLDER = "no-holder"
MISSING_HOLDER = "missing-holder"
UNRUN_HOLDER = "unrun-holder"
REFUSED_KINDS = frozenset({NO_HOLDER, MISSING_HOLDER, UNRUN_HOLDER})

type FunctionNode = ast.FunctionDef | ast.AsyncFunctionDef


def read_source(path: Path, root: Path) -> python_reader.Source | None:
    """Return the source of a Python file of a tree, or None when the file cannot be read or does not parse."""
    try:
        return python_reader.read(path, path.relative_to(root).as_posix())
    except OSError, python_reader.PythonError:
        return None


def top_functions(source: python_reader.Source) -> dict[str, FunctionNode]:
    """Return each function at the top level of a module, by its name."""
    return {node.name: node for node in source.tree.body if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}


def called_names(function: FunctionNode, table: python_reader.Bindings) -> frozenset[str]:
    """Return the dotted name of each callee of a function, such as assay.loop.gauge.judged_record.

    Complexity: linear in the nodes of the function.
    """
    names: set[str] = set()
    for node in ast.walk(function):
        if isinstance(node, ast.Call):
            name = python_reader.dotted(node.func, table)
            if name is not None:
                names.add(name)
    return frozenset(names)


def judged_calls(root: Path) -> dict[str, frozenset[str]]:
    """Return the callees of the judge of each command in the table HOLDERS of held.py.

    A tree with no such module has no judge.  A command with a judge that is
    not a function at the top level of held.py has no callee.
    """
    source = read_source(root / ENGINES / HELD_MODULE, root)
    if source is None:
        return {}
    functions = top_functions(source)
    table = python_reader.bindings(source)
    calls: dict[str, frozenset[str]] = {}
    for node in source.tree.body:
        targets: list[ast.expr] = []
        value: ast.expr | None = None
        if isinstance(node, ast.Assign):
            targets, value = node.targets, node.value
        elif isinstance(node, ast.AnnAssign):
            targets, value = [node.target], node.value
        is_table = any(isinstance(item, ast.Name) and item.id == HOLDERS_TABLE for item in targets)
        if not is_table or not isinstance(value, ast.Dict):
            continue
        for key, judge in zip(value.keys, value.values, strict=True):
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                function = functions.get(judge.id) if isinstance(judge, ast.Name) else None
                calls[key.value] = frozenset() if function is None else called_names(function, table)
    return calls


class Holders:
    """The facts that THE HOLDERS reads in one tree: the module of each rule, the judges, and the holder modules.

    The class parses held.py and each module that a field holder names one time.
    """

    __slots__ = ("calls", "context", "implemented", "modules")

    def __init__(self, context: MetaContext, implemented: dict[str, Path]) -> None:
        """Keep the tree and the module file of each rule with a function."""
        self.context = context
        self.implemented = implemented
        self.calls = judged_calls(context.root)
        self.modules: dict[Path, dict[str, FunctionNode] | None] = {}

    def functions_of(self, path: Path) -> dict[str, FunctionNode] | None:
        """Return the functions at the top level of a module, or None when the module does not parse."""
        if path not in self.modules:
            source = read_source(path, self.context.root)
            self.modules[path] = None if source is None else top_functions(source)
        return self.modules[path]

    def problem(self, rule: rule_registry.Rule) -> tuple[str, str] | None:
        """Return the kind and the message of the error of a held rule (THE HOLDERS), or None when it has none."""
        holder = rule.holder
        if holder is None:
            return NO_HOLDER, (
                f"the rule {rule.id} has the holder {rule.held_by} and no field holder.  Give the row the field "
                f"holder=MODULE{rule_registry.HOLDER_SEPARATOR}FUNCTION, with the function of {rule.held_by} that "
                f"judges the rule"
            )
        path = module_file(self.context.root, holder.module)
        if path is None:
            return MISSING_HOLDER, (
                f"the field holder {holder.text} of the rule {rule.id} names the module {holder.module}, and the "
                f"tree does not hold it.  Correct the field holder"
            )
        relative = path.relative_to(self.context.root).as_posix()
        functions = self.functions_of(path)
        if functions is None:
            return MISSING_HOLDER, (
                f"the field holder {holder.text} of the rule {rule.id} names {relative}, which does not parse.  "
                f"Correct the module"
            )
        if holder.function not in functions:
            return MISSING_HOLDER, (
                f"the field holder {holder.text} of the rule {rule.id} names the function {holder.function}, and "
                f"{relative} has no such function at its top level.  Correct the field holder"
            )
        held_by = rule.held_by or ""
        if held_by in self.context.registry.rules:
            if self.implemented.get(held_by) == path:
                return None
            return UNRUN_HOLDER, (
                f"the field holder {holder.text} of the rule {rule.id} names a function that the witness does not "
                f"run.  The witness runs the module of the rule {held_by}, and {relative} does not hold that rule.  "
                f"Name a function of the module of {held_by}"
            )
        if f"{holder.module}.{holder.function}" in self.calls.get(held_by, frozenset()):
            return None
        return UNRUN_HOLDER, (
            f"the field holder {holder.text} of the rule {rule.id} names a function that the witness does not run.  "
            f"The judge of {held_by} in the table {HOLDERS_TABLE} of {ENGINES}/{HELD_MODULE} does not call it.  "
            f"Call the function in that judge, or correct the field holder"
        )


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each rule of the registry with no function, and each function with no rule."""
    registry = context.registry
    shown = rule_registry.REGISTRY_PATH
    found: list[check_report.Finding] = []
    implemented: dict[str, Path] = {}
    engines_dir = context.root / ENGINES
    for path in sorted(engines_dir.glob("*/*.py")):
        if path.name == "__init__.py":
            continue
        engine = path.parent.name
        has_function, family = declared_rules(path)
        own = [path.stem.replace("_", "-")] if has_function else []
        relative = path.relative_to(context.root).as_posix()
        for rule_id in [*own, *family]:
            rule = registry.rules.get(rule_id)
            if rule is None:
                found.append(
                    check_report.Finding(
                        "error",
                        relative,
                        0,
                        RULE,
                        f"the module gives findings for {rule_id}, and the registry has no "
                        f"rule {rule_id}.  Add its row, or remove the function",
                        "orphan",
                        rule_id,
                    )
                )
            elif rule.engine.directory != engine:
                found.append(
                    check_report.Finding(
                        "error",
                        relative,
                        0,
                        RULE,
                        f"the module is in the directory {engine}, and the rule {rule_id} has "
                        f"the engine {rule.engine.value} with the directory {rule.engine.directory}.  Move the module",
                        "engine",
                        rule_id,
                    )
                )
            else:
                implemented[rule_id] = path
    holders = Holders(context, implemented)
    for rule in registry.rules.values():
        if rule.legacy is not None:
            module_path = context.root / "utils" / "assay" / Path(*rule.legacy.module.split("."))
            if not (module_path.with_suffix(".py").is_file() or (module_path / "__init__.py").is_file()):
                found.append(
                    check_report.Finding(
                        "error",
                        shown,
                        rule.line,
                        RULE,
                        f"the legacy module assay.{rule.legacy.module} of the rule "
                        f"{rule.id} does not exist.  Correct the field legacy",
                        "missing-legacy",
                        rule.id,
                    )
                )
            else:
                found.append(
                    check_report.Finding(
                        "error",
                        shown,
                        rule.line,
                        RULE,
                        f"the rule {rule.id} has only the legacy module "
                        f"assay.{rule.legacy.module}.  Write its module in {ENGINES}/{rule.engine.directory}/",
                        "legacy",
                        rule.id,
                    )
                )
        elif rule.held_by is not None:
            problem = holders.problem(rule)
            if problem is not None:
                found.append(check_report.Finding("error", shown, rule.line, RULE, problem[1], problem[0], rule.id))
        elif rule.id not in implemented:
            found.append(
                check_report.Finding(
                    "error",
                    shown,
                    rule.line,
                    RULE,
                    f"the rule {rule.id} has no function.  Write "
                    f"{ENGINES}/{rule.engine.directory}/{rule.id.replace('-', '_')}.py with the function findings, or "
                    f"name the rule in the table {FAMILY_TABLE} of a family module there",
                    "no-function",
                    rule.id,
                )
            )
    return found
