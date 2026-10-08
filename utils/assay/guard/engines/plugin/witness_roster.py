"""witness-roster — each class of the base with a door shape has a witness row of the rule table.

THE RULE
    A witness class attests to a fact that it cannot see, such as a pin, a
    mapping, a descriptor, a permission or a context.  Its constructor must
    take the proof of the fact, or be the operation that makes the proof.  A
    witness row of utils/rule-table.txt names each such class.  The command
    `loop derive witness-fixtures` writes one negative fixture for each row.
    The quarantine plugin gives one finding for each class of a base file
    with a door shape and no witness row (THE RULE witness-roster of
    utils/tools/quarantine/declaration.cpp):
      unrostered-witness   a class with a door shape and no witness row
    The door shapes are a mint_ friend, a passkey class and a constructor
    that takes a passkey first.  A static factory that returns
    `expected<CLASS, ...>` is a door shape too.  A named friend function and
    a friend type are door shapes only with a closed constructor.  The rule
    reads the lines in the scope of its row of utils/registry.txt.

THE LEDGER
    A row of utils/ledgers/witness-roster.txt admits one class:
    `PATH | unrostered-witness | admit | ENTITY | REASON`.  The entity is the
    full name of the class, with its namespaces and no template arguments.
    A row that admits no finding fails the rule.

Usage
    guard check --rule witness-roster --build-dir DIR [--warnings-dir DIR]
    guard write --rule witness-roster --build-dir DIR

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "witness-roster"
ACTIONS = {
    "unrostered-witness": "has a door shape and no witness row of utils/rule-table.txt.  Add the row with the "
    "expression of a forger, and write the fixtures with `python3 utils/bin/loop derive witness-fixtures --write`",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each class with a door shape and no witness row, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
