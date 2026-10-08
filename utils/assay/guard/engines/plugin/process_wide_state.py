"""process-wide-state — each `static` or `thread_local` object is one for each process, or a ledger row tells why not.

THE RULE
    The build compiles with -fvisibility=hidden.  Each shared library that
    compiles a header then holds its own copy of each inline variable and of
    each static of an inline function of that header.  crucible_native.py
    loads libcrucible_vessel.so and libcrucible_dispatch.so with dlopen and
    RTLD_LOCAL, and a write into the copy of one library is not seen in the
    other library.  CRUCIBLE_PROCESS_WIDE of foundation/Platform.h gives the
    object default visibility.  GCC then gives the object, and the guard of a
    static local, a unique global symbol.  The dynamic linker then uses the
    first copy that it loads for each library.  The marker goes on an inline
    variable, or on the inline function that holds the object as a static
    local.

    An object is a `static` or `thread_local` variable that can change, and a
    definition.  The quarantine plugin reads each object
    (utils/tools/quarantine/declaration.cpp, THE RULE process-wide-state):
      unmarked-static          an object with no marker
      marker-in-template       a marked object of a template.  An
                               instantiation takes the smallest visibility
                               of the template and its arguments, and each
                               type of this tree is hidden
      marker-internal-linkage  a marked object in an anonymous namespace, or
                               declared `static` at namespace scope
      marker-not-inline        a marked object, and its variable or function
                               is not inline, and it has no unique symbol
      marker-without-object    a marked declaration that holds no object
    test/test_process_wide_across_libraries.cpp shows the effect of the
    marker with two libraries that dlopen loads with RTLD_LOCAL.  The rule
    reads the lines in the scope of its row of utils/registry.txt.

THE LEDGER
    A row of utils/ledgers/process-wide-state.txt admits one object with no
    marker: `PATH | unmarked-static | admit | ENTITY | REASON`.  The entity is
    the full name of the variable, with its namespaces and classes, and
    FUNCTION()::NAME for a static local.  The reason tells why one copy in
    each library is correct.  A row
    that admits no finding fails the rule.

Usage
    guard check --rule process-wide-state --build-dir DIR [--warnings-dir DIR]
    guard write --rule process-wide-state --build-dir DIR

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "process-wide-state"
ACTIONS = {
    "unmarked-static": "has static storage duration and no CRUCIBLE_PROCESS_WIDE.  Mark the object, or add a "
    "ledger row with the reason why one copy in each library is correct",
    "marker-in-template": "carries CRUCIBLE_PROCESS_WIDE in a template, and the marker has no effect there.  Use a "
    "variable outside the template, with a key",
    "marker-internal-linkage": "carries CRUCIBLE_PROCESS_WIDE and has internal linkage, and the marker has no effect "
    "there.  Remove the anonymous namespace or the word static",
    "marker-not-inline": "carries CRUCIBLE_PROCESS_WIDE, and its variable or function is not inline, so its object "
    "has no unique symbol.  Make the holder inline",
    "marker-without-object": "carries CRUCIBLE_PROCESS_WIDE and holds no object with static storage duration that "
    "can change.  Remove the marker",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each object with no marker and each marker with no effect, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
