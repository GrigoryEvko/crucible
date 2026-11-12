"""The test of the landing of a change (assay.loop.land).

`guard selftest tests.loop.test_land` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases plant a git repository with a shared file, an own file and a
ledger.  The repository also holds a file of the index that .gitignore
matches.  The working tree then holds an edit of a
different writer in the shared file.  The cases compose a tree with an edit
of the shared file and the working-tree copy of the own file.  They land
batches of requests by compare-and-swap through a gate that each case plants.

A batch has no red request, one red request at each position, or a request
that does not compose.  It can also have a red interaction of two requests,
or a branch that is red with no request.  It can have a request that repairs
a red branch, or a branch that moves.  The cases count the runs of the gate
of each batch.  They cut a batch at a second change of a C++ file.  They drop
a request when its process goes from the queue.  They land the falls of the
gate.  The verdict of a landed request must name each flaky test of its run.

An own path can hold a file that an earlier request changes.  Such a request
must not compose, in a batch and after a swap.  The holder must give each
landed work tree its update before it frees the lock.  When the first request
of a batch has no work tree, the batch must land through the shared git
directory.

The cases write requests into a planted queue.  The holder of the tree must
take them in the sequence of their numbers and write the verdict of each.
An error of the check on the commit of one request must refuse only that
request.  An error that the run of the base raises too must give each
request the verdict of a tool error.  These rules also hold for an error
of a type that the landing does not expect, such as a TypeError.  Its
verdict must give its traceback, and the next holder must take no request
of its batch.
Each number must occur one time only.  A holder must write the verdicts of
the journals of a holder that ended after its swap.  A journal that git
cannot judge must give a refusal.

The process cases start landings as planted processes, and they land one
batch of three.  They hold a holder that ends in the gate, and a landing
that gets an interrupt.  They also hold a holder of the warm tree of an
earlier copy of the module.  A patch of a file and an own path with an
earlier copy of that file land in one batch, and in two batches.
"""

import contextlib
import fcntl
import functools
import json
import os
import sys
import tempfile
from collections.abc import Callable, Iterator, Sequence
from dataclasses import asdict, dataclass, replace
from pathlib import Path

from assay.core import git, paths, proc, testing
from assay.core.exit import ExitCode
from assay.loop import gate, heavy, land

# The files of the planted repository.
PLANTED = {
    "shared.txt": "alpha\nbeta\n",
    "own.txt": "own at head\n",
    ".gitignore": "kept.txt\n",
    "kept.txt": "in the index, and .gitignore matches it\n",
    "lib.cpp": "one\ntwo\n",
    "utils/ledgers/demo.txt": "format 1\nrule demo\n",
}
# The edit of a different writer that is not committed, and the working-tree copy of the own file.
WRITER_SHARED = "alpha\nbeta\nanother writer\n"
WORKING_OWN = "own in the working tree\n"
# The text of the own file of a request that the planted gate rejects.
RED = "red\n"
# The text of the own file of a request whose commit the planted gate cannot read, and the error that it raises.
UNREADABLE = "a row of a kind that the code of the gate does not know\n"
TABLE_ERROR = "rule-table.txt:525: the row kind 'linear-door' is unknown"
# The ledger that the falls of a planted gate write.
FALLEN = b"format 1\nrule demo\na.cpp | kind | 1 | entity | -\n"
# The time limit of a planted landing process, and of the wait for a file of a process, in seconds.
CHILD_S = 60.0
WAIT_S = 30.0
# A planted landing process.  It reads its part from the JSON of its first argument, and its request holds the own
# file and the patch of the part.  Its planted gate writes a line to the log, and then it ends the process, waits for
# a file, or accepts the commit.  Its holder lands only when the part lets it, and only when a named file is there.
CHILD = """\
import json
import os
import pathlib
import signal
import sys
import time

from assay.loop import gate, land

signal.signal(signal.SIGINT, signal.default_int_handler)
part = json.loads(sys.argv[1])
work = pathlib.Path(part["work"])
root = pathlib.Path(part["root"])


def check(candidate, is_retry):
    with open(part["log"], "a", encoding="utf-8") as handle:
        handle.write(part["subject"] + "\\n")
    if part.get("ends"):
        os._exit(9)
    go = part.get("go")
    if go:
        pathlib.Path(go + ".started").write_text("started\\n")
        while not pathlib.Path(go).exists():
            time.sleep(0.02)
    return gate.Verdict(("debug",), (), 1.0)


def holder(held_work, queue):
    after = part.get("holds_after")
    if not part.get("holds") or (after and not pathlib.Path(after).exists()):
        return False
    return land.hold(held_work, queue, check, lambda _seconds: None)


own = (part["own"],) if part["own"] else ()
patches = (pathlib.Path(part["patch"]),) if part["patch"] else ()
request = land.Request((part["subject"],), patches, (), own)
options = land.Options(False, False)
sys.exit(land.landing(work, root, request, options, holder, lambda _seconds: time.sleep(0.02)))
"""


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


def options(is_dry_run: bool = False, is_tree_only: bool = False, is_check: bool = False) -> land.Options:
    """Return the options of a landing."""
    return land.Options(is_dry_run, is_tree_only, is_check, False, False, 1)


def passing(seen: list[tuple[gate.Candidate, bool]], before: Callable[[], None] | None = None) -> land.Check:
    """Return a planted gate that keeps each call, runs `before` on each call, and accepts each commit."""

    def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        if before is not None:
            before()
        seen.append((candidate, is_retry))
        return gate.Verdict(("debug",), (), 1.0)

    return check


def marking(root: Path, names: Sequence[str], seen: list[tuple[gate.Candidate, bool]]) -> land.Check:
    """Return a planted gate that rejects a commit when one of the files holds the text RED, and keeps each call."""

    def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        seen.append((candidate, is_retry))
        red = [name for name in names if git.show(root, candidate.commit, name) == RED.encode()]
        return gate.Verdict(("debug",), tuple(f"debug: red alone too: test_{name}" for name in red), 1.0)

    return check


def no_pause(_seconds: float) -> None:
    """A pause that does not wait."""


def batch_landing(
    root: Path,
    requests: Sequence[land.Request],
    check: land.Check,
    is_check: bool = False,
    is_alive: land.IsAlive | None = None,
) -> tuple[dict[int, land.Decision], land.BatchLanding]:
    """Land a batch of requests numbered from 1 through a planted gate, and return each verdict and the landing.

    The function then gives the work tree the update of each verdict, as the process of each request does.
    """
    batch = [
        land.Queued(index + 1, root, request, options(is_check=is_check)) for index, request in enumerate(requests)
    ]
    decisions: dict[int, land.Decision] = {}

    def decide(queued: land.Queued, decision: land.Decision) -> None:
        decisions[queued.number] = decision

    batched = land.BatchLanding(batch, check, decide, no_pause, is_alive)
    batched.land(batch)
    for queued in batch:
        decision = decisions.get(queued.number)
        if decision is not None and decision.update is not None:
            warnings = land.updated_tree(root, "", queued.request, decision.update)
            lines = (*decision.lines, *(land.Line(text, True) for text in warnings))
            decisions[queued.number] = replace(decision, lines=lines)
    return decisions, batched


def bare_landing(
    root: Path, requests: Sequence[land.Request], check: land.Check
) -> tuple[dict[int, land.Decision], land.BatchLanding]:
    """Land a batch of requests numbered from 1 through a planted gate, and return each verdict and the landing.

    The function calls only the constructors and the method land() of the batch.  A case with it then also runs on an
    earlier copy of the module.  The function gives no work tree an update.
    """
    batch = [land.Queued(index + 1, root, request, options()) for index, request in enumerate(requests)]
    decisions: dict[int, land.Decision] = {}

    def decide(queued: land.Queued, decision: land.Decision) -> None:
        decisions[queued.number] = decision

    batched = land.BatchLanding(batch, check, decide, no_pause)
    batched.land(batch)
    return decisions, batched


def landed(
    root: Path,
    requests: Sequence[land.Request],
    check: land.Check,
    is_check: bool = False,
) -> tuple[dict[int, land.Decision], list[land.RunRecord]]:
    """Land a batch of requests numbered from 1 through a planted gate, and return each verdict and the runs."""
    decisions, batched = batch_landing(root, requests, check, is_check)
    return decisions, batched.records()


def own_requests(root: Path, texts: Sequence[str]) -> list[land.Request]:
    """Write one own file r1.txt, r2.txt, ... for each text, and return one request for each file."""
    requests: list[land.Request] = []
    for index, text in enumerate(texts, start=1):
        (root / f"r{index}.txt").write_text(text, encoding="utf-8")
        requests.append(request_of((f"Add r{index}",), own=(f"r{index}.txt",)))
    return requests


def subjects_since(root: Path, head: str) -> list[str]:
    """Return the subject of each commit of HEAD after a commit, from the earliest."""
    found: list[str] = []
    commit = git.head(root)
    while commit != head:
        found.append(git.commit_subject(root, commit))
        commit = git.parent(root, commit)
    return list(reversed(found))


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
        request, parsed = land.parse(
            [*words, "--jobs", "3", "--all-configurations", "--replace", "shared.txt", "old", "new", "--", "own.txt"],
            scratch,
        )
        expect(
            "the parse keeps a message that starts with -, reads the edit files from the current directory, and "
            "gives each patch a full path",
            request.messages == ("-x is a subject", "body")
            and request.edits == (land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n"),)
            and request.patches == ((scratch / "change.patch").resolve(),)
            and request.paths == ("own.txt",)
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
        status, printed = testing.captured(lambda: land.preview(root, request, options(is_tree_only=True)))
        expect(
            "--tree prints the tree with no gate, writes no commit and does not touch the working tree",
            status == 0
            and printed.strip() == composed.tree
            and git.head(root) == head
            and (root / "shared.txt").read_text(encoding="utf-8") == WRITER_SHARED,
        )
        status, printed = testing.captured(lambda: land.preview(root, request, options(is_dry_run=True)))
        expect(
            "--dry-run prints the diff of the commit with no gate and writes no commit",
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


def batch_cases(expect: testing.Expect) -> None:
    """Hold one run of the gate for a batch, the search for a red request at each position, and a refused request."""
    with tempfile.TemporaryDirectory(prefix="land-batch-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        names = ("r1.txt", "r2.txt", "r3.txt")
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, records = landed(root, own_requests(root, ("one\n", "two\n", "three\n")), marking(root, names, seen))
        tip = git.head(root)
        middle = git.parent(root, tip)
        expect(
            "a batch of three requests runs the gate one time on the last commit with the changed paths of the three, "
            "and one swap moves the branch over three commits in the sequence of the batch",
            [(candidate.head, candidate.changed, retry) for candidate, retry in seen] == [(head, names, False)]
            and seen[0][0].commit == tip
            and subjects_since(root, head) == ["Add r1", "Add r2", "Add r3"]
            and [decisions[number].status for number in (1, 2, 3)] == [0, 0, 0]
            and decisions[3].commit == tip
            and decisions[2].commit == middle,
        )
        expect(
            "the record of the run gives each request with the result landed",
            len(records) == 1 and records[0].results == (land.LANDED,) * 3 and records[0].run.numbers == (1, 2, 3),
        )
        counts: dict[int, int] = {}
        for position in (1, 2, 3):
            root = Path(work_text) / f"repo-red-{position}"
            head = planted_repository(root)
            texts = [RED if index == position else f"good {index}\n" for index in (1, 2, 3)]
            seen = []
            decisions, records = landed(root, own_requests(root, texts), marking(root, names, seen))
            counts[position] = len(seen)
            good = [index for index in (1, 2, 3) if index != position]
            expect(
                f"with the red request at position {position}, the search refuses only it, with the problem of its "
                f"run, and the branch holds the other two in their sequence.  Each run after the first is a retry",
                decisions[position].status == ExitCode.FINDINGS
                and any(f"r{position}.txt" in line.content for line in decisions[position].lines)
                and all(decisions[index].status == 0 for index in good)
                and subjects_since(root, head) == [f"Add r{index}" for index in good]
                and [retry for _candidate, retry in seen] == [False] + [True] * (len(seen) - 1)
                and seen[1][0].commit == head
                and sum(land.REFUSED in record.results for record in records) == 1
                and sum(land.LANDED in record.results for record in records) == 1,
            )
        expect(
            "with the run of the base, the search needs 4 runs for a red request at the first or the last position, "
            "and 5 runs at the second",
            counts == {1: 4, 2: 5, 3: 4},
        )
        root = Path(work_text) / "repo-lost"
        planted_repository(root)
        first = request_of(("Replace beta",), (land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n"),))
        second = request_of(("Replace beta again",), (land.Edit("shared.txt", land.REPLACE, "beta\n", "delta\n"),))
        third = request_of(("Replace alpha",), (land.Edit("shared.txt", land.REPLACE, "alpha\n", "omega\n"),))
        seen = []
        decisions, _ = landed(root, [first, second, third], passing(seen))
        expect(
            "a request that does not compose on the request before it gets the refusal of the composition only, and "
            "the others land in one run",
            decisions[2].status == ExitCode.USAGE
            and decisions[1].status == 0
            and decisions[3].status == 0
            and len(seen) == 1
            and git.show(root, "HEAD", "shared.txt") == b"omega\ngamma\n",
        )
        interaction_cases(expect, Path(work_text))


def interaction_cases(expect: testing.Expect, work: Path) -> None:
    """Hold a red interaction of two requests, a branch that is red with no request, and the cut of a batch."""
    root = work / "repo-interaction"
    head = planted_repository(root)
    requests = own_requests(root, ("alpha\n", "beta\n", "gamma\n"))
    seen: list[tuple[gate.Candidate, bool]] = []

    def interacting(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        """Reject a commit that holds r1.txt and r2.txt, and accept each other commit."""
        seen.append((candidate, is_retry))
        both = all(git.show(root, candidate.commit, name) is not None for name in ("r1.txt", "r2.txt"))
        return gate.Verdict(("debug",), ("debug: red alone too: test_pair",) if both else (), 1.0)

    decisions, _ = landed(root, requests, interacting)
    expect(
        "when only an interaction of two requests is red, the second of the two gets the refusal, and the others land",
        decisions[2].status == ExitCode.FINDINGS
        and any("test_pair" in line.content for line in decisions[2].lines)
        and decisions[1].status == 0
        and decisions[3].status == 0
        and subjects_since(root, head) == ["Add r1", "Add r3"]
        and len(seen) == 5,
    )
    root = work / "repo-cut"
    head = planted_repository(root)
    (root / "r3.txt").write_text("three\n", encoding="utf-8")
    first = request_of(("Change one",), (land.Edit("lib.cpp", land.REPLACE, "one\n", "uno\n"),))
    second = request_of(("Change two",), (land.Edit("lib.cpp", land.REPLACE, "two\n", "dos\n"),))
    third = request_of(("Add r3",), own=("r3.txt",))
    seen = []
    (decisions, batched), printed = testing.captured(lambda: batch_landing(root, [first, second, third], passing(seen)))
    expect(
        "a request that changes a C++ file of an earlier request of the batch waits for the next batch with each "
        "request after it, and gets no verdict",
        list(decisions) == [1]
        and [queued.number for queued in batched.deferred] == [2, 3]
        and subjects_since(root, head) == ["Change one"]
        and "wait for the next batch" in printed,
    )


def red_base_cases(expect: testing.Expect) -> None:
    """Hold the run of the base: a red branch, a request that repairs it, a lone request, and the problems of two runs.

    Each case with a planted repository calls only the constructors and the method land() of the batch.  It then
    also runs on an earlier copy of the module.  These cases come first.
    """
    with tempfile.TemporaryDirectory(prefix="land-red-base-") as work_text:
        work = Path(work_text)
        root = work / "repo-red-base"
        planted_repository(root)
        (root / "base.txt").write_text(RED, encoding="utf-8")
        red_base = git.commit_all(root, "Make the branch red")
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, batched = bare_landing(
            root, own_requests(root, ("one\n", "two\n", "three\n")), marking(root, ("base.txt",), seen)
        )
        expect(
            "on a branch that is red with no request that repairs it, the run of the base refuses each request with "
            "its problems, after 2 runs of the gate, and the run of the base is a retry",
            len(seen) == 2
            and seen[1][0].head == seen[1][0].commit == red_base
            and seen[1][0].changed == ("r1.txt", "r2.txt", "r3.txt")
            and seen[1][1]
            and all(decisions[number].status == ExitCode.FINDINGS for number in (1, 2, 3))
            and all(
                any("is red with no request" in line.content for line in decisions[number].lines)
                for number in (1, 2, 3)
            )
            and git.head(root) == red_base
            and batched.records()[1].run.numbers == (),
        )
        root = work / "repo-repair"
        planted_repository(root)
        (root / "base.txt").write_text(RED, encoding="utf-8")
        red_base = git.commit_all(root, "Make the branch red")
        (root / "base.txt").write_text("repaired\n", encoding="utf-8")
        (root / "r2.txt").write_text(RED, encoding="utf-8")
        (root / "r3.txt").write_text("three\n", encoding="utf-8")
        requests = [
            request_of(("Repair the branch",), own=("base.txt",)),
            request_of(("Add r2",), own=("r2.txt",)),
            request_of(("Add r3",), own=("r3.txt",)),
        ]
        seen = []
        decisions, _ = bare_landing(root, requests, marking(root, ("base.txt", "r2.txt"), seen))
        expect(
            "on a red branch, a batch with a request that repairs it lands the repair and the request after the red "
            "request, and refuses only the red request with its own problem, after 5 runs of the gate",
            [decisions[number].status for number in (1, 2, 3)] == [0, ExitCode.FINDINGS, 0]
            and subjects_since(root, red_base) == ["Repair the branch", "Add r3"]
            and len(seen) == 5
            and not any("red with no request" in line.content for line in decisions[2].lines),
        )
        root = work / "repo-red-lone"
        planted_repository(root)
        (root / "base.txt").write_text(RED, encoding="utf-8")
        red_lone = git.commit_all(root, "Make the branch red")
        seen = []
        decisions, _ = bare_landing(root, own_requests(root, ("one\n",)), marking(root, ("base.txt",), seen))
        expect(
            "a lone request on a red branch gets the run of the base too, and its refusal names the red commit",
            len(seen) == 2
            and seen[1][0].commit == red_lone
            and decisions[1].status == ExitCode.FINDINGS
            and any(
                f"the commit {red_lone[: land.SHORT_HASH]}" in line.content and "red with no request" in line.content
                for line in decisions[1].lines
            ),
        )
        root = work / "repo-lone-red"
        planted_repository(root)
        seen = []
        decisions, _ = bare_landing(root, own_requests(root, (RED,)), marking(root, ("r1.txt",), seen))
        expect(
            "a lone red request on a green branch gets the run of the base, and its refusal gives its own problem",
            len(seen) == 2
            and seen[1][1]
            and decisions[1].status == ExitCode.FINDINGS
            and any("test_r1.txt" in line.content for line in decisions[1].lines)
            and not any("red with no request" in line.content for line in decisions[1].lines),
        )
    first, second = (
        gate.Verdict(
            ("debug",),
            (f"debug: red alone too: test_a, see /work/logs/run-00000{run}/debug.test.log",),
            1.0,
            logs=f"/work/logs/run-00000{run}",
        )
        for run in (3, 7)
    )
    expect(
        "the problems of two runs compare equal with the mark as the replacement of the directory of the logs of each "
        "run",
        land.shown_problems(first) == land.shown_problems(second)
        and land.shown_problems(first) != frozenset(first.problems),
    )


def focus_cases(expect: testing.Expect) -> None:
    """Hold the focus of the run of the base: it is the focus of the first run of the batch (THE RUN OF THE BASE)."""
    with tempfile.TemporaryDirectory(prefix="land-focus-") as work_text:
        root = Path(work_text) / "repo"
        planted_repository(root)
        (root / "base.txt").write_text(RED, encoding="utf-8")
        red_base = git.commit_all(root, "Make the branch red")
        focus = (gate.Focus("debug", ("test_base.txt",)),)
        seen: list[tuple[gate.Candidate, bool]] = []

        def focusing(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Reject a commit with the red base.txt, with the focus of its test, and keep each call."""
            seen.append((candidate, is_retry))
            is_red = git.show(root, candidate.commit, "base.txt") == RED.encode()
            problems = ("debug: red alone too: test_base.txt",) if is_red else ()
            return gate.Verdict(("debug",), problems, 1.0, focus=focus if is_red else ())

        decisions, _ = landed(root, own_requests(root, ("one\n",)), focusing)
        expect(
            "the run of the base is a retry of the commit of the batch with the focus of the first run, and a branch "
            "that is red there too refuses the request",
            len(seen) == 2
            and seen[0][0].focus == ()
            and seen[1][0].focus == focus
            and seen[1][0].commit == red_base
            and seen[1][1]
            and decisions[1].status == ExitCode.FINDINGS
            and any("red with no request" in line.content for line in decisions[1].lines),
        )


def revert_cases(expect: testing.Expect) -> None:
    """Hold an own path with a file that a different request changes, and the shared git directory of a batch.

    The first two cases call only the constructors and the method land() of the batch.  They then also run on an
    earlier copy of the module.  The cases after them hold the update of the holder, and the own paths of a
    composition after a swap.
    """
    with tempfile.TemporaryDirectory(prefix="land-revert-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        patch = work / "delta.patch"
        (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
        patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
        (root / "shared.txt").write_text("alpha\nbeta\ngamma\n", encoding="utf-8")
        own_copy = request_of(("Add a line to the copy of the work tree",), own=("shared.txt",))
        requests = [request_of(("Change a line with a patch",), patches=(patch,)), own_copy]
        decisions, _ = bare_landing(root, requests, passing([]))
        expect(
            "in a batch, a request whose own path holds a file that an earlier request of the batch changes is "
            "refused, and the branch keeps the change of the earlier request",
            decisions[1].status == 0
            and decisions[2].status == ExitCode.USAGE
            and git.show(root, "HEAD", "shared.txt") == b"alpha\ndelta\n"
            and subjects_since(root, head) == ["Change a line with a patch"],
        )
        swapped_root, before_swap = root, head
        root = work / "repo-shared"
        planted_repository(root)
        (root / "r2.txt").write_text("two\n", encoding="utf-8")
        common = str(git.common_dir(root).resolve())
        branch = land.branch_of(root)
        gone = land.Queued(1, work / "gone-tree", request_of(("Add r1",), own=("r1.txt",)), options(), branch, common)
        alive = land.Queued(2, root, request_of(("Add r2",), own=("r2.txt",)), options(), branch, common)
        shared: dict[int, land.Decision] = {}
        try:
            land.BatchLanding(
                [gone, alive], passing([]), lambda item, decision: shared.update({item.number: decision}), no_pause
            ).land([gone, alive])
            raised = ""
        except (git.GitError, land.LandError) as problem:
            raised = str(problem)
        expect(
            "a batch whose first request has no work tree reads and moves the branch through the shared git "
            "directory, refuses that request, and lands the next",
            not raised
            and shared[1].status == ExitCode.TOOL_FAILED
            and shared[2].status == 0
            and git.commit_subject(root, "HEAD") == "Add r2",
        )
        root = work / "repo-holder"
        planted_repository(root)
        queue = land.queue_of(work / "work", root)
        (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
        patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
        (root / "shared.txt").write_text(PLANTED["shared.txt"], encoding="utf-8")
        with waiting(queue, root, request_of(("Change a line",), patches=(patch,))) as only:
            testing.captured(lambda: land.hold(work / "work", queue, passing([]), no_pause))
            decision = land.read_decision(queue, only.number, only.token)
            entry = git.tree_entry(root, "HEAD", "shared.txt")
            has_update = (root / "shared.txt").read_text(encoding="utf-8") == "alpha\ndelta\n" and (
                entry is not None and git.index_object(root, "shared.txt") == entry.name
            )
        expect(
            "the holder gives the work tree of a landed request its update before it frees the lock, also when the "
            "process of the request did not read its verdict",
            decision is not None and decision.status == 0 and has_update,
        )
        tip = git.head(swapped_root)
        try:
            land.compose(swapped_root, own_copy, tip, since=before_swap)
            refusal = ""
        except land.LandError as problem:
            refusal = str(problem)
        composed = land.compose(swapped_root, own_copy, tip, since=tip)
        expect(
            "after a swap, an own path with a file that the swap changed is refused when HEAD of the work tree was "
            "the commit before the swap, and it composes when HEAD was the commit of the swap",
            "changed after the request" in refusal and composed.changed == ("shared.txt",),
        )


def landing_cases(expect: testing.Expect) -> None:
    """Hold a landing through the gate, the index and the working tree, a moved branch, a rejected commit and --check."""
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
        decisions, _ = landed(root, [request], passing(seen))
        landed_commit = git.head(root)
        expect(
            "the landing moves the branch one commit, with the message, on the earlier HEAD, to the commit that the "
            "gate passed",
            decisions[1].status == 0
            and decisions[1].commit == landed_commit
            and git.parent(root, "HEAD") == head
            and git.commit_subject(root, "HEAD") == "Change a line"
            and [(candidate.head, candidate.commit, candidate.changed, retry) for candidate, retry in seen]
            == [(head, landed_commit, ("shared.txt",), False)],
        )
        expect(
            "after the update of the verdict, the index and the working tree hold the patch",
            git.is_index_clean(root, "HEAD") and (root / "shared.txt").read_text(encoding="utf-8") == "alpha\ndelta\n",
        )
        late_edit = land.Edit("own.txt", land.REPLACE, "head", "late")
        stale = land.compose(root, request_of(("Late",), (late_edit,)), landed_commit)
        (root / "own.txt").write_text("own moved by another landing\n", encoding="utf-8")
        other = git.commit_all(root, "Another landing")
        late = land.commit_tree(root, stale, "Late\n")
        expect(
            "a swap from a HEAD that moved is refused, and the branch keeps the other landing",
            not land.swap(root, land.branch_of(root), late, landed_commit, "Late") and git.head(root) == other,
        )
        moves: list[str] = []

        def move_head() -> None:
            """Land one commit that changes only kept.txt, as a different landing does, on the first call."""
            if not moves:
                (root / "kept.txt").write_text("moved while the gate ran\n", encoding="utf-8")
                tip = git.head(root)
                composed = land.compose(root, request_of(("Move",), own=("kept.txt",)), tip)
                moved = land.commit_tree(root, composed, "A landing while the gate runs\n")
                land.swap(root, land.branch_of(root), moved, tip, "Move")
                moves.append(moved)

        retry = request_of(("Again",), (land.Edit("own.txt", land.REPLACE, "moved by", "moved after"),))
        (root / "r2.txt").write_text("a second request of the batch\n", encoding="utf-8")
        second = request_of(("Add r2",), own=("r2.txt",))
        seen.clear()
        (decisions, _), _ = testing.captured(lambda: landed(root, [retry, second], passing(seen, move_head)))
        expect(
            "a branch that moves while the gate runs gives a second composition of the batch on the new tip, which "
            "the gate checks as a retry, and the batch keeps the other landing",
            decisions[1].status == 0
            and decisions[2].status == 0
            and [retry_flag for _candidate, retry_flag in seen] == [False, True]
            and seen[1][0].head == moves[0]
            and subjects_since(root, moves[0]) == ["Again", "Add r2"]
            and git.show(root, "HEAD", "own.txt") == b"own moved after another landing\n",
        )

        def move_always() -> None:
            (root / "kept.txt").write_text(f"moved {len(seen)}\n", encoding="utf-8")
            git.commit_all(root, f"A landing {len(seen)}")

        seen.clear()
        again = request_of(("Never",), (land.Edit("own.txt", land.REPLACE, "after", "never"),))
        (decisions, _), printed = testing.captured(lambda: landed(root, [again], passing(seen, move_always)))
        expect(
            "a branch that moves on each run stops the batch after the refusals of the rule, with a refusal",
            decisions[1].status == ExitCode.USAGE
            and len(seen) == land.ATTEMPTS
            and printed.count("branch moved") == land.ATTEMPTS - 1,
        )
        before = git.head(root)
        rejected = request_of(("Rejected",), (land.Edit("own.txt", land.REPLACE, "after", "rejected"),))
        (root / "red.txt").write_text(RED, encoding="utf-8")
        marked = request_of(("Rejected",), (land.Edit("own.txt", land.REPLACE, "after", "rejected"),), ("red.txt",))
        decisions, records = landed(root, [marked], marking(root, ("red.txt",), []))
        expect(
            "a commit that the gate rejects does not land, and its verdict gives the problem",
            decisions[1].status == ExitCode.FINDINGS
            and git.head(root) == before
            and any("REJECTED" in line.content for line in decisions[1].lines)
            and records[0].results == (land.REFUSED,),
        )
        seen.clear()
        decisions, records = landed(root, [rejected], passing(seen), is_check=True)
        expect(
            "--check runs the gate on a commit of the change and does not land it",
            decisions[1].status == 0
            and git.head(root) == before
            and git.show(root, seen[0][0].commit, "own.txt") == b"own moved rejected another landing\n"
            and records[0].results == (land.ACCEPTED,),
        )
        nothing = request_of(("Nothing",), (land.Edit("own.txt", land.REPLACE, "after", "after"),))
        decisions, _ = landed(root, [nothing], passing([]))
        expect(
            "a landing that changes nothing is refused",
            decisions[1].status == ExitCode.USAGE
            and any("change nothing" in line.content for line in decisions[1].lines),
        )
        update_cases(expect, work, root)


def update_cases(expect: testing.Expect, work: Path, root: Path) -> None:
    """Hold the update of a work tree: an error gives a warning, and a different branch gets no update.

    The first case calls only the constructors and the method land() of the batch.  It then also runs on an earlier
    copy of the module.
    """
    head = git.head(root)
    patch = work / "gone.patch"
    (root / "shared.txt").write_text("alpha\nepsilon\n", encoding="utf-8")
    patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
    (root / "shared.txt").write_text((git.show(root, "HEAD", "shared.txt") or b"").decode(), encoding="utf-8")
    request = request_of(("Change a line with a patch that goes",), patches=(patch,))
    decisions: dict[int, land.Decision] = {}
    queued = land.Queued(1, root, request, options())
    batched = land.BatchLanding(
        [queued], passing([], patch.unlink), lambda item, decision: decisions.update({item.number: decision}), no_pause
    )
    try:
        _, printed = testing.captured(lambda: batched.land([queued]))
        error = ""
    except OSError as problem:
        printed, error = "", str(problem)
    expect(
        "after the swap, an error of a file of the update of the holder gives a line, and the verdict stays a landing",
        not error
        and 1 in decisions
        and decisions[1].status == 0
        and git.parent(root, "HEAD") == head
        and "stops" in printed,
    )
    update = decisions[1].update
    warnings = land.updated_tree(root, land.branch_of(root), request, update) if update is not None else []
    expect(
        "after a landing, an error of a file of the update of the process gives a warning that the commit is on the "
        "branch",
        len(warnings) == 1 and "stops" in warnings[0] and "on the branch" in warnings[0],
    )
    elsewhere = land.updated_tree(root, "refs/heads/elsewhere", request, update) if update is not None else []
    expect(
        "a work tree with HEAD on a different branch gets no update, and a warning",
        len(elsewhere) == 1 and "gets no update" in elsewhere[0],
    )


def falls_cases(expect: testing.Expect) -> None:
    """Hold a verdict with the falls of the gate: the branch moves to the commit of the gate, with the ledger."""
    with tempfile.TemporaryDirectory(prefix="land-falls-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        (root / "own.txt").write_text(WORKING_OWN, encoding="utf-8")
        (root / "r2.txt").write_text("second\n", encoding="utf-8")

        def folding(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            final = git.replaced_commit(root, candidate.commit, {"utils/ledgers/demo.txt": FALLEN})
            return gate.Verdict(("debug", "debug"), (), 1.0, (), final, ("utils/ledgers/demo.txt",))

        requests = [request_of(("Remove a finding",), own=("own.txt",)), request_of(("Add r2",), own=("r2.txt",))]
        decisions, _ = landed(root, requests, folding)
        expect(
            "the branch moves to the commit of the gate, which holds the two changes and the written ledger, with "
            "the message of the last change on the first",
            decisions[2].status == 0
            and git.parent(root, git.parent(root, "HEAD")) == head
            and git.show(root, "HEAD", "own.txt") == WORKING_OWN.encode()
            and git.show(root, "HEAD", "utils/ledgers/demo.txt") == FALLEN
            and git.commit_subject(root, "HEAD") == "Add r2"
            and any("with the falls of utils/ledgers/demo.txt" in line.content for line in decisions[2].lines),
        )
        expect(
            "after the update, the index and the working tree hold the written ledger",
            git.is_index_clean(root, "HEAD") and (root / "utils/ledgers/demo.txt").read_bytes() == FALLEN,
        )
        root = Path(work_text) / "repo-search"
        head = planted_repository(root)
        finals: list[str] = []

        def folding_red(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Reject a commit with a red own file, and give each other commit the falls."""
            if git.show(root, candidate.commit, "r2.txt") == RED.encode():
                return gate.Verdict(("debug",), ("debug: red alone too: test_r2",), 1.0)
            final = git.replaced_commit(root, candidate.commit, {"utils/ledgers/demo.txt": FALLEN})
            finals.append(final)
            return gate.Verdict(("debug", "debug"), (), 1.0, (), final, ("utils/ledgers/demo.txt",))

        decisions, _ = landed(root, own_requests(root, ("one\n", RED, "three\n")), folding_red)
        expect(
            "falls in more than one run of the search land with the last accepted run, on the accepted part",
            decisions[2].status == ExitCode.FINDINGS
            and decisions[3].status == 0
            and git.head(root) == finals[-1]
            and git.show(root, "HEAD", "utils/ledgers/demo.txt") == FALLEN
            and subjects_since(root, head) == ["Add r1", "Add r3"]
            and (root / "utils/ledgers/demo.txt").read_bytes() == FALLEN,
        )


def flaky_cases(expect: testing.Expect) -> None:
    """Hold THE FLAKY TESTS: the verdict of a landed request names each flaky test of its accepted run of the gate."""
    with tempfile.TemporaryDirectory(prefix="land-flaky-") as work_text:
        root = Path(work_text) / "repo"
        planted_repository(root)

        def flaky_gate(_candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Accept each commit, with one test that was red in the suite and green alone."""
            return gate.Verdict(("debug",), (), 1.0, logs="/work/logs/run-000001", flaky=("debug: test_flaky",))

        decisions, _ = landed(root, own_requests(root, ("one\n", "two\n")), flaky_gate)
        named = (
            f"{land.FLAKY_MARK}debug: test_flaky is red in the suite and green alone (logs in /work/logs/run-000001)"
        )
        expect(
            "each landed request of the run gets one line that names the flaky test and the logs of its run",
            all(
                decisions[number].status == 0
                and [line.content for line in decisions[number].lines if line.content.startswith(land.FLAKY_MARK)]
                == [named]
                for number in (1, 2)
            ),
        )


def drop_cases(expect: testing.Expect) -> None:
    """Hold a request when its process goes from the queue while the gate runs: it drops out before the swap."""
    with tempfile.TemporaryDirectory(prefix="land-drop-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        alive = {1, 2, 3}
        seen: list[tuple[gate.Candidate, bool]] = []

        def leave() -> None:
            alive.discard(2)

        decisions, batched = batch_landing(
            root,
            own_requests(root, ("one\n", "two\n", "three\n")),
            passing(seen, leave),
            is_alive=lambda queued: queued.number in alive,
        )
        records = batched.records()
        expect(
            "the batch drops the request of a process that left before the swap, composes the others again, runs "
            "the gate again and lands them, and the dropped request gets no verdict",
            sorted(decisions) == [1, 3]
            and subjects_since(root, head) == ["Add r1", "Add r3"]
            and len(seen) == 2
            and records[0].results == (land.ACCEPTED, land.DROPPED, land.ACCEPTED)
            and records[1].results == (land.LANDED, land.LANDED),
        )


@contextlib.contextmanager
def waiting(queue: Path, root: Path, request: land.Request) -> Iterator[land.Queued]:
    """Write a request into the queue as a waiting process does, and hold its lock for the block."""
    with land.entered(queue, root, request, options()) as queued:
        yield queued


def queue_cases(expect: testing.Expect) -> None:
    """Hold the files of the queue, the numbers, the tokens, the liveness of a request, and the verdicts."""
    with tempfile.TemporaryDirectory(prefix="land-queue-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        planted_repository(root)
        queue = land.queue_of(work, root)
        patch = Path(work_text) / "change.patch"
        (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
        patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
        (root / "shared.txt").write_text(PLANTED["shared.txt"], encoding="utf-8")
        edit = land.Edit("shared.txt", land.INSERT, "alpha\n", "inserted\n")
        full = request_of(("A subject", "A body"), (edit,), ("own.txt",), (patch,))
        expect(
            "the queue of a repository is a directory of its own, keyed by its shared git directory",
            queue.parent == work / land.QUEUE and len(queue.name) == land.KEY_LENGTH,
        )
        with (
            waiting(queue, root, full) as first,
            waiting(queue, root, request_of(("Second",), own=("own.txt",))) as second,
        ):
            lock = land.queue_file(queue, first.number, land.LOCK_SUFFIX)
            found = land.waiting_requests(queue, no_pause)
            stored = found[0].request
            expect(
                "two waiting requests get the numbers 1 and 2, each holds its lock, and the holder reads them in "
                "their sequence with the message, the copy of the patch, the edit, the own path and the token",
                (first.number, second.number) == (1, 2)
                and heavy.is_held(lock)
                and [item.number for item in found] == [1, 2]
                and stored.messages == full.messages
                and stored.edits == full.edits
                and stored.paths == full.paths
                and [path.read_bytes() for path in stored.patches] == [patch.read_bytes()]
                and found[0].branch == land.branch_of(root)
                and found[0].root == root
                and found[0].token == first.token
                and first.token != second.token,
            )
            elsewhere = replace(found[1], branch="refs/heads/elsewhere")
            expect(
                "a batch holds the requests with the git directory and the branch of its first request, in their "
                "sequence",
                land.batch_of(found) == found and land.batch_of([found[0], elsewhere]) == [found[0]],
            )
            verdict = land.Decision(3, "abc", (land.Line("one"), land.Line("two", True)))
            land.write_decision(queue, found[0], verdict)
            expect(
                "a verdict reads back with its token and with no other token, and the holder takes no request with "
                "a verdict",
                land.read_decision(queue, 1, first.token) == verdict
                and land.read_decision(queue, 1, second.token) is None
                and [item.number for item in land.waiting_requests(queue, no_pause)] == [2],
            )
        expect(
            "a process that leaves the queue removes each file of its request, the lock file last",
            paths.listing(queue, "0*") == () and not heavy.is_held(lock),
        )
        with waiting(queue, root, request_of(("Third",), own=("own.txt",))) as third:
            expect(
                "a new request gets a number that no earlier request had, also after its files went", third.number == 3
            )
        stale = land.queue_file(queue, 9, land.VERDICT_SUFFIX)
        stale.write_bytes(land.encoded_decision(land.Decision(0, "stale"), ""))
        with waiting(queue, root, request_of(("After a stale verdict",), own=("own.txt",))) as fourth:
            found = land.waiting_requests(queue, no_pause)
            expect(
                "a number is above the number of each file of the queue, and a verdict with no lock goes",
                fourth.number == 10 and [item.number for item in found] == [10] and not stale.exists(),
            )
        queue_wait_cases(expect, queue, root)
        landing_queue_cases(expect, work, root)


def queue_wait_cases(expect: testing.Expect, queue: Path, root: Path) -> None:
    """Hold a request with no request file, a request file that does not read, and a verdict of a different token."""
    stuck_number, stuck = land.reserve(queue)
    with waiting(queue, root, request_of(("After a stuck one",), own=("own.txt",))) as late:
        try:
            pauses: list[float] = []
            found = land.waiting_requests(queue, pauses.append)
        finally:
            os.close(stuck)
        expect(
            "a live request with no request file waits for the wait of the rule, and then the holder takes the later "
            "requests",
            stuck_number < late.number
            and [item.number for item in found] == [late.number]
            and sum(pauses) >= land.INCOMPLETE_S,
        )
        broken_number, broken = land.reserve(queue)
        try:
            land.queue_file(queue, broken_number, land.REQUEST_SUFFIX).write_text("{}", encoding="utf-8")
            found = land.waiting_requests(queue, no_pause)
            decision = land.read_decision(queue, broken_number, "any token")
        finally:
            os.close(broken)
        expect(
            "a request file that does not read gets the verdict of a usage error, and the holder takes the other "
            "requests",
            [item.number for item in found] == [late.number]
            and decision is not None
            and decision.status == ExitCode.USAGE
            and any("does not read" in line.content for line in decision.lines),
        )
        land.write_decision(queue, replace(late, token="other"), land.Decision(0, "of a different request"))
        found = land.waiting_requests(queue, no_pause)
        expect(
            "a verdict with a different token goes, and the holder takes the request",
            [item.number for item in found] == [late.number]
            and not land.queue_file(queue, late.number, land.VERDICT_SUFFIX).exists(),
        )
    expect("the file of the next number stays in the queue", (queue / land.NEXT).is_file())


def landing_queue_cases(expect: testing.Expect, work: Path, root: Path) -> None:
    """Hold the wait of a landing: the holder lands the waiting requests in one batch, and each gets its verdict."""
    queue = land.queue_of(work, root)
    (root / "r1.txt").write_text("one\n", encoding="utf-8")
    (root / "r2.txt").write_text("two\n", encoding="utf-8")
    (root / "r3.txt").write_text("three\n", encoding="utf-8")
    head = git.head(root)
    seen: list[tuple[gate.Candidate, bool]] = []
    with (
        waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as first,
        waiting(queue, root, request_of(("Add r2",), own=("r2.txt",))) as second,
    ):
        status, printed = testing.captured(
            lambda: land.landing(
                work,
                root,
                request_of(("Add r3",), own=("r3.txt",)),
                options(),
                lambda held_work, held_queue: land.hold(held_work, held_queue, passing(seen), no_pause),
                no_pause,
            )
        )
        decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second)]
    records = [json.loads(line) for line in (work / land.RECORDS).read_text(encoding="utf-8").splitlines()]
    expect(
        "the holder takes the two waiting requests and its own in the sequence of their numbers, lands the three in "
        "one run of the gate, and writes the verdict of each",
        status == 0
        and len(seen) == 1
        and seen[0][0].head == head
        and subjects_since(root, head) == ["Add r1", "Add r2", "Add r3"]
        and [decision.status if decision is not None else None for decision in decisions] == [0, 0]
        and all(decision is not None and decision.update is not None for decision in decisions)
        and "committed" in printed,
    )
    entry = git.tree_entry(root, "HEAD", "r3.txt")
    expect(
        "the process of the holder gives its own work tree the update of its verdict",
        entry is not None and git.index_object(root, "r3.txt") == entry.name,
    )
    expect(
        "the record of the run gives each subject with its result",
        [item["subject"] for item in records[-1]["requests"]] == ["Add r1", "Add r2", "Add r3"]
        and all(item["result"] == land.LANDED for item in records[-1]["requests"])
        and records[-1]["landed"] is True
        and records[-1]["base"] is False,
    )
    hold_drop_cases(expect, work, root)


def hold_drop_cases(expect: testing.Expect, work: Path, root: Path) -> None:
    """Hold a process that goes from the queue while the gate of its holder runs: no verdict for it, and one for each other."""
    queue = land.queue_of(work, root)
    (root / "r4.txt").write_text("four\n", encoding="utf-8")
    (root / "r5.txt").write_text("five\n", encoding="utf-8")
    head = git.head(root)
    number, descriptor = land.reserve(queue)
    leaving = land.Queued(
        number,
        root,
        request_of(("Add r4",), own=("r4.txt",)),
        options(),
        land.branch_of(root),
        str(git.common_dir(root).resolve()),
        "leaving token",
    )
    land.queue_file(queue, number, land.REQUEST_SUFFIX).write_bytes(land.encoded_request(leaving, []))
    calls: list[int] = []

    def leave() -> None:
        if not calls:
            os.close(descriptor)
        calls.append(1)

    with waiting(queue, root, request_of(("Add r5",), own=("r5.txt",))) as staying:
        captured_hold = testing.captured(lambda: land.hold(work, queue, passing([], leave), no_pause))
        staying_decision = land.read_decision(queue, staying.number, staying.token)
        leaving_verdict = land.queue_file(queue, number, land.VERDICT_SUFFIX).exists()
    expect(
        "a holder drops a request whose process left in the gate, writes no verdict for it, and lands the other",
        captured_hold[0]
        and len(calls) == 2
        and not leaving_verdict
        and staying_decision is not None
        and staying_decision.status == 0
        and subjects_since(root, head) == ["Add r5"],
    )


def error_cases(expect: testing.Expect) -> None:
    """Hold THE ERROR OF A RUN: an error of the check on one request, and an error of the run of the base too."""
    with tempfile.TemporaryDirectory(prefix="land-error-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        queue = land.queue_of(work, root)
        texts = {"r1.txt": "one\n", "r2.txt": UNREADABLE, "r3.txt": "three\n"}
        for name, text in texts.items():
            (root / name).write_text(text, encoding="utf-8")
        seen: list[tuple[gate.Candidate, bool]] = []

        def raising(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Raise the error of a table that the code of the gate cannot read on a commit with r2.txt."""
            seen.append((candidate, is_retry))
            if git.show(root, candidate.commit, "r2.txt") == UNREADABLE.encode():
                raise ValueError(TABLE_ERROR)
            return gate.Verdict(("debug",), (), 1.0)

        with (
            waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as first,
            waiting(queue, root, request_of(("Add r2",), own=("r2.txt",))) as second,
            waiting(queue, root, request_of(("Add r3",), own=("r3.txt",))) as third,
        ):
            took, _printed = testing.captured(lambda: land.hold(work, queue, raising, no_pause))
            decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second, third)]
        statuses = [decision.status if decision is not None else None for decision in decisions]
        bad = decisions[1]
        expect(
            f"an error of the check on the commit of one request refuses only that request, with the text of the "
            f"error, after the run of the base and the search, and the other two land: statuses {statuses}, "
            f"{len(seen)} runs of the gate",
            took
            and statuses == [0, ExitCode.FINDINGS, 0]
            and bad is not None
            and any(TABLE_ERROR in line.content for line in bad.lines)
            and subjects_since(root, head) == ["Add r1", "Add r3"]
            and len(seen) == 5
            and seen[1][0].commit == head,
        )
        tip = git.head(root)
        (root / "r4.txt").write_text("four\n", encoding="utf-8")
        (root / "r5.txt").write_text("five\n", encoding="utf-8")
        calls: list[gate.Candidate] = []

        def broken(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Raise an error of the warm tree on each commit, also on the commit of the run of the base."""
            calls.append(candidate)
            raise gate.GateError("the warm tree cannot hold the commit")

        with (
            waiting(queue, root, request_of(("Add r4",), own=("r4.txt",))) as fourth,
            waiting(queue, root, request_of(("Add r5",), own=("r5.txt",))) as fifth,
        ):
            testing.captured(lambda: land.hold(work, queue, broken, no_pause))
            decisions = [land.read_decision(queue, item.number, item.token) for item in (fourth, fifth)]
        expect(
            "an error that the run of the base raises too gives each request the verdict of a tool error, after 2 runs "
            "of the gate, and the branch does not move",
            [decision.status if decision is not None else None for decision in decisions]
            == [ExitCode.TOOL_FAILED, ExitCode.TOOL_FAILED]
            and all(
                decision is not None and any("cannot hold the commit" in line.content for line in decision.lines)
                for decision in decisions
            )
            and len(calls) == 2
            and calls[1].commit == tip
            and git.head(root) == tip,
        )


def hold_guarded(work: Path, queue: Path, check: land.Check) -> tuple[bool, str]:
    """Run one holder with a planted gate, and return if it took a batch and the text of an error that it raised."""
    try:
        took, _printed = testing.captured(lambda: land.hold(work, queue, check, no_pause))
    except Exception as problem:  # noqa: BLE001
        return False, repr(problem)
    return took, ""


def defect_cases(expect: testing.Expect) -> None:
    """Hold an error of a type that the landing does not expect, such as a TypeError of the code of the gate."""
    with tempfile.TemporaryDirectory(prefix="land-defect-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        queue = land.queue_of(work, root)
        texts = {"r1.txt": "one\n", "r2.txt": UNREADABLE, "r3.txt": "three\n"}
        for name, text in texts.items():
            (root / name).write_text(text, encoding="utf-8")

        def typed(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Raise a TypeError on a commit with r2.txt, as the code of the gate does on a file of a new shape."""
            if git.show(root, candidate.commit, "r2.txt") == UNREADABLE.encode():
                raise TypeError("'NoneType' object is not subscriptable")
            return gate.Verdict(("debug",), (), 1.0)

        with (
            waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as first,
            waiting(queue, root, request_of(("Add r2",), own=("r2.txt",))) as second,
            waiting(queue, root, request_of(("Add r3",), own=("r3.txt",))) as third,
        ):
            took, raised = hold_guarded(work, queue, typed)
            decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second, third)]
        statuses = [decision.status if decision is not None else None for decision in decisions]
        bad = decisions[1]
        expect(
            f"a TypeError of the check on the commit of one request refuses only that request, with the type, the "
            f"text and the traceback of the error, and the other two land: statuses {statuses}, error {raised!r}",
            took
            and not raised
            and statuses == [0, ExitCode.FINDINGS, 0]
            and bad is not None
            and any("TypeError: 'NoneType'" in line.content and "Traceback" in line.content for line in bad.lines)
            and subjects_since(root, head) == ["Add r1", "Add r3"],
        )
        tip = git.head(root)
        (root / "r4.txt").write_text("four\n", encoding="utf-8")
        (root / "r5.txt").write_text("five\n", encoding="utf-8")
        calls: list[gate.Candidate] = []

        def defective(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Raise a TypeError on each commit, also on the commit of the run of the base."""
            calls.append(candidate)
            raise TypeError("a defect of the code of the gate")

        with (
            waiting(queue, root, request_of(("Add r4",), own=("r4.txt",))) as fourth,
            waiting(queue, root, request_of(("Add r5",), own=("r5.txt",))) as fifth,
        ):
            took, raised = hold_guarded(work, queue, defective)
            decisions = [land.read_decision(queue, item.number, item.token) for item in (fourth, fifth)]
            again = land.waiting_requests(queue, no_pause)
        expect(
            f"a TypeError of each run, also of the run of the base, stops the batch: each request gets a tool error "
            f"with the traceback, the branch does not move, and the next holder takes no request of the batch: "
            f"error {raised!r}",
            took
            and not raised
            and [decision.status if decision is not None else None for decision in decisions]
            == [ExitCode.TOOL_FAILED, ExitCode.TOOL_FAILED]
            and all(
                decision is not None
                and any(
                    "TypeError: a defect" in line.content and "Traceback" in line.content for line in decision.lines
                )
                for decision in decisions
            )
            and len(calls) == 2
            and git.head(root) == tip
            and again == [],
        )


def journal_cases(expect: testing.Expect) -> None:
    """Hold a holder that stops after its swap: the next holder writes the verdicts of the journals and composes nothing."""
    with tempfile.TemporaryDirectory(prefix="land-journal-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        queue = land.queue_of(work, root)
        requests = own_requests(root, ("one\n", "two\n"))
        with waiting(queue, root, requests[0]) as first, waiting(queue, root, requests[1]) as second:
            batch = land.waiting_requests(queue, no_pause)

            def failing(_queued: land.Queued, _decision: land.Decision) -> None:
                raise OSError("the disk is full")

            batched = land.BatchLanding(
                batch, passing([]), failing, no_pause, functools.partial(land.is_alive, queue), land.Journal(queue)
            )
            try:
                batched.land(batch)
                has_failed = False
            except OSError:
                has_failed = True
            journals = [land.queue_file(queue, item.number, land.JOURNAL_SUFFIX).is_file() for item in batch]
            settled: dict[int, land.Decision] = {}
            failure = land.refused(ExitCode.TOOL_FAILED, "loop land: the disk is full")
            land.settle(batch, lambda item, decision: settled.update({item.number: decision}), batched, failure)
            expect(
                "a verdict that cannot be written after the swap stops the batch with the branch moved, the journals "
                "stay, and the error does not refuse a request that landed",
                has_failed
                and subjects_since(root, head) == ["Add r1", "Add r2"]
                and journals == [True, True]
                and not any(land.queue_file(queue, item.number, land.VERDICT_SUFFIX).exists() for item in batch)
                and [settled[item.number].status for item in batch] == [0, 0],
            )
            tip = git.head(root)
            again = land.waiting_requests(queue, no_pause)
            decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second)]
            expect(
                "the next holder writes the verdict of each journal of a commit that the branch holds, takes no "
                "request, and the branch does not move",
                again == []
                and git.head(root) == tip
                and all(decision is not None and decision.status == 0 for decision in decisions)
                and all(decision is not None and decision.update is not None for decision in decisions),
            )
        root = Path(work_text) / "repo-no-swap"
        head = planted_repository(root)
        queue = land.queue_of(work, root)
        (root / "r1.txt").write_text("one\n", encoding="utf-8")
        with waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as only:
            composed = land.compose(root, only.request, head)
            unlanded = land.commit_tree(root, composed, "Add r1\n")
            entry = land.JournalEntry(only.token, only.branch, unlanded, land.Decision(0, unlanded))
            land.write_journal(queue, only, entry)
            found = land.waiting_requests(queue, no_pause)
            expect(
                "a journal of a swap that did not occur goes, and the holder takes the request",
                [item.number for item in found] == [only.number]
                and not land.queue_file(queue, only.number, land.JOURNAL_SUFFIX).exists()
                and land.read_decision(queue, only.number, only.token) is None,
            )
            missing = "f" * 40
            unknown = land.JournalEntry(only.token, only.branch, missing, land.Decision(0, missing))
            land.write_journal(queue, only, unknown)
            found = land.waiting_requests(queue, no_pause)
            decision = land.read_decision(queue, only.number, only.token)
            has_journal = land.queue_file(queue, only.number, land.JOURNAL_SUFFIX).exists()
            expect(
                "a journal that git cannot judge goes, and the request gets the refusal of a tool error with the "
                "reason, and its process then stops its wait",
                found == []
                and decision is not None
                and decision.status == ExitCode.TOOL_FAILED
                and any("git cannot tell" in line.content for line in decision.lines)
                and not has_journal,
            )


def record_cases(expect: testing.Expect) -> None:
    """Hold the record of the runs of the gate, and the driver of a tree with no copy of the module."""
    with tempfile.TemporaryDirectory(prefix="land-record-") as work_text:
        work = Path(work_text)
        candidate = gate.Candidate("a" * 40, "b" * 40, ("x.txt",))
        verdict = gate.Verdict(("debug", "release"), (), 3.5, logs="/logs/run-000001")
        run = land.Run(candidate, (1, 2), ("A subject", "B subject"), verdict)
        base = land.Run(gate.Candidate("a" * 40, "a" * 40, ("x.txt",)), (), (), verdict)
        land.write_records(work, "land", [land.RunRecord(run, (land.LANDED, land.REFUSED)), land.RunRecord(base, ())])
        land.write_records(work, "land", [])
        lines = (work / land.RECORDS).read_text(encoding="utf-8").splitlines()
        record = json.loads(lines[0])
        expect(
            "a run of the gate gives one record with its commits, each request with its result, its configurations "
            "and its logs, a run of the base gives a record with no request, and no run gives no record",
            len(lines) == 2
            and record["mode"] == "land"
            and record["commit"] == "b" * 40
            and record["base"] is False
            and record["requests"]
            == [
                {"number": 1, "subject": "A subject", "result": "landed"},
                {"number": 2, "subject": "B subject", "result": "refused"},
            ]
            and record["configurations"] == ["debug", "release"]
            and record["landed"] is True
            and record["logs"] == "/logs/run-000001"
            and record["problems"] == []
            and json.loads(lines[1])["base"] is True
            and json.loads(lines[1])["requests"] == [],
        )
        root = work / "repo"
        planted_repository(root)
        expect(
            "a tree whose main work tree holds no copy of the module gives the main work tree to the driver",
            land.driver_tree(root) == root.resolve() or land.driver_tree(root) == root,
        )


def heavy_lock_cases(expect: testing.Expect) -> None:
    """Hold the refusal of a landing in a heavy step: the variable of loop heavy, and a descriptor of the lock."""
    with tempfile.TemporaryDirectory(prefix="land-heavy-") as work_text:
        root = Path(work_text) / "repo"
        planted_repository(root)
        with testing.environment_variable(heavy.STEP_VARIABLE, None):
            free = land.held_heavy_lock(root)
        with testing.environment_variable(heavy.STEP_VARIABLE, heavy.STEP_VALUE):
            by_variable = land.held_heavy_lock(root)
        lock = heavy.lock_path(root)
        lock.parent.mkdir(parents=True, exist_ok=True)
        with testing.environment_variable(heavy.STEP_VARIABLE, None), open(lock, "a", encoding="ascii") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            by_descriptor = land.held_heavy_lock(root)
        expect(
            "a landing refuses to run with the variable of loop heavy or with an open descriptor of the lock, and "
            "runs with neither",
            free is None
            and by_variable is not None
            and heavy.STEP_VARIABLE in by_variable
            and by_descriptor is not None
            and str(lock.resolve()) in by_descriptor,
        )


@dataclass(frozen=True, slots=True)
class Part:
    """The part of one planted landing process: the subject, the own file and the patch of its request, and its behavior.

    An empty `own` or `patch` gives the request no own file or no patch.  `holds` lets its holder land, and
    `holds_after` names a file that must be there first.  `ends` makes its gate end the process.  `go` names a file
    that its gate waits for, after it writes that name with .started.
    """

    subject: str
    own: str
    holds: bool = True
    ends: bool = False
    go: str = ""
    holds_after: str = ""
    patch: str = ""


def started(work: Path, root: Path, log: Path, parts: Sequence[Part], before: int = 0) -> list[proc.Running]:
    """Start one planted landing process for each part, one after the other, each after the request of the one before.

    The requests then get the numbers in the sequence of the parts.  `before` is the count of the requests of the
    queue before the first part.
    """
    utils = Path(land.__file__).resolve().parents[2]
    environment = proc.environment({"PYTHONPATH": str(utils)})
    queue = land.queue_of(work, root)
    runs: list[proc.Running] = []
    for index, part in enumerate(parts, start=1):
        full = {"work": str(work), "root": str(root), "log": str(log), **asdict(part)}
        command = proc.Command(
            [sys.executable, "-c", CHILD, json.dumps(full)],
            timeout_s=CHILD_S,
            env=environment,
            stop=proc.Stop.INTERRUPT,
        )
        runs.append(proc.start(command))

        def has_entered(count: int = before + index) -> bool:
            """Tell if the queue holds the request files of the processes that started."""
            return len(paths.listing(queue, f"*{land.REQUEST_SUFFIX}")) >= count

        testing.wait_until(has_entered, WAIT_S)
    return runs


def log_lines(log: Path) -> list[str]:
    """Return the lines of the log of the planted gates of the processes."""
    return log.read_text(encoding="utf-8").splitlines() if log.is_file() else []


def process_cases(expect: testing.Expect) -> None:
    """Hold landings of planted processes: one batch, a holder that ends, an interrupt, and a holder of an earlier copy."""
    with tempfile.TemporaryDirectory(prefix="land-process-") as work_text:
        base = Path(work_text)
        work, root, log = base / "work-batch", base / "repo-batch", base / "batch.log"
        head = planted_repository(root)
        for index in (1, 2, 3):
            (root / f"r{index}.txt").write_text(f"{index}\n", encoding="utf-8")
        parts = [Part(f"Add r{index}", f"r{index}.txt") for index in (1, 2, 3)]
        with gate.tree_lock(work):
            runs = started(work, root, log, parts)
        exits = [run.wait().exit for run in runs]
        expect(
            "three landing processes that wait for the warm tree land as one batch with one run of the gate, in the "
            "sequence of their numbers, and each exits 0 with the update of its work tree",
            exits == [0, 0, 0]
            and len(log_lines(log)) == 1
            and subjects_since(root, head) == ["Add r1", "Add r2", "Add r3"]
            and git.is_index_clean(root, "HEAD"),
        )
        work, root, log = base / "work-ends", base / "repo-ends", base / "ends.log"
        head = planted_repository(root)
        for index in (1, 2, 3):
            (root / f"r{index}.txt").write_text(f"{index}\n", encoding="utf-8")
        parts = [
            Part("Add r1", "r1.txt", ends=True),
            Part("Add r2", "r2.txt", holds_after=str(log)),
            Part("Add r3", "r3.txt", holds=False),
        ]
        with gate.tree_lock(work):
            runs = started(work, root, log, parts)
        exits = [run.wait().exit for run in runs]
        expect(
            "a holder that ends in the gate lands nothing, and the next holder lands the other requests with no "
            "verdict for the request that ended",
            exits == [9, 0, 0]
            and log_lines(log) == ["Add r1", "Add r2"]
            and subjects_since(root, head) == ["Add r2", "Add r3"],
        )
        interrupt_cases(expect, base)
        revert_process_cases(expect, base)


def interrupt_cases(expect: testing.Expect, base: Path) -> None:
    """Hold an interrupt of a waiting landing in the gate, and a holder of the warm tree of an earlier copy."""
    work, root, log = base / "work-interrupt", base / "repo-interrupt", base / "interrupt.log"
    head = planted_repository(root)
    go = base / "go"
    for index in (1, 2, 3):
        (root / f"r{index}.txt").write_text(f"{index}\n", encoding="utf-8")
    parts = [
        Part("Add r1", "r1.txt", go=str(go)),
        Part("Add r2", "r2.txt", holds=False),
        Part("Add r3", "r3.txt", holds=False),
    ]
    with gate.tree_lock(work):
        runs = started(work, root, log, parts)
    is_started = testing.wait_until(lambda: Path(f"{go}.started").exists(), WAIT_S)
    interrupted = runs[1].stop()
    go.write_text("go\n", encoding="utf-8")
    exits = [runs[0].wait().exit, runs[2].wait().exit]
    expect(
        "a landing that gets an interrupt while the gate runs drops out before the swap, and the others land after a "
        "second run of the gate",
        is_started
        and interrupted.exit != 0
        and exits == [0, 0]
        and log_lines(log) == ["Add r1", "Add r1"]
        and subjects_since(root, head) == ["Add r1", "Add r3"],
    )
    work, root, log = base / "work-earlier", base / "repo-earlier", base / "earlier.log"
    head = planted_repository(root)
    (root / "old.txt").write_text("a landing of an earlier copy\n", encoding="utf-8")
    (root / "r1.txt").write_text("1\n", encoding="utf-8")
    with gate.tree_lock(work):
        runs = started(work, root, log, [Part("Add r1", "r1.txt")])
        earlier = land.commit_tree(root, land.compose(root, request_of(("Old",), own=("old.txt",)), head), "Old\n")
        has_swapped = land.swap(root, land.branch_of(root), earlier, head, "Old")
    exit_code = runs[0].wait().exit
    expect(
        "a landing waits while a landing of an earlier copy holds the warm tree, and then lands on its commit",
        has_swapped and exit_code == 0 and subjects_since(root, head) == ["Old", "Add r1"],
    )


def revert_process_cases(expect: testing.Expect, base: Path) -> None:
    """Hold processes with a patch of a file and an own path with an earlier copy of it: in one batch and after a swap.

    The copy of the own path has one more line, and the patch does not apply to it.
    """
    work, root, log = base / "work-revert", base / "repo-revert", base / "revert.log"
    head = planted_repository(root)
    patch = base / "revert.patch"
    (root / "shared.txt").write_text("alpha\ndelta\n", encoding="utf-8")
    patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
    (root / "shared.txt").write_text("alpha\nbeta\ngamma\n", encoding="utf-8")
    parts = [Part("Change a line", "", patch=str(patch)), Part("Add a line", "shared.txt")]
    with gate.tree_lock(work):
        runs = started(work, root, log, parts)
    exits = [run.wait().exit for run in runs]
    expect(
        "in one batch of two processes, the own path with an earlier copy of the file of the patch is refused, and "
        "the branch keeps the patch",
        exits == [0, ExitCode.USAGE]
        and git.show(root, "HEAD", "shared.txt") == b"alpha\ndelta\n"
        and subjects_since(root, head) == ["Change a line"],
    )
    work, root, log = base / "work-after", base / "repo-after", base / "after.log"
    head = planted_repository(root)
    (root / "shared.txt").write_text("alpha\nbeta\ngamma\n", encoding="utf-8")
    go = base / "go-after"
    with gate.tree_lock(work):
        first = started(work, root, log, [Part("Change a line", "", go=str(go), patch=str(patch))])
    is_started = testing.wait_until(lambda: Path(f"{go}.started").exists(), WAIT_S)
    second = started(work, root, log, [Part("Add a line", "shared.txt")], before=1)
    go.write_text("go\n", encoding="utf-8")
    exits = [first[0].wait().exit, second[0].wait().exit]
    expect(
        "a process with a request from before the swap of a different process is refused in the next batch when its "
        "own path holds a file of that swap, and the branch keeps the patch",
        is_started
        and exits == [0, ExitCode.USAGE]
        and git.show(root, "HEAD", "shared.txt") == b"alpha\ndelta\n"
        and subjects_since(root, head) == ["Change a line"],
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            word_cases,
            edit_cases,
            compose_cases,
            batch_cases,
            red_base_cases,
            focus_cases,
            revert_cases,
            landing_cases,
            falls_cases,
            flaky_cases,
            drop_cases,
            queue_cases,
            error_cases,
            defect_cases,
            journal_cases,
            record_cases,
            heavy_lock_cases,
            process_cases,
        ),
    )
