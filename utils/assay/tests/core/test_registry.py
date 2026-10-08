"""The test of the registry (assay.core.registry): the rule rows, the job rows, the path rows and the doc fields.

`guard selftest tests.core.test_registry` runs the test.  Each case prints
one line.  Exit 0 when each case holds, else 1.
"""

import sys
from collections.abc import Callable

from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar

GOOD_RULE = "rule alpha-rule engine=meta scope=tracked ledger=admit doc=XV | a planted refusal | fix it"
GUIDE = ("# A planted guide\n\n## XV. Headers\n\n1. **A rule.** Text.\n2. **Another.** Text.\n\n### The ratchet (R11)\n\n"
         "The text of R11.\n\n## L0 — Safety\n\nThe layer.\n")


def examined(*rows: str) -> tuple[rule_registry.Registry, list[rows_grammar.RowsError]]:
    """Examine a planted registry with the rows."""
    return rule_registry.examine(rule_registry.planted(rows), "planted.txt")


def problem_of(row: str) -> str:
    """Return the text of the one problem that a planted registry with one row has, or "" when it has none."""
    _, problems = examined(row)
    return str(problems[0]) if len(problems) == 1 else ""


def rule_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the rule rows: the fields, the engine, the policy, the thresholds and the holder."""
    registry, problems = examined(GOOD_RULE, rule_registry.measured_rule("slow-row", 10, 20, "s"))
    rule = registry.rules.get("alpha-rule")
    expect("a good registry has no problem, and a rule row gives its fields",
           not problems and rule is not None and rule.engine == rule_registry.Engine.META
           and rule.policy == rule_registry.Policy.ADMIT and rule.scope == ("tracked",) and rule.line == 3
           and rule.refuses == "a planted refusal" and rule.action == "fix it" and rule.has_module)
    slow = registry.rules["slow-row"]
    expect("a measured rule gives its thresholds and its holder, and has no module",
           slow.threshold == rule_registry.Threshold(10.0, 20.0, "s") and slow.held_by == "cmake"
           and not slow.has_module and set(registry.measured()) == {"slow-row"})
    refused = {
        "an unknown engine": GOOD_RULE.replace("engine=meta", "engine=regex"),
        "an unknown policy": GOOD_RULE.replace("ledger=admit", "ledger=many"),
        "an unknown field": GOOD_RULE.replace("doc=XV", "colour=red doc=XV"),
        "a missing field": GOOD_RULE.replace(" scope=tracked", ""),
        "a rule with one cell": "rule alpha-rule engine=meta scope=tracked ledger=admit doc=XV | a planted refusal",
        "an ID with an uppercase letter": GOOD_RULE.replace("alpha-rule", "Alpha"),
        "an absolute scope": GOOD_RULE.replace("scope=tracked", "scope=/etc"),
        "a scope that goes up": GOOD_RULE.replace("scope=tracked", "scope=../x"),
        "a doc that is not a section": GOOD_RULE.replace("doc=XV", "doc=section-15"),
        "only some of warn, error and unit": GOOD_RULE.replace("doc=XV", "warn=1 error=2 doc=XV"),
        "a warning threshold above the error threshold": rule_registry.measured_rule("slow-row", 20, 10, "s"),
        "an unknown unit": rule_registry.measured_rule("slow-row", 1, 2, "s").replace("unit=s", "unit=hours"),
        "a holder with no thresholds": GOOD_RULE.replace("doc=XV", "held-by=cmake doc=XV"),
        "a legacy module and a holder": rule_registry.measured_rule("slow-row", 1, 2, "s").replace(
            "held-by=cmake", "held-by=cmake legacy=guard.legacy.x"),
        "legacy-args with no legacy module": GOOD_RULE.replace("doc=XV", "legacy-args=--check,x doc=XV"),
        "a legacy module that is not a dotted name": GOOD_RULE.replace("doc=XV", "legacy=guard/legacy doc=XV"),
    }
    for name, row in refused.items():
        expect(f"a rule row with {name} is refused", bool(problem_of(row)))
    _, problems = examined(GOOD_RULE, GOOD_RULE.replace("a planted refusal", "a second refusal"))
    expect("a second rule row with the same ID is refused, with the line of the first",
           len(problems) == 1 and "line 3" in str(problems[0]))
    _, problems = rule_registry.examine("rule alpha-rule engine=meta\n", "planted.txt")
    expect("a registry with no format row is refused", any("format 1" in str(problem) for problem in problems))


def other_row_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the job rows, the path rows, an unknown row kind and load()."""
    registry, problems = examined("job subprocess module=assay.core.git | the one module that runs git",
                                  "path utils/registry.txt | the registry")
    expect("a job row and a path row read",
           not problems and registry.jobs[0].modules == ("assay.core.git",) and registry.paths[0].glob
           == "utils/registry.txt")
    expect("a job row with a module outside the package is refused",
           bool(problem_of("job subprocess module=os.path | a reason")))
    expect("a path row that goes up is refused", bool(problem_of("path ../x | a reason")))
    _, problems = examined("path a/b | one", "path a/b | two")
    expect("a second path row with the same glob is refused", len(problems) == 1)
    expect("an unknown row kind is refused", bool(problem_of("script utils/x.py | text")))
    registry = rule_registry.load()
    expect("the registry of the tree loads with no problem, and each measured rule has a unit of the schema",
           bool(registry.rules) and all(rule.threshold.unit in rule_registry.UNITS
                                        for rule in registry.measured().values() if rule.threshold is not None))


def doc_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the reference forms of the doc field and the sections that they name."""
    expect("the forms of a doc field", all(rule_registry.is_doc_reference(text)
                                           for text in ("XV", "XV.4", "XXII.R11", "L0", "L15")))
    expect("a doc field of another form is refused", not any(rule_registry.is_doc_reference(text)
                                                             for text in ("XCV", "XV.", "XV.x", "Lx", "15", "")))
    expect("a section gives its text", "1. **A rule.**" in (rule_registry.doc_section(GUIDE, "XV") or ""))
    expect("an item gives its line", (rule_registry.doc_section(GUIDE, "XV.2") or "").startswith("2. **Another"))
    expect("a rule gives its subsection", "The text of R11" in (rule_registry.doc_section(GUIDE, "XV.R11") or ""))
    expect("a layer gives its section", "The layer" in (rule_registry.doc_section(GUIDE, "L0") or ""))
    expect("a section, an item or a rule that the guide does not have gives None",
           all(rule_registry.doc_section(GUIDE, text) is None for text in ("XX", "XV.9", "XV.R12", "L1")))


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_registry", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    rule_cases(expect)
    other_row_cases(expect)
    doc_cases(expect)
    if failures:
        print(f"tests.core.test_registry: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_registry: every case holds.")
    return 0
