"""land — compose the commits of the waiting landings, test them one time in the gate, and move main by compare-and-swap.

THE REQUEST
    The words before the word -- give the message and the edits.  The words
    after it give the own paths, and that list can be empty:
      -m TEXT                   One paragraph of the message.  The first paragraph is the subject
      --patch FILE              A unified diff that git made against HEAD
      --replace FILE OLD NEW    In the shared FILE, the text of the file NEW replaces the text of the file OLD
      --block FILE AFTER BLOCK  In the shared FILE, the text of the file BLOCK goes after the text of the file AFTER
      `--grow` RULE REASON      The change adds rows to the ledger of RULE, and REASON tells why (THE GROWTH)
      `--keep` RULE REASON      The gate writes the measured value of each row of RULE that grew, with REASON (THE KEPT ROWS)
      --jobs N                  The jobs of each build and each test run of the gate, else DEFAULT_JOBS
      --all-configurations      The gate runs each configuration (assay.loop.gate, THE SELECTION)
      --check                   Run the gate on the commit, and do not land it
      --here                    With --check: run this copy of the module (THE DRIVER)
      --dry-run                 Print the diff of the commit, and change nothing
      --tree                    Print the object name of the tree of the commit, and change nothing
    The value of an option can be the word --.  The command then reads the
    options in sequence to find the end of the options (split_words()).  The
    files of --patch, --replace and --block are paths from the current
    directory.  An own path and the shared FILE are paths from the top of the
    repository of the current directory.  --check with no edit, patch or own
    path runs the gate on HEAD.

THE TREE
    The tree of the commit is the commit before it with the working-tree copy
    of each own path.  It also holds each shared file as the commit before it
    holds it, with the edits.  When an own path is gone from the working tree,
    the tree does not hold it.  A shared file does not come from the working
    tree.  The commit then cannot hold an edit of a different writer that is
    not committed, or an earlier copy of a shared file.  The text that
    --replace replaces and the text that --block writes after must each occur
    one time in the file.  An edit that the commit before holds is not
    applied again.  Some edits can name one file, and they apply in the given
    sequence.  An own path cannot be a shared file.  Each path that the commit
    changes is an own path, a file in an own directory, or a shared file.
    Else the request does not compose.

    The request file holds the commit of HEAD of the work tree at the
    request (THE QUEUE).  A different commit can land after that commit and
    change a file of an own path.  The working-tree copy can be without that
    change, and the commit of the request can revert it.  A request does not
    compose when it changes a file of an own path that is not the same in the
    two commits.  A request with no such commit compares with the commit that
    its batch started from.

THE COMPOSITION
    The command composes the tree in the index of a scratch repository.  That
    repository writes into the object directory of the repository (THE
    BORROWED INDEX of assay.core.git).  Each blob, tree and commit that the composition
    writes is then in the repository.  The scratch repository reads the own
    paths from the work tree of the request through --work-tree.

THE GROWTH
    A row `utils/ledgers/RULE.txt | grow | admit | COMMIT | REASON` of the
    ledger of the rule history names the commit that added rows to the
    ledger of RULE (assay.guard.engines.meta.history, THE GROWTH).  A commit
    cannot hold its own name.  The row then goes into a second commit on the
    first.  With `--grow`, the first commit holds the change.  The second
    commit adds one such row for each RULE, with COMMIT the first commit in
    9 characters.  The change must change the ledger of each RULE.  The two
    commits are the commits of the request in its batch.  The branch then
    does not hold the added rows with no such row.

THE KEPT ROWS
    A measured rule of the policy max keeps an item over its warning
    threshold with a row, up to the value of the row.  A change can make
    such an item larger in a configuration that its author does not build.
    An example is a bench object of the build bench-fuzz.  With `--keep`
    RULE REASON, the author gives the reason of that growth with the
    request.  The gate then writes the measured value of each row of RULE
    that rose, with REASON as its reason (THE KEPT ROWS of assay.loop.gate).

    The ledger goes into the commit of THE FALLS.  An item over the error
    threshold with no row stays an error.  RULE must be a measured rule of
    the policy max of the engine build.  A request with `--keep` lands in a
    batch of its own (THE BATCH).  Each raised row is then a row of that
    request.  The verdict and the record give each raised row.

THE QUEUE
    A landing writes its request into the queue of its repository before it
    waits for the lock of the warm tree.  The queue is the directory
    QUEUE/KEY of the work directory.  KEY is the first KEY_LENGTH digits of
    the SHA-256 of the path of the shared git directory of the repository.
    The requests of two repositories then do not mix.

    The request gets a new number.  The file NEXT of the queue holds the next
    number, and an exclusive lock of flock(2) on that file keeps two
    reservations apart.  The number is also above the number of each file of
    the queue.  Each number occurs one time only, and a verdict then cannot
    go to a different request.  The process locks a new file, and it links
    that file to the file NUMBER.lock.

    The process holds that exclusive lock of flock(2) while it waits.  Then
    it writes a copy of each patch and the file NUMBER.request.  That file
    holds the message, the edits, the own paths, the rows of THE GROWTH and
    of THE KEPT ROWS, and the options.  It also holds the work tree, its
    branch, the commit of its HEAD, its shared git directory and a random
    token.  The verdict of the request
    holds the same token, and the process reads no verdict with a different
    token.

    The kernel removes the lock when the process ends, also on a signal.  A
    process that ends removes its request file first and its lock file last.
    The next holder of the tree removes each file of a request with no lock.

THE WAIT
    The landing waits for one of two events, and it looks again each POLL_S
    seconds.  When the queue holds the file NUMBER.verdict with its token,
    the landing reads that verdict.  It gives its work tree the update of the
    verdict (THE INDEX AND THE WORKING TREE).  Then it writes the lines of the
    verdict and exits with its status.  When the landing gets the lock of the warm tree, it is
    the holder of the tree (THE HOLDER).  flock(2) wakes a waiter in no
    specified sequence, but the holder takes the requests in the sequence of
    their numbers.

    A landing of an earlier copy of this module writes no request.  It waits
    for the same lock with flock(2), and it runs alone when it gets the lock.
    A request then cannot land two times, because only a holder of the lock
    reads the queue.

THE HOLDER
    The holder of the tree takes the requests of the queue in the sequence
    of the numbers.  It takes each request with no verdict and with a
    process that holds its lock.  A request with a lock and no request file
    can come next.  The holder waits INCOMPLETE_S seconds for its request
    file, and then it takes the requests after it.  It takes the requests with
    the git directory and the branch of the first one, and the others wait
    for the next holder.  The batch reads and moves the branch, and runs the
    gate, through the shared git directory, and not through the work tree of
    a request.

    A request with `--keep` ends the batch before it, unless it is the first
    request.  A first request with `--keep` is the only request of its
    batch.  The holder lands the batch (THE BATCH).  It writes the verdict
    of a request only while the process of the request holds its lock.  The
    holder keeps the number of each request with a verdict, and it gives no
    request a second verdict.

    While it lands the batch, it holds the session of the gate.  That session
    goes before each other heavy step of the host (THE PRIORITY OF THE GATE
    of assay.loop.heavy).  --check holds that session too.  Then it frees the
    lock.  It does that again while its own request has no verdict.  An
    error of a tool gives each request of the batch with no verdict the
    verdict of that error, other than a request that landed.

THE BATCH
    The batch composes each request as its own commits on the commits of the
    request before it.  A request that does not compose on them gets a
    refusal for that reason only.  An example is an own path with a file that
    an earlier request of the batch changes (THE TREE).  The first
    composition also cuts the batch.  A request that changes a C++ file that
    an earlier request of the batch changes waits for the next batch, with
    each request after it.

    The rule quarantine-lines of the gate judges the hunks of the full change
    of the batch.  It keys a removed finding on its file (assay.guard.engines.plugin.quarantine_lines).
    With no C++ file in two requests, the judgment of the batch is the judgment of each request alone.
    Then a hunk of one request that removes a finding cannot admit a finding
    of a different request.

    The gate builds and tests the last commit one time (THE GATE).  The
    candidate of the gate is that commit, with the commit that the batch
    started from and the changed paths of each request.  The gate then runs
    each configuration that one or more of the requests selects.  The batch
    runs the gate with the smallest --jobs of its requests, and with
    --all-configurations when one request gives it.  When the gate accepts
    the last commit, one compare-and-swap moves the branch to the commit of
    the gate (THE COMPARE-AND-SWAP).

THE SEARCH
    When the gate rejects the last commit, a binary search finds each request
    that the gate rejects.  The batch keeps an accepted part: the first
    requests, in a chain that the gate accepted as a unit.  It also keeps the
    length of the smallest red part of the other requests.  Each run of the
    gate tests the accepted part and the first half of that red part.  An
    accepted run adds that half to the accepted part.

    A rejected run makes that half the red part.  A red part of one request
    is red alone on the accepted part, and that request gets the refusal with
    the problems of its run.  The other requests then compose again on the
    accepted part, and the gate tests them as a unit.  When only an
    interaction of two requests is red, the refusal goes to the second of the
    two.

    At the end, the compare-and-swap moves the branch to the accepted part.
    The branch then holds no commit that the gate did not accept as a unit.
    Each run after the first run of a batch runs `loop test` with the inputs
    that changed (assay.loop.gate).

THE RUN OF THE BASE
    The first run of a batch can be red, also for a batch of one request.
    The gate then runs on the commit of the batch with no request, with the
    changed paths of the batch.  It then runs the configurations of the
    batch.  The run of the base is a retry.  `loop test` then runs only the
    tests with an input that changed.  When that run is red too, the branch
    is red with no request.

    The problems of a run name the directory of its logs.  The batch
    compares the problems of two runs with LOGS_MARK as the replacement of
    that directory.  When the first run of the batch has each problem of the run
    of the base, no request repairs the branch.  Each request then gets the
    refusal with the problems of the two runs, and the search does not run.
    Else a request can repair the branch, and the search runs.  A refusal
    with only problems of the run of the base tells that the branch is red.

    A search on a red branch runs the gate approximately two times for each
    request.  An example is a batch of three requests with one red request.
    The batch runs the gate 4 times for the first or the last red request,
    and 5 times for the second red request.  With no red request, the batch
    runs the gate one time.  On a red branch, it runs the gate 2 times.  One
    red request alone runs the gate 2 times.

THE CHECKS BEFORE THE BUILDS
    Before the first run of the gate, the batch runs the rule history on the
    warm tree (Gate.history_problems() of assay.loop.gate).  It runs the rule
    at the commit of the batch and at the last commit of each request.  It
    runs the function findings() of the rule and judges it against the
    ledger of the rule, as `guard check --rule history` does.  A request with
    `--grow` also gets growth_kinds() of the rule for its first commit, and
    the kinds must hold a `grow` row.

    The first request with an error that the commit before it does not have
    gets the refusal in seconds, with no build.  Examples are a `grow` row
    that admits nothing, and a `grow` row that a change makes stale.  The
    other requests then compose again, and the check continues at the
    position of the refused request.  The batch does no such check when the
    rule history is red at the commit of the batch.  It also does no such
    check when a request changes PACKAGE_PREFIX, because the module of the
    rule can then be different.

    The scope of the rule is the registry, the ledgers, the rule table and
    the plugin.  A request with no row of THE GROWTH and no change in that
    scope gets no run of the rule.

THE GATE
    The gate builds and tests a commit in the warm tree of each configuration
    that the change selects (assay.loop.gate).  The gate has no base side.  A
    test that is red alone on HEAD then rejects each landing.  The gate
    writes the falls of the counted ledgers into a new commit (THE FALLS of
    assay.loop.gate).  That commit has the parents and the message of the
    last commit of the batch.

    The verdict gives it, and the compare-and-swap moves the branch to it.
    Each run of the gate writes its logs into a directory of its own
    (assay.loop.gate, THE LOGS).  The problems of a refusal then name logs
    that the next run does not change.

THE COMPARE-AND-SWAP
    The branch moves to the commit only when the branch holds the commit that
    the batch started from (`git update-ref REF NEW OLD`).  A different
    commit can land first, such as a commit of a landing of an earlier copy
    of this module.  The batch then composes its accepted requests again on
    the new commit of the branch, after a random pause, and the gate tests
    them again.  After ATTEMPTS refusals, each of them gets the refusal.  A
    landing then cannot revert a different commit.

    Before the swap, the holder drops each accepted request with a process
    that left the queue.  An interrupt of a landing then cancels it, and the
    others compose again and go to the gate again.  The holder keeps the
    lock of the warm tree for all of the batch.

THE JOURNAL
    Before the swap, the holder writes the file NUMBER.journal of each
    accepted request.  The file holds the token, the branch, the commit of
    the swap and the verdict of the request.  After the swap, the holder
    writes each verdict.  When the swap does not occur, it removes the
    journals.

    A holder can end between the swap and the last verdict.  The next holder
    then finds a request with a journal and no verdict.  When the branch
    holds the commit of the journal, that holder gives the work tree the
    update of the journal and writes its verdict.  It does not compose the
    request again.  Else it removes the journal, and it takes the request.
    When git cannot tell if the branch holds that commit, the request gets
    the refusal of a tool error with the reason.  The commit can then be on
    the branch.

THE INDEX AND THE WORKING TREE
    After the swap, the holder gives the work tree of each landed request its
    update.  Then it writes each verdict.  It does that before it frees the
    lock of the warm tree.  A landing that composes after it then reads a
    working tree that holds each landed change.  A landing of an earlier copy
    of this module reads that working tree too.  An error in a work tree
    gives a line of the holder, and it cannot change a verdict.

    The process of each landed request gives its own work tree the same
    update after it reads its verdict, and it writes the warnings.  An update
    that the work tree holds changes nothing.  The update also comes after a
    holder that ended after its swap (THE JOURNAL).  The verdict gives the
    update: the commit and the paths of the index, the two commits of a
    growth, and the falls.  Each update holds a lock of flock(2) on the file
    UPDATE_LOCK of the git directory of its work tree.  Two updates of one
    work tree then do not write its index at one time.

    The index gets the entry of the landed commit for each changed path, and
    the working tree does not change.  Then each patch goes to the working
    tree with git apply, unless the working tree holds it.  The rows of THE
    GROWTH go there in the same way.  The ledgers of THE FALLS go to each
    work tree of the batch one time.  Each edit goes to the working-tree
    copy of its file, unless the copy holds it.

    When a hunk does not apply, the process writes a warning.  The owner of
    the hunk must then apply it by hand before the next commit of that file,
    or that commit reverts it.  An error of git or of a file also gives a
    warning, because the commit is on the branch.  When HEAD of the work
    tree is not on the branch of the request, the work tree gets no update.

THE LOCK OF A HEAVY STEP
    The gate takes the lock of the heavy steps for each of its steps
    (assay.loop.heavy).  A landing in a heavy step then waits for its own
    lock.  The command refuses to run when its environment has the variable
    STEP_VARIABLE of assay.loop.heavy.  It also refuses to run when it has an
    open descriptor of the lock file, as flock(1) gives its command.

THE RECORD
    Each run of the gate adds one line of JSON to the file RECORDS of the
    work directory.  The line holds the time, the commits, the configurations
    and the problems.  It holds the subject and the result of each request of
    the run: landed, refused, accepted, rejected or dropped.  A run of the
    base holds no request.  It holds the commit that the gate judged last and
    the ledgers that it wrote.

    It also holds the wall time, the time of each configuration in its heavy
    step and the logs.  The work directory is the directory land of the work
    root of assay.core.cache.

THE DRIVER
    A landing runs the copy of this module in the main work tree of the
    repository, which holds main.  A different copy runs the copy of the
    main work tree with the same words.  An example is the copy of a work
    tree with a change of this module.  A change of the gate then cannot make
    the gate of its own landing weaker.  --here runs this copy, and it
    applies only to --check.

    --dry-run and --tree land nothing.  They run this copy, with no gate.
    --check does not go into the queue.  It waits for the lock of the warm
    tree and runs alone.

Usage
    loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... [--block FILE AFTER BLOCK]...
              [`--grow` RULE REASON]... [`--keep` RULE REASON]... [--jobs N] [--all-configurations]
              [--check [--here] | --dry-run | --tree] -- [PATH]...

    Exit 0 when the request landed, when the gate accepts the commit of
    --check, or when --dry-run or --tree printed.  Exit 1 when the gate
    rejects the request.  Exit 2 on a usage error, or on a request that does
    not compose.  Exit 4 when a tool fails.

`guard selftest tests.loop.test_land` does a test of the module on a planted repository.
"""

import contextlib
import fcntl
import functools
import hashlib
import json
import os
import random
import secrets
import sys
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import asdict, dataclass, replace
from pathlib import Path

from assay.core import cache, git, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import gate, heavy
from assay.loop import history as loop_history

# The refusals of the compare-and-swap before the batch stops, and the range of the random pause after one (THE
# COMPARE-AND-SWAP).
ATTEMPTS = 8
PAUSE_RANGE_S = (0.1, 0.9)
# The two kinds of an edit of a shared file (THE REQUEST).
INSERT = "insert"
REPLACE = "replace"
# The count of the values of each option, for the division of the words (split_words()).
VALUE_COUNTS = {"-m": 1, "--patch": 1, "--replace": 3, "--block": 3, "--grow": 2, "--keep": 2, "--jobs": 1}
FLAG_DRY_RUN = "--dry-run"
FLAG_TREE = "--tree"
FLAG_CHECK = "--check"
FLAG_HERE = "--here"
FLAG_EVERY = "--all-configurations"
FLAGS = (FLAG_DRY_RUN, FLAG_TREE, FLAG_CHECK, FLAG_HERE, FLAG_EVERY)
END_OF_OPTIONS = "--"
HELP_WORDS = (["-h"], ["--help"])
USAGE = (
    "usage: loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... "
    "[--block FILE AFTER BLOCK]...\n"
    "                 [--grow RULE REASON]... [--keep RULE REASON]... [--jobs N] [--all-configurations]\n"
    "                 [--check [--here] | --dry-run | --tree] -- [PATH]..."
)
# The subject of the commit of --check with no message.
CHECK_SUBJECT = "Check a change in the gate"
# The ledger of the rule history, which holds the rows of THE GROWTH.
HISTORY_LEDGER = rule_ledger.shown_path("history")
# The length of a commit in a line of the landing.
SHORT_HASH = 9
# The work directory of the gate, in the work root, and the file of the records in it (THE RECORD).
WORK_NAME = "land"
RECORDS = "records.jsonl"
# The directory of the queues in the work directory, and the length of the key of the queue of a repository (THE
# QUEUE).
QUEUE = "queue"
KEY_LENGTH = 16
# The file of the next number in a queue, and the new lock of a reservation (THE QUEUE).
NEXT = "next"
RESERVE = ".reserve"
# The files of one request in its queue (THE QUEUE, THE JOURNAL).
NUMBER_WIDTH = 8
LOCK_SUFFIX = ".lock"
REQUEST_SUFFIX = ".request"
VERDICT_SUFFIX = ".verdict"
JOURNAL_SUFFIX = ".journal"
PATCH_MARK = ".patch-"
REQUEST_FORMAT = 1
# The bytes of the random token of a request (THE QUEUE).
TOKEN_BYTES = 8
# The interval of the wait for a verdict or for the lock, in seconds (THE WAIT).  The wait for the request file of a
# request with a lock, and its interval, in seconds (THE HOLDER).
POLL_S = 2.0
INCOMPLETE_S = 5.0
INCOMPLETE_POLL_S = 0.1
# The lock file of the update of a work tree, in its git directory (THE INDEX AND THE WORKING TREE).
UPDATE_LOCK = "loop-land.lock"
# The directory of the open descriptors of this process (THE LOCK OF A HEAVY STEP).
DESCRIPTORS = Path("/proc/self/fd")
# The copy of this module in a tree, and the master that runs it (THE DRIVER).
MODULE_PATH = Path("utils/assay/loop/land.py")
MASTER_PATH = Path("utils/bin/loop")
# The limit of a landing that the driver runs from the main work tree, in seconds.
DRIVER_S = 8 * proc.BUILD_S
# The directory of the package of the rule history.  A batch that changes it gets no check before the builds (THE
# CHECKS BEFORE THE BUILDS).
PACKAGE_PREFIX = "utils/assay/"
# The results of a request in one run of the gate (THE RECORD).
LANDED = "landed"
REFUSED = "refused"
ACCEPTED = "accepted"
REJECTED = "rejected"
DROPPED = "dropped"
# The replacement of the directory of the logs of a run, when the batch compares the problems of two runs (THE RUN OF
# THE BASE).
LOGS_MARK = "LOGS"


class LandError(Exception):
    """The request does not compose: a usage error, an edit that does not apply, or a path that is not listed."""


@dataclass(frozen=True, slots=True)
class Edit:
    """One edit of a shared file: the path from the top of the repository, the kind, and the two texts."""

    path: str
    kind: str
    first: str
    second: str


@dataclass(frozen=True, slots=True)
class Grow:
    """One row of THE GROWTH in a request: the rule of the ledger that the change adds rows to, and the reason."""

    rule: str
    reason: str


@dataclass(frozen=True, slots=True)
class Request:
    """The message, the patches, the edits, the own paths and the rows of THE GROWTH of one landing (THE REQUEST).

    `keeps` holds the rows of `--keep` (THE KEPT ROWS).
    """

    messages: tuple[str, ...]
    patches: tuple[Path, ...]
    edits: tuple[Edit, ...]
    paths: tuple[str, ...]
    grows: tuple[Grow, ...] = ()
    keeps: tuple[gate.Keep, ...] = ()

    @property
    def message(self) -> str:
        """The message of the commit: the paragraphs with one empty line between two of them, and a newline at the end."""
        return "\n\n".join(self.messages or (CHECK_SUBJECT,)) + "\n"

    @property
    def subject(self) -> str:
        """The first paragraph of the message."""
        return self.messages[0] if self.messages else CHECK_SUBJECT

    @property
    def is_empty(self) -> bool:
        """Tell if the request holds no patch, no edit and no own path."""
        return not self.paths and not self.edits and not self.patches


@dataclass(frozen=True, slots=True)
class Options:
    """The words of the command line that are not part of the request."""

    is_dry_run: bool
    is_tree_only: bool
    is_check: bool = False
    is_here: bool = False
    is_every: bool = False
    jobs: int = proc.DEFAULT_JOBS


@dataclass(frozen=True, slots=True)
class Composed:
    """The result of one composition: the commit that it starts from, the tree, the changed paths, and the diff."""

    head: str
    tree: str
    changed: tuple[str, ...]
    diff: str


@dataclass(frozen=True, slots=True)
class Queued:
    """One request of a batch: its number in the queue, the work tree of its process, the request and its options.

    `branch` is the branch of HEAD of the work tree, and `common` is its shared git directory (THE HOLDER).  `token` is
    the random token of the request, and its verdict holds the same token (THE QUEUE).  `head` is the commit of HEAD of
    the work tree at the request, or "" (THE TREE).
    """

    number: int
    root: Path
    request: Request
    options: Options
    branch: str = ""
    common: str = ""
    token: str = ""
    head: str = ""

    @property
    def name(self) -> str:
        """The name of the files of the request in the queue: the number in NUMBER_WIDTH digits."""
        return f"{self.number:0{NUMBER_WIDTH}d}"


@dataclass(frozen=True, slots=True)
class Link:
    """One request composed on the commit before it: the request, that commit, its first and last commit, and its paths.

    The last commit is the second commit of a growth (THE GROWTH), else the first commit.
    """

    queued: Queued
    parent: str
    first: str
    commit: str
    changed: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Line:
    """One line that the process of a request writes: the content, and True for the standard error."""

    content: str
    is_error: bool = False


@dataclass(frozen=True, slots=True)
class Update:
    """The update of the work tree of a landed request (THE INDEX AND THE WORKING TREE).

    The index gets the entry of the commit `index_commit` for each path of `index_paths`.  `grown` holds the first and
    the second commit of a growth, or nothing.  `falls` holds the commit that the gate tested and the commit that
    landed, or nothing, and `ledgers` holds the ledgers of THE FALLS.
    """

    index_commit: str
    index_paths: tuple[str, ...]
    grown: tuple[str, ...] = ()
    falls: tuple[str, ...] = ()
    ledgers: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Decision:
    """The verdict of one request: the exit, the commit of the request that landed or "", the lines, and the update.

    `update` is the update of the work tree of a landed request, else None (THE INDEX AND THE WORKING TREE).
    """

    status: int
    commit: str = ""
    lines: tuple[Line, ...] = ()
    update: Update | None = None


@dataclass(frozen=True, slots=True)
class JournalEntry:
    """The journal of one request before a swap: its token, the branch, the commit of the swap and its verdict."""

    token: str
    branch: str
    final: str
    decision: Decision


@dataclass(frozen=True, slots=True)
class Run:
    """One run of the gate: the candidate, the number and the subject of each request of its chain, and the verdict.

    A run of the base has no request (THE RUN OF THE BASE).
    """

    candidate: gate.Candidate
    numbers: tuple[int, ...]
    subjects: tuple[str, ...]
    verdict: gate.Verdict


@dataclass(frozen=True, slots=True)
class RunRecord:
    """One run of the gate with the result of each of its requests, in the sequence of its chain (THE RECORD)."""

    run: Run
    results: tuple[str, ...]


# The check of the gate: the candidate and the retry flag give the verdict (THE GATE).
type Check = Callable[[gate.Candidate, bool], gate.Verdict]
# The check of the rule history before the builds (THE CHECKS BEFORE THE BUILDS).  It takes the commit, the first
# commit of its request and the rules of the rows of THE GROWTH of that request.  It gives the errors.
type Early = Callable[[str, str, tuple[str, ...]], tuple[str, ...]]
# The function that receives the verdict of each request (THE HOLDER).
type Decide = Callable[[Queued, Decision], None]
# The check that tells if the process of a request holds its lock (THE COMPARE-AND-SWAP).
type IsAlive = Callable[[Queued], bool]
# The holder of the tree: it takes the work directory and the queue, and it tells if a batch was there (THE WAIT).
type Holder = Callable[[Path, Path], bool]


# ── The request ────────────────────────────────────────────────────────────


def split_words(words: Sequence[str]) -> tuple[list[str], list[str]]:
    """Return the words before the first -- that is not the value of an option, and the words after it (THE REQUEST).

    Raises:
        LandError: If no such -- is in the words, or an option has fewer values than it takes

    Complexity: linear in the words.
    """
    index = 0
    while index < len(words) and words[index] != END_OF_OPTIONS:
        count = VALUE_COUNTS.get(words[index], 0)
        if index + count >= len(words):
            raise LandError(f"the option {words[index]} takes {count} value(s), and the words end before them")
        index += count + 1
    if index >= len(words):
        raise LandError("put -- before the own paths.  The list of the paths can be empty")
    return list(words[:index]), list(words[index + 1 :])


def read_text(path: Path) -> str:
    """Return the text of a file of an edit.

    Raises:
        LandError: If the file cannot be read
    """
    try:
        return path.read_text(encoding="utf-8")
    except OSError as problem:
        raise LandError(f"the file {path} of an edit cannot be read: {problem}") from None


def checked_options(flags: dict[str, bool], jobs_values: list[list[str]], request: Request) -> Options:
    """Return the options of the flags and of --jobs, and refuse a combination that does not apply.

    Raises:
        LandError: If the options do not apply together
    """
    if len(jobs_values) > 1:
        raise LandError("give --jobs one time")
    jobs_text = jobs_values[0][0] if jobs_values else None
    if jobs_text is not None and (not jobs_text.isdigit() or int(jobs_text) < 1):
        raise LandError(f"--jobs takes a number above 0, not {jobs_text!r}")
    if sum(flags[flag] for flag in (FLAG_CHECK, FLAG_DRY_RUN, FLAG_TREE)) > 1:
        raise LandError("give one of --check, --dry-run and --tree")
    if flags[FLAG_HERE] and not flags[FLAG_CHECK]:
        raise LandError("--here applies only to --check.  A landing runs the copy of the main work tree")
    if not request.messages and not flags[FLAG_TREE] and not flags[FLAG_CHECK]:
        raise LandError("give the message of the commit with -m")
    if request.is_empty and not flags[FLAG_CHECK]:
        raise LandError("the request holds nothing to commit")
    if request.is_empty and request.grows:
        raise LandError("--grow takes a change of the ledger of its rule")
    jobs = int(jobs_text) if jobs_text is not None else proc.DEFAULT_JOBS
    return Options(flags[FLAG_DRY_RUN], flags[FLAG_TREE], flags[FLAG_CHECK], flags[FLAG_HERE], flags[FLAG_EVERY], jobs)


def parse(argv: Sequence[str], current: Path) -> tuple[Request, Options]:
    """Return the request and the other options of the command line (THE REQUEST).

    The command reads the options itself, and not with argparse, because the value of an option can start with -.

    Args:
        argv: The words after `loop land`
        current: The directory that the paths of --patch, --replace and --block start from

    Raises:
        LandError: If the words are not a request

    Complexity: linear in the words.
    """
    before, own = split_words(argv)
    values: dict[str, list[list[str]]] = {option: [] for option in VALUE_COUNTS}
    flags = dict.fromkeys(FLAGS, False)
    index = 0
    while index < len(before):
        word = before[index]
        if word in flags:
            flags[word] = True
            index += 1
            continue
        if word not in VALUE_COUNTS:
            raise LandError(f"the word {word!r} is not an option of loop land.\n{USAGE}")
        count = VALUE_COUNTS[word]
        values[word].append(before[index + 1 : index + 1 + count])
        index += count + 1
    edits = [
        Edit(path, kind, read_text(current / first), read_text(current / second))
        for kind, option in ((INSERT, "--block"), (REPLACE, "--replace"))
        for path, first, second in values[option]
    ]
    grows = tuple(Grow(rule, reason) for rule, reason in values["--grow"])
    keeps = tuple(gate.Keep(rule, reason) for rule, reason in values["--keep"])
    for option, rule_id, reason in [("--grow", item.rule, item.reason) for item in grows] + [
        ("--keep", item.rule, item.reason) for item in keeps
    ]:
        if not rule_registry.is_rule_id(rule_id) or not reason.strip():
            raise LandError(f"{option} takes the ID of a rule and a reason, not {rule_id!r} and {reason!r}")
    request = Request(
        tuple(item[0] for item in values["-m"]),
        tuple((current / item[0]).resolve() for item in values["--patch"]),
        tuple(edits),
        tuple(own),
        grows,
        keeps,
    )
    return request, checked_options(flags, values["--jobs"], request)


# ── The edits ──────────────────────────────────────────────────────────────


def checked_keeps(root: Path, request: Request) -> None:
    """Refuse a row of `--keep` with a rule that is not a measured rule of the policy max of the engine build.

    Raises:
        LandError: If such a rule is not in the registry of the work tree, or the registry does not read
    """
    if not request.keeps:
        return
    try:
        registry = rule_registry.load(root / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise LandError(f"the registry of {root} does not read: {problem}") from None
    for keep in request.keeps:
        rule = registry.rules.get(keep.rule)
        if rule is None or rule.policy != rule_registry.Policy.MAX or rule.engine != rule_registry.Engine.BUILD:
            raise LandError(f"--keep takes a measured rule of the policy max of the engine build, and not {keep.rule}")


def edit_state(content: str, kind: str, first: str, second: str) -> str:
    """Tell if the content holds the edit ("applied"), can take it ("apply"), or is ambiguous ("ambiguous").

    A replacement is applied when the content holds the replacement, and
    each copy of the replaced text is in a copy of the replacement.  That
    rule also holds when the replacement holds the replaced text.  An edit
    that extends its text then does not apply a second time.
    """
    if kind == INSERT:
        if second in content:
            return "applied"
        return "apply" if content.count(first) == 1 else "ambiguous"
    replaced_count = content.count(first)
    if first in second:
        # The replacement extends the replaced text.  Each applied copy of the replacement then holds the replaced text.
        replacement_count = content.count(second)
        if replacement_count >= 1 and replaced_count == replacement_count * second.count(first):
            return "applied"
        return "apply" if replacement_count == 0 and replaced_count == 1 else "ambiguous"
    # A deletion, a shorter text or a plain replacement is applied when the replaced text is gone.  The content must
    # then hold the replacement, or the replacement is empty.
    if replaced_count == 0 and (second == "" or second in content):
        return "applied"
    return "apply" if replaced_count == 1 else "ambiguous"


def apply_edit(content: str, edit: Edit, where: str) -> str:
    """Return the content with one edit.

    Raises:
        LandError: If the text of the edit does not occur one time in the content
    """
    state = edit_state(content, edit.kind, edit.first, edit.second)
    if state == "applied":
        return content
    if state == "ambiguous":
        what = "anchor" if edit.kind == INSERT else "old text"
        raise LandError(f"the {what} in {where} does not occur exactly 1 time outside the edit")
    if edit.kind == INSERT:
        at = content.index(edit.first) + len(edit.first)
        return content[:at] + edit.second + content[at:]
    return content.replace(edit.first, edit.second, 1)


def edited_paths(request: Request) -> list[str]:
    """Return each shared file of an edit, one time, in the sequence of the edits."""
    return list(dict.fromkeys(edit.path for edit in request.edits))


# ── The composition ────────────────────────────────────────────────────────


def is_under(path: str, items: set[str]) -> bool:
    """Tell if a path is one of the items, or a file in the directory of one of them."""
    return any(path == item or path.startswith(item + "/") for item in items)


def compose(root: Path, request: Request, head: str, with_diff: bool = False, since: str = "") -> Composed:
    """Compose the tree of a request on a commit (THE TREE, THE COMPOSITION).

    Args:
        root: The top of the work tree of the request
        request: The request
        head: The full name of the commit that the tree starts from
        with_diff: Give the diff of the tree against the commit, too
        since: The commit of HEAD of the work tree at the request, or "".  The own paths compare with it

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails, such as a patch that does not apply

    Complexity: one read of the own paths.  When `since` is not `head`, git also compares the trees of the two commits.
    """
    own = {path.rstrip("/") for path in request.paths}
    with git.borrowing_repository(root) as scratch:
        git.run(scratch, "read-tree", head)
        if request.paths:
            git.run(scratch, "--work-tree", str(root), "add", "-A", "--", *request.paths)
        before_patches = git.write_tree(scratch)
        for patch in request.patches:
            git.run(scratch, "apply", "--cached", str(patch))
        after_patches = git.write_tree(scratch)
        patched = git.tree_changes(scratch, before_patches, after_patches)
        shared = list(dict.fromkeys([*edited_paths(request), *patched]))
        overlap = sorted(set(shared) & own)
        if overlap:
            raise LandError(f"{overlap} is both a shared file and an own path")
        for path in edited_paths(request):
            entry = git.tree_entry(root, head, path)
            if entry is None:
                raise LandError(f"the shared file {path} is not in the commit {head[:SHORT_HASH]}")
            stored = git.blobs(root, [git.index_object(scratch, path)])[0]
            if stored is None:
                raise LandError(f"the index of the composition holds no blob of the shared file {path}")
            content = stored.decode("utf-8")
            for edit in request.edits:
                if edit.path == path:
                    content = apply_edit(content, edit, f"{head[:SHORT_HASH]}:{path}")
            written = git.hash_object(root, content.encode("utf-8"))
            git.set_index_entry(scratch, replace(entry, name=written), path)
        changed = git.index_changes(scratch, head)
        allowed = own | set(shared)
        for path in changed:
            if not is_under(path, allowed):
                raise LandError(f"REFUSED, '{path}' changed but is not listed")
        if since and since != head:
            own_changed = {path for path in changed if is_under(path, own)}
            moved = sorted(own_changed.intersection(git.tree_changes(scratch, since, head)))
            if moved:
                raise LandError(
                    f"REFUSED, the own path(s) {moved} changed after the request.  HEAD of the work tree was "
                    f"{since[:SHORT_HASH]} at the request, and the commit {head[:SHORT_HASH]} holds different copies.  "
                    f"The working-tree copies can be without that change, and the commit can then revert it.  Land the "
                    f"request again after the work tree holds the commit {head[:SHORT_HASH]}"
                )
        tree = git.write_tree(scratch)
        diff = git.index_patch(scratch, head) if with_diff else ""
    return Composed(head, tree, changed, diff)


def grow_commit(root: Path, first: str, grows: Sequence[Grow], changed: Sequence[str]) -> str:
    """Write the second commit of a growth on the first commit, and return its full name (THE GROWTH).

    Raises:
        LandError: If the change does not change the ledger of a rule, or the ledger of the rule history does not read
        git.GitError: If a git command fails
    """
    from assay.guard.engines.meta import history

    for grow in grows:
        if rule_ledger.shown_path(grow.rule) not in changed:
            raise LandError(f"--grow {grow.rule}: the change does not change the ledger of the rule {grow.rule}")
    text = git.show(root, first, HISTORY_LEDGER)
    if text is None:
        raise LandError(f"the commit holds no {HISTORY_LEDGER}, and --grow writes its rows there")
    try:
        ledger = rule_ledger.parse(text.decode("utf-8"), HISTORY_LEDGER, history.RULE, rule_registry.Policy.ADMIT)
    except ValueError as problem:
        raise LandError(f"{HISTORY_LEDGER} does not read: {problem}") from None
    reasons = {(grow.rule, history.GROW_KIND): grow.reason for grow in grows}
    rendered = rule_ledger.render(history.with_admissions(ledger, first, reasons))
    blob = git.hash_object(root, rendered.encode("utf-8"))
    entry = git.tree_entry(root, first, HISTORY_LEDGER)
    if entry is None:
        raise LandError(
            f"the commit {first[: history.SHORT_HASH]} holds no {HISTORY_LEDGER}, and --grow writes its rows there"
        )
    with git.borrowing_repository(root) as scratch:
        git.run(scratch, "read-tree", first)
        git.set_index_entry(scratch, replace(entry, name=blob), HISTORY_LEDGER)
        tree = git.write_tree(scratch)
    rules = ", ".join(grow.rule for grow in grows)
    message = f"Admit the rows that the commit {first[: history.SHORT_HASH]} adds to the ledgers of {rules}\n"
    return git.commit_tree(root, tree, [first], message)


def commit_tree(root: Path, composed: Composed, message: str) -> str:
    """Write the commit of a composed tree on its commit, with the message, and return its full name."""
    return git.commit_tree(root, composed.tree, [composed.head], message)


def compose_link(queued: Queued, parent: str, since: str) -> Link:
    """Compose the commits of a request on a commit, and return the link (THE BATCH).

    `since` is the commit that the own paths compare with (THE TREE).

    Raises:
        LandError: If the request does not compose, or it changes nothing
        git.GitError: If a git command fails, such as a patch that does not apply
    """
    request = queued.request
    composed = compose(queued.root, request, parent, since=since)
    if not composed.changed:
        raise LandError("the commit would change nothing")
    first = commit_tree(queued.root, composed, request.message)
    if not request.grows:
        return Link(queued, parent, first, first, composed.changed)
    second = grow_commit(queued.root, first, request.grows, composed.changed)
    return Link(queued, parent, first, second, tuple(sorted({*composed.changed, HISTORY_LEDGER})))


# ── The index and the working tree ─────────────────────────────────────────


def branch_of(root: Path) -> str:
    """Return the branch of HEAD of a tree, such as refs/heads/main.

    Raises:
        LandError: If HEAD is not on a branch
    """
    branch = git.symbolic_head(root)
    if branch is None:
        raise LandError(f"HEAD of {root} is not on a branch, and no branch can move")
    return branch


def swap(root: Path, branch: str, new: str, head: str, subject: str) -> bool:
    """Move the branch to the new commit when it holds the commit head, and tell if it moved (THE COMPARE-AND-SWAP)."""
    return git.swap_ref(root, branch, new, head, f"loop land: {subject}")


def sync_index(root: Path, commit: str, changed: Sequence[str]) -> list[str]:
    """Give the index of a work tree the entry of a commit for each path, and return the warnings.

    The working tree does not change.  A busy index only gives a warning, because the commit is done.
    """
    entries = [(git.tree_entry(root, commit, path), path) for path in changed]
    if git.set_index_entries(root, entries):
        return []
    return [f"loop land: WARNING, the index of {root} was busy and is not updated"]


def apply_patch(root: Path, patch: bytes, shown: str) -> list[str]:
    """Give the working tree a patch unless it holds the patch, and return a warning for each hunk that does not apply."""
    if git.holds_patch(root, patch):
        return []
    problem = git.apply_patch(root, patch)
    if problem is None:
        return []
    warning = (
        f"loop land: WARNING, {shown} does not apply to the working tree:\n{problem}The owner must apply "
        f"these hunks by hand before the next commit of those files, or that commit reverts them."
    )
    return [warning]


def update_working_tree(
    root: Path,
    request: Request,
    grown: tuple[str, str] | None,
    folded: tuple[str, str, tuple[str, ...]] | None = None,
) -> list[str]:
    """Give the working tree each patch, each edit, the rows of THE GROWTH and the ledgers of THE FALLS.

    The working tree gets only what it does not hold.

    Args:
        root: The top of the work tree of the request
        request: The request
        grown: The first and the second commit of a growth, or None (THE GROWTH)
        folded: The commit that the gate tested, the commit that landed, and the ledgers of THE FALLS of
            assay.loop.gate.  None when the gate wrote no ledger

    Returns:
        The warning of each hunk and each edit that does not apply
    """
    warnings: list[str] = []
    for patch in request.patches:
        warnings += apply_patch(root, patch.read_bytes(), str(patch))
    if grown is not None:
        warnings += apply_patch(root, git.patch(root, grown, [HISTORY_LEDGER]), HISTORY_LEDGER)
    if folded is not None:
        before, after, ledgers = folded
        warnings += apply_patch(root, git.patch(root, (before, after), ledgers), " ".join(ledgers))
    for path in edited_paths(request):
        file = root / path
        content = file.read_text(encoding="utf-8")
        updated = content
        for edit in request.edits:
            if edit.path != path:
                continue
            state = edit_state(updated, edit.kind, edit.first, edit.second)
            if state == "ambiguous":
                warnings.append(
                    f"loop land: WARNING, the working-tree {path} lacks an edit, and its anchor or old text is not "
                    f"unique there.  Apply that edit by hand"
                )
            elif state == "apply":
                updated = apply_edit(updated, edit, path)
        if updated != content:
            file.write_text(updated, encoding="utf-8")
    return warnings


@contextlib.contextmanager
def update_lock(root: Path) -> Iterator[None]:
    """Hold an exclusive lock of flock(2) on the file UPDATE_LOCK of the git directory of a work tree for the block.

    Raises:
        git.GitError: If root is not in a git work tree
    """
    with open(git.git_dir(root) / UPDATE_LOCK, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def updated_tree(root: Path, branch: str, request: Request, update: Update) -> list[str]:
    """Give the work tree of a landed request the update of its verdict, and return the warnings.

    An error of git or of a file only gives a warning, because the commit is on the branch.  A work tree with HEAD on
    a different branch gets no update (THE INDEX AND THE WORKING TREE).
    """
    try:
        current = git.symbolic_head(root)
        if branch and current != branch:
            elsewhere = (
                f"loop land: WARNING, HEAD of {root} is on {current or 'no branch'} and not on {branch}, and the work "
                f"tree gets no update of the landed commit"
            )
            return [elsewhere]
        with update_lock(root):
            warnings = sync_index(root, update.index_commit, update.index_paths)
            grown = (update.grown[0], update.grown[1]) if update.grown else None
            folded = (update.falls[0], update.falls[1], update.ledgers) if update.falls else None
            warnings += update_working_tree(root, request, grown, folded)
    except (git.GitError, LandError, OSError, ValueError) as problem:
        stopped = (
            f"loop land: WARNING, the update of the work tree {root} stops: {problem}.  The commit is on the branch.  "
            f"Give the index and the working tree its changes by hand"
        )
        return [stopped]
    return warnings


def given_update(queued: Queued, decision: Decision) -> None:
    """Give the work tree of a landed request the update of its verdict as the holder, and write each warning as a line.

    The process of the request gives the same update again after it reads the verdict (THE INDEX AND THE WORKING TREE).
    """
    if decision.update is None:
        return
    for warning in updated_tree(queued.root, queued.branch, queued.request, decision.update):
        check_report.write_line(warning, is_error=True)


# ── The batch ──────────────────────────────────────────────────────────────


def chain_changed(chain: Sequence[Link]) -> tuple[str, ...]:
    """Return each path that a chain of links changes, one time, sorted."""
    return tuple(sorted({path for link in chain for path in link.changed}))


def repository_of(queued: Queued) -> Path:
    """Return the shared git directory of a request, or its work tree for a request with no such directory (THE HOLDER)."""
    return Path(queued.common) if queued.common else queued.root


def shown_problems(verdict: gate.Verdict) -> frozenset[str]:
    """Return the problems of a verdict with LOGS_MARK as the replacement of the directory of its logs.

    The problems of two runs then compare equal when only their directories of logs are different (THE RUN OF THE BASE).
    """
    if not verdict.logs:
        return frozenset(verdict.problems)
    return frozenset(item.replace(verdict.logs, LOGS_MARK) for item in verdict.problems)


def cut_point(links: Sequence[Link]) -> int:
    """Return the count of the first links in which no link changes a C++ file of an earlier link (THE BATCH).

    Complexity: linear in the paths of the links.
    """
    seen: set[str] = set()
    for index, link in enumerate(links):
        files = {path for path in link.changed if path.endswith(gate.CPP_SUFFIXES)}
        if files & seen:
            return index
        seen |= files
    return len(links)


def refused(status: int, *texts: str) -> Decision:
    """Return the verdict of a refused request, with each text as a line of the standard error."""
    return Decision(status, "", tuple(Line(text, True) for text in texts))


class Journal:
    """The journals of the requests of one queue (THE JOURNAL)."""

    __slots__ = ("queue",)

    def __init__(self, queue: Path) -> None:
        """Keep the queue."""
        self.queue = queue

    def write(self, queued: Queued, branch: str, final: str, decision: Decision) -> None:
        """Write the journal of one request before the swap."""
        write_journal(self.queue, queued, JournalEntry(queued.token, branch, final, decision))

    def clear(self, queued: Queued) -> None:
        """Remove the journal of one request after a swap that did not occur."""
        queue_file(self.queue, queued.number, JOURNAL_SUFFIX).unlink(missing_ok=True)


class BatchLanding:
    """The landing of one batch of requests (THE BATCH, THE SEARCH, THE COMPARE-AND-SWAP).

    The object keeps the runs of the gate, the run that landed, and the run that refused each request, for THE
    RECORD.  It also keeps the requests that wait for the next batch, the dropped requests and the verdicts of the
    requests that landed.
    """

    __slots__ = (
        "base",
        "base_run",
        "branch",
        "check",
        "decide",
        "deferred",
        "dropped",
        "early",
        "early_from",
        "is_alive",
        "is_check",
        "is_cut",
        "journal",
        "landed",
        "landed_run",
        "pause",
        "refused_by",
        "repository",
        "runs",
    )

    def __init__(
        self,
        batch: Sequence[Queued],
        check: Check,
        decide: Decide,
        pause: heavy.Pause,
        early: Early | None = None,
        is_alive: IsAlive | None = None,
        journal: Journal | None = None,
    ) -> None:
        """Keep the gate, the receiver of the verdicts, the pause, the checks before the builds, and the journal.

        The first request gives the shared git directory and the branch (THE HOLDER).  With no checks before the
        builds, the batch goes to the gate immediately.  With no check of the processes, each request stays in the
        batch.
        """
        self.repository = repository_of(batch[0])
        self.branch = batch[0].branch or branch_of(batch[0].root)
        self.check = check
        self.decide = decide
        self.pause = pause
        self.early = early
        self.early_from = -1
        self.is_alive: IsAlive = is_alive if is_alive is not None else (lambda _queued: True)
        self.journal = journal
        self.is_check = batch[0].options.is_check
        self.is_cut = False
        self.base = ""
        self.base_run = -1
        self.runs: list[Run] = []
        self.landed_run = -1
        self.refused_by: dict[int, int] = {}
        self.dropped: dict[int, int] = {}
        self.deferred: list[Queued] = []
        self.landed: dict[int, Decision] = {}

    def tip(self) -> str:
        """Return the commit of the branch of the batch.

        Raises:
            LandError: If the branch holds no commit
            git.GitError: If git cannot read the repository
        """
        commit = git.resolve(self.repository, self.branch)
        if commit is None:
            raise LandError(f"the branch {self.branch} holds no commit")
        return commit

    def early_problems(self, links: Sequence[Link]) -> tuple[int, tuple[str, ...]] | None:
        """Return the index of the first link that the rule history refuses, with its errors (THE CHECKS BEFORE THE BUILDS).

        The check starts at the link of the index `early_from`, because the rule accepts the links before it.  The
        index -1 tells that the commit of the batch has no check at this time.  When no link is red, the function
        gives None, and it does not run again.  It also gives None when the commit of the batch is red.  It gives
        None when a link changes the package of the rule, too.  Complexity: one run of the rule for the commit of
        the batch.  Each link with a change in the scope of the rule adds one run.
        """
        from assay.guard.engines.meta import history

        scope = (*history.TABLES, f"{history.LEDGER_DIRECTORY}/", *history.QUARANTINE_MACHINERY)
        early = self.early
        self.early = None
        if early is None or any(path.startswith(PACKAGE_PREFIX) for link in links for path in link.changed):
            return None
        if self.early_from < 0:
            if early(self.base, self.base, ()):
                check_report.write_line("loop land: the rule history is red on HEAD, and the batch goes to the gate")
                return None
            self.early_from = 0
        for index in range(self.early_from, len(links)):
            link = links[index]
            grown = tuple(grow.rule for grow in link.queued.request.grows)
            if not grown and not any(path.startswith(scope) for path in link.changed):
                continue
            problems = early(link.commit, link.first, grown)
            if problems:
                self.early = early
                self.early_from = index
                return index, problems
        return None

    def gate_run(self, chain: Sequence[Link]) -> gate.Verdict:
        """Run the gate on the last commit of a chain, keep the run, and return the verdict.

        The first run of the batch runs each test, and each subsequent run runs the tests with an input that changed.
        The candidate holds one row of `--keep` for each rule: the row of the first request with that rule.  A
        request with `--keep` is the only request of its batch (THE HOLDER).
        """
        keeps = {keep.rule: keep for link in reversed(chain) for keep in link.queued.request.keeps}
        kept = tuple(keeps[rule] for rule in sorted(keeps))
        candidate = gate.Candidate(self.base, chain[-1].commit, chain_changed(chain), kept)
        verdict = self.check(candidate, bool(self.runs))
        numbers = tuple(link.queued.number for link in chain)
        self.runs.append(Run(candidate, numbers, tuple(link.queued.request.subject for link in chain), verdict))
        return verdict

    def base_gate_run(self, changed: Sequence[str]) -> gate.Verdict:
        """Run the gate on the commit of the batch with the changed paths of the batch, and keep the run (THE RUN OF THE BASE).

        The run is a retry.  It then runs the tests of the configurations of the batch with an input that changed.
        """
        candidate = gate.Candidate(self.base, self.base, tuple(changed))
        verdict = self.check(candidate, True)
        self.runs.append(Run(candidate, (), (), verdict))
        self.base_run = len(self.runs) - 1
        return verdict

    def base_problems(self) -> frozenset[str]:
        """Return the problems of the run of the base with LOGS_MARK, or none when the batch has no such run."""
        return shown_problems(self.runs[self.base_run].verdict) if self.base_run >= 0 else frozenset()

    def red_base_line(self) -> str:
        """Return the line that tells that the commit of the batch is red with no request (THE RUN OF THE BASE)."""
        return f"loop land: REJECTED, the commit {self.base[:SHORT_HASH]} of {self.branch} is red with no request"

    def compose_chain(self, accepted: Sequence[Link], items: Sequence[Queued]) -> tuple[list[Link], list[Queued]]:
        """Compose each request on the accepted links and on the requests before it, and refuse each that does not.

        A request with a process that left the queue drops out with no verdict.  The own paths of a request compare
        with the commit of its HEAD at the request, else with the commit of the batch (THE TREE).  Returns the links
        and the requests that do not compose or drop out.
        """
        links: list[Link] = []
        lost: list[Queued] = []
        parent = accepted[-1].commit if accepted else self.base
        for queued in items:
            if not self.is_alive(queued):
                lost.append(queued)
                self.dropped[queued.number] = -1
                continue
            try:
                link = compose_link(queued, parent, queued.head or self.base)
            except LandError as problem:
                lost.append(queued)
                self.decide(queued, refused(ExitCode.USAGE, f"loop land: {problem}"))
                continue
            except git.GitError as problem:
                lost.append(queued)
                self.decide(
                    queued, refused(ExitCode.TOOL_FAILED, f"loop land: the request does not compose: {problem}")
                )
                continue
            links.append(link)
            parent = link.commit
        return links, lost

    def reject(self, queued: Queued, run: int) -> None:
        """Give a request that is red alone on the accepted part the refusal, with the problems of its run.

        When each problem of the run is a problem of the run of the base, the first line tells that the branch is red
        (THE RUN OF THE BASE).
        """
        self.refused_by[queued.number] = run
        verdict = self.runs[run].verdict
        lines = [f"loop land: REJECTED, {item}" for item in verdict.problems]
        problems = shown_problems(verdict)
        if problems and problems <= self.base_problems():
            lines.insert(0, self.red_base_line())
        self.decide(queued, refused(ExitCode.FINDINGS, *lines))

    def reject_on_red_base(self, items: Sequence[Queued], batch_run: int) -> None:
        """Give each request the refusal of a branch that is red with no request that repairs it (THE RUN OF THE BASE).

        The lines give each problem of the run of the base, and each other problem of the first run of the batch.
        """
        base_verdict = self.runs[self.base_run].verdict
        batch_verdict = self.runs[batch_run].verdict
        lines = [f"{self.red_base_line()}, and no request of the batch repairs it"]
        lines += [f"loop land: REJECTED, {item}" for item in base_verdict.problems]
        base = self.base_problems()
        lines += [
            f"loop land: the batch also gives: {item}"
            for item in batch_verdict.problems
            if not shown_problems(replace(batch_verdict, problems=(item,))) <= base
        ]
        for queued in items:
            self.refused_by[queued.number] = batch_run
            self.decide(queued, refused(ExitCode.FINDINGS, *lines))

    def cut(self, pending: list[Queued], links: list[Link]) -> tuple[list[Queued], list[Link]]:
        """Cut the batch at the first link that changes a C++ file of an earlier link.

        The requests from that link wait for the next batch.  The function cuts only at the first composition of the
        batch (THE BATCH).
        """
        if self.is_cut:
            return pending, links
        self.is_cut = True
        kept = cut_point(links)
        if kept == len(links):
            return pending, links
        later = [link.queued for link in links[kept:]]
        self.deferred += later
        check_report.write_line(
            f"loop land: {len(later)} request(s) from {later[0].name} change a C++ file of an earlier request of the "
            f"batch, and wait for the next batch"
        )
        return [item for item in pending if item not in later], links[:kept]

    def search(self, items: Sequence[Queued]) -> tuple[list[Link], int]:
        """Return the accepted links of the requests and the index of the run that accepted them as a unit (THE SEARCH).

        The index is -1 when the gate accepts no request.  A first run that is red gets THE RUN OF THE BASE.  When
        that run is red, and the first run has each of its problems, each request gets the refusal of a red branch.
        Complexity: one run of the gate when the gate accepts each request.  A batch of n requests with r red
        requests runs the gate O(r log n) times.  THE RUN OF THE BASE adds one run.
        """
        accepted: list[Link] = []
        accepted_run = -1
        pending = list(items)
        red_size: int | None = None
        red_run = -1
        while pending:
            if red_size == 1:
                self.reject(pending.pop(0), red_run)
                red_size = None
                continue
            take = len(pending) if red_size is None else red_size // 2
            links, lost = self.compose_chain(accepted, pending[:take])
            if lost:
                pending = [item for item in pending if item not in lost]
                red_size = None
                continue
            pending, links = self.cut(pending, links)
            take = min(take, len(links))
            found = self.early_problems(links) if not self.runs and not accepted and red_size is None else None
            if found is not None:
                index, problems = found
                early_line = (f"loop land: REJECTED before the builds, {problem}" for problem in problems)
                self.decide(pending.pop(index), refused(ExitCode.FINDINGS, *early_line))
                continue
            verdict = self.gate_run([*accepted, *links])
            if verdict.is_accepted:
                accepted += links
                accepted_run = len(self.runs) - 1
                del pending[:take]
                red_size = None if red_size is None else red_size - take
                continue
            batch_run = len(self.runs) - 1
            if not accepted and red_size is None and self.base_run < 0:
                base_verdict = self.base_gate_run(chain_changed(links))
                is_unrepaired = shown_problems(base_verdict) <= shown_problems(verdict)
                if not base_verdict.is_accepted and is_unrepaired:
                    self.reject_on_red_base(pending, batch_run)
                    return [], -1
            red_size, red_run = take, batch_run
        return accepted, accepted_run

    def accept_check(self, accepted: Sequence[Link], verdict: gate.Verdict) -> None:
        """Give each request of --check that the gate accepts its verdict.  Nothing lands."""
        final = verdict.final or accepted[-1].commit
        folded = f", with the falls of {' '.join(verdict.ledgers)} that the gate wrote" if verdict.ledgers else ""
        for link in accepted:
            line = Line(f"loop land: the gate accepts the commit {final[:SHORT_HASH]}{folded}")
            self.decide(link.queued, Decision(ExitCode.OK, final, (line,)))

    def landed_decisions(
        self, accepted: Sequence[Link], verdict: gate.Verdict, final: str
    ) -> list[tuple[Queued, Decision]]:
        """Return the verdict of each accepted request, with the update of its work tree (THE INDEX AND THE WORKING TREE).

        The ledgers of THE FALLS go to the last request of each work tree.  Complexity: linear in the paths of the
        links.
        """
        ledgers = verdict.ledgers
        last = len(accepted) - 1
        last_of_root = {link.queued.root: index for index, link in enumerate(accepted)}
        folded = f", with the falls of {' '.join(ledgers)} that the gate wrote" if ledgers else ""
        decisions: list[tuple[Queued, Decision]] = []
        for index, link in enumerate(accepted):
            commit = final if index == last else link.commit
            changed = tuple(sorted({*link.changed, *(ledgers if index == last else ())}))
            lines = [Line(f"loop land: committed {commit[:8]} on {link.parent[:8]}{folded if index == last else ''}:")]
            lines += [Line(f"  {path}") for path in changed]
            if len(accepted) > 1:
                lines.append(
                    Line(f"loop land: a batch of {len(accepted)} requests landed at {final[:SHORT_HASH]} with one swap")
                )
            if link.queued.request.keeps:
                lines += [Line(f"loop land: the gate wrote the kept row of {row}") for row in verdict.kept]
            has_falls = bool(ledgers) and last_of_root[link.queued.root] == index
            update = Update(
                final,
                tuple(sorted({*changed, *(ledgers if has_falls else ())})),
                (link.first, link.commit) if link.queued.request.grows else (),
                (accepted[-1].commit, final) if has_falls else (),
                ledgers if has_falls else (),
            )
            decisions.append((link.queued, Decision(ExitCode.OK, commit, tuple(lines), update)))
        return decisions

    def land(self, items: Sequence[Queued]) -> None:
        """Land the requests: compose, run the gate, search, and move the branch by compare-and-swap.

        After the swap, each landed work tree gets its update before the first verdict (THE INDEX AND THE WORKING TREE).

        Raises:
            gate.GateError: If the warm tree cannot hold a commit, or a step of the gate cannot start
            git.GitError: If a git command fails, other than a git command of a composition
            LandError: If the branch holds no commit
            OSError: If a journal or a verdict cannot be written
        """
        pending = list(items)
        swaps_refused = 0
        while pending:
            self.base = self.tip()
            self.base_run = -1
            accepted, run = self.search(pending)
            if not accepted:
                return
            gone = [link.queued for link in accepted if not self.is_alive(link.queued)]
            if gone:
                for queued in gone:
                    self.dropped[queued.number] = run
                pending = [link.queued for link in accepted if link.queued not in gone]
                check_report.write_line(
                    f"loop land: {len(gone)} request(s) left the queue before the swap, and the batch composes its "
                    f"{len(pending)} other accepted request(s) again"
                )
                continue
            verdict = self.runs[run].verdict
            if self.is_check:
                self.landed_run = run
                self.accept_check(accepted, verdict)
                return
            final = verdict.final or accepted[-1].commit
            decisions = self.landed_decisions(accepted, verdict, final)
            if self.journal is not None:
                for queued, decision in decisions:
                    self.journal.write(queued, self.branch, final, decision)
            shown = accepted[0].queued.request.subject
            subject = shown if len(accepted) == 1 else f"a batch of {len(accepted)} requests from {shown}"
            if swap(self.repository, self.branch, final, self.base, subject):
                self.landed_run = run
                self.landed = {queued.number: decision for queued, decision in decisions}
                for queued, decision in decisions:
                    given_update(queued, decision)
                for queued, decision in decisions:
                    self.decide(queued, decision)
                return
            if self.journal is not None:
                for queued, _decision in decisions:
                    self.journal.clear(queued)
            swaps_refused += 1
            pending = [link.queued for link in accepted]
            if swaps_refused >= ATTEMPTS:
                stop = f"loop land: the branch moved {ATTEMPTS} times, and the batch stops"
                for queued in pending:
                    self.decide(queued, refused(ExitCode.USAGE, stop))
                return
            check_report.write_line(
                f"loop land: the branch moved, and the batch composes its {len(pending)} accepted requests again",
                is_error=True,
            )
            self.pause(random.uniform(*PAUSE_RANGE_S))

    def records(self) -> list[RunRecord]:
        """Return each run of the gate with the result of each of its requests (THE RECORD)."""
        found: list[RunRecord] = []
        for index, run in enumerate(self.runs):
            results = []
            for number in run.numbers:
                if index == self.landed_run:
                    results.append(ACCEPTED if self.is_check else LANDED)
                elif self.refused_by.get(number) == index:
                    results.append(REFUSED)
                elif self.dropped.get(number) == index:
                    results.append(DROPPED)
                else:
                    results.append(ACCEPTED if run.verdict.is_accepted else REJECTED)
            found.append(RunRecord(run, tuple(results)))
        return found


def write_records(work: Path, mode: str, records: Sequence[RunRecord]) -> None:
    """Append one line of JSON for each run of the gate to the file RECORDS of the work directory (THE RECORD)."""
    if not records:
        return
    work.mkdir(parents=True, exist_ok=True)
    with open(work / RECORDS, "a", encoding="utf-8") as handle:
        for record in records:
            run = record.run
            line = {
                "time": loop_history.iso_now(),
                "mode": mode,
                "base": not run.numbers,
                "head": run.candidate.head,
                "commit": run.candidate.commit,
                "landed": LANDED in record.results,
                "requests": [
                    {"number": number, "subject": subject, "result": result}
                    for number, subject, result in zip(run.numbers, run.subjects, record.results, strict=True)
                ],
                **asdict(run.verdict),
            }
            handle.write(json.dumps(line, sort_keys=True) + "\n")


# ── The queue ──────────────────────────────────────────────────────────────


def queue_of(work: Path, root: Path) -> Path:
    """Return the queue of the repository of a work tree in the work directory (THE QUEUE).

    Raises:
        git.GitError: If root is not in a git work tree
    """
    common = str(git.common_dir(root).resolve())
    return work / QUEUE / hashlib.sha256(common.encode("utf-8")).hexdigest()[:KEY_LENGTH]


def queue_file(queue: Path, number: int, suffix: str) -> Path:
    """Return the path of one file of a request in the queue: the number in NUMBER_WIDTH digits, and the suffix."""
    return queue / f"{number:0{NUMBER_WIDTH}d}{suffix}"


def number_of(path: Path) -> int | None:
    """Return the number of a file of the queue, or None when its name does not start with NUMBER_WIDTH digits."""
    head = path.name[:NUMBER_WIDTH]
    return int(head) if len(head) == NUMBER_WIDTH and head.isdigit() else None


def reserve(queue: Path) -> tuple[int, int]:
    """Take a new number of the queue, and return it with the descriptor of its lock, which the process holds.

    Each number occurs one time only (THE QUEUE).  The process holds the lock of the file NEXT while it reserves.  It
    locks a new file first, and then it links that file to the file of the number.  A different process then does not see
    the file of the number with no lock.  Complexity: linear in the files of the queue.
    """
    queue.mkdir(parents=True, exist_ok=True)
    with open(queue / NEXT, "a+", encoding="ascii") as counter:
        fcntl.flock(counter, fcntl.LOCK_EX)
        counter.seek(0)
        text = counter.read().strip()
        highest = max((found for path in paths.listing(queue) if (found := number_of(path)) is not None), default=0)
        number = max(int(text) if text.isdigit() else 0, highest + 1)
        counter.seek(0)
        counter.truncate()
        counter.write(f"{number + 1}\n")
        counter.flush()
        fresh = queue / RESERVE
        fresh.unlink(missing_ok=True)
        descriptor = os.open(fresh, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX)
            os.link(fresh, queue_file(queue, number, LOCK_SUFFIX))
            fresh.unlink()
        except BaseException:
            os.close(descriptor)
            raise
    return number, descriptor


def encoded_request(queued: Queued, patch_names: Sequence[str]) -> bytes:
    """Return the bytes of the file NUMBER.request of a request (THE QUEUE)."""
    request = queued.request
    record = {
        "format": REQUEST_FORMAT,
        "token": queued.token,
        "root": str(queued.root),
        "branch": queued.branch,
        "common": queued.common,
        "head": queued.head,
        "messages": list(request.messages),
        "patches": list(patch_names),
        "edits": [asdict(edit) for edit in request.edits],
        "paths": list(request.paths),
        "grows": [asdict(grow) for grow in request.grows],
        "keeps": [asdict(keep) for keep in request.keeps],
        "jobs": queued.options.jobs,
        "every": queued.options.is_every,
    }
    return (json.dumps(record, sort_keys=True) + "\n").encode("utf-8")


def text_list(value: object, name: str) -> tuple[str, ...]:
    """Return a list of texts of a request file.

    Raises:
        TypeError: If the value is not a list of texts.  The message names the field
    """
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise TypeError(f"the field {name} is not a list of texts")
    return tuple(value)


def records_of(value: object, name: str, keys: tuple[str, ...]) -> list[tuple[str, ...]]:
    """Return the texts of some keys of each record of a list of a request file.

    Raises:
        TypeError: If the value is not a list of records with a text for each key.  The message names the field
    """
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        raise TypeError(f"the field {name} is not a list of records")
    return [text_list([item.get(key) for key in keys], name) for item in value]


def token_of(data: bytes) -> str:
    """Return the token of the bytes of a request file or of a verdict file, or "" when the bytes hold no token."""
    try:
        record = json.loads(data.decode("utf-8"))
    except ValueError:
        return ""
    token = record.get("token") if isinstance(record, dict) else None
    return token if isinstance(token, str) else ""


def decoded_request(queue: Path, number: int, data: bytes) -> Queued:
    """Return the request of the bytes of a file NUMBER.request (THE QUEUE).

    Raises:
        ValueError: If the bytes do not hold JSON of REQUEST_FORMAT
        TypeError: If a field of the request does not have its type
    """
    record = json.loads(data.decode("utf-8"))
    if not isinstance(record, dict):
        raise TypeError("the request is not a record")
    if record.get("format") != REQUEST_FORMAT:
        raise ValueError(f"the request is not of the format {REQUEST_FORMAT}")
    edits = tuple(Edit(*item) for item in records_of(record.get("edits"), "edits", ("path", "kind", "first", "second")))
    grows = tuple(Grow(*item) for item in records_of(record.get("grows"), "grows", ("rule", "reason")))
    keeps = tuple(gate.Keep(*item) for item in records_of(record.get("keeps"), "keeps", ("rule", "reason")))
    keys = ("root", "branch", "common", "token", "head")
    root, branch, common, token, head = text_list([record.get(key) for key in keys], "root")
    jobs, every = record.get("jobs"), record.get("every")
    if not isinstance(jobs, int) or not isinstance(every, bool):
        raise TypeError("the field jobs is not a number, or the field every is not a truth value")
    if jobs < 1:
        raise ValueError(f"the field jobs is {jobs}, and a request takes a number above 0")
    request = Request(
        text_list(record.get("messages"), "messages"),
        tuple(queue / name for name in text_list(record.get("patches"), "patches")),
        edits,
        text_list(record.get("paths"), "paths"),
        grows,
        keeps,
    )
    options = Options(False, False, is_every=every, jobs=jobs)
    return Queued(number, Path(root), request, options, branch, common, token, head)


@contextlib.contextmanager
def entered(queue: Path, root: Path, request: Request, options: Options) -> Iterator[Queued]:
    """Write a request into the queue, hold its lock for the block, give the request, and remove its files at the end.

    The request of the block keeps the patches of the request.  The copy of each patch is in the queue.

    Raises:
        LandError: If HEAD of the work tree is not on a branch, or a patch cannot be read
        git.GitError: If git cannot read the work tree
    """
    branch = branch_of(root)
    common = str(git.common_dir(root).resolve())
    head = git.head(root)
    number, descriptor = reserve(queue)
    try:
        queued = Queued(number, root, request, options, branch, common, secrets.token_hex(TOKEN_BYTES), head)
        names = [f"{queued.name}{PATCH_MARK}{index}" for index in range(len(request.patches))]
        for name, patch in zip(names, request.patches, strict=True):
            try:
                data = patch.read_bytes()
            except OSError as problem:
                raise LandError(f"the patch {patch} cannot be read: {problem}") from None
            cache.write_atomic(queue / name, data)
        cache.write_atomic(queue_file(queue, number, REQUEST_SUFFIX), encoded_request(queued, names))
        yield queued
    finally:
        remove_request(queue, number)
        os.close(descriptor)


def remove_request(queue: Path, number: int) -> None:
    """Remove each file of a request from the queue: the request file first, and the lock file last (THE QUEUE).

    A holder then takes no request of a process that left the queue, and it finds no file of a request with no lock.
    """
    for suffix in (REQUEST_SUFFIX, VERDICT_SUFFIX, JOURNAL_SUFFIX):
        queue_file(queue, number, suffix).unlink(missing_ok=True)
    for path in paths.listing(queue, f"{number:0{NUMBER_WIDTH}d}{PATCH_MARK}*"):
        path.unlink(missing_ok=True)
    queue_file(queue, number, LOCK_SUFFIX).unlink(missing_ok=True)


def encoded_decision(decision: Decision, token: str) -> bytes:
    """Return the bytes of the file NUMBER.verdict of a decision, with the token of its request."""
    record = {
        "format": REQUEST_FORMAT,
        "token": token,
        "status": decision.status,
        "commit": decision.commit,
        "lines": [asdict(line) for line in decision.lines],
        "update": asdict(decision.update) if decision.update is not None else None,
    }
    return (json.dumps(record, sort_keys=True) + "\n").encode("utf-8")


def decoded_update(value: object) -> Update | None:
    """Return the update of a verdict, or None for no update.

    Raises:
        TypeError: If the value is not a record of an update
    """
    if value is None:
        return None
    if not isinstance(value, dict):
        raise TypeError("the field update is not a record")
    index_commit = value.get("index_commit")
    if not isinstance(index_commit, str):
        raise TypeError("the field index_commit of the update is not a text")
    fields = [text_list(value.get(key), key) for key in ("index_paths", "grown", "falls", "ledgers")]
    return Update(index_commit, *fields)


def decoded_decision(data: bytes) -> Decision:
    """Return the decision of the bytes of a verdict file.

    Raises:
        ValueError: If the bytes do not hold JSON of a verdict
        TypeError: If a field of the verdict does not have its type
    """
    record = json.loads(data.decode("utf-8"))
    if not isinstance(record, dict):
        raise TypeError("the verdict is not a record")
    status, commit, lines_value = record.get("status"), record.get("commit"), record.get("lines")
    if not isinstance(status, int) or not isinstance(commit, str) or not isinstance(lines_value, list):
        raise TypeError("the verdict has no status, commit or lines of their types")
    lines = tuple(
        Line(item["content"], bool(item.get("is_error")))
        for item in lines_value
        if isinstance(item, dict) and isinstance(item.get("content"), str)
    )
    return Decision(status, commit, lines, decoded_update(record.get("update")))


def write_decision(queue: Path, queued: Queued, decision: Decision) -> None:
    """Write the verdict file of a request into the queue, with the token of the request (THE HOLDER)."""
    cache.write_atomic(queue_file(queue, queued.number, VERDICT_SUFFIX), encoded_decision(decision, queued.token))


def read_decision(queue: Path, number: int, token: str) -> Decision | None:
    """Return the verdict of a request from the queue, or None when the request has no verdict with its token.

    A verdict with no token is the verdict of a request file that does not read (THE HOLDER).
    """
    try:
        data = queue_file(queue, number, VERDICT_SUFFIX).read_bytes()
        decision = decoded_decision(data)
    except OSError, ValueError, TypeError:
        return None
    return decision if token_of(data) in (token, "") else None


def write_journal(queue: Path, queued: Queued, entry: JournalEntry) -> None:
    """Write the journal file of a request into the queue (THE JOURNAL)."""
    record = {
        "format": REQUEST_FORMAT,
        "token": entry.token,
        "branch": entry.branch,
        "final": entry.final,
        "decision": json.loads(encoded_decision(entry.decision, entry.token)),
    }
    cache.write_atomic(queue_file(queue, queued.number, JOURNAL_SUFFIX), (json.dumps(record) + "\n").encode("utf-8"))


def read_journal(queue: Path, number: int) -> JournalEntry | None:
    """Return the journal of a request, or None when the request has no journal that reads (THE JOURNAL)."""
    try:
        record = json.loads(queue_file(queue, number, JOURNAL_SUFFIX).read_text(encoding="utf-8"))
        token, branch, final = text_list([record.get(key) for key in ("token", "branch", "final")], "journal")
        decision = decoded_decision(json.dumps(record.get("decision")).encode("utf-8"))
    except OSError, ValueError, TypeError, AttributeError:
        return None
    return JournalEntry(token, branch, final, decision)


def is_alive(queue: Path, queued: Queued) -> bool:
    """Tell if the process of a request holds the lock of the request (THE HOLDER)."""
    return heavy.is_held(queue_file(queue, queued.number, LOCK_SUFFIX))


def has_journal_verdict(queue: Path, queued: Queued) -> bool:
    """Give a request with a journal the verdict that the journal settles, and tell if the request then has a verdict.

    A journal of a swap that did not occur goes, and the function gives False (THE JOURNAL).  For a journal of a
    swap that occurred, the work tree gets the update of the journal, and then the request gets its verdict.  When
    git cannot tell if the swap occurred, the journal goes, and the request gets the refusal of a tool error.
    """
    journal = queue_file(queue, queued.number, JOURNAL_SUFFIX)
    entry = read_journal(queue, queued.number)
    if entry is None:
        journal.unlink(missing_ok=True)
        return False
    try:
        has_landed = entry.token == queued.token and git.is_ancestor(repository_of(queued), entry.final, entry.branch)
    except git.GitError as problem:
        unknown = refused(
            ExitCode.TOOL_FAILED,
            f"loop land: git cannot tell if {entry.branch} holds the commit {entry.final[:SHORT_HASH]} of the journal "
            f"of this request: {problem}",
            "loop land: the commit can be on the branch.  Look at the branch before you land the request again",
        )
        write_decision(queue, queued, unknown)
        journal.unlink(missing_ok=True)
        return True
    if not has_landed:
        journal.unlink(missing_ok=True)
        return False
    check_report.write_line(
        f"loop land: {queued.name} landed before its holder ended, and gets the verdict of its journal"
    )
    given_update(queued, entry.decision)
    write_decision(queue, queued, entry.decision)
    return True


def removed_orphans(queue: Path) -> None:
    """Remove each file of a request with a verdict or a journal and no lock (THE QUEUE)."""
    for path in (*paths.listing(queue, f"*{VERDICT_SUFFIX}"), *paths.listing(queue, f"*{JOURNAL_SUFFIX}")):
        number = number_of(path)
        if number is not None and not heavy.is_held(queue_file(queue, number, LOCK_SUFFIX)):
            remove_request(queue, number)


def waiting_requests(queue: Path, pause: heavy.Pause = time.sleep) -> list[Queued]:
    """Return each request of the queue with no verdict and a live process, in the sequence of the numbers (THE HOLDER).

    The function removes the files of each request of a process that ended, and each verdict of a different token.
    It gives a request with a journal the verdict that the journal settles (THE JOURNAL).  It waits INCOMPLETE_S
    seconds for a live request with no request file, and then it skips it.  A request file that does not read gets a
    verdict of a usage error.  Complexity: linear in the files of the queue, for each look.
    """
    waited = 0.0
    while True:
        removed_orphans(queue)
        found: list[Queued] = []
        is_incomplete = False
        for lock in paths.listing(queue, f"*{LOCK_SUFFIX}"):
            number = number_of(lock)
            if number is None:
                continue
            if not heavy.is_held(lock):
                remove_request(queue, number)
                continue
            request_file = queue_file(queue, number, REQUEST_SUFFIX)
            try:
                data = request_file.read_bytes()
            except FileNotFoundError:
                is_incomplete = True
                continue
            except OSError as problem:
                data = f"{problem}".encode()
            verdict_file = queue_file(queue, number, VERDICT_SUFFIX)
            if verdict_file.is_file():
                with contextlib.suppress(OSError):
                    if token_of(verdict_file.read_bytes()) in (token_of(data), ""):
                        continue
                verdict_file.unlink(missing_ok=True)
            try:
                queued = decoded_request(queue, number, data)
            except (ValueError, TypeError) as problem:
                decision = refused(ExitCode.USAGE, f"loop land: the request file does not read: {problem}")
                cache.write_atomic(verdict_file, encoded_decision(decision, token_of(data)))
                continue
            if queue_file(queue, number, JOURNAL_SUFFIX).is_file() and has_journal_verdict(queue, queued):
                continue
            found.append(queued)
        if not is_incomplete or waited >= INCOMPLETE_S:
            return found
        pause(INCOMPLETE_POLL_S)
        waited += INCOMPLETE_POLL_S


def batch_of(found: Sequence[Queued]) -> list[Queued]:
    """Return the requests of the next batch, in their sequence (THE HOLDER).

    The batch holds the requests with the git directory and the branch of the first request.  A request with `--keep`
    ends it before itself, and a first request with `--keep` is the only request of its batch.
    """
    if not found:
        return []
    first = found[0]
    if first.request.keeps:
        return [first]
    same = [item for item in found if (item.common, item.branch) == (first.common, first.branch)]
    end = next((index for index, item in enumerate(same) if item.request.keeps), len(same))
    return same[:end]


def hold(work: Path, queue: Path, check: Check | None = None, pause: heavy.Pause = time.sleep) -> bool:
    """Land one batch of the queue as the holder of the tree, and tell if a batch was there (THE HOLDER).

    The caller holds the lock of the warm tree.  With no planted check, the batch runs the gate of assay.loop.gate on
    the shared git directory of its first request.  It holds the session of the gate, which goes before each other
    heavy step (THE PRIORITY OF THE GATE of assay.loop.heavy).  Each run of the gate writes its logs into a directory
    of its own.
    """
    batch = batch_of(waiting_requests(queue, pause))
    if not batch:
        return False
    decided: set[int] = set()

    def decide(queued: Queued, decision: Decision) -> None:
        """Write the verdict of one request while its process holds the lock, one time."""
        if queued.number in decided:
            return
        decided.add(queued.number)
        if is_alive(queue, queued):
            write_decision(queue, queued, decision)

    gated = check
    early: Early | None = None
    session: contextlib.AbstractContextManager[None] = contextlib.nullcontext()
    if gated is None:
        jobs = min(item.options.jobs for item in batch)
        is_every = any(item.options.is_every for item in batch)
        repository = repository_of(batch[0])
        planned = gate.Gate(heavy.main_work_tree(repository), work, jobs, is_every)

        def run_check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Run the gate with the logs of the run in a directory of its own."""
            return replace(planned, run=gate.next_run(work)).check(candidate, is_retry)

        gated = run_check
        early = planned.history_problems
        session = heavy.gate_session(repository)
    shown = " ".join(item.name for item in batch)
    check_report.write_line(f"loop land: the holder of the tree lands the batch {shown}")
    landing: BatchLanding | None = None
    try:
        with session:
            landing = BatchLanding(
                batch, gated, decide, pause, early, functools.partial(is_alive, queue), Journal(queue)
            )
            landing.land(batch)
    except (LandError, gate.GateError, git.GitError, proc.ToolFailed, OSError, ValueError) as problem:
        status = ExitCode.USAGE if isinstance(problem, LandError) else ExitCode.TOOL_FAILED
        settle(batch, decide, landing, refused(status, f"loop land: {problem}"))
    finally:
        write_records(work, "land", landing.records() if landing is not None else [])
    return True


def settle(batch: Sequence[Queued], decide: Decide, landing: BatchLanding | None, failure: Decision) -> None:
    """Give each request of a batch that stopped on an error its verdict (THE HOLDER).

    A request that landed gets the verdict of its landing.  A request that waits for the next batch, or that dropped
    out, gets no verdict.  Each other request gets the verdict of the error.  A verdict that cannot be written gives a
    line, and the journal then gives the verdict to the next holder.
    """
    landed: dict[int, Decision] = {}
    skipped: set[int] = set()
    if landing is not None:
        landed = landing.landed
        skipped = {item.number for item in landing.deferred} | set(landing.dropped)
    for queued in batch:
        if queued.number in skipped:
            continue
        try:
            decide(queued, landed.get(queued.number, failure))
        except OSError as problem:
            check_report.write_line(
                f"loop land: the verdict of {queued.name} cannot be written: {problem}", is_error=True
            )


def written(decision: Decision) -> int:
    """Write the lines of a verdict, and return its status."""
    for line in decision.lines:
        check_report.write_line(line.content, is_error=line.is_error)
    return decision.status


def landing(
    work: Path,
    root: Path,
    request: Request,
    options: Options,
    holder: Holder = hold,
    pause: heavy.Pause = time.sleep,
) -> int:
    """Write the request into the queue, wait for its verdict or for the lock, and return its exit (THE WAIT).

    The process then gives its work tree the update of a verdict of a landing (THE INDEX AND THE WORKING TREE).

    Raises:
        LandError: If HEAD of the work tree is not on a branch, or a patch cannot be read
        git.GitError: If git cannot read the work tree
    """
    queue = queue_of(work, root)
    with entered(queue, root, request, options) as own:
        check_report.write_line(f"loop land: the request waits in the queue as {own.name}")
        decision = read_decision(queue, own.number, own.token)
        while decision is None:
            with gate.tree_lock(work, wait=False) as is_held:
                while is_held:
                    decision = read_decision(queue, own.number, own.token)
                    if decision is not None or not holder(work, queue):
                        break
            if decision is None:
                pause(POLL_S)
                decision = read_decision(queue, own.number, own.token)
        if decision.update is not None:
            warnings = updated_tree(root, own.branch, request, decision.update)
            decision = replace(decision, lines=(*decision.lines, *(Line(text, True) for text in warnings)))
    return written(decision)


# ── The check, the preview and the driver ──────────────────────────────────


def held_heavy_lock(root: Path, environment: Mapping[str, str] = os.environ) -> str | None:
    """Tell how this process holds the lock of the heavy steps, or give None (THE LOCK OF A HEAVY STEP).

    Complexity: linear in the open descriptors of the process.
    """
    if environment.get(heavy.STEP_VARIABLE):
        return f"the variable {heavy.STEP_VARIABLE} of `loop heavy` is set"
    lock = heavy.lock_path(root).resolve()
    for link in paths.listing(DESCRIPTORS):
        try:
            target = Path(os.readlink(link))
        except OSError:
            continue
        if target == lock:
            return f"the descriptor {link.name} names {lock}"
    return None


def checked(root: Path, request: Request, options: Options, work: Path) -> int:
    """Run the gate on the commit of a request as --check does, alone with the lock of the warm tree, and return the exit.

    --check with an empty request checks HEAD itself.

    Raises:
        LandError: If HEAD of the work tree is not on a branch
        gate.GateError: If the warm tree cannot hold the commit, or a step of the gate cannot start
        git.GitError: If a git command fails
    """
    decisions: list[Decision] = []
    with gate.session(root, work, options.jobs, options.is_every) as session, heavy.gate_session(root):

        def run_check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Run the gate with the logs of the run in a directory of its own."""
            return replace(session, run=gate.next_run(work)).check(candidate, is_retry)

        queued = Queued(0, root, request, options)
        if request.is_empty:
            head = git.head(root)
            candidate = gate.Candidate(head, head, ())
            verdict = run_check(candidate, False)
            run = Run(candidate, (0,), (request.subject,), verdict)
            write_records(work, "check", [RunRecord(run, (ACCEPTED if verdict.is_accepted else REJECTED,))])
            if verdict.is_accepted:
                return written(
                    Decision(ExitCode.OK, head, (Line(f"loop land: the gate accepts HEAD {head[:SHORT_HASH]}"),))
                )
            return written(refused(ExitCode.FINDINGS, *(f"loop land: REJECTED, {item}" for item in verdict.problems)))
        landing = BatchLanding(
            [queued],
            run_check,
            lambda _queued, decision: decisions.append(decision),
            time.sleep,
            session.history_problems,
        )
        try:
            landing.land([queued])
        finally:
            write_records(work, "check", landing.records())
    return written(decisions[0]) if decisions else ExitCode.TOOL_FAILED


def preview(root: Path, request: Request, options: Options) -> int:
    """Print the diff or the tree of the commit of a request on HEAD, for --dry-run and --tree, with no gate.

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails
    """
    composed = compose(root, request, git.head(root), with_diff=options.is_dry_run)
    if options.is_dry_run:
        if not composed.changed:
            raise LandError("the commit would change nothing")
        check_report.write_line(composed.diff.rstrip("\n"))
    else:
        check_report.write_line(composed.tree)
    return ExitCode.OK


def driver_tree(root: Path) -> Path | None:
    """Return the main work tree when this module is not its copy there, else None (THE DRIVER)."""
    main_tree = heavy.main_work_tree(root)
    if Path(__file__).resolve() == (main_tree / MODULE_PATH).resolve():
        return None
    return main_tree


def run_driver(main_tree: Path, argv: Sequence[str]) -> int:
    """Run the same words with the copy of the main work tree, and return its exit (THE DRIVER)."""
    command = [sys.executable, str(main_tree / MASTER_PATH), "land", *argv]
    check_report.write_line(f"loop land: the landing runs the copy of {main_tree}")
    completed = proc.run(
        proc.Command(command, timeout_s=DRIVER_S, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT)
    )
    return heavy.exit_of(completed)


def main(argv: list[str]) -> int:
    """Land a request, or run the gate on it.

    Args:
        argv: The words after `loop land`

    Returns:
        The exit status
    """
    if argv in HELP_WORDS:
        check_report.write_line(USAGE)
        return ExitCode.OK
    current = Path.cwd()
    try:
        request, options = parse(argv, current)
        root = git.toplevel(current)
        if options.is_dry_run or options.is_tree_only:
            return preview(root, request, options)
        held = held_heavy_lock(root)
        if held is not None:
            raise LandError(
                f"this process holds the lock of the heavy steps: {held}.  The gate takes that lock for each of its "
                f"steps, so the landing would wait for itself.  Run loop land with no heavy lock"
            )
        main_tree = driver_tree(root)
        if main_tree is not None and not options.is_here:
            return run_driver(main_tree, argv)
        checked_keeps(root, request)
        work = cache.work_root() / WORK_NAME
        if options.is_check:
            return checked(root, request, options, work)
        return landing(work, root, request, options)
    except LandError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.USAGE
    except gate.GateError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
    except (git.GitError, proc.ToolFailed) as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
