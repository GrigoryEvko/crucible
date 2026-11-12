"""specialization-home — each specialization of a template of the tree stands in the home of its template.

THE RULE
    C++ has no orphan rule, so each unit can specialize each template that it
    can name.  A specialization of a trait in a different file changes what
    each gate that reads the trait admits.  The quarantine plugin reads each
    class template and each variable template of the tree that no home row of
    the rule table governs (utils/tools/quarantine/specialization.cpp).  The
    home of a template is the file that defines it, and the check file of a
    header stands where the header stands.  The kinds of its lines:
      specialization    an explicit or partial specialization, or an explicit
                        specialization of a member of an instance, outside the
                        home of a template with no open mark
      foreign-primary   a template of the namespace foundation or fixy that a
                        file outside the base declares first.  A forward
                        declaration before the header moves the home of a
                        template that no unit defines
      open-extension    a record: a template with the annotation
                        foundation::open_extension, at its home
    A template with the open mark is open to each file.  GCC merges the mark
    of a second declaration into the template, so a file can add the mark to
    a template of a different file.  The rule then judges each record
    open-extension as a finding, and only a ledger row admits it: the ledger
    is the roster of the open templates.

THE LEDGER
    A row of utils/ledgers/specialization-home.txt admits the lines of one
    file, kind and template: `PATH | KIND | admit | TEMPLATE | REASON`.  A
    row of the kind open-extension names the home of a marked template.  A
    row that admits no finding gives no finding, and a write removes it.

Usage
    guard check --rule specialization-home --build-dir DIR [--warnings-dir DIR]
    guard write --rule specialization-home --build-dir DIR [--admit --reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "specialization-home"
ACTIONS = {
    "specialization": "is specialized here, outside the file that defines it.  A specialization changes what each "
    "gate that reads the template admits.  Move the specialization into the home of the template, or give the "
    "template the annotation foundation::open_extension with its reason and a ledger row",
    "foreign-primary": "is a template of the base that this file declares first, so the home of the template moves "
    "here.  Include the header of the template and remove the declaration, or move the template into the base",
    "open-extension": "carries the open mark, and no ledger row admits it.  Add the row with the reason of the mark, "
    "or remove the mark.  A second declaration in a different file can add the mark",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each line of the rule, a record open-extension too, and each problem of the census.

    Complexity: linear in the lines of the rule.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    found = context.census().errors(RULE)
    lines = declarations.lines_of(declarations.rule_of(context, RULE), context)
    seen: set[tuple[str, int, str, str]] = set()
    for item in sorted(lines):
        key = (item.file, item.line, item.kind, item.entity)
        if key in seen:
            continue
        seen.add(key)
        action = ACTIONS.get(item.kind, "is a finding of the rule")
        found.append(
            check_report.Finding(
                "error", item.file, item.line, RULE, f"the template {item.entity} {action}", item.kind, item.entity
            )
        )
    return found
