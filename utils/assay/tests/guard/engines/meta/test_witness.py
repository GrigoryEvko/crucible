"""The test of the witness of the meta runner: the four prefixes of a plant and its environment (THE WITNESS).

`guard selftest tests.guard.engines.meta.test_witness` runs the test.  Each
case writes planted plants of a rule in a scratch tree, and runs the witness
with a planted judge.  The judge gives the outcome that the file outcome.txt
of the plant names.  Exit 0 when each case holds, else 1.
"""

import os
import tempfile
from pathlib import Path

from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.meta import runner as meta_runner

RULE_ID = "cache-size"
OUTCOME_FILE = "outcome.txt"
CI_VARIABLE = "GITHUB_ACTIONS"


def planted_judge(rule: object, context: meta_runner.RuleContext) -> list[check_report.Finding]:
    """Give the findings that the outcome of the copy of a plant names.

    The outcome "ci" gives an error, and a warning when GITHUB_ACTIONS is "true", as a time rule on a CI runner does.

    Raises:
        NotApplicable: For the outcome "skip"
    """
    outcome = (context.root / OUTCOME_FILE).read_text(encoding="utf-8").strip()
    if outcome == "skip":
        raise meta_runner.NotApplicable("the planted rule does not apply")
    if outcome == "ci":
        outcome = "warning" if os.environ.get(CI_VARIABLE) == "true" else "error"
    if outcome == "error":
        return [check_report.Finding("error", "a.txt", 1, RULE_ID, "a planted error")]
    if outcome == "warning":
        return [check_report.Finding("warning", "a.txt", 1, RULE_ID, "a planted warning")]
    return []


def plant(root: Path, name: str, outcome: str, environment: str = "") -> None:
    """Write one plant of the rule with its outcome, and its file environment.txt when `environment` holds rows."""
    directory = root / meta_runner.PLANTS / RULE_ID / name
    directory.mkdir(parents=True)
    (directory / OUTCOME_FILE).write_text(f"{outcome}\n", encoding="utf-8")
    if environment:
        (directory / meta_runner.ENVIRONMENT_FILE).write_text(environment, encoding="utf-8")


def run_witness(root: Path) -> tuple[int, str]:
    """Run the witness of the rule on the plants of a scratch tree, and give its status and its output."""
    from assay.core import registry as rule_registry

    rule = rule_registry.load().rules[RULE_ID]
    return testing.captured(lambda: meta_runner.witness(rule, root, judge=planted_judge, module=meta_runner))


def verdict_cases(expect: testing.Expect) -> None:
    """Judge the findings on one plant against the prefix of its name (plant_verdict())."""
    error = check_report.Finding("error", "a", 1, RULE_ID, "e")
    warning = check_report.Finding("warning", "a", 1, RULE_ID, "w")
    cases = [
        ("plant-a", [error], False, True),
        ("plant-a", [warning], False, False),
        ("plant-a", [warning], True, True),
        ("control-a", [warning], False, True),
        ("control-a", [warning], True, False),
        ("warning-a", [warning], False, True),
        ("warning-a", [warning, error], False, False),
        ("warning-a", [], False, False),
        ("skip-a", [], False, False),
    ]
    for name, found, counts_warnings, wanted in cases:
        holds, _, _ = meta_runner.plant_verdict(name, found, counts_warnings)
        expect(
            f"{name} with {len(found)} finding(s), {'counting' if counts_warnings else 'not counting'} warnings, "
            f"{'holds' if wanted else 'fails'}",
            holds == wanted,
        )


def environment_cases(expect: testing.Expect) -> None:
    """Read the variables of a plant, and set them only for the body (plant_variables(), planted_environment())."""
    with tempfile.TemporaryDirectory(prefix="test-witness-") as scratch:
        root = Path(scratch)
        (root / meta_runner.ENVIRONMENT_FILE).write_text("# planted\nGITHUB_ACTIONS | true\n", encoding="utf-8")
        expect(
            "plant_variables() reads each row NAME | VALUE",
            meta_runner.plant_variables(root) == ((CI_VARIABLE, "true"),),
        )
        (root / meta_runner.ENVIRONMENT_FILE).write_text("GITHUB_ACTIONS true | x\n", encoding="utf-8")
        try:
            meta_runner.plant_variables(root)
            refused = False
        except ValueError:
            refused = True
        expect("plant_variables() refuses a row that is not NAME | VALUE", refused)
    earlier = os.environ.get(CI_VARIABLE)
    os.environ[CI_VARIABLE] = "true"
    try:
        with meta_runner.planted_environment(()):
            removed = CI_VARIABLE not in os.environ
        with meta_runner.planted_environment(((CI_VARIABLE, "false"),)):
            planted = os.environ.get(CI_VARIABLE)
        expect(
            "the body of a plant that sets no GITHUB_ACTIONS has no such variable, a plant sets its own value, and "
            "the variable of the host comes back after each body",
            removed and planted == "false" and os.environ.get(CI_VARIABLE) == "true",
        )
    finally:
        if earlier is None:
            os.environ.pop(CI_VARIABLE, None)
        else:
            os.environ[CI_VARIABLE] = earlier


def witness_cases(expect: testing.Expect) -> None:
    """Run the witness on plants of each prefix, also on a host with GITHUB_ACTIONS (THE WITNESS)."""
    with tempfile.TemporaryDirectory(prefix="test-witness-") as scratch:
        root = Path(scratch)
        plant(root, "plant-error", "error")
        plant(root, "plant-ci-host", "ci")
        plant(root, "control-none", "none")
        plant(root, "warning-ci-runner", "ci", "# A planted CI runner.\nGITHUB_ACTIONS | true\n")
        plant(root, "skip-no-input", "skip")
        earlier = os.environ.get(CI_VARIABLE)
        os.environ[CI_VARIABLE] = "true"
        try:
            status, said = run_witness(root)
        finally:
            if earlier is None:
                os.environ.pop(CI_VARIABLE, None)
            else:
                os.environ[CI_VARIABLE] = earlier
        expect(
            "the witness holds for a plant, a control, a warning plant and a skip plant, and a plant with no "
            "environment.txt gives its error also on a host with GITHUB_ACTIONS",
            status == 0 and "holds for 5 plants" in said,
        )
    with tempfile.TemporaryDirectory(prefix="test-witness-") as scratch:
        root = Path(scratch)
        plant(root, "control-none", "none")
        plant(root, "warning-not-demoted", "ci")
        plant(root, "skip-applies", "none")
        plant(root, "plant-bad-environment", "error", "GITHUB_ACTIONS true | x\n")
        status, said = run_witness(root)
        expect(
            "the witness fails for a warning plant with an error, a skip plant that the rule applies to, and a bad "
            "environment.txt",
            status == 1
            and "warning-not-demoted: 1 error(s) and 0 warning(s)" in said
            and "skip-applies: the rule applies to it" in said
            and "plant-bad-environment: environment.txt:1" in said,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (verdict_cases, environment_cases, witness_cases))
