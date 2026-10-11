"""land — compose the commit of a change on main and move main to it by compare-and-swap, or run one step of a guard run.

THE REQUEST
    The words before the word -- give the message and the edits.  The words
    after it give the own paths, and that list can be empty:
      -m TEXT                   One paragraph of the message.  The first paragraph is the subject
      --patch FILE              A unified diff that git made against HEAD
      --replace FILE OLD NEW    In the shared FILE, the text of the file NEW replaces the text of the file OLD
      --block FILE AFTER BLOCK  In the shared FILE, the text of the file BLOCK goes after the text of the file AFTER
      --dry-run                 Print the diff of the commit, and change nothing
      --tree                    Print the object name of the tree of the commit, and change nothing
    The value of an option can be the word --.  The command then reads the
    options in sequence to find the end of the options (split_words()).  The
    files of --patch, --replace and --block are paths from the current
    directory.  An own path and the shared FILE are paths from the top of the
    repository of the current directory.

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

THE COMPARE-AND-SWAP
    The branch of HEAD moves to the commit only when the branch holds the
    commit that the composition started from (`git update-ref REF NEW OLD`).
    When a different commit lands first, the command composes the tree again
    on the new HEAD, after a random pause.  After ATTEMPTS refusals, it
    stops.  A landing then cannot revert a different commit.

THE INDEX AND THE WORKING TREE
    After the commit, the index of the repository gets the entry of the
    commit for each changed path, and the working tree does not change.
    Then each patch goes to the working tree with git apply, unless the
    working tree holds it.  Each edit goes to the working-tree copy of its
    file, unless the copy holds it.  When a hunk does not apply, the command
    writes a warning.  The owner of the hunk must then apply it by hand
    before the next commit of that file, or that commit reverts it.

THE GUARD RUN
    `--step change|base|compare --name NAME` runs one step of a guard run of
    the same request.  The work directory is ROOT/NAME.  ROOT is the value of
    --root, else the directory land of the work root of assay.core.cache.
      change   Compose the tree of the request on HEAD.  Export it as one commit on HEAD, with the message.  Then build
               it and run its guards.  HEAD goes to head.txt
      base     Export the HEAD of head.txt with its history.  Then build it and run its guards
      compare  Run each test that is red on one side only again, alone, on each side.  Then print the verdict
    An export is a repository that borrows the objects of the repository
    through the file `objects/info/alternates`.  A guard that reads git
    history then reads the history of the landing.  Each build configures the
    Debug build of the export with the toolchain of the export.  It builds
    each target of GUARD_TARGETS that the export has.  When the change holds
    an arm of an architecture, the export also configures each parse build
    that `loop syntax --changed` names.  Then the step runs `ctest -L
    ci_guard` and the tests of ROSTER_TESTS.  It writes the name of each test
    with the status fail or notrun to SIDE.txt.  The compare step exits 0
    when no test is red only with the change.

    The change step and the base step hold the lock of the heavy steps for
    the build and the tests.  The compare step holds it for its test runs
    (assay.loop.heavy).  A lock on the work directory keeps two runs with one
    name apart.

Usage
    loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... [--block FILE AFTER BLOCK]...
              [--dry-run | --tree] -- [PATH]...
    loop land --step change|base|compare --name NAME [--root DIR] [--jobs N] [THE SAME REQUEST] -- [PATH]...

    Exit 0 when the commit landed or a step has no error.  Exit 1 when the
    compare step finds a test that is red only with the change.  Exit 2 on a
    usage error, or on a request that does not compose.  Exit 4 when a tool
    fails.

`guard selftest tests.loop.test_land` does a test of the module on a planted repository.
"""

import contextlib
import fcntl
import random
import shutil
import sys
import tarfile
import tempfile
import time
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cache, git, proc
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import heavy
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

# The refusals of the compare-and-swap before the command stops, and the range of the random pause after one (THE
# COMPARE-AND-SWAP).
ATTEMPTS = 8
PAUSE_RANGE_S = (0.1, 0.9)
# The two kinds of an edit of a shared file (THE REQUEST).
INSERT = "insert"
REPLACE = "replace"
# The count of the values of each option, for the division of the words (split_words()).
VALUE_COUNTS = {"-m": 1, "--patch": 1, "--replace": 3, "--block": 3, "--step": 1, "--name": 1, "--root": 1, "--jobs": 1}
FLAG_DRY_RUN = "--dry-run"
FLAG_TREE = "--tree"
END_OF_OPTIONS = "--"
HELP_WORDS = (["-h"], ["--help"])
USAGE = (
    "usage: loop land -m SUBJECT [-m BODY]... [--patch FILE]... [--replace FILE OLD NEW]... "
    "[--block FILE AFTER BLOCK]...\n"
    "                 [--dry-run | --tree] -- [PATH]...\n"
    "       loop land --step change|base|compare --name NAME [--root DIR] [--jobs N] [THE SAME REQUEST] -- [PATH]..."
)
# The steps of a guard run, and the subject of a change commit of a request with no message (THE GUARD RUN).
STEPS = ("change", "base", "compare")
DEFAULT_SUBJECT = "Change with no message under a guard run"
# The targets that a build of a guard run builds when the export has them.  A guard that reads a binary then runs.
# test_armed_roster and test_row_hash_wrappers include each header.  A new gate predicate or namespace with no row of
# its roster or census then breaks them.  The label ci_guard does not see that.
GUARD_TARGETS = ("crucible", "test_forgeable_proofs", "dump_row_hashes", "test_armed_roster", "test_row_hash_wrappers")
ROSTER_TESTS = ("test_armed_roster", "test_row_hash_wrappers")
GUARD_LABEL = "ci_guard"
# The ctest limit of one test, and the limit of the run of the label, in seconds.
TEST_TIMEOUT_S = 1500
LABEL_TIMEOUT_S = 3000.0
# The statuses of a test that count as red.
RED = frozenset({ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.NOTRUN})
# The niceness of a build of a guard run.
BUILD_NICENESS = "10"
# The file of the shared git directory that the scratch repository copies (THE COMPOSITION).
EXCLUDE_FILE = Path("info/exclude")


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
class Request:
    """The message, the patches, the edits and the own paths of one landing (THE REQUEST)."""

    messages: tuple[str, ...]
    patches: tuple[Path, ...]
    edits: tuple[Edit, ...]
    paths: tuple[str, ...]

    @property
    def message(self) -> str:
        """The message of the commit: the paragraphs with one empty line between two of them, and a newline at the end."""
        return "\n\n".join(self.messages or (DEFAULT_SUBJECT,)) + "\n"

    @property
    def subject(self) -> str:
        """The first paragraph of the message."""
        return self.messages[0] if self.messages else DEFAULT_SUBJECT


@dataclass(frozen=True, slots=True)
class Options:
    """The words of the command line that are not part of the request."""

    is_dry_run: bool
    is_tree_only: bool
    step: str | None
    name: str | None
    root: Path | None
    jobs: int


@dataclass(frozen=True, slots=True)
class Composed:
    """The result of one composition: the HEAD that it starts from, the tree, the changed paths, and the diff."""

    head: str
    tree: str
    changed: tuple[str, ...]
    diff: str


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


def is_name(word: str) -> bool:
    """Tell if a word is a name of a guard run: letters, digits, - and _, and not empty."""
    return bool(word) and word.isascii() and word.replace("-", "").replace("_", "").isalnum()


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
    flags = {FLAG_DRY_RUN: False, FLAG_TREE: False}
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
    request = Request(
        tuple(item[0] for item in values["-m"]),
        tuple((current / item[0]).resolve() for item in values["--patch"]),
        tuple(edits),
        tuple(paths),
    )
    single: dict[str, str | None] = {}
    for option in ("--step", "--name", "--root", "--jobs"):
        if len(values[option]) > 1:
            raise LandError(f"give {option} one time")
        single[option] = values[option][0][0] if values[option] else None
    step, name, jobs_text = single["--step"], single["--name"], single["--jobs"]
    if step is not None and step not in STEPS:
        raise LandError(f"--step takes one of {', '.join(STEPS)}, not {step!r}")
    if step is not None and (name is None or not is_name(name)):
        raise LandError("a step of a guard run takes --name NAME, a word of letters, digits, - and _")
    if jobs_text is not None and (not jobs_text.isdigit() or int(jobs_text) < 1):
        raise LandError(f"--jobs takes a number above 0, not {jobs_text!r}")
    if step is None and not request.messages and not flags[FLAG_TREE]:
        raise LandError("give the message of the commit with -m")
    if not request.paths and not request.edits and not request.patches:
        raise LandError("the request holds nothing to commit")
    root = Path(single["--root"]).resolve() if single["--root"] is not None else None
    jobs = int(jobs_text) if jobs_text is not None else proc.DEFAULT_JOBS
    return request, Options(flags[FLAG_DRY_RUN], flags[FLAG_TREE], step, name, root, jobs)


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


def shared_directory(root: Path) -> Path:
    """Return the git directory that each work tree of the repository of a tree shares.

    Raises:
        git.GitError: If root is not in a git work tree
    """
    return Path(git.run(root, "rev-parse", "--path-format=absolute", "--git-common-dir").decode().strip())


@contextlib.contextmanager
def scratch_repository(root: Path) -> Iterator[Path]:
    """Make a scratch repository with a link to the object directory of the repository (THE COMPOSITION).

    The block gets the scratch repository.  The command removes it after the block.
    """
    shared = shared_directory(root)
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


# ── The landing ────────────────────────────────────────────────────────────


def branch_of(root: Path) -> str:
    """Return the branch of HEAD of a tree, such as refs/heads/main.

    Raises:
        LandError: If HEAD is not on a branch
    """
    completed = git.run_any_exit(root, "symbolic-ref", "-q", "HEAD")
    if completed.exit != 0:
        raise LandError(f"HEAD of {root} is not on a branch, so no branch can move")
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


def update_working_tree(root: Path, request: Request) -> None:
    """Give the working tree each patch and each edit that it does not hold (THE INDEX AND THE WORKING TREE)."""
    for patch in request.patches:
        if git.run_any_exit(root, "apply", "--reverse", "--check", str(patch)).exit == 0:
            continue
        applied = git.run_any_exit(root, "apply", str(patch))
        if applied.exit != 0:
            check_report.write_line(
                f"loop land: WARNING, {patch} does not apply to the working tree:\n{applied.err}The owner must apply "
                f"these hunks by hand before the next commit of those files, or that commit reverts them.",
                is_error=True,
            )
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


def land(root: Path, request: Request, options: Options, pause: Callable[[float], None] = time.sleep) -> int:
    """Compose the commit and move the branch to it by compare-and-swap, or print the diff or the tree.

    Args:
        root: The top of the work tree of the repository
        request: The request
        options: The options of the command line
        pause: The function that waits after a refused swap

    Returns:
        The exit status

    Raises:
        LandError: If the request does not compose
        git.GitError: If a git command fails
    """
    branch = branch_of(root)
    for attempt in range(1, ATTEMPTS + 1):
        head = git.head(root)
        composed = compose(root, request, head, with_diff=options.is_dry_run)
        if not composed.changed and not options.is_tree_only:
            raise LandError("the commit would change nothing")
        if options.is_dry_run:
            check_report.write_line(composed.diff.rstrip("\n"))
            return ExitCode.OK
        if options.is_tree_only:
            check_report.write_line(composed.tree)
            return ExitCode.OK
        new = commit_tree(root, composed, request.message)
        if swap(root, branch, new, head, request.subject):
            check_report.write_line(f"loop land: committed {new[:8]} on {head[:8]}:")
            for path in composed.changed:
                check_report.write_line(f"  {path}")
            sync_index(root, new, composed.changed)
            update_working_tree(root, request)
            return ExitCode.OK
        check_report.write_line(f"loop land: HEAD moved, composing again (attempt {attempt})", is_error=True)
        pause(random.uniform(*PAUSE_RANGE_S))
    raise LandError(f"HEAD kept moving through {ATTEMPTS} attempts, so the command stops")


# ── The guard run ──────────────────────────────────────────────────────────


@contextlib.contextmanager
def work_lock(work: Path) -> Iterator[None]:
    """Hold the lock of one work directory for the block (THE GUARD RUN).

    Raises:
        LandError: If a different run holds the lock
    """
    work.parent.mkdir(parents=True, exist_ok=True)
    with open(f"{work}.lock", "a", encoding="ascii") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise LandError(f"another run holds {work}.  Wait for it, or give a different --name") from None
        yield


def export_tree(root: Path, treeish: str, destination: Path) -> None:
    """Write the files of a tree or a commit of the repository into a new directory."""
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="loop-land-archive-") as scratch:
        archive = Path(scratch) / "tree.tar"
        git.run(root, "archive", "--format=tar", "-o", str(archive), treeish, timeout_s=proc.TOOL_S)
        with tarfile.open(archive) as bundle:
            bundle.extractall(destination, filter="data")


def make_repository(source: Path, export: Path, head: str, message: str | None, composed: str | None) -> None:
    """Make an export a repository with the HEAD commit head and the objects of the source (THE GUARD RUN).

    The index starts from the tree of head.  A file of the index that .gitignore matches then stays in the index.  A
    change export (a message that is not None) with a tree other than that of head gets one commit on head with the
    message.

    Args:
        source: A tree of the repository
        export: The export
        head: The full name of the HEAD commit of the source
        message: The message of the change commit, or None for the base export
        composed: The tree that the export must hold, or None

    Raises:
        LandError: If the export holds a different tree
    """
    git.init(export)
    info = export / ".git" / "objects" / "info"
    info.mkdir(parents=True, exist_ok=True)
    (info / "alternates").write_text(f"{shared_directory(source) / 'objects'}\n", encoding="utf-8")
    git.run(export, "update-ref", "HEAD", head)
    git.run(export, "read-tree", head)
    git.run(export, "add", "-A", "--", ".")
    tree = git.run(export, "write-tree").decode().strip()
    head_tree = git.run(export, "rev-parse", f"{head}^{{tree}}").decode().strip()
    if composed is not None and tree != composed:
        raise LandError(f"the export {export} holds the tree {tree}, and the composed tree is {composed}")
    if message is None:
        if tree != head_tree:
            raise LandError(f"the base export {export} holds the tree {tree}, and HEAD {head} holds {head_tree}")
        return
    if tree == head_tree:
        check_report.write_line("loop land: the change tree is the tree of HEAD, so the change export has no commit")
        return
    commit = git.run(export, "commit-tree", tree, "-p", head, stdin=message.encode("utf-8")).decode().strip()
    git.run(export, "update-ref", "HEAD", commit, head)


def build_tool(build: Path, name: str) -> str:
    """Return the cmake or the ctest of a configured build directory.

    Raises:
        LandError: If the CMake cache does not name the program
    """
    try:
        return cmake_pin.configured_program(build, name)
    except cmake_pin.PinError as problem:
        raise LandError(str(problem)) from None


def run_logged(command: list[str], cwd: Path, log: Path, timeout_s: float) -> int:
    """Run one command with its two outputs in a log, and return its exit.  A command that runs too long gives its exit."""
    try:
        return proc.run(proc.Command(command, timeout_s=timeout_s, cwd=cwd, stdout=log, stderr=proc.Output.MERGE)).exit
    except proc.TimedOut as problem:
        return problem.completed.exit if problem.completed is not None else ExitCode.TOOL_FAILED


def prepare(export: Path, build: Path, jobs: int) -> None:
    """Configure the Debug build of an export, build the guard targets, and configure the parse builds of an arm.

    The cmake of the first configure is the cmake of PATH, and the configure refuses a cmake that is not the pin.

    Raises:
        LandError: If a configure or the build fails
    """
    shutil.rmtree(build, ignore_errors=True)
    configure = [
        "cmake",
        "-S",
        str(export),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={export / 'cmake' / 'Toolchain-gcc16.cmake'}",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCRUCIBLE_BENCH=OFF",
        "-DCRUCIBLE_WERROR=ON",
    ]
    if run_logged(configure, export, Path(f"{build}.configure.log"), proc.BUILD_S) != 0:
        raise LandError(f"the configure of {build} failed, see {build}.configure.log")
    producer = ninja_reader.graph(build).producer
    targets = [target for target in GUARD_TARGETS if target in producer]
    cmake = build_tool(build, "cmake")
    command = ["nice", "-n", BUILD_NICENESS, cmake, "--build", str(build), "--target", *targets, "-j", str(jobs)]
    if run_logged(command, export, Path(f"{build}.build.log"), proc.BUILD_S) != 0:
        raise LandError(f"the build of {build} failed, see {build}.build.log")
    loop = str(export / "utils" / "bin" / "loop")
    try:
        presets = proc.run(
            proc.Command([sys.executable, loop, "syntax", "--changed", "--root", str(export)], timeout_s=proc.TOOL_S)
        )
    except proc.ToolFailed as problem:
        raise LandError(f"the parse presets of the change of {export} cannot be read: {problem}") from None
    if presets.exit not in (ExitCode.OK, ExitCode.USAGE):
        raise LandError(f"the parse presets of the change of {export} cannot be read: {presets.err.strip()}")
    for preset in presets.out.split() if presets.exit == ExitCode.OK else ():
        log = Path(f"{build}.{preset}.configure.log")
        if run_logged(["cmake", "--preset", preset], export, log, proc.BUILD_S) != 0:
            raise LandError(f"the configure of the parse build {preset} of {export} failed, see {log}")


def red_tests(build: Path, report: Path, jobs: int) -> list[str]:
    """Run the guards of a build and the roster tests, write the name of each red test to the report, and return them.

    Raises:
        LandError: If ctest writes no JUnit report
    """
    ctest = build_tool(build, "ctest")
    runs = (
        (["-L", GUARD_LABEL, "-j", str(jobs)], LABEL_TIMEOUT_S, "label"),
        (["-R", f"^({'|'.join(ROSTER_TESTS)})$"], float(TEST_TIMEOUT_S), "roster"),
    )
    red: set[str] = set()
    for words, timeout_s, part in runs:
        junit = Path(f"{report}.{part}.junit.xml")
        junit.unlink(missing_ok=True)
        command = [ctest, *words, "--timeout", str(TEST_TIMEOUT_S), "--output-junit", str(junit)]
        run_logged(command, build, Path(f"{report}.{part}.log"), timeout_s)
        try:
            statuses = ctest_reader.results(junit)
        except ctest_reader.CtestError as problem:
            raise LandError(f"the tests of {build} gave no result: {problem}") from None
        red |= {name for name, status in statuses.items() if status in RED}
    write_names(report, red)
    return sorted(red)


def read_names(path: Path) -> set[str]:
    """Return the names of a report of red tests."""
    return {line for line in path.read_text(encoding="utf-8").splitlines() if line}


def write_names(path: Path, found: set[str]) -> None:
    """Write the names of red tests to a report, sorted."""
    path.write_text("".join(f"{name}\n" for name in sorted(found)), encoding="utf-8")


def step_change(root: Path, work: Path, request: Request, jobs: int) -> int:
    """Compose, export and build the change side, and run its guards (THE GUARD RUN)."""
    head = git.head(root)
    composed = compose(root, request, head)
    check_report.write_line(f"loop land: HEAD {head[:9]}, change tree {composed.tree}, work directory {work}")
    export_tree(root, composed.tree, work / "change")
    (work / "head.txt").write_text(f"{head}\n", encoding="utf-8")
    (work / "message.txt").write_text(request.message, encoding="utf-8")
    if not request.messages:
        check_report.write_line(f"loop land: no -m paragraph, so the change commit has the subject '{DEFAULT_SUBJECT}'")
    make_repository(root, work / "change", head, request.message, composed.tree)
    with heavy.heavy_step(heavy.FULL_LEVEL_GB, root):
        prepare(work / "change", work / "change-build", jobs)
        found = red_tests(work / "change-build", work / "change.txt", jobs)
    check_report.write_line(f"loop land: change step done, {len(found)} red")
    return ExitCode.OK


def step_base(root: Path, work: Path, jobs: int) -> int:
    """Export and build the HEAD of the change step, and run its guards (THE GUARD RUN)."""
    recorded = work / "head.txt"
    if not recorded.is_file():
        raise LandError("run the change step first")
    head = recorded.read_text(encoding="utf-8").strip()
    export_tree(root, head, work / "base")
    make_repository(root, work / "base", head, None, git.run(root, "rev-parse", f"{head}^{{tree}}").decode().strip())
    with heavy.heavy_step(heavy.FULL_LEVEL_GB, root):
        prepare(work / "base", work / "base-build", jobs)
        found = red_tests(work / "base-build", work / "base.txt", jobs)
    check_report.write_line(f"loop land: base step done, {len(found)} red")
    return ExitCode.OK


def has_no_failure_alone(build: Path, name: str) -> bool:
    """Run one test alone in a build, and tell if it gives the status 0."""
    command = [build_tool(build, "ctest"), "-R", f"^{name}$", "--timeout", str(TEST_TIMEOUT_S)]
    try:
        return proc.run(proc.Command(command, timeout_s=float(TEST_TIMEOUT_S), cwd=build)).exit == 0
    except proc.ToolFailed:
        return False


def step_compare(root: Path, work: Path) -> int:
    """Run each test that is red on one side only again, alone, on each side, and print the verdict (THE GUARD RUN).

    A test that fails on one side only runs one more time on each side, alone.  Then a failure that only the
    parallel load caused is not a difference.
    """
    if not (work / "change.txt").is_file() or not (work / "base.txt").is_file():
        raise LandError("run the change and base steps first")
    sides = {side: read_names(work / f"{side}.txt") for side in ("change", "base")}
    with heavy.heavy_step(heavy.NAMED_LEVEL_GB, root):
        for name in sorted(sides["change"] ^ sides["base"]):
            for side, found in sides.items():
                if has_no_failure_alone(work / f"{side}-build", name):
                    found.discard(name)
                else:
                    found.add(name)
    for side, found in sides.items():
        write_names(work / f"{side}.txt", found)
    only_change = sorted(sides["change"] - sides["base"])
    only_base = sorted(sides["base"] - sides["change"])
    both = sorted(sides["change"] & sides["base"])
    check_report.write_line(f"red only with the change: {' '.join(only_change) or 'none'}")
    check_report.write_line(f"red only on HEAD:         {' '.join(only_base) or 'none'}")
    check_report.write_line(f"red on both:              {' '.join(both) or 'none'}")
    check_report.write_line(f"full logs: {work}/change.txt.label.log and {work}/base.txt.label.log")
    return ExitCode.FINDINGS if only_change else ExitCode.OK


def guard_run(root: Path, request: Request, options: Options) -> int:
    """Run one step of a guard run of the request (THE GUARD RUN)."""
    name = options.name or ""
    work = (options.root or cache.work_root() / "land") / name
    with work_lock(work):
        work.mkdir(parents=True, exist_ok=True)
        if options.step == "change":
            return step_change(root, work, request, options.jobs)
        if options.step == "base":
            return step_base(root, work, options.jobs)
        return step_compare(root, work)


def main(argv: list[str]) -> int:
    """Land a request, or run one step of a guard run of it.

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
        if options.step is not None:
            return guard_run(root, request, options)
        return land(root, request, options)
    except LandError as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.USAGE
    except (git.GitError, proc.ToolFailed, ninja_reader.NinjaError) as problem:
        check_report.write_line(f"loop land: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
