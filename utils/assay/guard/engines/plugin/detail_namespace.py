"""detail-namespace — only the base names a `detail` namespace of foundation or fixy.

THE RULE
    A function in a namespace with the name `detail` does the work of a door
    with no gate of its own.  The gate is the public door that calls it.  A
    file that names the function directly does not go through the gate.  Two
    owner rows of utils/rule-table.txt name the top namespaces foundation and
    fixy.  Each `detail` namespace in one of them is a `detail` namespace of
    the base.  The sites of each row are the base and the check files of its
    headers: include/foundation, include/fixy, src/foundation, src/fixy,
    test/layer/checks/foundation and test/layer/checks/fixy.

    The quarantine plugin gives the kind `detail-use` to each of these uses in
    a file that is not a site (utils/tools/quarantine/owner.cpp, THE NAMES OF
    THE SOURCE):
      * A name of a `detail` namespace or of a namespace alias of it.  This
        includes a qualifier, a using-directive, a using-declaration, a
        namespace alias, a template argument, a base class and an
        unevaluated operand
      * A name in the body of a `detail` namespace that a file opens again,
        and a declaration at namespace scope in such a body.
    A type alias of a type of a `detail` namespace is a public name of the
    base.  Its declaration named the namespace where it stands, and a use of
    the alias is no use.  A call that argument-dependent lookup finds names no
    namespace, and it is no use.  The base keeps each such function away from
    that lookup, for example as a hidden friend or a function object.  The
    entity of a finding is `HOLDER: NAMESPACE` (THE PLACE AND THE ENTITY of
    owner.cpp).  NAMESPACE is the outermost `detail` namespace, such as
    `foundation::effects::detail`.

THE LEDGER
    utils/ledgers/detail-namespace.txt counts the reviewed uses in test files
    (the policy count), and each row gives a reason:
        PATH | detail-use | COUNT | HOLDER: NAMESPACE | REASON
    Only a file in test/ can take a row, and a file in test/fuzz/ cannot.  A
    fuzz harness operates a production boundary, and it calls the public door
    as production code does.  A test file can also be a file that the build
    writes in test/ of the build directory, such as the roster of a test.  A
    row of a different file is an error.  A new use is more than its count
    and fails.  A row above the count of its uses fails until `guard write`
    writes it again.

Usage
    guard check --rule detail-namespace --build-dir DIR [--warnings-dir DIR]
    guard write --rule detail-namespace --build-dir DIR [--reason TEXT]

Complexity: linear in the finding lines of the build, and in the rows of the ledger.
"""

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "detail-namespace"
# The files of TEST_TREE can take a row, and the files of FUZZ_TREE, in TEST_TREE, cannot.
TEST_TREE = "test/"
FUZZ_TREE = "test/fuzz/"
# The prefix of the path of a file that the build writes (utils/tools/quarantine/plugin.h, THE FILES).
BUILD_PREFIX = "@build/"


def is_test_path(path: str) -> bool:
    """Return True when a file can take a row: a file in test/ and not in test/fuzz/, also in the build directory."""
    tested = path.removeprefix(BUILD_PREFIX)
    return tested.startswith(TEST_TREE) and not tested.startswith(FUZZ_TREE)


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return (
        f"a use of a detail namespace of the base outside the base ({item.entity}).  A detail function has no "
        f"gate of its own.  Call the public door of the base.  A test file can name a detail namespace with a "
        f"ledger row that gives the reason"
    )


def path_errors(rows: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of a file that is not in test/, or that is in test/fuzz/.

    Complexity: linear in the rows.
    """
    shown = rule_ledger.shown_path(RULE)
    return [
        check_report.Finding(
            "error",
            shown,
            entry.line,
            RULE,
            f"the row of {key.path} admits a use of a detail namespace, and only a file under {TEST_TREE} outside "
            f"{FUZZ_TREE} can take a row.  Call the public door of the base, and remove the row",
        )
        for key, entry in sorted(rows.base.items())
        if not is_test_path(key.path)
    ]


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each use of a `detail` namespace out of its sites, each bad row, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message, path_errors)
