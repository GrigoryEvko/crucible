"""The test of the import closure of the rule test-registration (assay.guard.engines.meta.test_registration).

`guard selftest tests.guard.engines.meta.test_test_registration` runs the test.
Each case plants modules of the package in a throwaway tree.  reads_trees()
then tells if the import closure of a module holds the reader of the parse
trees.  The rules of one call share the tables of the answers and of the
imports.  The call then parses each module one time.  Exit 0 when each case
holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import testing
from assay.guard.engines.meta import test_registration

# The planted modules: each one imports the next, and the last one imports the reader of the parse trees.
MODULES = {
    "assay/first.py": "from assay import second\n",
    "assay/second.py": "def run():\n    from assay.readers import tsast\n    return tsast\n",
    "assay/alone.py": "import json\n",
}


def plant(root: Path) -> None:
    """Write the planted modules in the directory utils/ of a tree."""
    for name, text in MODULES.items():
        path = root / "utils" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def closure_cases(expect: testing.Expect) -> None:
    """The closure follows each import, also one in a function, and the call parses each module one time."""
    with tempfile.TemporaryDirectory(prefix="test-registration-") as scratch:
        root = Path(scratch)
        plant(root)
        answers: dict[str, bool] = {}
        imports: dict[str, frozenset[str]] = {}
        expect(
            "a module that imports a module with an import of the reader in a function reads the parse trees",
            test_registration.reads_trees(root, "assay.first", answers, imports),
        )
        expect(
            "a module with no import of the package does not read the parse trees",
            not test_registration.reads_trees(root, "assay.alone", answers, imports),
        )
        expect(
            "the table of the imports holds the imports of the package of each module on the path to the reader",
            {"assay.first", "assay.second", "assay.alone"} <= set(imports)
            and imports["assay.first"] == frozenset({"assay", "assay.second"})
            and imports["assay.alone"] == frozenset(),
        )
        planted = {"assay.ghost": frozenset({test_registration.TREE_READER})}
        expect(
            "a module with an entry in the table of the imports is not parsed again, also with no file",
            test_registration.reads_trees(root, "assay.ghost", {}, planted),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (closure_cases,))
