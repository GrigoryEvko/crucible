"""The Python of the utils system in a tree: the modules of the package and the masters, each read one time.

THE SCOPE
    The rules of the engineering bar read the modules of the package, the
    files utils/assay/**/*.py, and the masters of utils/bin/.  The plants of
    utils/plants/ are not in the scope.  The list holds the files of git and
    the new files of the tree (MetaContext.tracked), sorted.

THE NAMES
    The name of a file is its dotted path from utils/.  The module
    utils/assay/core/rows.py has the name assay.core.rows, the file
    utils/assay/core/__init__.py has the name assay.core, and the master
    utils/bin/guard has the name bin.guard.  A job row of the registry names
    its owners with these names.

THE READ
    read_package() reads and parses each file one time.  A file that does not
    parse is a problem of the package.  A rule cannot read such a file, and
    parse_findings() gives the error of the kind parse for each problem.

Complexity: linear in the length of the files of the scope.
"""

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

PACKAGE_PREFIX = "utils/assay/"
MASTERS_PREFIX = "utils/bin/"
INIT = "__init__.py"


@dataclass(frozen=True, slots=True)
class PythonFile:
    """One file of the scope: its path from the root, its dotted name, its parsed source and the holder of each line."""

    path: str
    name: str
    parsed: python_reader.Source
    holders: tuple[str, ...]

    @property
    def is_master(self) -> bool:
        """True when the file is a master of utils/bin/."""
        return self.path.startswith(MASTERS_PREFIX)

    def holder(self, line: int) -> str:
        """Return the full name of the function or the class that holds a line, or <module>."""
        return python_reader.holder(self.holders, line)


@dataclass(frozen=True, slots=True)
class Package:
    """The files of the scope that parse, and a problem for each file that does not parse."""

    files: tuple[PythonFile, ...]
    problems: tuple[python_reader.PythonError, ...]


def is_in_scope(path: str) -> bool:
    """Return True when a path of the tree is a module of the package or a master."""
    if path.startswith(PACKAGE_PREFIX):
        return path.endswith(".py")
    return path.startswith(MASTERS_PREFIX) and "/" not in path[len(MASTERS_PREFIX) :]


def dotted_name(path: str) -> str:
    """Return the dotted name of a file of the scope (THE NAMES)."""
    pure = PurePosixPath(path.removeprefix("utils/"))
    parts = pure.parent.parts if pure.name == INIT else (*pure.parent.parts, pure.name.removesuffix(".py"))
    return ".".join(parts)


def read_package(context: MetaContext) -> Package:
    """Read and parse each file of the scope of a tree, sorted by path.

    Complexity: linear in the length of the files.
    """
    files: list[PythonFile] = []
    problems: list[python_reader.PythonError] = []
    for path in context.tracked():
        if not is_in_scope(path):
            continue
        try:
            source = python_reader.read(context.root / path, path)
        except python_reader.PythonError as problem:
            problems.append(problem)
            continue
        files.append(PythonFile(path, dotted_name(path), source, python_reader.holders(source)))
    return Package(tuple(files), tuple(problems))


def parse_findings(rule: str, package: Package) -> list[check_report.Finding]:
    """Return the error of the kind parse for each file of the package that does not parse."""
    return [
        check_report.Finding(
            "error",
            problem.path,
            problem.line,
            rule,
            f"the file does not parse, and the rule cannot read it: {problem}",
            "parse",
            problem.path,
        )
        for problem in package.problems
    ]


def each_file(
    context: MetaContext, rule: str, file_findings: Callable[[PythonFile], list[check_report.Finding]]
) -> list[check_report.Finding]:
    """Read the package of a tree, and return the parse errors and the findings of a rule for each file.

    Complexity: linear in the length of the files, and the cost of the rule for each file.
    """
    package = read_package(context)
    found = parse_findings(rule, package)
    for item in package.files:
        found += file_findings(item)
    return found
