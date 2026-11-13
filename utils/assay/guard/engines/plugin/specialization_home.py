"""specialization-home — each specialization of a template of the tree stands in the home of its template.

THE RULE
    C++ has no orphan rule, so each unit can specialize each template that it
    can name.  A specialization of a trait in a different file changes what
    each gate that reads the trait admits, and an explicit specialization of a
    function template gives the function a different body, which is a friend
    of each class that befriends the template.  The quarantine plugin reads
    each class template and variable template of the tree that no home row of
    the rule table governs, and each function template of the tree
    (utils/tools/quarantine/specialization.cpp).  The
    home of a template is the file that defines it, and the check file of a
    header stands where the header stands.  The kinds of its lines:
      specialization    an explicit or partial specialization, an explicit
                        specialization of a function template, or an explicit
                        specialization of a member of an instance, outside the
                        home of a template with no open mark
      orphan            such a site of a template with the open mark, in a
                        file that does not define each top-level template
                        argument.  The mark opens a template to each file for
                        its own types only, as the orphan rule of Rust does
                        (THE COHERENCE of specialization.cpp)
      forward-declaration
                        a variable template that a unit declares with extern
                        before its definition.  GCC keeps the place of that
                        declaration as the home, also after the definition
      open-extension    a record: a template with the annotation
                        foundation::open_extension, at its home
    A class template or a variable template with the open mark is open to
    each file for the types that the file defines.  The mark does not open a
    function template.  GCC merges the mark
    of a second declaration into the template, so a file can add the mark to
    a template of a different file.  The rule then judges each record
    open-extension as a finding, and only a ledger row admits it: the ledger
    is the roster of the open templates.  A template of the namespace
    foundation or fixy that a file outside the base declares first is a
    finding of the rule base-namespace.

    The plugin makes each line of the first three kinds a compile error in
    each build and each mode (THE ERRORS of specialization.cpp), so no object
    holds such a line.  A test of a forged specialization puts it in an
    opt-out region of the reason class PROBE:, and the rule quarantine counts
    the opted-out finding.  This rule still refuses each such line that a
    report holds.

THE LEDGER
    A row of utils/ledgers/specialization-home.txt admits the records of one
    file and template: `PATH | open-extension | admit | TEMPLATE | REASON`.
    The row names the home of a marked template.  A row of a different kind
    can admit no line, because the compile of such a line fails.  A row that
    admits no finding gives no finding, and a write removes it.

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
    "orphan": "carries the open mark, and this file specializes it for a type that a different file defines.  The "
    "specialization gets the authority that the template gives that type.  Specialize the template in the file that "
    "defines each of its arguments, or in its home",
    "forward-declaration": "is declared here with extern before its definition, so its home moves here and a "
    "specialization beside the declaration is at home.  Include the header that defines the template, and remove the "
    "declaration",
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
