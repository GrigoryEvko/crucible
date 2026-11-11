"""The test of the rule owners, on planted lines of the engine plugin.

THE CASES
    A case gives planted finding lines to a context of the runner with a
    planted census.  A scratch tree holds a planted registry, a rule table and
    a ledger.  The cases hold:
      * One error for each counted line.  A line of the kinds door-use and
        testing-use counts only in the trees of production code, and a line
        of each other kind counts in each file
      * One error for each row of the ledger with no reason
      * One error for each row of a refused kind, and for each row of the kind
        detail-use outside test/ or in test/fuzz/
      * A ledger that does not read gives no row error.
"""

import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.plugin import declarations, owners, runner
from assay.readers import sections as plugin_sections

RULE = owners.RULE
REGISTRY_ROWS = (
    (
        f"rule {RULE} engine=plugin scope=tracked ledger=count kinds=door-use,testing-use,detail-use,lifetime-start "
        f"doc=L0 | a planted rule of the owners | use a name only at a site of its owner row"
    ),
)
TABLE_TEXT = "layer base 0 include/fixy/\nquarantine src/\nquarantine test/\n"
DOOR_ENTITY = "planted::start(): foundation::effects::host::InitOwner"
DETAIL_ENTITY = "namespace-scope: fixy::detail"


def line(file: str, number: int, kind: str = "door-use", entity: str = DOOR_ENTITY) -> plugin_sections.Finding:
    """Return a planted finding line of the rule at a position."""
    return plugin_sections.Finding(file, number, 1, RULE, kind, entity)


def scratch_tree(root: Path, ledger_text: str) -> None:
    """Write the planted registry, the rule table and the ledger into a scratch tree."""
    (root / "utils" / "ledgers").mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted(REGISTRY_ROWS), encoding="utf-8")
    (root / declarations.TABLE).write_text(TABLE_TEXT, encoding="utf-8")
    (root / "utils" / "ledgers" / f"{RULE}.txt").write_text(ledger_text, encoding="utf-8")


def errors_of(root: Path, lines: list[plugin_sections.Finding]) -> list[check_report.Finding]:
    """Return the errors of the rule for planted lines in the scratch tree."""
    context = runner.PluginContext(root, census=runner.Census(1, frozenset(lines)))
    return owners.findings(context)


def scope_cases(expect: testing.Expect) -> None:
    """Hold that a door kind counts only in production code, and that a kind of each other rule counts in each file."""
    with tempfile.TemporaryDirectory(prefix="plugin-owners-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root, f"format 1\nrule {RULE}\n")
        found = errors_of(
            root,
            [
                line("src/entry.cpp", 6),
                line("src/entry.cpp", 9, "testing-use", "namespace-scope: foundation::effects::testing"),
                line("test/door.cpp", 7),
                line("test/door.cpp", 8, "testing-use", "namespace-scope: foundation::effects::testing"),
                line("test/detail.cpp", 3, "detail-use", DETAIL_ENTITY),
            ],
        )
        expect(
            "each line of src/ is one error, the door lines of test/ are none, and the detail line of test/ is one",
            [(item.path, item.line, item.kind) for item in found]
            == [
                ("src/entry.cpp", 6, "door-use"),
                ("src/entry.cpp", 9, "testing-use"),
                ("test/detail.cpp", 3, "detail-use"),
            ],
        )
        expect("an error keeps the entity of its line", found[0].entity == DOOR_ENTITY)


def reason_cases(expect: testing.Expect) -> None:
    """Hold one error for each row with no reason, and none for a row with a reason."""
    with tempfile.TemporaryDirectory(prefix="plugin-owners-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(
            root,
            f"format 1\nrule {RULE}\n"
            f"src/entry.cpp | door-use | 1 | {DOOR_ENTITY} | -\n"
            f"src/other.cpp | door-use | 1 | {DOOR_ENTITY} | a planted reason\n",
        )
        found = errors_of(root, [])
        expect(
            "the row with no reason is one error at its line, and the row with a reason is none",
            [(item.path, item.line) for item in found] == [(rule_ledger.shown_path(RULE), 3)],
        )


def row_cases(expect: testing.Expect) -> None:
    """Hold one error for a row of a refused kind, and for a row of detail-use outside test/ or in test/fuzz/."""
    with tempfile.TemporaryDirectory(prefix="plugin-owners-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(
            root,
            f"format 1\nrule {RULE}\n"
            f"@build/test/roster.h | detail-use | 1 | {DETAIL_ENTITY} | a generated roster\n"
            f"src/start.cpp | lifetime-start | 1 | namespace-scope: std::start_lifetime_as | a planted reason\n"
            f"src/use.cpp | detail-use | 1 | {DETAIL_ENTITY} | a planted reason\n"
            f"test/fuzz/harness.cpp | detail-use | 1 | {DETAIL_ENTITY} | a planted reason\n"
            f"test/unit.cpp | detail-use | 1 | {DETAIL_ENTITY} | a planted reason\n",
        )
        found = errors_of(root, [])
        expect(
            "the rows of lifetime-start, of src/ and of test/fuzz/ are errors, and the rows of test/ are none",
            sorted(item.line for item in found) == [4, 5, 6],
        )
        expect(
            "the error of the refused kind names the kind",
            any("lifetime-start" in item.message and item.line == 4 for item in found),
        )


def unread_cases(expect: testing.Expect) -> None:
    """Hold that a ledger that does not read gives no row error.  The judgement of the ledger reports it."""
    with tempfile.TemporaryDirectory(prefix="plugin-owners-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root, f"format 1\nrule {RULE}\nsrc/entry.cpp | door-use\n")
        found = errors_of(root, [line("src/entry.cpp", 6)])
        expect(
            "the line is one error, and the row that does not read gives none",
            [(item.path, item.line) for item in found] == [("src/entry.cpp", 6)],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (scope_cases, reason_cases, row_cases, unread_cases))
