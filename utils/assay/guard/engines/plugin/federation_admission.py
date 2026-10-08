"""federation-admission — FederationAdmission and each part of it stand only in fixy/Federation.h.

THE RULE
    foundation/permissions/Permission.h declares the class template
    FederationAdmission and the key of a federation peer token.  The one
    friend of the key is that template.  fixy/Federation.h holds the
    definition of the admission, and that definition builds the key only
    after a correct handshake.  C++ lets each unit give the template a second
    definition that the key also befriends.  These are the forms of such a
    definition:
      * An explicit specialization, and a specialization with template
        parameters
      * A definition of the primary template in a unit that does not include
        fixy/Federation.h
      * An explicit specialization of one member for a new argument, also
        through an alias or a decltype.

    The quarantine plugin gives a finding of the kind `outside-home` for each
    definition or specialization of the admission that is not in a path of
    its home row in utils/rule-table.txt (utils/tools/quarantine/home.cpp).
    It also gives one for each such definition of a member or a class in the
    admission.  The plugin finds the entity by name lookup.  An alias, a
    decltype or a macro of a different file then hides no site.

    The rule has no ledger, and each finding is an error.  Each path of the
    home row that admits no site is an error too
    (assay.guard.engines.plugin.home, THE PATHS OF A HOME ROW).

Usage
    guard check --rule federation-admission --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import home, runner

RULE = "federation-admission"
message = home.message_of(
    "a definition or a specialization of {entity}, or a definition of a part of it, stands here, and its home row "
    "admits only {paths}.  The key of a federation peer token befriends the admission, so a second definition "
    "builds the key with no handshake.  Remove it"
)


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give the errors of the rule (assay.guard.engines.plugin.home, THE FINDINGS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return home.rule_findings(context, RULE, message)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the rule judged against its ledger (assay.guard.engines.plugin.home.judge)."""
    return home.judge(rule, context)


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context and write its findings (assay.guard.engines.plugin.home.run)."""
    return home.run(rule, context, warnings_dir)
