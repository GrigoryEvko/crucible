"""The test of the rules that read the names of the source, on planted lines of the engine plugin.

THE CASES
    A case gives planted finding lines to a context of the runner with a
    planted census.  A scratch tree holds a planted registry, a rule table and
    a ledger.  The cases hold:
      * One error for each line in the scope of the rule, and no error for a
        line out of the scope
      * One error for each row of the ledger with no reason
      * The errors of a row check, together with the errors of the reasons
      * A ledger that does not read gives no row error.
"""

import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "ctx-init-door"
REGISTRY_ROWS = (
    (
        f"rule {RULE} engine=plugin scope=src/ ledger=count kinds=door-use doc=L0 | a planted rule of the names | "
        f"name the door only in a reviewed entry point"
    ),
)
TABLE_TEXT = "layer base 0 include/fixy/\nquarantine src/\nquarantine test/\n"
ENTITY = "planted::start(): foundation::effects::host::InitOwner"


def line(file: str, number: int) -> plugin_sections.Finding:
    """Return a planted finding line of the rule at a position."""
    return plugin_sections.Finding(file, number, 1, RULE, "door-use", ENTITY)


def message(item: plugin_sections.Finding) -> str:
    """Return the planted message of one line."""
    return f"a planted use ({item.entity})"


def scratch_tree(root: Path, ledger_text: str) -> None:
    """Write the planted registry, the rule table and the ledger into a scratch tree."""
    (root / "utils" / "ledgers").mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted(REGISTRY_ROWS), encoding="utf-8")
    (root / declarations.TABLE).write_text(TABLE_TEXT, encoding="utf-8")
    (root / "utils" / "ledgers" / f"{RULE}.txt").write_text(ledger_text, encoding="utf-8")


def errors_of(
    root: Path,
    lines: list[plugin_sections.Finding],
    row_check: Callable[[rule_ledger.Ledger], list[check_report.Finding]] | None = None,
) -> list[check_report.Finding]:
    """Return the errors of scoped_errors for planted lines in the scratch tree."""
    context = runner.PluginContext(root, census=runner.Census(1, frozenset(lines)))
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message, row_check)


def scope_cases(expect: testing.Expect) -> None:
    """Hold one error for each line in the scope of the rule, and no error for a line out of the scope."""
    with tempfile.TemporaryDirectory(prefix="plugin-names-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root, f"format 1\nrule {RULE}\n")
        found = errors_of(root, [line("src/entry.cpp", 6), line("src/entry.cpp", 9), line("test/door.cpp", 7)])
        expect(
            "each line of src/ is one error, and the line of test/ is none",
            [(item.path, item.line, item.kind) for item in found]
            == [("src/entry.cpp", 6, "door-use"), ("src/entry.cpp", 9, "door-use")],
        )
        expect("an error keeps the entity of its line", all(item.entity == ENTITY for item in found))


def reason_cases(expect: testing.Expect) -> None:
    """Hold one error for each row with no reason, and the errors of a row check together with them."""
    with tempfile.TemporaryDirectory(prefix="plugin-names-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(
            root,
            f"format 1\nrule {RULE}\n"
            f"src/entry.cpp | door-use | 1 | {ENTITY} | -\n"
            f"src/other.cpp | door-use | 1 | {ENTITY} | a planted reason\n",
        )
        found = errors_of(root, [])
        expect(
            "the row with no reason is one error at its line, and the row with a reason is none",
            [(item.path, item.line) for item in found] == [(rule_ledger.shown_path(RULE), 3)],
        )

        def refuse_other(rows: rule_ledger.Ledger) -> list[check_report.Finding]:
            """Refuse each row of src/other.cpp."""
            return [
                check_report.Finding("error", "planted", entry.line, RULE, "a planted refusal")
                for key, entry in rows.base.items()
                if key.path == "src/other.cpp"
            ]

        found = errors_of(root, [], refuse_other)
        expect(
            "the row check gives its errors beside the error of the reason",
            sorted((item.path, item.line) for item in found) == [("planted", 4), (rule_ledger.shown_path(RULE), 3)],
        )


def unread_cases(expect: testing.Expect) -> None:
    """Hold that a ledger that does not read gives no row error.  The judgement of the ledger reports it."""
    with tempfile.TemporaryDirectory(prefix="plugin-names-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root, f"format 1\nrule {RULE}\nsrc/entry.cpp | door-use\n")
        found = errors_of(root, [line("src/entry.cpp", 6)])
        expect(
            "the line is one error, and the row that does not read gives none",
            [(item.path, item.line) for item in found] == [("src/entry.cpp", 6)],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (scope_cases, reason_cases, unread_cases))
