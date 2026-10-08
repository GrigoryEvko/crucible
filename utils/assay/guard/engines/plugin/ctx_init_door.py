"""ctx-init-door — only a reviewed entry point names the owner of the init door or of the background door.

THE RULE
    include/foundation/effects/Effect.h gives the init context one production
    door, `host::InitOwner::mint_init_context()`, and the background context
    one production door, `host::BackgroundOwner::mint_background_context()`.
    An init context permits the effects of process startup, and a background
    context permits the effects of a background thread.  Each use of an owner
    must be in a reviewed process entry point or in the reviewed entry
    function of a background thread.

    Two owner rows of utils/rule-table.txt name the two owners.  The
    quarantine plugin gives the kind `door-use` to these uses
    (utils/tools/quarantine/owner.cpp, THE NAMES OF THE SOURCE):
      * A name of an owner or of an alias of it
      * A using-declaration of an owner
      * A member access of a member of an owner
      * A friend of a class that names an owner.
    A comment and a literal are no use.  The entity of a finding is
    `HOLDER: OWNER` (THE PLACE AND THE ENTITY of owner.cpp).  The holder is
    the function that holds the use, with the types of its parameters.  For a
    use that a macro body spells, the holder is "macro NAME".  At namespace
    scope, the holder is "namespace-scope".

THE SCOPE
    The rule reads the files of the trees of production code:
    include/foundation, include/fixy, include/crucible, src, vessel,
    utils/tools and examples.  test/ and bench/ use the test door, and the
    rule does not read them.

THE LEDGER
    utils/ledgers/ctx-init-door.txt counts the reviewed uses of each file and
    entity (the policy count), and each row gives a reason:
        PATH | door-use | COUNT | HOLDER: OWNER | REASON
    A new use is more than its count and fails, and a row above the count of
    its uses fails until `guard write` writes it again.  A row with no reason
    is an error.

Usage
    guard check --rule ctx-init-door --build-dir DIR [--warnings-dir DIR]
    guard write --rule ctx-init-door --build-dir DIR [--reason TEXT]

Complexity: linear in the finding lines of the build, times the items of the scope.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "ctx-init-door"


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return (
        f"a use of the owner of a production door ({item.entity}).  The owner gives a context with the effects of "
        f"process startup or of a background thread.  Name it only in a reviewed process entry point or in the "
        f"reviewed entry function of a background thread, and give that use a ledger row with its reason"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each use of an owner in the scope of the rule, each row with no reason, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message)
