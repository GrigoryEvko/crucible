"""The test of the git module (assay.core.git): the command, the answers and the isolation of a repository.

`guard selftest tests.core.test_git` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.  Each case runs in a throwaway
repository.
"""

import os
import sys
from collections.abc import Callable
from pathlib import Path

from assay.core import git


def refuses(action: Callable[[], object], error: type[Exception]) -> Exception | None:
    """Return the error that an action raises, or None when it raises no such error."""
    try:
        action()
    except error as problem:
        return problem
    return None


def command_cases(expect: Callable[[str, bool], None]) -> None:
    """Check a command: its output, its failure, a command that cannot start, and the result for each exit."""
    with git.scratch_repository({"a.txt": b"one\n", "sub/b.txt": b"two\n"}) as root:
        expect("tracked_files gives each file of the index, sorted", git.tracked_files(root) == ("a.txt", "sub/b.txt"))
        expect("tracked_files takes a pathspec", git.tracked_files(root, ("sub",)) == ("sub/b.txt",))
        expect("run gives the standard output as bytes", git.run(root, "ls-files", "a.txt") == b"a.txt\n")
        problem = refuses(lambda: git.run(root, "no-such-command-of-git"), git.GitError)
        expect(
            "run raises GitError with the error text of git on a failure",
            problem is not None and "no-such-command-of-git" in str(problem),
        )
        problem = refuses(lambda: git.run(root, "status", extra={"PATH": "/no/such/directory"}), git.GitError)
        expect("run raises GitError when git cannot start", problem is not None and "cannot run" in str(problem))
        completed = git.run_any_exit(root, "diff", "--quiet", "--no-index", "a.txt", "sub/b.txt")
        expect("run_any_exit gives the result for an exit that is not 0", completed.exit == 1)


def answer_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the answers: head, resolve, is_clean and show."""
    with git.scratch_repository({"a.txt": b"one\n", "sub/b.txt": b"two\n"}) as root:
        commit = git.commit_all(root, "The first commit")
        expect("head gives the full hash of HEAD", git.head(root) == commit and len(commit) == 40)
        expect("resolve gives the commit of a revision", git.resolve(root, "HEAD") == commit)
        expect("resolve gives None for a revision that names no commit", git.resolve(root, "no-such-revision") is None)
        expect("is_clean is true for a tree with no change", git.is_clean(root))
        (root / "sub" / "b.txt").write_text("changed\n")
        expect("is_clean is false for a changed tracked file", not git.is_clean(root))
        expect("is_clean reads only the pathspec", git.is_clean(root, ("a.txt",)) and not git.is_clean(root, ("sub",)))
        expect("show gives a file at a revision", git.show(root, "HEAD", "sub/b.txt") == b"two\n")
        expect("show gives None for a file that the revision does not hold", git.show(root, "HEAD", "none.txt") is None)
        dated = git.commit_all(root, "A dated commit", date="2025-01-02T12:00:00")
        dates = git.run(root, "log", "-1", "--format=%ad %cd", "--date=short").decode().split()
        expect("commit_all gives a date to the author and to the committer", dates == ["2025-01-02", "2025-01-02"])
        expect(
            "changed_paths gives each path of a commit against its parent, and each path of a root commit",
            git.changed_paths(root, dated) == {"sub/b.txt"}
            and git.changed_paths(root, commit) == {"a.txt", "sub/b.txt"},
        )


def numbered_lines(count: int, changed: frozenset[int]) -> bytes:
    """Return COUNT lines of the same length, with other text on each line of CHANGED."""
    return "".join(
        f"{'changed line' if index in changed else 'the first text'} {index:02d} of the moved file\n"
        for index in range(count)
    ).encode()


def move_cases(expect: Callable[[str, bool], None]) -> None:
    """Check THE MOVES: a rename with most lines kept is a move, and a rewrite that keeps half of the lines is not."""
    with git.scratch_repository({"old.cpp": numbered_lines(20, frozenset())}) as root:
        git.commit_all(root, "The first commit")
        (root / "old.cpp").unlink()
        (root / "rewritten.cpp").write_bytes(numbered_lines(20, frozenset(range(8))))
        git.run(root, "add", "-A", "--", ".")
        plain = git.run(root, "diff", "--find-renames", "--name-status", "HEAD", "--").decode()
        expect(
            "the plant is a rewrite that the default of git (50 %) reports as a move",
            plain.startswith("R") and "rewritten.cpp" in plain,
        )
        expect("moves gives no move for a rewrite below MOVE_SIMILARITY", git.moves(root) == {})
        rewrite = git.commit_all(root, "Rewrite the file under a new name")
        expect(
            "changes gives the rule history no move for that rewrite",
            git.changes(root, ["rewritten.cpp"])[rewrite][1] == {},
        )
        (root / "rewritten.cpp").unlink()
        (root / "renamed.cpp").write_bytes(numbered_lines(20, frozenset(range(9))))
        git.run(root, "add", "-A", "--", ".")
        expect(
            "moves gives the old path of a rename that keeps 19 of 20 lines",
            git.moves(root) == {"renamed.cpp": "rewritten.cpp"},
        )


def isolation_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that a repository variable of this process does not reach a git command."""
    with git.scratch_repository({"own.txt": b"own\n"}) as own, git.scratch_repository({"other.txt": b"x\n"}) as other:
        saved = {name: os.environ.get(name) for name in ("GIT_DIR", "GIT_INDEX_FILE")}
        os.environ["GIT_DIR"] = str(other / ".git")
        os.environ["GIT_INDEX_FILE"] = str(other / ".git" / "index")
        try:
            resolved = git.run(own, "rev-parse", "--absolute-git-dir").decode().strip()
            listed = git.tracked_files(own)
            commit = git.commit_all(own, "A commit with GIT_DIR of another repository exported")
            other_head = git.resolve(other, "HEAD")
        finally:
            for name, value in saved.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value
        expect(
            "a git command resolves the repository of its root, also with GIT_DIR exported",
            Path(resolved) == (own / ".git").resolve(),
        )
        expect("tracked_files lists the root, also with GIT_INDEX_FILE exported", listed == ("own.txt",))
        expect(
            "a commit lands in the root and not in the repository of GIT_DIR",
            git.head(own) == commit and other_head is None,
        )


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_git", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    command_cases(expect)
    answer_cases(expect)
    move_cases(expect)
    isolation_cases(expect)
    if failures:
        print(f"tests.core.test_git: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_git: every case holds.")
    return 0
