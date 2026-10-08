"""The reader of Python source: the syntax tree, the comments, the docstrings, the scopes and the imports of one file.

The table of the readers of the utils system names this module as the one
reader of Python source.  A module of the package reads a Python file through
this module, and reads no Python file as text.

THE SOURCE
    read() reads one file and parses it one time.  parse() parses a text.  A
    text that does not parse gives PythonError with the path and the line.

THE COMMENTS
    comments() gives each comment of a source with its line and its column,
    from the tokens of the tokenize module.  The body of a comment is its
    text with no "#".

THE DOCSTRINGS
    docstrings() gives each docstring of a module, of its classes and of its
    functions.  A docstring has the line of its first character, its holder
    and its body, the text of the docstring.

THE SCOPES
    The full name of a function or a class holds the names of the classes and
    the functions that hold it, such as Registry.measured.  holders() gives a
    table with the full name of the innermost function or class that holds
    each line, and a line of no function or class has the name <module>.  A
    rule gives that name as the entity of a finding, because the name holds
    no line number.

THE IMPORTS
    imports() gives the dotted name of each module that a source imports, also
    in a function.  A relative import keeps its points.  bindings() gives the
    import table of a source: the dotted target of each name that an import
    makes.  dotted() gives the dotted name of a name or of a chain of
    attributes through that table, such as subprocess.run for `sp.run` after
    `import subprocess as sp`.

`guard selftest tests.readers.test_python` does a test of the module.
"""

import ast
import io
import tokenize
from collections.abc import Iterator, Mapping
from dataclasses import dataclass
from pathlib import Path

MODULE_SCOPE = "<module>"
# The nodes that hold a docstring and a scope of their own.
type ScopeNode = ast.FunctionDef | ast.AsyncFunctionDef | ast.ClassDef


class PythonError(ValueError):
    """A Python source does not parse.  The arguments are the path, the line and the message."""

    @property
    def path(self) -> str:
        """The path of the source, as the caller shows it."""
        return str(self.args[0])

    @property
    def line(self) -> int:
        """The line of the problem, or 0."""
        return int(self.args[1])

    def __str__(self) -> str:
        """Return the path, the line and the message."""
        return f"{self.args[0]}:{self.args[1]}: {self.args[2]}"


@dataclass(frozen=True, slots=True)
class Source:
    """One Python file: its path as the caller shows it, its text and its syntax tree."""

    path: str
    text: str
    tree: ast.Module


@dataclass(frozen=True, slots=True)
class Comment:
    """One comment: its line, its column and its body, the text with no "#"."""

    line: int
    column: int
    body: str


@dataclass(frozen=True, slots=True)
class Docstring:
    """One docstring: the line of its first character, the full name of its holder and its body."""

    line: int
    holder: str
    body: str


def parse(text: str, shown: str) -> Source:
    """Parse the text of a Python file.

    Raises:
        PythonError: If the text does not parse
    """
    try:
        tree = ast.parse(text, shown)
    except SyntaxError as problem:
        raise PythonError(shown, problem.lineno or 0, problem.msg) from None
    return Source(shown, text, tree)


def read(path: Path, shown: str) -> Source:
    """Read and parse one Python file.

    Raises:
        PythonError: If the file does not parse
        OSError: If the file cannot be read
    """
    return parse(path.read_text(encoding="utf-8"), shown)


def comments(source: Source) -> tuple[Comment, ...]:
    """Return each comment of a source, in the sequence of the text.

    Complexity: linear in the length of the text.

    Raises:
        PythonError: If the text does not give its tokens
    """
    found: list[Comment] = []
    try:
        for token in tokenize.generate_tokens(io.StringIO(source.text).readline):
            if token.type == tokenize.COMMENT:
                found.append(Comment(token.start[0], token.start[1], token.string[1:]))
    except (tokenize.TokenError, SyntaxError) as problem:
        raise PythonError(source.path, 0, f"the tokens of the file cannot be read: {problem}") from None
    return tuple(found)


def scope_nodes(node: ast.AST, prefix: str = "") -> Iterator[tuple[str, ScopeNode]]:
    """Give each function and each class in a node with its full name, in the sequence of the text.

    An outer scope comes before the scopes that it holds.  Complexity: linear
    in the number of the nodes of the tree.
    """
    for child in ast.iter_child_nodes(node):
        if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            name = f"{prefix}.{child.name}" if prefix else child.name
            yield name, child
            yield from scope_nodes(child, name)
        else:
            yield from scope_nodes(child, prefix)


def docstring_of(node: ast.Module | ScopeNode) -> ast.Constant | None:
    """Return the constant of the docstring of a module, a class or a function, or None."""
    first = node.body[0] if node.body else None
    if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant) and isinstance(first.value.value, str):
        return first.value
    return None


def docstrings(source: Source) -> tuple[Docstring, ...]:
    """Return each docstring of a source: of the module, then of each class and function, the outer first.

    Complexity: linear in the number of the nodes of the tree.
    """
    found: list[Docstring] = []
    module_doc = docstring_of(source.tree)
    if module_doc is not None:
        found.append(Docstring(module_doc.lineno, MODULE_SCOPE, str(module_doc.value)))
    for name, node in scope_nodes(source.tree):
        constant = docstring_of(node)
        if constant is not None:
            found.append(Docstring(constant.lineno, name, str(constant.value)))
    return tuple(found)


def holders(source: Source) -> tuple[str, ...]:
    """Return the name of the innermost function or class that holds each line, at the index LINE - 1.

    An inner span comes after its outer span, and its name then replaces the
    name of the outer span on its lines.  Complexity: linear in the lines of
    the spans, which is at most the lines of the text times the depth of
    nesting.
    """
    table = [MODULE_SCOPE] * (source.text.count("\n") + 1)
    for name, node in scope_nodes(source.tree):
        last = node.end_lineno if node.end_lineno is not None else node.lineno
        for index in range(node.lineno - 1, min(last, len(table))):
            table[index] = name
    return tuple(table)


def holder(table: tuple[str, ...], line: int) -> str:
    """Return the name of the holder of a line from the table of holders(), or <module> for a line that is not in the text."""
    return table[line - 1] if 1 <= line <= len(table) else MODULE_SCOPE


@dataclass(frozen=True, slots=True)
class Bindings:
    """The import table of a source: the dotted target of each name that an import makes (THE IMPORTS)."""

    targets: Mapping[str, str]

    def target(self, name: str) -> str | None:
        """Return the dotted target of a name, or None when no import makes the name."""
        return self.targets.get(name)

    def is_module(self, name: str) -> bool:
        """Return True when the target of a name is a module with the same name, as after `import os`."""
        return self.targets.get(name) == name


def bindings(source: Source) -> Bindings:
    """Return the import table of a source.

    `import a.b` gives the name a the target a, `import a.b as c` gives c the
    target a.b, and `from a import b as c` gives c the target a.b.  The table
    holds the imports of each scope of the source, and a subsequent import of
    a name replaces an earlier one.  A relative import makes no name.
    Complexity: linear in the number of the nodes of the tree.
    """
    targets: dict[str, str] = {}
    for node in ast.walk(source.tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                if alias.asname is not None:
                    targets[alias.asname] = alias.name
                else:
                    first = alias.name.split(".", 1)[0]
                    targets[first] = first
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module is not None:
            for alias in node.names:
                targets[alias.asname or alias.name] = f"{node.module}.{alias.name}"
    return Bindings(targets)


def dotted(node: ast.expr, table: Bindings) -> str | None:
    """Return the dotted name of a name or of a chain of attributes on a name, or None for each other expression.

    The first name becomes its target in the table of bindings().  A name that
    no import makes, such as a builtin, does not change.  Each attribute of
    the chain comes after it.  Complexity: linear in the length of the chain.
    """
    parts: list[str] = []
    current = node
    while isinstance(current, ast.Attribute):
        parts.append(current.attr)
        current = current.value
    if not isinstance(current, ast.Name):
        return None
    parts.append(table.target(current.id) or current.id)
    return ".".join(reversed(parts))


def imports(source: Source) -> tuple[str, ...]:
    """Return the dotted name of each module that a source imports, sorted, each one time.

    An import of a name from a module gives the module and the module with
    the name, because the name can be a module.  Complexity: linear in the
    number of the nodes of the tree, and O(m log m) for m names.
    """
    names: set[str] = set()
    for node in ast.walk(source.tree):
        if isinstance(node, ast.Import):
            names.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            base = "." * node.level + (node.module or "")
            names.add(base)
            names.update(f"{base}.{alias.name}" if node.module else f"{base}{alias.name}" for alias in node.names)
    return tuple(sorted(names))
