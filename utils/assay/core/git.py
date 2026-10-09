"""The one module of the package that runs git: the files, the history and the moves of a tree, and a throwaway repository.

THE COMMAND
    run() runs one git command in a tree, through assay.core.proc, with a
    time limit of TIMEOUT_S, and it returns the standard output.  A command
    that cannot start, that runs past its limit, or that exits with a status
    that is not 0 raises GitError.  run_any_exit() gives the result for each
    exit, for a command whose exit is its answer.  head(), resolve(),
    is_clean() and show() give the answers that callers ask for most, so a
    caller reads no output of git itself.

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

THE HISTORY
    path_history() reads the commits that changed some paths, with one `git
    log` and one `git cat-file --batch`: the content of each path at each such
    commit, at its first parent, at HEAD and in the working tree.  A merge is
    not one of the commits, because a merge brings a change that a commit of
    the other side made.

THE MOVES
    A move is a file that git finds as renamed with a similarity of
    MOVE_SIMILARITY percent or more.  moves() gives the ledger writers the
    previous path of each moved file, and the file takes the rows of that
    path.  changes() gives the rule history the same moves.  The default of
    git is 50 %.  With that default, a file that keeps half of its lines takes
    each row of its previous path.  Its new half can then hold new findings of
    the same entities.  A rewrite below the threshold is a removed file and a
    new file.
"""

import contextlib
import tempfile
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import proc

# The separators of the fields and the records of `git log`, which no subject or body holds.
FIELD = "\x1f"
RECORD = "\x1e"
# The identity of a commit of a throwaway repository.
SCRATCH_IDENTITY = {"GIT_AUTHOR_NAME": "crucible-scratch", "GIT_AUTHOR_EMAIL": "crucible-scratch@invalid",
                    "GIT_COMMITTER_NAME": "crucible-scratch", "GIT_COMMITTER_EMAIL": "crucible-scratch@invalid"}
# The time limit of one git command, in seconds.
TIMEOUT_S = 300.0
# The lowest similarity of a move, in percent (THE MOVES), and the option of git that finds such moves.
MOVE_SIMILARITY = 90
FIND_MOVES = f"--find-renames={MOVE_SIMILARITY}%"


class NotIsolated(Exception):
    """git resolves a repository other than the fixture directory."""


class GitError(Exception):
    """A git command cannot run, or it exits with a status that is not 0."""


@dataclass(frozen=True, slots=True)
class Commit:
    """One commit that changed a watched path: its hash, its subject, its body and the watched paths it changed."""

    sha: str
    subject: str
    body: str
    paths: tuple[str, ...]

    @property
    def short(self) -> str:
        """The hash in 9 characters."""
        return self.sha[:9]


@dataclass(frozen=True, slots=True)
class PathHistory:
    """The states of some paths: at each commit that changed one of them (newest first), at its parent, at HEAD and in
    the working tree.  A missing file is None."""

    commits: tuple[Commit, ...]
    at: Mapping[tuple[str, str], bytes | None] = field(default_factory=dict)
    before: Mapping[tuple[str, str], bytes | None] = field(default_factory=dict)
    head: Mapping[str, bytes | None] = field(default_factory=dict)
    working: Mapping[str, bytes | None] = field(default_factory=dict)


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
    completed = run_any_exit(root, "diff", "--quiet", "HEAD", "--", *pathspec)
    if completed.exit in (0, 1):
        return completed.exit == 0
    raise GitError(f"`git diff --quiet HEAD` failed in {root}: {completed.err.strip()}")


def show(root: Path, revision: str, path: str) -> bytes | None:
    """Return the content of a file at a revision, or None when the revision has no such file.

    Raises:
        GitError: If git fails
    """
    return blobs(root, [f"{revision}:{path}"])[0]


def is_shallow(root: Path) -> bool:
    """Whether the history of the repository of root is shallow.

    Raises:
        GitError: If root is not in a git work tree
    """
    return run(root, "rev-parse", "--is-shallow-repository").decode().strip() == "true"


def tracked_files(root: Path, pathspec: Sequence[str] = (), with_new: bool = False) -> tuple[str, ...]:
    """Return each tracked file of a tree, sorted.  With with_new, also each new file that .gitignore does not match.

    Raises:
        GitError: If root is not in a git work tree
    """
    arguments = ["ls-files", "-z", "--cached"] + (["--others", "--exclude-standard"] if with_new else [])
    listed = run(root, *arguments, "--", *pathspec).decode("utf-8", "surrogateescape").split("\0")
    return tuple(sorted({path for path in listed if path and (root / path).is_file()}))


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


def commits(root: Path, paths: Sequence[str]) -> tuple[Commit, ...]:
    """Return each commit that is not a merge and that changed one of the paths, newest first.

    Raises:
        GitError: If git fails
    """
    log = run(root, "log", "--no-merges", "--full-history", "--topo-order", f"--format={RECORD}%H{FIELD}%s{FIELD}%b{FIELD}",
              "--name-only", "--", *paths).decode("utf-8", "replace")
    watched = set(paths)
    found: list[Commit] = []
    for record in log.split(RECORD)[1:]:
        sha, subject, body, names = record.split(FIELD, 3)
        changed = tuple(name for name in names.split("\n") if name in watched)
        found.append(Commit(sha, subject.strip(), body.strip(), changed))
    return tuple(found)


def changes(root: Path, paths: Sequence[str]) -> dict[str, tuple[frozenset[str], dict[str, str]]]:
    """Return each path that each commit of the paths changed, and the old path of each file that it moved.

    The commits are those that changed one of `paths`, newest first, and the
    result holds each path of the commit (`--full-diff`).

    Raises:
        GitError: If git fails

    Complexity: linear in the size of the log.
    """
    log = run(root, "log", "--no-merges", "--full-history", "--topo-order", "--full-diff", FIND_MOVES,
              "--name-status", f"--format={RECORD}%H", "--", *paths).decode("utf-8", "replace")
    found: dict[str, tuple[frozenset[str], dict[str, str]]] = {}
    for record in log.split(RECORD)[1:]:
        lines = record.split("\n")
        changed: set[str] = set()
        moved: dict[str, str] = {}
        for line in lines[1:]:
            cells = line.split("\t")
            if len(cells) == 3 and cells[0].startswith("R"):
                moved[cells[2]] = cells[1]
                changed.update(cells[1:])
            elif len(cells) >= 2:
                changed.update(cells[1:])
        found[lines[0].strip()] = (frozenset(changed), moved)
    return found


def changed_paths(root: Path, revision: str) -> frozenset[str]:
    """Return each path that one commit changed against its first parent, or each of its paths for a root commit.

    Raises:
        GitError: If git fails

    Complexity: linear in the number of the paths of the commit.
    """
    names = run(root, "diff-tree", "--no-commit-id", "--name-only", "-r", "--root", revision).decode("utf-8", "replace")
    return frozenset(name for name in names.split("\n") if name)


def path_history(root: Path, paths: Sequence[str], groups: Sequence[Sequence[str]] = ()) -> PathHistory:
    """Read each commit that changed one of the paths, and each path at each such commit, its parent, HEAD and the
    working tree.

    A commit that changed one path of a group also reads each other path of
    the group, so the state of the group is whole at the commit and at its
    parent.  A group holds paths of `paths`.

    Raises:
        GitError: If git fails
    """
    found = commits(root, paths)
    requests: list[tuple[str, str]] = []
    for commit in found:
        wanted = list(commit.paths)
        for group in groups:
            if any(path in commit.paths for path in group):
                wanted += [path for path in group if path not in wanted]
        requests += [(commit.sha, path) for path in wanted]
    names = ([f"{sha}:{path}" for sha, path in requests] + [f"{sha}^1:{path}" for sha, path in requests]
             + [f"HEAD:{path}" for path in paths])
    contents = blobs(root, names)
    count = len(requests)
    working: dict[str, bytes | None] = {}
    for path in paths:
        file = root / path
        working[path] = file.read_bytes() if file.is_file() else None
    return PathHistory(found, {request: contents[index] for index, request in enumerate(requests)},
                       {request: contents[count + index] for index, request in enumerate(requests)},
                       {path: contents[2 * count + index] for index, path in enumerate(paths)}, working)


def moves(root: Path, base: str = "HEAD") -> dict[str, str]:
    """Return the old path of each file that git reports as moved between `base` and the working tree (THE MOVES).

    A tree that is not a git work tree, or a failed git command, gives no move.
    """
    try:
        output = run(root, "diff", FIND_MOVES, "--name-status", "-z", base, "--")
    except GitError:
        return {}
    found: dict[str, str] = {}
    words = output.decode("utf-8", "surrogateescape").split("\0")
    index = 0
    while index < len(words):
        status = words[index]
        if status[:1] in ("R", "C") and index + 2 < len(words):
            if status[0] == "R":
                found[words[index + 2]] = words[index + 1]
            index += 3
        else:
            index += 2 if status else 1
    return found


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
