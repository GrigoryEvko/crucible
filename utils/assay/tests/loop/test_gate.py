"""The test of the gate of a landing (assay.loop.gate).

`guard selftest tests.loop.test_gate` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the selection of the configurations of a change.  They hold
the table of the configurations against the ledgers and the presets of this
tree, and the warm tree on a planted repository.  The planted repository
holds a registry with a rule that reads the build, the ledger of a counted
rule, and two planted masters.  The planted master loop writes a JUnit
report that a mode file of the build directory selects.  A planted ctest
lists the test of the rule that reads the build.  It also runs a red test
again alone.  The run of the gate then gives its verdict with no compiler
and no CMake.  A change of a C++ file must run the tests of the changed
inputs in each other configuration of objects.  A red test there must
reject the commit.  A change of the module of the rule that reads the build
must run that test in each other configuration of objects.
"""

import json
import os
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

from assay.core import git, paths, proc, testing
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.loop import gate, syntax
from assay.loop import inputs as guard_inputs
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader

# The registry of the planted repository: one measured rule, one rule with a ledger of configurations, and a rule that
# reads the build.  It also holds the other rules that a parse configuration checks.
REGISTRY = (
    "format 1\n"
    "rule slow-row engine=build scope=builddir ledger=max warn=10 error=20 unit=s held-by=cmake doc=XV | a time | "
    "lower the value\n"
    "rule door-unit engine=plugin scope=tracked ledger=count kinds=header-door doc=XXII.R4 | a door | move the door\n"
    "rule plan-check engine=meta scope=builddir ledger=none doc=XIII | a planted rule of the build | change nothing\n"
    "rule quarantine engine=plugin scope=tracked ledger=count kinds=std-object doc=XXII | a finding | remove it\n"
    "rule heavy-include engine=plugin scope=include/ ledger=count kinds=heavy-include doc=XV | a header | remove it\n"
    "rule syscall-capability engine=plugin scope=tracked ledger=count kinds=claim doc=XXII | a call | remove it\n"
)
# The module of the rule that reads the build, its test, and the module of the test launcher of the planted repository.
BUILD_RULE_MODULE = "utils/assay/guard/engines/meta/plan_check.py"
BUILD_RULE_TEST = "guard_plan_check"
LAUNCHER_MODULE = "utils/assay/loop/teststep.py"
# The build directory of each configuration of objects of the gate other than default, with the mode clean.
OTHER_BUILDS = {
    "build-release": "clean",
    "build-tsan": "clean",
    "build-ubsan-strict": "clean",
    "build-bench-fuzz": "clean",
}
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
with open(build / "words.txt", "a") as handle:
    handle.write(" ".join(words[2:]) + "\\n")
mode = (build / "mode.txt").read_text().strip()
if mode == "build":
    print("the build gives an error")
    sys.exit(1)
statuses = {"test_passes": "run"}
if mode in ("flaky", "broken"):
    statuses["test_" + mode] = "fail"
if mode == "falls":
    ledger = pathlib.Path("utils/ledgers/door-unit.txt").read_text()
    statuses["test_ledger"] = "fail" if "| 2 |" in ledger else "run"
if mode == "keep":
    ledger = pathlib.Path("utils/ledgers/slow-row.txt").read_text()
    statuses["guard_slow_row"] = "fail" if "| 10 |" in ledger else "run"
cases = "".join(f'<testcase name="{name}" status="{status}"/>' for name, status in statuses.items())
cases += '<testcase name="test_skips" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/></testcase>'
report = build / "affected-tests" / "junit.xml"
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
sys.exit(1 if "fail" in statuses.values() or mode == "late" else 0)
"""
# The planted master guard: `warnings` gives 0, and `check` writes its rule and the base of the change, and does not
# apply.  `write` lowers the count 2 of the planted ledger to 1, as the write of a fall does, and writes its rule.  It
# also removes the row of gone.h, as the write of a fall to 0 does.  The write of slow-row raises the value 10 of its
# ledger to 12, as the write of a kept row does.  The write of the rule history runs the guard of this tree
# (GUARD_MARK).
GUARD_MARK = "@GUARD@"
GUARD_MASTER = """import os
import pathlib
import subprocess
import sys

words = sys.argv[1:]
if words[0] == "check":
    with open(pathlib.Path(words[4]) / "checks.txt", "a", encoding="utf-8") as handle:
        handle.write(f"{words[2]} {os.environ.get('CRUCIBLE_CHANGE_BASE', '-')}\\n")
if words[:3] == ["write", "--rule", "history"]:
    sys.exit(subprocess.run([sys.executable, "@GUARD@", *words], check=False).returncode)
if words[0] == "write":
    name, old, new = ("slow-row", "| 10 |", "| 12 |") if words[2] == "slow-row" else ("door-unit", "| 2 |", "| 1 |")
    ledger = pathlib.Path(f"utils/ledgers/{name}.txt")
    lines = ledger.read_text().replace(old, new).splitlines(keepends=True)
    ledger.write_text("".join(line for line in lines if "gone.h" not in line))
    with open(pathlib.Path(words[4]) / "writes.txt", "a", encoding="utf-8") as handle:
        handle.write(f"{words[2]}\\n")
    sys.exit(0)
sys.exit(0 if words[0] == "warnings" else 77)
"""
# The planted ctest: the listing of a build is the file tests.json of the build.  A test that runs alone is green,
# unless it is test_broken, or test_ledger or guard_slow_row before its write.
CTEST = """import pathlib
import sys

words = sys.argv[1:]
if "--show-only=json-v1" in words:
    print(pathlib.Path(words[words.index("--test-dir") + 1], "tests.json").read_text())
    sys.exit(0)
names = pathlib.Path(words[words.index("--tests-from-file") + 1]).read_text().split()
report = pathlib.Path(words[words.index("--output-junit") + 1])
ledger = pathlib.Path("utils/ledgers/door-unit.txt").read_text()
red = {"test_broken"} | ({"test_ledger"} if "| 2 |" in ledger else set())
slow = pathlib.Path("utils/ledgers/slow-row.txt")
red |= {"guard_slow_row"} if slow.is_file() and "| 10 |" in slow.read_text() else set()
cases = "".join(f'<testcase name="{name}" status="{"fail" if name in red else "run"}"/>' for name in names)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
"""
PLANTED = {
    "notes.txt": "notes at head\n",
    "utils/registry.txt": REGISTRY,
    "utils/ledgers/door-unit.txt": DOOR_LEDGER,
    "utils/bin/loop": LOOP_MASTER,
    "utils/bin/guard": GUARD_MASTER,
    BUILD_RULE_MODULE: "VALUE = 1\n",
}
# A memory reader with much memory, and a pause that does not wait, for the heavy step of a run.
MUCH_MEMORY_GB = 1e6
# The row of the rule history and the ledgers of the repository of the checks before the builds.  Its registry also
# holds a measured rule with a ledger.
HISTORY_RULE = (
    "rule history engine=meta scope=utils/registry.txt ledger=admit doc=XV | a planted history rule | give the "
    "measurement"
)
SLOW_LEDGER = "utils/ledgers/slow-row.txt"
HISTORY_LEDGER = "utils/ledgers/history.txt"
LEDGER_HEAD = "# a planted ledger\nformat 1\nrule {rule}\n"


def planted_repository(root: Path, files: Mapping[str, str] | None = None) -> str:
    """Plant the repository in root with one commit, and return the commit.

    `files` replaces or adds some files of PLANTED.  The guard master runs the guard of this tree for the rule history.
    """
    root.mkdir(parents=True, exist_ok=True)
    git.init(root)
    for name, text in {**PLANTED, **(files or {})}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text.replace(GUARD_MARK, str(paths.GUARD)), encoding="utf-8")
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
        "a change of a text file selects the default build only",
        names_of(gate.select(["CLAUDE.md", "utils/assay/loop/land.py"], [], False)) == ["default"],
    )
    expect(
        "a change of a C++ test selects the default build, the release build and ThreadSanitizer",
        names_of(gate.select(["test/test_arena.cpp"], [], False)) == ["default", "release", "tsan"],
    )
    expect(
        "a change of include/ or of a top build file adds ThreadSanitizer and UBSan-strict",
        names_of(gate.select(["include/foundation/Platform.h"], [], False))
        == names_of(gate.select(["CMakeLists.txt"], [], False))
        == ["default", "release", "tsan", "ubsan-strict"],
    )
    expect(
        "a change of bench/ or test/fuzz/ adds the bench and fuzz build",
        names_of(gate.select(["test/fuzz/CMakeLists.txt"], [], False)) == ["default", "release", "tsan", "bench-fuzz"],
    )
    expect(
        "the parse presets of an arm join, in the sequence of the table",
        names_of(gate.select(["notes.txt"], ["syntax-aarch64"], False)) == ["syntax-aarch64", "default"],
    )
    every = names_of(gate.CONFIGURATIONS)
    expect(
        "a change of the plugin or of the rule table, and --all-configurations, select each configuration",
        names_of(gate.select(["utils/tools/quarantine/unit.cpp"], [], False))
        == names_of(gate.select(["utils/rule-table.txt"], [], False))
        == names_of(gate.select(["notes.txt"], [], True))
        == every,
    )
    others = tuple(item for item in gate.CONFIGURATIONS if not item.is_parse and item.name != gate.ALWAYS)
    changed = gate.other_runs(others, True, ("guard_rule",))
    touched = gate.other_runs(others, False, ("guard_rule",))
    expect(
        "a change that can change each build runs the tests of the changed inputs in each other configuration of "
        "objects, and another change runs only the touched tests there, or nothing",
        [run.mode for run in changed] == [gate.Mode.CHANGED] * len(others)
        and [(run.mode, run.names) for run in touched] == [(gate.Mode.TOUCHED, ("guard_rule",))] * len(others)
        and gate.other_runs(others, False, ()) == ()
        and len(others) == 4,
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
    parse = [configuration.name for configuration in gate.CONFIGURATIONS if configuration.is_parse]
    expect(
        f"the parse configurations of the gate are the parse presets of loop syntax, in their sequence: {parse}",
        parse == list(syntax.PARSE_PRESETS),
    )
    presets_cases(expect)


# The configure presets of a planted presets file: two presets with the tier gate, one of them a parse preset.
PLANTED_BASE: dict[str, object] = {
    "name": "base",
    "binaryDir": "${sourceDir}/build",
    "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan"}},
}
PLANTED_NIGHT: dict[str, object] = {
    "name": "night",
    "binaryDir": "out/night",
    "vendor": {"crucible": {"tier": "nightly"}},
}
PLANTED_PARSE: dict[str, object] = {
    "name": "parse",
    "inherits": "base",
    "binaryDir": "${sourceDir}/build-parse",
    "cacheVariables": {"CRUCIBLE_SYNTAX_ONLY": "ON"},
    "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan-syntax"}},
}


def planted_configurations(configure: list[object]) -> tuple[gate.Configuration, ...] | str:
    """Return the configurations of the gate of a planted presets file, or the text of the refusal of the gate.

    `configure` holds the configure presets of the file, each a JSON object.
    """
    with tempfile.TemporaryDirectory(prefix="gate-presets-") as root_text:
        root = Path(root_text)
        text = json.dumps({"version": 6, "configurePresets": configure})
        (root / "CMakePresets.json").write_text(text, encoding="utf-8")
        try:
            return gate.configurations(root)
        except gate.GateError as problem:
            return str(problem)


def presets_cases(expect: testing.Expect) -> None:
    """Hold the configurations that the gate reads from the tiers of a planted presets file (THE CONFIGURATIONS)."""
    found = planted_configurations([PLANTED_BASE, PLANTED_NIGHT, PLANTED_PARSE])
    expect(
        f"the presets with the tier gate are the configurations, the parse presets first: {found}",
        found
        == (
            gate.Configuration("parse", "build-parse", "x86_64-debug-asan-syntax", True),
            gate.Configuration("base", "build", "x86_64-debug-asan", False),
        ),
    )
    refusal = planted_configurations([PLANTED_BASE, {"name": "loose", "binaryDir": "out/loose"}])
    expect(
        f"a preset with no tier makes the gate refuse the presets: {refusal}",
        isinstance(refusal, str) and "the configure preset loose gives no tier" in refusal,
    )
    away = {**PLANTED_BASE, "binaryDir": "${sourceParentDir}/away-build"}
    refusal = planted_configurations([away])
    expect(
        f"a gate preset with a build directory outside the tree is a refusal: {refusal}",
        isinstance(refusal, str) and "is not in the tree" in refusal,
    )
    refusal = planted_configurations([PLANTED_NIGHT])
    expect(
        f"presets with no tier gate are a refusal: {refusal}",
        isinstance(refusal, str) and "has the tier gate" in refusal,
    )


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


def history_cases(expect: testing.Expect) -> None:
    """Hold the checks before the builds: the rule history on the warm tree, and the kinds of a growth."""
    with tempfile.TemporaryDirectory(prefix="gate-history-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        root.mkdir()
        git.init(root)
        rows = sorted([rule_registry.measured_rule("slow-row", 10, 20, "s", "a time"), HISTORY_RULE])
        files = {
            rule_registry.REGISTRY_PATH: rule_registry.planted(rows),
            SLOW_LEDGER: LEDGER_HEAD.format(rule="slow-row"),
            HISTORY_LEDGER: LEDGER_HEAD.format(rule="history"),
        }
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text, encoding="utf-8")
        base = git.commit_all(root, "Add the planted registry", "The first registry.")
        planted = gate.Gate(root, work / "land", 1, False)
        slow_rows = "".join(f"test/{name} | time | 21 | - | a slow test\n" for name in ("a", "b"))
        grown = commit_change(root, SLOW_LEDGER, files[SLOW_LEDGER] + slow_rows, "Admit two slow tests")
        grow_row = f"{SLOW_LEDGER} | grow | admit | {grown[:9]} | the two slow tests must stay\n"
        admitted = commit_change(root, HISTORY_LEDGER, files[HISTORY_LEDGER] + grow_row, "Admit the growth")
        expect(
            "a commit with no growth, and a grow row that admits the growth of its commit, give no error",
            planted.history_problems(base, base, ()) == ()
            and planted.history_problems(admitted, admitted, ()) == ()
            and git.head(planted.tree) == admitted,
        )
        rewritten_rows = slow_rows.replace("test/a | time | 21", "test/a | time | 25")
        rewritten = commit_change(root, SLOW_LEDGER, files[SLOW_LEDGER] + rewritten_rows, "Write a kept value again")
        expect(
            "a write of the measured value of a kept row adds no row, and the rule history gives no error",
            planted.history_problems(rewritten, rewritten, ()) == (),
        )
        stale = commit_change(root, SLOW_LEDGER, files[SLOW_LEDGER], "Make each slow test fast")
        problems = planted.history_problems(stale, stale, ())
        expect(
            "a change that makes a grow row stale gives the error of the rule history at the row",
            any(HISTORY_LEDGER in item and "admits no finding" in item for item in problems),
        )
        noted = commit_change(root, SLOW_LEDGER, files[SLOW_LEDGER] + "# a note\n", "Note the slow ledger")
        problems = planted.history_problems(noted, noted, ("slow-row",))
        expect(
            "a growth of a commit that adds no row gives the refusal of --grow, with the kinds of its rows",
            any(item.startswith("--grow slow-row: no grow row admits") and "kinds none" in item for item in problems),
        )


def change_cases(expect: testing.Expect) -> None:
    """Hold the change of a commit as the touch reads it, and the touch of the inputs of a test (THE TOUCH)."""
    with tempfile.TemporaryDirectory(prefix="gate-change-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        (root / "notes.txt").unlink()
        (root / rule_registry.REGISTRY_PATH).write_text(REGISTRY.replace("error=20", "error=21"), encoding="utf-8")
        commit = git.commit_all(root, "Raise the slow row and remove the notes")
        tree = work / "warm"
        gate.move_tree(root, tree, commit)
        change = guard_inputs.read_change(tree, head, ["notes.txt", rule_registry.REGISTRY_PATH])
        expect(
            "a change holds each changed path, each path that it deletes, and each rule whose row changed",
            change.paths == {str(tree / "notes.txt"), str(tree / rule_registry.REGISTRY_PATH)}
            and change.deleted == {str(tree / "notes.txt")}
            and change.rows == {"slow-row"},
        )
        item, gone, other = str(tree / "data" / "item.txt"), str(tree / "data" / "gone.txt"), str(tree / "other.txt")
        read = guard_inputs.Inputs(files=(item,), directories=(str(tree / "data"),), rules=("plan-check",))
        empty: frozenset[str] = frozenset()
        expect(
            "a change of a file of the inputs, a deleted file of a directory of the inputs, and a change of the row "
            "of a rule of the inputs each touch the inputs",
            read.is_touched(guard_inputs.Change(frozenset({item}), empty, empty))
            and read.is_touched(guard_inputs.Change(frozenset({gone}), frozenset({gone}), empty))
            and read.is_touched(guard_inputs.Change(empty, empty, frozenset({"plan-check"}))),
        )
        expect(
            "a change of a different file and of a different row does not touch the inputs, and inputs with a reason "
            "to run on each call are touched by each change",
            not read.is_touched(guard_inputs.Change(frozenset({other}), frozenset({other}), frozenset({"slow-row"})))
            and guard_inputs.Inputs(always="a planted reason").is_touched(guard_inputs.Change(empty, empty, empty)),
        )


def listing(tree: Path, build: Path) -> str:
    """Return the ctest listing of a planted build: the test of the rule that reads the build, behind the launcher."""
    source, built = tree.resolve(), str(build.resolve())
    command = [
        sys.executable,
        "-S",
        str(source / "utils/bin/loop"),
        "test-step",
        "--build-dir",
        built,
        "--name",
        BUILD_RULE_TEST,
        sys.executable,
        str(source / "utils/bin/guard"),
        "check",
        "--rule",
        "plan-check",
        "--build-dir",
        built,
    ]
    return json.dumps({"tests": [{"name": BUILD_RULE_TEST, "command": command, "properties": []}]})


def plant_build(work: Path, tree: Path, name: str, mode: str) -> None:
    """Plant one build directory of the warm tree with a mode, a planted ctest and the listing of its tests."""
    build = tree / name
    build.mkdir(parents=True, exist_ok=True)
    ninja_reader.plant_graph(build, "")
    (build / "mode.txt").write_text(f"{mode}\n", encoding="utf-8")
    (build / "tests.json").write_text(listing(tree, build), encoding="utf-8")
    ctest = work / "ctest"
    ctest.write_text(f"#!{sys.executable}\n{CTEST}", encoding="utf-8")
    ctest.chmod(0o755)
    cmake_reader.cache_file(build).write_text("", encoding="utf-8")
    cmake_reader.plant(build, cache={"CMAKE_CTEST_COMMAND": str(ctest), "CMAKE_HOME_DIRECTORY": str(tree)})


def planted_gate(work: Path, root: Path, tree: Path, mode: str, others: Mapping[str, str] | None = None) -> gate.Gate:
    """Plant the default build of the warm tree with a mode, and each other build with its mode, and return a gate.

    `others` maps the name of a build directory, such as build-bench-fuzz, to its mode.
    """
    for name, build_mode in {"build": mode, **(others or {})}.items():
        plant_build(work, tree, name, build_mode)
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
            "a change of a text file runs the default build, and the gate accepts a run with no red test",
            verdicts["clean"].configurations == ("default",) and verdicts["clean"].is_accepted,
        )
        expect(
            "a test that gives a skip does not reject the commit, and the gate writes its name",
            "default: 1 skipped: test_skips" in outputs["clean"],
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


def falls_cases(expect: testing.Expect) -> None:
    """Hold THE FALLS: a fall of a count in the base lands in a new commit of the gate, with no manual write."""
    with tempfile.TemporaryDirectory(prefix="gate-falls-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        ledger = "utils/ledgers/door-unit.txt"
        counted_ledger = (
            "format 1\nrule door-unit\nconfiguration x86_64-debug-asan kind x86_64-debug-asan\n"
            "src/door.cpp | header-door | 2 | door.h | -\n"
        )
        head = commit_change(root, ledger, counted_ledger, "Count two doors")
        commit = commit_change(root, "notes.txt", "a change that removes one door\n", "Remove a door\n\nThe body.")
        candidate = gate.Candidate(head, commit, ("notes.txt",))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "falls")
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        expect(
            "the gate accepts a commit whose suite is red only on a fall of a counted ledger",
            verdict.is_accepted and verdict.configurations == ("default", "default"),
        )
        expect(
            "the counted rule writes its fall, the gate folds the ledger into a new commit with the parents and the "
            "message of the commit, and the warm tree holds that commit",
            verdict.final not in ("", commit)
            and verdict.ledgers == (ledger,)
            and git.show(root, verdict.final, ledger) == counted_ledger.replace("| 2 |", "| 1 |").encode()
            and git.show(root, verdict.final, "notes.txt") == b"a change that removes one door\n"
            and git.parent(root, verdict.final) == head
            and git.message_of(root, verdict.final) == git.message_of(root, commit)
            and git.head(tree) == verdict.final
            and (tree / "build" / "writes.txt").read_text(encoding="utf-8") == "door-unit\n",
        )
        expect(
            "after the fold, the tests run again on the new commit",
            "folded into the commit" in printed and "of objects run again" in printed,
        )


def falls_history_cases(expect: testing.Expect) -> None:
    """Hold THE FALLS with the rule history: a fall that makes a row of the kind `grow` admit nothing removes it."""
    with tempfile.TemporaryDirectory(prefix="gate-falls-history-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        ledger = "utils/ledgers/door-unit.txt"
        doors = (
            "format 1\nrule door-unit\nconfiguration x86_64-debug-asan kind x86_64-debug-asan\n"
            "src/door.cpp | header-door | 2 | door.h | -\n"
        )
        planted_repository(root, {ledger: doors, rule_registry.REGISTRY_PATH: f"{REGISTRY}{HISTORY_RULE}\n"})
        gone = "src/gone.cpp | header-door | 1 | gone.h | -\n"
        grown = commit_change(root, ledger, doors + gone, "Count the door of gone.h")
        grow_row = f"{ledger} | grow | admit | {grown[:9]} | the door of gone.h must stay\n"
        rows = LEDGER_HEAD.format(rule="history") + grow_row
        head = commit_change(root, HISTORY_LEDGER, rows, "Admit the door of gone.h")
        commit = commit_change(root, "notes.txt", "a change that removes gone.h\n", "Remove gone.h\n\nThe body.")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, commit)
        at_head = history_status(tree)
        (tree / ledger).write_text(doors.replace("| 2 |", "| 1 |"), encoding="utf-8")
        with_fall = history_status(tree)
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "falls")
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",)), False))
        folded = (git.show(root, verdict.final, HISTORY_LEDGER) or b"").decode()
        at_final = history_status(tree)
        holds = (
            at_head == 0
            and with_fall == 1
            and verdict.is_accepted
            and verdict.ledgers == (ledger, HISTORY_LEDGER)
            and "| grow |" not in folded
            and at_final == 0
        )
        label = (
            f"a fall that makes a grow row admit nothing makes the gate write the ledger of the rule history, and the "
            f"commit of the falls holds no such row: the rule history gives {at_head} at the commit, {with_fall} "
            f"with the fall and the row, and {at_final} at the commit of the falls, and the gate folds "
            f"{verdict.ledgers}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def keep_cases(expect: testing.Expect) -> None:
    """Hold the kept rows: a red test of a rule of --keep gets the write of the rule, and the gate folds the ledger."""
    with tempfile.TemporaryDirectory(prefix="gate-keep-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        row = "@build/bench/a.o | x86_64-debug-asan-bench-fuzz | 10 | - | the measure of the first build\n"
        head = commit_change(root, SLOW_LEDGER, LEDGER_HEAD.format(rule="slow-row") + row, "Keep an object")
        commit = commit_change(root, "notes.txt", "a change that grows the object\n", "Grow the object")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "keep")
        keep = gate.Keep("slow-row", "the doors of fixy/os add code to the bench")
        verdict, _ = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",)), False))
        expect(
            "with no --keep, a red test of a measured rule rejects the commit",
            not verdict.is_accepted and any("guard_slow_row" in problem for problem in verdict.problems),
        )
        verdict, _ = testing.captured(
            lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",), (keep,)), True)
        )
        kept = (git.show(root, verdict.final, SLOW_LEDGER) or b"").decode()
        expect(
            "with --keep, the gate writes the measured value, gives the raised row the reason of --keep, and folds the "
            "ledger into the commit of the falls",
            verdict.is_accepted
            and verdict.ledgers == (SLOW_LEDGER,)
            and "| 12 | - | the doors of fixy/os add code to the bench" in kept
            and verdict.kept == ("slow-row: @build/bench/a.o in x86_64-debug-asan-bench-fuzz from 10 to 12",)
            and git.parent(root, verdict.final) == head,
        )


def changed_tests_cases(expect: testing.Expect) -> None:
    """Hold a configuration of the tests of the changed inputs: no --all, a red test rejects, and --keep applies."""
    with tempfile.TemporaryDirectory(prefix="gate-changed-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        bench = "configuration x86_64-debug-asan-bench-fuzz kind x86_64-debug-asan-bench-fuzz\n"
        door = DOOR_LEDGER.replace("configuration aarch64-debug-asan kind aarch64-debug-asan\n", bench)
        commit_change(root, "utils/ledgers/door-unit.txt", door, "Count the bench build")
        row = "@build/bench/a.o | x86_64-debug-asan-bench-fuzz | 10 | - | the measure of the first build\n"
        head = commit_change(root, SLOW_LEDGER, LEDGER_HEAD.format(rule="slow-row") + row, "Keep an object")
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        verdicts: dict[str, gate.Verdict] = {}
        for mode in ("broken", "keep"):
            others = {**OTHER_BUILDS, "build-bench-fuzz": mode}
            planted = planted_gate(work, root, tree, "clean", others)
            keeps = (gate.Keep("slow-row", "the bench grows"),) if mode == "keep" else ()
            candidate = gate.Candidate(head, commit, ("test/a.cpp",), keeps)

            def run(planted: gate.Gate = planted, candidate: gate.Candidate = candidate) -> gate.Verdict:
                return planted.check(candidate, False)

            verdicts[mode], _ = testing.captured(run)
        words = {
            name: (tree / name / "words.txt").read_text(encoding="utf-8").splitlines()
            for name in ("build", "build-bench-fuzz")
        }
        expect(
            "a change of a C++ test runs the suite of its configurations with --all, and the tests of the changed "
            "inputs in UBSan-strict and in the bench and fuzz build with no --all",
            verdicts["broken"].configurations == ("default", "release", "tsan", "ubsan-strict", "bench-fuzz")
            and words["build"][0].endswith("--all")
            and not any(line.endswith("--all") for line in words["build-bench-fuzz"]),
        )
        expect(
            "a test that is red alone too in that configuration rejects the commit",
            not verdicts["broken"].is_accepted
            and verdicts["broken"].problems[0].startswith("bench-fuzz: red alone too: test_broken"),
        )
        kept = (git.show(root, verdicts["keep"].final, SLOW_LEDGER) or b"").decode()
        expect(
            "with --keep, the gate writes the measured value of a kept row in that configuration, and accepts the "
            "commit",
            verdicts["keep"].is_accepted
            and "| 12 | - | the bench grows" in kept
            and verdicts["keep"].kept == ("slow-row: @build/bench/a.o in x86_64-debug-asan-bench-fuzz from 10 to 12",),
        )


def touched_cases(expect: testing.Expect) -> None:
    """Hold the runs with no full suite of a change with no C++ file: the touched tests, nothing, or each test."""
    with tempfile.TemporaryDirectory(prefix="gate-touched-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        previous = planted_repository(root)
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, previous)
        objects = ("default", "release", "tsan", "ubsan-strict", "bench-fuzz")
        cases = (
            ("the module of a rule that reads the build", BUILD_RULE_MODULE, objects, f"--only {BUILD_RULE_TEST}"),
            ("a self-test module that reads no build", "utils/assay/tests/sample/test_one.py", ("default",), ""),
            ("a module of the test launcher", LAUNCHER_MODULE, objects, "-j 1"),
        )
        for label, path, expected, shown in cases:
            commit = commit_change(root, path, "VALUE = 2\n", f"Change {path}")
            planted = planted_gate(work, root, tree, "clean", OTHER_BUILDS)
            candidate = gate.Candidate(previous, commit, (path,))

            def run(planted: gate.Gate = planted, candidate: gate.Candidate = candidate) -> gate.Verdict:
                return planted.check(candidate, False)

            verdict, printed = testing.captured(run)
            words = tree / "build-bench-fuzz" / "words.txt"
            lines = words.read_text(encoding="utf-8").splitlines() if words.is_file() else []
            words.unlink(missing_ok=True)
            only = planted.logs / "bench-fuzz.only.txt"
            last = lines[-1] if lines else ""
            if last.startswith("-j 1 --only ") and only.is_file():
                last = f"--only {only.read_text(encoding='utf-8').strip()}"
            steps = printed.count("memwait: ")
            holds = verdict.is_accepted and verdict.configurations == expected and last == shown and steps == 1
            label_text = (
                f"a change of {label} runs in {expected} with the words {shown!r} in the bench and fuzz build, in one "
                f"heavy step, and the gate gives {verdict.configurations} and {last!r} in {steps} heavy steps"
            )
            expect(label_text if holds else f"{label_text}.  The output of the gate:\n{printed}", holds)
            previous = commit


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
            with gate.tree_lock(work / "land", wait=False) as is_held:
                expect("with no wait, the lock tells at once that a different holder holds it", not is_held)
        with gate.tree_lock(work / "land", wait=False) as is_held:
            expect("with no wait, a free lock is held for the block", is_held)
        names = [gate.next_run(work / "land") for _ in range(gate.RUNS_KEPT + 2)]
        kept = [path.name for path in sorted((work / "land" / gate.LOGS).iterdir())]
        expect(
            "each run gets a new directory of logs one above the highest, and only the last directories of the rule stay",
            names[0] == f"{gate.RUN_MARK}{1:0{gate.RUN_WIDTH}d}"
            and len(set(names)) == len(names)
            and kept == names[-gate.RUNS_KEPT :],
        )
        planted = gate.Gate(work, work / "land", 1, False, run=names[-1])
        expect(
            "a gate of a run writes its logs into the directory of the run",
            planted.logs == work / "land" / gate.LOGS / names[-1]
            and gate.Gate(work, work / "land", 1, False).logs == work / "land" / gate.LOGS,
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            select_cases,
            table_cases,
            tree_cases,
            history_cases,
            change_cases,
            run_cases,
            falls_cases,
            falls_history_cases,
            keep_cases,
            changed_tests_cases,
            touched_cases,
            lock_cases,
        ),
    )
