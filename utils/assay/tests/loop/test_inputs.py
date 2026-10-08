"""The test of the inputs of a test of the master guard (assay.loop.inputs).

`guard selftest tests.loop.test_inputs` runs the test.  Each case plants a
git work tree with a registry, modules of the package, plants, a rule table
and a test group.  Then it reads the inputs that the module gives for the
words of a test.  Exit 0 when each case holds, else 1.
"""

from pathlib import Path

from assay.core import git, testing
from assay.core import registry as rule_registry
from assay.loop import inputs
from assay.readers import compdb

# The rows of the planted registry.
RULES = (
    "rule alpha engine=meta scope=data/ ledger=none doc=XV | a planted rule | change nothing",
    "rule beta engine=tsast scope=tracked ledger=none inputs=extra/ doc=XV | a planted rule | change nothing",
    "rule gamma engine=build scope=builddir ledger=none doc=XV | a planted rule of the build | change nothing",
    "rule held engine=build scope=builddir ledger=none warn=1 error=2 unit=s held-by=alpha doc=XV | a value | lower it",
    "rule delta engine=plugin scope=include/ ledger=none doc=XV | a planted rule of the plugin | change nothing",
)
# The files of the planted tree.  The check imports the runner of each engine in a function, and the closure of a
# rule follows only the runner of its own engine.
FILES = {
    ".gitignore": "build/\n",
    "utils/bin/guard": "import sys\n",
    "utils/rule-table.txt": "quarantine data/\n",
    "utils/assay/__init__.py": "",
    "utils/assay/guard/check.py": (
        "from assay.core import rows\n"
        "def main():\n"
        "    from assay.guard.engines.meta import runner\n"
        "    from assay.guard.engines.tsast import runner as tree_runner\n"
    ),
    "utils/assay/guard/witness.py": "from assay.guard import check\n",
    "utils/assay/guard/selftest.py": "import importlib\n",
    "utils/assay/core/rows.py": "VALUE = 1\n",
    "utils/assay/core/pins.py": "VALUE = 1\n",
    "utils/assay/core/registry.py": "VALUE = 1\n",
    "utils/assay/guard/engines/meta/runner.py": "from assay.core import pins\n",
    "utils/assay/guard/engines/meta/alpha.py": "from .helper import VALUE\n",
    "utils/assay/guard/engines/meta/helper.py": "VALUE = 1\n",
    "utils/assay/guard/engines/tsast/runner.py": "VALUE = 1\n",
    "utils/assay/guard/engines/tsast/beta.py": "VALUE = 1\n",
    "utils/assay/guard/engines/plugin/runner.py": "VALUE = 1\n",
    "utils/assay/guard/engines/plugin/witness.py": "VALUE = 1\n",
    "utils/assay/guard/engines/plugin/delta.py": "VALUE = 1\n",
    "utils/assay/tests/sample/test_one.py": "from assay.core import registry\n",
    "utils/plants/alpha/plant-one/input.txt": "one\n",
    "utils/plants/beta/plant-one/unit.cpp.planted": "int planted;\n",
    "data/item.txt": "data\n",
    "data/unit.cpp": "int data;\n",
    "extra/notes.txt": "notes\n",
    "include/planted.h": "#pragma once\n",
    "other.txt": "other\n",
}


def plant(root: Path, rows: tuple[str, ...] = RULES) -> None:
    """Write the planted tree with the rows of the registry, and commit it."""
    for name, text in {**FILES, "utils/registry.txt": rule_registry.planted(rows)}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    git.commit_all(root, "a planted tree")


def relative(root: Path, found: inputs.Inputs) -> set[str]:
    """Return the input files of a result, relative to the root."""
    return {str(Path(path).relative_to(root)) for path in found.files if Path(path).is_relative_to(root)}


def parse_cases(expect: testing.Expect) -> None:
    """Check the reader of the words of a call, the paths of a module and the name of a relative import."""
    call = inputs.parsed_call(["witness", "--engine", "plugin", "--cxx", "/c", "--build-dir", "/b", "--jobs", "8"], ())
    expect(
        "a witness of an engine with a compiler reads",
        call is not None and call.engines == ("plugin",) and call.runs_compiler and call.build_dir == "/b",
    )
    expect(
        "a check of two rules reads",
        inputs.parsed_call(["check", "--rule", "a", "--rule", "b"], ())
        == inputs.Call("check", ("a", "b"), (), "", False, False),
    )
    expect(
        "a variable of the compiler marks a call that compiles",
        getattr(inputs.parsed_call(["witness", "--rule", "a"], ("ASSAY_TEST_CXX",)), "runs_compiler", False),
    )
    expect("an unknown option gives no call", inputs.parsed_call(["check", "--rule", "a", "--other"], ()) is None)
    expect("an option with no value gives no call", inputs.parsed_call(["check", "--rule"], ()) is None)
    expect("another subcommand gives no call", inputs.parsed_call(["write", "--rule", "a"], ()) is None)
    expect(
        "the places of a module are its file and the file of its package",
        inputs.module_path("assay.core.rows") == ("utils/assay/core/rows.py", "utils/assay/core/rows/__init__.py"),
    )
    expect(
        "a relative import resolves in the package of a module",
        inputs.absolute_import("assay.guard.engines.meta.alpha", ".helper", False) == "assay.guard.engines.meta.helper",
    )
    expect(
        "a relative import with two points resolves in the parent package",
        inputs.absolute_import("assay.guard.engines.meta.alpha", "..tsast", False) == "assay.guard.engines.tsast",
    )
    expect(
        "a relative import of a package resolves in the package itself",
        inputs.absolute_import("assay.core", ".rows", True) == "assay.core.rows",
    )


def rule_cases(expect: testing.Expect) -> None:
    """Check the inputs of the rule tests on a planted tree."""
    with git.scratch_repository() as root:
        plant(root)
        deriver = inputs.Deriver(root)
        alpha = deriver.test_inputs(["check", "--rule", "alpha", "--build-dir", "/b"], ())
        files = relative(root, alpha) if alpha is not None else set()
        expect(
            "a check of a rule reads its plants, its scope, its ledger, the rule table and the masters",
            {
                "utils/plants/alpha/plant-one/input.txt",
                "data/item.txt",
                "data/unit.cpp",
                "utils/ledgers/alpha.txt",
                "utils/rule-table.txt",
                "utils/bin/guard",
            }
            <= files,
        )
        expect(
            "the closure of a rule holds its module, a relative import and the runner of the engine meta",
            {
                "utils/assay/guard/engines/meta/alpha.py",
                "utils/assay/guard/engines/meta/helper.py",
                "utils/assay/guard/engines/meta/runner.py",
                "utils/assay/guard/check.py",
            }
            <= files,
        )
        expect(
            "the closure of a rule of the engine meta does not hold the runner of the engine tsast",
            "utils/assay/guard/engines/tsast/runner.py" not in files,
        )
        expect("a module of the closure gives its data file", "utils/pins.txt" in files)
        expect(
            "no input of a rule is a plant of another rule or a file outside its scope",
            "utils/plants/beta/plant-one/unit.cpp.planted" not in files and "other.txt" not in files,
        )
        texts = alpha.texts if alpha is not None else ()
        expect(
            "the texts of a rule hold its row, the row that it holds, the commit and the list of the files",
            sum("id='alpha'" in text for text in texts) == 1
            and sum("id='held'" in text for text in texts) == 1
            and any(text.startswith("head ") for text in texts)
            and any(text.startswith("files ") for text in texts),
        )
        beta = deriver.test_inputs(["check", "--rule", "beta"], ())
        beta_files = relative(root, beta) if beta is not None else set()
        expect(
            "a scope of a rule of the engine tsast holds only the C++ files of the tree",
            "data/unit.cpp" in beta_files and "data/item.txt" not in beta_files,
        )
        expect(
            "the field inputs adds its files, and a rule of the engine tsast follows the runner of tsast",
            "extra/notes.txt" in beta_files and "utils/assay/guard/engines/tsast/runner.py" in beta_files,
        )
        gamma_check = deriver.test_inputs(["check", "--rule", "gamma", "--build-dir", "/b"], ())
        expect(
            "a check of a rule that reads the build directory runs on each call",
            gamma_check is not None and "build directory" in gamma_check.always,
        )
        gamma_witness = deriver.test_inputs(["witness", "--rule", "gamma"], ())
        expect(
            "the witness of a rule that reads the build directory has inputs",
            gamma_witness is not None and not gamma_witness.always and bool(gamma_witness.files),
        )
        plugin = deriver.test_inputs(["witness", "--engine", "plugin", "--cxx", "/c", "--build-dir", "/b"], ())
        plugin_files = relative(root, plugin) if plugin is not None else set()
        expect(
            "the witness of the engine plugin reads the witness of the plugin, include/ and the compile database",
            "utils/assay/guard/engines/plugin/witness.py" in plugin_files
            and "include/planted.h" in plugin_files
            and plugin is not None
            and str(compdb.database_file(Path("/b"))) in plugin.files,
        )
        engine = deriver.test_inputs(["check", "--engine", "plugin", "--build-dir", "/b"], ())
        expect("a check of an engine runs on each call", engine is not None and "objects of the build" in engine.always)
        missing = deriver.test_inputs(["check", "--rule", "nothing"], ())
        expect("a rule that the registry does not hold runs on each call", missing is not None and bool(missing.always))
        expect("the words of a module test give no inputs", deriver.test_inputs(["selftest", "core.rows"], ()) is None)


def group_cases(expect: testing.Expect) -> None:
    """Check the inputs of a group test, and a tree that is not a git work tree."""
    with git.scratch_repository() as root:
        plant(root)
        deriver = inputs.Deriver(root)
        group = deriver.test_inputs(["selftest", "sample", "--part", "0/2"], ("ASSAY_TEST_CXX",))
        files = relative(root, group) if group is not None else set()
        expect(
            "a group reads its test modules, their closure, the registry and the masters",
            {
                "utils/assay/tests/sample/test_one.py",
                "utils/assay/core/registry.py",
                "utils/registry.txt",
                "utils/assay/guard/selftest.py",
                "utils/bin/guard",
            }
            <= files,
        )
        expect("a group that compiles reads include/", "include/planted.h" in files)
        expect(
            "a group reads no plant and no rule module",
            not any(path.startswith("utils/plants/") for path in files)
            and "utils/assay/guard/engines/meta/alpha.py" not in files,
        )
        expect("a directory that is no group gives no inputs", deriver.test_inputs(["selftest", "absent"], ()) is None)
    with git.scratch_repository() as root:
        for name, text in FILES.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        (root / "utils/registry.txt").write_text(rule_registry.planted(RULES))
        found = inputs.Deriver(root).test_inputs(["check", "--rule", "alpha"], ())
        expect("a tree with no commit runs each test on each call", found is not None and "git" in found.always)


def row_cases(expect: testing.Expect) -> None:
    """Check that a change of a row of a different rule changes no input of a rule."""
    with git.scratch_repository() as root:
        plant(root)
        before = inputs.Deriver(root).test_inputs(["check", "--rule", "beta"], ())
        changed = tuple(row.replace("a rule that reads the store", "a changed rule") for row in RULES)
        (root / "utils/registry.txt").write_text(rule_registry.planted(changed))
        after = inputs.Deriver(root).test_inputs(["check", "--rule", "beta"], ())
        expect("a change of the row of a different rule changes no input", before == after)
        alpha_changed = tuple(
            row.replace(
                "rule alpha engine=meta scope=data/ ledger=none doc=XV | a planted rule",
                "rule alpha engine=meta scope=data/ ledger=none doc=XV | a changed rule",
            )
            for row in RULES
        )
        first = inputs.Deriver(root).test_inputs(["check", "--rule", "alpha"], ())
        (root / "utils/registry.txt").write_text(rule_registry.planted(alpha_changed))
        second = inputs.Deriver(root).test_inputs(["check", "--rule", "alpha"], ())
        expect(
            "a change of the row of the rule changes its texts",
            first is not None and second is not None and first.texts != second.texts,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (parse_cases, rule_cases, group_cases, row_cases))
