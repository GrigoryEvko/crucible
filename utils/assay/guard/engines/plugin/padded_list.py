"""padded-list — the element type of a long list has no padding bit.

THE RULE
    With -ftrivial-auto-var-init=zero, GCC writes zero to each padding hole
    of an automatic object that has an initializer, and of each object that a
    return statement makes.  It writes one store for each hole of each
    element of a list, with no loop.  A list of 256 padded ledger entries,
    returned through std::expected, gave a function of 934 KB of machine
    code.  The compile of each file that included it used 75 s more.  Make
    each padding byte a member, as include/crucible/ledger/Verdict.h does with
    `pad`.

    A list is an array of a class or a union.  Such an array in a
    specialization of a class template or in a class with no name is a list
    too.  The count of a list of lists is the product of the counts.  The quarantine
    plugin reads the type of each alias, variable and data member.  It also
    reads the return type and each parameter that a function takes by value.
    It reads no template, and it reads the files of include/ and src/
    (utils/tools/quarantine/declaration.cpp, THE RULE padded-list).  The
    plugin argument padded-list-limit is the error threshold of the row of
    this rule in utils/registry.txt (utils/tools/quarantine/Quarantine.cmake).
    The plugin reads each element type that has a padding bit.  It gives one
    line for the longest list in a declaration, when the list has more
    elements than that threshold:
      padded-alias      the list is in the type of an alias
      padded-member     the list is in the type of a variable or a data member
      padded-return     the list is in the return type of a function
      padded-parameter  the list is in the type of a parameter
    A list of fewer or the same number of elements gives no line, and in the
    mode error no error.  The entity of a line is `DECLARATION: ELEMENT |
    count=N holes=H`, with the element count and the padding bits of one
    element.  The rule keeps each line in the scope of its row.  The entity
    of its finding is `DECLARATION: ELEMENT`, and a change of the count
    changes no key.  A list has one finding, at its first position
    (assay.guard.engines.plugin.declarations, THE POSITION OF AN ENTITY).  A
    line that the rule cannot read is an error.  A line of a list at or below
    the threshold is an error of the kind threshold.  Such a line comes from a
    build with a different threshold, and no ledger row admits it
    (REFUSED_KINDS).  Each problem of the census of the runner is an error
    too.

THE LEDGER
    utils/ledgers/padded-list.txt counts the padded lists of each file, kind
    and entity (assay.core.ledger, the policy count).  A row with no reason
    is a padded list that the tree holds at this time.  A row with a reason
    keeps an element type with padding on purpose, such as a record that
    fills a cache line.  Each run gives a warning for each file with rows.  A
    new padded list is an error.  A row that names no padded list is an error
    too, until `guard write --rule padded-list --build-dir DIR` writes the
    ledger again in the same commit.

Usage
    guard check --rule padded-list --build-dir DIR [--warnings-dir DIR]
    guard write --rule padded-list --build-dir DIR [--admit --reason TEXT]

Complexity: linear in the lines of the rule.
"""

from dataclasses import dataclass, replace

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner
from assay.readers import sections as plugin_sections

RULE = "padded-list"
# The text between the declaration and the element type, and between the entity of a line and its facts.
ELEMENT_SEPARATOR = ": "
FACTS_SEPARATOR = " | "
FACTS = ("count", "holes")
# The kind of a line of a list at or below the threshold.  No ledger row admits it (assay.guard.engines.meta.runner,
# THE REFUSED KINDS), because the object comes from a build with a different threshold.
THRESHOLD_KIND = "threshold"
REFUSED_KINDS = frozenset({THRESHOLD_KIND})


class LineError(ValueError):
    """A line of the rule does not have the format `DECLARATION: ELEMENT | count=N holes=H`."""


@dataclass(frozen=True, slots=True)
class PaddedList:
    """One padded list, as a line of the plugin gives it.

    `entity` is `DECLARATION: ELEMENT`, the key of the list.  `count` is the element count of the list, and `holes`
    is the number of the padding bits of one element.
    """

    entity: str
    count: int
    holes: int


def parse_line(item: plugin_sections.Finding) -> PaddedList:
    """Return the padded list of one line of the rule.

    Raises:
        LineError: If the entity of the line does not have the format of THE RULE
    """
    entity, separator, facts = item.entity.rpartition(FACTS_SEPARATOR)
    declaration, element_separator, element = entity.partition(ELEMENT_SEPARATOR)
    values: dict[str, int] = {}
    for word in facts.split(" "):
        key, equal, value = word.partition("=")
        if equal and key in FACTS and key not in values and value.isdigit():
            values[key] = int(value)
    if not separator or not element_separator or not declaration or not element or set(values) != set(FACTS):
        raise LineError(f"{item.text()!r}: a line of {RULE} is 'DECLARATION: ELEMENT | count=N holes=H'")
    return PaddedList(entity, values["count"], values["holes"])


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each padded list of more elements than the threshold, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    rule = declarations.rule_of(context, RULE)
    limit = rule.threshold.error if rule.threshold is not None else 0.0
    found = context.census().errors(RULE)
    kept: list[plugin_sections.Finding] = []
    lists: dict[tuple[str, str], PaddedList] = {}
    for item in declarations.lines_of(rule, context):
        try:
            padded = parse_line(item)
        except LineError as problem:
            found.append(check_report.Finding("error", item.file, item.line, RULE, str(problem)))
            continue
        if padded.count <= limit:
            found.append(
                check_report.Finding(
                    "error",
                    item.file,
                    item.line,
                    RULE,
                    f"the plugin gave a line of a list of {padded.count} elements, and the threshold of the rule is "
                    f"{limit:g}.  The object comes from a build with a different threshold.  Configure and build the "
                    f"tree again",
                    THRESHOLD_KIND,
                    padded.entity,
                )
            )
            continue
        kept.append(replace(item, entity=padded.entity))
        lists[(item.kind, padded.entity)] = padded
    for item in sorted(declarations.first_places(kept).values()):
        padded = lists[(item.kind, item.entity)]
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"the {item.kind.removeprefix('padded-')} {padded.entity} holds a list of {padded.count} elements, "
                f"and each element has {padded.holes} padding bits.  With -ftrivial-auto-var-init=zero, each "
                f"automatic object of it gets one store for each hole of each element, with no loop.  Make each "
                f"padding byte a member, as include/crucible/ledger/Verdict.h does with pad.  An element type with "
                f"padding on purpose gets a ledger row with its reason",
                item.kind,
                item.entity,
            )
        )
    return found
