"""proof-routes — no compiled shape builds a proof type with no call to its door.

THE RULE
    A proof type is a context, a key, a capability, a permission, a pin or a
    placement.  Only the door that its header names builds one.  The language
    refuses most other shapes.  The quarantine plugin of
    utils/tools/quarantine/ reads the shapes that the language permits, on the
    semantic tree of each unit (THE PROOF ROUTES in expression.cpp):
      union                           a union definition
      pointer-cast                    a cast to a pointer or a reference to a
                                      proof type
      allocator                       std::allocator, allocator_traits or
                                      polymorphic_allocator of a proof type
      function-specialization         an explicit specialization of a function
                                      template
      member-pointer-specialization   an explicit specialization with a pointer
                                      to a member in its template arguments
      friend-template-specialization  a specialization of a class template that
                                      a class of include/ befriends, in a file
                                      that does not hold the definition of the
                                      template
    A witness row of utils/rule-table.txt names a proof type.  A class of
    foundation or fixy with the shape of a proof is a proof type too.  The
    entity of a finding is `KIND: KEY`.

    The rule parse-proof-routes reads two shapes that the compiler keeps no
    tree of: an explicit instantiation, and an allocation with `sizeof` of a
    proof type in a function that is not a template.

    The rule reads the finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).

THE LEDGER
    utils/ledgers/proof-routes.txt admits a reviewed site with a count and a
    reason (assay.core.ledger).  The reason tells why the site builds no proof
    type.  Each problem of the census of the runner is an error too.

Usage
    guard check --rule proof-routes --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "proof-routes"
# The danger of each kind, for the message of its finding.
DANGER = {
    "union": "a union can hold each object type, and a member that is not active holds no object",
    "pointer-cast": "the cast makes a pointer to a proof type from storage with no object",
    "allocator": "allocate gives a typed pointer with no object",
    "function-specialization": "a specialization of a door is a friend of each class that befriends the door",
    "member-pointer-specialization": "an explicit specialization does no access check of its template arguments",
    "friend-template-specialization": "a specialization of a befriended template is a friend of the proof type",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each shape that can build a proof type, and each problem of the census.

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
                f"a shape that can build a proof type with no call to its door ({item.entity}): "
                f"{DANGER.get(item.kind, 'the shape skips the door')}.  Build the proof only through its door, "
                f"or give the site a reviewed row with its reason in the ledger",
                item.kind,
                item.entity,
            )
        )
    return found
