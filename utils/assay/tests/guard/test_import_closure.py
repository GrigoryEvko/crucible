"""The test of the import closure of the step meters: the second part of the rule import-closure.

THE CLOSURE
    CMake starts a hidden subcommand of a master for each compile, each link,
    each test and each fixture.  The modules that the import of its module
    adds are a cost of each step.  The test imports the module of each hidden
    subcommand in a new interpreter, with the options -I, -S and -B.  The
    modules of the package that the import adds must be the modules of the
    list CLOSURES.  Each other module must be a module of that list.  A
    module of the standard library counts by its first name, and a name that
    starts with "_" does not count.  The test then accepts a Python with fewer
    internal imports.

THE LIST
    A change that adds a module to a closure fails the test.  When the module
    is necessary, add it to the list in the same commit, and give the cost of
    the import in the commit.  The test also reads the table SUBCOMMANDS of
    each master, and each hidden subcommand must have a row in CLOSURES.

`guard selftest tests.guard.test_import_closure` runs the test.  Each case
writes one line.  Exit 0 when each case holds, else 1.
"""

import ast
import sys

from assay.core import paths, proc, testing
from assay.readers import python as python_reader

# The import in a new interpreter: the modules that the import of the module of the second argument adds.
PROBE = (
    "import sys\n"
    "before = set(sys.modules)\n"
    "sys.path.insert(0, sys.argv[1])\n"
    "__import__(sys.argv[2])\n"
    "print(' '.join(sorted(set(sys.modules) - before)))\n"
)
PACKAGE = "assay"
STEP_METER = frozenset({"abc", "errno", "genericpath", "os", "posixpath", "stat"})
PATH_TOOLS = frozenset(
    {
        "abc",
        "collections",
        "contextlib",
        "copyreg",
        "enum",
        "errno",
        "fcntl",
        "fnmatch",
        "functools",
        "genericpath",
        "glob",
        "grp",
        "io",
        "itertools",
        "keyword",
        "ntpath",
        "operator",
        "os",
        "pathlib",
        "posixpath",
        "pwd",
        "re",
        "reprlib",
        "stat",
        "types",
    }
)
PARSER_TOOLS = PATH_TOOLS | frozenset(
    {
        "annotationlib",
        "argparse",
        "ast",
        "bisect",
        "bz2",
        "compression",
        "copy",
        "dataclasses",
        "dis",
        "gettext",
        "importlib",
        "inspect",
        "lzma",
        "math",
        "opcode",
        "random",
        "shutil",
        "tempfile",
        "token",
        "tokenize",
        "warnings",
        "weakref",
        "zlib",
    }
)
# The closure of each hidden subcommand: the modules of the package, and the first names of the other modules.
CLOSURES: dict[str, tuple[frozenset[str], frozenset[str]]] = {
    "assay.loop.step": (
        frozenset({"assay", "assay.core", "assay.core.meter", "assay.loop", "assay.loop.step"}),
        STEP_METER,
    ),
    "assay.loop.meter_compiler": (
        frozenset({"assay", "assay.core", "assay.core.meter", "assay.loop", "assay.loop.meter_compiler"}),
        STEP_METER,
    ),
    "assay.loop.teststep": (
        frozenset({"assay", "assay.core", "assay.core.meter", "assay.loop", "assay.loop.teststep"}),
        STEP_METER,
    ),
    "assay.loop.fixture.store": (
        frozenset(
            {
                "assay",
                "assay.core",
                "assay.core.paths",
                "assay.loop",
                "assay.loop.fixture",
                "assay.loop.fixture.store",
                "assay.readers",
                "assay.readers.compdb",
            }
        ),
        PATH_TOOLS | frozenset({"bisect", "hashlib", "json", "zlib"}),
    ),
    "assay.loop.stamps": (
        frozenset(
            {"assay", "assay.core", "assay.core.paths", "assay.core.ruletable", "assay.loop", "assay.loop.stamps"}
        ),
        PARSER_TOOLS,
    ),
    "assay.core.cache": (frozenset({"assay", "assay.core", "assay.core.cache"}), PATH_TOOLS),
    "assay.core.report": (
        frozenset(
            {
                "assay",
                "assay.core",
                "assay.core.meter",
                "assay.core.paths",
                "assay.core.registry",
                "assay.core.report",
                "assay.core.rows",
            }
        ),
        PARSER_TOOLS | frozenset({"typing"}),
    ),
}


def hidden_modules() -> set[str]:
    """Return the module of each hidden subcommand of each master: a row of SUBCOMMANDS with no help line."""
    found: set[str] = set()
    for master in (paths.GUARD, paths.LOOP, paths.SETUP):
        source = python_reader.read(master, master.name)
        for node in ast.walk(source.tree):
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "SUBCOMMANDS" for target in node.targets
            ):
                rows = node.value.values if isinstance(node.value, ast.Dict) else []
                found.update(
                    str(row.elts[0].value)
                    for row in rows
                    if isinstance(row, ast.Tuple)
                    and isinstance(row.elts[0], ast.Constant)
                    and isinstance(row.elts[1], ast.Constant)
                    and row.elts[1].value is None
                )
    return found


def added_modules(module: str) -> tuple[frozenset[str], frozenset[str]]:
    """Import a module in a new interpreter, and return the modules of the package and the other first names.

    Raises:
        proc.ToolFailed: If the interpreter cannot start, runs longer than its time limit, or the import fails
    """
    completed = proc.require(
        proc.run(
            proc.Command(
                [sys.executable, "-I", "-S", "-B", "-c", PROBE, str(paths.REPO_ROOT / "utils"), module],
                timeout_s=proc.QUICK_S,
            )
        )
    )
    names = completed.out.split()
    package = frozenset(name for name in names if name.split(".")[0] == PACKAGE)
    others = frozenset(name.split(".")[0] for name in names if not name.startswith(("_", PACKAGE)))
    return package, others


def closure_cases(expect: testing.Expect) -> None:
    """Check that each hidden subcommand has a closure, and that each import adds only the modules of its closure."""
    expect(
        "each hidden subcommand of the masters has a row in CLOSURES, and each row is a hidden subcommand",
        hidden_modules() == set(CLOSURES),
    )
    for module, (package, others) in sorted(CLOSURES.items()):
        found_package, found_others = added_modules(module)
        extra = sorted(found_others - others)
        expect(
            f"the import of {module} adds the modules {sorted(package)} of the package, and no other module "
            f"than its list{f': not {extra}' if extra else ''}",
            found_package == package and not extra,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (closure_cases,))
