"""rule-implementations — each rule of the registry has one function, and each function has one rule.

THE RULE
    A rule with no legacy module and no holder has the module
    assay/guard/engines/ENGINE/ID.py, with underscores in place of the hyphens,
    and the module defines the function findings.  A family module holds a
    table RULES whose keys are the IDs of the rules that it gives findings
    for.  The rule refuses:
      no-function     a rule with no such module and no family table that names it
      orphan          a module that defines findings or RULES for an ID that is not a rule of the registry
      engine          a module in the directory of an engine that is not the engine of its rule
      legacy          a rule that has only a legacy module.  During the migration of the utils system, the ledger
                      admits each one, and the change that replaces the legacy module removes its row
      missing-legacy  a legacy module that does not exist
      held            a measured rule with no function: its holder reads its thresholds.  The ledger admits each one
    The rule reads each module with the Python ast module, and it imports
    none.  A module with no function findings and no table RULES is a helper,
    such as the runner of an engine.

Complexity: linear in the size of the modules of assay/guard/engines/.
"""

import ast
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext

RULE = "rule-implementations"
ENGINES = "utils/assay/guard/engines"


def declared_rules(path: Path) -> tuple[bool, list[str]]:
    """Return whether a module defines the function findings, and the string keys of its table RULES."""
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), str(path))
    except SyntaxError:
        return False, []
    has_function = any(isinstance(node, ast.FunctionDef) and node.name == "findings" for node in tree.body)
    keys: list[str] = []
    for node in tree.body:
        targets = node.targets if isinstance(node, ast.Assign) else [node.target] if isinstance(node, ast.AnnAssign) else []
        value = node.value if isinstance(node, (ast.Assign, ast.AnnAssign)) else None
        if any(isinstance(target, ast.Name) and target.id == "RULES" for target in targets) and isinstance(value, ast.Dict):
            keys += [key.value for key in value.keys if isinstance(key, ast.Constant) and isinstance(key.value, str)]
    return has_function, keys


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each rule of the registry with no function, and each function with no rule."""
    registry = context.registry
    shown = rule_registry.REGISTRY_PATH
    found: list[check_report.Finding] = []
    implemented: dict[str, str] = {}
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
                found.append(check_report.Finding(
                    "error", relative, 0, RULE, f"the module gives findings for {rule_id}, and the registry has no "
                    f"rule {rule_id}.  Add its row, or remove the function", "orphan", rule_id))
            elif rule.engine.value != engine:
                found.append(check_report.Finding(
                    "error", relative, 0, RULE, f"the module is in the directory of the engine {engine}, and the "
                    f"rule {rule_id} has the engine {rule.engine.value}.  Move the module", "engine", rule_id))
            else:
                implemented[rule_id] = relative
    for rule in registry.rules.values():
        if rule.legacy is not None:
            module_path = context.root / "utils" / "assay" / Path(*rule.legacy.module.split("."))
            if not (module_path.with_suffix(".py").is_file() or (module_path / "__init__.py").is_file()):
                found.append(check_report.Finding(
                    "error", shown, rule.line, RULE, f"the legacy module assay.{rule.legacy.module} of the rule "
                    f"{rule.id} does not exist.  Correct the field legacy", "missing-legacy", rule.id))
            else:
                found.append(check_report.Finding(
                    "error", shown, rule.line, RULE, f"the rule {rule.id} has only the legacy module "
                    f"assay.{rule.legacy.module}.  Write its module in {ENGINES}/{rule.engine.value}/", "legacy",
                    rule.id))
        elif rule.held_by is not None:
            found.append(check_report.Finding(
                "error", shown, rule.line, RULE, f"the measured rule {rule.id} has no function: {rule.held_by} reads "
                f"its thresholds.  Give it a function in {ENGINES}/{rule.engine.value}/", "held", rule.id))
        elif rule.id not in implemented:
            found.append(check_report.Finding(
                "error", shown, rule.line, RULE, f"the rule {rule.id} has no function.  Write "
                f"{ENGINES}/{rule.engine.value}/{rule.id.replace('-', '_')}.py with the function findings",
                "no-function", rule.id))
    return found
