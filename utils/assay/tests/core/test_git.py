"""The test of the git module (assay.core.git): the command, the answers and the isolation of a repository.

`guard selftest tests.core.test_git` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.  Each case runs in a throwaway
repository.
"""

import inspect
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
        number = git.version(root)
        expect(
            "version gives the number of the version of git, with no words before it",
            number[:1].isdigit() and "." in number and " " not in number,
        )
        problem = refuses(lambda: git.version(root.parent / "no-such-directory"), git.GitError)
        expect("version raises GitError when git cannot run in the tree", problem is not None)


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
            "is_ancestor tells if the history of a revision holds a commit, and the commit holds itself",
            git.is_ancestor(root, second, "HEAD")
            and git.is_ancestor(root, third, third)
            and not git.is_ancestor(root, third, second),
        )
        expect(
            "git_dir gives the git directory of the work tree as an absolute path",
            git.git_dir(root) == (root / ".git").resolve(),
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
        expect(
            "the moves of the change give no moved file for a rewrite below MOVE_SIMILARITY",
            git.diff_moves(root, "HEAD").renamed == {},
        )
        rewrite = git.commit_all(root, "Rewrite the file under a new name")
        expect(
            "changes gives the rule history no move for that rewrite",
            git.changes(root, ["rewritten.cpp"])[rewrite][1] == {},
        )
        (root / "rewritten.cpp").unlink()
        (root / "renamed.cpp").write_bytes(numbered_lines(20, frozenset(range(9))))
        git.run(root, "add", "-A", "--", ".")
        expect(
            "the moves of the change give the old path of a rename that keeps 19 of 20 lines",
            git.diff_moves(root, "HEAD").renamed == {"renamed.cpp": "rewritten.cpp"},
        )
        git.run(root, "branch", "-M", git.MAIN_BRANCH)
        expect(
            "change_moves reads the change against change_base(), here HEAD of a tree with an edit",
            git.change_moves(root).renamed == {"renamed.cpp": "rewritten.cpp"},
        )
        git.commit_all(root, "Rename the file")
        expect(
            "change_moves of a clean main reads the last commit, which the moves against HEAD do not see",
            git.change_moves(root).renamed == {"renamed.cpp": "rewritten.cpp"}
            and git.diff_moves(root, "HEAD").renamed == {},
        )
        moves = git.change_moves(root)
        expect(
            "the moves give the commit of the base, and base_file reads a file there, or None with no file or no base",
            moves.base == rewrite
            and git.base_file(root, moves, "rewritten.cpp") is not None
            and git.base_file(root, moves, "renamed.cpp") is None
            and git.base_file(root, git.Moves(), "rewritten.cpp") is None,
        )


def moved_line_cases(expect: Callable[[str, bool], None]) -> None:
    """Check THE MOVED LINES: a division into two new files, a changed text, an empty line and the multiset of texts."""
    header = b"#pragma once\n\nint first_value = 1;\n  int second_value = 2;\nint third_value = 3;\n\nint kept = 4;\n"
    with git.scratch_repository({"split.h": header, "other.h": b"int third_value = 3;\n"}) as root:
        base = git.commit_all(root, "The first commit")
        (root / "split.h").write_bytes(b'#pragma once\n\n#include "part_a.h"\n#include "part_b.h"\n\nint kept = 4;\n')
        (root / "part_a.h").write_bytes(b"#pragma once\n\nint first_value = 1;\nint second_value = 2;\n")
        (root / "part_b.h").write_bytes(
            b"#pragma once\n\nint third_value = 3;\nint third_value = 3;\nint fourth = 5;\n"
        )
        (root / "other.h").write_bytes(b"")
        moves = git.moves_of(git.file_diffs(root, base, with_new=True))
        expect(
            "a line that moves to a new file with the same text, also with other white space, is a moved line",
            moves.is_moved("part_a.h", 3) and moves.is_moved("part_a.h", 4),
        )
        expect(
            "each removed copy pays for one added copy: two removed copies of a text pay for two added copies",
            moves.is_moved("part_b.h", 3) and moves.is_moved("part_b.h", 4),
        )
        expect("a line with a text that no removed line has is not moved", not moves.is_moved("part_b.h", 5))
        expect(
            "a line whose text the first file keeps is not moved, such as the pragma of a new part",
            not moves.is_moved("part_a.h", 1) and not moves.is_moved("part_b.h", 1),
        )
        expect("a blank line is no moved line", not moves.is_moved("part_a.h", 2))
        expect(
            "the flows give each pair of files where the change moved a line",
            moves.flows == {("split.h", "part_a.h"), ("split.h", "part_b.h"), ("other.h", "part_b.h")},
        )
        expect("a new file is not a moved file", moves.renamed == {})
        (root / "part_b.h").write_bytes(b"#pragma once\n\nint third_value = 30;\n")
        changed = git.moves_of(git.file_diffs(root, base, with_new=True))
        expect(
            "a line whose text the change also changed is not moved, and gives no flow",
            not changed.is_moved("part_b.h", 3) and ("split.h", "part_b.h") not in changed.flows,
        )


def diff_reader_cases(expect: Callable[[str, bool], None]) -> None:
    """Check read_diff(): the counts of a hunk give its lines, and a removed line that starts with '-- ' is no head."""
    output = (
        b"diff --git a/a.sql b/a.sql\n"
        b"index 1111111..2222222 100644\n"
        b"--- a/a.sql\n"
        b"+++ b/a.sql\n"
        b"@@ -2,2 +2 @@ select\n"
        b"--- a comment of the old text\n"
        b"-+++ b/not-a-file\n"
        b"+-- a new comment\n"
        b"\\ No newline at end of file\n"
        b"diff --git a/gone.h b/gone.h\n"
        b"deleted file mode 100644\n"
        b"--- a/gone.h\n"
        b"+++ /dev/null\n"
        b"@@ -1 +0,0 @@\n"
        b"-int gone = 0;\n"
        b"diff --git a/old.h b/new.h\n"
        b"similarity index 100%\n"
        b"rename from old.h\n"
        b"rename to new.h\n"
        b"diff --git a/tool.sh b/tool.sh\n"
        b"old mode 100644\n"
        b"new mode 100755\n"
    )
    diffs = git.read_diff(output)
    expect(
        "a removed line that starts with '-- ' or '++ ' is a line of its hunk, not the head of a file",
        len(diffs) == 3
        and diffs[0]
        == git.FileDiff(
            "a.sql",
            "a.sql",
            (git.Hunk(2, 2, 2, 1),),
            ("-- a comment of the old text", "+++ b/not-a-file"),
            ((2, "-- a new comment"),),
        ),
    )
    expect(
        "a removed file has no new path and gives its removed lines",
        diffs[1] == git.FileDiff("gone.h", None, (git.Hunk(1, 1, 0, 0),), ("int gone = 0;",), ()),
    )
    expect(
        "a move with no change of a line has its two paths and no hunk", diffs[2] == git.FileDiff("old.h", "new.h", ())
    )
    expect("a change of the mode only gives no file", all(item.new_path != "tool.sh" for item in diffs))


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
    """Check file_diffs(): the hunks of a changed file, a move with an edit, a new file and the head of a hunk."""
    with git.scratch_repository(
        {"kept.cpp": numbered_lines(10, frozenset()), "moved.cpp": numbered_lines(20, frozenset())}
    ) as root:
        base = git.commit_all(root, "The first commit")
        (root / "kept.cpp").write_bytes(numbered_lines(10, frozenset({2})) + b"one more line\n")
        (root / "moved.cpp").unlink()
        (root / "renamed.cpp").write_bytes(numbered_lines(20, frozenset({5})))
        (root / "fresh.cpp").write_bytes(b"int first = 1;\nint second = 2;\n")
        git.run(root, "add", "-A", "--", ".")
        changes = {item.new_path: item for item in git.file_diffs(root, base) if item.new_path is not None}
        kept = changes.get("kept.cpp")
        expect(
            "a changed file has one hunk for each block, with the removed and the added lines",
            kept is not None
            and kept.old_path == "kept.cpp"
            and kept.hunks == (git.Hunk(3, 1, 3, 1), git.Hunk(10, 0, 11, 1))
            and kept.added == ((3, "changed line 02 of the moved file"), (11, "one more line"))
            and kept.removed == ("the first text 02 of the moved file",),
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
        expect("a removed file has no new path", "moved.cpp" not in changes)
        (root / "untracked.cpp").write_bytes(b"int third = 3;\nint fourth = 4;\n")
        with_new = {item.new_path: item for item in git.file_diffs(root, base, with_new=True)}
        untracked = with_new.get("untracked.cpp")
        expect(
            "with_new gives a new file that is not in the index, with each of its lines as an added line",
            untracked is not None
            and untracked.old_path is None
            and untracked.hunks == (git.Hunk(0, 0, 1, 2),)
            and untracked.added == ((1, "int third = 3;"), (2, "int fourth = 4;"))
            and "untracked.cpp" not in changes,
        )
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


def kind_cases(expect: Callable[[str, bool], None]) -> None:
    """Check that each function of the module has a kind, and that a function of no reads has no second kind."""
    functions = {
        name for name, value in vars(git).items() if inspect.isfunction(value) and value.__module__ == git.__name__
    }
    kinds = git.READS_HISTORY | git.READS_NAMES | git.READS_NEITHER
    expect(
        f"each function of the module has a kind, and these have none: {sorted(functions - kinds)}", functions <= kinds
    )
    expect(
        f"each name of a kind is a function of the module, and these are not: {sorted(kinds - functions)}",
        kinds <= functions,
    )
    expect(
        "a function of READS_NEITHER has no second kind",
        not (git.READS_NEITHER & (git.READS_HISTORY | git.READS_NAMES)),
    )
    expect(
        "a function that calls new_files() reads the names of the files",
        {"change_base", "changed_files"} <= git.READS_NAMES,
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
    moved_line_cases(expect)
    diff_reader_cases(expect)
    change_base_cases(expect)
    file_change_cases(expect)
    replaced_cases(expect)
    isolation_cases(expect)
    kind_cases(expect)
    if failures:
        print(f"tests.core.test_git: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_git: every case holds.")
    return 0
