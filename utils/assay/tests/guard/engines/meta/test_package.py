"""The test of the package scope of the meta rules (assay.guard.engines.meta.package): the scope, the names and the
read.

`guard selftest tests.guard.engines.meta.test_package` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import copy_tree

# The files of a planted tree: two modules, a package, a master and a file that does not parse.  The other files are
# not in the scope.
FILES = {
    "utils/assay/core/rows.py": "def read() -> None:\n    pass\n",
    "utils/assay/core/__init__.py": '"""The package."""\n',
    "utils/assay/broken.py": "def broken(:\n",
    "utils/bin/guard": "import sys\n",
    "utils/bin/sub/thing": "x = 1\n",
    "utils/assay/data.txt": "not Python\n",
    "utils/plants/rule/plant-one/utils/assay/x.py": "x = 1\n",
    "test/harness/check.py": "x = 1\n",
}


def scope_cases(expect: testing.Expect) -> None:
    """Check the scope and the dotted names."""
    expect(
        "a module of the package and a master are in the scope",
        all(package_scope.is_in_scope(path) for path in ("utils/assay/core/rows.py", "utils/bin/guard")),
    )
    expect(
        "a plant, a file of a subdirectory of utils/bin/, a data file and a Python file of test/ are not",
        not any(
            package_scope.is_in_scope(path)
            for path in (
                "utils/plants/rule/plant-one/utils/assay/x.py",
                "utils/bin/sub/thing",
                "utils/assay/data.txt",
                "test/harness/check.py",
            )
        ),
    )
    expect(
        "dotted_name() gives the name of a module, of a package and of a master",
        package_scope.dotted_name("utils/assay/core/rows.py") == "assay.core.rows"
        and package_scope.dotted_name("utils/assay/core/__init__.py") == "assay.core"
        and package_scope.dotted_name("utils/bin/guard") == "bin.guard",
    )


def one_finding(item: package_scope.PythonFile) -> list[check_report.Finding]:
    """Give one planted finding for a file, with the name of the file as its entity."""
    return [check_report.Finding("error", item.path, 1, "planted-rule", "a planted finding", "kind", item.name)]


def read_cases(expect: testing.Expect) -> None:
    """Check the read of a planted tree: the files, the problems and the findings of each file."""
    with tempfile.TemporaryDirectory(prefix="test-package-") as scratch:
        plant = Path(scratch) / "plant"
        for path, text in FILES.items():
            (plant / path).parent.mkdir(parents=True, exist_ok=True)
            (plant / path).write_text(text, encoding="utf-8")
        context = copy_tree(plant, Path(scratch) / "tree")
        package = package_scope.read_package(context)
        expect(
            "read_package() reads each file of the scope that parses, sorted by path",
            [item.path for item in package.files]
            == ["utils/assay/core/__init__.py", "utils/assay/core/rows.py", "utils/bin/guard"],
        )
        rows = package.files[1]
        expect(
            "a file gives its name, its master flag and the holder of a line",
            rows.name == "assay.core.rows"
            and not rows.is_master
            and package.files[2].is_master
            and rows.holder(2) == "read"
            and rows.holder(9) == "<module>",
        )
        expect(
            "a file that does not parse is a problem with its path and its line",
            [(problem.path, problem.line) for problem in package.problems] == [("utils/assay/broken.py", 1)],
        )
        parse_errors = package_scope.parse_findings("planted-rule", package)
        expect(
            "parse_findings() gives one error of the kind parse for each problem",
            [(item.kind, item.entity, item.level) for item in parse_errors]
            == [("parse", "utils/assay/broken.py", "error")],
        )
        found = package_scope.each_file(context, "planted-rule", one_finding)
        expect(
            "each_file() gives the parse errors and the findings of each file that parses",
            [item.entity for item in found] == ["utils/assay/broken.py", "assay.core", "assay.core.rows", "bin.guard"],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (scope_cases, read_cases))
