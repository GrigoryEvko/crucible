"""start-lifetime — std::start_lifetime_as and std::start_lifetime_as_array only in the checked start.

THE RULE
    A proof type, such as a mint key, a context, a capability or a
    permission, is not trivially copyable, and it is not an implicit-lifetime
    type.  So std::bit_cast and std::start_lifetime_as<P> refuse it.  Three
    calls of the same library family can give a pointer to a proof object
    with no lifetime:
      * std::start_lifetime_as<P[1]>(buffer), because an array type is an
        implicit-lifetime type for each element
      * std::start_lifetime_as_array<P>(buffer, 1), because libstdc++ puts no
        limit on the element type
      * std::start_lifetime_as<Holder>(buffer), with an aggregate that holds
        a P, or std::array<P, 1>.
    A read through that pointer is undefined behavior, and no property of a
    type refuses the three calls.  The checked start of
    include/foundation/Lifetime.h refuses each one at compile time.  Two
    owner rows of utils/rule-table.txt give that file as the one site of the
    two names.  The quarantine plugin gives each other use the kind
    `lifetime-start` (utils/tools/quarantine/owner.cpp, THE OWNER ROWS).  A
    use is a call, an address, or a candidate of a dependent call, in each
    file that a build compiles.  The entity of a finding is HOLDER: NAME (THE
    PLACE AND THE ENTITY of owner.cpp).

    test/fixy/test_forgeable_proofs.cpp gives the other ways to a proof that
    stay open.  Examples are a void pointer, an allocator and a member of a
    union that is not active.  The rule proof-routes reads them.

THE LEDGER
    The rule has no ledger, and each finding is an error.  A negative fixture
    that names the two names must fail to compile.  No build compiles it, and
    it is not necessary to admit it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule start-lifetime --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

RULE = "start-lifetime"


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return (
        f"the code starts the lifetime of an object in a file other than foundation/Lifetime.h ({item.entity}).  "
        f"An array of proofs, an aggregate that holds a proof and an array start can each give a proof with no "
        f"lifetime.  Call foundation::lifetime::start_as_array"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each use of the two names in a file other than the checked start, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return context.census().line_errors(RULE, message)
