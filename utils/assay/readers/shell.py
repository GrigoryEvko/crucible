"""The reader of shell scripts: the commands, the pipelines and the marked words of each script.

THE ENGINE
    The pinned ast-grep reads each script with its bash grammar, in one run
    for each call, and it prints its matches as JSON.  So the text of a
    heredoc body or of a comment is not a command, and a command over two
    lines is one command.  `setup install ast-grep` (assay.setup.install)
    installs the binary, and that module gives its path.

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
from collections.abc import Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import proc

INSTALL_COMMAND = "python3 utils/bin/setup install ast-grep"
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


def binary() -> str:
    """Return the path of the pinned ast-grep binary.

    Raises:
        AstGrepMissing: If the binary is absent or does not meet its pin
        ShellError: If the host has no pin of ast-grep
    """
    from assay.setup import install

    try:
        return str(install.program("ast-grep"))
    except install.InstallError as problem:
        if problem.status == install.ABSENT:
            raise AstGrepMissing(f"the pinned ast-grep is not installed.  Run: {INSTALL_COMMAND}\n{problem}") from None
        raise ShellError(f"the pinned ast-grep cannot be found: {problem}") from None


def read(root: Path, paths: Sequence[str], program: str | None = None) -> dict[str, ShellScript]:
    """Read each script, a path relative to the root, with one run of ast-grep.

    Raises:
        AstGrepMissing: If there is a script and the pinned ast-grep is absent
        ShellError: If ast-grep fails or gives output that is not its JSON
    """
    if not paths:
        return {}
    executable = program or binary()
    try:
        completed = proc.run(proc.Command([executable, "scan", "--inline-rules", RULES, "--json=compact", *paths],
                                          timeout_s=TIMEOUT_S, cwd=root))
    except proc.CannotStart as problem:
        raise ShellError(f"ast-grep cannot run: {problem.error}") from None
    except proc.TimedOut as problem:
        raise ShellError(f"ast-grep cannot run: {problem}") from None
    if completed.exit != 0:
        raise ShellError(f"ast-grep failed (exit {completed.exit}): {completed.err.strip()}")
    try:
        matches = json.loads(completed.out or "[]")
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
