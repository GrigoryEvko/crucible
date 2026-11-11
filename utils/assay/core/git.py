"""The one module of the package that runs git: the files and the history of a tree, and a throwaway repository.

THE COMMAND
    run() runs one git command in a tree, through assay.core.proc, with a
    time limit of TIMEOUT_S, and it returns the standard output.  A command
    that cannot start, that runs past its limit, or that exits with a status
    that is not 0 raises GitError.  run_any_exit() gives the result for each
    exit, for a command whose exit is its answer.  head(), resolve(),
    is_clean() and show() give the answers that callers ask for most, so a
    caller reads no output of git itself.  Each question of a caller gets a
    function here with a typed answer.

THE KINDS OF THE FUNCTIONS
    READS_HISTORY names each function that reads a commit or a ref: the log,
    an object of a revision, HEAD or a branch.  READS_NAMES names each
    function that reads the names of the files of a tree, which .gitignore
    can change.  A function can be in the two sets.  READS_NEITHER names each
    other function.  A function also has the kinds of each function of this
    module that it calls, because a caller sees only the name that it calls.
    `loop test` gives the HEAD commit only to a test that can call a function
    of READS_HISTORY (assay.loop.inputs).  It gives the list of the files of
    the tree only to a test that can call a function of READS_NAMES.  The
    test of this module refuses a function with no kind, and a function of
    READS_NEITHER with a second kind.

THE OBJECTS
    write_tree(), hash_object(), commit_tree(), tree_entry(),
    index_object(), set_index_entry(), tree_changes(), index_changes() and
    index_patch() write the objects of a commit.  `loop land` calls them in
    a scratch repository that shares the object store of the repository.
    symbolic_head(), swap_ref() and set_index_entries() move a branch by
    compare-and-swap, and they give the index the entries of the commit.
    is_ancestor() tells if a branch holds a commit, and git_dir() gives the
    git directory of one work tree.

THE ISOLATION
    A repository variable in the environment of the caller wins over
    `git -C DIR`: with GIT_DIR set, `git -C tmp init` initializes the
    repository that GIT_DIR names again, and a later add and commit land
    there.  On 2026-09-24 an exported GIT_DIR sent the fixture commits of one
    self-test into the real repository and emptied its main branch.

    assay.core.proc gives no child a variable of proc.REPOSITORY_VARIABLES,
    so each git command of this module resolves its repository from the
    directory of -C, also when this process holds such a variable.  init()
    makes a repository and stops unless git resolves the directory itself.

THE CHANGE
    change_base() gives the commit that a change starts from, and
    changed_files() gives each path that the change touches.  The rules of
    the parse builds read them (THE CHANGE OF AN ARM of assay.loop.syntax).
    The base is the first of these commits that applies:
      * The commit that the environment variable CHANGE_BASE_VARIABLE names.
      * When HEAD is on a branch other than main, and the repository has a
        branch main: the merge base of HEAD and main.  A work tree then
        judges each change that its branch adds to main, also after a merge
        of main.  A commit of main that comes in with the merge is no change.
      * When the working tree changed a file of HEAD, or it holds a new file
        that .gitignore does not match: HEAD.  The change is then the working
        tree, as in a checkout of main before a commit.
      * Else the first parent of HEAD.  The change is then the last commit,
        as in a clone at the commit of a change.  A merge commit at HEAD is
        judged against its first parent.
    A root commit with no change in the working tree has no base.

THE BORROWED INDEX
    borrowing_repository() makes a scratch repository.  Its object directory
    is a link to the object directory of a repository.  A tree that its index
    composes, and each blob, tree and commit that it writes, are then in that
    repository.  This module cannot give git an index file in the
    environment, because assay.core.proc gives no child a repository
    variable.  The scratch repository gets a copy of the file `info/exclude`
    of the repository, and it runs no garbage collection.
    replaced_commit() writes a commit with the parents and the message of a
    commit, and its tree with some files replaced, through such an index.
"""

import contextlib
import os
import shutil
import tempfile
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc

# The identity of a commit of a throwaway repository.
SCRATCH_IDENTITY = {"GIT_AUTHOR_NAME": "crucible-scratch", "GIT_AUTHOR_EMAIL": "crucible-scratch@invalid",
                    "GIT_COMMITTER_NAME": "crucible-scratch", "GIT_COMMITTER_EMAIL": "crucible-scratch@invalid"}
# The time limit of one git command, in seconds.
TIMEOUT_S = 300.0
# The environment variable that names the base of a change (THE CHANGE), and the branch that a work tree branches from.
CHANGE_BASE_VARIABLE = "CRUCIBLE_CHANGE_BASE"
MAIN_BRANCH = "main"
# The options of `git ls-files` that list each file that is not in the index and that .gitignore does not match.
NEW_FILE_OPTIONS = ("--others", "--exclude-standard")
# The file of the shared git directory that a borrowing repository copies, and the mode of a new file of a tree (THE
# BORROWED INDEX).
EXCLUDE_FILE = Path("info/exclude")
PLAIN_FILE_MODE = "100644"
# The head line of `git status --porcelain=v2 --branch` that names the commit, and its word for no commit.
BRANCH_OID = "# branch.oid "
INITIAL_COMMIT = "(initial)"
# The object name that removes a path from the index in `git update-index --index-info`.
NULL_OBJECT = "0" * 40
# The kind of each function of this module (THE KINDS OF THE FUNCTIONS).
READS_HISTORY = frozenset({
    "archive", "blobs", "change_base", "changed_files", "commit_subject", "has_no_change", "head", "index_changes",
    "index_patch", "is_ancestor", "is_clean", "is_index_clean", "message_of", "parent", "patch", "quiet_diff",
    "replaced_commit", "resolve", "short_name", "show", "status", "symbolic_head", "tree_changes", "tree_entry",
    "work_changes",
})
READS_NAMES = frozenset({
    "change_base", "changed_files", "has_no_change", "listed_files", "listed_paths", "new_files", "tracked_files",
})
READS_NEITHER = frozenset({
    "apply_patch", "borrowing_repository", "commit_all", "commit_tree", "common_dir", "git_dir", "hash_files",
    "hash_object", "holds_patch", "index_object", "init", "null_separated", "run", "run_any_exit",
    "scratch_repository", "set_index_entries", "set_index_entry", "swap_ref", "toplevel", "write_tree",
})


class NotIsolated(Exception):
    """git resolves a repository other than the fixture directory."""


class GitError(Exception):
    """A git command cannot run, or it exits with a status that is not 0."""


@dataclass(frozen=True, slots=True)
class Status:
    """The state of a tree: its commit, or None with no commit, and the number of index files that are not as in it."""

    commit: str | None
    changed: int


@dataclass(frozen=True, slots=True)
class TreeEntry:
    """One entry of a tree object: its mode, its type and the name of its object."""

    mode: str
    kind: str
    name: str


def run_any_exit(root: Path, *arguments: str, stdin: bytes = b"", extra: Mapping[str, str] | None = None,
                 timeout_s: float = TIMEOUT_S) -> proc.Completed:
    """Run one git command in a tree, and return its result whatever its exit.

    Raises:
        GitError: If git cannot start, or it runs past its limit
    """
    command = proc.Command(["git", "-C", str(root), *arguments], timeout_s=timeout_s, stdin=stdin,
                           env=proc.environment(dict(extra or {})))
    try:
        return proc.run(command)
    except proc.ToolFailed as problem:
        raise GitError(f"git {' '.join(arguments[:2])} cannot run in {root}: {problem}") from None


def run(root: Path, *arguments: str, stdin: bytes = b"", extra: Mapping[str, str] | None = None,
        timeout_s: float = TIMEOUT_S) -> bytes:
    """Run one git command in a tree, and return its standard output.

    Raises:
        GitError: If git cannot start, it runs past its limit, or it exits with a status that is not 0
    """
    completed = run_any_exit(root, *arguments, stdin=stdin, extra=extra, timeout_s=timeout_s)
    if completed.exit != 0:
        raise GitError(f"`git {' '.join(arguments[:2])}` failed in {root}: {completed.err.strip()}")
    return completed.stdout


def init(root: Path) -> None:
    """Make a repository in root, and refuse to go on unless git resolves root itself.

    Raises:
        GitError: If git fails
        NotIsolated: If git resolves a repository outside root
    """
    run(root, "init", "-q")
    resolved = run(root, "rev-parse", "--absolute-git-dir").decode().strip()
    if Path(resolved).resolve() != (root / ".git").resolve():
        raise NotIsolated(f"the fixture repository {root} resolves to {resolved}; the self-test stops before it "
                          f"writes to a repository that is not its own")


def toplevel(root: Path) -> Path:
    """Return the top of the work tree that holds root.

    Raises:
        GitError: If root is not in a git work tree
    """
    return Path(run(root, "rev-parse", "--show-toplevel").decode().strip())


def common_dir(root: Path) -> Path:
    """Return the git directory that each work tree of the repository of a tree shares, as an absolute path.

    Raises:
        GitError: If root is not in a git work tree
    """
    return Path(run(root, "rev-parse", "--path-format=absolute", "--git-common-dir").decode().strip())


def head(root: Path) -> str:
    """Return the full hash of the HEAD commit of a tree.

    Raises:
        GitError: If root is not in a git work tree, or it has no commit
    """
    return run(root, "rev-parse", "HEAD").decode().strip()


def resolve(root: Path, revision: str) -> str | None:
    """Return the full hash of the commit that a revision names, or None when it names no commit.

    Raises:
        GitError: If git cannot start, or root is not in a git work tree
    """
    completed = run_any_exit(root, "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}")
    if completed.exit == 0:
        return completed.out.strip()
    if completed.exit == 1:
        return None
    raise GitError(f"`git rev-parse --verify {revision}` failed in {root}: {completed.err.strip()}")


def is_clean(root: Path, pathspec: Sequence[str] = ()) -> bool:
    """Whether the tracked files of a tree, or of the pathspec, are the same as in the HEAD commit.

    Raises:
        GitError: If git cannot start, or root is not in a git work tree with a commit
    """
    return quiet_diff(root, ["HEAD", "--", *pathspec])


def quiet_diff(root: Path, options: Sequence[str]) -> bool:
    """Tell if `git diff --quiet` with the options finds no difference.

    Raises:
        GitError: If git cannot start, or it gives a status other than 0 and 1
    """
    completed = run_any_exit(root, "diff", "--quiet", *options)
    if completed.exit in (0, 1):
        return completed.exit == 0
    raise GitError(f"`git diff --quiet {' '.join(options[:2])}` failed in {root}: {completed.err.strip()}")


def show(root: Path, revision: str, path: str) -> bytes | None:
    """Return the content of a file at a revision, or None when the revision has no such file.

    Raises:
        GitError: If git fails
    """
    return blobs(root, [f"{revision}:{path}"])[0]


def tracked_files(root: Path, pathspec: Sequence[str] = (), with_new: bool = False) -> tuple[str, ...]:
    """Return each tracked file of a tree, sorted.  With with_new, also each new file that .gitignore does not match.

    Raises:
        GitError: If root is not in a git work tree
    """
    return listed_files(root, ["--cached", *(NEW_FILE_OPTIONS if with_new else ())], pathspec)


def listed_files(root: Path, options: Sequence[str], pathspec: Sequence[str] = ()) -> tuple[str, ...]:
    """Return each file that `git ls-files -z` lists with the options and that the tree holds, sorted.

    Raises:
        GitError: If root is not in a git work tree
    """
    listed = null_separated(run(root, "ls-files", "-z", *options, "--", *pathspec))
    return tuple(sorted({path for path in listed if (root / path).is_file()}))


def blobs(root: Path, names: Sequence[str]) -> list[bytes | None]:
    """Read each object name (REVISION:PATH) with one `git cat-file --batch`, or None for a missing object.

    Raises:
        GitError: If git fails or its output ends early

    Complexity: linear in the size of the objects.
    """
    if not names:
        return []
    output = run(root, "cat-file", "--batch", stdin="".join(f"{name}\n" for name in names).encode())
    contents: list[bytes | None] = []
    position = 0
    for _ in names:
        end = output.find(b"\n", position)
        if end < 0:
            raise GitError(f"the output of `git cat-file --batch` in {root} ends before its last object")
        header = output[position:end].decode(errors="replace").split(" ")
        position = end + 1
        if len(header) == 3 and header[1] == "blob":
            size = int(header[2])
            contents.append(output[position:position + size])
            position += size + 1
        elif len(header) == 3:
            position += int(header[2]) + 1
            contents.append(None)
        else:
            contents.append(None)
    return contents


def new_files(root: Path) -> tuple[str, ...]:
    """Return each file of a tree that is not in the index and that .gitignore does not match, sorted.

    Raises:
        GitError: If root is not in a git work tree
    """
    return listed_files(root, NEW_FILE_OPTIONS)


def change_base(root: Path) -> str | None:
    """Return the full hash of the base of the change of a tree (THE CHANGE), or None for a root commit with no change.

    Raises:
        GitError: If root is not in a git work tree, it has no commit, or the variable names no commit
    """
    named = os.environ.get(CHANGE_BASE_VARIABLE, "")
    if named:
        commit = resolve(root, named)
        if commit is None:
            raise GitError(f"{CHANGE_BASE_VARIABLE}={named} names no commit of {root}")
        return commit
    branch = run_any_exit(root, "symbolic-ref", "-q", "--short", "HEAD")
    if branch.exit == 0 and branch.out.strip() != MAIN_BRANCH and resolve(root, f"refs/heads/{MAIN_BRANCH}"):
        return run(root, "merge-base", "HEAD", f"refs/heads/{MAIN_BRANCH}").decode().strip()
    if not is_clean(root) or new_files(root):
        return head(root)
    return resolve(root, "HEAD^1")


def changed_files(root: Path, base: str) -> tuple[str, ...]:
    """Return each path that the working tree changes against a commit, sorted (THE CHANGE).

    The paths are each changed, added and deleted file of the diff, and each new file (new_files()).  A moved file
    gives its path at the base and its path in the working tree.  Complexity: linear in the paths of the diff.

    Raises:
        GitError: If git fails
    """
    names = set(null_separated(run(root, "diff", "--name-only", "--no-renames", "-z", base, "--")))
    return tuple(sorted(names | set(new_files(root))))


def null_separated(output: bytes) -> tuple[str, ...]:
    """Return the paths of the output of a git command that gives each path with a NUL after it, in sequence."""
    return tuple(name for name in output.decode("utf-8", "surrogateescape").split("\0") if name)


def listed_paths(root: Path, options: Sequence[str]) -> tuple[str, ...]:
    """Return each path that `git ls-files -z` lists with the options, sorted, also a path with no file in the tree.

    Raises:
        GitError: If root is not in a git work tree
    """
    return tuple(sorted(set(null_separated(run(root, "ls-files", "-z", *options)))))


def hash_files(root: Path, paths: Sequence[str]) -> tuple[str, ...]:
    """Return the blob name of the content of each file of a tree, in the sequence of the paths.  Write no object.

    Raises:
        GitError: If git fails
    """
    if not paths:
        return ()
    output = run(root, "hash-object", "--stdin-paths",
                 stdin="".join(f"{path}\n" for path in paths).encode("utf-8", "surrogateescape"))
    return tuple(name.decode() for name in output.split())


def parent(root: Path, commit: str) -> str:
    """Return the full hash of the first parent of a commit.

    Raises:
        GitError: If the commit has no parent, or git fails
    """
    return run(root, "rev-parse", f"{commit}^").decode().strip()


def is_ancestor(root: Path, commit: str, revision: str) -> bool:
    """Tell if the history of a revision holds a commit, as `git merge-base --is-ancestor` does.

    The commit itself is in its own history.

    Raises:
        GitError: If git cannot start, or a name is not a commit
    """
    completed = run_any_exit(root, "merge-base", "--is-ancestor", commit, revision)
    if completed.exit in (0, 1):
        return completed.exit == 0
    raise GitError(f"`git merge-base --is-ancestor {commit} {revision}` failed in {root}: {completed.err.strip()}")


def git_dir(root: Path) -> Path:
    """Return the git directory of the work tree of a tree, as an absolute path.

    A linked work tree has a git directory of its own in the shared git directory.

    Raises:
        GitError: If root is not in a git work tree
    """
    return Path(run(root, "rev-parse", "--absolute-git-dir").decode().strip())


def short_name(root: Path, revision: str) -> str:
    """Return the short name of the commit of a revision, as `git rev-parse --short` gives it.

    Raises:
        GitError: If the revision names no object, or git fails
    """
    return run(root, "rev-parse", "--short", revision).decode().strip()


def commit_subject(root: Path, revision: str) -> str:
    """Return the subject of the commit of a revision.

    Raises:
        GitError: If the revision names no commit, or git fails
    """
    return run(root, "log", "-1", "--format=%s", revision).decode("utf-8", "replace").strip()


def status(root: Path) -> Status:
    """Return the commit of a tree and the number of index files that are not as in the commit.  Write nothing.

    The commit is None in a repository with no commit.

    Raises:
        GitError: If root is not in a git work tree
    """
    output = run(root, "status", "--porcelain=v2", "--branch", "--untracked-files=no")
    commit: str | None = None
    changed = 0
    for line in proc.decoded(output).splitlines():
        if line.startswith(BRANCH_OID):
            shown = line.removeprefix(BRANCH_OID).strip()
            commit = None if shown == INITIAL_COMMIT else shown
        elif not line.startswith("#"):
            changed += 1
    return Status(commit, changed)


def archive(root: Path, revision: str, paths: Sequence[str]) -> bytes:
    """Return the tar archive of the paths at a revision.

    Raises:
        GitError: If the revision or a path names no object, or git fails
    """
    return run(root, "archive", revision, *paths)


def write_tree(root: Path) -> str:
    """Write the tree of the index of a repository, and return its name.

    Raises:
        GitError: If git fails
    """
    return run(root, "write-tree").decode().strip()


def hash_object(root: Path, content: bytes) -> str:
    """Write a blob with the content into the object store of a repository, and return its name.

    Raises:
        GitError: If git fails
    """
    return run(root, "hash-object", "-w", "--stdin", stdin=content).decode().strip()


def commit_tree(root: Path, tree: str, parents: Sequence[str], message: str,
                extra: Mapping[str, str] | None = None) -> str:
    """Write a commit of a tree with the parents and the message, and return its name.  Move no branch.

    Raises:
        GitError: If git fails
    """
    options = [word for name in parents for word in ("-p", name)]
    return run(root, "commit-tree", tree, *options, stdin=message.encode(), extra=extra).decode().strip()


def tree_entry(root: Path, revision: str, path: str) -> TreeEntry | None:
    """Return the entry of a path in the tree of a revision, or None when the tree holds no such path.

    Raises:
        GitError: If the revision names no tree, or git fails
    """
    entry = run(root, "ls-tree", "-z", revision, "--", path).decode("utf-8", "surrogateescape").rstrip("\0")
    if not entry:
        return None
    meta, _ = entry.split("\t", 1)
    mode, kind, name = meta.split(" ")
    return TreeEntry(mode, kind, name)


def index_object(root: Path, path: str) -> str:
    """Return the name of the blob of a path in the index of a repository.

    Raises:
        GitError: If the index holds no such path, or git fails
    """
    return run(root, "rev-parse", f":{path}").decode().strip()


def set_index_entry(root: Path, entry: TreeEntry, path: str) -> None:
    """Give a path of the index of a repository the mode and the object of an entry.

    Raises:
        GitError: If git fails
    """
    run(root, "update-index", "--cacheinfo", f"{entry.mode},{entry.name},{path}")


def tree_changes(root: Path, old: str, new: str) -> tuple[str, ...]:
    """Return each path that is not the same in two trees, with no search for renames, in the sequence of git.

    Raises:
        GitError: If git fails
    """
    return null_separated(run(root, "diff-tree", "-r", "-z", "--name-only", "--no-renames", old, new))


def index_changes(root: Path, base: str) -> tuple[str, ...]:
    """Return each path of the index that is not as in a commit, with no search for renames, in the sequence of git.

    Raises:
        GitError: If git fails
    """
    return null_separated(run(root, "diff", "--cached", "-z", "--name-only", "--no-renames", base))


def work_changes(root: Path, base: str, paths: Sequence[str]) -> tuple[str, ...]:
    """Return each file of the working tree in the paths that is not as in a commit, with no search for renames.

    Raises:
        GitError: If git fails
    """
    return null_separated(run(root, "diff", "-z", "--name-only", "--no-renames", base, "--", *paths))


def index_patch(root: Path, base: str) -> str:
    """Return the patch from a commit to the index of a repository, as text to show.

    Raises:
        GitError: If git fails
    """
    return run(root, "diff", "--cached", base).decode("utf-8", "replace")


def patch(root: Path, revisions: Sequence[str], paths: Sequence[str]) -> bytes:
    """Return the patch of `git diff` with the revisions and the paths: one revision against the working tree, or two.

    Raises:
        GitError: If git fails
    """
    return run(root, "diff", *revisions, "--", *paths)


def is_index_clean(root: Path, base: str) -> bool:
    """Tell if the index of a repository holds the tree of a commit.

    Raises:
        GitError: If git cannot start, or it gives a status other than 0 and 1
    """
    return quiet_diff(root, ["--cached", base])


def has_no_change(root: Path) -> bool:
    """Tell if `git status --porcelain` of a tree gives no line: no change of an index file and no new file.

    Raises:
        GitError: If root is not in a git work tree
    """
    return run(root, "status", "--porcelain") == b""


def symbolic_head(root: Path) -> str | None:
    """Return the branch of HEAD of a tree, such as refs/heads/main, or None when HEAD is on no branch.

    Raises:
        GitError: If git cannot start
    """
    completed = run_any_exit(root, "symbolic-ref", "-q", "HEAD")
    return completed.out.strip() if completed.exit == 0 else None


def swap_ref(root: Path, ref: str, new: str, old: str, message: str) -> bool:
    """Move a ref to a new commit only when it holds the commit `old`, and tell if it moved.

    Raises:
        GitError: If git cannot start
    """
    return run_any_exit(root, "update-ref", "-m", message, ref, new, old).exit == 0


def set_index_entries(root: Path, entries: Sequence[tuple[TreeEntry | None, str]]) -> bool:
    """Give each path of the index the mode and the object of its entry, or remove a path with no entry.

    Tell if git gives the index the entries.  A busy index gives False.

    Raises:
        GitError: If git cannot start
    """
    lines = "".join(f"{entry.mode} {entry.name}\t{path}\n" if entry is not None else f"0 {NULL_OBJECT}\t{path}\n"
                    for entry, path in entries)
    return run_any_exit(root, "update-index", "--index-info", stdin=lines.encode()).exit == 0


def holds_patch(root: Path, patch_bytes: bytes) -> bool:
    """Tell if the working tree of a repository holds a patch: the patch applies in reverse.

    Raises:
        GitError: If git cannot start
    """
    return run_any_exit(root, "apply", "--reverse", "--check", stdin=patch_bytes).exit == 0


def apply_patch(root: Path, patch_bytes: bytes) -> str | None:
    """Give the working tree of a repository a patch, and return the error of git when a hunk does not apply, or None.

    Raises:
        GitError: If git cannot start
    """
    applied = run_any_exit(root, "apply", stdin=patch_bytes)
    return applied.err if applied.exit != 0 else None


def commit_all(root: Path, subject: str, body: str = "", date: str = "") -> str:
    """Commit each file of a throwaway repository with a fixed identity, and return the hash.

    A date that git reads, such as 2025-01-01T12:00:00, becomes the author
    date and the committer date of the commit.  An empty date gives the time
    of the commit.

    Raises:
        GitError: If git fails
    """
    run(root, "add", "-A", "--", ".")
    message = ["-m", subject] + (["-m", body] if body else [])
    stamp = {"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date} if date else {}
    run(root, "commit", "-q", "--allow-empty", *message, extra={**SCRATCH_IDENTITY, **stamp})
    return run(root, "rev-parse", "HEAD").decode().strip()


@contextlib.contextmanager
def borrowing_repository(root: Path) -> Iterator[Path]:
    """Make a scratch repository that writes into the object directory of the repository of a tree (THE BORROWED INDEX).

    The block gets the scratch repository, and this function removes it after the block.

    Raises:
        GitError: If git fails
    """
    shared = common_dir(root)
    with tempfile.TemporaryDirectory(prefix="assay-borrow-") as work:
        scratch = Path(work)
        init(scratch)
        run(scratch, "config", "gc.auto", "0")
        objects = scratch / ".git" / "objects"
        shutil.rmtree(objects)
        objects.symlink_to(shared / "objects", target_is_directory=True)
        exclude = shared / EXCLUDE_FILE
        if exclude.is_file():
            shutil.copyfile(exclude, scratch / ".git" / EXCLUDE_FILE)
        yield scratch


def replaced_commit(root: Path, commit: str, files: Mapping[str, bytes | None]) -> str:
    """Write a commit with the parents and the message of a commit, and its tree with some files replaced.

    Each path of `files` gets its bytes.  For None, the tree does not hold the path.  A replaced file keeps its mode,
    and a new file gets the mode of a plain file.  The objects go into the repository of root (THE BORROWED INDEX).
    Complexity: two git commands for each file, and a constant number of other git commands.

    Raises:
        GitError: If git fails, for example for a commit that the repository does not hold
    """
    parents = run(root, "rev-list", "--parents", "-n", "1", commit).decode().split()[1:]
    entries: list[tuple[TreeEntry | None, str]] = []
    for path, data in sorted(files.items()):
        if data is None:
            entries.append((None, path))
            continue
        kept = tree_entry(root, commit, path)
        mode = kept.mode if kept is not None else PLAIN_FILE_MODE
        entries.append((TreeEntry(mode, "blob", hash_object(root, data)), path))
    with borrowing_repository(root) as scratch:
        run(scratch, "read-tree", commit)
        if not set_index_entries(scratch, entries):
            raise GitError(f"git cannot give the index of a scratch repository the files of the commit {commit}")
        tree = write_tree(scratch)
    return commit_tree(root, tree, parents, message_of(root, commit))


def message_of(root: Path, commit: str) -> str:
    """Return the message of a commit as the commit holds it, with no line that git adds.

    Raises:
        GitError: If git fails, for example for a commit that the repository does not hold
    """
    # The raw object holds the header lines, one empty line, and then the message.
    return run(root, "cat-file", "commit", commit).partition(b"\n\n")[2].decode("utf-8")


@contextlib.contextmanager
def scratch_repository(files: Mapping[str, bytes] | None = None) -> Iterator[Path]:
    """Make a throwaway repository with the files, and remove it after the body of the with statement.

    The repository has no commit.  `git add` puts each file in the index, so
    tracked_files() lists it.
    """
    with tempfile.TemporaryDirectory(prefix="assay-scratch-") as scratch:
        root = Path(scratch)
        init(root)
        for name, data in (files or {}).items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if files:
            run(root, "add", "-A", "--", ".")
        yield root
