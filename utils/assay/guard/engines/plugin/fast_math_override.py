"""fast-math-override — no function turns on a fast-math option that the command line of its unit does not turn on.

THE RULE
    CLAUDE.md section V bans the fast-math family, because each option breaks
    one of the numerical invariants of replay.  It also bans each option that
    cancels a flag of the floor of cmake/FpStrict.cmake, because GCC uses the
    last of two contrary options.  An optimize attribute and a GCC optimize
    pragma give a function options of its own.  The floor of the command line
    does not hold these options.

    The quarantine plugin reads each function of a file of the source root
    (unit.cpp, THE FAST-MATH OPTIONS).  It compares the options of the
    function with the options of the command line of its unit.  The plugin
    gives a finding of the kind fast-math-override to each function that
    turns on such an option with its own options.  The finding holds the
    name of the function and the options.  The finding is a compile error in
    each build and each mode.

    A header can open a GCC optimize pragma and not close it.  Then the
    pragma gives its options to each function after the include, and each
    such function gets a finding.  A pragma that no function follows changes
    no code and is no finding.  The rule reads the finding lines in the
    sections of the objects of the target all of one build
    (assay.guard.engines.plugin.runner, THE SECTIONS).

    The rule no-ffast-math reads the flags of the compile database, of the
    link lines and of the presets.

The rule has no ledger, and each finding is an error.  A function in an
opt-out region gives an opted-out finding of the rule quarantine.

Usage
    guard check --rule fast-math-override --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

RULE = "fast-math-override"
ACTION = (
    "Keep the FP floor of cmake/FpStrict.cmake: remove the option from the optimize attribute or the GCC optimize "
    "pragma (CLAUDE.md section V)"
)


def message(item: plugin_sections.Finding) -> str:
    """Return the message of the error of one finding line."""
    return f"{item.entity} turns on a fast-math option.  {ACTION}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return context.census().line_errors(RULE, message)
