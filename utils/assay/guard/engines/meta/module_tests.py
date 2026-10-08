"""module-tests — each module of the package with logic has its test module.

THE RULE
    A module with logic is a module of the package with a function or a
    method.  Its test module is in utils/assay/tests/, with the path of the
    module and the name test_NAME.py:
      utils/assay/core/rows.py                  utils/assay/tests/core/test_rows.py
      utils/assay/loop/fixture/__init__.py      utils/assay/tests/loop/test_fixture.py
    The rule refuses:
      no-test  a module with logic and no test module
    It is not necessary for these modules to have a test module:
      * A module of utils/assay/tests/, which is a test
      * The module of a rule in utils/assay/guard/engines/ENGINE/, with the
        function findings or the table RULES.  The plants of the rule are its
        tests, and the rule rule-witness holds them.
    The entity of a finding is the dotted name of the module.

Complexity: linear in the length of the files of the scope.
"""

import ast
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta import rule_implementations
from assay.guard.engines.meta.runner import MetaContext

RULE = "module-tests"
TESTS = "utils/assay/tests/"
ENGINES = "utils/assay/guard/engines/"


def has_logic(tree: ast.Module) -> bool:
    """Return True when a module has a function or a method."""
    return any(isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) for node in ast.walk(tree))


def is_rule_module(context: MetaContext, item: package_scope.PythonFile) -> bool:
    """Return True when a file is the module of a rule: in a directory of an engine, with findings or RULES."""
    if not item.path.startswith(ENGINES):
        return False
    has_function, family = rule_implementations.declared_rules(context.root / item.path)
    return has_function or bool(family)


def test_path(path: str) -> str:
    """Return the path of the test module of a module of the package (THE RULE)."""
    pure = PurePosixPath(path.removeprefix(package_scope.PACKAGE_PREFIX))
    if pure.name == package_scope.INIT:
        return f"{TESTS}{pure.parent.parent.as_posix()}/test_{pure.parent.name}.py".replace("/./", "/")
    return f"{TESTS}{pure.parent.as_posix()}/test_{pure.name}".replace("/./", "/")


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each module of the package with logic and with no test module."""
    scope = package_scope.read_package(context)
    found = package_scope.parse_findings(RULE, scope)
    present = frozenset(context.tracked())
    for item in scope.files:
        if (
            item.is_master
            or item.path.startswith(TESTS)
            or not has_logic(item.parsed.tree)
            or is_rule_module(context, item)
        ):
            continue
        expected = test_path(item.path)
        if expected not in present:
            found.append(
                check_report.Finding(
                    "error",
                    item.path,
                    0,
                    RULE,
                    f"the module has logic and no test module.  Write {expected}, which `guard selftest` starts",
                    "no-test",
                    item.name,
                )
            )
    return found
