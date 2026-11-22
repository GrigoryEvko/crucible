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
gate.  Two requests that change the summary row of the mint inventory must
compose and land with the inventory that the gate writes.  A batch that adds
a fixture and loses its swap to a lane that adds a different fixture must
compose again and land with the inventory of the two.  A refusal must give
the rows of the ratchet ledgers after its problems.  The verdict of a
landed request must name each flaky test of its run.
A red run that a green run of the same tree does not repeat must land its
request, with a line that names the problem as flaky.

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
import threading
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from unittest import mock

from assay.core import git, paths, proc, testing
from assay.core import registry as rule_registry
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
# The bound of each wait of the case with three batches in threads, in seconds.  The case ends long before it.
THREAD_BOUND_S = 120.0
# The text of the own file of a request whose commit the planted gate cannot read, and the error that it raises.
UNREADABLE = "a row of a kind that the code of the gate does not know\n"
TABLE_ERROR = "rule-table.txt:525: the row kind 'linear-door' is unknown"
# The ledger that the falls of a planted gate write.
FALLEN = b"format 1\nrule demo\na.cpp | kind | 1 | entity | -\n"
# The registry with the count rule demo, its ledger, and the head of that ledger, for the cases of the merge.
MERGE_REGISTRY = "format 1\nrule demo engine=meta scope=tracked ledger=count doc=XV | a planted refusal | fix it\n"
MERGE_LEDGER = "utils/ledgers/demo.txt"
MERGE_LEDGER_HEAD = "format 1\nrule demo\n"
# The generated file that the gate writes from the build of the composed tree, for the cases of the merge.
INVENTORY = "misc/mint-inventory.md"
# The planted entry times of two requests into the queue, in seconds since the epoch, and their ISO 8601 texts in UTC
# (the record of assay.loop.land).
FIRST_ENTRY_S = 1_800_000_000.0
SECOND_ENTRY_S = 1_800_000_061.0
FIRST_ENTRY = "2027-01-15T08:00:00Z"
SECOND_ENTRY = "2027-01-15T08:01:01Z"
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


def holder(held_work, queue, lane):
    after = part.get("holds_after")
    if not part.get("holds") or (after and not pathlib.Path(after).exists()):
        return False
    return land.hold(held_work, queue, check, lambda _seconds: None, lane)


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
            ("a budget reason with a bar", ["--budget-reason", "a | b", "-m", "s", "--patch", "p", "--"]),
            ("an empty budget reason", ["--budget-reason", " ", "-m", "s", "--patch", "p", "--"]),
            ("two budget reasons", ["--budget-reason", "a", "--budget-reason", "b", "-m", "s", "--patch", "p", "--"]),
            ("a budget reason with --dry-run", ["--budget-reason", "a", "--dry-run", "-m", "s", "--patch", "p", "--"]),
            ("a row text with a bar", ["--budget-reason", "include/a.h=a | b", "-m", "s", "--patch", "p", "--"]),
            ("an empty row text", ["--budget-reason", "include/a.h=", "-m", "s", "--patch", "p", "--"]),
            (
                "two texts of one row",
                ["--budget-reason", "a.o=x", "--budget-reason", "a.o=y", "-m", "s", "--patch", "p", "--"],
            ),
            ("a row text with --tree", ["--budget-reason", "a.o=x", "--tree", "-m", "s", "--patch", "p", "--"]),
            ("a lower with no reason", ["--lower", "step-cost", "a.o", "-m", "s", "--patch", "p", "--"]),
            ("a lower reason with no lower", ["--lower-reason", "a cut", "-m", "s", "--patch", "p", "--"]),
            (
                "a lower of a rule with no named rows",
                ["--lower", "instruction-total", "x", "--lower-reason", "a cut", "-m", "s", "--patch", "p", "--"],
            ),
            (
                "a lower of an item with a bar",
                ["--lower", "step-cost", "a | b", "--lower-reason", "a cut", "-m", "s", "--patch", "p", "--"],
            ),
            (
                "a lower with --tree",
                ["--lower", "step-cost", "a.o", "--lower-reason", "a cut", "--tree", "-m", "s", "--patch", "p", "--"],
            ),
        ):
            try:
                land.parse(words, scratch)
                expect(f"the parse refuses {name}", False)
            except land.LandError:
                expect(f"the parse refuses {name}", True)
        _request, reasoned = land.parse(
            ["--series", "wt/a", "--budget-reason", "the cause of the growth", "--"], scratch
        )
        expect(
            "--budget-reason gives the reason of the raise of a budget, also for a series",
            reasoned.budget_reason == "the cause of the growth" and reasoned.series == "wt/a",
        )
        row_words = ["--budget-reason", "include/a.h=the cause of a.h", "--budget-reason", "the cause of x=y"]
        _request, per_row = land.parse(
            ["--series", "wt/a", *row_words, "--budget-reason", "@build/b.o=the cause of b.o", "--"], scratch
        )
        _request, only_rows = land.parse(["--series", "wt/a", "--budget-reason", "a.o=the cause of a.o", "--"], scratch)
        expect(
            f"--budget-reason ITEM=TEXT gives the text of one row, in the sequence of the words, and a value with a "
            f"space before its '=' is the plain text: {per_row.budget_reason!r} {per_row.budget_rows}",
            per_row.budget_reason == "the cause of x=y"
            and per_row.budget_rows == (("include/a.h", "the cause of a.h"), ("@build/b.o", "the cause of b.o"))
            and only_rows.budget_reason == ""
            and only_rows.budget_rows == (("a.o", "the cause of a.o"),),
        )
        lower_words = ["--lower", "step-cost", "a.o", "--lower", "header-alone", "include/a.h", "--lower", "step-cost"]
        _request, lowering = land.parse(
            ["--series", "wt/a", *lower_words, "a.o", "--lower-reason", "the cut of a door", "--"], scratch
        )
        expect(
            "--lower gives each named row once, in the sequence of the words, and --lower-reason the reason of the cut",
            lowering.lowers == (("step-cost", "a.o"), ("header-alone", "include/a.h"))
            and lowering.lower_reason == "the cut of a door",
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
            f"with the run of the base and the runs of the confirmation, the search needs 5 runs for a red request at "
            f"the first position, 7 at the second and 6 at the last: {counts}",
            counts == {1: 5, 2: 7, 3: 6},
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
        f"when only an interaction of two requests is red, the second of the two is red again after the first and "
        f"green alone, so it gets the refusal of an interaction, and the others land, after {len(seen)} runs",
        decisions[2].status == ExitCode.FINDINGS
        and any("test_pair" in line.content for line in decisions[2].lines)
        and "an interaction" in decisions[2].lines[0].content
        and decisions[1].status == 0
        and decisions[3].status == 0
        and subjects_since(root, head) == ["Add r1", "Add r3"]
        and len(seen) == 7,
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


def finding_gate(root: Path, seen: list[tuple[gate.Candidate, bool]]) -> land.Check:
    """Return a planted gate that fails the test guard_plugin with one finding for each word of base.txt.

    Each red run gives the same problem, and its items name the file of each finding (the failing items of
    assay.loop.gate).  The gate keeps each call.
    """

    def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        seen.append((candidate, is_retry))
        shown = git.show(root, candidate.commit, "base.txt")
        files = shown.decode("utf-8").split() if shown is not None else []
        items = tuple(f"debug: guard_plugin: {name}:3 [must-consume]" for name in files)
        problems = ("debug: red alone too: guard_plugin",) if items else ()
        return gate.Verdict(("debug",), problems, 1.0, items=items)

    return check


def item_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the comparison of the failing items of a batch and of its base: one rule on a different file, and more files.

    The two cases call only the constructors and the method land() of the batch.
    """
    root = work / "repo-items-moved"
    planted_repository(root)
    (root / "base.txt").write_text("include/base.h\n", encoding="utf-8")
    red_base = git.commit_all(root, "Make the branch red")
    edit = land.Edit("base.txt", land.REPLACE, "include/base.h\n", "include/r1.h\n")
    seen: list[tuple[gate.Candidate, bool]] = []
    decisions, _ = bare_landing(root, [request_of(("Move the finding",), (edit,))], finding_gate(root, seen))
    lines = [line.content for line in decisions[1].lines] if 1 in decisions else []
    expect(
        f"a batch that fails the rule of the base on a different file is not red with no request: the search runs, "
        f"and the refusal gives the problem of the request with no line of a red branch, after 3 runs of the gate.  "
        f"The gate ran {len(seen)} times, and the refusal gives {lines}",
        len(seen) == 3
        and 1 in decisions
        and decisions[1].status == ExitCode.FINDINGS
        and "loop land: REJECTED, debug: red alone too: guard_plugin" in lines
        and not any("red with no request" in line for line in lines)
        and git.head(root) == red_base,
    )
    root = work / "repo-items-kept"
    planted_repository(root)
    (root / "base.txt").write_text("include/base.h\n", encoding="utf-8")
    red_base = git.commit_all(root, "Make the branch red")
    edit = land.Edit("base.txt", land.REPLACE, "include/base.h\n", "include/base.h\ninclude/r1.h\n")
    seen = []
    decisions, _ = bare_landing(root, [request_of(("Add a finding",), (edit,))], finding_gate(root, seen))
    lines = [line.content for line in decisions[1].lines] if 1 in decisions else []
    expect(
        f"a batch that keeps each failing item of the base is red with no request, and the refusal gives the other "
        f"item of the batch, after 2 runs of the gate.  The gate ran {len(seen)} times, and the refusal gives {lines}",
        len(seen) == 2
        and 1 in decisions
        and decisions[1].status == ExitCode.FINDINGS
        and any("red with no request" in line for line in lines)
        and "loop land: the batch also gives: debug: guard_plugin: include/r1.h:3 [must-consume]" in lines
        and git.head(root) == red_base,
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
            "request, and refuses only the red request with its own problem, after 7 runs of the gate",
            [decisions[number].status for number in (1, 2, 3)] == [0, ExitCode.FINDINGS, 0]
            and subjects_since(root, red_base) == ["Repair the branch", "Add r3"]
            and len(seen) == 7
            and not any("red with no request" in line.content for line in decisions[2].lines),
        )
        titles = [candidate.title for candidate, _ in seen]
        shown = [
            f"{candidate.title.split(' at ')[0]} at {candidate.commit[: land.SHORT_HASH]}" for candidate, _ in seen
        ]
        expect(
            f"the title of each run names its kind and its commit: the run of the batch, the run of the base, and the "
            f"run alone of the red request: {titles}",
            titles == shown
            and titles[1] == f"the run of the base at {red_base[: land.SHORT_HASH]}"
            and [title.split(" at ")[0] for title in titles].count("the run alone of 00000002") == 1
            and all(
                title.startswith(("the run of the batch at ", "the run alone of 00000002 at "))
                for index, title in enumerate(titles)
                if index != 1
            ),
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
            "a lone red request on a green branch gets the run of the base and the run alone, and its refusal gives "
            "its own problem",
            len(seen) == 3
            and seen[1][1]
            and seen[2][1]
            and seen[2][0].changed == ("r1.txt",)
            and decisions[1].status == ExitCode.FINDINGS
            and any("test_r1.txt" in line.content for line in decisions[1].lines)
            and not any("red with no request" in line.content for line in decisions[1].lines),
        )
        item_cases(expect, work)
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
        "a verdict with no items gets each problem as its item, and the items of two runs compare equal with the mark "
        "as the replacement of the directory of the logs of each run",
        first.items == first.problems
        and land.shown_items(first) == land.shown_items(second)
        and land.shown_items(first) != frozenset(first.problems),
    )


def focus_cases(expect: testing.Expect) -> None:
    """Hold the focus of the run of the base: it is the focus of the first run of the batch (the run of the base)."""
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
            "a commit that the gate rejects does not land, and its verdict gives the problem of its run alone",
            decisions[1].status == ExitCode.FINDINGS
            and git.head(root) == before
            and any("REJECTED" in line.content for line in decisions[1].lines)
            and records[0].results == (land.REJECTED,)
            and records[-1].results == (land.REFUSED,),
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
    patch = work / "read-only.patch"
    (root / "shared.txt").write_text("alpha\nepsilon\n", encoding="utf-8")
    patch.write_bytes(git.patch(root, ["HEAD"], ["shared.txt"]))
    (root / "shared.txt").write_text((git.show(root, "HEAD", "shared.txt") or b"").decode(), encoding="utf-8")
    request = request_of(("Change a line of a file that the update cannot write",), patches=(patch,))
    decisions: dict[int, land.Decision] = {}
    queued = land.Queued(1, root, request, options())

    def read_only() -> None:
        """Make the working-tree copy of the file of the patch read-only, so the update cannot write it."""
        (root / "shared.txt").chmod(0o444)

    batched = land.BatchLanding(
        [queued], passing([], read_only), lambda item, decision: decisions.update({item.number: decision}), no_pause
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


def ledger_text(counts: Sequence[int]) -> str:
    """Return the planted ledger of the rule demo with the rows a.cpp, b.cpp and c.cpp and their counts."""
    rows = "".join(f"{name}.cpp | kind | {count} | - | -\n" for name, count in zip("abc", counts, strict=True))
    return f"{MERGE_LEDGER_HEAD}{rows}"


def numbered(changed: Mapping[int, str]) -> str:
    """Return ten numbered lines, with the text of CHANGED at the lines that it names."""
    return "".join(f"{changed.get(index, f'line {index}')}\n" for index in range(10))


def patch_of(root: Path, work: Path, name: str, files: Mapping[str, str]) -> Path:
    """Write the files, keep their diff against HEAD as the patch NAME, give the files back their copy of HEAD."""
    for path, text in files.items():
        (root / path).write_text(text, encoding="utf-8")
    patch = work / name
    patch.write_bytes(git.patch(root, ["HEAD"], list(files)))
    for path in files:
        (root / path).write_bytes(git.show(root, "HEAD", path) or b"")
    return patch


def merge_cases(expect: testing.Expect) -> None:
    """Hold the merge: a patch that does not apply as text composes by a three-way merge, or is refused on a conflict.

    The cases also hold the index and the working tree after a fold of the gate next to the hunk of a patch.
    """
    with tempfile.TemporaryDirectory(prefix="land-merge-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        (root / "utils/registry.txt").write_text(MERGE_REGISTRY, encoding="utf-8")
        (root / MERGE_LEDGER).write_text(ledger_text((4, 2, 1)), encoding="utf-8")
        (root / "lines.txt").write_text(numbered({}), encoding="utf-8")
        git.commit_all(root, "Add the registry, the ledger and the lines")
        patch = patch_of(
            root, work, "raise.patch", {MERGE_LEDGER: ledger_text((7, 2, 1)), "lines.txt": numbered({2: "theirs"})}
        )
        conflict = patch_of(root, work, "conflict.patch", {"lines.txt": numbered({8: "a different line"})})
        (root / MERGE_LEDGER).write_text(ledger_text((5, 2, 1)), encoding="utf-8")
        (root / "lines.txt").write_text(numbered({8: "ours"}), encoding="utf-8")
        moved = git.commit_all(root, "Raise a count and change a line on main")
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, _ = landed(root, [request_of(("Raise a count with a patch",), patches=(patch,))], passing(seen))
        lines = [line.content for line in decisions[1].lines]
        expect(
            "a patch that does not apply as text composes by a three-way merge: the count is ours plus theirs less the "
            "base, the two changes of the lines merge, the gate runs each configuration, and the verdict names the "
            f"merged files: {lines}",
            decisions[1].status == 0
            and git.parent(root, "HEAD") == moved
            and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((8, 2, 1)).encode()
            and git.show(root, "HEAD", "lines.txt") == numbered({2: "theirs", 8: "ours"}).encode()
            and [candidate.is_every for candidate, _retry in seen] == [True]
            and any("three-way merge composed lines.txt utils/ledgers/demo.txt" in line for line in lines)
            and not any("WARNING" in line for line in lines),
        )
        expect(
            "after the landing, the index and the working tree hold the merged commit",
            git.is_index_clean(root, "HEAD")
            and (root / MERGE_LEDGER).read_text(encoding="utf-8") == ledger_text((8, 2, 1))
            and (root / "lines.txt").read_text(encoding="utf-8") == numbered({2: "theirs", 8: "ours"}),
        )
        head = git.head(root)
        decisions, _ = landed(root, [request_of(("Change the line of main",), patches=(conflict,))], passing([]))
        refusal = " ".join(line.content for line in decisions[1].lines)
        expect(
            f"a patch whose merge conflicts gets the refusal of the composition, which names the file: {refusal}",
            decisions[1].status == ExitCode.USAGE
            and "lines.txt: the two sides change the same lines" in refusal
            and git.head(root) == head,
        )
        fall_cases(expect, work)
        row_merge_cases(expect, work)


def max_ledger_text(rows: Mapping[str, tuple[str, str]]) -> str:
    """Return the planted ledger of the max rule demo-max: the value and the reason of each object, sorted."""
    entries = "".join(f"{name} | object | {value} | - | {why}\n" for name, (value, why) in sorted(rows.items()))
    return f"format 1\nrule demo-max\n{entries}"


def row_merge_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the merge of the rows of a ledger: changes of adjacent keys compose, and two raises of one max row compose.

    Each patch changes a row on a line next to a line that main changed, so `git apply` refuses its hunk, and the
    composition merges the ledger by its keys.  A row of a max ledger that main raises and a patch lowers refuses the
    patch.
    """
    root = work / "repo-rows"
    planted_repository(root)
    registry = rule_registry.planted(
        [MERGE_REGISTRY.splitlines()[1], rule_registry.measured_rule("demo-max", 64, "KB")]
    )
    (root / "utils/registry.txt").write_text(registry, encoding="utf-8")
    max_path = "utils/ledgers/demo-max.txt"
    base_rows = {"a.o": ("100", "base a"), "b.o": ("200", "base b"), "c.o": ("300", "base c")}
    (root / MERGE_LEDGER).write_text(ledger_text((4, 2, 1)), encoding="utf-8")
    (root / max_path).write_text(max_ledger_text(base_rows), encoding="utf-8")
    git.commit_all(root, "Add the registry and the two ledgers")
    adjacent = patch_of(root, work, "adjacent.patch", {MERGE_LEDGER: ledger_text((4, 3, 1))})
    raised_rows = {**base_rows, "a.o": ("110", "theirs a"), "c.o": ("310", "theirs c")}
    raised = patch_of(root, work, "raised.patch", {max_path: max_ledger_text(raised_rows)})
    raised_more = patch_of(
        root, work, "raised-more.patch", {max_path: max_ledger_text({**base_rows, "b.o": ("260", "theirs b")})}
    )
    (root / MERGE_LEDGER).write_text(ledger_text((5, 2, 1)), encoding="utf-8")
    main_rows = {**base_rows, "a.o": ("120", "ours a"), "b.o": ("230", "ours b")}
    (root / max_path).write_text(max_ledger_text(main_rows), encoding="utf-8")
    git.commit_all(root, "Raise a count and two rows of the max ledger on main")
    requests = [
        request_of(("Raise the adjacent count",), patches=(adjacent,)),
        request_of(("Raise a row of the max ledger less than main, and a different row",), patches=(raised,)),
        request_of(("Raise a row of the max ledger more than main",), patches=(raised_more,)),
    ]
    decisions, _ = landed(root, requests, passing([]))
    landed_rows = {"a.o": ("120", "ours a"), "b.o": ("260", "theirs b"), "c.o": ("310", "theirs c")}
    statuses = [decision.status for decision in decisions.values()]
    expect(
        "changes of adjacent keys of a count ledger compose by the keys, and two raises of one row of a max ledger "
        f"compose to the larger value with the reason of its side: {statuses}",
        statuses == [0, 0, 0]
        and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((5, 3, 1)).encode()
        and git.show(root, "HEAD", max_path) == max_ledger_text(landed_rows).encode(),
    )
    lowered = patch_of(
        root, work, "lowered.patch", {max_path: max_ledger_text({**landed_rows, "a.o": ("90", "a fall")})}
    )
    (root / max_path).write_text(max_ledger_text({**landed_rows, "a.o": ("130", "ours a again")}), encoding="utf-8")
    git.commit_all(root, "Raise a row of the max ledger on main again")
    head = git.head(root)
    decisions, _ = landed(root, [request_of(("Lower a row of the max ledger",), patches=(lowered,))], passing([]))
    refusal = " ".join(line.content for line in decisions[1].lines)
    expect(
        f"a row of a max ledger that main raises and a patch lowers refuses the patch: {refusal}",
        decisions[1].status == ExitCode.USAGE and "below its value" in refusal and git.head(root) == head,
    )


def count_sum_cases(expect: testing.Expect) -> None:
    """Hold the sum of the count rows of two requests: each request adds its change of a count to the base.

    Two series each raise one row from 1 to 2 in a commit of the ledger only, as two agents that add one region each do.
    The composed tree needs 3.  A raise and a fall compose to the base, a conflict outside the ledgers still refuses,
    a patch that main holds whole changes nothing, and a series with a landed commit is refused before the queue.
    """
    with tempfile.TemporaryDirectory(prefix="land-count-sum-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        scratch = work / "patches"
        scratch.mkdir()
        planted_repository(root)
        (root / "utils/registry.txt").write_text(MERGE_REGISTRY, encoding="utf-8")
        (root / MERGE_LEDGER).write_text(ledger_text((1, 2, 1)), encoding="utf-8")
        (root / "lines.txt").write_text(numbered({}), encoding="utf-8")
        base = git.commit_all(root, "Add the registry, the ledger and the lines")
        raised = {MERGE_LEDGER: ledger_text((2, 2, 1))}
        requests: list[land.Request] = []
        for name, own in (("first", "r1.txt"), ("second", "r2.txt")):
            planted_branch(root, name, (({own: f"{name}\n"}, f"Add {own}"), (raised, f"Raise a for {own}")))
            (scratch / name).mkdir()
            requests.append(land.series_request(root, name, scratch / name))
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, _ = batch_landing(root, requests, passing(seen))
        lines = [line.content for line in decisions[2].lines]
        expect(
            "two series that each raise one row from 1 to 2 compose to 3, each with its own commits, and the gate runs "
            f"each configuration for the sum: {lines}",
            [decisions[number].status for number in (1, 2)] == [0, 0]
            and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((3, 2, 1)).encode()
            and subjects_since(root, base) == ["Add r1.txt", "Raise a for r1.txt", "Add r2.txt", "Raise a for r2.txt"]
            and seen[-1][0].is_every
            and any("with a sum of two changes of a count" in line for line in lines),
        )
        moved = git.head(root)
        raise_patch = patch_of(root, work, "raise.patch", {MERGE_LEDGER: ledger_text((4, 2, 1))})
        fall_patch = patch_of(root, work, "fall.patch", {MERGE_LEDGER: ledger_text((2, 2, 1))})
        decisions, _ = landed(
            root,
            [request_of(("Raise a again",), patches=(raise_patch,)), request_of(("Lower a",), patches=(fall_patch,))],
            passing([]),
        )
        expect(
            "a raise and a fall of one row compose to the base",
            [decisions[number].status for number in (1, 2)] == [0, 0]
            and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((3, 2, 1)).encode()
            and subjects_since(root, moved) == ["Raise a again", "Lower a"],
        )
        moved = git.head(root)
        both = {MERGE_LEDGER: ledger_text((4, 2, 1)), "lines.txt": numbered({3: "first"})}
        first = patch_of(root, work, "first.patch", both)
        clash = patch_of(root, work, "clash.patch", {**both, "lines.txt": numbered({3: "second"})})
        decisions, _ = landed(
            root,
            [request_of(("Raise a and change a line",), patches=(first,)), request_of(("Clash",), patches=(clash,))],
            passing([]),
        )
        refusal = " ".join(line.content for line in decisions[2].lines)
        expect(
            f"a request whose ledger rows sum and whose other file conflicts is refused for that file: {refusal}",
            decisions[1].status == 0
            and decisions[2].status == ExitCode.USAGE
            and "lines.txt: the two sides change the same lines" in refusal
            and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((4, 2, 1)).encode()
            and subjects_since(root, moved) == ["Raise a and change a line"],
        )
        decisions, _ = landed(root, [request_of(("Land the first patch again",), patches=(first,))], passing([]))
        expect(
            "a patch of a request that main holds whole landed already, so it changes nothing and is refused",
            decisions[1].status == ExitCode.USAGE
            and "the commit would change nothing" in " ".join(line.content for line in decisions[1].lines)
            and git.show(root, "HEAD", MERGE_LEDGER) == ledger_text((4, 2, 1)).encode(),
        )
        try:
            land.series_request(root, "first", scratch / "first")
            said = ""
        except land.LandError as problem:
            said = str(problem)
        expect(
            f"a series with a commit whose subject a commit of HEAD after the merge base holds is refused: {said}",
            "'Add r1.txt' of the series has the subject of a commit of HEAD" in said and "git rebase main" in said,
        )


def registry_row(kinds: str, text: str) -> str:
    """Return a planted registry with one rule row of the kinds and the text, after a path row."""
    rule = f"rule demo engine=meta scope=tracked ledger=count kinds={kinds} doc=XV | {text} | fix it"
    return f"format 1\npath utils/a.py | a planted path\n{rule}\n"


def registry_merge_cases(expect: testing.Expect) -> None:
    """Hold the merge of the registry: two requests that each add a kind and change the text of one row compose.

    Two changes of one word of the text still conflict.  On the old code the line merge refused the second request.
    """
    with tempfile.TemporaryDirectory(prefix="land-registry-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        registry = "utils/registry.txt"
        (root / registry).write_text(registry_row("a,b,c", "a text of the rule"), encoding="utf-8")
        git.commit_all(root, "Add the registry")
        added = patch_of(root, work, "added.patch", {registry: registry_row("a,x,b,c", "a full text of the rule")})
        clash = patch_of(root, work, "clash.patch", {registry: registry_row("a,b,c", "a short text of the rule")})
        (root / registry).write_text(
            registry_row("a,b,c,d", "a text of the rule.  A second sentence"), encoding="utf-8"
        )
        git.commit_all(root, "Add a kind and a sentence on main")
        decisions, _ = landed(root, [request_of(("Add a kind on the branch",), patches=(added,))], passing([]))
        wanted = registry_row("a,x,b,c,d", "a full text of the rule.  A second sentence")
        expect(
            "two changes of one registry row compose: the kinds keep the add of each side, and the text keeps the "
            "words of each side",
            decisions[1].status == 0 and git.show(root, "HEAD", registry) == wanted.encode(),
        )
        decisions, _ = landed(root, [request_of(("Change the same word",), patches=(clash,))], passing([]))
        refusal = " ".join(line.content for line in decisions[1].lines)
        expect(
            f"two changes of one word of the text of a row conflict: {refusal[:200]}",
            decisions[1].status == ExitCode.USAGE and "the same words of a text of the registry row" in refusal,
        )


def budget_reason_cases(expect: testing.Expect) -> None:
    """Hold the budget reason: the request file holds it, a chain gets it only from each request, and a verdict names
    each raised row."""
    with tempfile.TemporaryDirectory(prefix="land-budget-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        queue = work / "queue"
        queue.mkdir()
        first, second = (land.Options(False, False, jobs=1, budget_reason=text) for text in ("a cause", "b cause"))
        own_row = (("include/a.h", "the a.h cause"),)
        with_row = replace(first, budget_rows=own_row)
        queued = land.Queued(7, root, request_of(("Add r1",), own=("r1.txt",)), with_row, token="planted")
        decoded = land.decoded_request(queue, 7, land.encoded_request(queued, []))
        expect(
            "the request file holds the budget reason and the text of each named row",
            decoded.options.budget_reason == "a cause" and decoded.options.budget_rows == own_row,
        )
        line = "utils/ledgers/header-alone.txt: include/a.h from 3.1 to 3.3"
        seen: list[gate.Candidate] = []

        def raising(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Accept each commit with one raised row, as a raise of a budget gives."""
            seen.append(candidate)
            return gate.Verdict(("debug",), (), 1.0, raised=(line,))

        def reasons_of(chosen: Sequence[land.Options], label: str) -> tuple[gate.Candidate | None, list[str]]:
            """Land one request with each option set, and give the candidate and the lines of request 1.

            The label goes into each own file, so each request of each call changes its file.  The candidate is None
            when a request did not land.
            """
            seen.clear()
            requests = own_requests(root, [f"{label} {index}\n" for index in range(len(chosen))])
            batch = [land.Queued(index + 1, root, item, chosen[index]) for index, item in enumerate(requests)]
            decisions: dict[int, land.Decision] = {}
            landing = land.BatchLanding(
                batch, raising, lambda item, made: decisions.__setitem__(item.number, made), no_pause
            )
            landing.land(batch)
            numbers = [number for number, made in sorted(decisions.items()) if made.status == 0]
            candidate = seen[0] if numbers == [1, 2] else None
            return candidate, [item.content for item in decisions[1].lines]

        candidate, lines = reasons_of([first, second], "reasoned")
        reason = candidate.budget_reason if candidate is not None else "both did not land"
        expect(
            f"a chain of requests that each give a reason gets the reason of each, and the verdict names the raised "
            f"row: {reason!r} {lines}",
            reason == "a cause.  b cause"
            and f"loop land: the gate raised the row {line} with the budget reason" in lines,
        )
        candidate, _lines = reasons_of([first, land.Options(False, False, jobs=1)], "mixed")
        expect(
            "a chain with a request of no reason gets no reason, so it gets no raise",
            candidate is not None and candidate.budget_reason == "",
        )
        rows_only = land.Options(False, False, jobs=1, budget_rows=own_row)
        chains = {
            "own and plain": [with_row, second],
            "rows only and plain": [rows_only, second],
            "rows only and none": [rows_only, land.Options(False, False, jobs=1)],
        }
        found: dict[str, tuple[str, tuple[tuple[str, str], ...]] | None] = {}
        for name, chosen in chains.items():
            candidate, _lines = reasons_of(chosen, name.replace(" ", "-"))
            found[name] = (candidate.budget_reason, candidate.budget_rows) if candidate is not None else None
        expect(
            f"the text of a named row in a chain holds the own text or the plain text of each request, and a chain "
            f"with a request that gives no text for the row gets no text for it: {found}",
            found
            == {
                "own and plain": ("a cause.  b cause", (("include/a.h", "the a.h cause.  b cause"),)),
                "rows only and plain": ("", (("include/a.h", "the a.h cause.  b cause"),)),
                "rows only and none": ("", ()),
            },
        )


def lower_cases(expect: testing.Expect) -> None:
    """Hold the lower: the request file holds the rows and the reason, each row of a chain takes the reason of its own
    request, and a verdict names each lowered row."""
    with tempfile.TemporaryDirectory(prefix="land-lower-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        queue = work / "queue"
        queue.mkdir()
        first = land.Options(False, False, jobs=1, lowers=(("step-cost", "a.o"),), lower_reason="a cut")
        second = land.Options(
            False, False, jobs=1, lowers=(("step-cost", "a.o"), ("object-size", "b.o")), lower_reason="b cut"
        )
        queued = land.Queued(7, root, request_of(("Add r1",), own=("r1.txt",)), second, token="planted")
        decoded = land.decoded_request(queue, 7, land.encoded_request(queued, []))
        expect(
            "the request file holds the rows of a lower and the reason of the cut",
            decoded.options.lowers == second.lowers and decoded.options.lower_reason == "b cut",
        )
        line = "utils/ledgers/step-cost.txt: @build/a.o compile from 123.912 to 111.001"
        seen: list[gate.Candidate] = []

        def lowering(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Accept each commit with one lowered row, as a lower of a budget gives."""
            seen.append(candidate)
            return gate.Verdict(("debug",), (), 1.0, lowered=(line,))

        requests = own_requests(root, ["lowered 0\n", "lowered 1\n"])
        batch = [land.Queued(index + 1, root, item, (first, second)[index]) for index, item in enumerate(requests)]
        decisions: dict[int, land.Decision] = {}
        landing = land.BatchLanding(
            batch, lowering, lambda item, made: decisions.__setitem__(item.number, made), no_pause
        )
        landing.land(batch)
        lines = [item.content for item in decisions[1].lines]
        expect(
            f"each row of a chain takes the reason of the first request that names it, and the verdict names the "
            f"lowered row: {seen[0].lowers if seen else ()} {lines}",
            bool(seen)
            and seen[0].lowers == (("step-cost", "a.o", "a cut"), ("object-size", "b.o", "b cut"))
            and f"loop land: the lower of a budget: {line}" in lines,
        )


def fall_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the update of a work tree after a fold of the gate next to the hunk of a patch.

    The update of the holder and the update of the process each give the working tree the commit, with no warning.
    """
    root = work / "repo-fall"
    planted_repository(root)
    (root / "utils/registry.txt").write_text(MERGE_REGISTRY, encoding="utf-8")
    (root / MERGE_LEDGER).write_text(ledger_text((4, 2, 1)), encoding="utf-8")
    git.commit_all(root, "Add the registry and the ledger")
    patch = patch_of(root, work, "row.patch", {MERGE_LEDGER: ledger_text((3, 2, 1))})

    def folding(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        """Fold a fall of the row next to the hunk of the patch into the commit, as the reduce does."""
        final = git.replaced_commit(root, candidate.commit, {MERGE_LEDGER: ledger_text((3, 1, 1)).encode()})
        return gate.Verdict(("debug",), (), 1.0, (), final, (MERGE_LEDGER,))

    decisions, _ = landed(root, [request_of(("Lower a count with a patch",), patches=(patch,))], folding)
    lines = [line.content for line in decisions[1].lines]
    expect(
        f"after a fold next to the hunk of a patch, the updates of the holder and of the process give no warning, "
        f"and the working tree holds the commit of the fold: {[line for line in lines if 'WARNING' in line]}",
        decisions[1].status == 0
        and not any("WARNING" in line for line in lines)
        and git.is_index_clean(root, "HEAD")
        and (root / MERGE_LEDGER).read_text(encoding="utf-8") == ledger_text((3, 1, 1)),
    )


def inventory_text(counts: Mapping[str, int]) -> str:
    """Return a planted mint inventory in the shape of the real file: a row for each mint, and a summary row.

    The summary row counts the mints and the mints with fewer than two fixtures.  A change of one row then changes the
    summary row too, as in misc/mint-inventory.md.
    """
    rows = "".join(f"| `{name}` | HS14: {count} |\n" for name, count in sorted(counts.items()))
    under = sum(1 for count in counts.values() if count < 2)
    summary = f"| mints | under the floor |\n|---|---|\n| {len(counts)} | {under} |\n"
    return f"# Mint inventory\n\n| mint | HS14 |\n|---|---|\n{rows}\n## Summary\n\n{summary}"


def built_cases(expect: testing.Expect) -> None:
    """Hold the merge of a file of BUILT_FILES of assay.loop.gate: the gate writes it, and the batch lands.

    Two requests of one batch each change a file and the summary row of the mint inventory, as two families that add
    fixtures at one time do.  The second patch does not apply as text after the first.  Its merge keeps the copy of the
    composition, and the gate writes the inventory from the build of the composed tree.
    """
    with tempfile.TemporaryDirectory(prefix="land-built-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        (root / INVENTORY).parent.mkdir(parents=True, exist_ok=True)
        (root / INVENTORY).write_text(inventory_text({"mint_a": 1, "mint_b": 2}), encoding="utf-8")
        (root / "mints.txt").write_text("mint_a\nmint_b\n", encoding="utf-8")
        (root / "fixtures.txt").write_text("mint_a\nmint_b\nmint_b\n", encoding="utf-8")
        head = git.commit_all(root, "Add the mints, the fixtures and the inventory")
        first_inventory = inventory_text({"mint_a": 1, "mint_b": 2, "mint_c": 2})
        first_files = {"mints.txt": "mint_a\nmint_b\nmint_c\n", INVENTORY: first_inventory}
        first = patch_of(root, work, "mint.patch", first_files)
        second_inventory = inventory_text({"mint_a": 2, "mint_b": 2})
        second_files = {"fixtures.txt": "mint_a\nmint_a\nmint_b\nmint_b\n", INVENTORY: second_inventory}
        second = patch_of(root, work, "fixture.patch", second_files)
        requests = [
            request_of(("Add mint_c with two fixtures",), patches=(first,)),
            request_of(("Add a fixture of mint_a",), patches=(second,)),
        ]
        after_first = land.commit_tree(root, land.compose(root, requests[0], head), requests[0].message)
        try:
            composed = land.compose(root, requests[1], after_first)
            refusal = ""
        except land.LandError as problem:
            composed, refusal = None, str(problem)
        expect(
            f"a patch that changes the summary row of the inventory after a different change of it composes, keeps the "
            f"copy of the composition, and names the inventory as a file that the gate writes: {refusal or 'composed'}",
            composed is not None
            and composed.built == (INVENTORY,)
            and INVENTORY not in composed.changed
            and INVENTORY in gate.BUILT_FILES,
        )
        written = inventory_text({"mint_a": 2, "mint_b": 2, "mint_c": 2})
        seen: list[gate.Candidate] = []

        def writing(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Write the inventory of the composed tree when the candidate names it, as the built files of the gate does."""
            seen.append(candidate)
            if INVENTORY not in candidate.built:
                return gate.Verdict(("debug",), (), 1.0)
            final = git.replaced_commit(root, candidate.commit, {INVENTORY: written.encode()})
            return gate.Verdict(("debug",), (), 1.0, (), final, built=(INVENTORY,))

        decisions, _ = landed(root, requests, writing)
        lines = {number: [line.content for line in decision.lines] for number, decision in decisions.items()}
        expect(
            f"the batch of the two requests gives the gate one candidate that names the inventory as a changed file "
            f"and as a file that the gate writes, and the two land: {lines}",
            [decisions[number].status for number in (1, 2)] == [0, 0]
            and [candidate.built for candidate in seen] == [(INVENTORY,)]
            and INVENTORY in seen[0].changed
            and subjects_since(root, head) == ["Add mint_c with two fixtures", "Add a fixture of mint_a"],
        )
        expect(
            "the branch holds the inventory that the gate wrote and the files of the two requests, and the commit of "
            "the first request keeps its own inventory",
            git.show(root, "HEAD", INVENTORY) == written.encode()
            and git.show(root, "HEAD", "mints.txt") == b"mint_a\nmint_b\nmint_c\n"
            and git.show(root, "HEAD", "fixtures.txt") == b"mint_a\nmint_a\nmint_b\nmint_b\n"
            and git.show(root, "HEAD~1", INVENTORY) == first_inventory.encode(),
        )
        expect(
            "the verdict of the second request tells that the gate writes the inventory, and that the gate wrote it "
            "into the landed commit",
            any(f"the gate writes {INVENTORY} from the build of the composed tree" in line for line in lines[2])
            and any(f"with {INVENTORY} that the gate wrote" in line for line in lines[2])
            and not any("WARNING" in line for line in [*lines[1], *lines[2]]),
        )
        expect(
            "after the update, the index and the working tree hold the inventory that the gate wrote",
            git.is_index_clean(root, "HEAD") and (root / INVENTORY).read_text(encoding="utf-8") == written,
        )


def fixture_inventory(root: Path, commit: str) -> str:
    """Return the planted inventory of the fixtures of a commit, as the build of a gate gives it.

    Each file of the directory fixtures/ names one mint on each line, and the count of a mint is the count of its lines.
    """
    names = git.run(root, "ls-tree", "--name-only", commit, "fixtures/").decode().split()
    counts: dict[str, int] = {}
    for name in names:
        for mint in (git.show(root, commit, name) or b"").decode().split():
            counts[mint] = counts.get(mint, 0) + 1
    return inventory_text(counts)


def writing_gate(root: Path, seen: list[gate.Candidate], before: Callable[[], None]) -> land.Check:
    """Return a planted gate that writes the inventory of the fixtures of each candidate, as the built files does.

    The gate calls `before` at the start of each run.  It folds the inventory only when the bytes change.
    """

    def check(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
        seen.append(candidate)
        before()
        written = fixture_inventory(root, candidate.commit)
        if git.show(root, candidate.commit, INVENTORY) == written.encode():
            return gate.Verdict(("debug",), (), 1.0, (), candidate.commit)
        final = git.replaced_commit(root, candidate.commit, {INVENTORY: written.encode()})
        return gate.Verdict(("debug",), (), 1.0, (), final, built=(INVENTORY,))

    return check


def built_race_cases(expect: testing.Expect) -> None:
    """Hold two batches that each add a fixture, with a lost compare-and-swap between them (the built files).

    The request of the batch adds a fixture of mint_a and the copy of the inventory of its own build.  While its gate
    runs, a different lane lands a fixture of mint_b with the inventory that its gate wrote.  The summary rows of the
    two inventories differ.
    """
    with tempfile.TemporaryDirectory(prefix="land-built-race-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        (root / "fixtures").mkdir()
        (root / "fixtures" / "first.txt").write_text("mint_a\nmint_b\nmint_b\n", encoding="utf-8")
        (root / INVENTORY).parent.mkdir(parents=True, exist_ok=True)
        (root / INVENTORY).write_text(inventory_text({"mint_a": 1, "mint_b": 2}), encoding="utf-8")
        git.commit_all(root, "Add the fixtures and the inventory")
        own_inventory = inventory_text({"mint_a": 2, "mint_b": 2})
        fixtures = "mint_a\nmint_a\nmint_b\nmint_b\n"
        patch = patch_of(root, work, "fixture.patch", {"fixtures/first.txt": fixtures, INVENTORY: own_inventory})
        request = request_of(("Add a fixture of mint_a",), patches=(patch,))
        moved: list[str] = []

        def other_lane() -> None:
            """Land a fixture of mint_b with its written inventory on the first run, as a different lane does."""
            if moved:
                return
            (root / "fixtures" / "b.txt").write_text("mint_b\n", encoding="utf-8")
            (root / INVENTORY).write_text(inventory_text({"mint_a": 1, "mint_b": 3}), encoding="utf-8")
            tip = git.head(root)
            composed = land.compose(root, request_of(("Add b",), own=("fixtures/b.txt", INVENTORY)), tip)
            land.swap(root, land.branch_of(root), land.commit_tree(root, composed, "Add b\n"), tip, "Add b")
            moved.append(git.head(root))

        seen: list[gate.Candidate] = []
        decisions, _ = landed(root, [request], writing_gate(root, seen, other_lane))
        lines = [line.content for line in decisions[1].lines]
        expect(
            f"a batch that adds a fixture loses its swap to a lane that adds a different fixture, composes again "
            f"through THE MERGE of the inventory, and lands with the inventory of the two fixtures: {lines}",
            decisions[1].status == 0
            and len(seen) == 2
            and seen[1].head == moved[0]
            and seen[1].built == (INVENTORY,)
            and git.parent(root, "HEAD") == moved[0]
            and git.show(root, "HEAD", INVENTORY) == inventory_text({"mint_a": 2, "mint_b": 3}).encode()
            and git.show(root, "HEAD", "fixtures/first.txt") == fixtures.encode(),
        )


def ratchet_rows_cases(expect: testing.Expect) -> None:
    """Hold the ratchet rows of assay.loop.gate in a refusal: the lines of the rows come after the problems."""
    with tempfile.TemporaryDirectory(prefix="land-rows-") as work_text:
        root = Path(work_text) / "repo"
        planted_repository(root)
        requests = own_requests(root, ("a use of a door\n",))
        rows = (
            "loop land: debug: the build gives these rows of utils/ledgers/owners.txt:",
            "  src/a.cpp | door-use | 1 | a: b | REASON",
        )
        problem = "debug: red alone too: guard_plugin, see /work/logs/debug.test.log"

        def check(candidate: gate.Candidate, _is_retry: bool) -> gate.Verdict:
            """Accept the run of the base, and refuse each other run with a red test of the plugin rules and rows."""
            if candidate.commit == candidate.head:
                return gate.Verdict(("debug",), (), 1.0)
            return gate.Verdict(("debug",), (problem,), 1.0, focus=(gate.Focus("debug", ("guard_plugin",)),), rows=rows)

        decisions, _ = landed(root, requests, check)
        lines = [line.content for line in decisions[1].lines]
        expect(
            f"a refusal gives the rows of the ratchet ledgers of its run after its problems: {lines}",
            decisions[1].status == ExitCode.FINDINGS and lines == [f"loop land: REJECTED, {problem}", *rows],
        )


def flaky_once(root: Path, name: str, seen: list[tuple[gate.Candidate, bool]]) -> land.Check:
    """Return a planted gate that rejects the first run of a commit that holds a file, as a flaky test does.

    The gate accepts each other run, and it keeps each call.
    """
    reds: list[str] = []

    def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        seen.append((candidate, is_retry))
        logs = f"/work/logs/run-{len(seen):06d}"
        if git.show(root, candidate.commit, name) is not None and not reds:
            reds.append(candidate.commit)
            return gate.Verdict(
                ("debug",), (f"debug: red alone too: test_flaky, see {logs}/debug.test.log",), 1.0, logs=logs
            )
        return gate.Verdict(("debug",), (), 1.0, logs=logs)

    return check


def confirmation_cases(expect: testing.Expect) -> None:
    """Hold the confirmation: a red run that a green run of the same tree does not repeat lands the request."""
    with tempfile.TemporaryDirectory(prefix="land-confirmation-") as work_text:
        root = Path(work_text) / "repo-lone"
        head = planted_repository(root)
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, records = landed(root, own_requests(root, ("one\n",)), flaky_once(root, "r1.txt", seen))
        noted = [line.content for line in decisions[1].lines if line.content.startswith(land.FLAKY_MARK)]
        expect(
            f"a lone request whose red run does not repeat in its run alone lands after the run of the base and the "
            f"run alone, and its verdict names the problem that did not repeat as flaky: {len(seen)} runs, {noted}",
            decisions[1].status == 0
            and subjects_since(root, head) == ["Add r1"]
            and len(seen) == 3
            and len(noted) == 1
            and "test_flaky" in noted[0]
            and records[-1].results == (land.LANDED,),
        )
        root = Path(work_text) / "repo-batch"
        head = planted_repository(root)
        seen = []
        texts = ("one\n", "two\n", "three\n")
        decisions, _ = landed(root, own_requests(root, texts), flaky_once(root, "r2.txt", seen))
        statuses = [decisions[number].status for number in (1, 2, 3)]
        expect(
            f"a batch whose first run is red only by a flaky test lands each request, and the request that the search "
            f"judged gets the line of the problem that did not repeat: statuses {statuses}, {len(seen)} runs",
            statuses == [0, 0, 0]
            and subjects_since(root, head) == ["Add r1", "Add r2", "Add r3"]
            and len(seen) == 5
            and any(line.content.startswith(land.FLAKY_MARK) for line in decisions[3].lines),
        )
        dependent_cases(expect, Path(work_text))


def dependent_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the confirmation for a request that needs an accepted request, and for a request that cannot run alone."""
    root = work / "repo-dependent"
    head = planted_repository(root)
    seen: list[tuple[gate.Candidate, bool]] = []
    reds: list[str] = []

    def needing(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        """Reject r2.txt with no r1.txt, as a test of a module of r1 does, and the first run of r2.txt as a flake."""
        seen.append((candidate, is_retry))
        has_first, has_second = (git.show(root, candidate.commit, name) is not None for name in ("r1.txt", "r2.txt"))
        if has_second and not has_first:
            return gate.Verdict(("debug",), ("debug: red alone too: test_needs_r1",), 1.0)
        if has_second and not reds:
            reds.append(candidate.commit)
            return gate.Verdict(("debug",), ("debug: red alone too: test_flaky",), 1.0)
        return gate.Verdict(("debug",), (), 1.0)

    decisions, _ = landed(root, own_requests(root, ("one\n", "two\n")), needing)
    expect(
        f"a request that needs an accepted request of the batch and is red by a flake lands, because the red tree "
        f"runs again with the accepted request: {len(seen)} runs",
        [decisions[number].status for number in (1, 2)] == [0, 0]
        and subjects_since(root, head) == ["Add r1", "Add r2"]
        and len(seen) == 4,
    )
    root = work / "repo-no-alone"
    head = planted_repository(root)
    first = request_of(("Replace beta",), (land.Edit("shared.txt", land.REPLACE, "beta\n", "gamma\n"),))
    second = request_of(("Add a line after gamma",), (land.Edit("shared.txt", land.INSERT, "gamma\n", "delta\n"),))
    seen = []

    def delta(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        """Reject each commit with the line delta, and accept each other commit."""
        seen.append((candidate, is_retry))
        is_red = b"delta" in (git.show(root, candidate.commit, "shared.txt") or b"")
        return gate.Verdict(("debug",), ("debug: red alone too: test_delta",) if is_red else (), 1.0)

    decisions, _ = landed(root, [first, second], delta)
    expect(
        f"a request whose tree is red two times after the accepted part and that does not compose alone gets the "
        f"refusal, with no run alone: {len(seen)} runs",
        [decisions[number].status for number in (1, 2)] == [0, ExitCode.FINDINGS]
        and "does not compose alone" in decisions[2].lines[0].content
        and subjects_since(root, head) == ["Replace beta"]
        and len(seen) == 4,
    )
    focus_red_branch_cases(expect, work)


def focus_red_branch_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the confirmation on a red branch with a planted gate that runs only the focus of a run of the base."""
    root = work / "repo-focus-red"
    planted_repository(root)
    (root / "base.txt").write_text(RED, encoding="utf-8")
    red_base = git.commit_all(root, "Make the branch red")
    (root / "base.txt").write_text("repaired\n", encoding="utf-8")
    (root / "r2.txt").write_text("two\n", encoding="utf-8")
    seen: list[tuple[gate.Candidate, bool]] = []
    reds: list[str] = []

    def configured(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
        """Judge two planted configurations: debug is red on a red base.txt, and other on the first run of r2.txt.

        A candidate with a focus runs only the configurations of its focus, as the gate does.
        """
        seen.append((candidate, is_retry))
        ran = {item.configuration for item in candidate.focus} or {"debug", "other"}
        problems: list[str] = []
        focus: list[gate.Focus] = []
        if "debug" in ran and git.show(root, candidate.commit, "base.txt") == RED.encode():
            problems.append("debug: red alone too: test_base")
            focus.append(gate.Focus("debug", ("test_base",)))
        if "other" in ran and git.show(root, candidate.commit, "r2.txt") is not None and not reds:
            reds.append(candidate.commit)
            problems.append("other: red alone too: test_flaky")
            focus.append(gate.Focus("other", ("test_flaky",)))
        return gate.Verdict(tuple(sorted(ran)), tuple(problems), 1.0, focus=tuple(focus))

    requests = [request_of(("Repair the branch",), own=("base.txt",)), request_of(("Add r2",), own=("r2.txt",))]
    decisions, _ = bare_landing(root, requests, configured)
    expect(
        f"on a red branch, a repair and a request that a flake makes red both land, because the red tree runs again "
        f"with the repair, and no run alone on the red branch judges the request: {len(seen)} runs",
        [decisions[number].status for number in (1, 2)] == [0, 0]
        and subjects_since(root, red_base) == ["Repair the branch", "Add r2"]
        and len(seen) == 4
        and seen[1][0].focus == (gate.Focus("other", ("test_flaky",)),),
    )


def flaky_cases(expect: testing.Expect) -> None:
    """Hold the flaky tests: the verdict of a landed request names each flaky test of its accepted run of the gate."""
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


def store_cases(expect: testing.Expect) -> None:
    """Hold the store: a damaged git store refuses the batch with its reason, runs no gate and moves no branch."""
    with tempfile.TemporaryDirectory(prefix="land-store-") as work_text:
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        requests = own_requests(root, ("one\n",))
        config = root / ".git" / "config"
        kept = config.read_bytes()
        config.write_bytes(b"")
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, _ = batch_landing(root, requests, passing(seen))
        config.write_bytes(kept)
        said = " ".join(line.content for line in decisions[1].lines)
        expect(
            f"a store whose file config has no bytes refuses the batch with that reason, runs no gate, and the branch "
            f"does not move: {said}",
            decisions[1].status == ExitCode.TOOL_FAILED
            and "has no bytes" in said
            and not seen
            and git.head(root) == head,
        )
        tree_hash = git.run(root, "rev-parse", "HEAD^{tree}").decode().strip()
        loose = root / ".git" / "objects" / tree_hash[:2] / tree_hash[2:]
        tree_bytes = git.run(root, "cat-file", "tree", tree_hash)
        loose.chmod(0o644)
        loose.write_bytes(b"")
        decisions, _ = batch_landing(root, requests, passing(seen))
        said = " ".join(line.content for line in decisions[1].lines)
        expect(
            f"a store with an object of no bytes fails `git fsck --connectivity-only`, and the batch is refused with "
            f"the error of git and runs no gate: {said[:160]}",
            decisions[1].status == ExitCode.TOOL_FAILED and "git fsck --connectivity-only" in said and not seen,
        )
        pauses: list[float] = []

        def repairing(seconds: float) -> None:
            """Give the object its bytes back on the first pause, as a ref write of a different tree ends."""
            pauses.append(seconds)
            if len(pauses) == 1:
                loose.unlink()
                git.run(root, "hash-object", "-w", "-t", "tree", "--stdin", stdin=tree_bytes)

        batch = [land.Queued(1, root, requests[0], options())]
        made: dict[int, land.Decision] = {}
        landing = land.BatchLanding(
            batch, passing(seen), lambda item, decision: made.__setitem__(item.number, decision), repairing
        )
        landing.land(batch)
        expect(
            f"a store that a run of the check refuses and the next run accepts lands after one pause: {pauses}",
            made[1].status == 0 and pauses[:1] == [land.STORE_RETRY_S] and git.head(root) != head,
        )
        side = root / ".git" / "logs" / "refs" / "heads" / "side"
        git.run(root, "branch", "side")
        with open(side, "a", encoding="utf-8") as handle:
            handle.write(f"{'0' * 40} {'1' * 40} p <p@p> 1 +0000\tan entry that a rebase writes\n")
        moved = git.head(root)
        decisions, _ = batch_landing(root, own_requests(root, ("two\n",)), passing(seen))
        expect(
            "a reflog entry of a different branch that git cannot read refuses no request, because the check reads no "
            "reflog",
            decisions[1].status == 0 and git.head(root) != moved,
        )


# The bound of the wait of a planted gate for the free of an entry of the stack, in seconds.  The watch frees the entry
# in a few looks of land.WATCH_S, so the bound only ends a case whose code never frees it.
FREE_BOUND_S = 60.0


def gone_watch_cases(expect: testing.Expect) -> None:
    """Hold the free of the entry of the stack when a request of the run leaves the queue during the run (the stack).

    On the old code the entry stayed live until the run ended, and a batch above waited on a commit that could never
    land.
    """
    with tempfile.TemporaryDirectory(prefix="land-gone-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        queue = work / "queue"
        queue.mkdir()
        requests = own_requests(root, ("one\n",))
        entry = land.queue_file(queue, 5, land.STACK_SUFFIX)
        lock = land.queue_file(queue, 5, land.STACK_LOCK_SUFFIX)
        is_alive = [True]
        seen: list[bool] = []

        def leaving(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Cancel the request in the run, and wait until the entry of the batch and its lock file go."""
            seen.append(entry.is_file() and lock.is_file())
            is_alive[0] = False
            deadline = time.monotonic() + FREE_BOUND_S
            while (entry.exists() or lock.exists()) and time.monotonic() < deadline:
                time.sleep(0.01)
            seen.append(entry.exists() or lock.exists())
            return gate.Verdict(("debug",), (), 1.0)

        batch = [land.Queued(5, root, requests[0], options())]
        made: dict[int, land.Decision] = {}
        with mock.patch.object(land, "WATCH_S", 0.02):
            landing = land.BatchLanding(
                batch,
                leaving,
                lambda item, decision: made.__setitem__(item.number, decision),
                no_pause,
                is_alive=lambda _item: is_alive[0],
                stack=land.Stack(queue, 5),
            )
            _result, printed = testing.captured(lambda: landing.land(batch))
        expect(
            "a request that leaves the queue during the run frees the entry of the stack and its lock file at once, "
            "and the batch lands nothing",
            seen == [True, False]
            and 5 not in made
            and git.head(root) == head
            and "left the queue during the run" in printed,
        )


def other_right(queue: Path, number: int) -> int:
    """Take the right of way of a different batch, as the holder of a different lane does, and return its descriptor."""
    path = land.queue_file(queue, number, land.RIGHT_SUFFIX)
    descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
    fcntl.flock(descriptor, fcntl.LOCK_EX)
    return descriptor


def short_landing(root: Path, text: str) -> None:
    """Land one commit that changes only kept.txt, as the short landing of a different lane does."""
    (root / "kept.txt").write_text(text, encoding="utf-8")
    tip = git.head(root)
    composed = land.compose(root, request_of(("Move",), own=("kept.txt",)), tip)
    land.swap(root, land.branch_of(root), land.commit_tree(root, composed, "A short landing\n"), tip, "Move")


def right_batch(
    root: Path, queue: Path, number: int, check: land.Check, pause: heavy.Pause
) -> tuple[dict[int, land.Decision], str]:
    """Land one request with the number through a planted gate, with a right of way in the queue (the right of way).

    The function returns each verdict and the printed text.
    """
    batch = [land.Queued(number, root, own_requests(root, (f"right {number}\n",))[0], options())]
    decisions: dict[int, land.Decision] = {}

    def decide(queued: land.Queued, decision: land.Decision) -> None:
        decisions[queued.number] = decision

    right = land.RightOfWay(queue, number, pause)
    batched = land.BatchLanding(batch, check, decide, pause, right=right, stack=land.Stack(queue, number))
    _, printed = testing.captured(lambda: batched.land(batch))
    return decisions, printed


def turn_landing(root: Path, queue: Path, number: int, pause: heavy.Pause) -> land.BatchLanding:
    """Return the batch of one request with the number, a right of way and a stack, with the branch as its base."""
    batch = [land.Queued(number, root, own_requests(root, (f"turn {number}\n",))[0], options())]
    right = land.RightOfWay(queue, number, pause)
    landing = land.BatchLanding(
        batch, passing([]), lambda _queued, _decision: None, pause, right=right, stack=land.Stack(queue, number)
    )
    landing.base = landing.tip()
    return landing


def dead_holder_cases(expect: testing.Expect) -> None:
    """Hold the sweep of the files of dead holders: an old file with no lock goes, a held or young file stays.

    The case plants the files of three batches in a queue, with planted file times, and calls the sweep with a planted
    time, so the verdict reads no clock.  Batch 1 ended long ago, batch 2 is a live holder with old files, and batch 3
    made its files a moment ago and has no lock yet.
    """
    with tempfile.TemporaryDirectory(prefix="land-dead-holder-") as work_text:
        queue = Path(work_text)
        now = 2_000_000_000.0
        old = now - land.DEAD_HOLDER_AGE_S - 1.0
        young = now - 1.0
        files = {
            (1, land.RIGHT_SUFFIX): old,
            (1, land.STACK_SUFFIX): old,
            (1, land.STACK_LOCK_SUFFIX): old,
            (1, land.LINE_SUFFIX): old,
            (2, land.RIGHT_SUFFIX): old,
            (2, land.STACK_SUFFIX): old,
            (2, land.STACK_LOCK_SUFFIX): old,
            (2, land.LINE_SUFFIX): old,
            (3, land.RIGHT_SUFFIX): young,
            (3, land.STACK_LOCK_SUFFIX): young,
        }
        for (number, suffix), moment in files.items():
            path = land.queue_file(queue, number, suffix)
            path.write_text("{}\n" if suffix in (land.STACK_SUFFIX, land.LINE_SUFFIX) else "", encoding="utf-8")
            os.utime(path, (moment, moment))
        with (
            open(land.queue_file(queue, 2, land.RIGHT_SUFFIX), encoding="utf-8") as right,
            open(land.queue_file(queue, 2, land.STACK_LOCK_SUFFIX), encoding="utf-8") as stacked,
        ):
            fcntl.flock(right, fcntl.LOCK_EX)
            fcntl.flock(stacked, fcntl.LOCK_EX)
            removed = land.removed_dead_holders(queue, now)
        kept = sorted(path.name for path in queue.iterdir())
        expect(
            f"the sweep removes the old files of a dead holder, and keeps the files of a live holder and the young files "
            f"of a holder that has no lock yet: removed {removed}, kept {kept}",
            removed == ["00000001.line", "00000001.right", "00000001.stack", "00000001.stacked"]
            and kept
            == [
                "00000002.line",
                "00000002.right",
                "00000002.stack",
                "00000002.stacked",
                "00000003.right",
                "00000003.stacked",
            ],
        )


def right_cases(expect: testing.Expect) -> None:
    """Hold the right of way: a batch that loses swaps takes it, and the swap of a different batch waits for it."""
    with tempfile.TemporaryDirectory(prefix="land-right-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        queue = work / "queue"
        queue.mkdir()
        planted_repository(root)
        right_file = land.queue_file(queue, 1, land.RIGHT_SUFFIX)
        held_on_run: list[bool] = []

        def move_unless_right() -> None:
            """Move the branch on each run, as the short landings of the other lanes do, unless they wait."""
            is_right = heavy.is_held(right_file)
            held_on_run.append(is_right)
            if not is_right:
                short_landing(root, f"moved {len(held_on_run)}\n")

        decisions, printed = right_batch(root, queue, 1, passing([], move_unless_right), no_pause)
        expect(
            "a batch that loses RIGHT_AFTER swaps takes the right of way, the other lanes keep the branch while it "
            "runs, it lands on the next run, and it frees the right of way",
            decisions[1].status == 0
            and held_on_run == [False] * land.RIGHT_AFTER + [True]
            and git.commit_subject(root, "HEAD") == "Add r1"
            and not right_file.exists()
            and "takes the right of way" in printed,
        )
        first = other_right(queue, 1)
        later = other_right(queue, 9)
        land.queue_file(queue, 3, land.RIGHT_SUFFIX).write_text("", encoding="utf-8")
        right = land.RightOfWay(queue, 5, no_pause)
        waits_for = right.first()
        right.take()
        taken_waits_for = right.first()
        right.free()
        expect(
            "a batch with no right of way waits for each held right of way, a batch with one waits only for a "
            "smaller number, and a file with no lock holds no right of way",
            waits_for == [1, 9]
            and taken_waits_for == [1]
            and not land.queue_file(queue, 5, land.RIGHT_SUFFIX).exists(),
        )
        pauses: list[float] = []
        turn = turn_landing(root, queue, 5, pauses.append)
        is_moved, printed = testing.captured(lambda: turn.wait_turn(land.SWAP_STEP))
        expect(
            "a right of way that is never freed, with no move of the branch, holds the swap for RIGHT_WAIT_S seconds "
            "only, with a line",
            not is_moved
            and len(pauses) == int(land.RIGHT_WAIT_S / land.POLL_S)
            and "for the right of way of 00000001 00000009" in printed
            and "waited" in printed,
        )
        added: list[int] = []

        def new_holder(_seconds: float) -> None:
            """Let a different batch take a right of way on the first pause of the wait."""
            if not added:
                added.append(other_right(queue, 7))

        turn = turn_landing(root, queue, 5, new_holder)
        _, printed = testing.captured(lambda: turn.wait_turn(land.SWAP_STEP))
        os.close(added[0])
        expect(
            "a holder that takes a right of way during the wait gets its own line, and no line names a holder again",
            printed.count("the batch waits before its swap") == 2
            and "for the right of way of 00000001 00000009\n" in printed
            and "for the right of way of 00000007\n" in printed,
        )
        os.close(later)
        moved: list[str] = []
        seen: list[tuple[gate.Candidate, bool]] = []

        def holder_lands(_seconds: float) -> None:
            """Land the batch with the right of way and free it, on the first pause of the wait."""
            if not moved:
                short_landing(root, "the batch with the right of way landed\n")
                moved.append(git.head(root))
                os.close(first)

        decisions, printed = right_batch(root, queue, 5, passing(seen), holder_lands)
        expect(
            f"a batch waits before its first run while a different batch holds the right of way, then runs one time on "
            f"the commit of that batch, and lands: {len(seen)} runs",
            decisions[5].status == 0
            and "waits before its run for the right of way of 00000001" in printed
            and f"waited {land.POLL_S:.0f} s before its run" in printed
            and len(seen) == 1
            and seen[0][0].head == moved[0]
            and git.parent(root, "HEAD") == moved[0],
        )
        holder = [other_right(queue, 1)]
        polls: list[tuple[int, str]] = []
        landed_first: list[str] = []
        gate_runs: list[tuple[gate.Candidate, bool]] = []

        def holder_moves_then_frees(seconds: float) -> None:
            """Move the branch on the first poll of the wait, and free the right of way on the second poll."""
            if seconds != land.POLL_S:
                return
            polls.append((len(gate_runs), git.head(root)))
            if len(polls) == 1:
                short_landing(root, "a different lane landed during the wait\n")
                landed_first.append(git.head(root))
            elif holder:
                os.close(holder.pop())

        decisions, printed = right_batch(root, queue, 6, passing(gate_runs), holder_moves_then_frees)
        expect(
            "a move of the branch does not end the wait before the run: the batch waits until the holder frees its "
            "right of way, then runs one time on the last commit, and lands on it",
            decisions[6].status == 0
            and [count for count, _ in polls] == [0, 0]
            and polls[1][1] == landed_first[0]
            and len(gate_runs) == 1
            and gate_runs[0][0].head == landed_first[0]
            and git.parent(root, "HEAD") == landed_first[0]
            and "moved while the batch waited" not in printed
            and not holder,
        )
        taken: list[int] = []
        holder_landed: list[str] = []
        runs: list[tuple[gate.Candidate, bool]] = []

        def take_during_run() -> None:
            """Let a different batch take a smaller right of way during the first run of the batch."""
            if not taken:
                taken.append(other_right(queue, 1))

        def holder_lands_then_frees(seconds: float) -> None:
            """Land the batch with the right of way on the first poll, and free its right of way on the second poll."""
            if seconds != land.POLL_S or not taken:
                return
            if not holder_landed:
                short_landing(root, "the holder landed during the swap wait\n")
                holder_landed.append(git.head(root))
            elif taken[0] >= 0:
                os.close(taken[0])
                taken[0] = -1

        decisions, printed = right_batch(root, queue, 8, passing(runs, take_during_run), holder_lands_then_frees)
        expect(
            "a right of way that a different batch takes during the run holds the swap.  The move of its landing ends "
            "that wait at once, and the batch waits before its next run until the holder frees its right of way.  "
            "Then it runs one more time on the commit of the holder, and lands",
            decisions[8].status == 0
            and "waits before its swap for the right of way of 00000001" in printed
            and "moved while the batch waited for a right of way" in printed
            and "waits before its run for the right of way of 00000001" in printed
            and len(runs) == 2
            and runs[1][0].head == holder_landed[0]
            and git.parent(root, "HEAD") == holder_landed[0]
            and taken == [-1],
        )


def side_commit(root: Path, parent: str, name: str, text: str) -> str:
    """Write a commit that sets one file on a commit, as the batch of a different lane composes it, and return it."""
    (root / name).write_text(text, encoding="utf-8")
    composed = land.compose(root, request_of((f"Set {name}",), own=(name,)), parent)
    return land.commit_tree(root, composed, f"Set {name}\n")


class OtherEntry:
    """The entry of a different batch in the stack, as the holder of a different lane writes it (the stack)."""

    __slots__ = ("branch", "stack")

    def __init__(
        self, queue: Path, branch: str, number: int, base: str, commit: str, is_accepted: bool = False
    ) -> None:
        """Write the entry of a batch on the branch, and hold its lock."""
        self.branch = branch
        self.stack = land.Stack(queue, number)
        self.stack.publish(branch, base, commit, is_accepted)

    @property
    def is_live(self) -> bool:
        """Tell if the entry still holds its lock."""
        return self.stack.descriptor >= 0

    def move(self, base: str, commit: str, is_accepted: bool = False) -> None:
        """Write a new entry, as a batch that composes again or that the gate accepted does."""
        self.stack.publish(self.branch, base, commit, is_accepted)

    def leave(self) -> None:
        """Remove the entry and free its lock, as a batch that landed or that the gate refused does."""
        self.stack.free()

    def die(self) -> None:
        """Free the lock and keep the file of the entry, as the kernel does for a holder that ends."""
        os.close(self.stack.descriptor)
        self.stack.descriptor = -1


def stack_cases(expect: testing.Expect) -> None:
    """Hold the stack: a stack of three, a refusal in the middle, an outside push, red runs, a fold, a dead holder, the
    bound of the depth, and a right of way above the batch."""
    with tempfile.TemporaryDirectory(prefix="land-stack-") as work_text:
        work = Path(work_text)
        made: list[Path] = []

        def fresh() -> tuple[Path, Path, str, str]:
            """Plant a new repository and queue, and return them with the commit and the name of the branch."""
            made.append(work / f"case-{len(made)}")
            root, queue = made[-1] / "repo", made[-1] / "queue"
            queue.mkdir(parents=True)
            planted_repository(root)
            return root, queue, git.head(root), land.branch_of(root)

        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, first, "b.txt", "b\n")
        below = [OtherEntry(queue, branch, 1, tip, first), OtherEntry(queue, branch, 2, first, second)]
        seen: list[tuple[gate.Candidate, bool]] = []
        published: list[land.StackEntry] = []
        polls: list[str] = []

        def own_entry() -> None:
            """Keep the entry of the batch, as a different lane reads it."""
            published.extend(item for item in land.Stack(queue, 0).entries(branch) if item.number == 3)

        def lands_in_order(seconds: float) -> None:
            """Land the first batch below on the first poll, and the second one on the second poll."""
            if seconds != land.POLL_S:
                return
            polls.append(git.head(root))
            own_entry()
            if len(polls) == 1:
                land.swap(root, branch, first, tip, "First")
                below[0].leave()
            elif len(polls) == 2:
                land.swap(root, branch, second, first, "Second")
                below[1].leave()

        decisions, printed = right_batch(root, queue, 3, passing(seen, own_entry), lands_in_order)
        expect(
            "a stack of three lands in order: the batch composes on the top of the two entries below it, writes its "
            "entry, runs one time, waits at its swap until the two batches below land, and lands on the second one",
            decisions[3].status == 0
            and len(seen) == 1
            and seen[0][0].head == second
            and git.parent(root, "HEAD") == second
            and git.parent(root, second) == first
            and seen[0][0].built == tuple(sorted(gate.BUILT_FILES))
            and len(polls) == 2
            and f"composes on {second[: land.SHORT_HASH]} of the stack, above 00000001 00000002" in printed
            and "waits before its swap for the batches below it in the stack: 00000001 00000002" in printed
            and published[0] == land.StackEntry(3, second, seen[0][0].commit, False)
            and published[-1] == land.StackEntry(3, second, seen[0][0].commit, True)
            and not land.queue_file(queue, 3, land.STACK_SUFFIX).exists()
            and not land.queue_file(queue, 3, land.STACK_LOCK_SUFFIX).exists(),
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, first, "b.txt", "b\n")
        below = [OtherEntry(queue, branch, 1, tip, first), OtherEntry(queue, branch, 2, first, second)]
        seen = []
        polls = []

        def middle_refused(seconds: float) -> None:
            """Refuse the batch in the middle on the first poll, and land the batch below it on the second poll."""
            if seconds != land.POLL_S:
                return
            polls.append(git.head(root))
            if len(polls) == 1:
                below[1].leave()
            elif len(polls) == 2:
                land.swap(root, branch, first, tip, "First")
                below[0].leave()

        decisions, printed = right_batch(root, queue, 3, passing(seen), middle_refused)
        expect(
            "a refusal in the middle of the stack makes the batch on the top compose again on the batch below the "
            "refused one, run one more time, and land on it",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [second, first]
            and git.parent(root, "HEAD") == first
            and "the base of the batch left the stack, and the batch composes its 1 accepted requests again" in printed,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []
        moved: list[str] = []

        def outside_push(seconds: float) -> None:
            """Push the branch from outside on the first poll, and land the batch below, composed again, on the next."""
            if seconds != land.POLL_S:
                return
            if not moved:
                short_landing(root, "an outside push\n")
                pushed = git.head(root)
                again = side_commit(root, pushed, "a.txt", "a\n")
                holder.move(pushed, again)
                moved.extend((pushed, again))
            elif holder.is_live:
                land.swap(root, branch, moved[1], moved[0], "First again")
                holder.leave()

        decisions, printed = right_batch(root, queue, 3, passing(seen), outside_push)
        expect(
            "an outside push of the branch makes each batch of the stack compose again: the batch below writes its new "
            "commit, and the batch on the top composes on it, runs one more time, and lands on it",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, moved[1]]
            and git.parent(root, "HEAD") == moved[1]
            and git.parent(root, moved[1]) == moved[0],
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []

        def red_on_stack(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Reject each commit on the commit of the batch below, as an interaction with it does."""
            seen.append((candidate, is_retry))
            problems = ("debug: red alone too: test_pair",) if candidate.head == first else ()
            return gate.Verdict(("debug",), problems, 1.0)

        def holder_refused(seconds: float) -> None:
            """Refuse the batch below on the first poll."""
            if seconds == land.POLL_S and holder.is_live:
                holder.leave()

        decisions, printed = right_batch(root, queue, 3, red_on_stack, holder_refused)
        expect(
            "a red run on the stack gives no verdict and runs no run of the base.  When the batch below leaves the "
            "stack, the batch composes again on the branch, and lands",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, tip]
            and [candidate.built for candidate, _ in seen] == [tuple(sorted(gate.BUILT_FILES)), ()]
            and "the run on the stack is red" in printed
            and git.parent(root, "HEAD") == tip,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []

        def red_with_first(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Reject each commit of the batch on the commit of the batch below, and accept that commit itself."""
            seen.append((candidate, is_retry))
            is_red = candidate.commit != first and git.show(root, candidate.commit, "a.txt") is not None
            return gate.Verdict(("debug",), ("debug: red alone too: test_pair",) if is_red else (), 1.0)

        def holder_lands(seconds: float) -> None:
            """Land the batch below on the first poll."""
            if seconds == land.POLL_S and holder.is_live:
                land.swap(root, branch, first, tip, "First")
                holder.leave()

        decisions, _ = right_batch(root, queue, 3, red_with_first, holder_lands)
        lines = [line.content for line in decisions[3].lines] if 3 in decisions else []
        expect(
            f"a red run on the stack waits until the batch below lands.  Then the search judges the batch on its exact "
            f"base, and it refuses the request with no line of a red branch: {lines}",
            decisions[3].status == ExitCode.FINDINGS
            and seen[0][0].commit != first
            and all(candidate.head == first for candidate, _ in seen)
            and len(seen) == 4
            and not any("red with no request" in line for line in lines)
            and git.head(root) == first,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        folded = side_commit(root, tip, "a.txt", "a with a fold\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []
        polls = []

        def holder_folds(seconds: float) -> None:
            """Accept the batch below with a fold on the first poll, and land its final commit on the second poll."""
            if seconds != land.POLL_S:
                return
            polls.append(git.head(root))
            if len(polls) == 1:
                holder.move(tip, folded, True)
            elif len(polls) == 2:
                land.swap(root, branch, folded, tip, "First with a fold")
                holder.leave()

        decisions, _ = right_batch(root, queue, 3, passing(seen), holder_folds)
        expect(
            "a fold below gives a final commit that the batch did not test: the batch composes again on that final "
            "commit, runs again as a retry, and lands on it",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, folded]
            and seen[1][1]
            and git.parent(root, "HEAD") == folded,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []

        def holder_dies(seconds: float) -> None:
            """End the holder of the batch below on the first poll, with its file of the entry still there."""
            if seconds == land.POLL_S and holder.is_live:
                holder.die()

        decisions, _ = right_batch(root, queue, 3, passing(seen), holder_dies)
        expect(
            "a holder below that ends leaves a file with no lock, which is no entry: the batch composes again on the "
            "branch, and lands",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, tip]
            and land.queue_file(queue, 1, land.STACK_SUFFIX).exists()
            and git.parent(root, "HEAD") == tip,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, first, "b.txt", "b\n")
        third = side_commit(root, second, "c.txt", "c\n")
        chain = [
            OtherEntry(queue, branch, 1, tip, first),
            OtherEntry(queue, branch, 2, first, second),
            OtherEntry(queue, branch, 4, second, third),
        ]
        seen = []
        steps = [(tip, first), (first, second), (second, third)]

        def chain_lands(seconds: float) -> None:
            """Land one batch of the stack on each poll, from the bottom."""
            if seconds == land.POLL_S and steps:
                old, new = steps.pop(0)
                land.swap(root, branch, new, old, "Below")
                chain[2 - len(steps)].leave()

        decisions, printed = right_batch(root, queue, 3, passing(seen), chain_lands)
        expect(
            "a stack of STACK_DEPTH entries holds the batch before its run, and when the bottom one lands, the batch "
            "composes on the top, and lands after the two other entries",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [third]
            and "waits before its run for the full stack: 00000001 00000002 00000004" in printed
            and f"waited {land.POLL_S:.0f} s before its run" in printed
            and git.parent(root, "HEAD") == third,
        )
        root, queue, tip, branch = fresh()
        seen = []
        upper: list[OtherEntry] = []
        rights: list[int] = []
        pauses: list[float] = []

        def upper_stacks(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Let a batch with a right of way stack on the commit of the run, as a different lane does."""
            seen.append((candidate, is_retry))
            if not upper:
                upper.append(
                    OtherEntry(queue, branch, 2, candidate.commit, side_commit(root, candidate.commit, "u.txt", "u\n"))
                )
                rights.append(other_right(queue, 2))
            return gate.Verdict(("debug",), (), 1.0)

        decisions, printed = right_batch(root, queue, 1, upper_stacks, pauses.append)
        expect(
            "a batch with a right of way that is stacked on the batch does not hold the swap of the batch: the batch "
            "swaps with no poll",
            decisions[1].status == 0
            and len(seen) == 1
            and land.POLL_S not in pauses
            and "waits before its swap" not in printed,
        )
        os.close(rights[0])
        upper[0].leave()
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        holder = OtherEntry(queue, branch, 1, tip, first)
        seen = []
        held: dict[int, land.Decision] = {}

        def decide_held(queued: land.Queued, decision: land.Decision) -> None:
            held[queued.number] = decision

        batch = [land.Queued(5, root, own_requests(root, ("held right\n",))[0], options())]
        right = land.RightOfWay(queue, 5, no_pause)
        right.take()
        landing = land.BatchLanding(
            batch, passing(seen), decide_held, no_pause, right=right, stack=land.Stack(queue, 5)
        )
        _, printed = testing.captured(lambda: landing.land(batch))
        holder.leave()
        expect(
            "a batch that holds its right of way composes only on the branch, also with an entry on the branch, so it "
            "never waits for a batch below it",
            held[5].status == 0
            and [candidate.head for candidate, _ in seen] == [tip]
            and "of the stack" not in printed
            and git.parent(root, "HEAD") == tip,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        elsewhere = OtherEntry(queue, "refs/heads/elsewhere", 1, tip, first)
        seen = []
        decisions, printed = right_batch(root, queue, 3, passing(seen), no_pause)
        elsewhere.leave()
        expect(
            "an entry of a different branch is no entry of the stack of the branch: the batch composes on the branch",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [tip]
            and git.parent(root, "HEAD") == tip,
        )
        root, queue, tip, branch = fresh()
        seen = []
        fold_commits: list[str] = []
        during: list[land.StackEntry] = []

        def folds_built(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Fold the built files during the run, as the gate does before the tests, and accept the folded commit."""
            seen.append((candidate, is_retry))
            fold_commits.append(side_commit(root, tip, "built.txt", "built\n"))
            if candidate.on_fold is not None:
                candidate.on_fold(fold_commits[0])
            during.extend(land.Stack(queue, 0).entries(branch))
            return gate.Verdict(("debug",), (), 1.0, (), fold_commits[0])

        decisions, printed = right_batch(root, queue, 3, folds_built, no_pause)
        fold_commit = fold_commits[0] if fold_commits else ""
        expect(
            "when the gate folds the built files during the run, the batch writes the folded commit into its entry, "
            "so a batch that starts then stacks on the commit that lands",
            decisions[3].status == 0
            and during == [land.StackEntry(3, tip, fold_commit, False)]
            and git.head(root) == fold_commit
            and f"the entry of the batch holds the commit {fold_commit[: land.SHORT_HASH]} of the fold" in printed,
        )
        root, queue, tip, branch = fresh()
        first = side_commit(root, tip, "a.txt", "a\n")
        stuck = OtherEntry(queue, branch, 1, tip, first)
        seen = []
        with mock.patch.object(land, "STACK_WAIT_S", 5 * land.POLL_S):
            decisions, printed = right_batch(root, queue, 3, passing(seen), no_pause)
        stuck.leave()
        expect(
            "a batch below that stays live and never lands holds the swap for STACK_WAIT_S only.  Then the next round "
            "composes on the branch, and the batch lands",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, tip]
            and f"waited {5 * land.POLL_S:.0f} seconds, and it goes on to its swap" in printed
            and git.parent(root, "HEAD") == tip,
        )


def lost_run_cases(expect: testing.Expect) -> None:
    """Hold the rule of one lost run of the stack: one run at once on an older open entry, then a wait for a verdict.

    The batch 1 below composes again two times, each time on a new commit with no verdict, and then the gate accepts
    its last commit.  The batch 3 on the top loses its first run, and it stacks at once on the open entry of the older
    batch 1.  It loses that run too, waits for the verdict of the batch 1, and runs one more time.  Then the batch 8
    loses its run, the younger batch 9 holds an open entry on the branch, and the batch 8 composes below it at once.
    """
    with tempfile.TemporaryDirectory(prefix="land-lost-run-") as work_text:
        root, queue = Path(work_text) / "repo", Path(work_text) / "queue"
        queue.mkdir(parents=True)
        tip = planted_repository(root)
        branch = land.branch_of(root)
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, tip, "a.txt", "a2\n")
        third = side_commit(root, tip, "a.txt", "a3\n")
        below = OtherEntry(queue, branch, 1, tip, first)
        seen: list[tuple[gate.Candidate, bool]] = []
        steps: list[str] = []

        def below_composes_again(seconds: float) -> None:
            """Move the batch below to a new commit two times, then mark its commit accepted, then land it."""
            if seconds != land.POLL_S:
                return
            if not steps:
                below.move(tip, second)
                steps.append("second")
            elif steps[-1] == "second":
                below.move(tip, third)
                steps.append("third")
            elif steps[-1] == "third":
                below.move(tip, third, is_accepted=True)
                steps.append("accepted")
            elif steps[-1] == "accepted":
                land.swap(root, branch, third, tip, "Below")
                below.leave()
                steps.append("landed")

        decisions, printed = right_batch(root, queue, 3, passing(seen), below_composes_again)
        expect(
            f"a batch whose base left the stack runs again at once on the open entry of the older batch below it.  After "
            f"its second lost run it runs only on a base with a verdict: it waits while the batch below has no verdict, "
            f"runs one time on the accepted commit, and lands on it: {[candidate.head for candidate, _ in seen]}",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, second, third]
            and git.parent(root, "HEAD") == third
            and printed.count("the base of the batch left the stack") == 2
            and "waits before its run for the verdicts of the stack: 00000001" in printed,
        )
        tip = git.head(root)
        older = side_commit(root, tip, "c.txt", "c\n")
        younger_commit = side_commit(root, tip, "d.txt", "d\n")
        refused = OtherEntry(queue, branch, 1, tip, older)
        younger: list[OtherEntry] = []
        seen = []

        def younger_stacks(seconds: float) -> None:
            """Refuse the older batch below, and let the younger batch 9 write an open entry on the branch."""
            if seconds == land.POLL_S and refused.is_live:
                refused.leave()
                younger.append(OtherEntry(queue, branch, 9, tip, younger_commit))

        with mock.patch.object(land, "STACK_WAIT_S", 5 * land.POLL_S):
            decisions, printed = right_batch(root, queue, 8, passing(seen), younger_stacks)
        for entry in younger:
            entry.leave()
        expect(
            f"after its first lost run, a batch does not stack on the open entry of a younger batch, and it does not "
            f"wait for its verdict: it composes on the branch below that entry at once, and lands: "
            f"{[candidate.head for candidate, _ in seen]}",
            decisions[8].status == 0
            and [candidate.head for candidate, _ in seen] == [older, tip]
            and git.parent(root, "HEAD") == tip
            and "the verdicts of the stack" not in printed
            and "it goes on to its run" not in printed,
        )


def newer_entry_cases(expect: testing.Expect) -> None:
    """Hold the wait of the rule of one lost run to the entries of its first look (the stack).

    The batch 3 loses its first two runs, because the batch 1 below it composes again two times.  While the batch 3
    waits for the verdict of the batch 1, the batch 4 stacks on the batch 1, and its run never ends.  The batch 1 then
    lands.  The entry of the batch 4 came into the stack during the wait, so it does not hold the batch 3.
    """
    with tempfile.TemporaryDirectory(prefix="land-newer-entry-") as work_text:
        root, queue = Path(work_text) / "repo", Path(work_text) / "queue"
        queue.mkdir(parents=True)
        tip = planted_repository(root)
        branch = land.branch_of(root)
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, tip, "a.txt", "a2\n")
        third = side_commit(root, tip, "a.txt", "a3\n")
        above = side_commit(root, third, "b.txt", "b\n")
        below = OtherEntry(queue, branch, 1, tip, first)
        newer: list[OtherEntry] = []
        seen: list[tuple[gate.Candidate, bool]] = []
        steps: list[str] = []

        def newer_batch_stacks(seconds: float) -> None:
            """Move the batch 1 two times, stack the batch 4 on it, accept the batch 1, then land it."""
            if seconds != land.POLL_S:
                return
            if not steps:
                below.move(tip, second)
                steps.append("moved")
            elif steps[-1] == "moved":
                below.move(tip, third)
                steps.append("moved again")
            elif steps[-1] == "moved again":
                newer.append(OtherEntry(queue, branch, 4, third, above))
                steps.append("stacked")
            elif steps[-1] == "stacked":
                below.move(tip, third, is_accepted=True)
                steps.append("accepted")
            elif steps[-1] == "accepted":
                land.swap(root, branch, third, tip, "Below")
                below.leave()
                steps.append("landed")

        with mock.patch.object(land, "STACK_WAIT_S", 40 * land.POLL_S):
            decisions, printed = right_batch(root, queue, 3, passing(seen), newer_batch_stacks)
        for entry in newer:
            entry.leave()
        expect(
            "a batch whose base left the stack two times waits for the verdict of the entry of its first look, and not "
            "for an entry that stacks on it during the wait: it runs on the accepted commit of that entry, and lands",
            decisions[3].status == 0
            and [candidate.head for candidate, _ in seen] == [first, second, third]
            and git.parent(root, "HEAD") == third
            and "waits before its run for the verdicts of the stack: 00000001" in printed
            and "00000004" not in printed
            and "it goes on to its run" not in printed,
        )


def line_cases(expect: testing.Expect) -> None:
    """Hold the line of the stack: the batches that wait for a place of a full stack go in the sequence of the queue.

    The stack holds the batches 1, 2 and 4.  The older batch 3 of a different lane waits in the line, and the batch 6
    waits too.  The batch 1 lands, and the batch 6 looks at the stack before the batch 3 does.  The free place goes to
    the batch 3, and the batch 6 takes the next free place, on the commit of the batch 3.
    """
    with tempfile.TemporaryDirectory(prefix="land-line-") as work_text:
        root, queue = Path(work_text) / "repo", Path(work_text) / "queue"
        queue.mkdir(parents=True)
        tip = planted_repository(root)
        branch = land.branch_of(root)
        first = side_commit(root, tip, "a.txt", "a\n")
        second = side_commit(root, first, "b.txt", "b\n")
        third = side_commit(root, second, "c.txt", "c\n")
        fourth = side_commit(root, third, "d.txt", "d\n")
        chain = [
            OtherEntry(queue, branch, 1, tip, first),
            OtherEntry(queue, branch, 2, first, second),
            OtherEntry(queue, branch, 4, second, third),
        ]
        older = land.Stack(queue, 3)
        older.join_line(branch)
        steps = [(tip, first, 0), (first, second, 1), (second, third, 2), (third, fourth, -1)]
        runs_at_take: list[int] = []
        seen: list[tuple[gate.Candidate, bool]] = []

        def older_takes_the_place(seconds: float) -> None:
            """Land the bottom batch, let the batch 3 take the free place, and then land one batch on each poll."""
            if seconds != land.POLL_S or not steps:
                return
            if len(steps) == 3 and older.descriptor >= 0 and not runs_at_take:
                runs_at_take.append(len(seen))
                older.publish(branch, third, fourth, False)
                return
            old, new, index = steps.pop(0)
            land.swap(root, branch, new, old, "Below")
            if index >= 0:
                chain[index].leave()
            else:
                older.free()

        decisions, printed = right_batch(root, queue, 6, passing(seen), older_takes_the_place)
        older.free()
        for entry in chain:
            entry.leave()
        expect(
            f"a free place of a full stack goes to the older batch in the line, also when the younger batch looks "
            f"first.  The younger batch then runs one time on the commit of the older batch, and lands after it: "
            f"{[candidate.head for candidate, _ in seen]}",
            decisions[6].status == 0
            and runs_at_take == [0]
            and [candidate.head for candidate, _ in seen] == [fourth]
            and git.parent(root, "HEAD") == fourth
            and "waits before its run for the full stack: 00000001 00000002 00000004" in printed
            and "waits before its run for the older batches in the line of the stack: 00000003" in printed
            and not land.queue_file(queue, 6, land.LINE_SUFFIX).exists(),
        )
        marks = {number: land.Stack(queue, number) for number in (2, 5, 7)}
        marks[2].join_line(branch)
        marks[5].join_line("refs/heads/elsewhere")
        marks[7].join_line(branch)
        dead = land.queue_file(queue, 3, land.LINE_SUFFIX)
        dead.write_text(json.dumps({"branch": branch}) + "\n", encoding="utf-8")
        waiting = land.Stack(queue, 6)
        ahead = waiting.ahead(branch)
        marks[2].publish(branch, tip, first, False)
        is_left = not land.queue_file(queue, 2, land.LINE_SUFFIX).exists()
        for mark in marks.values():
            mark.free()
        dead.unlink()
        expect(
            f"the line ahead of a batch holds only the live marks of older batches on its branch, and the entry of a "
            f"batch replaces its mark: {ahead}",
            ahead == [2] and is_left,
        )


def stack_thread_cases(expect: testing.Expect) -> None:
    """Hold the stack with three real batches in threads: the top one ends its run first, and they land in order."""
    with tempfile.TemporaryDirectory(prefix="land-stack-threads-") as work_text:
        work = Path(work_text)
        root, queue = work / "repo", work / "queue"
        queue.mkdir()
        planted_repository(root)
        tip = git.head(root)
        requests = own_requests(root, ("one\n", "two\n", "three\n"))
        numbers = (1, 2, 3)
        started = {number: threading.Event() for number in numbers}
        released = {number: threading.Event() for number in numbers}
        seen: dict[int, list[gate.Candidate]] = {number: [] for number in numbers}
        decisions: dict[int, land.Decision] = {}

        def gate_of(number: int) -> land.Check:
            """Return the planted gate of one batch: it waits for its release, and it accepts each commit."""

            def check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
                seen[number].append(candidate)
                started[number].set()
                released[number].wait(THREAD_BOUND_S)
                return gate.Verdict(("debug",), (), 1.0)

            return check

        def short_pause(_seconds: float) -> None:
            """Pause a short real time, so the other threads go on."""
            time.sleep(0.01)

        def run_batch(number: int) -> None:
            """Land the batch of one request as the holder of a lane does, with a right of way and a stack."""
            batch = [land.Queued(number, root, requests[number - 1], options())]

            def decide(queued: land.Queued, decision: land.Decision) -> None:
                decisions[queued.number] = decision

            right = land.RightOfWay(queue, number, short_pause)
            landing = land.BatchLanding(
                batch, gate_of(number), decide, short_pause, right=right, stack=land.Stack(queue, number)
            )
            landing.land(batch)

        def run_all() -> None:
            """Start each batch after the run of the one before it starts, and release the runs from the top."""
            threads = []
            for number in numbers:
                thread = threading.Thread(target=run_batch, args=(number,))
                thread.start()
                threads.append(thread)
                started[number].wait(THREAD_BOUND_S)
            for number in reversed(numbers):
                released[number].set()
            for thread in threads:
                thread.join(THREAD_BOUND_S)

        _, printed = testing.captured(run_all)
        commits = [seen[number][0].commit if seen[number] else "" for number in numbers]
        expect(
            "three real batches in threads: each composes on the commit that the batch before it tests, the top one "
            "ends its run first, and the three land in order with one run each",
            all(decisions.get(number, land.refused(ExitCode.USAGE, "no verdict")).status == 0 for number in numbers)
            and [len(seen[number]) for number in numbers] == [1, 1, 1]
            and seen[1][0].head == tip
            and seen[2][0].head == commits[0]
            and seen[3][0].head == commits[1]
            and git.head(root) == commits[2]
            and git.parent(root, commits[2]) == commits[1]
            and git.parent(root, commits[1]) == commits[0],
        )
        if not all(decisions.get(number) for number in numbers):
            print(printed)


def branch_commit(root: Path, parent: str, files: Mapping[str, str], message: str) -> str:
    """Write a commit on a commit that sets some files, with the message, as a commit of a branch, and return it."""
    for name, text in files.items():
        (root / name).write_text(text, encoding="utf-8")
    composed = land.compose(root, request_of((message,), own=tuple(files)), parent)
    return git.commit_tree(root, composed.tree, [parent], message)


def planted_branch(root: Path, name: str, commits: Sequence[tuple[Mapping[str, str], str]]) -> list[str]:
    """Write the commits of a branch on HEAD, one on the other, point the branch at the last, and return them."""
    made: list[str] = []
    parent = git.head(root)
    for files, message in commits:
        parent = branch_commit(root, parent, files, message)
        made.append(parent)
    git.run(root, "update-ref", f"refs/heads/{name}", parent)
    return made


def series_landing(
    root: Path, scratch: Path, branch: str, check: land.Check
) -> tuple[dict[int, land.Decision], land.BatchLanding]:
    """Land the series of a branch as request 1 through a planted gate, and return its verdict and the landing."""
    request = land.series_request(root, branch, scratch)
    return batch_landing(root, [request], check)


def series_cases(expect: testing.Expect) -> None:
    """Hold the series: a series lands in order with its messages, a red tip, a commit that does not compose, a stack,
    the merges of a branch, the words of --series, and the queue file of a series."""
    with tempfile.TemporaryDirectory(prefix="land-series-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        scratch = work / "patches"
        scratch.mkdir()
        messages = (
            "Add the first file\n\nThe first body.\n",
            "Add the second file\n\nThe second body, with\ntwo lines.\n",
            "Change the first file\n",
        )
        made = planted_branch(
            root,
            "three",
            ((({"a.txt": "a\n"}), messages[0]), ({"b.txt": "b\n"}, messages[1]), ({"a.txt": "a again\n"}, messages[2])),
        )
        request = land.series_request(root, "three", scratch)
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, _ = batch_landing(root, [request], passing(seen))
        second = git.parent(root, "HEAD")
        expect(
            "a series of three commits lands as three commits in sequence, each with its own message, after one run "
            "of the gate on its tip",
            decisions[1].status == 0
            and [part.commit for part in request.parts] == made
            and subjects_since(root, head) == ["Add the first file", "Add the second file", "Change the first file"]
            and git.message_of(root, git.parent(root, second)) == messages[0]
            and git.message_of(root, second) == messages[1]
            and git.message_of(root, "HEAD") == messages[2]
            and git.show(root, "HEAD", "a.txt") == b"a again\n"
            and len(seen) == 1
            and seen[0][0].commit == git.head(root)
            and git.parent(root, git.parent(root, git.parent(root, "HEAD"))) == head
            and "the series of 3 commits" in decisions[1].lines[0].content,
        )
        before = git.head(root)
        planted_branch(root, "red", (({"c.txt": "c\n"}, "Add c\n"), ({"d.txt": RED}, "Add a red d\n")))
        seen = []

        def red_tip(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Reject each commit that holds the red d.txt, and accept each other commit."""
            seen.append((candidate, is_retry))
            is_red = git.show(root, candidate.commit, "d.txt") == RED.encode()
            return gate.Verdict(("debug",), ("debug: red alone too: test_d",) if is_red else (), 1.0)

        decisions, _ = series_landing(root, scratch, "red", red_tip)
        expect(
            "a series whose tip is red is refused whole, and main gets no commit of it",
            decisions[1].status == ExitCode.FINDINGS
            and git.head(root) == before
            and any("test_d" in line.content for line in decisions[1].lines),
        )
        planted_branch(
            root,
            "clash",
            (
                ({"shared.txt": "alpha\nbranch\n"}, "Change beta on the branch\n"),
                ({"own.txt": "own on the branch\n"}, "Change own on the branch\n"),
            ),
        )
        main_change = branch_commit(root, before, {"shared.txt": "alpha\nmain\n"}, "Change beta on main\n")
        land.swap(root, land.branch_of(root), main_change, before, "Change beta on main")
        decisions, _ = series_landing(root, scratch, "clash", passing([]))
        said = " ".join(line.content for line in decisions[1].lines) if 1 in decisions else ""
        expect(
            f"a series whose first commit does not compose on main is refused with the number, the commit and the "
            f"subject of that commit: {said[:200]}",
            decisions[1].status == ExitCode.USAGE
            and "the commit 1 of the 2 commits of the series" in said
            and "'Change beta on the branch'" in said
            and git.head(root) == main_change,
        )
        queue = work / "queue"
        queue.mkdir()
        tip = git.head(root)
        lower = side_commit(root, tip, "lower.txt", "lower\n")
        below = OtherEntry(queue, land.branch_of(root), 1, tip, lower)
        planted_branch(root, "stacked", (({"e.txt": "e\n"}, "Add e\n"), ({"f.txt": "f\n"}, "Add f\n")))
        request = land.series_request(root, "stacked", scratch)
        batch = [land.Queued(3, root, request, options())]
        stacked: dict[int, land.Decision] = {}
        seen = []

        def lower_lands(seconds: float) -> None:
            """Land the batch below on the first poll of the wait before the swap."""
            if seconds == land.POLL_S and below.is_live:
                land.swap(root, land.branch_of(root), lower, tip, "Lower")
                below.leave()

        def decide_stacked(queued: land.Queued, decision: land.Decision) -> None:
            stacked[queued.number] = decision

        right = land.RightOfWay(queue, 3, lower_lands)
        landing = land.BatchLanding(
            batch, passing(seen), decide_stacked, lower_lands, right=right, stack=land.Stack(queue, 3)
        )
        testing.captured(lambda: landing.land(batch))
        expect(
            "a series on the stack composes on the commit of the batch below, and lands on it as one request",
            stacked[3].status == 0
            and len(seen) == 1
            and seen[0][0].head == lower
            and subjects_since(root, lower) == ["Add e", "Add f"],
        )


def series_words_cases(expect: testing.Expect) -> None:
    """Hold the words of --series, the merges of a series, an empty series, and the queue file of a series."""
    with tempfile.TemporaryDirectory(prefix="land-series-words-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        scratch = work / "patches"
        scratch.mkdir()
        refusals = []
        for words in (
            ["--series", "feature", "-m", "A message", "--"],
            ["--series", "feature", "--", "own.txt"],
            ["--series", "feature", "--patch", "x.patch", "--"],
            ["--series", "one", "--series", "two", "--"],
        ):
            try:
                land.parse(words, root)
                refusals.append(False)
            except land.LandError:
                refusals.append(True)
        _, options = land.parse(["--series", "feature", "--all-configurations", "--"], root)
        expect(
            "--series takes no -m, no own path, no patch and no second --series, and it keeps the other options",
            refusals == [True, True, True, True] and options.series == "feature" and options.is_every,
        )
        feature = branch_commit(root, head, {"shared.txt": "alpha\nfeature\n"}, "Change beta on the feature\n")
        on_main = branch_commit(root, head, {"shared.txt": "alpha\nmain\n"}, "Change beta on main\n")
        land.swap(root, land.branch_of(root), on_main, head, "Change beta on main")
        resolved = branch_commit(root, feature, {"shared.txt": "alpha\nresolved\n"}, "Resolve\n")
        tree = git.run(root, "rev-parse", f"{resolved}^{{tree}}").decode().strip()
        merge = git.commit_tree(root, tree, [feature, on_main], "Merge main into the feature\n")
        git.run(root, "update-ref", "refs/heads/feature", merge)
        try:
            land.series_request(root, "feature", scratch)
            said = ""
        except land.LandError as problem:
            said = str(problem)
        expect(
            f"a merge that resolves a conflict refuses the series, and the refusal tells how to rebase: {said[:160]}",
            f"the merge {merge[: land.SHORT_HASH]} of the series holds a change of its own" in said
            and "Rebase feature onto main (git rebase main), resolve each conflict in its own commit" in said,
        )
        clean = branch_commit(root, head, {"g.txt": "g\n"}, "Add g\n")
        union = branch_commit(root, clean, {"shared.txt": "alpha\nmain\n"}, "Union\n")
        tree = git.run(root, "rev-parse", f"{union}^{{tree}}").decode().strip()
        clean_merge = git.commit_tree(root, tree, [clean, on_main], "Merge main into the clean branch\n")
        git.run(root, "update-ref", "refs/heads/clean", clean_merge)
        request = land.series_request(root, "clean", scratch)
        expect(
            "a merge of main with no change of its own gives no commit of the series",
            [part.commit for part in request.parts] == [clean] and request.parts[0].subject == "Add g",
        )
        try:
            land.series_request(root, "HEAD", scratch)
            said = ""
        except land.LandError as problem:
            said = str(problem)
        expect("a branch that main holds gives an empty series, which is refused", "the series is empty" in said)
        status, printed = testing.captured(
            lambda: land.preview(root, request, land.Options(True, False, series="clean"))
        )
        expect(
            "--dry-run of a series prints each commit that it composes and the diff of the series, and lands nothing",
            status == 0 and "the series composes" in printed and "+g" in printed and git.head(root) == on_main,
        )
        queue = work / "queue"
        queue.mkdir()
        with land.entered(queue, root, request, options) as queued:
            files = sorted(path.name for path in queue.iterdir() if path.name.startswith(queued.name))
            found = land.waiting_requests(queue, no_pause)
        expect(
            f"the queue holds a series in the file NUMBER.series and no file NUMBER.request, and a holder reads it with "
            f"its commits: {files}",
            f"{queued.name}{land.SERIES_SUFFIX}" in files
            and f"{queued.name}{land.REQUEST_SUFFIX}" not in files
            and [item.request.parts[0].commit for item in found] == [clean]
            and found[0].request.parts[0].message == "Add g\n",
        )


def series_cost_cases(expect: testing.Expect) -> None:
    """Hold the cost of a series: five commits run the gate one time, and five requests of one commit five times."""
    with tempfile.TemporaryDirectory(prefix="land-series-cost-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        scratch = work / "patches"
        scratch.mkdir()
        planted_branch(root, "five", [({f"s{index}.txt": f"{index}\n"}, f"Add s{index}\n") for index in range(5)])
        seen: list[tuple[gate.Candidate, bool]] = []
        decisions, _ = series_landing(root, scratch, "five", passing(seen))
        series_runs = len(seen)
        series_subjects = subjects_since(root, head)
        statuses = [decisions[1].status]
        single_head = git.head(root)
        seen = []
        for index in range(5):
            (root / f"t{index}.txt").write_text(f"{index}\n", encoding="utf-8")
            landed_one, _ = batch_landing(root, [request_of((f"Add t{index}",), own=(f"t{index}.txt",))], passing(seen))
            statuses.append(landed_one[1].status)
        expect(
            f"a series of five commits runs the gate {series_runs} time, and five requests of one commit, one after the "
            f"other, run it {len(seen)} times",
            series_runs == 1
            and len(seen) == 5
            and series_subjects == [f"Add s{index}" for index in range(5)]
            and len(subjects_since(root, single_head)) == 5
            and statuses == [0] * 6,
        )


@contextlib.contextmanager
def waiting(
    queue: Path, root: Path, request: land.Request, clock: Callable[[], float] = time.time
) -> Iterator[land.Queued]:
    """Write a request into the queue as a waiting process does, at the time of the clock, and hold its lock."""
    with land.entered(queue, root, request, options(), clock) as queued:
        yield queued


def cited_log_cases(expect: testing.Expect) -> None:
    """Hold the logs of a verdict: the run that an unread verdict cites survives the sweep of the runs until the read.

    The holder writes a verdict that names the logs of the first run, and then more than RUNS_KEPT runs end.  The
    process of the request reads the verdict later and leaves the queue (the logs of assay.loop.gate).
    """
    with tempfile.TemporaryDirectory(prefix="land-cited-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        planted_repository(root)
        queue = land.queue_of(work, root)
        with gate.hold_run(work) as cited:
            pass
        logs = work / gate.LOGS / cited
        with waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as queued:
            refusal = land.refused(
                ExitCode.FINDINGS, f"loop land: REJECTED, tsan: red alone too: guard_plugin, see {logs}"
            )
            land.write_decision(queue, queued, refusal)
            for _ in range(gate.RUNS_KEPT + 2):
                with gate.hold_run(work, land.cited_runs(work)):
                    pass
            is_kept = logs.is_dir() and land.cited_runs(work) == frozenset({cited})
            decision = land.read_decision(queue, queued.number, queued.token)
        with gate.hold_run(work, land.cited_runs(work)):
            pass
        expect(
            "the run that an unread verdict cites survives the sweep of more than RUNS_KEPT later runs, the process "
            "then reads the verdict with its logs, and after it leaves the queue the next sweep removes the run",
            is_kept
            and decision is not None
            and str(logs) in decision.lines[0].content
            and land.cited_runs(work) == frozenset()
            and not logs.exists(),
        )


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
        waiting(queue, root, request_of(("Add r1",), own=("r1.txt",)), lambda: FIRST_ENTRY_S) as first,
        waiting(queue, root, request_of(("Add r2",), own=("r2.txt",)), lambda: SECOND_ENTRY_S) as second,
    ):
        status, printed = testing.captured(
            lambda: land.landing(
                work,
                root,
                request_of(("Add r3",), own=("r3.txt",)),
                options(),
                lambda held_work, held_queue, lane: land.hold(held_work, held_queue, passing(seen), no_pause, lane),
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
        and records[-1]["base"] is False
        and records[-1]["lane"] == 1,
    )
    entry_times = [item.get("queued") for item in records[-1]["requests"]]
    expect(
        "the record of the run gives the entry time of each request into the queue: the two times of the clocks of "
        "the two waiting requests, and an ISO 8601 time in UTC for the own request of the holder",
        entry_times[:2] == [FIRST_ENTRY, SECOND_ENTRY]
        and isinstance(entry_times[2], str)
        and len(entry_times[2]) == len(FIRST_ENTRY)
        and entry_times[2].endswith("Z"),
    )
    hold_drop_cases(expect, work, root)
    claim_cases(expect, work, root)


def claim_cases(expect: testing.Expect, work: Path, root: Path) -> None:
    """Hold the lanes: a holder takes no request that a different holder claims, and no batch beside an earlier copy."""
    queue = land.queue_of(work, root)
    (root / "c1.txt").write_text("claimed\n", encoding="utf-8")
    (root / "c2.txt").write_text("free\n", encoding="utf-8")
    head = git.head(root)
    seen: list[tuple[gate.Candidate, bool]] = []
    with (
        waiting(queue, root, request_of(("Add c1",), own=("c1.txt",))) as first,
        waiting(queue, root, request_of(("Add c2",), own=("c2.txt",))) as second,
        open(land.queue_file(queue, first.number, land.CLAIM_SUFFIX), "a", encoding="ascii") as other_claim,
    ):
        fcntl.flock(other_claim, fcntl.LOCK_EX)
        took, _ = testing.captured(lambda: land.hold(work, queue, passing(seen), no_pause, 1))
        decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second)]
        expect(
            "a holder of a second lane takes no request that a different holder claims, and lands the other one",
            took
            and decisions[0] is None
            and decisions[1] is not None
            and decisions[1].status == 0
            and subjects_since(root, head) == ["Add c2"],
        )
        fcntl.flock(other_claim, fcntl.LOCK_UN)
        request_file = land.queue_file(queue, first.number, land.REQUEST_SUFFIX)
        record = json.loads(request_file.read_text(encoding="utf-8"))
        record.pop("claims")
        request_file.write_text(json.dumps(record) + "\n", encoding="utf-8")
        later, _ = testing.captured(lambda: land.hold(work, queue, passing(seen), no_pause, 1))
        expect(
            "a holder of a second lane takes no batch while a request of an earlier copy waits",
            not later and land.read_decision(queue, first.number, first.token) is None,
        )
        first_lane, _ = testing.captured(lambda: land.hold(work, queue, passing(seen), no_pause, 0))
        expect(
            "the holder of the first lane takes the request of an earlier copy",
            first_lane and land.read_decision(queue, first.number, first.token) is not None,
        )
    deferred_claim_cases(expect, work)


def deferred_claim_cases(expect: testing.Expect, work: Path) -> None:
    """Hold the claim of a request that waits for the next batch: the holder frees it at the cut (the lanes).

    The planted gate reads the claim of the second request while the run of the first batch goes on, as the holder of
    a different lane reads it.
    """
    root = work / "repo-deferred"
    head = planted_repository(root)
    queue = land.queue_of(work, root)
    seen: list[tuple[gate.Candidate, bool]] = []
    claimed_in_run: list[bool] = []
    first_request = request_of(("Change one",), (land.Edit("lib.cpp", land.REPLACE, "one\n", "uno\n"),))
    second_request = request_of(("Change two",), (land.Edit("lib.cpp", land.REPLACE, "two\n", "dos\n"),))
    with waiting(queue, root, first_request) as first, waiting(queue, root, second_request) as second:

        def reads_claim() -> None:
            """Keep if a holder claims the second request during the run of the first batch."""
            claimed_in_run.append(land.is_claimed(queue, second.number))

        took, printed = testing.captured(lambda: land.hold(work, queue, passing(seen, reads_claim), no_pause, 1))
        expect(
            "a holder frees the claim of a request that waits for the next batch at the cut, so the holder of a "
            "different lane can take it while the first batch runs, and the first batch gives it no verdict",
            took
            and "wait for the next batch" in printed
            and claimed_in_run == [False]
            and land.read_decision(queue, first.number, first.token) is not None
            and land.read_decision(queue, second.number, second.token) is None
            and subjects_since(root, head) == ["Change one"],
        )


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
    """Hold the error of a run: an error of the check on one request, and an error of the run of the base too."""
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
            and len(seen) == 7
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


def hold_guarded(
    work: Path, queue: Path, check: land.Check, pause: Callable[[float], None] = no_pause
) -> tuple[bool, str]:
    """Run one holder with a planted gate, and return if it took a batch and the text of an error that it raised."""
    try:
        took, _printed = testing.captured(lambda: land.hold(work, queue, check, pause))
    except Exception as problem:  # noqa: BLE001
        return False, repr(problem)
    return took, ""


def queue_defect_cases(expect: testing.Expect) -> None:
    """Hold an error of the read of the queue, before the holder knows its batch (the holder)."""
    with tempfile.TemporaryDirectory(prefix="land-queue-defect-") as work_text:
        work = Path(work_text) / "work"
        root = Path(work_text) / "repo"
        head = planted_repository(root)
        queue = land.queue_of(work, root)
        _stuck_number, stuck = land.reserve(queue)

        def failing(_seconds: float) -> None:
            """Raise an error of the code of the queue in the wait for a request file."""
            raise RuntimeError("a defect of the read of the queue")

        try:
            with (
                waiting(queue, root, request_of(("Add r1",), own=("r1.txt",))) as first,
                waiting(queue, root, request_of(("Add r2",), own=("r2.txt",))) as second,
            ):
                took, raised = hold_guarded(work, queue, passing([]), failing)
                decisions = [land.read_decision(queue, item.number, item.token) for item in (first, second)]
                again = land.waiting_requests(queue, no_pause)
        finally:
            os.close(stuck)
        statuses = [decision.status if decision is not None else None for decision in decisions]
        expect(
            f"an error of the read of the queue gives each live request a tool error with the traceback, the branch "
            f"does not move, and the next holder takes no request: statuses {statuses}, error {raised!r}",
            took
            and not raised
            and statuses == [ExitCode.TOOL_FAILED, ExitCode.TOOL_FAILED]
            and all(
                decision is not None
                and any(
                    "RuntimeError: a defect" in line.content and "Traceback" in line.content for line in decision.lines
                )
                for decision in decisions
            )
            and git.head(root) == head
            and again == [],
        )


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
        run = land.Run(candidate, (1, 2), ("A subject", "B subject"), (FIRST_ENTRY, ""), verdict)
        base = land.Run(gate.Candidate("a" * 40, "a" * 40, ("x.txt",)), (), (), (), verdict)
        land.write_records(work, "land", [land.RunRecord(run, (land.LANDED, land.REFUSED)), land.RunRecord(base, ())])
        land.write_records(work, "land", [])
        lines = (work / land.RECORDS).read_text(encoding="utf-8").splitlines()
        record = json.loads(lines[0])
        expect(
            "a run of the gate gives one record with its commits, each request with its entry time and its result, "
            "its configurations and its logs, a run of the base gives a record with no request, and no run gives no "
            "record",
            len(lines) == 2
            and record["mode"] == "land"
            and record["commit"] == "b" * 40
            and record["base"] is False
            and record["requests"]
            == [
                {"number": 1, "queued": FIRST_ENTRY, "subject": "A subject", "result": "landed"},
                {"number": 2, "queued": "", "subject": "B subject", "result": "refused"},
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


def only_first_lane(work: Path, lanes: contextlib.ExitStack) -> None:
    """Hold each lane of a work directory after the first one in the stack, so the planted processes share one lane.

    The gate gives the lanes.  A case of one lane then holds the batch of one holder.
    """
    for lane in range(1, gate.LANES):
        lanes.enter_context(gate.tree_lock(work, wait=False, lane=lane))


def process_cases(expect: testing.Expect) -> None:
    """Hold landings of planted processes: one batch, a holder that ends, an interrupt, and a holder of an earlier copy."""
    with tempfile.TemporaryDirectory(prefix="land-process-") as work_text, contextlib.ExitStack() as lanes:
        base = Path(work_text)
        work, root, log = base / "work-batch", base / "repo-batch", base / "batch.log"
        only_first_lane(work, lanes)
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
        only_first_lane(work, lanes)
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
        interrupt_cases(expect, base, lanes)
        revert_process_cases(expect, base, lanes)


def interrupt_cases(expect: testing.Expect, base: Path, lanes: contextlib.ExitStack) -> None:
    """Hold an interrupt of a waiting landing in the gate, and a holder of the warm tree of an earlier copy."""
    work, root, log = base / "work-interrupt", base / "repo-interrupt", base / "interrupt.log"
    only_first_lane(work, lanes)
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
    only_first_lane(work, lanes)
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


def revert_process_cases(expect: testing.Expect, base: Path, lanes: contextlib.ExitStack) -> None:
    """Hold processes with a patch of a file and an own path with an earlier copy of it: in one batch and after a swap.

    The copy of the own path has one more line, and the patch does not apply to it.
    """
    work, root, log = base / "work-revert", base / "repo-revert", base / "revert.log"
    only_first_lane(work, lanes)
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
    only_first_lane(work, lanes)
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
            merge_cases,
            count_sum_cases,
            registry_merge_cases,
            budget_reason_cases,
            lower_cases,
            built_cases,
            built_race_cases,
            ratchet_rows_cases,
            flaky_cases,
            confirmation_cases,
            drop_cases,
            store_cases,
            gone_watch_cases,
            dead_holder_cases,
            right_cases,
            stack_cases,
            lost_run_cases,
            newer_entry_cases,
            line_cases,
            stack_thread_cases,
            series_cases,
            series_words_cases,
            series_cost_cases,
            queue_cases,
            cited_log_cases,
            error_cases,
            defect_cases,
            queue_defect_cases,
            journal_cases,
            record_cases,
            heavy_lock_cases,
            process_cases,
        ),
    )
