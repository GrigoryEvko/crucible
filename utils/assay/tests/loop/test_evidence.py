"""The test of the command `loop evidence` (assay.loop.evidence).

`guard selftest tests.loop.test_evidence` runs the test.  Each case writes one line.  Exit 0 when each case holds,
else 1.

The cases hold the usage errors of the command, the status of a build that is not configured, and the JSON status.
"""

import json
import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.core.exit import ExitCode
from assay.loop import evidence
from assay.loop.evidence import status as evidence_status


def usage_cases(expect: testing.Expect) -> None:
    """Hold the refusal of a budget that is not positive and of an unknown kind."""
    status, _ = testing.captured(lambda: evidence.main(["run", "repetition", "--budget-s", "0"]))
    expect("a budget that is not positive is a usage error", status == ExitCode.USAGE)
    try:
        testing.captured(lambda: evidence.main(["run", "unknown", "--budget-s", "5"]))
        expect("an unknown kind is a usage error", False)
    except SystemExit as stop:
        expect("an unknown kind is a usage error", stop.code == ExitCode.USAGE)


def status_cases(expect: testing.Expect) -> None:
    """Hold the status of a build that is not configured, as a table and as JSON (THE SUBCOMMANDS, status)."""
    with tempfile.TemporaryDirectory(prefix="evidence-command-") as directory, cache.scratch_root():
        missing = Path(directory) / "none"
        status, text = testing.captured(lambda: evidence.main(["status", "--build-dir", str(missing)]))
        expect(
            "the status of a build that is not configured names the problem", status == 0 and "not configured" in text
        )
        status, text = testing.captured(lambda: evidence.main(["status", "--build-dir", str(missing), "--json"]))
        found = json.loads(text)
        expect(
            "the JSON status has the format, no row and the problem of the build",
            status == 0
            and found["format"] == evidence_status.FORMAT
            and found["rows"] == []
            and "not configured" in found["problems"][0],
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (usage_cases, status_cases))
