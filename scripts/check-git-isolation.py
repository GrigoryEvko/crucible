#!/usr/bin/env python3
"""check-git-isolation — a script that writes to a git repository names it and clears the caller's repository.

A repository variable in the caller's environment wins over `git -C DIR`.
With GIT_DIR exported, `git init DIR` initializes the repository that
GIT_DIR names again, and a later add and commit land there.  On 2026-09-24
the self-test of one script sent its fixture commits into the real
repository this way and emptied its main branch.

THE RULE
    A script under scripts/, tools/ or toolchain/ that runs git init, commit,
    update-ref, reset, checkout, fetch or apply must clear every repository
    variable before it, and must stop unless git resolves the repository it
    means.
      Python   The script calls throwaway_repo.init(), which clears the
               variables in scripts/throwaway_repo.py, makes the
               repository, and stops unless `rev-parse --absolute-git-dir`
               names it.  The verb counts when it is a string constant of
               the module and the module builds a git argv (a list or tuple
               literal whose first element is "git"), so a verb passed
               through a wrapper counts too.  The Python `ast` module reads
               the script.
      Shell    The script names each repository variable in an `unset`
               command and passes `--absolute-git-dir` to a command.  The
               verb is the first argument of a git command that is not an
               option or the value of one.  A git command is a command whose
               name is git, or the expansion of every element of an array,
               which is how a script keeps a git argv.  ast-grep's bash
               grammar reads the script (the pinned binary from
               scripts/install-ast-grep.sh), so the text of a heredoc body or
               a comment is not a command.
    scripts/throwaway_repo.py is the implementation and is not read.

Exit 0 clean, 1 on a violation, 2 on a usage error or a failed self-test,
3 when the pinned ast-grep is not installed.
"""

from __future__ import annotations

import ast
import json
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ROOTS = ("scripts", "tools", "toolchain")
IMPLEMENTATION = "scripts/throwaway_repo.py"
MUTATING = frozenset({"init", "commit", "update-ref", "reset", "checkout", "fetch", "apply"})
# A global git option that takes the next argument as its value when it is
# not written with `=`.  The verb comes after the options and their values.
OPTIONS_WITH_VALUE = frozenset({"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path",
                                "--config-env", "--super-prefix"})
# The three ast-grep rules that read a shell script.  A git command is a
# command whose name is git (or a path ending in /git), or whose name
# expands every element of an array.  ast-grep's JSON gives each argument
# of the command in order, and the verb is picked from them below.
AST_GREP_RULES = """\
id: git-command
language: bash
rule:
  all:
    - pattern: $CMD $$$ARGS
    - any:
        - has: {field: name, regex: '^(.*/)?git$'}
        - has: {field: name, has: {stopBy: end, kind: subscript, has: {field: index, regex: '^[@*]$'}}}
---
id: unset
language: bash
rule:
  pattern: unset $$$NAMES
---
id: resolve-check
language: bash
rule:
  kind: word
  regex: '^--absolute-git-dir$'
  inside:
    kind: command
"""


class AstGrepMissing(RuntimeError):
    """The pinned ast-grep binary is not installed."""


@dataclass
class ShellFacts:
    """What the parse of one shell script says about its git writes."""

    verbs: set[str] = field(default_factory=set)
    cleared: set[str] = field(default_factory=set)
    checks_git_dir: bool = False


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


def git_verb(arguments: list[str]) -> str | None:
    """Return the verb of a git command from its arguments, or None when it has no verb.

    Each argument is the text of one argument node of the command.  An
    option starts with `-`.  A quoted or expanded argument is never a verb.
    """
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
            continue
        if argument in OPTIONS_WITH_VALUE:
            skip_next = True
            continue
        if argument.startswith(("-", '"', "'", "$")):
            continue
        return argument
    return None


def ast_grep_binary() -> str:
    """Return the path of the pinned ast-grep binary.

    Raises:
        AstGrepMissing: If scripts/install-ast-grep.sh reports that it is absent
        RuntimeError: If the installer fails in another way
    """
    result = subprocess.run(["bash", str(REPO_ROOT / "scripts" / "install-ast-grep.sh"), "--print-bin"],
                            capture_output=True, text=True)
    if result.returncode == 3:
        raise AstGrepMissing("the pinned ast-grep is not installed; run: bash scripts/install-ast-grep.sh")
    if result.returncode != 0:
        raise RuntimeError(f"scripts/install-ast-grep.sh --print-bin failed:\n{result.stderr.strip()}")
    return result.stdout.strip()


def shell_facts(paths: list[Path]) -> dict[Path, ShellFacts]:
    """Parse each shell script with ast-grep and collect its git verbs, its unsets and its repository check.

    One ast-grep run reads every script.  Complexity: linear in the total
    size of the scripts.

    Raises:
        AstGrepMissing: If the pinned ast-grep is not installed
        RuntimeError: If ast-grep fails or gives output that is not JSON
    """
    facts = {path: ShellFacts() for path in paths}
    if not paths:
        return facts
    result = subprocess.run([ast_grep_binary(), "scan", "--inline-rules", AST_GREP_RULES, "--json=compact",
                             *(str(path) for path in paths)], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ast-grep failed (exit {result.returncode}):\n{result.stderr.strip()}")
    try:
        matches = json.loads(result.stdout or "[]")
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"ast-grep printed output that is not JSON: {exc}") from exc
    by_path = {str(path): path for path in paths}
    for match in matches:
        path = by_path.get(match["file"])
        if path is None:
            raise RuntimeError(f"ast-grep reported {match['file']}, which is not a scanned script")
        multi = match["metaVariables"]["multi"]
        if match["ruleId"] == "git-command":
            verb = git_verb([node["text"] for node in multi.get("ARGS", [])])
            if verb in MUTATING:
                facts[path].verbs.add(verb)
        elif match["ruleId"] == "unset":
            facts[path].cleared |= {node["text"] for node in multi.get("NAMES", [])
                                    if not node["text"].startswith("-")}
        elif match["ruleId"] == "resolve-check":
            facts[path].checks_git_dir = True
    return facts


def shell_violation(facts: ShellFacts) -> str | None:
    """Return why a shell script writes to a repository it has not isolated, or None."""
    if not facts.verbs:
        return None
    missing = [name for name in throwaway_repo.REPOSITORY_VARIABLES if name not in facts.cleared]
    reasons = []
    if missing:
        reasons.append(f"it does not unset {', '.join(missing)}")
    if not facts.checks_git_dir:
        reasons.append("it never checks --absolute-git-dir")
    if not reasons:
        return None
    return f"it runs git {', '.join(sorted(facts.verbs))}, and " + ", and ".join(reasons)


def scan(root: Path) -> list[str]:
    """Return one report line for each script under the roots that writes to an unisolated repository.

    Raises:
        AstGrepMissing: If there is a shell script to read and the pinned ast-grep is not installed
    """
    scripts: list[tuple[str, Path]] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            rel = path.relative_to(root).as_posix()
            if path.is_file() and rel != IMPLEMENTATION and path.suffix in (".py", ".sh"):
                scripts.append((rel, path))
    shells = shell_facts([path for _, path in scripts if path.suffix == ".sh"])
    report = []
    for rel, path in scripts:
        if path.suffix == ".py":
            why = python_violation(path.read_text(encoding="utf-8", errors="replace"))
        else:
            why = shell_violation(shells[path])
        if why is not None:
            report.append(f"GIT-ISOLATION violation: {rel} — {why}.")
    return report


def check(root: Path) -> int:
    """Scan one tree and print the report."""
    try:
        report = scan(root)
    except AstGrepMissing as exc:
        print(f"check-git-isolation: {exc}", file=sys.stderr)
        return 3
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

    def expect(name: str, holds: bool, negative: bool = False) -> None:
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
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
        # A heredoc body is data.  The text scan read its line as a command.
        "scripts/sh_heredoc.sh": 'cat > note.txt <<EOF\ngit commit -m fixture\nEOF\ngit log -1\n',
        # A check named only in a comment checks nothing.  The text scan
        # accepted the word anywhere in the file.
        "scripts/sh_check_in_comment.sh": unset_all + 'git -C "$tmp" commit -q -m x  # --absolute-git-dir\n',
        # An option that takes a value hides nothing: the verb is still found.
        "scripts/sh_option_value.sh": 'git -c user.name=x --git-dir "$tmp/.git" reset --hard\n',
        # A verb name as the argument of a read is not a write.
        "scripts/sh_verb_as_argument.sh": 'git log --grep init\n',
        "scripts/sh_apply.sh": 'git -C "$src" apply p.patch\n',
        "tools/tool_reset.sh": 'git reset --hard HEAD\n',
        "toolchain/tc_fetch.sh": 'git init -q "$src"\ngit -C "$src" fetch -q --depth 1 "$url" "$commit"\n',
        "scripts/throwaway_repo.py": 'import subprocess\nsubprocess.run(["git", "init"])\n',
        "include/not_a_root.sh": 'git commit -m x\n',
    }
    try:
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            for rel, text in planted.items():
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(text, encoding="utf-8")
            report = "\n".join(scan(root))
            refused = (("scripts/py_bare_init.py", "a Python init with no isolation"),
                       ("scripts/py_wrapped_commit.py", "a Python commit through a wrapper"),
                       ("scripts/sh_bare.sh", "a shell commit with no unset"),
                       ("scripts/sh_partial.sh", "a shell commit that unsets two variables"),
                       ("scripts/sh_no_resolve_check.sh", "a shell commit that never checks the repository"),
                       ("scripts/sh_array_bare.sh", "an update-ref through an argv array"),
                       ("scripts/sh_check_in_comment.sh", "a shell commit whose check is only a comment"),
                       ("scripts/sh_option_value.sh", "a reset after options that take values"),
                       ("scripts/sh_apply.sh", "an apply with no isolation"),
                       ("tools/tool_reset.sh", "a reset under tools/"),
                       ("toolchain/tc_fetch.sh", "an init and a fetch under toolchain/"))
            for rel, label in refused:
                expect(f"refused: {label}", f"violation: {rel} " in report, True)
            for rel, label in (("scripts/py_isolated.py", "a Python commit after throwaway_repo.init()"),
                               ("scripts/py_reads_only.py", "a Python script whose git calls only read"),
                               ("scripts/sh_isolated.sh", "a shell commit after the unset and the check"),
                               ("scripts/sh_comment.sh", "a commit inside a comment"),
                               ("scripts/sh_heredoc.sh", "a commit inside a heredoc body"),
                               ("scripts/sh_verb_as_argument.sh", "a verb name as the argument of a read"),
                               ("scripts/throwaway_repo.py", "the implementation itself"),
                               ("include/not_a_root.sh", "a script outside scripts/, tools/ and toolchain/")):
                expect(f"admitted: {label}", f"violation: {rel} " not in report)
            expect("the partial unset names the missing variables",
                   "does not unset GIT_INDEX_FILE" in report and "GIT_CEILING_DIRECTORIES" in report)
            expect("the comment-only check is named as missing",
                   "scripts/sh_check_in_comment.sh — it runs git commit, and it never checks --absolute-git-dir"
                   in report)
            expect("the toolchain script names both verbs", "toolchain/tc_fetch.sh — it runs git fetch, init" in report)
            expect("exactly one violation per refused script",
                   report.count("GIT-ISOLATION violation:") == len(refused))
            expect("a planted tree fails the check", check(root) == 1, True)
    except AstGrepMissing as exc:
        print(f"check-git-isolation --self-test: {exc}", file=sys.stderr)
        return 3
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
