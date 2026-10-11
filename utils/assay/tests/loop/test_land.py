"""The test of the landing of a change and of the steps of a guard run (assay.loop.land).

`guard selftest tests.loop.test_land` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases plant a git repository with a shared file and an own file.  The
repository also holds a file of the index that .gitignore matches, and a
registry with one measured rule.  The working tree then holds an edit of a
different writer in the shared file.  The cases compose a tree with an edit
of the shared file and the working-tree copy of the own file.  They export
the base and the change of a guard run, and they run the rule history on each
export.  They land a patch by compare-and-swap, and they make sure that a
moved HEAD refuses a swap.
"""

import sys
import tempfile
from pathlib import Path

from assay.core import git, paths, proc, testing
from assay.loop import land

# The files of the planted repository.  The registry holds one measured rule, and the rule history can then judge a
# raise.
PLANTED = {
    "shared.txt": "alpha\nbeta\n",
    "own.txt": "own at head\n",
    ".gitignore": "kept.txt\n",
    "kept.txt": "in the index, and .gitignore matches it\n",
    "utils/registry.txt": (
        "format 1\nrule slow-row engine=build scope=builddir ledger=max warn=10 error=20 unit=s "
        "held-by=cmake doc=XV | a time | lower the value\n"
    ),
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
) -> land.Request:
    """Return a request of the planted repository."""
    return land.Request(messages, patches, edits, own)


def options(is_dry_run: bool = False, is_tree_only: bool = False) -> land.Options:
    """Return the options of a landing with no guard run."""
    return land.Options(is_dry_run, is_tree_only, None, None, None, 1)


def history_status(root: Path) -> int:
    """Run the rule history of this tree on a planted tree, and return its exit."""
    command = [sys.executable, str(paths.GUARD), "check", "--rule", "history", "--root", str(root)]
    return proc.run(proc.Command(command, timeout_s=proc.TOOL_S)).exit


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
        words = ["-m", "-x is a subject", "-m", "body", "--patch", "change.patch"]
        request, parsed = land.parse([*words, "--replace", "shared.txt", "old", "new", "--", "own.txt"], scratch)
        expect(
            "the parse keeps a message that starts with -, reads the edit files from the current directory, and "
            "gives each patch a full path",
            request.messages == ("-x is a subject", "body")
            and request.edits == (land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n"),)
            and request.patches == ((scratch / "change.patch").resolve(),)
            and request.paths == ("own.txt",)
            and request.message == "-x is a subject\n\nbody\n"
            and parsed.step is None,
        )
        for name, words in (
            ("a landing with no message", ["--patch", "p", "--"]),
            ("a request with nothing to commit", ["-m", "subject", "--"]),
            ("a step with no name", ["--step", "change", "--patch", "p", "--"]),
            ("a step that is not a step", ["--step", "later", "--name", "x", "--patch", "p", "--"]),
            ("a word that is no option", ["--force", "-m", "s", "--patch", "p", "--"]),
        ):
            try:
                land.parse(words, scratch)
                expect(f"the parse refuses {name}", False)
            except land.LandError:
                expect(f"the parse refuses {name}", True)
        step_words = ["--step", "base", "--name", "unit-1", "--jobs", "3", "--patch", "p", "--"]
        step_request, step_options = land.parse(step_words, scratch)
        expect(
            "a step of a guard run takes no message, and its subject is the subject of a request with none",
            step_options.step == "base"
            and step_options.name == "unit-1"
            and step_options.jobs == 3
            and step_request.subject == land.DEFAULT_SUBJECT,
        )


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
    """Hold the composition, the exports of a guard run and the rule history on each export."""
    with tempfile.TemporaryDirectory(prefix="land-compose-") as work_text:
        work = Path(work_text)
        root = work / "repo"
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
        status, printed = testing.captured(lambda: land.land(root, request, options(is_tree_only=True)))
        expect(
            "--tree prints the tree, writes no commit and does not touch the working tree",
            status == 0
            and printed.strip() == composed.tree
            and git.head(root) == head
            and (root / "shared.txt").read_text(encoding="utf-8") == WRITER_SHARED,
        )
        status, printed = testing.captured(lambda: land.land(root, request, options(is_dry_run=True)))
        expect(
            "--dry-run prints the diff of the commit and writes no commit",
            status == 0 and "+gamma" in printed and git.head(root) == head,
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

        base = work / "base"
        land.export_tree(root, head, base)
        land.make_repository(root, base, head, None, git.run(root, "rev-parse", f"{head}^{{tree}}").decode().strip())
        expect(
            "the base export is HEAD with its history, a clean status, and the file of the index that .gitignore "
            "matches",
            git.head(base) == head
            and git.run(base, "status", "--porcelain") == b""
            and git.run(base, "ls-files", "kept.txt").strip() == b"kept.txt",
        )
        change = work / "change"
        land.export_tree(root, composed.tree, change)
        land.make_repository(root, change, head, request.message, composed.tree)
        expect(
            "the change export is one commit on HEAD with the composed tree and the message",
            git.run(change, "rev-parse", "HEAD^").decode().strip() == head
            and git.run(change, "rev-parse", "HEAD^{tree}").decode().strip() == composed.tree
            and git.run(change, "log", "-1", "--format=%B").decode().strip()
            == "Replace a line of the shared file\n\nThe body of the landing."
            and git.run(change, "status", "--porcelain") == b"",
        )

        raise_row = land.Edit("utils/registry.txt", land.REPLACE, "error=20 unit=s", "error=30 unit=s")
        raised = land.compose(root, request_of(("s",), (raise_row,)), head)
        verdicts: dict[str, int] = {"base": history_status(base)}
        bodies = (("unmeasured", "The tests are slower."), ("measured", "The slowest job of slow-row took 26 s."))
        for case, body in bodies:
            export = work / case
            land.export_tree(root, raised.tree, export)
            land.make_repository(root, export, head, f"Raise the slow row\n\n{body}\n", raised.tree)
            verdicts[case] = history_status(export)
        expect(
            f"the rule history gives 0 on the base export, 1 on a raise with no measurement and 0 on a measured "
            f"raise, and it gives {verdicts}",
            verdicts == {"base": 0, "unmeasured": 1, "measured": 0},
        )


def landing_cases(expect: testing.Expect) -> None:
    """Hold the landing of a patch, the index and the working tree after it, and the refusal of a moved HEAD."""
    with tempfile.TemporaryDirectory(prefix="land-landing-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        # A patch that a work tree made against HEAD.
        (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
        patch = work / "change.patch"
        patch.write_bytes(git.run(root, "diff", "HEAD", "--", "shared.txt"))
        (root / "shared.txt").write_text(PLANTED["shared.txt"], encoding="utf-8")
        request = request_of(("Change a line",), patches=(patch,))
        status, _ = testing.captured(lambda: land.land(root, request, options()))
        landed = git.head(root)
        expect(
            "the landing moves the branch one commit, with the message, on the earlier HEAD",
            status == 0
            and git.run(root, "rev-parse", "HEAD^").decode().strip() == head
            and git.run(root, "log", "-1", "--format=%s").decode().strip() == "Change a line",
        )
        expect(
            "after the landing, the index and the working tree hold the patch",
            git.run_any_exit(root, "diff", "--quiet", "--cached", "HEAD").exit == 0
            and (root / "shared.txt").read_text(encoding="utf-8") == "alpha\ndelta\n",
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
        retry = request_of(("Again",), (land.Edit("own.txt", land.REPLACE, "moved by", "moved after"),))
        status, _ = testing.captured(lambda: land.land(root, retry, options(), pause=lambda _seconds: None))
        expect(
            "a landing after the move composes on the new HEAD and keeps the other landing",
            status == 0
            and git.run(root, "rev-parse", "HEAD^").decode().strip() == other
            and git.show(root, "HEAD", "own.txt") == b"own moved after another landing\n",
        )
        try:
            land.land(root, retry, options())
            expect("a landing that changes nothing is refused", False)
        except land.LandError:
            expect("a landing that changes nothing is refused", True)


def lock_cases(expect: testing.Expect) -> None:
    """Hold the lock of a work directory of a guard run."""
    with tempfile.TemporaryDirectory(prefix="land-lock-") as work_text:
        work = Path(work_text) / "unit"
        with land.work_lock(work):
            try:
                with land.work_lock(work):
                    expect("a second run with the same name is refused while the first holds the lock", False)
            except land.LandError:
                expect("a second run with the same name is refused while the first holds the lock", True)
        with land.work_lock(work):
            expect("the lock is free after the first run", True)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (word_cases, edit_cases, compose_cases, landing_cases, lock_cases)
    )
