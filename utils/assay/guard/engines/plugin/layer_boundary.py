"""layer-boundary — a base file names only its own project root and the roots below it.

THE RULE
    The tree has three project roots in three layers, and each layer can name
    only the roots at or below it:

        include/foundation/  src/foundation/  names  foundation
        include/fixy/        src/fixy/        names  foundation, fixy
        include/crucible/    the other files  name   each project root

    The layer rows of the rule table utils/rule-table.txt give the sequence.
    The rank of a root is the lowest rank of a layer row of the root.  A root
    of a quarantine row is above each layer.  The quarantine plugin gives a
    finding of the kind `upward-name` to these items (include.cpp, THE NAMES
    OF A HIGHER ROOT):
      * the first declaration of the namespace of a higher root in a base file
      * each reference to a declaration of a higher root that a base file
        spells.
    The include rules of the plugin give a finding of the kind
    `upward-include` for an include of a higher layer, and the rule
    quarantine counts it.  The rule reads the finding lines of the kind
    `upward-name` in the sections of the objects of the target all of one build
    (assay.guard.engines.plugin.runner, THE SECTIONS).

    The plugin reads only the arms of a preprocessor conditional that a build
    compiles.  The rule layer-residual of the engine tsast reads these items:
      * each #include directive of each arm of a base file
      * each include with a macro as its path in a base file
      * each file of a base directory that is not C++.

The rule has no ledger, and each finding is an error.  A file that must name
a higher layer is in the incorrect layer.

Usage
    guard check --rule layer-boundary --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

RULE = "layer-boundary"
ACTION = (
    "A base file names only its own project root and the roots below it, in the sequence foundation, fixy, "
    "crucible.  Move the code to its layer, or move the declaration that it names down (CLAUDE.md section XXII)"
)


def message(item: plugin_sections.Finding) -> str:
    """Return the message of the error of one finding line."""
    return f"{item.kind} {item.entity}.  {ACTION}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return context.census().line_errors(RULE, message)
