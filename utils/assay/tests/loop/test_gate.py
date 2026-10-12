"""The test of the gate of a landing (assay.loop.gate).

`guard selftest tests.loop.test_gate` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the selection of the configurations of a change.  They hold
the table of the configurations against the ledgers and the presets of this
tree, and the warm tree on a planted repository.  The planted repository
holds a registry with two rules, the ledger of one of them, and two planted
masters.  The planted master loop writes a JUnit report that a mode file of
the build directory selects.  A planted ctest runs a red test again alone.
The run of the gate then gives its verdict with no compiler and no CMake.
"""

import os
import sys
import tempfile
from pathlib import Path

from assay.core import git, paths, proc, testing
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.loop import gate, syntax
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader

# The registry of the planted repository: one measured rule and one rule with a ledger of configurations.
REGISTRY = (
    "format 1\n"
    "rule slow-row engine=build scope=builddir ledger=max warn=10 error=20 unit=s held-by=cmake doc=XV | a time | "
    "lower the value\n"
    "rule door-unit engine=plugin scope=tracked ledger=count kinds=header-door doc=XXII.R4 | a door | move the door\n"
)
DOOR_LEDGER = (
    "format 1\nrule door-unit\n"
    "configuration x86_64-release kind x86_64-release\n"
    "configuration aarch64-debug-asan kind aarch64-debug-asan\n"
)
# The planted master loop: `syntax --changed` names no preset, and `test BUILD` writes the report of the mode of the
# build directory.  The mode build gives a build that fails, and the mode late gives an error after a green run.
LOOP_MASTER = """import os
import pathlib
import sys

words = sys.argv[1:]
if words[:2] == ["syntax", "--changed"]:
    sys.exit(0)
build = pathlib.Path(words[1])
(build / "base.txt").write_text(os.environ.get("CRUCIBLE_CHANGE_BASE", "-"))
mode = (build / "mode.txt").read_text().strip()
if mode == "build":
    print("the build gives an error")
    sys.exit(1)
statuses = {"test_passes": "run"}
if mode in ("flaky", "broken"):
    statuses["test_" + mode] = "fail"
cases = "".join(f'<testcase name="{name}" status="{status}"/>' for name, status in statuses.items())
cases += '<testcase name="test_skips" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/></testcase>'
report = build / "affected-tests" / "junit.xml"
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
sys.exit(1 if "fail" in statuses.values() or mode == "late" else 0)
"""
# The planted master guard: `warnings` gives 0, and `check` writes its rule and the base of the change, and does not
# apply.
GUARD_MASTER = """import os
import pathlib
import sys

words = sys.argv[1:]
if words[0] == "check":
    with open(pathlib.Path(words[4]) / "checks.txt", "a", encoding="utf-8") as handle:
        handle.write(f"{words[2]} {os.environ.get('CRUCIBLE_CHANGE_BASE', '-')}\\n")
sys.exit(0 if words[0] == "warnings" else 77)
"""
# The planted ctest: a test that runs alone is green, unless it is test_broken.
CTEST = """import pathlib
import sys

words = sys.argv[1:]
names = pathlib.Path(words[words.index("--tests-from-file") + 1]).read_text().split()
report = pathlib.Path(words[words.index("--output-junit") + 1])
cases = "".join(
    f'<testcase name="{name}" status="{"fail" if name == "test_broken" else "run"}"/>' for name in names
)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
"""
PLANTED = {
    "notes.txt": "notes at head\n",
    "utils/registry.txt": REGISTRY,
    "utils/ledgers/door-unit.txt": DOOR_LEDGER,
    "utils/bin/loop": LOOP_MASTER,
    "utils/bin/guard": GUARD_MASTER,
}
# A memory reader with much memory, and a pause that does not wait, for the heavy step of a run.
MUCH_MEMORY_GB = 1e6


def planted_repository(root: Path) -> str:
    """Plant the repository in root with one commit, and return the commit."""
    root.mkdir(parents=True, exist_ok=True)
    git.init(root)
    for name, text in PLANTED.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return git.commit_all(root, "Add the planted tree")


def commit_change(root: Path, path: str, text: str, message: str = "Change a file") -> str:
    """Write one file of the planted repository, commit it, and return the commit."""
    file = root / path
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(text, encoding="utf-8")
    return git.commit_all(root, message)


def history_status(root: Path) -> int:
    """Run the rule history of this tree on a planted tree, and return its exit."""
    command = [sys.executable, str(paths.GUARD), "check", "--rule", "history", "--root", str(root)]
    return proc.run(proc.Command(command, timeout_s=proc.TOOL_S)).exit


def names_of(configurations: tuple[gate.Configuration, ...]) -> list[str]:
    """Return the names of some configurations."""
    return [configuration.name for configuration in configurations]


def select_cases(expect: testing.Expect) -> None:
    """Hold the selection of the configurations of a change (THE SELECTION)."""
    expect(
        "a change of a text file selects the debug build only",
        names_of(gate.select(["CLAUDE.md", "utils/assay/loop/land.py"], [], [], False)) == ["debug"],
    )
    expect(
        "a change of a C++ test selects the debug build, the release build and ThreadSanitizer",
        names_of(gate.select(["test/test_arena.cpp"], [], [], False)) == ["debug", "release", "tsan"],
    )
    expect(
        "a change of include/ or of a top build file adds ThreadSanitizer and UBSan-strict",
        names_of(gate.select(["include/foundation/Platform.h"], [], [], False))
        == names_of(gate.select(["CMakeLists.txt"], [], [], False))
        == ["debug", "release", "tsan", "ubsan-strict"],
    )
    expect(
        "a change of bench/ or test/fuzz/ adds the bench and fuzz build",
        names_of(gate.select(["test/fuzz/CMakeLists.txt"], [], [], False))
        == ["debug", "release", "tsan", "bench-fuzz"],
    )
    expect(
        "the parse presets of an arm and the configurations of a changed ledger join, in the sequence of the table",
        names_of(gate.select(["notes.txt"], ["syntax-aarch64"], ["x86_64-debug-tsan", "aarch64-debug-asan"], False))
        == ["syntax-aarch64", "debug", "tsan"],
    )
    every = names_of(gate.CONFIGURATIONS)
    expect(
        "a change of the plugin or of the rule table, and --all-configurations, select each configuration",
        names_of(gate.select(["utils/tools/quarantine/unit.cpp"], [], [], False))
        == names_of(gate.select(["utils/rule-table.txt"], [], [], False))
        == names_of(gate.select(["notes.txt"], [], [], True))
        == every,
    )


def table_cases(expect: testing.Expect) -> None:
    """Hold the table of the configurations against the ledgers and the presets of this tree (THE CONFIGURATIONS)."""
    registry = rule_registry.load(paths.REPO_ROOT / rule_registry.REGISTRY_PATH)
    named: set[str] = set()
    for rule in registry.rules.values():
        if rule.policy != rule_registry.Policy.NONE:
            named.update(item.name for item in rule_ledger.read(rule, paths.REPO_ROOT).configurations)
    unknown = sorted(name for name in named if name not in gate.BY_LEDGER_NAME)
    expect(f"each configuration that a ledger of this tree names is in the table of the gate: {unknown}", not unknown)
    wrong: list[str] = []
    for configuration in gate.CONFIGURATIONS:
        try:
            build = syntax.build_directory(paths.REPO_ROOT, configuration.preset)
        except syntax.ParseError as problem:
            wrong.append(str(problem))
            continue
        if configuration.is_parse and build != gate.build_directory(paths.REPO_ROOT, configuration):
            wrong.append(f"{configuration.name}: {build}")
    expect(f"each preset of the table is in the presets, and each parse build is at its binaryDir: {wrong}", not wrong)


def tree_cases(expect: testing.Expect) -> None:
    """Hold the warm tree: its commit, its history, a kept time of change, and a tree in the repository."""
    with tempfile.TemporaryDirectory(prefix="gate-tree-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        tree = work / "warm"
        gate.move_tree(root, tree, head)
        expect(
            "the warm tree holds the commit with its history and a clean status",
            git.head(tree) == head
            and (tree / "notes.txt").read_text(encoding="utf-8") == PLANTED["notes.txt"]
            and git.has_no_change(tree),
        )
        kept = (tree / "utils/registry.txt").stat().st_mtime_ns
        os.utime(tree / "utils/registry.txt", ns=(kept - 10**9, kept - 10**9))
        before = (tree / "utils/registry.txt").stat().st_mtime_ns
        second = commit_change(root, "notes.txt", "notes of the second commit\n")
        gate.move_tree(root, tree, second)
        expect(
            "a move writes only the files that the commit changes, and each other file keeps its time of change",
            git.head(tree) == second
            and (tree / "notes.txt").read_text(encoding="utf-8") == "notes of the second commit\n"
            and (tree / "utils/registry.txt").stat().st_mtime_ns == before,
        )
        verdicts = {"base": history_status(tree)}
        raised = REGISTRY.replace("error=20 unit=s", "error=30 unit=s")
        bodies = (("unmeasured", "The tests are slower."), ("measured", "The slowest job of slow-row took 26 s."))
        for case, body in bodies:
            case_root = work / case
            planted_repository(case_root)
            commit = commit_change(case_root, "utils/registry.txt", raised, f"Raise the slow row\n\n{body}\n")
            case_tree = work / f"warm-{case}"
            gate.move_tree(case_root, case_tree, commit)
            verdicts[case] = history_status(case_tree)
        expect(
            f"the rule history reads the history of the warm tree: 0 at the base, 1 on a raise with no measurement "
            f"and 0 on a measured raise, and it gives {verdicts}",
            verdicts == {"base": 0, "unmeasured": 1, "measured": 0},
        )
        try:
            gate.move_tree(root, root / "inner", head)
            expect("the gate refuses a warm tree in the work tree of the repository", False)
        except gate.GateError:
            expect("the gate refuses a warm tree in the work tree of the repository", True)


def rules_cases(expect: testing.Expect) -> None:
    """Hold the changed rules of a change, and the configurations of their ledgers."""
    with tempfile.TemporaryDirectory(prefix="gate-rules-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        tree = work / "warm"
        module = "utils/assay/guard/engines/plugin/door_unit.py"
        raised = REGISTRY.replace("error=20", "error=21")
        cases: tuple[tuple[str, str, str, list[str]], ...] = (
            ("a change of its ledger", "utils/ledgers/door-unit.txt", DOOR_LEDGER + "# a comment\n", ["door-unit"]),
            ("a change of its row of the registry", "utils/registry.txt", raised, ["slow-row"]),
            ("a change of its module", module, "# the planted module\n", ["door-unit"]),
            ("a change of a different file", "notes.txt", "other notes\n", []),
        )
        previous = head
        for name, path, text, expected in cases:
            commit = commit_change(root, path, text)
            gate.move_tree(root, tree, commit)
            found = [rule.id for rule in gate.changed_rules(tree, previous, [path])]
            expect(f"{name} changes the rules {expected}, and the gate finds {found}", found == expected)
            previous = commit
        door = rule_registry.load(tree / rule_registry.REGISTRY_PATH).rules["door-unit"]
        names = gate.ledger_names(tree, [door])
        expect(
            "the ledger of a changed rule gives its configuration names, and the selection builds those that the "
            "host can build",
            names == ["aarch64-debug-asan", "x86_64-release"]
            and names_of(gate.select(["notes.txt"], [], names, False)) == ["debug", "release"],
        )


def planted_gate(work: Path, root: Path, tree: Path, mode: str) -> gate.Gate:
    """Plant the debug build of the warm tree with a mode and a planted ctest, and return a gate on it."""
    build = tree / "build"
    build.mkdir(parents=True, exist_ok=True)
    ninja_reader.plant_graph(build, "")
    (build / "mode.txt").write_text(f"{mode}\n", encoding="utf-8")
    ctest = work / "ctest"
    ctest.write_text(f"#!{sys.executable}\n{CTEST}", encoding="utf-8")
    ctest.chmod(0o755)
    cmake_reader.plant(build, cache={"CMAKE_CTEST_COMMAND": str(ctest)})
    return gate.Gate(root, work / "land", 1, False, read=lambda: MUCH_MEMORY_GB, pause=lambda _seconds: None)


def run_cases(expect: testing.Expect) -> None:
    """Hold the verdict of a run: a red test that is green alone, a test that is red alone too, and a build error."""
    with tempfile.TemporaryDirectory(prefix="gate-run-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "notes.txt", "a change of the notes\n")
        candidate = gate.Candidate(head, commit, ("notes.txt",))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        verdicts: dict[str, gate.Verdict] = {}
        outputs: dict[str, str] = {}
        for mode in ("clean", "flaky", "broken", "build", "late"):
            planted = planted_gate(work, root, tree, mode)

            def run(planted: gate.Gate = planted) -> gate.Verdict:
                return planted.check(candidate, False)

            verdicts[mode], outputs[mode] = testing.captured(run)
        expect(
            "a change of a text file runs the debug build, and the gate accepts a run with no red test",
            verdicts["clean"].configurations == ("debug",) and verdicts["clean"].is_accepted,
        )
        expect(
            "a test that gives a skip does not reject the commit, and the gate writes its name",
            "debug: 1 skipped: test_skips" in outputs["clean"],
        )
        expect(
            "a red test that is green alone does not reject the commit",
            verdicts["flaky"].is_accepted,
        )
        expect(
            "a test that is red alone too rejects the commit, and the problem names it",
            not verdicts["broken"].is_accepted and "red alone too: test_broken" in verdicts["broken"].problems[0],
        )
        expect(
            "a build error rejects the commit",
            not verdicts["build"].is_accepted
            and "the build or the test run gives an error" in verdicts["build"].problems[0],
        )
        expect(
            "an exit other than 0 after a run with no red test rejects the commit",
            not verdicts["late"].is_accepted and "with no red test" in verdicts["late"].problems[0],
        )
        expect("each run leaves the warm tree at the commit", git.head(tree) == commit)
        build = tree / "build"
        expect(
            "the suite of one commit gets no base of the change, and no rule runs again",
            (build / "base.txt").read_text(encoding="utf-8") == "-" and not (build / "checks.txt").exists(),
        )
        commit_change(root, "notes.txt", "the first of two commits\n")
        second = commit_change(root, "notes.txt", "the second of two commits\n")
        stacked = planted_gate(work, root, tree, "clean")

        def run_stacked(planted: gate.Gate = stacked) -> gate.Verdict:
            return planted.check(gate.Candidate(commit, second, ("notes.txt",)), False)

        verdict, _ = testing.captured(run_stacked)
        expect(
            "two commits run quarantine-lines again with the base of the change, and the suite gets no base",
            verdict.is_accepted
            and (build / "base.txt").read_text(encoding="utf-8") == "-"
            and (build / "checks.txt").read_text(encoding="utf-8") == f"quarantine-lines {commit}\n",
        )


def lock_cases(expect: testing.Expect) -> None:
    """Hold the lock of the warm tree: a second session waits for the first."""
    with tempfile.TemporaryDirectory(prefix="gate-lock-") as work_text:
        work = Path(work_text)
        with gate.session(work, work / "land", 2, True) as first:
            expect(
                "a session gives the gate of its work directory, and holds the lock file",
                first.work == work / "land" and first.jobs == 2 and (work / "land" / gate.LOCK).is_file(),
            )
        with gate.session(work, work / "land", 1, False):
            expect("the lock is free after the first session", True)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (select_cases, table_cases, tree_cases, rules_cases, run_cases, lock_cases),
    )
