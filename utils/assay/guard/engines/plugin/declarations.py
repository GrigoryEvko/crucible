"""The family declaration property: the lines of one rule in its scope, one position for each entity, and the mints.

THE FAMILY
    The quarantine plugin gives the findings of the family declaration
    property (utils/tools/quarantine/declaration.cpp, THE FAMILY).  Each line
    of the family has a rule of the engine plugin of utils/registry.txt.  The
    module of the rule reads the lines of its rule in the census of the
    runner (assay.guard.engines.plugin.runner, THE SECTIONS).

THE SCOPE
    The plugin reads a quarantined header in each unit that includes it, and
    a test file or a source file holds declarations too.  lines_of() keeps
    the lines of a file in the scope of the row of the rule.  A path of the
    scope holds each file in it, and the scope word `tracked` holds each
    file.  The scope words `base` and `quarantined` hold each file of that
    class in the rule table.

THE POSITION OF AN ENTITY
    One entity can have lines at two positions.  An example is a function
    with a declaration in one header and the definition in a different
    header.  The unit of each header gives a line.  first_places() gives each
    kind and entity one line, at the first position.  The ledger row of the
    entity then names one path.

THE MINTS
    A record of the kind mint holds the facts of one mint (declaration.cpp,
    THE RULE mint).  The reader gives these facts
    (assay.readers.sections, THE ENTITIES OF THE RECORDS).  mints() gives one
    mint for each entity:
    the record with the body of the mint, and else the record at the first
    position.  A record of the kind mint-alias names an alias of a class
    with a member mint.  aliases() gives the names of the aliases of each
    class.  The rule mint and the inventory of the mints read the same
    records.

`guard selftest tests.guard.engines.plugin.test_declarations` does a test of the module.
"""

from collections.abc import Iterable, Mapping
from dataclasses import dataclass

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

MINT_RULE = "mint"
MINT_RECORD = "mint"
ALIAS_RECORD = "mint-alias"
# The kinds of the lines of the rule mint that are records and no findings.
RECORD_KINDS = frozenset({MINT_RECORD, ALIAS_RECORD})
SCOPE_TRACKED = "tracked"
# The scope words that a class of the rule table gives (ruletable.RuleTable.class_of).
CLASS_WORDS = frozenset({"base", "quarantined"})
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)


@dataclass(frozen=True, slots=True)
class Mint:
    """One mint of the build: the position of its selected record and the facts of that record (THE MINTS)."""

    path: str
    line: int
    record: plugin_sections.MintRecord


def rule_of(context: runner.PluginContext, rule_id: str) -> rule_registry.Rule:
    """Return the row of a rule in the registry of the tree of the context, or of this repository.

    Raises:
        rows_grammar.RowsError: If the registry does not obey its grammar
        KeyError: If the registry has no row of the rule
    """
    path = context.root / rule_registry.REGISTRY_PATH
    registry = rule_registry.load(path) if path.is_file() else rule_registry.load()
    return registry.rules[rule_id]


def table_of(context: runner.PluginContext) -> ruletable.RuleTable:
    """Return the rule table of the tree of the context, or the table of this repository when the tree has none.

    Raises:
        ruletable.TableError: If the table does not obey its grammar
    """
    path = context.root / TABLE
    return ruletable.load(path if path.is_file() else REPO_ROOT / TABLE)


def in_scope(rule: rule_registry.Rule, path: str, table: ruletable.RuleTable) -> bool:
    """Return True when the scope of the row of the rule holds a file (THE SCOPE)."""
    file_class = table.class_of(path)[0]
    for item in rule.scope:
        if item == SCOPE_TRACKED or (item in CLASS_WORDS and file_class == item):
            return True
        if item not in rule_registry.SCOPE_WORDS and ruletable.holds(item, path):
            return True
    return False


def lines_of(rule: rule_registry.Rule, context: runner.PluginContext) -> list[plugin_sections.Finding]:
    """Return the lines of the rule in its scope, sorted (THE SCOPE).

    Complexity: linear in the lines of the rule, times the items of its scope.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        ruletable.TableError: If the rule table does not obey its grammar
    """
    table = table_of(context)
    return [item for item in context.census().of_rule(rule.id) if in_scope(rule, item.file, table)]


def first_places(lines: Iterable[plugin_sections.Finding]) -> dict[tuple[str, str], plugin_sections.Finding]:
    """Return one line for each kind and entity: the line at the first position (THE POSITION OF AN ENTITY).

    Complexity: linear in the lines.
    """
    placed: dict[tuple[str, str], plugin_sections.Finding] = {}
    for item in lines:
        key = (item.kind, item.entity)
        if key not in placed or item < placed[key]:
            placed[key] = item
    return placed


def rule_findings(
    context: runner.PluginContext,
    rule_id: str,
    actions: Mapping[str, str],
    excluded: frozenset[str] = frozenset(),
) -> list[check_report.Finding]:
    """Return one error for each kind and entity of a rule of the family at its first position, and each problem.

    The message is the entity and the action of its kind.  A file of `excluded` gives no finding.

    Complexity: linear in the lines of the rule, times the items of its scope.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    lines = [item for item in lines_of(rule_of(context, rule_id), context) if item.file not in excluded]
    found = census.errors(rule_id)
    for item in sorted(first_places(lines).values()):
        action = actions.get(item.kind, "is a finding of the rule")
        found.append(
            check_report.Finding(
                "error", item.file, item.line, rule_id, f"{item.entity} {action}", item.kind, item.entity
            )
        )
    return found


def owner_aliases(records: Iterable[plugin_sections.Finding]) -> dict[str, frozenset[str]]:
    """Return the names of the aliases of each class, by the full name of the class (THE MINTS).

    Complexity: linear in the records.

    Raises:
        EntityError: If a record does not have the grammar of an alias record
    """
    names: dict[str, set[str]] = {}
    for item in records:
        alias = plugin_sections.mint_alias(item.entity, repr(item.text()))
        names.setdefault(alias.owner, set()).add(alias.name)
    return {owner: frozenset(found) for owner, found in names.items()}


def chosen_mints(records: Iterable[plugin_sections.Finding]) -> dict[str, Mint]:
    """Return one mint for each entity: the record with the body, and else the record at the first position.

    Complexity: O(r log r) for r records.

    Raises:
        EntityError: If a record does not have the grammar of a mint record
    """
    chosen: dict[str, Mint] = {}
    for item in sorted(records):
        record = plugin_sections.mint_record(item.entity, repr(item.text()))
        held = chosen.get(record.entity)
        if held is None or (record.is_defined and not held.record.is_defined):
            chosen[record.entity] = Mint(item.file, item.line, record)
    return chosen


def mints(context: runner.PluginContext, rule: rule_registry.Rule) -> list[Mint]:
    """Return the mints of the build in the scope of the rule, one for each entity, sorted by path and line.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        EntityError: If a record does not have the grammar of a mint record
        ruletable.TableError: If the rule table does not obey its grammar
    """
    records = [item for item in lines_of(rule, context) if item.kind == MINT_RECORD]
    return sorted(chosen_mints(records).values(), key=lambda mint: (mint.path, mint.line, mint.record.entity))


def aliases(context: runner.PluginContext, rule: rule_registry.Rule) -> dict[str, frozenset[str]]:
    """Return the names of the aliases of each class with a member mint in the scope of the rule (THE MINTS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        EntityError: If a record does not have the grammar of an alias record
        ruletable.TableError: If the rule table does not obey its grammar
    """
    return owner_aliases(item for item in lines_of(rule, context) if item.kind == ALIAS_RECORD)
