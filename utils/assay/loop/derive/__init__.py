"""loop derive — write each generated file of the tree and compare each one, or write, compare or print one.

usage: loop derive              write each generated file of REFRESH, then run each check of CHECKS
       loop derive --check      run each check of CHECKS, and write nothing
       loop derive TARGET [ARG]...

THE REFRESH
    A commit that moves, renames or deletes a path can leave a generated file
    that names it, and two kinds of drift need a check:
      * A path goes dead.  A key of an allowlist or a sentence names a file
        that is no longer there.  The check of the allowlist keys finds it.
      * The content goes stale while each path still resolves.  The mint
        inventory can stop counting a mint, and only a new write shows it.
    `loop derive` writes each generated file of REFRESH, and then it runs
    each check of CHECKS.  Run it before `git commit`.  It names each file
    that the writes changed, and those files belong in the same commit.  A
    change of the tree from before the run is not named.  A check that is
    still red after the writes reads a list that a person keeps, so it needs
    an edit and not a second run.

THE TARGETS
    TARGETS gives the module of each target.  The command calls the function
    main of that module with the words of the target and each ARG.  For a
    target with a write and a check, --write gives the words of its write
    and --check the words of its check, and each other ARG goes unchanged.

    mint-inventory    misc/mint-inventory.md and the HS14 floor (--write, --check, --check-floor)
    witness-fixtures  the negative fixtures of the witness roster (--write, --check)
    walk-units        test/layer/walk-checks.txt (--write, --check)
    mint-model        print the mint model of include/, one line for each mint
    aten-ops          vessel/torch/aten_op_table.h, from the PyTorch tree of TORCH_DIR
    session-oracle    test/session_oracle/golden.csv and the tests that it gives: the modes regenerate, derive and
                      emit of utils/assay/oracle/runner.py

Exit 0 when each step passed, 1 when a check is still red, 2 on a usage error, or the value that main of a target
returns.
"""

from __future__ import annotations

import subprocess
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import git
from assay.core.paths import GUARD, LOOP, REPO_ROOT


@dataclass(frozen=True, slots=True)
class Target:
    """One target: its module, the words before the words of the call, and the words of its write and its check."""

    module: str
    words: tuple[str, ...] = ()
    write: tuple[str, ...] | None = None
    check: tuple[str, ...] | None = None

    def translate(self, argv: Sequence[str]) -> list[str]:
        """Return the words for main of the module: --write and --check give their words, and each other word stays."""
        translated = list(self.words)
        for word in argv:
            if word == "--write" and self.write is not None:
                translated.extend(self.write)
            elif word == "--check" and self.check is not None:
                translated.extend(self.check)
            else:
                translated.append(word)
        return translated


TARGETS = {
    "mint-inventory": Target("assay.loop.derive.mint_inventory", (), ("--write",), ("--check",)),
    "witness-fixtures": Target("assay.guard.check", ("--rule", "witness-roster"), ("--gen",), ("--check",)),
    "walk-units": Target("assay.guard.check", ("--rule", "walk-units"), ("--write",), ()),
    "mint-model": Target("assay.loop.derive.mint_model"),
    "aten-ops": Target("assay.loop.derive.aten_ops"),
    "session-oracle": Target("assay.oracle.runner"),
}
# The targets that the refresh writes, in this order.
REFRESH = ("mint-inventory", "witness-fixtures", "walk-units")
# The checks of the refresh, in the order that a moved path breaks them: a label and a command.
CHECKS = (
    ("allowlist keys and prose", ("bash", str(REPO_ROOT / "utils" / "scripts" / "check-allowlist-keys.sh"))),
    ("mint inventory", (sys.executable, str(LOOP), "derive", "mint-inventory", "--check")),
    ("witness roster", (sys.executable, str(GUARD), "check", "--rule", "witness-roster", "--check")),
    ("walk units", (sys.executable, str(GUARD), "check", "--rule", "walk-units")),
)
LABEL_WIDTH = 34


def tree_state(root: Path) -> set[str]:
    """Return one line for each path that differs from HEAD: the hash of its content and the path, or - and the path.

    A path with no regular file, or a link, gives - and the path.  The writes
    run between two calls, and a path whose line changed between them is a
    path that the writes made, changed or deleted.  Complexity: linear in the
    number and the size of the changed files.

    Args:
        root: The tree

    Returns:
        The lines
    """
    listed = git.run(root, "ls-files", "-z", "--modified", "--deleted", "--others", "--exclude-standard")
    paths = sorted({name.decode("utf-8", "surrogateescape") for name in listed.split(b"\0") if name})
    files = [path for path in paths if (root / path).is_file() and not (root / path).is_symlink()]
    lines = {f"- {path}" for path in paths if path not in set(files)}
    if files:
        hashes = git.run(root, "hash-object", "--stdin-paths",
                         stdin="".join(f"{path}\n" for path in files).encode("utf-8", "surrogateescape")).split()
        lines |= {f"{digest.decode()} {path}" for digest, path in zip(hashes, files)}
    return lines


def changed_paths(before: set[str], after: set[str]) -> list[str]:
    """Return each path whose line is in one state and not in the other, sorted."""
    return sorted({line.split(" ", 1)[1] for line in before ^ after})


def run_step(label: str, command: Sequence[str], root: Path) -> bool:
    """Run one step with its output captured, print its label and its verdict, and its output when it failed.

    Args:
        label: The name of the step in the report
        command: The command
        root: The working directory

    Returns:
        Whether the step passed
    """
    print(f"  {label:<{LABEL_WIDTH}} ", end="", file=sys.stderr, flush=True)
    completed = subprocess.run(list(command), cwd=root, capture_output=True, text=True, errors="replace",
                               check=False)
    if completed.returncode == 0:
        print("ok", file=sys.stderr)
        return True
    print(f"FAILED (exit {completed.returncode})", file=sys.stderr)
    for line in (completed.stdout + completed.stderr).splitlines():
        print(f"      {line}", file=sys.stderr)
    return False


def refresh(root: Path, should_write: bool) -> int:
    """Write each generated file of REFRESH when should_write, then run each check, as THE REFRESH tells.

    Args:
        root: The tree
        should_write: Whether to write the generated files first

    Returns:
        0 when each step passed, 1 when a step is red
    """
    failures = 0
    before: set[str] = set()
    if should_write:
        print("loop derive: writing", file=sys.stderr)
        before = tree_state(root)
        for name in REFRESH:
            failures += not run_step(name, (sys.executable, str(LOOP), "derive", name, "--write"), root)
    print("loop derive: checking", file=sys.stderr)
    for label, command in CHECKS:
        failures += not run_step(label, command, root)
    if should_write:
        rewritten = changed_paths(before, tree_state(root))
        if rewritten:
            print("\nloop derive: the writes changed these files, and they belong in the same commit:",
                  file=sys.stderr)
            for path in rewritten:
                print(f"  {path}", file=sys.stderr)
    if failures:
        print(f"\nloop derive: {failures} step(s) still red after the writes.", file=sys.stderr)
        print("A red that stays after a write reads a list that a person keeps, so it needs an edit and not a second "
              "run.", file=sys.stderr)
        return 1
    print("\nloop derive: clean.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Check the words of each target and the changed paths of two states of a planted repository.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    expect("--write and --check give the words of the target, and each other word stays",
           TARGETS["witness-fixtures"].translate(["--write"]) == ["--rule", "witness-roster", "--gen"]
           and TARGETS["walk-units"].translate(["--check"]) == ["--rule", "walk-units"]
           and TARGETS["mint-inventory"].translate(["--check-floor"]) == ["--check-floor"]
           and TARGETS["session-oracle"].translate(["emit"]) == ["emit"])
    expect("each target of the refresh has a write and a check",
           all(TARGETS[name].write is not None and TARGETS[name].check is not None for name in REFRESH))
    with git.scratch_repository({"kept.txt": b"one\n", "changed.txt": b"one\n", "deleted.txt": b"one\n"}) as root:
        (root / "before.txt").write_text("an edit before the run\n", encoding="utf-8")
        before = tree_state(root)
        (root / "changed.txt").write_text("two\n", encoding="utf-8")
        (root / "deleted.txt").unlink()
        (root / "made.txt").write_text("new\n", encoding="utf-8")
        expect("the changed paths are the files that the writes made, changed or deleted, and not an earlier edit",
               changed_paths(before, tree_state(root)) == ["changed.txt", "deleted.txt", "made.txt"])

    if failures:
        print(f"loop derive --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop derive --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Refresh, check, run the self-test, or run the module of one target.

    Args:
        argv: The words after the subcommand: nothing, --check, --self-test, or TARGET and the words for its module

    Returns:
        The exit status, or the value that main of the module returns
    """
    if not argv:
        return refresh(REPO_ROOT, should_write=True)
    if argv == ["--check"]:
        return refresh(REPO_ROOT, should_write=False)
    if argv == ["--self-test"]:
        return self_test()
    target = TARGETS.get(argv[0])
    if target is None:
        print(f"usage: loop derive [--check | TARGET [ARG]...]  (TARGET is one of: {', '.join(TARGETS)})",
              file=sys.stderr)
        return 2
    __import__(target.module)
    return sys.modules[target.module].main(target.translate(argv[1:]))
