"""borrow-match — no arm of a borrow match writes the sum that the match reads.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ gives a finding of the
    kind borrow-match for each assignment, move or mutating call of the
    receiver of a borrow match in an arm that holds a reference into the
    payload, in each build and each mode (borrow.cpp).  A borrow match carries
    the annotation foundation::linear::borrow_match of
    include/foundation/Linear.h, such as the match of a const Option, Result or
    OneOf.  The rule reads those finding lines in the sections of the objects
    of the target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).  The same line from two objects counts one time.  The entity of
    a finding is the kind of the write and the receiver.

    The rule has no ledger, and each finding is an error.  A write in an
    opt-out region is a finding of the kind opted-out of the rule quarantine,
    and the ledger of that rule counts it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule borrow-match --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "borrow-match"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each write of an arm of a borrow match to its sum in the build, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"an arm of a borrow match writes the sum that the match reads ({item.entity}).  Match the rvalue, "
                f"std::move(sum).match(...), or change the sum after the match: the write ends or changes the "
                f"payload while the arm holds a reference into it (CLAUDE.md section XXII)",
                item.kind,
                item.entity,
            )
        )
    return found
