"""The reader of shell scripts: the commands, the pipelines and the marked words of each script.

THE ENGINE
    The pinned ast-grep reads each script with its bash grammar, in one run
    for each call, and it prints its matches as JSON.  So the text of a
    heredoc body or of a comment is not a command, and a command over two
    lines is one command.  utils/scripts/install-ast-grep.sh installs the
    binary, and `install-ast-grep.sh --print-bin` gives its path.

WHAT THE READER GIVES
    For each script:
      commands        each command: its name, the text of each argument, the
                      byte range and the line.  The name of the expansion of
                      each element of an array, such as "${g[@]}", is the text
                      of that expansion
      pipelines       the byte range of each pipeline
      lexer_words     the lines of each word, string or heredoc body that
                      names cxx_lex
      unset_names     each name of an `unset` command
      checks_git_dir  whether a command has the argument --absolute-git-dir

Complexity: linear in the size of the scripts.
"""

import json
import subprocess
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core.paths import REPO_ROOT

INSTALLER = "utils/scripts/install-ast-grep.sh"
MISSING_STATUS = 3
TIMEOUT_S = 300.0
# The rules of ast-grep.  A regex in a rule is data of ast-grep, in its own dialect.
RULES = """\
id: command
language: bash
rule:
  pattern: $CMD $$$ARGS
---
id: pipeline
language: bash
rule:
  kind: pipeline
---
id: lexer-word
language: bash
rule:
  any:
    - {kind: word, regex: 'cxx_lex'}
    - {kind: string, regex: 'cxx_lex'}
    - {kind: raw_string, regex: 'cxx_lex'}
    - {kind: heredoc_body, regex: 'cxx_lex'}
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
    """The pinned ast-grep is not installed."""


class ShellError(RuntimeError):
    """ast-grep fails, or it gives output that the reader cannot read."""


@dataclass(frozen=True, slots=True)
class ShellCommand:
    """One command of a script: its name, the text of each argument, its byte range and its line (from 1)."""

    name: str
    arguments: tuple[str, ...]
    start: int
    end: int
    line: int


@dataclass(frozen=True, slots=True)
class ShellScript:
    """What the parse of one script gives."""

    path: str
    commands: tuple[ShellCommand, ...] = ()
    pipelines: tuple[tuple[int, int], ...] = ()
    lexer_words: tuple[int, ...] = ()
    unset_names: frozenset[str] = field(default_factory=frozenset)
    checks_git_dir: bool = False


def binary(root: Path = REPO_ROOT) -> str:
    """Return the path of the pinned ast-grep binary.

    Raises:
        AstGrepMissing: If the installer reports that the binary is absent
        ShellError: If the installer fails in another way
    """
    try:
        completed = subprocess.run(["bash", str(root / INSTALLER), "--print-bin"], capture_output=True, text=True,
                                   timeout=TIMEOUT_S, check=False)
    except (OSError, subprocess.TimeoutExpired) as problem:
        raise ShellError(f"{INSTALLER} --print-bin cannot run: {problem}") from None
    if completed.returncode == MISSING_STATUS:
        raise AstGrepMissing(f"the pinned ast-grep is not installed.  Run: bash {INSTALLER}")
    if completed.returncode != 0:
        raise ShellError(f"{INSTALLER} --print-bin failed: {completed.stderr.strip()}")
    return completed.stdout.strip()


def read(root: Path, paths: Sequence[str], program: str | None = None) -> dict[str, ShellScript]:
    """Read each script, a path relative to the root, with one run of ast-grep.

    Raises:
        AstGrepMissing: If there is a script and the pinned ast-grep is absent
        ShellError: If ast-grep fails or gives output that is not its JSON
    """
    if not paths:
        return {}
    executable = program or binary(REPO_ROOT)
    try:
        completed = subprocess.run([executable, "scan", "--inline-rules", RULES, "--json=compact", *paths],
                                   capture_output=True, text=True, cwd=root, timeout=TIMEOUT_S, check=False)
    except (OSError, subprocess.TimeoutExpired) as problem:
        raise ShellError(f"ast-grep cannot run: {problem}") from None
    if completed.returncode != 0:
        raise ShellError(f"ast-grep failed (exit {completed.returncode}): {completed.stderr.strip()}")
    try:
        matches = json.loads(completed.stdout or "[]")
    except json.JSONDecodeError as problem:
        raise ShellError(f"ast-grep printed output that is not JSON: {problem}") from None
    commands: dict[str, list[ShellCommand]] = {path: [] for path in paths}
    pipelines: dict[str, list[tuple[int, int]]] = {path: [] for path in paths}
    lexer: dict[str, list[int]] = {path: [] for path in paths}
    unsets: dict[str, set[str]] = {path: set() for path in paths}
    checked: set[str] = set()
    for match in matches:
        path = match["file"]
        if path not in commands:
            raise ShellError(f"ast-grep reported {path}, which is not a script of the call")
        rule = match["ruleId"]
        span = match["range"]
        start, end = span["byteOffset"]["start"], span["byteOffset"]["end"]
        line = span["start"]["line"] + 1
        variables = match.get("metaVariables", {})
        multi = variables.get("multi", {})
        if rule == "command":
            name = variables["single"]["CMD"]["text"]
            arguments = tuple(node["text"] for node in multi.get("ARGS", []))
            commands[path].append(ShellCommand(name, arguments, start, end, line))
        elif rule == "pipeline":
            pipelines[path].append((start, end))
        elif rule == "lexer-word":
            lexer[path].append(line)
        elif rule == "unset":
            unsets[path].update(node["text"] for node in multi.get("NAMES", []) if not node["text"].startswith("-"))
        elif rule == "resolve-check":
            checked.add(path)
    return {path: ShellScript(path, tuple(sorted(commands[path], key=lambda item: item.start)),
                              tuple(sorted(pipelines[path])), tuple(sorted(lexer[path])), frozenset(unsets[path]),
                              path in checked)
            for path in paths}
