#!/usr/bin/env python3
"""check-guard-engines — every guard names its engine, and no guard reads C++ as text.

A guard that decides a fact about C++ source must decide it from a parse: the
tree of scripts/tsast.py, the preprocessor store of scripts/preprocessed.py, or
the compiler.  A regex over source text, a hand count of brackets or a line scan
misses a comment, a string, a macro or a declaration over two lines.  This guard
keeps that count at zero.

THE ROSTER
    scripts/guard-engines.txt has one row for each script in scope:

        path | engine[,engine...] | the text the script reads, if any

    The engines are tsast, preprocessor, compiler, json, cmake-api, python-ast,
    ast-grep-bash, git, text, none and kit.  `text` names input that is not C++
    source (an allowlist, compiler output, git output, prose in a .md file), and
    the third column must say what it is.  `kit` names a module that implements
    an engine, such as scripts/tsast.py itself, and the code rules below do not
    read it.

THE SCOPE
    Every tracked or new file matching scripts/*.py, scripts/*.sh,
    test/**/*.py, test/**/*.sh, tools/mutation/*.py and
    tools/session_oracle/emit.py.

THE RULES
    1. Each file in scope has exactly one row, and each row names a file in
       scope.  A row has a known engine, and a `text` row says what it reads.
    2. scripts/cxx_lex.py does not exist, and no script uses it: no Python
       import (the Python `ast` module reads the script), and no word, string
       or heredoc body that names it in a shell script (ast-grep's bash grammar
       reads the script).
    3. A Python script that is not a `kit` row never passes the text of a
       tsast object to a regex or a string method.  The text of a tsast object
       is an attribute `.text`, `.source` or `.joined`, a call `.slice(...)`,
       or a call spelled(...).  The shapes refused are: a `re` function or a
       compiled pattern's search, match, fullmatch, findall, finditer, sub,
       subn or split with such an argument; a string method (split, rsplit,
       splitlines, partition, rpartition, startswith, endswith, find, rfind,
       index, rindex, count, replace, strip, lstrip, rstrip) called on such a
       text; and an `in` or `not in` test whose right side is such a text.
       Three tsast doors are admitted: prose_text() gives the words of a
       comment or a string, lexeme() gives the spelling of exactly one token,
       and excerpt() or Tree.line() gives a row to show in a report.  A
       comparison with `==` reads a whole leaf and is admitted.
    4. A shell script never runs grep, egrep, fgrep, rg, ugrep, awk, gawk, mawk,
       sed or perl over C++: no argument names a C++ source root (include, src,
       test, vessel, bench, fuzz, tools, examples), a C++ suffix or a C++ file
       type, and no xargs or find hands such a tool a C++ root.
    5. A row agrees with its script: a Python `tsast` row imports tsast or
       mintmodel, a `preprocessor` row imports preprocessed, a `python-ast` row
       imports ast, a `json` row imports json, and an `ast-grep-bash` row names
       scripts/install-ast-grep.sh.

WHAT THE RULES CANNOT SEE
    Rule 3 reads direct expressions.  A text that goes through a variable
    first (`name = node.text; re.search(p, name)`) passes.  Rule 4 reads the
    arguments of one command.  A root that reaches the tool through a variable
    set elsewhere passes.  Review holds the rest.

Exit 0 clean, 1 on a finding, 2 on a usage error or a failed self-test, 3 when
the pinned ast-grep is not installed.
"""

from __future__ import annotations

import ast
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ROSTER = Path("scripts/guard-engines.txt")
# Git pathspecs.  A plain pathspec's `*` also matches `/`, so "test/*.py"
# covers every depth under test/.
SCOPE = (
    "scripts/*.py", "scripts/*.sh", "test/*.py", "test/*.sh", "tools/mutation/*.py",
    "tools/session_oracle/emit.py",
)
ENGINES = frozenset({
    "tsast", "preprocessor", "compiler", "json", "cmake-api", "python-ast", "ast-grep-bash", "git", "text",
    "none", "kit",
})
RE_FUNCTIONS = frozenset({"search", "match", "fullmatch", "findall", "finditer", "sub", "subn", "split"})
STRING_METHODS = frozenset({
    "split", "rsplit", "splitlines", "partition", "rpartition", "startswith", "endswith", "find", "rfind",
    "index", "rindex", "count", "replace", "strip", "lstrip", "rstrip",
})
TEXT_ATTRIBUTES = frozenset({"text", "source", "joined"})
# The tsast doors whose result a string operation may read: the words of a
# comment or a string, the spelling of one token, and a row shown in a report.
ADMITTED_READS = frozenset({"prose_text", "lexeme", "excerpt"})
TEXT_TOOLS = r"^(.*/)?(grep|egrep|fgrep|rg|ugrep|awk|gawk|mawk|sed|perl)$"
SOURCE_ROOTS = ("include", "src", "test", "vessel", "bench", "fuzz", "tools", "examples")
# An argument that names C++ source: a root directory as the whole word or a
# path segment, a C++ suffix or glob, or a ripgrep or grep file type.
SOURCE_ARGUMENT = re.compile(
    r"(^|[/'\"=}])(" + "|".join(SOURCE_ROOTS) + r")(/|['\"]?$)"
    r"|\.(h|hpp|cpp|cc)\b"
    r"|^(-t|--type=?)['\"]?(cpp|c\+\+|cxx)['\"]?$"
    r"|--include=?['\"]?\*\.(h|hpp|cpp|cc)"
)

# The ast-grep rules that read a shell script.
AST_GREP_RULES = """\
id: text-tool
language: bash
rule:
  all:
    - pattern: $CMD $$$ARGS
    - has: {field: name, regex: '%s'}
---
id: tool-runner
language: bash
rule:
  all:
    - pattern: $CMD $$$ARGS
    - has: {field: name, regex: '^(.*/)?(xargs|find)$'}
---
id: xargs-pipeline
language: bash
rule:
  kind: pipeline
  all:
    - has:
        kind: command
        all:
          - has: {field: name, regex: '^(.*/)?xargs$'}
          - has: {field: argument, regex: '%s'}
    - has:
        kind: command
        has: {field: argument, regex: '%s'}
---
id: cxx-lex
language: bash
rule:
  any:
    - {kind: word, regex: 'cxx_lex'}
    - {kind: string, regex: 'cxx_lex'}
    - {kind: raw_string, regex: 'cxx_lex'}
    - {kind: heredoc_body, regex: 'cxx_lex'}
---
id: ast-grep-installer
language: bash
rule:
  any:
    - {kind: word, regex: 'install-ast-grep\\.sh'}
    - {kind: string, regex: 'install-ast-grep\\.sh'}
""" % (
    TEXT_TOOLS,
    # The same tool names as one argument word, and the source test for an
    # argument, in the Rust regex dialect of ast-grep: \\x27 is a single quote,
    # which a single-quoted YAML string cannot hold as is.
    r"^[\"\x27]?(grep|egrep|fgrep|rg|ugrep|awk|gawk|mawk|sed|perl)[\"\x27]?$",
    r"(^|[/=}\"\x27])(" + "|".join(SOURCE_ROOTS) + r")(/|[\"\x27]?$)|\.(h|hpp|cpp|cc)\b",
)


# The options of each text tool that take the next word as a value, and the
# options that supply the pattern or program, so that no word is taken as one.
# A word that is not an option and not a value is the pattern or program, when
# no option supplied one, and every later such word is a file.
VALUE_OPTIONS = {
    "grep": {"-e", "-f", "-m", "-A", "-B", "-C", "-d", "-D", "--regexp", "--file", "--max-count",
             "--after-context", "--before-context", "--context", "--include", "--exclude", "--exclude-dir",
             "--label"},
    "rg": {"-e", "-f", "-g", "-t", "-T", "-m", "-A", "-B", "-C", "-M", "-j", "-r", "-d", "--type", "--type-not",
           "--glob", "--iglob", "--regexp", "--file", "--max-count", "--type-add", "--max-columns", "--threads",
           "--sort", "--sortr", "--color", "--colors", "--encoding", "--engine", "--replace", "--pre",
           "--pre-glob", "--max-depth", "--path-separator", "--context-separator"},
    "awk": {"-f", "-v", "-F", "--file", "--assign", "--field-separator"},
    "sed": {"-e", "-f", "-l", "--expression", "--file", "--line-length"},
    "perl": {"-e", "-E", "-I", "-M", "-m"},
}
PATTERN_OPTIONS = {
    "grep": {"-e", "-f", "--regexp", "--file"},
    "rg": {"-e", "-f", "--regexp", "--file"},
    "awk": {"-f", "--file"},
    "sed": {"-e", "-f", "--expression", "--file"},
    "perl": {"-e", "-E"},
}
TOOL_FAMILY = {"grep": "grep", "egrep": "grep", "fgrep": "grep", "ugrep": "grep", "rg": "rg", "awk": "awk",
               "gawk": "awk", "mawk": "awk", "sed": "sed", "perl": "perl"}
CPP_TYPE = re.compile(r"^(c\+\+|cpp|cxx)$")
CPP_GLOB = re.compile(r"\*\.(h|hpp|cpp|cc)\b")


def unquote(word: str) -> str:
    """Remove one pair of matching outer quotes from a shell word."""
    if len(word) >= 2 and word[0] == word[-1] and word[0] in "'\"":
        return word[1:-1]
    return word


NON_CPP_SUFFIX = re.compile(r"\.(txt|json|md|py|sh|yml|yaml|cmake|in|log|csv)$")


def names_cpp_source(word: str) -> bool:
    """Say whether a file operand names C++ source: a source root, a C++ file or a C++ glob.

    A file under a source root whose suffix is not C++, such as
    bench/CMakeLists.txt, is not source.
    """
    return bool(SOURCE_ARGUMENT.search(word)) and not NON_CPP_SUFFIX.search(unquote(word))


def reads_cpp_source(tool: str, arguments: list[str]) -> list[str]:
    """Return the arguments that make one text-tool command read C++ source.

    The pattern or program of the tool is not a file, so a pattern that
    mentions a header name reads nothing.  A file operand that names a C++
    root or suffix reads source, and so does a ripgrep C++ type or glob.

    Args:
        tool: The command name, without its directory
        arguments: The argument words of the command, as written

    Returns:
        The words that read C++ source, empty when the command reads none
    """
    family = TOOL_FAMILY.get(tool)
    if family is None:
        return []
    values = VALUE_OPTIONS[family]
    supplied = PATTERN_OPTIONS[family]
    hits: list[str] = []
    operands: list[str] = []
    pattern_given = False
    index = 0
    while index < len(arguments):
        word = unquote(arguments[index])
        option, _, attached = word.partition("=")
        if family == "rg" and option in ("-t", "--type") and index + 1 < len(arguments) and not attached:
            if CPP_TYPE.match(unquote(arguments[index + 1])):
                hits.append(arguments[index] + " " + arguments[index + 1])
        if family == "rg" and (option == "--type" and CPP_TYPE.match(attached) or
                               word.startswith("-t") and len(word) > 2 and CPP_TYPE.match(word[2:])):
            hits.append(arguments[index])
        if family in ("rg", "grep") and option in ("-g", "--glob", "--iglob", "--include"):
            glob = attached or (unquote(arguments[index + 1]) if index + 1 < len(arguments) else "")
            if CPP_GLOB.search(glob):
                hits.append(arguments[index])
        if option in supplied:
            pattern_given = True
        if option in values and not attached:
            index += 2
            continue
        if word.startswith("-") and word != "-":
            index += 1
            continue
        operands.append(arguments[index])
        index += 1
    files = operands if pattern_given else operands[1:]
    hits.extend(word for word in files if names_cpp_source(word))
    return hits


class AstGrepMissing(RuntimeError):
    """The pinned ast-grep binary is not installed."""


@dataclass(frozen=True)
class Row:
    """One roster row."""

    path: str
    engines: frozenset[str]
    reads: str
    line: int


def read_roster(root: Path) -> tuple[list[Row], list[str]]:
    """Read the roster and return its rows and every malformed-row finding.

    Args:
        root: The repository root

    Returns:
        (rows, findings)
    """
    rows: list[Row] = []
    findings: list[str] = []
    path = root / ROSTER
    if not path.is_file():
        return rows, [f"GUARD-ENGINES roster missing: {ROSTER} does not exist."]
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip() if not raw.lstrip().startswith("#") else ""
        if not line:
            continue
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 3:
            findings.append(f"GUARD-ENGINES roster row {ROSTER}:{number} has {len(cells)} cells, not 3: {raw!r}")
            continue
        engines = frozenset(name.strip() for name in cells[1].split(",") if name.strip())
        unknown = sorted(engines - ENGINES)
        if not engines or unknown:
            findings.append(f"GUARD-ENGINES roster row {ROSTER}:{number} names an unknown engine {unknown or '(none)'}; "
                            f"the engines are {', '.join(sorted(ENGINES))}.")
            continue
        if "text" in engines and not cells[2]:
            findings.append(f"GUARD-ENGINES roster row {ROSTER}:{number} reads text and does not say which text: "
                            f"name the input, for example 'allowlist rows' or 'compiler diagnostics'.")
        rows.append(Row(cells[0], engines, cells[2], number))
    return rows, findings


def scope_files(root: Path) -> list[str]:
    """Return every tracked or new file in scope, sorted.

    Args:
        root: The repository root

    Returns:
        Root-relative paths
    """
    listed = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "--", *SCOPE],
        capture_output=True, text=True, check=True,
    ).stdout.split()
    return sorted(path for path in dict.fromkeys(listed) if (root / path).is_file())


def is_text_of_tsast(node: ast.AST) -> bool:
    """Say whether an expression is, or holds directly, the text of a tsast object.

    Args:
        node: A Python expression

    Returns:
        True for `.text`, `.source`, `.slice(...)` or `spelled(...)` inside it,
        not counting the body of a lambda
    """
    stack = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, ast.Attribute) and current.attr in TEXT_ATTRIBUTES:
            return True
        if isinstance(current, ast.Call):
            func = current.func
            if isinstance(func, ast.Attribute) and func.attr in ("slice", "spelled"):
                return True
            if isinstance(func, ast.Name) and func.id == "spelled":
                return True
            if isinstance(func, ast.Attribute) and func.attr in ADMITTED_READS:
                continue
            if isinstance(func, ast.Name) and func.id in ADMITTED_READS:
                continue
        if isinstance(current, ast.Lambda):
            continue
        stack.extend(ast.iter_child_nodes(current))
    return False


def python_text_reads(text: str) -> list[tuple[int, str]]:
    """Return each place where a Python script passes tsast text to a regex or a string method.

    Complexity: linear in the size of the module's syntax tree.

    Args:
        text: The Python source

    Returns:
        (line, description) for each refused shape
    """
    try:
        tree = ast.parse(text)
    except SyntaxError as exc:
        return [(exc.lineno or 0, f"the script does not parse: {exc.msg}")]
    found: list[tuple[int, str]] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            method = node.func.attr
            receiver = node.func.value
            arguments = [*node.args, *(keyword.value for keyword in node.keywords)]
            if method in RE_FUNCTIONS and any(is_text_of_tsast(argument) for argument in arguments):
                found.append((node.lineno, f"a regex {method}() over the text of a tsast node"))
            elif method in STRING_METHODS and is_text_of_tsast(receiver):
                found.append((node.lineno, f"a string {method}() on the text of a tsast node"))
        elif isinstance(node, ast.Compare):
            for operator, right in zip(node.ops, node.comparators):
                if isinstance(operator, (ast.In, ast.NotIn)) and is_text_of_tsast(right):
                    found.append((node.lineno, "an `in` test over the text of a tsast node"))
    return sorted(found)


def python_imports(text: str) -> set[str]:
    """Return the top-level module names a Python script imports.

    Args:
        text: The Python source

    Returns:
        The module names, or an empty set when the script does not parse
    """
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return set()
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module is not None and node.level == 0:
            names.add(node.module.split(".")[0])
    return names


def python_names_installer(text: str) -> bool:
    """Say whether a Python script names scripts/install-ast-grep.sh in a string constant."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return False
    return any(isinstance(node, ast.Constant) and isinstance(node.value, str)
               and "install-ast-grep.sh" in node.value for node in ast.walk(tree))


def ast_grep_binary(root: Path) -> str:
    """Return the path of the pinned ast-grep binary.

    Raises:
        AstGrepMissing: If scripts/install-ast-grep.sh reports that it is absent
        RuntimeError: If the installer fails in another way
    """
    result = subprocess.run(["bash", str(root / "scripts" / "install-ast-grep.sh"), "--print-bin"],
                            capture_output=True, text=True)
    if result.returncode == 3:
        raise AstGrepMissing("the pinned ast-grep is not installed; run: bash scripts/install-ast-grep.sh")
    if result.returncode != 0:
        raise RuntimeError(f"scripts/install-ast-grep.sh --print-bin failed:\n{result.stderr.strip()}")
    return result.stdout.strip()


@dataclass
class ShellFacts:
    """What the parse of one shell script says."""

    source_reads: list[tuple[int, str]]
    names_cxx_lex: bool = False
    names_installer: bool = False


def shell_facts(root: Path, binary: str, paths: list[str]) -> dict[str, ShellFacts]:
    """Parse the shell scripts with ast-grep and collect what the rules need.

    One ast-grep run reads every script.  Complexity: linear in their size.

    Args:
        root: The repository root
        binary: The ast-grep binary
        paths: Root-relative shell script paths

    Returns:
        The facts of each script
    """
    facts = {path: ShellFacts([]) for path in paths}
    if not paths:
        return facts
    result = subprocess.run([binary, "scan", "--inline-rules", AST_GREP_RULES, "--json=compact", *paths],
                            capture_output=True, text=True, cwd=root)
    if result.returncode != 0:
        raise RuntimeError(f"ast-grep failed (exit {result.returncode}):\n{result.stderr.strip()}")
    matches = json.loads(result.stdout or "[]")
    tool_name = re.compile(TEXT_TOOLS)
    for match in matches:
        path = match["file"]
        if path not in facts:
            raise RuntimeError(f"ast-grep reported {path}, which is not a scanned script")
        line = match["range"]["start"]["line"] + 1
        rule = match["ruleId"]
        if rule == "cxx-lex":
            facts[path].names_cxx_lex = True
        elif rule == "ast-grep-installer":
            facts[path].names_installer = True
        elif rule == "xargs-pipeline":
            facts[path].source_reads.append((line, "a pipeline that hands C++ files to a text tool through xargs"))
        elif rule in ("text-tool", "tool-runner"):
            arguments = [node["text"] for node in match["metaVariables"]["multi"].get("ARGS", [])]
            command = match["metaVariables"]["single"]["CMD"]["text"]
            if rule == "text-tool":
                hits = reads_cpp_source(command.rsplit("/", 1)[-1], arguments)
            else:
                # `find ROOTS ... -exec TOOL`: the start paths of find, the
                # words before its first expression word, are what the tool
                # reads.  An xargs TOOL reads its files from the pipe, which
                # the xargs-pipeline rule checks.
                if not any(tool_name.match(unquote(word)) for word in arguments):
                    continue
                if command.rsplit("/", 1)[-1] != "find":
                    continue
                starts: list[str] = []
                for word in arguments:
                    if unquote(word).startswith(("-", "(", "!", ")")):
                        break
                    starts.append(word)
                hits = [word for word in starts if names_cpp_source(word)]
            if hits:
                facts[path].source_reads.append((line, f"{command} over C++ ({', '.join(hits[:3])})"))
    return facts


def check_tree(root: Path) -> list[str]:
    """Return every finding for one tree.

    Args:
        root: The repository root

    Returns:
        One message for each finding, in a stable order

    Raises:
        AstGrepMissing: If there is a shell script and the pinned ast-grep is not installed
    """
    rows, findings = read_roster(root)
    files = scope_files(root)
    by_path: dict[str, Row] = {}
    for row in rows:
        if row.path in by_path:
            findings.append(f"GUARD-ENGINES roster lists {row.path} twice ({ROSTER}:{by_path[row.path].line} "
                            f"and {ROSTER}:{row.line}).")
        by_path[row.path] = row
    for path in files:
        if path not in by_path:
            findings.append(f"GUARD-ENGINES unlisted: {path} has no row in {ROSTER}. Add `{path} | <engine> | "
                            f"<text it reads>`.")
    for path in sorted(set(by_path) - set(files)):
        findings.append(f"GUARD-ENGINES stale row: {ROSTER}:{by_path[path].line} names {path}, which is not a "
                        f"script in scope. Remove the row.")
    if (root / "scripts" / "cxx_lex.py").exists():
        findings.append("GUARD-ENGINES scripts/cxx_lex.py exists. It blanks comments so a regex can read code, "
                        "and the parse replaces it. Delete it.")

    shells = [path for path in files if path.endswith(".sh")]
    # The binary comes from this checkout, so the self-test's scratch tree
    # needs no install of its own.
    facts = shell_facts(root, ast_grep_binary(REPO_ROOT), shells) if shells else {}
    for path in files:
        row = by_path.get(path)
        if path.endswith(".py"):
            text = (root / path).read_text(encoding="utf-8", errors="replace")
            imports = python_imports(text)
            if "cxx_lex" in imports:
                findings.append(f"GUARD-ENGINES {path} imports cxx_lex. Read the code from tsast nodes.")
            if row is None:
                continue
            if "kit" not in row.engines:
                for line, what in python_text_reads(text):
                    findings.append(f"GUARD-ENGINES {path}:{line}: {what}. Read the answer from the node's "
                                    f"children, leaf_name() or qualified_parts(), or read prose through "
                                    f"tsast.prose_text().")
            wants = {"tsast": {"tsast", "mintmodel"}, "preprocessor": {"preprocessed"},
                     "python-ast": {"ast"}, "json": {"json"}}
            for engine, modules in wants.items():
                if engine in row.engines and not imports & modules:
                    findings.append(f"GUARD-ENGINES {ROSTER}:{row.line} says {path} uses {engine}, and it imports "
                                    f"none of {', '.join(sorted(modules))}.")
            if "ast-grep-bash" in row.engines and not python_names_installer(text):
                findings.append(f"GUARD-ENGINES {ROSTER}:{row.line} says {path} uses ast-grep-bash, and it never "
                                f"names scripts/install-ast-grep.sh.")
        else:
            shell = facts[path]
            if shell.names_cxx_lex:
                findings.append(f"GUARD-ENGINES {path} names cxx_lex. Read the code from tsast nodes.")
            for line, what in shell.source_reads:
                findings.append(f"GUARD-ENGINES {path}:{line}: {what}. A shell script does not read C++; move "
                                f"the check to Python on tsast nodes.")
            if row is not None and "ast-grep-bash" in row.engines and not shell.names_installer:
                findings.append(f"GUARD-ENGINES {ROSTER}:{row.line} says {path} uses ast-grep-bash, and it never "
                                f"names scripts/install-ast-grep.sh.")
    return findings


def run_check(root: Path) -> int:
    """Check one tree and print the report.

    Args:
        root: The repository root

    Returns:
        0 clean, 1 on a finding, 3 when ast-grep is absent
    """
    try:
        findings = check_tree(root)
    except AstGrepMissing as exc:
        print(f"check-guard-engines: {exc}", file=sys.stderr)
        return 3
    for message in findings:
        print(message, file=sys.stderr)
    if findings:
        print(f"\ncheck-guard-engines: {len(findings)} finding(s).", file=sys.stderr)
        return 1
    print("check-guard-engines: clean — every script names its engine, and none reads C++ as text.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant a scratch tree with each violation and each admitted shape.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        throwaway_repo.init(root)
        (root / "scripts").mkdir()
        (root / "test").mkdir()
        files = {
            "scripts/install-ast-grep.sh": "#!/bin/bash\nprintf 'a stand-in with no source read\\n'\n",
            "scripts/clean.py": "import tsast\nimport re\n"
                                "def f(node, comment, tree):\n"
                                "    ok = node.text == 'x' and tsast.lexeme(node).endswith('_t')\n"
                                "    shown = tsast.excerpt(node).strip() + tree.line(3).strip()\n"
                                "    return re.search('MARK', tsast.prose_text(comment)) and ok and shown\n",
            "scripts/report_lines.py": "import tsast\n"
                                       "def f(tree):\n    return tree.source.decode().splitlines()\n",
            "scripts/regex_text.py": "import tsast\nimport re\n"
                                     "def f(node):\n    return re.search('::', node.text)\n",
            "scripts/split_text.py": "import tsast\ndef f(node):\n    return node.text.split('::')\n",
            "scripts/in_text.py": "import tsast\ndef f(node):\n    return 'decltype' in node.text\n",
            "scripts/pattern_spelled.py": "import tsast\nimport re\nPAT = re.compile('x')\n"
                                          "def f(node):\n    return PAT.findall(tsast.spelled(node))\n",
            "scripts/uses_lexer.py": "import tsast\nimport cxx_lex\n",
            "scripts/wrong_engine.py": "import json\n",
            "scripts/kit.py": "import re\ndef f(node):\n    return re.search('x', node.text)\n",
            "scripts/grep_source.sh": "#!/bin/bash\nrg -n 'reinterpret_cast' include src\n",
            "scripts/xargs_source.sh": "#!/bin/bash\nfind include -name '*.h' | xargs grep -l foo\n",
            "scripts/grep_other.sh": "#!/bin/bash\ngit log --format=%s | rg -q '^Give'\n"
                                     "cat <<'EOF'\nrg -n x include\nEOF\n",
            "scripts/lexer_heredoc.sh": "#!/bin/bash\npython3 - <<'EOF'\nimport cxx_lex\nEOF\n",
            "scripts/grep_log.sh": "#!/bin/bash\ngrep -q 'include/planted/Moved.h:5' \"$log\"\n"
                                   "awk '/include\\/x.h/ {print}' \"$out\"\n",
            "scripts/rg_type.sh": "#!/bin/bash\nrg --type=cpp -n 'reinterpret_cast'\n",
            "scripts/find_exec.sh": "#!/bin/bash\nfind src -name '*.cpp' -exec grep -l foo {} +\n",
            "test/driver.py": "import json\n",
        }
        for path, text in files.items():
            (root / path).write_text(text)
        roster = [
            "scripts/clean.py | tsast | ",
            "scripts/report_lines.py | tsast | ",
            "scripts/regex_text.py | tsast | ",
            "scripts/split_text.py | tsast | ",
            "scripts/in_text.py | tsast | ",
            "scripts/pattern_spelled.py | tsast | ",
            "scripts/uses_lexer.py | tsast | ",
            "scripts/wrong_engine.py | tsast | ",
            "scripts/kit.py | kit | ",
            "scripts/grep_source.sh | text | shell text",
            "scripts/xargs_source.sh | text | shell text",
            "scripts/grep_other.sh | git,text | git log subjects",
            "scripts/lexer_heredoc.sh | none | ",
            "scripts/grep_log.sh | text | a guard's report",
            "scripts/rg_type.sh | text | shell text",
            "scripts/find_exec.sh | text | shell text",
            "scripts/install-ast-grep.sh | text | the release checksum",
            "scripts/gone.py | tsast | ",
            "scripts/bad_engine.py | regex | ",
            "scripts/no_input.py | text | ",
        ]
        (root / ROSTER).write_text("\n".join(roster) + "\n")
        try:
            findings = check_tree(root)
        except AstGrepMissing as exc:
            print(f"check-guard-engines --self-test: {exc}", file=sys.stderr)
            return 3
        joined = "\n".join(findings)

        def flagged(fragment: str) -> bool:
            """Say whether a finding names the fragment."""
            return any(fragment in message for message in findings)

        expect("a regex over node.text is refused", flagged("regex_text.py:4"))
        expect("a split() on node.text is refused", flagged("split_text.py:3"))
        expect("an `in` test over node.text is refused", flagged("in_text.py:3"))
        expect("a compiled pattern over spelled() is refused", flagged("pattern_spelled.py:5"))
        expect("an import of cxx_lex is refused", flagged("uses_lexer.py imports cxx_lex"))
        expect("a heredoc that names cxx_lex is refused", flagged("lexer_heredoc.sh names cxx_lex"))
        expect("rg over include and src is refused", flagged("grep_source.sh:2"))
        expect("find and xargs that hand grep a C++ root are refused", flagged("xargs_source.sh:2"))
        expect("a row whose engine the script does not import is refused",
               flagged("wrong_engine.py uses tsast"))
        expect("a script with no row is refused", flagged("unlisted: test/driver.py"))
        expect("a row with no script is refused", flagged("names scripts/gone.py"))
        expect("an unknown engine is refused", flagged("unknown engine ['regex']"))
        expect("a text row that does not say what it reads is refused", flagged("does not say which text"))
        expect("a splitlines() on the file source is refused", flagged("report_lines.py:3"))
        expect("a leaf compare, a lexeme, an excerpt, a row and a prose read are admitted", "clean.py" not in joined)
        expect("a kit row is not read by the code rules", "kit.py" not in joined)
        expect("rg over git output, and a heredoc that mentions rg, are admitted", "grep_other.sh" not in joined)
        expect("a pattern or program that names a header, over a report file, is admitted",
               "grep_log.sh" not in joined)
        expect("rg with a C++ file type is refused", flagged("rg_type.sh:2"))
        expect("find over a C++ root with -exec grep is refused", flagged("find_exec.sh:2"))
        (root / "scripts" / "cxx_lex.py").write_text("")
        expect("an existing scripts/cxx_lex.py is refused", any("cxx_lex.py exists" in m for m in check_tree(root)))
    if failures:
        print(f"check-guard-engines --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-guard-engines --self-test: every case passes.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    if sys.argv[1:]:
        print("usage: check-guard-engines.py [--self-test]", file=sys.stderr)
        sys.exit(2)
    sys.exit(run_check(REPO_ROOT))
