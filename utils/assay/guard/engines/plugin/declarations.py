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
    THE RULE mint-pattern).  mints() gives one mint for each entity: the
    record with the body of the mint, and else the record at the first
    position.  The rule mint-pattern and the inventory of the mints read the
    same records.

`guard selftest tests.guard.engines.plugin.test_declarations` does a test of the module.
"""

from collections.abc import Iterable
from dataclasses import dataclass

from assay.core import registry as rule_registry
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

MINT_RULE = "mint-pattern"
MINT_RECORD = "mint"
# The separator between the entity of a mint and its facts, in a record.
FACTS_SEPARATOR = " | "
# The fact that a record gives last, because the name of a class can hold a space.
OWNER_FACT = " owner="
# The facts of a record with the value 0 or 1, in the sequence of the record.
FLAG_FACTS = (
    "static",
    "template",
    "nodiscard",
    "constexpr",
    "noexcept",
    "constrained",
    "gated",
    "projection",
    "defined",
)
SHAPES = frozenset({"ctx", "token", "member"})
# The owner of a mint that is no member.
NO_OWNER = "-"
SCOPE_TRACKED = "tracked"
# The scope words that a class of the rule table gives (ruletable.RuleTable.class_of).
CLASS_WORDS = frozenset({"base", "quarantined"})
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)


class RecordError(ValueError):
    """A record line of the plugin does not have the format of its kind."""


@dataclass(frozen=True, slots=True)
class Mint:
    """One mint, as its record gives it (declaration.cpp, THE RULE mint-pattern).

    `entity` is the full name of the mint, with its namespaces and the types of its parameters.  `owner` is the class
    of a member, or "-".  Each flag is a fact of the record.
    """

    entity: str
    path: str
    line: int
    name: str
    shape: str
    owner: str
    is_static: bool
    is_template: bool
    is_nodiscard: bool
    is_constexpr: bool
    is_noexcept: bool
    is_constrained: bool
    is_gated: bool
    is_projection: bool
    is_defined: bool


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


def parse_mint(item: plugin_sections.Finding) -> Mint:
    """Return the mint of one record line.

    Raises:
        RecordError: If the entity of the line is not `ENTITY | FACTS` with each fact of a mint record
    """
    entity, separator, facts = item.entity.rpartition(FACTS_SEPARATOR)
    head, marker, owner = facts.partition(OWNER_FACT)
    if not separator or not entity or not marker or not owner:
        raise RecordError(f"{item.text()!r}: a mint record is 'ENTITY | FACTS', and its last fact is the owner")
    values: dict[str, str] = {}
    for word in head.split(" "):
        key, equal, value = word.partition("=")
        if not equal or key in values:
            raise RecordError(f"{item.text()!r}: the fact {word!r} is not KEY=VALUE, or its key is there two times")
        values[key] = value
    flags = {key: values.get(key) for key in FLAG_FACTS}
    if set(values) != {"name", "shape", *FLAG_FACTS} or any(value not in ("0", "1") for value in flags.values()):
        raise RecordError(
            f"{item.text()!r}: a mint record gives name, shape and the flags {', '.join(FLAG_FACTS)}, each flag 0 or 1"
        )
    if values["shape"] not in SHAPES:
        raise RecordError(f"{item.text()!r}: the shape {values['shape']!r} is not one of {', '.join(sorted(SHAPES))}")
    return Mint(
        entity, item.file, item.line, values["name"], values["shape"], owner, *(flags[key] == "1" for key in FLAG_FACTS)
    )


def chosen_mints(records: Iterable[plugin_sections.Finding]) -> dict[str, Mint]:
    """Return one mint for each entity: the record with the body, and else the record at the first position.

    Complexity: O(r log r) for r records.

    Raises:
        RecordError: If a record does not have the format of a mint record
    """
    chosen: dict[str, tuple[bool, plugin_sections.Finding, Mint]] = {}
    for item in sorted(records):
        mint = parse_mint(item)
        held = chosen.get(mint.entity)
        if held is None or (mint.is_defined and not held[0]):
            chosen[mint.entity] = (mint.is_defined, item, mint)
    return {entity: mint for entity, (_, _, mint) in chosen.items()}


def mints(context: runner.PluginContext, rule: rule_registry.Rule) -> list[Mint]:
    """Return the mints of the build in the scope of the rule, one for each entity, sorted by path and line.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        RecordError: If a record does not have the format of a mint record
        ruletable.TableError: If the rule table does not obey its grammar
    """
    records = [item for item in lines_of(rule, context) if item.kind == MINT_RECORD]
    return sorted(chosen_mints(records).values(), key=lambda mint: (mint.path, mint.line, mint.entity))
