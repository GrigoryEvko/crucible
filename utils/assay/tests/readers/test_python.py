"""The test of the reader of Python source (assay.readers.python): the parse, the scopes and the imports.

`guard selftest tests.readers.test_python` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.
"""

import ast
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import testing
from assay.readers import python as python_reader

SHOWN = "planted.py"
SOURCE = '''"""The module docstring."""

import os
import subprocess as sp
from pathlib import Path as P
from . import sibling
import a.b.c


# A comment of the module.
class Holder:
    """The class docstring."""

    def method(self) -> None:
        """The method docstring."""
        value = 1  # A comment at the end of a line.

        def inner() -> int:
            return value

        return None


def function() -> str:
    sp.run(["true"])
    return os.path.join("a", "b")
'''


# A source with code that runs at the import: an assignment, a decorator and a default of a function.
AT_IMPORT = """from assay.core import git

LISTED = git.tracked_files


@decorate
def planted(value=compute()):
    return helper(value)
"""


def parsed() -> python_reader.Source:
    """Parse the planted source."""
    return python_reader.parse(SOURCE, SHOWN)


def refuses(action: Callable[[], object]) -> python_reader.PythonError | None:
    """Return the PythonError that an action raises, or None."""
    try:
        action()
    except python_reader.PythonError as error:
        return error
    return None


def source_cases(expect: testing.Expect) -> None:
    """Check the parse, the read of a file and the error of a text that does not parse."""
    source = parsed()
    expect(
        "parse() keeps the path as shown and the text, and gives a module",
        source.path == SHOWN and source.text == SOURCE and isinstance(source.tree, ast.Module),
    )
    error = refuses(lambda: python_reader.parse("def broken(:\n", SHOWN))
    expect(
        "a text that does not parse gives PythonError with the path and the line",
        error is not None and error.path == SHOWN and error.line == 1 and str(error).startswith(f"{SHOWN}:1: "),
    )
    with tempfile.TemporaryDirectory(prefix="test-python-") as scratch:
        path = Path(scratch) / "file.py"
        path.write_text(SOURCE, encoding="utf-8")
        expect("read() reads and parses a file", python_reader.read(path, SHOWN).text == SOURCE)


def scope_cases(expect: testing.Expect) -> None:
    """Check the scopes: the full names and the holder of each line."""
    source = parsed()
    names = [name for name, _ in python_reader.scope_nodes(source.tree)]
    expect(
        "scope_nodes() gives each scope with its full name, the outer first",
        names == ["Holder", "Holder.method", "Holder.method.inner", "function"],
    )
    table = python_reader.holders(source)
    expect(
        "holders() gives the innermost holder of each line",
        python_reader.holder(table, 1) == "<module>"
        and python_reader.holder(table, 12) == "Holder"
        and python_reader.holder(table, 16) == "Holder.method"
        and python_reader.holder(table, 19) == "Holder.method.inner"
        and python_reader.holder(table, 20) == "Holder.method"
        and python_reader.holder(table, 26) == "function",
    )
    expect(
        "a line that is not in the text has the holder <module>",
        python_reader.holder(table, 0) == "<module>" and python_reader.holder(table, 999) == "<module>",
    )
    docs = {name: python_reader.docstring_of(node) for name, node in python_reader.scope_nodes(source.tree)}
    module_doc = python_reader.docstring_of(source.tree)
    expect(
        "docstring_of() gives the docstring constant of the module, a class and a function, and None with no docstring",
        module_doc is not None
        and module_doc.value == "The module docstring."
        and (doc := docs["Holder.method"]) is not None
        and doc.value == "The method docstring."
        and docs["Holder.method.inner"] is None
        and docs["function"] is None,
    )


def import_cases(expect: testing.Expect) -> None:
    """Check the imports, the import table and the dotted names."""
    source = parsed()
    expect(
        "imports() gives each module, sorted, with the module of each imported name and the relative points",
        python_reader.imports(source) == (".", ".sibling", "a.b.c", "os", "pathlib", "pathlib.Path", "subprocess"),
    )
    table = python_reader.bindings(source)
    expect(
        "bindings() gives the target of each name that an import makes",
        table.target("os") == "os"
        and table.target("sp") == "subprocess"
        and table.target("P") == "pathlib.Path"
        and table.target("a") == "a"
        and table.target("sibling") is None,
    )
    expect(
        "is_module() is True only for a name with the same target",
        table.is_module("os") and not table.is_module("sp") and not table.is_module("P"),
    )
    call = ast.parse("sp.run(x)", mode="eval").body
    attribute = ast.parse("os.path.join", mode="eval").body
    expect(
        "dotted() replaces the first name by its target and keeps the attributes",
        isinstance(call, ast.Call)
        and python_reader.dotted(call.func, table) == "subprocess.run"
        and python_reader.dotted(attribute, table) == "os.path.join",
    )
    expect(
        "dotted() keeps a name that no import makes, and gives None for an expression that is not a name",
        python_reader.dotted(ast.parse("print", mode="eval").body, table) == "print"
        and python_reader.dotted(ast.parse("f().x", mode="eval").body, table) is None,
    )
    expect(
        "references() gives the longest chain of names that starts with a name of an import, and no local name",
        python_reader.references(source) == ("os.path.join", "subprocess.run"),
    )
    shaped = python_reader.shape(source)
    expect(
        "shape() gives each top-level function and class with the references and the names of its code",
        set(shaped.definitions) == {"Holder", "function"}
        and shaped.definitions["function"].references == ("os.path.join", "subprocess.run")
        and "value" in shaped.definitions["Holder"].names
        and shaped.module.references == ()
        and shaped.imports.target("sp") == "subprocess",
    )
    at_import = python_reader.shape(python_reader.parse(AT_IMPORT, SHOWN))
    expect(
        "the code that runs at the import holds each other statement, and the decorators and defaults of a function",
        at_import.module.references == ("assay.core.git.tracked_files",)
        and {"compute", "decorate"} <= set(at_import.module.names)
        and "helper" not in at_import.module.names
        and {"compute", "decorate", "helper"} <= set(at_import.definitions["planted"].names),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (source_cases, scope_cases, import_cases))
