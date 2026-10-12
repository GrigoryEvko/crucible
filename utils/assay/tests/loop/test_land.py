"""The test of the landing of a change (assay.loop.land).

`guard selftest tests.loop.test_land` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases plant a git repository with a shared file, an own file, a ledger
and the ledger of the rule history.  The repository also holds a file of the
index that .gitignore matches.  The working tree then holds an edit of a
different writer in the shared file.  The cases compose a tree with an edit
of the shared file and the working-tree copy of the own file.  They land a
patch by compare-and-swap through a gate that each case plants.  They make
sure that a moved HEAD refuses a swap, and that a rejected commit does not
land.  They land added rows of a ledger as two commits with one swap.
"""

import json
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import git, testing
from assay.loop import gate, land

# The files of the planted repository.
PLANTED = {
    "shared.txt": "alpha\nbeta\n",
    "own.txt": "own at head\n",
    ".gitignore": "kept.txt\n",
    "kept.txt": "in the index, and .gitignore matches it\n",
    "utils/ledgers/demo.txt": "format 1\nrule demo\n",
    "utils/ledgers/history.txt": "# The ledger of the rule history.\nformat 1\nrule history\n",
}
# The edit of a different writer that is not committed, and the working-tree copy of the own file.
WRITER_SHARED = "alpha\nbeta\nanother writer\n"
WORKING_OWN = "own in the working tree\n"


def planted_repository(root: Path) -> str:
    """Plant the repository in root with one commit, and return the commit."""
    root.mkdir(parents=True, exist_ok=True)
    git.init(root)
    for name, text in PLANTED.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    git.run(root, "add", "-f", "kept.txt")
    return git.commit_all(root, "Add the planted tree")


def request_of(
    messages: tuple[str, ...],
    edits: tuple[land.Edit, ...] = (),
    own: tuple[str, ...] = (),
    patches: tuple[Path, ...] = (),
    grows: tuple[land.Grow, ...] = (),
) -> land.Request:
    """Return a request of the planted repository."""
    return land.Request(messages, patches, edits, own, grows)


def options(is_dry_run: bool = False, is_tree_only: bool = False, is_check: bool = False) -> land.Options:
    """Return the options of a landing."""
    return land.Options(is_dry_run, is_tree_only, is_check, False, False, 1)


def passing(seen: list[tuple[gate.Candidate, bool]], before: Callable[[], None] | None = None) -> land.Check:
    """Return a planted gate that keeps each call, runs `before` on the first call, and accepts each commit."""

    def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        if before is not None and not seen:
            before()
        seen.append((candidate, is_retry))
        return gate.Verdict(("debug",), (), 1.0)

    return check


def rejecting(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
    """A planted gate that rejects each commit."""
    return gate.Verdict(("debug",), (f"debug: red alone too: test_planted in {candidate.commit} {is_retry}",), 2.0)


def word_cases(expect: testing.Expect) -> None:
    """Hold the division of the words and the parse of the request."""
    before, after = land.split_words(
        ["--patch", "p", "-m", "A subject", "--replace", "f", "-m", "n", "-m", "--", "--", "-m", "path"]
    )
    expect(
        "the division skips the values of each option, so a value that is -- or -m is no end of the options",
        before == ["--patch", "p", "-m", "A subject", "--replace", "f", "-m", "n", "-m", "--"]
        and after == ["-m", "path"],
    )
    for name, words in (
        ("a request with no --", ["-m", "subject"]),
        ("an option with fewer values than it takes", ["--replace", "f", "o"]),
    ):
        try:
            land.split_words(words)
            expect(f"the division refuses {name}", False)
        except land.LandError:
            expect(f"the division refuses {name}", True)
    with tempfile.TemporaryDirectory(prefix="land-words-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "old").write_text("beta\n", encoding="utf-8")
        (scratch / "new").write_text("gamma\n", encoding="utf-8")
        words = ["-m", "-x is a subject", "-m", "body", "--patch", "change.patch", "--grow", "demo", "-- is a reason"]
        request, parsed = land.parse(
            [*words, "--jobs", "3", "--all-configurations", "--replace", "shared.txt", "old", "new", "--", "own.txt"],
            scratch,
        )
        expect(
            "the parse keeps a message and a reason that start with -, reads the edit files from the current "
            "directory, and gives each patch a full path",
            request.messages == ("-x is a subject", "body")
            and request.edits == (land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n"),)
            and request.patches == ((scratch / "change.patch").resolve(),)
            and request.paths == ("own.txt",)
            and request.grows == (land.Grow("demo", "-- is a reason"),)
            and request.message == "-x is a subject\n\nbody\n"
            and parsed == land.Options(False, False, False, False, True, 3),
        )
        check_request, check_options = land.parse(["--check", "--here", "--"], scratch)
        expect(
            "--check takes no message and no change, and then checks HEAD with the subject of a check",
            check_request.is_empty
            and check_options.is_check
            and check_options.is_here
            and check_request.subject == land.CHECK_SUBJECT,
        )
        for name, words in (
            ("a landing with no message", ["--patch", "p", "--"]),
            ("a request with nothing to commit", ["-m", "subject", "--"]),
            ("--here with no --check", ["--here", "-m", "s", "--patch", "p", "--"]),
            ("--check with --dry-run", ["--check", "--dry-run", "-m", "s", "--patch", "p", "--"]),
            ("a word that is no option", ["--force", "-m", "s", "--patch", "p", "--"]),
            ("--jobs that is no number above 0", ["--jobs", "0", "-m", "s", "--patch", "p", "--"]),
            ("--grow with no rule ID", ["--grow", "Not A Rule", "why", "-m", "s", "--patch", "p", "--"]),
            ("--grow with no change", ["--check", "--grow", "demo", "why", "--"]),
        ):
            try:
                land.parse(words, scratch)
                expect(f"the parse refuses {name}", False)
            except land.LandError:
                expect(f"the parse refuses {name}", True)


def edit_cases(expect: testing.Expect) -> None:
    """Hold the state of an edit in a content."""
    expect(
        "an insert applies one time after a unique text, and it is applied when its block is in the content",
        land.edit_state("a\nb\n", land.INSERT, "a\n", "x\n") == "apply"
        and land.edit_state("a\nx\nb\n", land.INSERT, "a\n", "x\n") == "applied"
        and land.edit_state("a\na\n", land.INSERT, "a\n", "x\n") == "ambiguous",
    )
    expect(
        "a replacement that extends its text applies one time, and not a second time",
        land.edit_state("one\n", land.REPLACE, "one", "one two") == "apply"
        and land.edit_state("one two\n", land.REPLACE, "one", "one two") == "applied",
    )
    expect(
        "a deletion is applied when the text is gone, and a plain replacement with two copies is ambiguous",
        land.edit_state("a\n", land.REPLACE, "b\n", "") == "applied"
        and land.edit_state("b\nb\n", land.REPLACE, "b\n", "c\n") == "ambiguous",
    )
    edit = land.Edit("f", land.REPLACE, "beta", "gamma")
    expect("apply_edit replaces the one copy of the text", land.apply_edit("alpha beta", edit, "f") == "alpha gamma")


def compose_cases(expect: testing.Expect) -> None:
    """Hold the composition, --tree and --dry-run."""
    with tempfile.TemporaryDirectory(prefix="land-compose-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        (root / "shared.txt").write_text(WRITER_SHARED, encoding="utf-8")
        (root / "own.txt").write_text(WORKING_OWN, encoding="utf-8")
        edit = land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n")
        request = request_of(("Replace a line of the shared file", "The body of the landing."), (edit,), ("own.txt",))
        composed = land.compose(root, request, head)
        expect(
            "the tree holds HEAD with the edit of the shared file and not the hunk of a different writer, and the "
            "working-tree copy of the own file",
            git.show(root, composed.tree, "shared.txt") == b"alpha\ngamma\n"
            and git.show(root, composed.tree, "own.txt") == WORKING_OWN.encode()
            and composed.changed == ("own.txt", "shared.txt"),
        )
        outcome, printed = testing.captured(lambda: land.land(root, request, options(is_tree_only=True), land.no_check))
        expect(
            "--tree prints the tree with no gate, writes no commit and does not touch the working tree",
            outcome.status == 0
            and printed.strip() == composed.tree
            and git.head(root) == head
            and (root / "shared.txt").read_text(encoding="utf-8") == WRITER_SHARED,
        )
        outcome, printed = testing.captured(lambda: land.land(root, request, options(is_dry_run=True), land.no_check))
        expect(
            "--dry-run prints the diff of the commit with no gate and writes no commit",
            outcome.status == 0 and "+gamma" in printed and git.head(root) == head,
        )
        for name, broken in (
            ("an own path that is also a shared file", request_of(("s",), (edit,), ("shared.txt",))),
            (
                "a shared file that HEAD does not hold",
                request_of(("s",), (land.Edit("missing.txt", land.REPLACE, "a", "b"),)),
            ),
            (
                "an edit with a text that the file does not hold",
                request_of(("s",), (land.Edit("shared.txt", land.REPLACE, "zeta", "omega"),)),
            ),
        ):
            try:
                land.compose(root, broken, head)
                expect(f"the composition refuses {name}", False)
            except land.LandError:
                expect(f"the composition refuses {name}", True)


def landing_cases(expect: testing.Expect) -> None:
    """Hold the landing through the gate, the index and the working tree, a moved HEAD, a rejected commit and --check."""
    with tempfile.TemporaryDirectory(prefix="land-landing-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        # A patch that a work tree made against HEAD.
        (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
        patch = work / "change.patch"
        patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
        (root / "shared.txt").write_text(PLANTED["shared.txt"], encoding="utf-8")
        request = request_of(("Change a line",), patches=(patch,))
        seen: list[tuple[gate.Candidate, bool]] = []
        outcome, _ = testing.captured(lambda: land.land(root, request, options(), passing(seen)))
        landed = git.head(root)
        expect(
            "the landing moves the branch one commit, with the message, on the earlier HEAD, to the commit that the "
            "gate passed",
            outcome.status == 0
            and outcome.is_landed
            and git.parent(root, "HEAD") == head
            and git.commit_subject(root, "HEAD") == "Change a line"
            and [(candidate.head, candidate.commit, candidate.changed, retry) for candidate, retry in seen]
            == [(head, landed, ("shared.txt",), False)],
        )
        expect(
            "after the landing, the index and the working tree hold the patch",
            git.is_index_clean(root, "HEAD") and (root / "shared.txt").read_text(encoding="utf-8") == "alpha\ndelta\n",
        )
        late_edit = land.Edit("own.txt", land.REPLACE, "head", "late")
        stale = land.compose(root, request_of(("Late",), (late_edit,)), landed)
        (root / "own.txt").write_text("own moved by another landing\n", encoding="utf-8")
        other = git.commit_all(root, "Another landing")
        late = land.commit_tree(root, stale, "Late\n")
        expect(
            "a swap from a HEAD that moved is refused, and the branch keeps the other landing",
            not land.swap(root, land.branch_of(root), late, landed, "Late") and git.head(root) == other,
        )

        def move_head() -> None:
            (root / "kept.txt").write_text("moved while the gate ran\n", encoding="utf-8")
            git.commit_all(root, "A landing while the gate runs")

        retry = request_of(("Again",), (land.Edit("own.txt", land.REPLACE, "moved by", "moved after"),))
        seen.clear()
        outcome, _ = testing.captured(
            lambda: land.land(root, retry, options(), passing(seen, move_head), pause=lambda _seconds: None)
        )
        moved = git.parent(root, "HEAD")
        expect(
            "a HEAD that moves while the gate runs gives a second composition on the new HEAD, which the gate checks "
            "as a retry, and the landing keeps the other landing",
            outcome.status == 0
            and outcome.compositions == 2
            and [retry_flag for _candidate, retry_flag in seen] == [False, True]
            and seen[1][0].head == moved
            and git.parent(root, moved) == other
            and git.show(root, "HEAD", "own.txt") == b"own moved after another landing\n",
        )
        before = git.head(root)
        rejected = request_of(("Rejected",), (land.Edit("own.txt", land.REPLACE, "after", "rejected"),))
        outcome, printed = testing.captured(lambda: land.land(root, rejected, options(), rejecting))
        expect(
            "a commit that the gate rejects does not land, and the command prints the problem",
            outcome.status == 1 and not outcome.is_landed and git.head(root) == before and "REJECTED" in printed,
        )
        seen.clear()
        outcome, _ = testing.captured(lambda: land.land(root, rejected, options(is_check=True), passing(seen)))
        expect(
            "--check runs the gate on a commit of the change and does not land it",
            outcome.status == 0
            and not outcome.is_landed
            and git.head(root) == before
            and outcome.candidate is not None
            and git.show(root, outcome.candidate.commit, "own.txt") == b"own moved rejected another landing\n",
        )
        empty = request_of(())
        outcome, _ = testing.captured(lambda: land.land(root, empty, options(is_check=True), passing(seen)))
        expect(
            "--check of an empty request checks HEAD itself",
            outcome.candidate == gate.Candidate(before, before, ()),
        )
        testing.captured(lambda: land.land(root, rejected, options(), passing([])))
        try:
            land.land(root, rejected, options(), passing([]))
            expect("a landing that changes nothing is refused", False)
        except land.LandError:
            expect("a landing that changes nothing is refused", True)


def grow_cases(expect: testing.Expect) -> None:
    """Hold the rows that a change adds to a ledger: two commits with one swap, and a ledger that does not change."""
    with tempfile.TemporaryDirectory(prefix="land-grow-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        (root / "utils/ledgers/demo.txt").write_text("format 1\nrule demo\nsome/path | kind | 1 | entity | why\n")
        grows = (land.Grow("demo", "A row that must stay"),)
        request = request_of(("Add a row to the demo ledger",), own=("utils/ledgers/demo.txt",), grows=grows)
        seen: list[tuple[gate.Candidate, bool]] = []
        outcome, _ = testing.captured(lambda: land.land(root, request, options(), passing(seen)))
        first = git.parent(root, "HEAD")
        history = (git.show(root, "HEAD", land.HISTORY_LEDGER) or b"").decode()
        expect(
            "a growth lands as the change on HEAD and a second commit with the grow row that names the change",
            outcome.status == 0
            and git.parent(root, first) == head
            and f"utils/ledgers/demo.txt | grow | admit | {first[:9]} | A row that must stay" in history
            and history.startswith("# The ledger of the rule history.")
            and seen[0][0].changed == ("utils/ledgers/demo.txt", land.HISTORY_LEDGER),
        )
        expect(
            "after a growth, the index and the working tree hold the grow row",
            git.is_index_clean(root, "HEAD") and (root / land.HISTORY_LEDGER).read_text(encoding="utf-8") == history,
        )
        (root / "own.txt").write_text("a change with no ledger\n", encoding="utf-8")
        wrong = request_of(("No growth",), own=("own.txt",), grows=(land.Grow("demo", "reason"),))
        try:
            land.land(root, wrong, options(), passing([]))
            expect("a grow row of a ledger that the change does not change is refused", False)
        except land.LandError:
            expect("a grow row of a ledger that the change does not change is refused", True)


def record_cases(expect: testing.Expect) -> None:
    """Hold the record of a run of the gate, and the driver of a tree with no copy of the module."""
    with tempfile.TemporaryDirectory(prefix="land-record-") as work_text:
        work = Path(work_text)
        candidate = gate.Candidate("a" * 40, "b" * 40, ("x.txt",))
        outcome = land.Outcome(0, candidate, 1, gate.Verdict(("debug", "release"), (), 3.5), True)
        land.write_record(work, request_of(("A subject",)), options(), outcome)
        printed = land.Outcome(0, None, 1, None, False)
        land.write_record(work, request_of(("Printed",)), options(is_dry_run=True), printed)
        lines = (work / land.RECORDS).read_text(encoding="utf-8").splitlines()
        record = json.loads(lines[0])
        expect(
            "a run of the gate gives one record with its commits, its configurations and its result, and --dry-run "
            "gives none",
            len(lines) == 1
            and record["subject"] == "A subject"
            and record["mode"] == "land"
            and record["commit"] == "b" * 40
            and record["configurations"] == ["debug", "release"]
            and record["landed"] is True
            and record["problems"] == [],
        )
        root = work / "repo"
        planted_repository(root)
        expect(
            "a tree whose main work tree holds no copy of the module gives the main work tree to the driver",
            land.driver_tree(root) == root.resolve() or land.driver_tree(root) == root,
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (word_cases, edit_cases, compose_cases, landing_cases, grow_cases, record_cases),
    )
