#!/usr/bin/env python3
"""check-git-isolation — a script that writes to a git repository names it and clears the caller's repository.

A repository variable in the caller's environment wins over `git -C DIR`.
With GIT_DIR exported, `git init DIR` initializes the repository that
GIT_DIR names again, and a later add and commit land there.  On 2026-09-24
the self-test of one script sent its fixture commits into the real
repository this way and emptied its main branch.

THE RULE
    A script under scripts/ or tools/ that runs git init, commit,
    update-ref, reset or checkout must clear every repository variable
    before it, and must stop unless git resolves the repository it means.
      Python   The script calls throwaway_repo.init(), which clears the
               variables in scripts/throwaway_repo.py, makes the
               repository, and stops unless `rev-parse --absolute-git-dir`
               names it.  The verb counts when it is a string constant of
               the module and the module builds a git argv (a list or tuple
               literal whose first element is "git"), so a verb passed
               through a wrapper counts too.
      Shell    The script names each repository variable in an `unset`
               and checks `--absolute-git-dir`.  The verb counts on a line
               outside a comment where git, or an expansion of an argv
               array, comes before it.
    scripts/throwaway_repo.py is the implementation and is not read.

Exit 0 clean, 1 on a violation, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import ast
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ROOTS = ("scripts", "tools")
IMPLEMENTATION = "scripts/throwaway_repo.py"
MUTATING = frozenset({"init", "commit", "update-ref", "reset", "checkout"})
# git, or the expansion of an argv array, then options, then the verb.
SHELL_MUTATION = re.compile(r"""(?:(?<![\w./-])git|\[@\]\}"?)(?:\s+(?:-C\s+\S+|-c\s+\S+|--?[\w-]+(?:=\S+)?|"[^"]*"))*"""
                            r"""\s+(init|commit|update-ref|reset|checkout)\b""")
UNSET = re.compile(r"\bunset\s+((?:[A-Z_][A-Z0-9_]*\s*(?:\\\n\s*)?)+)")


def python_violation(text: str) -> str | None:
    """Return why a Python script writes to a repository it has not isolated, or None.

    Complexity: linear in the size of the module's syntax tree.
    """
    try:
        tree = ast.parse(text)
    except SyntaxError as exc:
        return f"the script does not parse, so its git calls are unknown: {exc.msg}"
    def is_git_argv(node: ast.AST) -> bool:
        return (isinstance(node, (ast.List, ast.Tuple)) and bool(node.elts)
                and isinstance(node.elts[0], ast.Constant) and node.elts[0].value == "git")

    def verbs_in(nodes: list[ast.expr]) -> set[str]:
        return {node.value for node in nodes if isinstance(node, ast.Constant) and node.value in MUTATING}

    argvs = [node for node in ast.walk(tree) if is_git_argv(node)]
    verbs = set().union(*(verbs_in(argv.elts) for argv in argvs))
    # A wrapper builds a git argv from its own arguments, so a verb passed
    # to it counts at the call.
    wrappers = {func.name for func in ast.walk(tree) if isinstance(func, (ast.FunctionDef, ast.AsyncFunctionDef))
                and any(is_git_argv(node) and any(isinstance(elt, ast.Starred) for elt in node.elts)
                        for node in ast.walk(func))}
    verbs |= set().union(*(verbs_in(call.args) for call in ast.walk(tree) if isinstance(call, ast.Call)
                           and isinstance(call.func, ast.Name) and call.func.id in wrappers))
    if not verbs:
        return None
    isolated = any(isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "init"
                   and isinstance(node.func.value, ast.Name) and node.func.value.id == "throwaway_repo"
                   for node in ast.walk(tree))
    if isolated:
        return None
    return (f"it runs git {', '.join(sorted(verbs))} and never calls throwaway_repo.init(), so an exported GIT_DIR "
            f"sends the write to the caller's repository")


def shell_violation(text: str) -> str | None:
    """Return why a shell script writes to a repository it has not isolated, or None.

    Complexity: linear in the length of the script.
    """
    code = "\n".join("" if line.lstrip().startswith("#") else line for line in text.splitlines())
    verbs = {match.group(1) for match in SHELL_MUTATION.finditer(code)}
    if not verbs:
        return None
    cleared = {name for match in UNSET.finditer(code) for name in re.findall(r"[A-Z_][A-Z0-9_]*", match.group(1))}
    missing = [name for name in throwaway_repo.REPOSITORY_VARIABLES if name not in cleared]
    reasons = []
    if missing:
        reasons.append(f"it does not unset {', '.join(missing)}")
    if "--absolute-git-dir" not in code:
        reasons.append("it never checks --absolute-git-dir")
    if not reasons:
        return None
    return f"it runs git {', '.join(sorted(verbs))}, and " + ", and ".join(reasons)


def scan(root: Path) -> list[str]:
    """Return one report line for each script under the roots that writes to an unisolated repository."""
    report = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            rel = path.relative_to(root).as_posix()
            if not path.is_file() or rel == IMPLEMENTATION or path.suffix not in (".py", ".sh"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            why = python_violation(text) if path.suffix == ".py" else shell_violation(text)
            if why is not None:
                report.append(f"GIT-ISOLATION violation: {rel} — {why}.")
    return report


def check(root: Path) -> int:
    """Scan one tree and print the report."""
    report = scan(root)
    for line in report:
        print(line, file=sys.stderr)
    if report:
        print("check-git-isolation: a Python script calls throwaway_repo.init() on its fixture; a shell script "
              "unsets every name in throwaway_repo.REPOSITORY_VARIABLES and checks --absolute-git-dir.",
              file=sys.stderr)
        return 1
    print("check-git-isolation: clean — every script that writes to a repository isolates it.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant scripts that isolate their repository and scripts that do not, and check each verdict."""
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    unset_all = "unset " + " \\\n      ".join(throwaway_repo.REPOSITORY_VARIABLES) + "\n"
    planted = {
        "scripts/py_bare_init.py": 'import subprocess\nsubprocess.run(["git", "-C", "tmp", "init", "-q"])\n',
        "scripts/py_wrapped_commit.py": ('import subprocess\ndef sh(*a):\n    subprocess.run(["git", "-C", "t", *a])\n'
                                         'sh("commit", "-m", "x")\n'),
        "scripts/py_isolated.py": ('import subprocess, throwaway_repo\nthrowaway_repo.init(root)\n'
                                   'subprocess.run(["git", "-C", "t", "commit", "-q"])\n'),
        "scripts/py_reads_only.py": 'import subprocess\nsubprocess.run(["git", "log", "-1"])\nx = "commit"\n',
        "scripts/sh_bare.sh": 'git init -q "$tmp"\ngit -C "$tmp" commit -q -m x\n',
        "scripts/sh_partial.sh": 'unset GIT_DIR GIT_WORK_TREE\ngit -C "$tmp" commit -q -m x\n',
        "scripts/sh_no_resolve_check.sh": unset_all + 'git -C "$tmp" commit -q -m x\n',
        "scripts/sh_isolated.sh": (unset_all + 'g=(git --git-dir="$tmp/.git" --work-tree="$tmp")\n'
                                   '[[ "$("${g[@]}" rev-parse --absolute-git-dir)" == "$tmp/.git" ]] || exit 2\n'
                                   '"${g[@]}" commit -q -m x\n'),
        "scripts/sh_array_bare.sh": 'g=(git -C "$tmp")\n"${g[@]}" update-ref refs/heads/x HEAD\n',
        "scripts/sh_comment.sh": '# git commit -m example\ngit log -1\n',
        "tools/tool_reset.sh": 'git reset --hard HEAD\n',
        "scripts/throwaway_repo.py": 'import subprocess\nsubprocess.run(["git", "init"])\n',
        "include/not_a_root.sh": 'git commit -m x\n',
    }
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in planted.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        report = "\n".join(scan(root))
        for rel, label in (("scripts/py_bare_init.py", "a Python init with no isolation"),
                           ("scripts/py_wrapped_commit.py", "a Python commit through a wrapper"),
                           ("scripts/sh_bare.sh", "a shell commit with no unset"),
                           ("scripts/sh_partial.sh", "a shell commit that unsets two variables"),
                           ("scripts/sh_no_resolve_check.sh", "a shell commit that never checks the repository"),
                           ("scripts/sh_array_bare.sh", "an update-ref through an argv array"),
                           ("tools/tool_reset.sh", "a reset under tools/")):
            expect(f"refused: {label}", f"violation: {rel} " in report, True)
        for rel, label in (("scripts/py_isolated.py", "a Python commit after throwaway_repo.init()"),
                           ("scripts/py_reads_only.py", "a Python script whose git calls only read"),
                           ("scripts/sh_isolated.sh", "a shell commit after the unset and the check"),
                           ("scripts/sh_comment.sh", "a commit inside a comment"),
                           ("scripts/throwaway_repo.py", "the implementation itself"),
                           ("include/not_a_root.sh", "a script outside scripts/ and tools/")):
            expect(f"admitted: {label}", f"violation: {rel} " not in report)
        expect("the partial unset names the missing variables",
               "does not unset GIT_INDEX_FILE" in report and "GIT_CEILING_DIRECTORIES" in report)
        expect("exactly seven violations", report.count("GIT-ISOLATION violation:") == 7)
        expect("a planted tree fails the check", check(root) == 1, True)
    if failures:
        print(f"check-git-isolation --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-git-isolation --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check on this repository, or the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-git-isolation.py [--self-test]", file=sys.stderr)
        return 2
    return self_test() if argv else check(REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
