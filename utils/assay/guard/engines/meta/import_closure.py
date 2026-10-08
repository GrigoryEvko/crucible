"""import-closure — a module imports a heavy module only in the function that uses it.

THE RULE
    CMake starts `loop step` for each compile and each link, and ctest starts
    `loop test-step` for each test.  The imports of a start are then a cost
    of each step.  An import of argparse costs 6.3 ms, and an import of
    concurrent.futures costs 11.7 ms.  The rule reads each module of the
    package and each master (assay.guard.engines.meta.package), and it
    refuses:
      module-import  an import at module level of argparse,
                     concurrent.futures, multiprocessing or logging, also in an
                     if or a try at module level.  An import in the block
                     `if TYPE_CHECKING:` is not an import at run time.  The
                     owner that the job row argparse of the registry names can
                     import argparse
    The entity of a finding is the name of the heavy module.

THE CLOSURE
    The test module assay.tests.guard.test_import_closure holds the other part
    of the rule.  It imports the module of each hidden subcommand of the
    masters in a new interpreter.  Then it compares the modules that the
    import adds with the list in the test.

Complexity: linear in the length of the files of the scope.
"""

import ast
from collections.abc import Iterator

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext

RULE = "import-closure"
HEAVY = frozenset({"argparse", "concurrent.futures", "multiprocessing", "logging"})
TYPE_CHECKING = "TYPE_CHECKING"


def is_type_checking(test: ast.expr) -> bool:
    """Return True when the condition of an if is TYPE_CHECKING or typing.TYPE_CHECKING."""
    return (isinstance(test, ast.Name) and test.id == TYPE_CHECKING) or (
        isinstance(test, ast.Attribute) and test.attr == TYPE_CHECKING
    )


def module_level_imports(body: list[ast.stmt]) -> Iterator[ast.Import | ast.ImportFrom]:
    """Give each import of a body at module level, also in an if or a try, but not in the block of TYPE_CHECKING."""
    for node in body:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            yield node
        elif isinstance(node, ast.If):
            if not is_type_checking(node.test):
                yield from module_level_imports(node.body)
            yield from module_level_imports(node.orelse)
        elif isinstance(node, ast.Try):
            for block in (node.body, node.orelse, node.finalbody, *(handler.body for handler in node.handlers)):
                yield from module_level_imports(block)


def heavy_names(node: ast.Import | ast.ImportFrom) -> list[str]:
    """Return each heavy module that one import statement imports."""
    if isinstance(node, ast.Import):
        names = [alias.name for alias in node.names]
    elif node.level == 0 and node.module is not None:
        names = [node.module, *(f"{node.module}.{alias.name}" for alias in node.names)]
    else:
        names = []
    return sorted({heavy for heavy in HEAVY for name in names if name == heavy or name.startswith(f"{heavy}.")})


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each import of a heavy module at module level that its owner does not do."""
    owners = {job.entity: job.modules for job in context.registry.jobs}
    scope = package_scope.read_package(context)
    found = package_scope.parse_findings(RULE, scope)
    for item in scope.files:
        for node in module_level_imports(item.parsed.tree.body):
            for heavy in heavy_names(node):
                if item.name in owners.get(heavy, ()):
                    continue
                found.append(
                    check_report.Finding(
                        "error",
                        item.path,
                        node.lineno,
                        RULE,
                        f"the module imports {heavy} at module level, and each "
                        f"start of the module pays for it.  Import it in the function that uses it",
                        "module-import",
                        heavy,
                    )
                )
    return found
