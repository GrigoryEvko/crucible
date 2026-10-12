"""land — compose the commit of a change, build and test it in the gate, and move main to it by compare-and-swap.

THE REQUEST
    The words before the word -- give the message and the edits.  The words
    after it give the own paths, and that list can be empty:
      -m TEXT                   One paragraph of the message.  The first paragraph is the subject
      --patch FILE              A unified diff that git made against HEAD
      --replace FILE OLD NEW    In the shared FILE, the text of the file NEW replaces the text of the file OLD
      --block FILE AFTER BLOCK  In the shared FILE, the text of the file BLOCK goes after the text of the file AFTER
      `--grow` RULE REASON      The change adds rows to the ledger of RULE, and REASON tells why (THE GROWTH)
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
    The tree of the commit is HEAD with the working-tree copy of each own
    path.  It also holds each shared file as HEAD holds it, with the edits.
    When an own path is gone from the working tree, the tree does not hold
    it.  A shared file does not come from the working tree.  The commit then
    cannot hold an edit of a different writer that is not committed, or an
    earlier copy of a shared file.  The text that --replace replaces and the text that
    --block writes after must each occur one time in the file.  An edit that
    HEAD holds is not applied again.  Some edits can name one file, and they
    apply in the given sequence.  An own path cannot be a shared file.  Each
    path that the commit changes is an own path, a file in an own directory,
    or a shared file.  Else the command stops.

THE COMPOSITION
    The command composes the tree in the index of a scratch repository.  The
    object directory of the scratch repository is a link to the object
    directory of the repository.  Each blob, tree and commit that the
    composition writes is then in the repository.  The command cannot give git an
    index file in the environment, because assay.core.proc gives no child a
    repository variable.  The scratch repository reads the own paths from
    the work tree of the repository through --work-tree.  It gets a copy of
    the file `info/exclude` of the repository, and it runs no garbage
    collection.

THE GROWTH
    A row `utils/ledgers/RULE.txt | grow | admit | COMMIT | REASON` of the
    ledger of the rule history names the commit that added rows to the
    ledger of RULE (assay.guard.engines.meta.history, THE GROWTH).  A commit
    cannot hold its own name.  The row then goes into a second commit on the
    first.  With `--grow`, the first commit holds the change.  The second
    commit adds one such row for each RULE, with COMMIT the first commit in
    9 characters.  The change must change the ledger of each RULE.  The gate
    tests the second commit, and one compare-and-swap moves the branch to
    it.  The branch then does not hold the added rows with no such row.

THE GATE
    Before the compare-and-swap, the gate builds and tests the commit in the
    warm tree of each configuration that the change selects (assay.loop.gate).
    A commit that the gate rejects does not land.  The gate has no base
    side.  A test that is red alone on HEAD then rejects each landing.

THE COMPARE-AND-SWAP
    The branch of HEAD moves to the commit only when the branch holds the
    commit that the composition started from (`git update-ref REF NEW OLD`).
    When a different commit lands first, the command composes the commit
    again on the new HEAD, after a random pause.  The gate then runs `loop
    test` with the inputs that changed, in each configuration that the change
    selects.  After ATTEMPTS refusals, the command stops.  A landing then
    cannot revert a different commit.  The gate holds the lock of the warm
    tree for all of the landing.

THE INDEX AND THE WORKING TREE
    After the commit, the index of the repository gets the entry of the
    commit for each changed path, and the working tree does not change.
    Then each patch goes to the working tree with git apply, unless the
    working tree holds it.  The rows of THE GROWTH go there in the same way.  Each
    edit goes to the working-tree copy of its file, unless the copy holds it.
    When a hunk does not apply, the command writes a warning.  The owner of
    the hunk must then apply it by hand before the next commit of that file,
    or that commit reverts it.

THE RECORD
    Each run of the gate adds one line of JSON to the file RECORDS of the
    work directory.  The line holds the time, the subject, the commits, the
    count of compositions, the configurations and the problems.  It also
    holds the wall time, the time of each configuration in its heavy step,
    and the result.  The work directory is the directory land of the work
    root of assay.core.cache.

THE DRIVER
    A landing runs the copy of this module in the main work tree of the
    repository, which holds main.  A different copy runs the copy of the
    main work tree with the same words.  An example is the copy of a work
    tree with a change of this module.  A change of the gate then cannot make
    the gate of its own landing weaker.  --here runs this copy, and it
    applies only to --check.  --dry-run and --tree land nothing.  They run
    this copy, with no gate.

Usage
    loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... [--block FILE AFTER BLOCK]...
              [`--grow` RULE REASON]... [--jobs N] [--all-configurations] [--check [--here] | --dry-run | --tree]
              -- [PATH]...

    Exit 0 when the commit landed, when the gate accepts the commit of
    --check, or when --dry-run or --tree printed.  Exit 1 when the gate
    rejects the commit.  Exit 2 on a usage error, or on a request that does
    not compose.  Exit 4 when a tool fails.

`guard selftest tests.loop.test_land` does a test of the module on a planted repository.
"""

import contextlib
import json
import random
import shutil
import sys
import tempfile
import time
from collections.abc import Callable, Iterator, Sequence
from dataclasses import asdict, dataclass, replace
from pathlib import Path

from assay.core import cache, git, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import gate, heavy
from assay.loop import history as loop_history

# The refusals of the compare-and-swap before the command stops, and the range of the random pause after one (THE
# COMPARE-AND-SWAP).
ATTEMPTS = 8
PAUSE_RANGE_S = (0.1, 0.9)
# The two kinds of an edit of a shared file (THE REQUEST).
INSERT = "insert"
REPLACE = "replace"
# The count of the values of each option, for the division of the words (split_words()).
VALUE_COUNTS = {"-m": 1, "--patch": 1, "--replace": 3, "--block": 3, "--grow": 2, "--jobs": 1}
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
    "                 [--grow RULE REASON]... [--jobs N] [--all-configurations] "
    "[--check [--here] | --dry-run | --tree] -- [PATH]..."
)
# The subject of the commit of --check with no message.
CHECK_SUBJECT = "Check a change in the gate"
# The file of the shared git directory that the scratch repository copies (THE COMPOSITION).
EXCLUDE_FILE = Path("info/exclude")
# The ledger of the rule history, and the kind and the hash length of its row of THE GROWTH.
HISTORY_RULE = "history"
HISTORY_LEDGER = f"{rule_ledger.LEDGER_DIRECTORY}/{HISTORY_RULE}.txt"
GROW_KIND = "grow"
SHORT_HASH = 9
# The work directory of the gate, in the work root, and the file of the records in it (THE RECORD).
WORK_NAME = "land"
RECORDS = "records.jsonl"
# The copy of this module in a tree, and the master that runs it (THE DRIVER).
MODULE_PATH = Path("utils/assay/loop/land.py")
MASTER_PATH = Path("utils/bin/loop")
# The limit of a landing that the driver runs from the main work tree, in seconds.
DRIVER_S = 8 * proc.BUILD_S


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
    """The message, the patches, the edits, the own paths and the rows of THE GROWTH of one landing (THE REQUEST)."""

    messages: tuple[str, ...]
    patches: tuple[Path, ...]
    edits: tuple[Edit, ...]
    paths: tuple[str, ...]
    grows: tuple[Grow, ...] = ()

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
    """The result of one composition: the HEAD that it starts from, the tree, the changed paths, and the diff."""

    head: str
    tree: str
    changed: tuple[str, ...]
    diff: str


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of one landing: the exit, the last candidate, the compositions, the last verdict, and the landing."""

    status: int
    candidate: gate.Candidate | None
    compositions: int
    verdict: gate.Verdict | None
    is_landed: bool


# The check of the gate: the candidate and the retry flag give the verdict (THE GATE).
type Check = Callable[[gate.Candidate, bool], gate.Verdict]


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
    before, paths = split_words(argv)
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
    for grow in grows:
        if not rule_registry.is_rule_id(grow.rule) or not grow.reason.strip():
            raise LandError(f"--grow takes the ID of a rule and a reason, not {grow.rule!r} and {grow.reason!r}")
    request = Request(
        tuple(item[0] for item in values["-m"]),
        tuple((current / item[0]).resolve() for item in values["--patch"]),
        tuple(edits),
        tuple(paths),
        grows,
    )
    return request, checked_options(flags, values["--jobs"], request)


# ── The edits ──────────────────────────────────────────────────────────────


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


@contextlib.contextmanager
def scratch_repository(root: Path) -> Iterator[Path]:
    """Make a scratch repository with a link to the object directory of the repository (THE COMPOSITION).

    The block gets the scratch repository.  The command removes it after the block.
    """
    shared = git.common_dir(root)
    with tempfile.TemporaryDirectory(prefix="loop-land-") as work:
        scratch = Path(work)
        git.init(scratch)
        git.run(scratch, "config", "gc.auto", "0")
        objects = scratch / ".git" / "objects"
        shutil.rmtree(objects)
        objects.symlink_to(shared / "objects", target_is_directory=True)
        exclude = shared / EXCLUDE_FILE
        if exclude.is_file():
            shutil.copyfile(exclude, scratch / ".git" / EXCLUDE_FILE)
        yield scratch


def names(output: bytes) -> list[str]:
    """Return the paths of the output of a git command that gives each path with a NUL after it."""
    return [name for name in output.decode("utf-8", "surrogateescape").split("\0") if name]


def compose(root: Path, request: Request, head: str, with_diff: bool = False) -> Composed:
    """Compose the tree of a request on a commit (THE TREE, THE COMPOSITION).

    Args:
        root: The top of the work tree of the repository
        request: The request
        head: The full name of the commit that the tree starts from
        with_diff: Give the diff of the tree against the commit, too

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails
    """
    own = {path.rstrip("/") for path in request.paths}
    with scratch_repository(root) as scratch:
        git.run(scratch, "read-tree", head)
        if request.paths:
            git.run(scratch, "--work-tree", str(root), "add", "-A", "--", *request.paths)
        before_patches = git.run(scratch, "write-tree").decode().strip()
        for patch in request.patches:
            git.run(scratch, "apply", "--cached", str(patch))
        after_patches = git.run(scratch, "write-tree").decode().strip()
        patched = names(
            git.run(scratch, "diff-tree", "-r", "-z", "--name-only", "--no-renames", before_patches, after_patches)
        )
        shared = list(dict.fromkeys([*edited_paths(request), *patched]))
        overlap = sorted(set(shared) & own)
        if overlap:
            raise LandError(f"{overlap} is both a shared file and an own path")
        for path in edited_paths(request):
            listing = git.run(root, "ls-tree", head, "--", path).decode().split()
            if not listing:
                raise LandError(f"the shared file {path} is not in HEAD")
            blob = git.run(scratch, "rev-parse", f":{path}").decode().strip()
            content = git.run(root, "cat-file", "blob", blob).decode("utf-8")
            for edit in request.edits:
                if edit.path == path:
                    content = apply_edit(content, edit, f"HEAD:{path}")
            written = git.run(root, "hash-object", "-w", "--stdin", stdin=content.encode("utf-8")).decode().strip()
            git.run(scratch, "update-index", "--cacheinfo", f"{listing[0]},{written},{path}")
        changed = names(git.run(scratch, "diff", "--cached", "-z", "--name-only", "--no-renames", head))
        allowed = own | set(shared)
        for path in changed:
            if not any(path == item or path.startswith(item + "/") for item in allowed):
                raise LandError(f"REFUSED, '{path}' changed but is not listed")
        tree = git.run(scratch, "write-tree").decode().strip()
        diff = git.run(scratch, "diff", "--cached", head).decode("utf-8", "replace") if with_diff else ""
    return Composed(head, tree, tuple(changed), diff)


def grow_commit(root: Path, first: str, grows: Sequence[Grow], changed: Sequence[str]) -> str:
    """Write the second commit of a growth on the first commit, and return its full name (THE GROWTH).

    Raises:
        LandError: If the change does not change the ledger of a rule, or the ledger of the rule history does not read
        git.GitError: If a git command fails
    """
    for grow in grows:
        if f"{rule_ledger.LEDGER_DIRECTORY}/{grow.rule}.txt" not in changed:
            raise LandError(f"--grow {grow.rule}: the change does not change the ledger of the rule {grow.rule}")
    text = git.show(root, first, HISTORY_LEDGER)
    if text is None:
        raise LandError(f"the commit holds no {HISTORY_LEDGER}, and --grow writes its rows there")
    try:
        ledger = rule_ledger.parse(text.decode("utf-8"), HISTORY_LEDGER, HISTORY_RULE, rule_registry.Policy.ADMIT)
    except ValueError as problem:
        raise LandError(f"{HISTORY_LEDGER} does not read: {problem}") from None
    rows: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for grow in grows:
        key = rule_ledger.Key(f"{rule_ledger.LEDGER_DIRECTORY}/{grow.rule}.txt", GROW_KIND, first[:SHORT_HASH])
        rows[key] = rule_ledger.Entry(key, rule_ledger.ADMIT_VALUE, grow.reason)
    rendered = rule_ledger.render(replace(ledger, base={**ledger.base, **rows}))
    blob = git.run(root, "hash-object", "-w", "--stdin", stdin=rendered.encode("utf-8")).decode().strip()
    mode = git.run(root, "ls-tree", first, "--", HISTORY_LEDGER).decode().split()[0]
    with scratch_repository(root) as scratch:
        git.run(scratch, "read-tree", first)
        git.run(scratch, "update-index", "--cacheinfo", f"{mode},{blob},{HISTORY_LEDGER}")
        tree = git.run(scratch, "write-tree").decode().strip()
    rules = ", ".join(grow.rule for grow in grows)
    message = f"Admit the rows that the commit {first[:SHORT_HASH]} adds to the ledgers of {rules}\n"
    return git.run(root, "commit-tree", tree, "-p", first, stdin=message.encode()).decode().strip()


# ── The landing ────────────────────────────────────────────────────────────


def branch_of(root: Path) -> str:
    """Return the branch of HEAD of a tree, such as refs/heads/main.

    Raises:
        LandError: If HEAD is not on a branch
    """
    completed = git.run_any_exit(root, "symbolic-ref", "-q", "HEAD")
    if completed.exit != 0:
        raise LandError(f"HEAD of {root} is not on a branch, and no branch can move")
    return completed.out.strip()


def commit_tree(root: Path, composed: Composed, message: str) -> str:
    """Write the commit of a composed tree on its HEAD, with the message, and return its full name."""
    return git.run(root, "commit-tree", composed.tree, "-p", composed.head, stdin=message.encode()).decode().strip()


def swap(root: Path, branch: str, new: str, head: str, subject: str) -> bool:
    """Move the branch to the new commit when it holds the commit head, and tell if it moved (THE COMPARE-AND-SWAP)."""
    return git.run_any_exit(root, "update-ref", "-m", f"loop land: {subject}", branch, new, head).exit == 0


def sync_index(root: Path, commit: str, paths: Sequence[str]) -> None:
    """Give the index of the repository the entry of a commit for each path, and do not change the working tree.

    A busy index only gives a warning, because the commit is done.
    """
    lines: list[str] = []
    for path in paths:
        entry = git.run(root, "ls-tree", "-z", commit, "--", path).decode("utf-8", "surrogateescape").rstrip("\0")
        if entry:
            meta, _ = entry.split("\t", 1)
            mode, _kind, blob = meta.split(" ")
            lines.append(f"{mode} {blob}\t{path}\n")
        else:
            lines.append(f"0 {'0' * 40}\t{path}\n")
    if git.run_any_exit(root, "update-index", "--index-info", stdin="".join(lines).encode()).exit != 0:
        check_report.write_line(
            "loop land: WARNING, the index of the repository was busy and is not updated", is_error=True
        )


def apply_patch(root: Path, patch: bytes, shown: str) -> None:
    """Give the working tree a patch unless it holds the patch, and write a warning when a hunk does not apply."""
    if git.run_any_exit(root, "apply", "--reverse", "--check", stdin=patch).exit == 0:
        return
    applied = git.run_any_exit(root, "apply", stdin=patch)
    if applied.exit != 0:
        check_report.write_line(
            f"loop land: WARNING, {shown} does not apply to the working tree:\n{applied.err}The owner must apply "
            f"these hunks by hand before the next commit of those files, or that commit reverts them.",
            is_error=True,
        )


def update_working_tree(root: Path, request: Request, grown: tuple[str, str] | None) -> None:
    """Give the working tree each patch, each edit and the rows of THE GROWTH that it does not hold.

    Args:
        root: The top of the work tree of the repository
        request: The request
        grown: The first and the second commit of a growth, or None (THE GROWTH)
    """
    for patch in request.patches:
        apply_patch(root, patch.read_bytes(), str(patch))
    if grown is not None:
        apply_patch(root, git.run(root, "diff", grown[0], grown[1], "--", HISTORY_LEDGER), HISTORY_LEDGER)
    for path in edited_paths(request):
        file = root / path
        content = file.read_text(encoding="utf-8")
        updated = content
        for edit in request.edits:
            if edit.path != path:
                continue
            state = edit_state(updated, edit.kind, edit.first, edit.second)
            if state == "ambiguous":
                check_report.write_line(
                    f"loop land: WARNING, the working-tree {path} lacks an edit, and its anchor or old text is not "
                    f"unique there.  Apply that edit by hand",
                    is_error=True,
                )
            elif state == "apply":
                updated = apply_edit(updated, edit, path)
        if updated != content:
            file.write_text(updated, encoding="utf-8")


def candidate_of(root: Path, request: Request, options: Options, head: str) -> tuple[gate.Candidate, Composed]:
    """Compose the commit of a request on head, and return the candidate of the gate with the composition.

    --check with an empty request gives HEAD as the candidate.

    Raises:
        LandError: If the request does not compose, or it changes nothing
        git.GitError: If a git command fails
    """
    if request.is_empty:
        return gate.Candidate(head, head, ()), Composed(head, "", (), "")
    composed = compose(root, request, head, with_diff=options.is_dry_run)
    if not composed.changed and not options.is_tree_only:
        raise LandError("the commit would change nothing")
    if options.is_dry_run or options.is_tree_only:
        return gate.Candidate(head, head, composed.changed), composed
    commit = commit_tree(root, composed, request.message)
    changed = composed.changed
    if request.grows:
        commit = grow_commit(root, commit, request.grows, composed.changed)
        changed = tuple(sorted({*changed, HISTORY_LEDGER}))
    return gate.Candidate(head, commit, changed), composed


def land(
    root: Path, request: Request, options: Options, check: Check, pause: Callable[[float], None] = time.sleep
) -> Outcome:
    """Compose the commit, run the gate, and move the branch to the commit by compare-and-swap.

    Args:
        root: The top of the work tree of the repository
        request: The request
        options: The options of the command line
        check: The gate (THE GATE)
        pause: The function that waits after a refused swap

    Returns:
        The outcome of the landing

    Raises:
        LandError: If the request does not compose, or HEAD keeps moving
        git.GitError: If a git command fails
    """
    branch = branch_of(root)
    verdict: gate.Verdict | None = None
    for composition in range(1, ATTEMPTS + 1):
        head = git.head(root)
        candidate, composed = candidate_of(root, request, options, head)
        if options.is_dry_run:
            check_report.write_line(composed.diff.rstrip("\n"))
            return Outcome(ExitCode.OK, None, composition, None, False)
        if options.is_tree_only:
            check_report.write_line(composed.tree)
            return Outcome(ExitCode.OK, None, composition, None, False)
        verdict = check(candidate, composition > 1)
        if not verdict.is_accepted:
            for problem in verdict.problems:
                check_report.write_line(f"loop land: REJECTED, {problem}", is_error=True)
            return Outcome(ExitCode.FINDINGS, candidate, composition, verdict, False)
        if options.is_check:
            check_report.write_line(f"loop land: the gate accepts the commit {candidate.commit[:SHORT_HASH]}")
            return Outcome(ExitCode.OK, candidate, composition, verdict, False)
        if swap(root, branch, candidate.commit, head, request.subject):
            check_report.write_line(f"loop land: committed {candidate.commit[:8]} on {head[:8]}:")
            for path in candidate.changed:
                check_report.write_line(f"  {path}")
            sync_index(root, candidate.commit, candidate.changed)
            first = git.run(root, "rev-parse", f"{candidate.commit}^").decode().strip() if request.grows else ""
            update_working_tree(root, request, (first, candidate.commit) if request.grows else None)
            return Outcome(ExitCode.OK, candidate, composition, verdict, True)
        check_report.write_line(
            f"loop land: HEAD moved, and the command composes the commit again (composition {composition + 1})",
            is_error=True,
        )
        pause(random.uniform(*PAUSE_RANGE_S))
    raise LandError(f"HEAD moved {ATTEMPTS} times, and the command stops")


# ── The record and the driver ──────────────────────────────────────────────


def write_record(work: Path, request: Request, options: Options, outcome: Outcome) -> None:
    """Append the record of one run of the gate to the file RECORDS of the work directory (THE RECORD)."""
    if outcome.candidate is None or outcome.verdict is None:
        return
    record = {
        "time": loop_history.iso_now(),
        "subject": request.subject,
        "mode": "check" if options.is_check else "land",
        "head": outcome.candidate.head,
        "commit": outcome.candidate.commit,
        "compositions": outcome.compositions,
        "landed": outcome.is_landed,
        **asdict(outcome.verdict),
    }
    work.mkdir(parents=True, exist_ok=True)
    with open(work / RECORDS, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, sort_keys=True) + "\n")


def no_check(candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
    """Refuse a run of the gate, for --dry-run and --tree, which print before the gate.

    Raises:
        LandError: Always
    """
    raise LandError(f"the gate cannot run on {candidate.commit} with --dry-run or --tree (retry {is_retry})")


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
            return land(root, request, options, no_check).status
        main_tree = driver_tree(root)
        if main_tree is not None and not options.is_here:
            return run_driver(main_tree, argv)
        work = cache.work_root() / WORK_NAME
        with gate.session(root, work, options.jobs, options.is_every) as session:
            outcome = land(root, request, options, session.check)
        write_record(work, request, options, outcome)
        return outcome.status
    except LandError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.USAGE
    except gate.GateError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
    except (git.GitError, proc.ToolFailed) as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
