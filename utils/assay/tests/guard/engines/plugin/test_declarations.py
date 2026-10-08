"""The test of the family declaration property of the engine plugin and of its rule mint-pattern, on planted lines.

THE CASES
    A case gives planted finding lines to a context of the runner with a
    planted census.  A scratch tree holds a planted registry and rule table.
    The cases hold:
      * The scope of a rule: a path, the scope word `tracked` and the class words
      * One position for each kind and entity
      * The facts of a mint record, and each record that does not read
      * The aliases of each class, from the records of the kind mint-alias
      * The record with the body of a mint, before the first position
      * The findings of the rule mint-pattern at the position of the chosen record
      * A row of the ledger that admits a finding.
"""

import contextlib
import io
import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import ruletable, testing
from assay.guard.engines.plugin import declarations, mint_pattern, runner
from assay.readers import sections as plugin_sections

RULE = mint_pattern.RULE
REGISTRY_ROWS = (
    (
        "rule mint-pattern engine=plugin scope=include/ ledger=admit kinds=mint,mint-not-constexpr doc=XXI | a "
        "planted mint rule | give the mint its shape"
    ),
    "rule process-wide-state engine=plugin scope=tracked ledger=admit doc=XV | a planted rule of each file | mark it",
    "rule no-raw-array-member engine=plugin scope=base ledger=admit doc=II | a planted rule of the base | use it",
)
TABLE_TEXT = "layer base 0 include/fixy/\nquarantine include/crucible/\nquarantine test/\n"
FACTS = (
    "name=mint_cell static=0 shape=ctx template=1 nodiscard=1 constexpr=0 noexcept=1 constrained=1 gated=1 projection=0"
)
ENTITY = "fixy::mint_cell(const Ctx&)"


def record(
    file: str, line: int, defined: int, owner: str = "-", facts: str = FACTS, class_name: str = "-"
) -> plugin_sections.Finding:
    """Return a planted mint record at a position, with the fact `defined`, the class and the owner."""
    return plugin_sections.Finding(
        file,
        line,
        1,
        RULE,
        declarations.MINT_RECORD,
        f"{ENTITY} | {facts} defined={defined} class={class_name} owner={owner}",
    )


def alias_record(alias: str, owner: str, entity: str | None = None) -> plugin_sections.Finding:
    """Return a planted record of the kind mint-alias of an alias of a class."""
    text = entity if entity is not None else f"fixy::{alias} | alias={alias} owner={owner}"
    return plugin_sections.Finding("include/fixy/A.h", 9, 1, RULE, declarations.ALIAS_RECORD, text)


def finding(file: str, line: int, kind: str = "mint-not-constexpr") -> plugin_sections.Finding:
    """Return a planted finding of the rule mint-pattern at a position."""
    return plugin_sections.Finding(file, line, 1, RULE, kind, ENTITY)


def scratch_tree(root: Path) -> None:
    """Write the planted registry and rule table into a scratch tree."""
    (root / "utils").mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted(REGISTRY_ROWS), encoding="utf-8")
    (root / declarations.TABLE).write_text(TABLE_TEXT, encoding="utf-8")


def context_of(root: Path, lines: list[plugin_sections.Finding]) -> runner.PluginContext:
    """Return a context of the scratch tree with a planted census of the lines."""
    return runner.PluginContext(root, census=runner.Census(1, frozenset(lines)))


def scope_cases(expect: testing.Expect) -> None:
    """Hold the scope of a rule: a path of the scope, the scope word `tracked` and the class words of the table."""
    registry, problems = rule_registry.examine(rule_registry.planted(REGISTRY_ROWS))
    table = ruletable.parse(TABLE_TEXT, "rules.txt")
    expect("the planted registry reads", not problems)
    mint_rule = registry.rules[RULE]
    expect("a path of the scope holds a file under it", declarations.in_scope(mint_rule, "include/fixy/A.h", table))
    expect(
        "a path of the scope does not hold a file outside it",
        not declarations.in_scope(mint_rule, "test/test_a.cpp", table),
    )
    expect(
        "the word tracked holds each file",
        declarations.in_scope(registry.rules["process-wide-state"], "test/test_a.cpp", table),
    )
    base_rule = registry.rules["no-raw-array-member"]
    expect("the word base holds a file of a layer row", declarations.in_scope(base_rule, "include/fixy/A.h", table))
    expect(
        "the word base does not hold a quarantined file",
        not declarations.in_scope(base_rule, "include/crucible/B.h", table),
    )


def place_cases(expect: testing.Expect) -> None:
    """Hold one position for each kind and entity: the line at the first position."""
    later = finding("include/fixy/B.h", 9)
    first = finding("include/fixy/A.h", 30)
    other = finding("include/fixy/B.h", 9, "mint-not-noexcept")
    placed = declarations.first_places([later, first, other])
    expect("each kind and entity has one line", len(placed) == 2)
    expect("the line at the first place wins", placed[("mint-not-constexpr", ENTITY)] == first)


def record_cases(expect: testing.Expect) -> None:
    """Hold the facts of a mint record, the owner with a space, and each record that does not read."""
    mint = declarations.parse_mint(record("include/fixy/A.h", 4, 1, "fixy::Cell<int, char>", class_name="Cell"))
    expect(
        "a record gives the entity, the name, the shape and the class",
        (mint.entity, mint.name, mint.shape, mint.class_name) == (ENTITY, "mint_cell", "ctx", "Cell"),
    )
    expect(
        "a record gives each flag",
        mint.is_template
        and mint.is_nodiscard
        and not mint.is_constexpr
        and mint.is_noexcept
        and mint.is_constrained
        and mint.is_gated
        and not mint.is_projection
        and mint.is_defined
        and not mint.is_static,
    )
    expect("the owner is the last fact, also with a space in it", mint.owner == "fixy::Cell<int, char>")
    broken = [
        plugin_sections.Finding("include/fixy/A.h", 4, 1, RULE, declarations.MINT_RECORD, f"{ENTITY} {FACTS}"),
        record("include/fixy/A.h", 4, 2),
        record("include/fixy/A.h", 4, 1, facts=FACTS.replace("shape=ctx", "shape=free")),
        record("include/fixy/A.h", 4, 1, facts=FACTS.replace(" gated=1", "")),
        record("include/fixy/A.h", 4, 1, facts=f"{FACTS} name=again"),
    ]
    refused = 0
    for item in broken:
        try:
            declarations.parse_mint(item)
        except declarations.RecordError:
            refused += 1
    expect(
        "a record with no facts, a flag of 2, an unknown shape, a missing flag and a second name do not read",
        refused == len(broken),
    )


def alias_cases(expect: testing.Expect) -> None:
    """Hold the aliases of each class, from the records of the kind mint-alias, and each record that does not read."""
    found = declarations.owner_aliases(
        [
            alias_record("Epoch", "fixy::Counter"),
            alias_record("Generation", "fixy::Counter"),
            alias_record("Epoch", "fixy::Counter"),
            alias_record("Wide", "fixy::Cell<int, char>"),
        ]
    )
    expect(
        "each class gets the names of its aliases, also an owner with a space",
        found == {"fixy::Counter": frozenset({"Epoch", "Generation"}), "fixy::Cell<int, char>": frozenset({"Wide"})},
    )
    broken = [
        alias_record("Epoch", "-", "fixy::Epoch alias=Epoch owner=fixy::Counter"),
        alias_record("Epoch", "-", "fixy::Epoch | name=Epoch owner=fixy::Counter"),
        alias_record("Epoch", "-", "fixy::Epoch | alias=Epoch"),
        alias_record("Epoch", "-", "fixy::Epoch | alias=Two Words owner=fixy::Counter"),
    ]
    refused = 0
    for item in broken:
        try:
            declarations.owner_aliases([item])
        except declarations.RecordError:
            refused += 1
    expect(
        "a record with no facts, no fact alias, no owner or a name with a space does not read",
        refused == len(broken),
    )


def chosen_cases(expect: testing.Expect) -> None:
    """Hold the record of a mint with the body before the record at the first position."""
    declared = record("include/fixy/Borrowed.h", 121, 0)
    defined = record("include/fixy/OwnedRegion.h", 452, 1)
    chosen = declarations.chosen_mints([declared, defined])
    expect("the record with the body wins", chosen[ENTITY].path == "include/fixy/OwnedRegion.h")
    chosen = declarations.chosen_mints([declared, record("include/fixy/Cell.h", 7, 0)])
    expect("with no body, the record at the first place wins", chosen[ENTITY].path == "include/fixy/Borrowed.h")


def rule_cases(expect: testing.Expect) -> None:
    """Hold the findings of the rule mint-pattern, their scope and position, and a ledger row that admits one."""
    with tempfile.TemporaryDirectory(prefix="plugin-declarations-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root)
        lines = [
            record("include/fixy/Borrowed.h", 121, 0),
            record("include/fixy/OwnedRegion.h", 452, 1),
            alias_record("Epoch", "fixy::Counter"),
            finding("include/fixy/Borrowed.h", 121),
            finding("include/fixy/OwnedRegion.h", 452),
            finding("test/test_a.cpp", 3),
        ]
        context = context_of(root, lines)
        found = mint_pattern.findings(context)
        expect(
            "a mint record and an alias record are no finding, and a line outside the scope is none",
            [(item.path, item.line, item.kind) for item in found]
            == [("include/fixy/OwnedRegion.h", 452, "mint-not-constexpr")],
        )
        expect(
            "the finding names its entity and the action",
            bool(found) and found[0].entity == ENTITY and "is not constexpr" in found[0].message,
        )
        rule = declarations.rule_of(context, RULE)
        key = rule_ledger.Key("include/fixy/OwnedRegion.h", "mint-not-constexpr", ENTITY)
        ledger = rule_ledger.Ledger(
            RULE,
            rule.policy,
            "# planted\n",
            base={key: rule_ledger.Entry(key, rule_ledger.ADMIT_VALUE, "a planted reason")},
        )
        rule_ledger.store(ledger, root)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            judged = runner.judged(rule, context)
        expect("a ledger row admits the finding", not [item for item in judged if item.level == "error"])
        broken = context_of(root, [record("include/fixy/A.h", 4, 2)])
        refused = mint_pattern.findings(broken)
        expect("a record that does not read is one error", len(refused) == 1 and "mint record" in refused[0].message)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (scope_cases, place_cases, record_cases, alias_cases, chosen_cases, rule_cases),
    )
