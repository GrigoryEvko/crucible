"""no-unchecked-access — access_context::unchecked() only in reviewed reflection walks.

THE RULE
    In GCC 16, a splice of a nonstatic data member has no access check.  With
    the reflection of a private member `m`, `obj.[:m:] = v` writes it.  Only a
    walk with `std::meta::access_context::unchecked()` gives the reflection of
    a private member.  That call is the one door to the private state of each
    proof type.  Examples are a count, a stamp, a grade, a Refined value and a
    Secret payload.  An owner row of utils/rule-table.txt names the function.
    The quarantine plugin gives these kinds (utils/tools/quarantine/owner.cpp,
    THE NAMES OF THE SOURCE and THE WALKED RETURN):
      * unchecked-use: a name or a member access of the function, through
        each qualifier, alias and object.  In a template, a member with the
        name `unchecked` of a dependent object is a use too
      * access-alias: a using-declaration, an alias declaration or a typedef
        of `access_context`
      * access-reflection: a reflection of `access_context`, of `std::meta` or
        of `std`.  A walk of their members gets `unchecked` with no name
      * member-splice: a splice of a nonstatic data member, of a nonstatic
        member function or of a base, and a member access through a splice.
        This is the write itself, and it stops each path to the reflection
      * open-static-member: a writable static data member with private or
        `protected` access, in a class of include/foundation, include/fixy or
        include/crucible.  A splice of a static data member has no object
      * walked-return: a function or a lambda that uses `unchecked` and
        returns a member of a class that a walk found.
    A use that a macro body spells counts in the macro body and at the
    expansion point.  The entity of a finding is `HOLDER: SHAPE` (THE PLACE
    AND THE ENTITY of owner.cpp).

THE LEDGER
    utils/ledgers/no-unchecked-access.txt admits each reviewed use (the policy
    admit), and each row gives the reason:
        PATH | KIND | admit | HOLDER: SHAPE | REASON
    No row admits a finding of the kinds open-static-member and walked-return
    (REFUSED_KINDS).  Make the member const or constexpr, or move it into the
    object.  Give the caller no reflection of a private member.  A row that
    admits no finding is stale and fails.

REVIEW RULE FOR THE ADMITTED FILES
    An admitted file does not publish a reflection of a nonstatic data member
    through a different path.  A public constexpr verdict with a field that
    names a private member opens the door again.  `obj.[:verdict.field:]`
    then writes the member with no `unchecked()`.  The reviewer of each row
    does this check.

Usage
    guard check --rule no-unchecked-access --build-dir DIR [--warnings-dir DIR]
    guard write --rule no-unchecked-access --build-dir DIR [--admit] [--reason TEXT]

Complexity: linear in the finding lines of the build, and in the rows of the ledger.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "no-unchecked-access"
# The kinds that no ledger row admits (assay.guard.engines.meta.runner, THE REFUSED KINDS).
REFUSED_KINDS = frozenset({"open-static-member", "walked-return"})
ACTIONS = {
    "unchecked-use": "a use of access_context::unchecked(), the door to the private state of each proof type",
    "access-alias": "a second name of access_context, which hides a use of unchecked() behind it",
    "access-reflection": "a reflection of access_context or of a namespace that holds it, which reaches "
    "unchecked() with no name",
    "member-splice": "a splice that names a member of an object, which writes a private member with no access check",
    "open-static-member": "a writable private or protected static data member, which a splice writes with no "
    "object.  Make the member const or constexpr, or move it into the object",
    "walked-return": "a function that uses unchecked() and gives its caller a member that a walk found, which the "
    "caller can splice with no door of its own",
}


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    shape = ACTIONS.get(item.kind, "a finding of the rule")
    advice = (
        ""
        if item.kind in REFUSED_KINDS
        else ".  Use a reviewed walk with access_context::current(), or give the use a ledger row with the reason"
    )
    return f"{shape} ({item.entity}){advice}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule in its scope, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message)
