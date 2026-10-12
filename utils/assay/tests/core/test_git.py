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


def object_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the typed answers and the objects of a commit that a scratch repository writes (THE OBJECTS)."""
    with git.scratch_repository({"a.txt": b"one\n", "sub/b.txt": b"two\n"}) as root:
        first = git.commit_all(root, "The first commit", date="2025-01-02T12:00:00")
        (root / "a.txt").write_text("one and more\n")
        second = git.commit_all(root, "The second commit", date="2025-01-03T12:00:00")
        expect("parent gives the first parent of a commit", git.parent(root, second) == first)
        expect("short_name gives a prefix of the full hash", second.startswith(git.short_name(root, second)))
        expect("author_date gives the author date", git.author_date(root, first).isoformat() == "2025-01-02")
        expect("commit_subject gives the subject", git.commit_subject(root, second) == "The second commit")
        expect(
            "dated_commits gives each commit of a path with its date, oldest first",
            [(entry.sha, entry.date.isoformat()) for entry in git.dated_commits(root, ["a.txt"])]
            == [(first, "2025-01-02"), (second, "2025-01-03")],
        )
        entry = git.tree_entry(root, second, "sub/b.txt")
        expect(
            "tree_entry gives the mode, the type and the object of a path, and None for a missing path",
            entry is not None
            and (entry.mode, entry.kind) == ("100644", "blob")
            and git.blobs(root, [entry.name]) == [b"two\n"]
            and git.tree_entry(root, second, "none.txt") is None,
        )
        expect("status gives the commit and no changed file", git.status(root) == git.Status(second, 0))
        (root / "sub" / "b.txt").write_text("changed\n")
        (root / "new.txt").write_text("new\n")
        expect("status counts a changed tracked file and no new file", git.status(root) == git.Status(second, 1))
        expect(
            "listed_paths gives each listed path, sorted",
            git.listed_paths(root, ["--modified", "--others", "--exclude-standard"]) == ("new.txt", "sub/b.txt"),
        )
        blob = git.hash_object(root, b"three\n")
        expect(
            "hash_files gives the blob name of the content of each file, as hash_object writes it",
            git.hash_files(root, ["sub/b.txt"]) == (git.hash_object(root, b"changed\n"),)
            and git.blobs(root, [blob]) == [b"three\n"],
        )
        git.set_index_entry(root, git.TreeEntry("100644", "blob", blob), "sub/b.txt")
        expect("index_object gives the blob of a path of the index", git.index_object(root, "sub/b.txt") == blob)
        expect(
            "index_changes gives each path of the index that differs", git.index_changes(root, second) == ("sub/b.txt",)
        )
        expect("index_patch gives the patch of the index", "+three" in git.index_patch(root, second))
        expect("is_index_clean is false for a changed index", not git.is_index_clean(root, second))
        tree = git.write_tree(root)
        third = git.commit_tree(root, tree, [second], "A composed commit\n", extra=git.SCRATCH_IDENTITY)
        expect(
            "commit_tree writes a commit of a tree on its parent, and moves no branch",
            git.parent(root, third) == second
            and git.head(root) == second
            and git.show(root, third, "sub/b.txt") == b"three\n",
        )
        expect(
            "tree_changes gives each path that differs between two trees",
            git.tree_changes(root, f"{second}^{{tree}}", tree) == ("sub/b.txt",),
        )
        branch = git.symbolic_head(root)
        expect(
            "swap_ref moves a branch only from the old commit",
            branch is not None
            and not git.swap_ref(root, branch, third, first, "a stale swap")
            and git.swap_ref(root, branch, third, second, "a swap")
            and git.head(root) == third,
        )
        expect(
            "set_index_entries gives the index the entries of a commit",
            git.set_index_entries(root, [(git.tree_entry(root, third, "sub/b.txt"), "sub/b.txt")])
            and git.is_index_clean(root, third),
        )
        patch = git.patch(root, [second, third], ["sub/b.txt"])
        (root / "sub" / "b.txt").write_text("two\n")
        expect(
            "apply_patch gives the working tree a patch, and holds_patch then sees it",
            not git.holds_patch(root, patch)
            and git.apply_patch(root, patch) is None
            and git.holds_patch(root, patch)
            and (root / "sub" / "b.txt").read_text() == "three\n",
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


def change_base_cases(expect: Callable[[str, bool], None]) -> None:
    """Check THE CHANGE: the base of a change in each case that change_base() reads."""
    with git.scratch_repository({"a.cpp": numbered_lines(4, frozenset())}) as root:
        first = git.commit_all(root, "The first commit")
        git.run(root, "branch", "-M", git.MAIN_BRANCH)
        expect("a root commit with no change has no base", git.change_base(root) is None)
        (root / "a.cpp").write_bytes(numbered_lines(4, frozenset({1})))
        expect("a changed tracked file gives HEAD as the base", git.change_base(root) == first)
        second = git.commit_all(root, "The second commit")
        expect("a clean main gives the first parent of HEAD as the base", git.change_base(root) == first)
        (root / "new.cpp").write_bytes(b"int value = 0;\n")
        expect("a new file that git does not track gives HEAD as the base", git.change_base(root) == second)
        expect("new_files lists the new file", git.new_files(root) == ("new.cpp",))
        (root / "new.cpp").unlink()
        git.run(root, "branch", "feature")
        git.run(root, "symbolic-ref", "HEAD", "refs/heads/feature")
        (root / "a.cpp").write_bytes(numbered_lines(4, frozenset({1, 2})))
        branched = git.commit_all(root, "A commit of the branch")
        expect("a branch other than main gives the merge base with main", git.change_base(root) == second)
        previous = os.environ.get(git.CHANGE_BASE_VARIABLE)
        os.environ[git.CHANGE_BASE_VARIABLE] = first
        try:
            named = git.change_base(root)
            os.environ[git.CHANGE_BASE_VARIABLE] = "no-such-revision"
            problem = refuses(lambda: git.change_base(root), git.GitError)
        finally:
            if previous is None:
                os.environ.pop(git.CHANGE_BASE_VARIABLE, None)
            else:
                os.environ[git.CHANGE_BASE_VARIABLE] = previous
        expect("the environment variable names the base", named == first)
        expect("a variable that names no commit raises GitError", problem is not None)
        expect("the commit of the branch is not the base", branched != git.change_base(root))


def file_change_cases(expect: Callable[[str, bool], None]) -> None:
    """Check file_changes(): the hunks of a changed file, a move with an edit, a new file and the head of a hunk."""
    with git.scratch_repository(
        {"kept.cpp": numbered_lines(10, frozenset()), "moved.cpp": numbered_lines(20, frozenset())}
    ) as root:
        base = git.commit_all(root, "The first commit")
        (root / "kept.cpp").write_bytes(numbered_lines(10, frozenset({2})) + b"one more line\n")
        (root / "moved.cpp").unlink()
        (root / "renamed.cpp").write_bytes(numbered_lines(20, frozenset({5})))
        (root / "fresh.cpp").write_bytes(b"int first = 1;\nint second = 2;\n")
        git.run(root, "add", "-A", "--", ".")
        changes = git.file_changes(root, base)
        kept = changes.get("kept.cpp")
        expect(
            "a changed file has one hunk for each block, with the removed and the added lines",
            kept is not None
            and kept.old_path == "kept.cpp"
            and kept.hunks == (git.Hunk(3, 1, 3, 1), git.Hunk(10, 0, 11, 1)),
        )
        renamed = changes.get("renamed.cpp")
        expect(
            "a move with an edit gives its previous path and the hunk of the edit",
            renamed is not None and renamed.old_path == "moved.cpp" and renamed.hunks == (git.Hunk(6, 1, 6, 1),),
        )
        fresh = changes.get("fresh.cpp")
        expect(
            "a new file has no previous path and one hunk that adds each line",
            fresh is not None and fresh.old_path is None and fresh.hunks == (git.Hunk(0, 0, 1, 2),),
        )
        expect("a removed file has no entry", "moved.cpp" not in changes)
        (root / "untracked.cpp").write_bytes(b"int third = 3;\n")
        expect(
            "changed_files gives a changed, a removed, a moved, an added and a new file",
            git.changed_files(root, base) == ("fresh.cpp", "kept.cpp", "moved.cpp", "renamed.cpp", "untracked.cpp"),
        )
        expect(
            "the head of a hunk reads the counts, and a count that the diff omits is 1",
            git.hunk_of("@@ -3 +4,0 @@ int x;") == git.Hunk(3, 1, 4, 0)
            and git.hunk_of("@@@ -1 +1 @@") is None
            and git.hunk_of("+++ b/a.cpp") is None,
        )
        expect(
            "Hunk.adds holds each added line",
            git.Hunk(3, 1, 3, 2).adds(4) and not git.Hunk(3, 1, 3, 2).adds(5) and not git.Hunk(10, 0, 10, 0).adds(10),
        )


def replaced_cases(expect: Callable[[str, bool], None]) -> None:
    """Check replaced_commit() and message_of(): the parents, the message, and each replaced, new or removed file."""
    with git.scratch_repository({"a.txt": b"one\n", "tool.sh": b"echo\n", "gone.txt": b"x\n"}) as root:
        (root / "tool.sh").chmod(0o755)
        first = git.commit_all(root, "The first commit")
        (root / "a.txt").write_text("two\n")
        second = git.commit_all(root, "The second commit", "With a body.")
        replaced = git.replaced_commit(
            root, second, {"a.txt": b"three\n", "tool.sh": b"echo 2\n", "new/b.txt": b"new\n", "gone.txt": None}
        )
        entries = [git.tree_entry(root, replaced, path) for path in ("tool.sh", "new/b.txt")]
        modes = [entry.mode if entry is not None else "" for entry in entries]
        expect(
            "replaced_commit keeps the parents and the message, and replaces, adds and removes the files",
            replaced != second
            and git.parent(root, replaced) == first
            and git.message_of(root, replaced) == git.message_of(root, second) == "The second commit\n\nWith a body.\n"
            and git.show(root, replaced, "a.txt") == b"three\n"
            and git.show(root, replaced, "new/b.txt") == b"new\n"
            and git.show(root, replaced, "gone.txt") is None,
        )
        expect(
            "a replaced file keeps its mode, and a new file gets the mode of a plain file",
            modes == ["100755", git.PLAIN_FILE_MODE],
        )
        expect("replaced_commit does not move HEAD", git.head(root) == second)
        (root / "a.txt").write_text("four\n")
        (root / "untracked.txt").write_text("new\n")
        expect(
            "work_changes gives each changed file of the working tree under the paths, and no untracked file",
            git.work_changes(root, "HEAD", ["a.txt", "tool.sh", "untracked.txt"]) == ("a.txt",),
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
    object_cases(expect)
    move_cases(expect)
    change_base_cases(expect)
    file_change_cases(expect)
    replaced_cases(expect)
    isolation_cases(expect)
    if failures:
        print(f"tests.core.test_git: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_git: every case holds.")
    return 0
