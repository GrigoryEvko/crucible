"""The git commands of the package: the files, the history and the moves of a tree, and a throwaway repository.

THE ISOLATION
    A repository variable in the environment of the caller wins over
    `git -C DIR`: with GIT_DIR set, `git -C tmp init` initializes the
    repository that GIT_DIR names again, and a later add and commit land
    there.  On 2026-09-24 an exported GIT_DIR sent the fixture commits of one
    self-test into the real repository and emptied its main branch.

    isolate() removes each such variable from the environment of this
    process.  run() gives each git command an environment with no such
    variable, so a read of this module resolves its repository from its own
    path also when the process keeps the variables.  init() makes a
    repository and stops unless git resolves the directory itself.

THE HISTORY
    path_history() reads the commits that changed some paths, with one `git
    log` and one `git cat-file --batch`: the content of each path at each such
    commit, at its first parent, at HEAD and in the working tree.  A merge is
    not one of the commits, because a merge brings a change that a commit of
    the other side made.
"""

import contextlib
import os
import subprocess
import tempfile
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

# Each variable that tells git which repository, index or object store to
# use, whatever directory it runs in.
REPOSITORY_VARIABLES = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_NAMESPACE",
    "GIT_CEILING_DIRECTORIES",
)
# The separators of the fields and the records of `git log`, which no subject or body holds.
FIELD = "\x1f"
RECORD = "\x1e"
# The identity of a commit of a throwaway repository.
SCRATCH_IDENTITY = {"GIT_AUTHOR_NAME": "crucible-scratch", "GIT_AUTHOR_EMAIL": "crucible-scratch@invalid",
                    "GIT_COMMITTER_NAME": "crucible-scratch", "GIT_COMMITTER_EMAIL": "crucible-scratch@invalid"}
# The time limit of one git command, in seconds.
TIMEOUT_S = 300.0


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


def isolate() -> None:
    """Remove every repository variable from the environment of this process."""
    for name in REPOSITORY_VARIABLES:
        os.environ.pop(name, None)


def clean_environment(extra: Mapping[str, str] | None = None) -> dict[str, str]:
    """Return the environment of this process with no repository variable, and with the extra variables."""
    environment = {name: value for name, value in os.environ.items() if name not in REPOSITORY_VARIABLES}
    environment.update(extra or {})
    return environment


def run(root: Path, *arguments: str, stdin: bytes | None = None, extra: Mapping[str, str] | None = None) -> bytes:
    """Run one git command in a tree, and return its standard output.

    Raises:
        GitError: If git cannot run, or it exits with a status that is not 0
    """
    try:
        completed = subprocess.run(["git", "-C", str(root), *arguments], input=stdin, capture_output=True,
                                   env=clean_environment(extra), timeout=TIMEOUT_S, check=False)
    except (OSError, subprocess.TimeoutExpired) as problem:
        raise GitError(f"git {' '.join(arguments[:2])} cannot run in {root}: {problem}") from None
    if completed.returncode != 0:
        raise GitError(f"`git {' '.join(arguments[:2])}` failed in {root}: "
                       f"{completed.stderr.decode(errors='replace').strip()}")
    return completed.stdout


def init(root: Path) -> None:
    """Make a repository in root, and refuse to go on unless git resolves root itself.

    Raises:
        NotIsolated: If git resolves a repository outside root
    """
    isolate()
    subprocess.run(["git", "-C", str(root), "init", "-q"], check=True, capture_output=True)
    resolved = subprocess.run(["git", "-C", str(root), "rev-parse", "--absolute-git-dir"], check=True,
                              capture_output=True, text=True).stdout.strip()
    if Path(resolved).resolve() != (root / ".git").resolve():
        raise NotIsolated(f"the fixture repository {root} resolves to {resolved}; the self-test stops before it "
                          f"writes to a repository that is not its own")


def toplevel(root: Path) -> Path:
    """Return the top of the work tree that holds root.

    Raises:
        GitError: If root is not in a git work tree
    """
    return Path(run(root, "rev-parse", "--show-toplevel").decode().strip())


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
    log = run(root, "log", "--no-merges", "--full-history", "--topo-order", "--full-diff", "--find-renames",
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
    """Return the old path of each file that git reports as moved between `base` and the working tree.

    A tree that is not a git work tree, or a failed git command, gives no move.
    """
    try:
        output = run(root, "diff", "--find-renames", "--name-status", "-z", base, "--")
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


def commit_all(root: Path, subject: str, body: str = "") -> str:
    """Commit each file of a throwaway repository with a fixed identity, and return the hash.

    Raises:
        GitError: If git fails
    """
    run(root, "add", "-A", "--", ".")
    message = ["-m", subject] + (["-m", body] if body else [])
    run(root, "commit", "-q", "--allow-empty", *message, extra=SCRATCH_IDENTITY)
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
