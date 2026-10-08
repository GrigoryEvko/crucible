"""ctx-testing-boundary — the testing door stays out of the code that ships.

THE RULE
    The testing door, `foundation::effects::testing`, gives a context with no
    key.  `bg()`, `init()`, `test()` and `foreground()` each mint a context.
    `TestWitness` and `ForegroundWitness` are the friends that can build the
    keys.  Each ctx-bound mint checks for a context.  A use of the door in code
    that ships gives an authority that no mint gave.

    An owner row of utils/rule-table.txt names the namespace of the door.  The
    quarantine plugin gives the kind `testing-use` to these uses
    (utils/tools/quarantine/owner.cpp, THE NAMES OF THE SOURCE):
      * A name of the namespace or of an alias of it, also in a
        using-directive, a using-declaration and a namespace alias
      * A member of the namespace that the source spells with no qualifier
      * A member access of a member of a class of the namespace
      * A friend of a class that names such a class.
    A comment and a literal are no use.  The entity of a finding is
    `HOLDER: foundation::effects::testing` (THE PLACE AND THE ENTITY of
    owner.cpp).

THE SCOPE
    The rule reads the files of the trees of production code:
    include/foundation, include/fixy, include/crucible, src, vessel,
    utils/tools and examples.  test/ and bench/ use the test door, and the
    rule does not read them.  A check file of test/layer/checks/ compiles only
    into libraries that no target links, and it is test code too.

THE LEDGER
    utils/ledgers/ctx-testing-boundary.txt counts the reviewed uses of each
    file and entity (the policy count), and each row gives a reason:
        PATH | testing-use | COUNT | HOLDER: foundation::effects::testing | REASON
    A new use is more than its count and fails, and a row above the count of
    its uses fails until `guard write` writes it again.  A row with no reason
    is an error.

Usage
    guard check --rule ctx-testing-boundary --build-dir DIR [--warnings-dir DIR]
    guard write --rule ctx-testing-boundary --build-dir DIR [--reason TEXT]

Complexity: linear in the finding lines of the build, times the items of the scope.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "ctx-testing-boundary"


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return (
        f"a use of the testing door in code that ships ({item.entity}).  The door mints a context with no key.  "
        f"Code that ships takes its context from a real mint.  A self-test in a header can use the door: give its "
        f"use a ledger row with the reason"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each use of the testing door in the scope of the rule, each bad row, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message)
