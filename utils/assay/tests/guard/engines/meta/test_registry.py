"""The test of the rule registry (assay.guard.engines.meta.registry) and of the import closure of its part tests.

`guard selftest tests.guard.engines.meta.test_registry` runs the test.  The
closure cases plant modules of the package in a throwaway tree.
reads_trees() then tells if the import closure of a module holds the reader
of the parse trees.  The rules of one call share the tables of the answers
and of the imports.  The call then parses each module one time.  The part
cases read the name of the part of a plant, the refused kinds, and the parts
of a run with no build directory.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import testing
from assay.guard.engines.meta import (
    registry,
    registry_kinds,
    registry_ledgers,
    registry_modules,
    registry_shape,
    registry_tests,
)
from assay.guard.engines.meta.runner import MetaContext

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
    with tempfile.TemporaryDirectory(prefix="registry-") as scratch:
        root = Path(scratch)
        plant(root)
        answers: dict[str, bool] = {}
        imports: dict[str, frozenset[str]] = {}
        expect(
            "a module that imports a module with an import of the reader in a function reads the parse trees",
            registry_tests.reads_trees(root, "assay.first", answers, imports),
        )
        expect(
            "a module with no import of the package does not read the parse trees",
            not registry_tests.reads_trees(root, "assay.alone", answers, imports),
        )
        expect(
            "the table of the imports holds the imports of the package of each module on the path to the reader",
            {"assay.first", "assay.second", "assay.alone"} <= set(imports)
            and imports["assay.first"] == frozenset({"assay", "assay.second"})
            and imports["assay.alone"] == frozenset(),
        )
        planted = {"assay.ghost": frozenset({registry_tests.TREE_READER})}
        expect(
            "a module with an entry in the table of the imports is not parsed again, also with no file",
            registry_tests.reads_trees(root, "assay.ghost", {}, planted),
        )


def part_cases(expect: testing.Expect) -> None:
    """The name of a plant selects its part, and the three parts with no ledger give only refused kinds."""
    names = {
        "plant-shape-bad-doc": "shape",
        "control-tests-registered": "tests",
        "warning-layout-planted": "layout",
        "skip-kinds-planted": "kinds",
    }
    expect(
        "the word after the prefix of a plant names its part",
        all((found := registry.part_of(name)) is not None and found.name == part for name, part in names.items()),
    )
    expect(
        "a name with no prefix, or with no part after the prefix, names no part",
        registry.part_of("shape-bad-doc") is None and registry.part_of("plant-bad-doc") is None,
    )
    expect(
        "the refused kinds are the kinds of the parts shape, kinds and ledgers, and the three holder kinds",
        registry.REFUSED_KINDS
        == registry_shape.KINDS | registry_kinds.KINDS | registry_ledgers.KINDS | registry_modules.HOLDER_KINDS
        and {"row", "doc", "missing-kind", "ledger-schema", "unrun-holder"} <= registry.REFUSED_KINDS
        and not {"no-test", "no-plant", "no-row", "legacy"} & registry.REFUSED_KINDS,
    )
    expect(
        "each part has a name of its own",
        len(registry.PARTS_BY_NAME) == len(registry.PARTS),
    )
    with tempfile.TemporaryDirectory(prefix="registry-") as scratch:
        context = MetaContext(Path(scratch))
        unread = registry.unread_parts(context)
        expect(
            "with no build directory, a write cannot do the parts tests and kinds",
            [item.split(" ", 1)[0] for item in unread[:2]] == ["tests", "kinds"],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (closure_cases, part_cases))
