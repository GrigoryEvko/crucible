"""structured-reads — each input is read through its reader, no module reads C++ as text, and git runs only in its module.

THE SCOPE
    Each Python file and each shell script that git tracks, or that is new
    and that .gitignore does not match, outside utils/plants/.  A master of
    utils/bin/ has no suffix, and the rule reads it as a Python file.  The
    reader of assay.readers.python reads each Python file, and the reader of
    assay.readers.shell reads each shell script with the bash grammar of the
    pinned ast-grep.  A module of utils/assay/readers/ is a reader: the kinds
    node-text, node-text-variable, source-text, regex-call, diagnostic-text
    and format-read do not read it.

THE KINDS
    lexer               a file named cxx_lex.py, an import of a module named cxx_lex, or a word, a string or a heredoc
                        body of a shell script that names cxx_lex.  That module blanks comments so that a regex can
                        read code, and the parse replaces it
    node-text           the text of a tsast object (an attribute .text, .source or .joined, or a call .slice() or
                        spelled()) given to a regex function, to a string method (split, strip, startswith, find,
                        replace and the others), or on the right side of `in`.  A slice of the text and a string
                        method of TEXT_RESULTS on it give a text too.  str() of the text, a concatenation and an
                        f-string with the text give a text too.  A text that a call gives to a different function is
                        no such text, such as a key of a dict.  Three tsast doors are admitted.  prose_text() gives
                        the words of a comment or a string, and lexeme() gives the spelling of one token.  excerpt()
                        or Tree.line() gives a row to show in a report.  A comparison with == reads a whole leaf and
                        is admitted
    node-text-variable  the same reads of a local variable that holds the text of a tsast object.  A variable that a
                        text of a second variable gives holds a text too
    source-text         a string method, an `in` test or a regex over the text of a C++ file.  The module reads the
                        text with read_text(), read_bytes() or open().  A path names a C++ file when it holds a
                        string constant with a C++ suffix, such as "include/a.h" or "*.cpp".  A variable of the
                        module, of a function or of a loop can name a C++ file.  A variable can also hold the text,
                        a line of the text or an open C++ file.  The parse tree of assay.readers.tsast replaces the
                        read
    git-output          a use of the value of run() or run_any_exit() of assay.core.git in a different module.  A
                        call that is a statement uses no value, because it runs git for its effect.  A module gets
                        each answer of git from a function of assay.core.git that gives a typed value
    shell-cpp-read      a shell command grep, egrep, fgrep, rg, ugrep, awk, gawk, mawk, sed or perl whose file operand
                        names C++ source (a source root include, src, test, vessel, bench, utils/tools or examples, a
                        C++ suffix or a C++ file type), a find over such a root whose -exec runs such a tool, and a
                        pipeline that hands such a root to such a tool through xargs.  The pattern or the program of
                        the tool is not a file
    git-isolation       a script that runs git init, commit, update-ref, reset, checkout, fetch or apply and does not
                        isolate its repository.  A Python file calls init() of assay.core.git, which stops unless
                        git resolves the repository.  A shell script names each repository variable in an unset
                        command and passes --absolute-git-dir to a command.  utils/assay/core/git.py is the
                        implementation, and the rule does not read it
    git-command         a list or a tuple of words whose first word is "git" and whose second word is a git option,
                        a git command of GIT_VERBS or an expression, in a Python file outside utils/assay/core/git.py.
                        assay.core.git is the one reader of git (the plan, section 18.4): it runs each git command
                        through assay.core.proc, with a time limit and with no repository variable.  A tuple of
                        program names that starts with "git", such as the tools that a check of the host looks for,
                        is not a git command
    regex-call          a call of a function of the module re outside a reader.  The import table of the module gives
                        the target of the name of each call.  A call through an alias of re, such as `import re as
                        rx`, is such a call, and a call of a name that `from re import` makes is one too.  A call
                        whose subject is the result of prose_text() reads prose, and the rule admits it
    diagnostic-text     a string method with an argument that is the head of a diagnostic of GCC, such as "error:"
                        or "warning: ".  An `in` test with such a head on its left side, or with a list, a tuple or
                        a set of such heads on its right side.  The kinds of assay.readers.sarif give the heads.  The
                        text output of GCC has no format.  The SARIF log of the compile gives the kind, the message
                        and the option of each diagnostic
    format-read         a read of a structured format in a module that is not its reader.  A string constant that
                        names a structured file: CMakeCache.txt, compile_commands.json, build.ninja, .ninja_log, the
                        ctest list --show-only=json-v1 or the section .crucible.quarantine.  A string constant that
                        names a tool that writes a structured format as text: nm, readelf, c++filt, objdump or ldd.
                        A bytes constant that starts with the magic of an ELF file or of an ar archive.  A shell
                        command that runs such a tool.  A list, a tuple or a shell command that holds the words
                        HELP_WORDS one after the other.  These words run the help output of the build tool, and the
                        code model of assay.readers.cmake gives the targets.  The tables FORMATS, TOOLS, MAGICS and
                        HELP_WORDS of this module name each one, and the rule does not read this module for the kind
    The ledger of the rule holds the count of each file, kind and entity of
    today: the entity of a Python finding is the function that holds it, and
    each change that moves a read into its reader lowers a count.

WHAT THE RULE CANNOT SEE
    node-text-variable and source-text read the local variables of one
    function and the variables of the module.  The rule does not see a text
    that goes through a parameter, a return value or an attribute.
    source-text sees a C++ path only when a constant with a C++ suffix names
    it, and not a path from a list or a parameter.  A shell argument that
    reaches a tool through a variable set elsewhere passes.  git-command
    reads the literal word "git": a variable that holds the word, or a git
    command that is not in GIT_VERBS after a constant first word, passes.
    diagnostic-text sees a head only as a constant of the call or of the
    test.  A head in a variable, or a head that a call builds, gives no
    finding.
    Review holds the rest.

Complexity: linear in the size of the files of the scope.
"""

import ast
from collections.abc import Iterator, Sequence
from pathlib import Path, PurePosixPath
from typing import TypeGuard

from assay.core import proc
from assay.core import report as check_report
from assay.guard.engines.meta.runner import PLANTS, MetaContext, NotApplicable
from assay.readers import python as python_reader
from assay.readers import sarif
from assay.readers import shell as shell_reader

RULE = "structured-reads"
MASTERS = "utils/bin/"
READERS = "utils/assay/readers/"
GIT_IMPLEMENTATION = "utils/assay/core/git.py"
# This module: its table FORMATS names each structured file, and that table is no read.
SELF = "utils/assay/guard/engines/meta/structured_reads.py"
GIT_MODULE = "assay.core.git"
# The two doors of assay.core.git that give the raw output of a git command, and the one module that reads it.
GIT_RUNS = frozenset({f"{GIT_MODULE}.run", f"{GIT_MODULE}.run_any_exit"})
GIT_READERS = frozenset({GIT_IMPLEMENTATION})
LEXER = "cxx_lex"
RE_FUNCTIONS = frozenset({"search", "match", "fullmatch", "findall", "finditer", "sub", "subn", "split"})
RE_CALLS = RE_FUNCTIONS | {"compile"}
# The dotted name of each function of the module re that the kind regex-call refuses.  The import table of the module
# gives the target of the name of a call.
REGEX_CALLS = frozenset(f"re.{name}" for name in RE_CALLS)
# The head of each kind of a diagnostic of GCC in its text output, with and without the space after the colon (the
# kind diagnostic-text).
DIAGNOSTIC_HEADS = frozenset(head for kind in sarif.Kind for head in (f"{kind.value}:", f"{kind.value}: "))
# The words that run the help output of the build tool (the kind format-read).
HELP_WORDS = ("--target", "help")
HELP_READER = "assay.readers.cmake"
STRING_METHODS = frozenset({
    "split", "rsplit", "splitlines", "partition", "rpartition", "startswith", "endswith", "find", "rfind",
    "index", "rindex", "count", "replace", "strip", "lstrip", "rstrip",
})
TEXT_ATTRIBUTES = frozenset({"text", "source", "joined"})
TEXT_CALLS = frozenset({"slice", "spelled"})
# The string methods that give a text made from the text of their receiver.
TEXT_RESULTS = frozenset({
    "strip", "lstrip", "rstrip", "lower", "upper", "casefold", "replace", "removeprefix", "removesuffix", "decode",
    "encode", "expandtabs",
})
ADMITTED_READS = frozenset({"prose_text", "lexeme", "excerpt"})
# The calls that read a file as text, the calls that read an open file, and the calls that give the lines of a text.
SOURCE_READS = frozenset({"read_text", "read_bytes"})
HANDLE_READS = frozenset({"read", "readlines"})
LINE_SPLITS = frozenset({"splitlines", "split", "rsplit"})
PROSE_READ = "prose_text"
# Each tool that writes a structured format as text, and the reader of that format.  A run of the tool in a
# different module reads the format as text.
TOOLS = {"nm": "assay.readers.elf", "readelf": "assay.readers.elf", "c++filt": "assay.readers.elf",
         "objdump": "assay.readers.objdump", "ldd": "assay.readers.elf"}
# Each structured file or tool, and its reader.  Only its reader spells its name.
FORMATS = {"CMakeCache.txt": "assay.readers.cmake", "compile_commands.json": "assay.readers.compdb",
           "build.ninja": "assay.readers.ninja", ".ninja_log": "assay.readers.ninja",
           "--show-only=json-v1": "assay.readers.ctest", ".crucible.quarantine": "assay.readers.sections", **TOOLS}
# The magic at the start of each binary format, the entity of a finding, and its reader.
MAGICS = {b"\x7fELF": ("the ELF magic", "assay.readers.elf"), b"!<arch>\n": ("the ar magic", "assay.readers.elf")}
MUTATING = frozenset({"init", "commit", "update-ref", "reset", "checkout", "fetch", "apply"})
# The git commands that a second word of an argv can name (the kind git-command).
GIT_VERBS = frozenset({
    "add", "am", "apply", "archive", "bisect", "blame", "branch", "bundle", "cat-file", "check-attr", "check-ignore",
    "checkout", "checkout-index", "cherry-pick", "clean", "clone", "commit", "commit-tree", "config", "describe",
    "diff", "diff-files", "diff-index", "diff-tree", "fetch", "for-each-ref", "format-patch", "fsck", "gc", "grep",
    "hash-object", "init", "log", "ls-files", "ls-remote", "ls-tree", "merge", "merge-base", "mktree", "mv", "notes",
    "pull", "push", "read-tree", "rebase", "reflog", "remote", "repack", "reset", "restore", "rev-list", "rev-parse",
    "revert", "rm", "show", "show-ref", "sparse-checkout", "stash", "status", "submodule", "switch", "symbolic-ref",
    "tag", "update-index", "update-ref", "var", "verify-commit", "version", "worktree", "write-tree",
})
# A global git option that takes the next argument as its value when it is not written with '='.
OPTIONS_WITH_VALUE = frozenset({"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path",
                                "--config-env", "--super-prefix"})
TOOL_FAMILY = {"grep": "grep", "egrep": "grep", "fgrep": "grep", "ugrep": "grep", "rg": "rg", "awk": "awk",
               "gawk": "awk", "mawk": "awk", "sed": "sed", "perl": "perl"}
# The options of each text tool that take the next word as a value, and the options that give the pattern or the
# program, so that no such word is a file.  A word that is not an option and not a value is the pattern or the
# program, when no option gave one, and each later such word is a file.
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
SOURCE_ROOTS = ("include", "src", "test", "vessel", "bench", "utils/tools", "examples")
CPP_SUFFIXES = (".h", ".hpp", ".cpp", ".cc")
CPP_TYPES = frozenset({"c++", "cpp", "cxx"})
NON_CPP_SUFFIXES = (".txt", ".json", ".md", ".py", ".sh", ".yml", ".yaml", ".cmake", ".in", ".log", ".csv")
ROOT_BEFORE = "/'\"=}"
QUOTES = "'\""


# ── The words of a shell command ───────────────────────────────────


def unquote(word: str) -> str:
    """Remove one pair of matching outer quotes from a shell word."""
    if len(word) >= 2 and word[0] == word[-1] and word[0] in QUOTES:
        return word[1:-1]
    return word


def is_word_character(character: str) -> bool:
    """Whether a character is a word character: a letter, a digit or '_'."""
    return character.isalnum() or character == "_"


def names_root(word: str) -> bool:
    """Whether a word names a source root: the root as the whole word or as a path segment.

    The root starts the word or follows '/', a quote, '=' or '}', and the
    word ends after it, or a '/' follows it, or a quote ends the word after
    it.
    """
    for root in SOURCE_ROOTS:
        start = word.find(root)
        while start >= 0:
            before = word[start - 1] if start > 0 else ""
            after = word[start + len(root):]
            if (not before or before in ROOT_BEFORE) and (not after or after[0] == "/"
                                                          or (len(after) == 1 and after in QUOTES)):
                return True
            start = word.find(root, start + 1)
    return False


def names_suffix(word: str) -> bool:
    """Whether a word holds a C++ suffix that no word character follows."""
    for suffix in CPP_SUFFIXES:
        start = word.find(suffix)
        while start >= 0:
            end = start + len(suffix)
            if end == len(word) or not is_word_character(word[end]):
                return True
            start = word.find(suffix, start + 1)
    return False


def names_source(word: str) -> bool:
    """Whether a word names C++ source as the ast-grep rule of a pipeline reads it: a source root or a C++ suffix."""
    return names_root(word) or names_suffix(word)


def names_cpp_file(word: str) -> bool:
    """Whether a file operand names C++ source.  A file under a source root with a suffix that is not C++ does not."""
    return names_source(word) and not unquote(word).endswith(NON_CPP_SUFFIXES)


def is_cpp_type(text: str) -> bool:
    """Whether a file type of rg is C++."""
    return unquote(text) in CPP_TYPES


def is_cpp_glob(text: str) -> bool:
    """Whether a glob holds a C++ suffix after '*'."""
    for suffix in CPP_SUFFIXES:
        start = text.find("*" + suffix)
        while start >= 0:
            end = start + 1 + len(suffix)
            if end == len(text) or not is_word_character(text[end]):
                return True
            start = text.find("*" + suffix, start + 1)
    return False


def tool_name(word: str) -> str:
    """Return the name of a command without its directory and its quotes."""
    return unquote(word).rsplit("/", 1)[-1]


def reads_cpp_source(tool: str, arguments: tuple[str, ...]) -> list[str]:
    """Return the arguments that make one text-tool command read C++ source.

    The pattern or the program of the tool is not a file, so a pattern that
    names a header reads nothing.  A file operand that names a C++ root or
    suffix reads source, and so does a C++ type or glob of rg.

    Complexity: linear in the arguments.
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
        option, equals, attached = word.partition("=")
        if family == "rg" and option in ("-t", "--type") and index + 1 < len(arguments) and not equals:
            if is_cpp_type(arguments[index + 1]):
                hits.append(arguments[index] + " " + arguments[index + 1])
        if family == "rg" and ((option == "--type" and is_cpp_type(attached))
                               or (word.startswith("-t") and len(word) > 2 and is_cpp_type(word[2:]))):
            hits.append(arguments[index])
        if family in ("rg", "grep") and option in ("-g", "--glob", "--iglob", "--include"):
            glob = attached or (unquote(arguments[index + 1]) if index + 1 < len(arguments) else "")
            if is_cpp_glob(glob):
                hits.append(arguments[index])
        if option in supplied:
            pattern_given = True
        if option in values and not equals:
            index += 2
            continue
        if word.startswith("-") and word != "-":
            index += 1
            continue
        operands.append(arguments[index])
        index += 1
    files = operands if pattern_given else operands[1:]
    hits.extend(word for word in files if names_cpp_file(word))
    return hits


def git_verb(arguments: tuple[str, ...]) -> str | None:
    """Return the verb of a git command from its arguments, or None.  A quoted or expanded argument is never a verb."""
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


def is_git_command(name: str) -> bool:
    """Whether a command runs git: its name is git, or the expansion of each element of an array."""
    if tool_name(name) == "git":
        return True
    text = unquote(name)
    return text.startswith("${") and text.endswith(("[@]}", "[*]}"))


def shell_findings(path: str, script: shell_reader.ShellScript) -> Iterator[check_report.Finding]:
    """Give the findings of one shell script."""
    for line in script.lexer_words[:1]:
        yield check_report.Finding("error", path, line, RULE, "the script names cxx_lex, which blanks comments so "
                                   "that a regex can read code.  Read the code from tsast nodes", "lexer", LEXER)
    for command in script.commands:
        tool = tool_name(command.name)
        if tool in TOOLS:
            yield check_report.Finding(
                "error", path, command.line, RULE, f"{command.name} writes a structured format as text, and a shell "
                f"script reads it outside its reader {TOOLS[tool]}.  Read the file through its reader", "format-read",
                tool)
        if has_run([unquote(word) for word in command.arguments], HELP_WORDS):
            yield check_report.Finding(
                "error", path, command.line, RULE, f"{command.name} runs the help output of the build tool, which has "
                f"no format.  Read the targets from the code model of {HELP_READER}", "format-read",
                " ".join(HELP_WORDS))
        hits = reads_cpp_source(tool, command.arguments)
        if not hits and tool == "find" and any(tool_name(word) in TOOL_FAMILY for word in command.arguments):
            starts: list[str] = []
            for word in command.arguments:
                if unquote(word).startswith(("-", "(", "!", ")")):
                    break
                starts.append(word)
            hits = [word for word in starts if names_cpp_file(word)]
        if hits:
            yield check_report.Finding(
                "error", path, command.line, RULE, f"{command.name} reads C++ ({', '.join(hits[:3])}).  A shell "
                f"script does not read C++.  Move the check to Python on tsast nodes", "shell-cpp-read", tool)
    for start, end in script.pipelines:
        members = [command for command in script.commands if start <= command.start and command.end <= end]
        hands_tool = any(tool_name(command.name) == "xargs"
                         and any(tool_name(word) in TOOL_FAMILY for word in command.arguments) for command in members)
        names_root_word = any(names_source(word) for command in members for word in command.arguments)
        if hands_tool and names_root_word:
            line = min(command.line for command in members)
            yield check_report.Finding(
                "error", path, line, RULE, "a pipeline hands C++ files to a text tool through xargs.  Move the check to "
                "Python on tsast nodes", "shell-cpp-read", "xargs")
    verbs = {verb for command in script.commands if is_git_command(command.name)
             if (verb := git_verb(command.arguments)) in MUTATING}
    if verbs:
        missing = [name for name in proc.REPOSITORY_VARIABLES if name not in script.unset_names]
        reasons = ([f"it does not unset {', '.join(missing)}"] if missing else []) + (
            [] if script.checks_git_dir else ["it never checks --absolute-git-dir"])
        if reasons:
            yield check_report.Finding(
                "error", path, 0, RULE, f"it runs git {', '.join(sorted(verbs))}, and {', and '.join(reasons)}.  Unset "
                f"each repository variable and check the repository with --absolute-git-dir", "git-isolation", "git")


# ── The reads of a Python file ─────────────────────────────────────


def is_text_of_tsast(node: ast.AST, variables: frozenset[str] = frozenset()) -> bool:
    """Whether the value of an expression is the text of a tsast object, or a text that the expression makes from it.

    These expressions give such a text: an attribute of TEXT_ATTRIBUTES, a call of TEXT_CALLS and a variable of
    `variables`.  A slice of such a text, a string method of TEXT_RESULTS on it and str() of it give a text too.  A
    concatenation, an f-string and a conditional expression with such a text in it give a text too.  A text that a
    call gives to a different function is no value of the expression: `users.setdefault(node.text, [])` gives a
    list.  An admitted door gives no such text.  Complexity: linear in the number of nodes of the expression.
    """
    if isinstance(node, ast.Name):
        return node.id in variables
    if isinstance(node, ast.Attribute):
        return node.attr in TEXT_ATTRIBUTES
    if isinstance(node, ast.Call):
        func = node.func
        if isinstance(func, ast.Attribute) and func.attr in TEXT_CALLS:
            return True
        if isinstance(func, ast.Name) and func.id == "spelled":
            return True
        if isinstance(func, ast.Attribute) and func.attr in TEXT_RESULTS:
            return is_text_of_tsast(func.value, variables)
        if isinstance(func, ast.Name) and func.id == "str":
            return any(is_text_of_tsast(argument, variables) for argument in node.args)
        return False
    if isinstance(node, (ast.Subscript, ast.NamedExpr)):
        return is_text_of_tsast(node.value, variables)
    if isinstance(node, ast.BinOp):
        return is_text_of_tsast(node.left, variables) or is_text_of_tsast(node.right, variables)
    if isinstance(node, ast.IfExp):
        return is_text_of_tsast(node.body, variables) or is_text_of_tsast(node.orelse, variables)
    if isinstance(node, ast.BoolOp):
        return any(is_text_of_tsast(value, variables) for value in node.values)
    if isinstance(node, ast.JoinedStr):
        return any(isinstance(value, ast.FormattedValue) and is_text_of_tsast(value.value, variables)
                   for value in node.values)
    return False


def is_prose_read(node: ast.AST) -> bool:
    """Whether an expression is a call of prose_text()."""
    return isinstance(node, ast.Call) and (
        (isinstance(node.func, ast.Attribute) and node.func.attr == PROSE_READ)
        or (isinstance(node.func, ast.Name) and node.func.id == PROSE_READ))


def holds_git_argv(node: ast.List | ast.Tuple) -> bool:
    """Whether a list or a tuple holds the words of a git command: "git", then an option, a verb or an expression."""
    if len(node.elts) < 2 or not (isinstance(node.elts[0], ast.Constant) and node.elts[0].value == "git"):
        return False
    second = node.elts[1]
    if isinstance(second, ast.Constant):
        return isinstance(second.value, str) and (second.value.startswith("-") or second.value in GIT_VERBS)
    return True


def is_diagnostic_head(node: ast.AST) -> TypeGuard[ast.Constant]:
    """Return True for a string constant that is the head of a diagnostic of GCC (DIAGNOSTIC_HEADS)."""
    return isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value in DIAGNOSTIC_HEADS


def holds_diagnostic_head(node: ast.AST) -> bool:
    """Return True for a head of a diagnostic of GCC, and for a list, a tuple or a set that holds one."""
    if isinstance(node, (ast.List, ast.Tuple, ast.Set)):
        return any(is_diagnostic_head(element) for element in node.elts)
    return is_diagnostic_head(node)


def has_run(values: Sequence[object], words: tuple[str, ...]) -> bool:
    """Return True when a sequence holds `words` one after the other.  Complexity: the values times the words."""
    return any(tuple(values[start:start + len(words)]) == words for start in range(len(values) - len(words) + 1))


def holds_words(node: ast.List | ast.Tuple, words: tuple[str, ...]) -> bool:
    """Return True when a list or a tuple holds string constants equal to `words`, one after the other."""
    return has_run([element.value if isinstance(element, ast.Constant) else None for element in node.elts], words)


def docstring_nodes(tree: ast.Module) -> set[int]:
    """Return the id of the constant of each docstring of a module, its classes and its functions."""
    found: set[int] = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)) and node.body:
            first = node.body[0]
            if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant) and isinstance(first.value.value, str):
                found.add(id(first.value))
    return found


class PythonReads(ast.NodeVisitor):
    """Collect the findings of one Python file, with the function that holds each one."""

    def __init__(self, path: str, is_reader: bool, docstrings: set[int], bindings: python_reader.Bindings) -> None:
        """Keep the place of the file, its import table and the state of the walk."""
        self.path = path
        self.is_reader = is_reader
        self.docstrings = docstrings
        self.bindings = bindings
        self.scopes: list[str] = []
        self.text_variables: list[set[str]] = [set()]
        # The variables of each scope that name a C++ file, that hold its text, and that hold an open C++ file.
        self.cpp_paths: list[set[str]] = [set()]
        self.source_texts: list[set[str]] = [set()]
        self.cpp_handles: list[set[str]] = [set()]
        self.discarded: set[int] = set()
        self.found: list[check_report.Finding] = []

    def where(self) -> str:
        """Return the qualified name of the function or the class that the walk is in, or <module>."""
        return ".".join(self.scopes) if self.scopes else "<module>"

    def add(self, node: ast.AST, kind: str, message: str, entity: str | None = None) -> None:
        """Add one finding at a node."""
        self.found.append(check_report.Finding("error", self.path, getattr(node, "lineno", 0), RULE, message, kind,
                                               entity if entity is not None else self.where()))

    def visit_scope(self, node: ast.FunctionDef | ast.AsyncFunctionDef | ast.ClassDef) -> None:
        """Walk the body of a function or a class in its own scope."""
        self.scopes.append(node.name)
        for table in (self.text_variables, self.cpp_paths, self.source_texts, self.cpp_handles):
            table.append(set())
        self.generic_visit(node)
        for table in (self.text_variables, self.cpp_paths, self.source_texts, self.cpp_handles):
            table.pop()
        self.scopes.pop()

    visit_FunctionDef = visit_scope
    visit_AsyncFunctionDef = visit_scope
    visit_ClassDef = visit_scope

    def is_text_variable(self, node: ast.AST) -> bool:
        """Whether an expression gives the text of a tsast object only through a local variable that holds it."""
        return is_text_of_tsast(node, frozenset(self.text_variables[-1])) and not is_text_of_tsast(node)

    def is_cpp_name(self, name: str, table: list[set[str]]) -> bool:
        """Tell if a variable of the scope, or of the module, is in one table of the C++ reads."""
        return name in table[-1] or name in table[0]

    def names_cpp(self, node: ast.AST) -> bool:
        """Tell if an expression names a C++ file: a string constant with a C++ suffix, or a variable that names one."""
        return any(
            (isinstance(part, ast.Constant) and isinstance(part.value, str) and part.value.endswith(CPP_SUFFIXES))
            or (isinstance(part, ast.Name) and self.is_cpp_name(part.id, self.cpp_paths))
            for part in ast.walk(node)
        )

    def is_source_text(self, node: ast.AST) -> bool:
        """Tell if the value of an expression is the text of a C++ file, or a text that the expression makes from it.

        A read_text() or a read_bytes() of a path that names a C++ file gives that text.  A read() of an open C++
        file and a variable that holds such a text give it too.  A slice of the text, a string method of TEXT_RESULTS
        on it, and a concatenation or a conditional expression with it give a text too.  Complexity: linear in the
        number of nodes.
        """
        if isinstance(node, ast.Name):
            return self.is_cpp_name(node.id, self.source_texts)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            method, receiver = node.func.attr, node.func.value
            if method in SOURCE_READS:
                return self.names_cpp(receiver)
            if method in HANDLE_READS:
                return isinstance(receiver, ast.Name) and self.is_cpp_name(receiver.id, self.cpp_handles)
            return method in TEXT_RESULTS and self.is_source_text(receiver)
        if isinstance(node, (ast.Subscript, ast.NamedExpr)):
            return self.is_source_text(node.value)
        if isinstance(node, ast.BinOp):
            return self.is_source_text(node.left) or self.is_source_text(node.right)
        if isinstance(node, ast.IfExp):
            return self.is_source_text(node.body) or self.is_source_text(node.orelse)
        if isinstance(node, ast.BoolOp):
            return any(self.is_source_text(value) for value in node.values)
        return False

    def is_source_lines(self, node: ast.AST) -> bool:
        """Tell if an iterable gives the lines or the parts of the text of a C++ file, or that text itself."""
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr in LINE_SPLITS:
            return self.is_source_text(node.func.value)
        return self.is_source_text(node)

    def bind(self, target: ast.AST, table: list[set[str]], holds: bool) -> None:
        """Add each name of a target to one table of the scope when `holds` is set, and remove it when it is not set."""
        for part in ast.walk(target):
            if isinstance(part, ast.Name):
                if holds:
                    table[-1].add(part.id)
                else:
                    table[-1].discard(part.id)

    def visit_Assign(self, node: ast.Assign) -> None:
        """Record a local variable that holds the text of a tsast object, a C++ path or a C++ text, and forget others."""
        holds_text = is_text_of_tsast(node.value, frozenset(self.text_variables[-1]))
        holds_source = self.is_source_text(node.value)
        holds_path = not holds_source and self.names_cpp(node.value)
        for target in node.targets:
            if isinstance(target, ast.Name):
                if holds_text:
                    self.text_variables[-1].add(target.id)
                else:
                    self.text_variables[-1].discard(target.id)
            self.bind(target, self.source_texts, holds_source)
            self.bind(target, self.cpp_paths, holds_path)
        self.generic_visit(node)

    def visit_loop(self, node: ast.For | ast.comprehension) -> None:
        """Record a loop variable that names a C++ file, or that holds a line or a part of the text of one."""
        holds_source = self.is_source_lines(node.iter)
        self.bind(node.target, self.source_texts, holds_source)
        self.bind(node.target, self.cpp_paths, not holds_source and self.names_cpp(node.iter))
        self.generic_visit(node)

    visit_For = visit_loop
    visit_comprehension = visit_loop

    def visit_comprehension_scope(self, node: ast.ListComp | ast.SetComp | ast.GeneratorExp | ast.DictComp) -> None:
        """Walk the generators of a comprehension before its element, because the generators give the names."""
        for generator in node.generators:
            self.visit(generator)
        for field in ("elt", "key", "value"):
            part = getattr(node, field, None)
            if part is not None:
                self.visit(part)

    visit_ListComp = visit_comprehension_scope
    visit_SetComp = visit_comprehension_scope
    visit_GeneratorExp = visit_comprehension_scope
    visit_DictComp = visit_comprehension_scope

    def visit_With(self, node: ast.With) -> None:
        """Record the variable of an open() of a C++ file, or of a path that names one."""
        for item in node.items:
            context = item.context_expr
            opens = isinstance(context, ast.Call) and (
                (isinstance(context.func, ast.Name) and context.func.id == "open" and bool(context.args)
                 and self.names_cpp(context.args[0]))
                or (isinstance(context.func, ast.Attribute) and context.func.attr == "open"
                    and self.names_cpp(context.func.value)))
            if item.optional_vars is not None:
                self.bind(item.optional_vars, self.cpp_handles, opens)
        self.generic_visit(node)

    def visit_Import(self, node: ast.Import) -> None:
        """Refuse an import of the lexer."""
        for alias in node.names:
            if alias.name.rsplit(".", 1)[-1] == LEXER:
                self.add(node, "lexer", f"the file imports {LEXER}.  Read the code from tsast nodes", LEXER)
        self.generic_visit(node)

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        """Refuse an import of the lexer."""
        names = [node.module or ""] + [f"{node.module}.{alias.name}" for alias in node.names]
        if any(name.rsplit(".", 1)[-1] == LEXER for name in names if name):
            self.add(node, "lexer", f"the file imports {LEXER}.  Read the code from tsast nodes", LEXER)
        self.generic_visit(node)

    def visit_Expr(self, node: ast.Expr) -> None:
        """Remember a call that is a statement, because the walk uses no value of it."""
        if isinstance(node.value, ast.Call):
            self.discarded.add(id(node.value))
        self.generic_visit(node)

    def visit_Call(self, node: ast.Call) -> None:
        """Refuse a read of tsast text or of GCC text, a regex call outside a reader, and a use of a git output."""
        func = node.func
        arguments = [*node.args, *(keyword.value for keyword in node.keywords)]
        dotted_name = python_reader.dotted(func, self.bindings)
        if self.path not in GIT_READERS and id(node) not in self.discarded and dotted_name in GIT_RUNS:
            self.add(node, "git-output", f"a use of the value of {dotted_name}() outside {GIT_MODULE} reads the "
                     f"output of git as text.  Add a function to {GIT_MODULE} that runs the command and gives a typed "
                     f"value, and call that function")
        if (not self.is_reader and dotted_name in REGEX_CALLS
                and not (len(node.args) >= 2 and is_prose_read(node.args[1]))):
            self.add(node, "regex-call", f"a call of {dotted_name}() outside a reader.  Read the input through its "
                     f"reader in assay/readers/, or read prose through tsast.prose_text()")
        if not self.is_reader and isinstance(func, ast.Attribute):
            method, receiver = func.attr, func.value
            if method in RE_FUNCTIONS and any(is_text_of_tsast(argument) for argument in arguments):
                self.add(node, "node-text", f"a regex {method}() over the text of a tsast node.  Read the answer from "
                         f"the children of the node, or read prose through tsast.prose_text()")
            elif method in STRING_METHODS and is_text_of_tsast(receiver):
                self.add(node, "node-text", f"a string {method}() on the text of a tsast node.  Read the answer from "
                         f"the children of the node, leaf_name() or qualified_parts()")
            elif method in RE_FUNCTIONS and any(self.is_text_variable(argument) for argument in arguments):
                self.add(node, "node-text-variable", f"a regex {method}() over a variable that holds the text of a "
                         f"tsast node.  Read the answer from the children of the node")
            elif method in STRING_METHODS and self.is_text_variable(receiver):
                self.add(node, "node-text-variable", f"a string {method}() on a variable that holds the text of a "
                         f"tsast node.  Read the answer from the children of the node")
            if (method in STRING_METHODS and self.is_source_text(receiver)) or (
                    method in RE_FUNCTIONS and any(self.is_source_text(argument) for argument in arguments)):
                self.add(node, "source-text", f"a {method}() on the text of a C++ file that the module reads as text.  "
                         f"Read the file through assay.readers.tsast, and read the answer from the parse tree")
            heads = [argument.value for argument in node.args if is_diagnostic_head(argument)]
            if method in STRING_METHODS and heads:
                self.add(node, "diagnostic-text", f"a string {method}() with the head {heads[0]!r} of a diagnostic of "
                         f"GCC reads the text output of the compiler.  Compile with {sarif.SARIF_FLAG}PATH, and read "
                         f"the kind and the message of each record of the log with assay.readers.sarif")
        self.generic_visit(node)

    def visit_Compare(self, node: ast.Compare) -> None:
        """Refuse an `in` test over tsast text or over the text of a C++ file, and an `in` test with a head of GCC."""
        if not self.is_reader:
            lefts = [node.left, *node.comparators[:-1]]
            for left, operator, right in zip(lefts, node.ops, node.comparators):
                if isinstance(operator, (ast.In, ast.NotIn)) and (
                        is_diagnostic_head(left) or holds_diagnostic_head(right)):
                    self.add(node, "diagnostic-text", "an `in` test with the head of a diagnostic of GCC reads the "
                             f"text output of the compiler.  Compile with {sarif.SARIF_FLAG}PATH, and read the kind "
                             "and the message of each record of the log with assay.readers.sarif")
                if isinstance(operator, (ast.In, ast.NotIn)) and is_text_of_tsast(right):
                    self.add(node, "node-text", "an `in` test over the text of a tsast node.  Read the answer from "
                             "the children of the node")
                elif isinstance(operator, (ast.In, ast.NotIn)) and self.is_text_variable(right):
                    self.add(node, "node-text-variable", "an `in` test over a variable that holds the text of a tsast "
                             "node.  Read the answer from the children of the node")
                if isinstance(operator, (ast.In, ast.NotIn)) and self.is_source_text(right):
                    self.add(node, "source-text", "an `in` test over the text of a C++ file that the module reads as "
                             "text.  Read the file through assay.readers.tsast, and read the answer from the parse tree")
        self.generic_visit(node)

    def visit_sequence(self, node: ast.List | ast.Tuple) -> None:
        """Refuse the words of a git command outside assay.core.git, and the words that run the help output."""
        if self.path != GIT_IMPLEMENTATION and holds_git_argv(node):
            self.add(node, "git-command", "a git command outside assay.core.git.  Call a function of assay.core.git, "
                     "which runs git through assay.core.proc with a time limit and with no repository variable")
        if not self.is_reader and self.path != SELF and holds_words(node, HELP_WORDS):
            self.add(node, "format-read", f"the words {' '.join(HELP_WORDS)} run the help output of the build tool, "
                     f"which has no format.  Read the targets from the code model of {HELP_READER}",
                     " ".join(HELP_WORDS))
        self.generic_visit(node)

    visit_List = visit_sequence
    visit_Tuple = visit_sequence

    def visit_Constant(self, node: ast.Constant) -> None:
        """Refuse a constant that names a structured file or a tool, or that holds a magic.

        The reader of the format and this module can hold such a constant.
        """
        if self.is_reader or self.path == SELF or id(node) in self.docstrings:
            return
        if isinstance(node.value, str):
            for name, reader in FORMATS.items():
                if node.value == name or node.value.endswith(f"/{name}"):
                    self.add(node, "format-read", f"the constant names {name} outside its reader {reader}.  Read "
                             f"the file through its reader", name)
        elif isinstance(node.value, bytes):
            for magic, (entity, reader) in MAGICS.items():
                if node.value.startswith(magic):
                    self.add(node, "format-read", f"the constant holds {entity} outside its reader {reader}.  Read "
                             f"the file through its reader", entity)


def git_aliases(tree: ast.Module) -> set[str]:
    """Return each name that a module binds to assay.core.git."""
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == "assay.core" and node.level == 0:
            names.update(alias.asname or alias.name for alias in node.names if alias.name == "git")
        elif isinstance(node, ast.Import):
            names.update(alias.asname for alias in node.names if alias.name == GIT_MODULE and alias.asname)
    return names


def python_git_violation(tree: ast.Module) -> str | None:
    """Return why a Python file writes to a repository that it has not isolated, or None.

    The verb counts when it is a string constant of the module and the module
    builds a git argv (a list or a tuple literal whose first element is "git"),
    so a verb that goes through a wrapper counts too.  Complexity: linear in
    the size of the syntax tree.
    """
    def is_git_argv(node: ast.AST) -> bool:
        return (isinstance(node, (ast.List, ast.Tuple)) and bool(node.elts)
                and isinstance(node.elts[0], ast.Constant) and node.elts[0].value == "git")

    def verbs_in(nodes: list[ast.expr]) -> set[str]:
        return {node.value for node in nodes if isinstance(node, ast.Constant) and node.value in MUTATING}

    argvs = [node for node in ast.walk(tree) if is_git_argv(node)]
    verbs = set().union(*(verbs_in(argv.elts) for argv in argvs))  # type: ignore[attr-defined]
    wrappers = {func.name for func in ast.walk(tree) if isinstance(func, (ast.FunctionDef, ast.AsyncFunctionDef))
                and any(is_git_argv(node) and any(isinstance(element, ast.Starred) for element in node.elts)  # type: ignore[attr-defined]
                        for node in ast.walk(func))}
    verbs |= set().union(*(verbs_in(call.args) for call in ast.walk(tree) if isinstance(call, ast.Call)
                           and isinstance(call.func, ast.Name) and call.func.id in wrappers))
    if not verbs:
        return None
    aliases = git_aliases(tree)
    isolated = any(isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "init"
                   and isinstance(node.func.value, ast.Name) and node.func.value.id in aliases
                   for node in ast.walk(tree))
    if isolated:
        return None
    return (f"it runs git {', '.join(sorted(verbs))} and never calls init() of {GIT_MODULE}, so an exported GIT_DIR "
            f"sends the write to the repository of the caller")


def python_findings(path: str, file: Path) -> list[check_report.Finding]:
    """Give the findings of one Python file, which the reader of Python reads and parses."""
    try:
        source = python_reader.read(file, path)
    except python_reader.PythonError as problem:
        return [check_report.Finding("error", path, problem.line, RULE, f"the file does not parse, and its reads are "
                                     f"unknown: {problem}", "parse", path)]
    except UnicodeDecodeError as problem:
        return [check_report.Finding("error", path, 0, RULE, f"the file is not UTF-8, and its reads are unknown: "
                                     f"{problem}", "parse", path)]
    tree = source.tree
    visitor = PythonReads(path, path.startswith(READERS), docstring_nodes(tree), python_reader.bindings(source))
    visitor.visit(tree)
    found = visitor.found
    if path != GIT_IMPLEMENTATION:
        why = python_git_violation(tree)
        if why is not None:
            found.append(check_report.Finding("error", path, 0, RULE, f"{why}.  Call init() of {GIT_MODULE} on the "
                                              f"throwaway repository first", "git-isolation", "git"))
    return found


def is_python(path: str) -> bool:
    """Whether a file of the scope is a Python file: a .py file or a master of utils/bin/."""
    return path.endswith(".py") or (path.startswith(MASTERS) and "." not in PurePosixPath(path).name)


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each read of the scope that is not structured, and each git write that does not isolate its repository."""
    files = [path for path in context.tracked() if not path.startswith(f"{PLANTS}/")]
    found: list[check_report.Finding] = []
    for path in files:
        if PurePosixPath(path).name == f"{LEXER}.py":
            found.append(check_report.Finding("error", path, 0, RULE, f"the file {LEXER}.py blanks comments so that a "
                                              f"regex can read code, and the parse replaces it.  Delete it", "lexer",
                                              LEXER))
        if is_python(path):
            found += python_findings(path, context.root / path)
    shells = [path for path in files if path.endswith(".sh")]
    try:
        scripts = shell_reader.read(context.root, shells)
    except shell_reader.AstGrepMissing as problem:
        raise NotApplicable(str(problem)) from None
    for path in shells:
        found += shell_findings(path, scripts[path])
    return found
