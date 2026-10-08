"""The test of the rule decide-cite-ratio (assay.guard.engines.meta.decide_cite_ratio).

`guard selftest tests.guard.engines.meta.test_decide_cite_ratio` runs the
test.  Each case writes one line.  Exit 0 when each case holds, else 1.  The
witness of the rule holds the verdicts on a tree.  This test holds the trees
that the rule does not judge: a shallow history and a tree with no commit.
"""

import tempfile
from pathlib import Path

from assay.core import git, testing
from assay.guard.engines.meta import decide_cite_ratio
from assay.guard.engines.meta.runner import MetaContext, NotApplicable

DECIDE = "#pragma once\nnamespace foundation::decide {\nconstexpr bool planted_good(int) noexcept { return true; }\n}\n"


def refusal(root: Path) -> str:
    """Return the reason why the rule does not apply to a tree, or an empty text when the rule applies."""
    try:
        decide_cite_ratio.findings(MetaContext(root))
    except NotApplicable as reason:
        return str(reason)
    return ""


def applicability_cases(expect: testing.Expect) -> None:
    """Check that the rule does not apply to a shallow history or to a tree with no commit."""
    files = {decide_cite_ratio.CATALOG: DECIDE.encode(), "src/anchor.cpp": b"int anchor = 0;\n"}
    with git.scratch_repository(files) as root:
        expect("the rule does not apply to a tree with no commit", "no commit" in refusal(root))
        git.commit_all(root, "The first commit", date="2025-01-01T12:00:00")
        git.commit_all(root, "The second commit", date="2025-02-01T12:00:00")
        expect("the rule applies to a full history", refusal(root) == "")
        with tempfile.TemporaryDirectory(prefix="test-decide-") as scratch:
            shallow = Path(scratch) / "shallow"
            git.run(Path(scratch), "clone", "-q", "--depth", "1", f"file://{root}", str(shallow))
            expect("the rule does not apply to a shallow history", "shallow" in refusal(shallow))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (applicability_cases,))
