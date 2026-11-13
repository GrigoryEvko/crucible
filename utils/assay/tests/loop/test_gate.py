"""The test of the gate of a landing (assay.loop.gate).

`guard selftest tests.loop.test_gate` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the selection of the configurations of a change.  They hold
the table of the configurations against the presets of this tree, and the
warm tree on a planted repository.  The planted repository
holds a registry with a rule that reads the build, the ledger of a counted
rule, and two planted masters.  The planted master loop writes a JUnit
report that a mode file of the build directory selects.  A planted ctest
lists the test of the rule that reads the build.  It also runs a red test
again alone.  The run of the gate then gives its verdict with no compiler
and no CMake.  A change of a C++ file must run the tests of the changed
inputs in each other configuration of objects.  A red test there must
reject the commit.  A change of the module of the rule that reads the build
must run that test in each other configuration of objects.  A landing that
builds each configuration must reduce each counted ledger over the builds,
and a landing that builds only some configurations must write no ledger.
The first run of each configuration of objects must end with the rule
reconfigure-noop, and a failure of that rule must reject the commit.  A
registry that the code of the gate cannot read must give the widest
selection and no reduce.  A candidate that names a built file must get the
write of that file from the build of default before the suite, and the fold
of the write into its commit.
"""

import fcntl
import json
import os
import sys
import tempfile
import time
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path

from assay.core import git, paths, testing
from assay.core import registry as rule_registry
from assay.loop import gate, heavy, syntax
from assay.loop import inputs as guard_inputs
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader

# The registry of the planted repository: one measured rule, one counted rule that a parse configuration checks, and a
# rule that reads the build.  It also holds the other rules that a parse configuration checks.  The counted rule home
# is there too, with its kind open-read, and no parse configuration checks it.
REGISTRY = (
    "format 1\n"
    "rule slow-row engine=build scope=builddir ledger=max limit=20 unit=s held-by=loop-gauge doc=XV | a time | "
    "lower the value\n"
    "rule door-unit engine=plugin scope=tracked ledger=count kinds=header-door doc=XXII.R4 | a door | move the door\n"
    "rule plan-check engine=meta scope=builddir ledger=none doc=XIII | a planted rule of the build | change nothing\n"
    "rule quarantine engine=plugin scope=tracked ledger=count kinds=std-object doc=XXII | a finding | remove it\n"
    "rule heavy-include engine=plugin scope=include/ ledger=count kinds=heavy-include doc=XV | a header | remove it\n"
    "rule syscalls engine=plugin scope=tracked ledger=count kinds=claim doc=XXII | a call | remove it\n"
    "rule home engine=plugin scope=tracked ledger=count kinds=open-read doc=XVI | a read | close the gate\n"
)
# The planted registry with the scope quarantined for the rule quarantine.  The inputs of the rule then read the rule
# table for each C++ file of the tree.  The registry and the rule table of a commit that adds a row of a kind that the
# code of the gate does not know (THE CAREFUL SELECTION).
QUARANTINED_REGISTRY = REGISTRY.replace(
    "rule quarantine engine=plugin scope=tracked", "rule quarantine engine=plugin scope=quarantined"
)
UNREADABLE_REGISTRY = QUARANTINED_REGISTRY + "budget a-row-of-a-new-kind\n"
RULE_TABLE = "utils/rule-table.txt"
UNREADABLE_TABLE = "planted-kind a::end_each | a row of a new kind\n"
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
# The ledger of the rule door-unit with no row, and with a row of the count 2.
DOOR_LEDGER = "format 1\nrule door-unit\n"
DOOR_PATH = "utils/ledgers/door-unit.txt"
COUNTED_DOORS = f"{DOOR_LEDGER}src/door.cpp | header-door | 2 | door.h | -\n"
# The ledger of the rule home.
HOME_LEDGER = "utils/ledgers/home.txt"
# The mint inventory of the cases of THE BUILT FILES: the copy that a composition keeps, and the copy that the planted
# build gives.
INVENTORY = "misc/mint-inventory.md"
STALE_INVENTORY = "| `mint_a` | HS14: 1 |\n"
BUILT_INVENTORY = "| `mint_a` | HS14: 2 |\n"
# The registry of the counted rules.  It holds two count ledgers with a row, an admit ledger, a count rule with no
# ledger file and a count rule of a different engine.
COUNTED_REGISTRY = (
    "format 1\n"
    "rule door-unit engine=plugin scope=tracked ledger=count kinds=header-door doc=XXII.R4 | a door | move the door\n"
    "rule erased-brand engine=tsast scope=tracked ledger=count doc=XXII | a brand | name the brand\n"
    "rule home engine=plugin scope=tracked ledger=count kinds=open-read doc=XVI | a read | close the gate\n"
    "rule mint engine=plugin scope=include/ ledger=admit kinds=mint doc=XXI | a mint | shape the mint\n"
    "rule padded-list engine=plugin scope=include/ ledger=count kinds=padded-member doc=XV.7 | a list | add a member\n"
)
# The planted master loop: `syntax --changed` names no preset, and `test BUILD` writes the report of the mode of the
# build directory.  The mode build gives a build that fails, and the mode late gives an error after a green run.  The
# master writes MAKEFLAGS to makeflags.txt, and the times of its start and its end to times.txt.  The mode slow runs
# for SLOW_S seconds, and the mode hang runs until a stop ends it.  The mode deep starts a child in a process group of
# its own with assay.core.proc of this tree (UTILS_MARK).  It writes the pid of the child to deep.pid.  The mode
# broken-after-deep waits for that file of the build of default, and then it is the mode broken.  `derive TARGET --write
# --build-dir BUILD` writes inventory.txt of the build into the tree as the mint inventory, or exits 3 in the mode
# unwritten.  With inventory.txt in the build, the test test_built is red when the tree holds a different inventory.
# Each call writes its first words to calls.txt of its build.
LOOP_MASTER = """import os
import pathlib
import sys
import time

words = sys.argv[1:]
if words[:2] == ["syntax", "--changed"]:
    sys.exit(0)
if words[0] == "derive":
    build = pathlib.Path(words[words.index("--build-dir") + 1])
    with open(build / "calls.txt", "a") as handle:
        handle.write(" ".join(words[:3]) + "\\n")
    if (build / "mode.txt").read_text().strip() == "unwritten":
        sys.exit(3)
    pathlib.Path("misc/mint-inventory.md").write_text((build / "inventory.txt").read_text())
    sys.exit(0)
build = pathlib.Path(words[1])
with open(build / "calls.txt", "a") as handle:
    handle.write(words[0] + "\\n")
began = time.time()
(build / "base.txt").write_text(os.environ.get("CRUCIBLE_CHANGE_BASE", "-"))
(build / "makeflags.txt").write_text(os.environ.get("MAKEFLAGS", "-"))
with open(build / "words.txt", "a") as handle:
    handle.write(" ".join(words[2:]) + "\\n")
mode = (build / "mode.txt").read_text().strip()
if mode == "slow":
    time.sleep(@SLOW_S@)
if mode == "hang":
    time.sleep(600)
if mode == "deep":
    sys.path.insert(0, "@UTILS@")
    from assay.core import proc

    running = proc.start(proc.Command(["sleep", "600"], timeout_s=900.0))
    (build / "deep.pid").write_text(str(running.pid))
    running.wait()
if mode == "broken-after-deep":
    deadline = time.time() + 30.0
    while not (build.parent / "build" / "deep.pid").is_file() and time.time() < deadline:
        time.sleep(0.02)
    mode = "broken"
if mode in ("greedy", "count"):
    descriptor = os.open(os.environ["MAKEFLAGS"].split("fifo:", 1)[1], os.O_RDWR | os.O_NONBLOCK)
    deadline = time.time() + 30.0
    while mode == "count" and not (build.parent / "build" / "greedy.held").is_file() and time.time() < deadline:
        time.sleep(0.02)
    held = b""
    end = time.time() + 1.5
    while time.time() < end:
        try:
            held += os.read(descriptor, 64)
        except BlockingIOError:
            time.sleep(0.01)
    (build / "tokens.txt").write_text(str(len(held)))
    (build / "greedy.held").write_text("1")
    deadline = time.time() + 30.0
    while mode == "greedy" and not (build.parent / "build-release" / "tokens.txt").is_file() and time.time() < deadline:
        time.sleep(0.02)
    os.write(descriptor, held)
    os.close(descriptor)
with open(build / "times.txt", "a") as handle:
    handle.write(f"{began} {time.time()}\\n")
if mode == "build":
    print("the build gives an error")
    sys.exit(1)
statuses = {"test_passes": "run"}
if mode in ("flaky", "broken"):
    statuses["test_" + mode] = "fail"
if mode == "both":
    statuses["test_flaky"] = statuses["test_broken"] = "fail"
if mode == "moves":
    ledger = pathlib.Path("utils/ledgers/home.txt").read_text()
    statuses["test_ledger"] = "fail" if "include/old.h" in ledger else "run"
inventory = build / "inventory.txt"
if inventory.is_file():
    held = pathlib.Path("misc/mint-inventory.md").read_text()
    statuses["test_built"] = "run" if held == inventory.read_text() else "fail"
cases = "".join(f'<testcase name="{name}" status="{status}"/>' for name, status in statuses.items())
cases += '<testcase name="test_skips" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/></testcase>'
report = build / "affected-tests" / "junit.xml"
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
sys.exit(1 if "fail" in statuses.values() or mode == "late" else 0)
"""
# The planted master guard: `warnings` gives 0, and `check` writes its rule and the base of the change, and does not
# apply.  In a build with the mode parse-falls, `check` gives findings when the commit of the warm tree holds the fall
# of the planted ledger, as after the fold of the gate.  In a build with the mode reconfigure, `check` of the rule
# reconfigure-noop gives findings.  `write` of door-unit lowers the count 2 of its ledger to 1, as the reduce of the
# gate does.  Each write writes its rule and the count of its build directories to writes.txt of each of them.
UTILS_MARK = "@UTILS@"
SLOW_MARK = "@SLOW_S@"
SLOW_S = 1.0
GUARD_MASTER = """import os
import pathlib
import subprocess
import sys

words = sys.argv[1:]
if words[0] == "check":
    build = pathlib.Path(words[4])
    with open(build / "checks.txt", "a", encoding="utf-8") as handle:
        handle.write(f"{words[2]} {os.environ.get('CRUCIBLE_CHANGE_BASE', '-')}\\n")
    mode = (build / "mode.txt").read_text().strip() if (build / "mode.txt").is_file() else ""
    if mode == "reconfigure" and words[2] == "reconfigure-noop":
        sys.exit(1)
    if mode == "parse-falls":
        shown = ["git", "show", "HEAD:utils/ledgers/door-unit.txt"]
        if "| 1 |" in subprocess.run(shown, capture_output=True, text=True, check=False).stdout:
            sys.exit(1)
if words[0] == "write":
    builds = [pathlib.Path(words[index + 1]) for index, word in enumerate(words) if word == "--build-dir"]
    if words[2] == "door-unit":
        ledger = pathlib.Path("utils/ledgers/door-unit.txt")
        ledger.write_text(ledger.read_text().replace("| 2 |", "| 1 |"))
    for build in builds:
        with open(build / "writes.txt", "a", encoding="utf-8") as handle:
            handle.write(f"{words[2]} {len(builds)}\\n")
    sys.exit(0)
sys.exit(0 if words[0] == "warnings" else 77)
"""
# The planted cmake: `cmake --build BUILD --target TARGET` writes its target to cmake.txt of the build, and exits 0.
CMAKE = """import pathlib
import sys

words = sys.argv[1:]
build = pathlib.Path(words[words.index("--build") + 1])
with open(build / "cmake.txt", "a", encoding="utf-8") as handle:
    handle.write(" ".join(words[words.index("--build") + 2 :]) + "\\n")
"""
# The planted ctest: the listing of a build is the file tests.json of the build.  A test that runs alone is green,
# unless it is test_broken, test_ledger before the write of its ledger, or test_built before the write of the inventory.  A fall of door-unit makes no test red,
# because a ledger fails only on a growth (THE POLICIES of assay.core.ledger).  Each run of red tests adds one line to
# alone-runs.txt of the build: the value of -j and the names of the tests.
CTEST = """import pathlib
import sys

words = sys.argv[1:]
if "--show-only=json-v1" in words:
    print(pathlib.Path(words[words.index("--test-dir") + 1], "tests.json").read_text())
    sys.exit(0)
names = pathlib.Path(words[words.index("--tests-from-file") + 1]).read_text().split()
jobs = words[words.index("-j") + 1] if "-j" in words else "-"
with open(pathlib.Path(words[words.index("--test-dir") + 1], "alone-runs.txt"), "a") as handle:
    handle.write(" ".join([jobs, *names]) + "\\n")
report = pathlib.Path(words[words.index("--output-junit") + 1])
red = {"test_broken"}
reads = pathlib.Path("utils/ledgers/home.txt")
red |= {"test_ledger"} if reads.is_file() and "include/old.h" in reads.read_text() else set()
inventory = pathlib.Path(words[words.index("--test-dir") + 1], "inventory.txt")
held = pathlib.Path("misc/mint-inventory.md")
red |= {"test_built"} if inventory.is_file() and held.read_text() != inventory.read_text() else set()
cases ="".join(f'<testcase name="{name}" status="{"fail" if name in red else "run"}"/>' for name in names)
report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{cases}</testsuite>')
"""
PLANTED = {
    "notes.txt": "notes at head\n",
    "utils/registry.txt": REGISTRY,
    DOOR_PATH: DOOR_LEDGER,
    "utils/bin/loop": LOOP_MASTER,
    "utils/bin/guard": GUARD_MASTER,
    BUILD_RULE_MODULE: "VALUE = 1\n",
}
# The available memory of the heavy step of a run.  With much memory, the step gets the jobs of the gate.  With a small
# memory, the step gets (3.0 - RESERVE_GB) / GB_PER_JOB = 2 jobs (THE MEMORY of assay.loop.heavy).
MUCH_MEMORY_GB = 1e6
SMALL_MEMORY_GB = 3.0
SMALL_MEMORY_JOBS = 2
# The slots of the planted pool of a case (THE JOBSERVER of assay.loop.gate).
POOL_SLOTS = 64


def planted_repository(root: Path, files: Mapping[str, str] | None = None) -> str:
    """Plant the repository in root with one commit, and return the commit.

    `files` replaces or adds some files of PLANTED.
    """
    root.mkdir(parents=True, exist_ok=True)
    git.init(root)
    for name, text in {**PLANTED, **(files or {})}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        marked = text.replace(SLOW_MARK, str(SLOW_S)).replace(UTILS_MARK, str(paths.REPO_ROOT / "utils"))
        path.write_text(marked, encoding="utf-8")
    return git.commit_all(root, "Add the planted tree")


def commit_change(root: Path, path: str, text: str, message: str = "Change a file") -> str:
    """Write one file of the planted repository, commit it, and return the commit."""
    file = root / path
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(text, encoding="utf-8")
    return git.commit_all(root, message)


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
        "objects with --build-only, and another change runs only the touched tests there, or nothing",
        [(run.mode, run.is_build_only) for run in changed] == [(gate.Mode.CHANGED, True)] * len(others)
        and [(run.mode, run.names) for run in touched] == [(gate.Mode.TOUCHED, ("guard_rule",))] * len(others)
        and gate.other_runs(others, False, ()) == ()
        and len(others) == 4,
    )


def table_cases(expect: testing.Expect) -> None:
    """Hold the table of the configurations against the presets of this tree (THE CONFIGURATIONS)."""
    names = [configuration.configuration_name for configuration in gate.CONFIGURATIONS]
    expect(
        f"each configuration of the gate names the configuration of its build, and no two name the same: {names}",
        all(names) and len(set(names)) == len(names),
    )
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
        try:
            gate.move_tree(root, root / "inner", head)
            expect("the gate refuses a warm tree in the work tree of the repository", False)
        except gate.GateError:
            expect("the gate refuses a warm tree in the work tree of the repository", True)


def change_cases(expect: testing.Expect) -> None:
    """Hold the change of a commit as the touch reads it, and the touch of the inputs of a test (THE TOUCH)."""
    with tempfile.TemporaryDirectory(prefix="gate-change-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        (root / "notes.txt").unlink()
        (root / rule_registry.REGISTRY_PATH).write_text(REGISTRY.replace("limit=20", "limit=21"), encoding="utf-8")
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
        item, gone, other = (str(tree / "data" / "item.txt"), str(tree / "data" / "gone.txt"), str(tree / "other.txt"))
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
    """Plant one build directory of the warm tree with a mode, a planted ctest, a planted cmake and its test listing."""
    build = tree / name
    build.mkdir(parents=True, exist_ok=True)
    ninja_reader.plant_graph(build, "")
    (build / "mode.txt").write_text(f"{mode}\n", encoding="utf-8")
    (build / "tests.json").write_text(listing(tree, build), encoding="utf-8")
    ctest = testing.planted_program(work, "ctest", CTEST)
    cmake = testing.planted_program(work, "cmake", CMAKE)
    cmake_reader.cache_file(build).write_text("", encoding="utf-8")
    cache = {"CMAKE_CTEST_COMMAND": ctest, "CMAKE_COMMAND": cmake, "CMAKE_HOME_DIRECTORY": str(tree)}
    cmake_reader.plant(build, cache=cache)


def planted_gate(work: Path, root: Path, tree: Path, mode: str, others: Mapping[str, str] | None = None) -> gate.Gate:
    """Plant the default build of the warm tree with a mode, and each other build with its mode, and return a gate.

    `others` maps the name of a build directory, such as build-bench-fuzz, to its mode.
    """
    for name, build_mode in {"build": mode, **(others or {})}.items():
        plant_build(work, tree, name, build_mode)
    return gate.Gate(root, work / "land", 1, False, read=lambda: MUCH_MEMORY_GB, pool=planted_pool(work))


def planted_pool(work: Path) -> heavy.Pool:
    """Return the planted slot pool of a case, with POOL_SLOTS slots, so no case takes the slots of the host."""
    return heavy.Pool(work / "slots", POOL_SLOTS)


def every_gate(work: Path, root: Path, tree: Path, modes: Mapping[str, str] | None = None) -> gate.Gate:
    """Plant the build of each configuration of the gate, and return a gate of --all-configurations.

    Each build has the mode clean, and `modes` maps the name of a build directory to a different mode.
    """
    others = {item.build: "clean" for item in gate.CONFIGURATIONS if item.build != "build"}
    planted = planted_gate(work, root, tree, "clean", {**others, **(modes or {})})
    return replace(planted, is_every=True)


def run_cases(expect: testing.Expect) -> None:
    """Hold the verdict of a run: a red test that is green alone, a test that is red alone too, and a build error.

    A second configure that compiles again rejects the commit too (RECONFIGURE_RULE).
    """
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
        for mode in ("clean", "flaky", "broken", "build", "late", "reconfigure"):
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
            "a red test that is green alone does not reject the commit, and the verdict and a line of the gate name it "
            "as a flaky test",
            verdicts["flaky"].is_accepted
            and verdicts["flaky"].flaky == ("default: test_flaky",)
            and "default: FLAKY, red in the suite and green alone: test_flaky" in outputs["flaky"]
            and verdicts["clean"].flaky == (),
        )
        expect(
            "a test that is red alone too rejects the commit, the problem names it, and the focus gives the test",
            not verdicts["broken"].is_accepted
            and "red alone too: test_broken" in verdicts["broken"].problems[0]
            and verdicts["broken"].focus == (gate.Focus("default", ("test_broken",)),),
        )
        expect(
            "a build error rejects the commit, and the focus gives the configuration with no test",
            not verdicts["build"].is_accepted
            and "the build or the test run gives an error" in verdicts["build"].problems[0]
            and verdicts["build"].focus == (gate.Focus("default"),),
        )
        expect("an accepted commit has no focus", verdicts["clean"].focus == ())
        expect(
            "an exit other than 0 after a run with no red test rejects the commit",
            not verdicts["late"].is_accepted and "with no red test" in verdicts["late"].problems[0],
        )
        reconfigured = verdicts["reconfigure"]
        expect(
            f"a second configure that compiles again rejects the commit, and the focus gives the configuration with no "
            f"test: {reconfigured.problems}",
            not reconfigured.is_accepted
            and reconfigured.problems == (reconfigured.problems[0],)
            and reconfigured.problems[0].startswith("default: the rule reconfigure-noop gives findings")
            and reconfigured.focus == (gate.Focus("default"),),
        )
        expect("each run leaves the warm tree at the commit", git.head(tree) == commit)
        build = tree / "build"
        checks = (build / "checks.txt").read_text(encoding="utf-8").splitlines()
        expect(
            f"the suite of one commit gets no base of the change, and only the rule reconfigure-noop runs after it, "
            f"one time in each run with no other problem: {checks}",
            (build / "base.txt").read_text(encoding="utf-8") == "-" and checks == ["reconfigure-noop -"] * 3,
        )
        (build / "checks.txt").unlink()
        commit_change(root, "notes.txt", "the first of two commits\n")
        second = commit_change(root, "notes.txt", "the second of two commits\n")
        stacked = planted_gate(work, root, tree, "clean")

        def run_stacked(planted: gate.Gate = stacked) -> gate.Verdict:
            return planted.check(gate.Candidate(commit, second, ("notes.txt",)), False)

        verdict, _ = testing.captured(run_stacked)
        expect(
            "the suite of two commits gets no base of the change either, and only the rule reconfigure-noop runs",
            verdict.is_accepted
            and (build / "base.txt").read_text(encoding="utf-8") == "-"
            and (build / "checks.txt").read_text(encoding="utf-8") == "reconfigure-noop -\n",
        )
        alone_runs = build / "alone-runs.txt"
        alone_runs.unlink(missing_ok=True)
        parallel = replace(planted_gate(work, root, tree, "both"), jobs=4)

        def run_both(planted: gate.Gate = parallel) -> gate.Verdict:
            return planted.check(candidate, False)

        both, _ = testing.captured(run_both)
        runs = alone_runs.read_text(encoding="utf-8").splitlines() if alone_runs.is_file() else []
        expect(
            f"two red tests run again at the same time, in one run of ctest with the jobs of the step, and each keeps "
            f"its own verdict: {runs}",
            runs == [f"{POOL_SLOTS} test_broken test_flaky"]
            and not both.is_accepted
            and "red alone too: test_broken," in both.problems[0]
            and both.focus == (gate.Focus("default", ("test_broken",)),)
            and both.flaky == ("default: test_flaky",),
        )


def reduce_cases(expect: testing.Expect) -> None:
    """Hold THE REDUCE: a landing that builds each configuration lowers a counted ledger over the builds, and folds it."""
    with tempfile.TemporaryDirectory(prefix="gate-reduce-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {DOOR_PATH: COUNTED_DOORS})
        commit = commit_change(root, "notes.txt", "a change that removes one door\n", "Remove a door\n\nThe body.")
        candidate = gate.Candidate(head, commit, ("notes.txt",))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = every_gate(work, root, tree)
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        every = tuple(item.name for item in gate.CONFIGURATIONS)
        checkers = sum(1 for item in gate.CONFIGURATIONS if gate.is_checked(item, "door-unit"))
        writes = tree / "build" / "writes.txt"
        written = writes.read_text(encoding="utf-8").splitlines() if writes.is_file() else []
        holds = verdict.is_accepted and verdict.configurations == every + every and written == [f"door-unit {checkers}"]
        label = (
            f"a landing that builds each configuration with no problem runs one write of each counted rule, with the "
            f"build of each configuration that checks it: the gate runs {verdict.configurations} and writes {written}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        expect(
            "the gate folds the lowered ledger into a new commit with the parents and the message of the commit, and "
            "the warm tree holds that commit",
            verdict.final not in ("", commit)
            and verdict.ledgers == (DOOR_PATH,)
            and git.show(root, verdict.final, DOOR_PATH) == COUNTED_DOORS.replace("| 2 |", "| 1 |").encode()
            and git.show(root, verdict.final, "notes.txt") == b"a change that removes one door\n"
            and git.parent(root, verdict.final) == head
            and git.message_of(root, verdict.final) == git.message_of(root, commit)
            and git.head(tree) == verdict.final,
        )
        expect(
            "after the fold, the tests run again on the new commit",
            "folded into the commit" in printed and "each configuration of the run runs again" in printed,
        )
        logs = planted.logs
        rounds = sorted(path.name for path in logs.glob(f"{gate.ALWAYS}*.test.log"))
        expect(
            f"the round after the fold writes its own logs, and the logs of the first round stay: {rounds}",
            rounds == [f"{gate.ALWAYS}{gate.AGAIN_MARK}.test.log", f"{gate.ALWAYS}.test.log"],
        )
        checks = (tree / "build" / "checks.txt").read_text(encoding="utf-8").splitlines()
        expect(
            f"the rule reconfigure-noop runs after the first build of the run, and not again after the fold: {checks}",
            checks == ["reconfigure-noop -"],
        )


def partial_cases(expect: testing.Expect) -> None:
    """Hold THE REDUCE of a landing that builds only some configurations: the gate and its legs write no ledger."""
    with tempfile.TemporaryDirectory(prefix="gate-partial-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {DOOR_PATH: COUNTED_DOORS})
        commit = commit_change(root, "notes.txt", "a change that removes one door\n", "Remove a door\n\nThe body.")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "clean")
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",)), False))
        holds = (
            verdict.is_accepted
            and verdict.configurations == ("default",)
            and verdict.final == commit
            and not verdict.ledgers
            and not (tree / "build" / "writes.txt").is_file()
            and "lowers no row" in printed
        )
        label = (
            f"a landing that builds only some configurations lowers no row, and the gate keeps the commit: the gate "
            f"runs {verdict.configurations} and folds {verdict.ledgers}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        reads = "format 1\nrule home\ninclude/old.h | open-read | 2 | a::trait_v | -\n"
        moved = commit_change(root, HOME_LEDGER, reads, "Count two open reads")
        divided = commit_change(root, "notes.txt", "a change that divides include/old.h\n", "Divide a header")
        planted = every_gate(work, root, tree, {"build": "moves"})
        verdict, printed = testing.captured(
            lambda: planted.check(gate.Candidate(moved, divided, ("notes.txt",)), False)
        )
        holds = (
            not verdict.is_accepted
            and any("red alone too: test_ledger" in problem for problem in verdict.problems)
            and not verdict.ledgers
            and not (tree / "build" / "writes.txt").is_file()
        )
        label = (
            f"a commit whose suite is red on a moved row is a refusal, because a leg writes no ledger: the change "
            f"writes the raise before the landing.  The gate gives {verdict.problems[:1]} and folds {verdict.ledgers}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def counted_cases(expect: testing.Expect) -> None:
    """Hold THE COUNTED RULES, the words of the reduce of a counted rule, and is_each_built() (THE REDUCE)."""
    with tempfile.TemporaryDirectory(prefix="gate-counted-") as work_text:
        root = Path(work_text)
        files = {
            rule_registry.REGISTRY_PATH: COUNTED_REGISTRY,
            DOOR_PATH: COUNTED_DOORS,
            HOME_LEDGER: "format 1\nrule home\ninclude/old.h | open-read | 2 | a::trait_v | -\n",
            "utils/ledgers/mint.txt": "format 1\nrule mint\ninclude/a.h | mint | admit | a::mint_a | a hatch\n",
            "utils/ledgers/padded-list.txt": "format 1\nrule padded-list\n",
            "utils/ledgers/erased-brand.txt": "format 1\nrule erased-brand\nc.h | - | 1 | z | -\n",
        }
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text, encoding="utf-8")
        counted = gate.counted_rules(root)
        expect(
            f"a rule of the engine plugin or elf with the policy count or admit and a ledger with a row is counted, and "
            f"a ledger with no row and a rule of a different engine are not: {counted}",
            counted == ("door-unit", "home", "mint"),
        )
    tree = Path("/planted/tree")
    every = [str(gate.build_directory(tree, item)) for item in gate.CONFIGURATIONS]
    objects = [str(gate.build_directory(tree, item)) for item in gate.CONFIGURATIONS if not item.is_parse]
    expect(
        "the reduce of a rule that a parse configuration checks gets the build of each configuration, and the reduce "
        "of a different rule gets the build of each configuration of objects",
        gate.reduce_words(tree, "door-unit")
        == ["write", "--rule", "door-unit", *(word for build in every for word in ("--build-dir", build))]
        and gate.reduce_words(tree, "home")
        == ["write", "--rule", "home", *(word for build in objects for word in ("--build-dir", build))]
        and len(objects) < len(every),
    )
    built = [gate.Outcome(item.name, (), (), 1.0, False) for item in gate.CONFIGURATIONS]
    first = built[0].name
    expect(
        "a round builds each configuration when each configuration ends with no problem, and not when one "
        "configuration is missing, stopped or red",
        gate.is_each_built(built)
        and not gate.is_each_built(built[1:])
        and not gate.is_each_built([*built[1:], gate.Outcome(first, (), (), 1.0, True)])
        and not gate.is_each_built([*built[1:], gate.Outcome(first, ("a problem",), (), 1.0, False)]),
    )


def reduce_parse_cases(expect: testing.Expect) -> None:
    """Hold THE REDUCE with the parse configurations: a folded ledger that makes their check red rejects the commit."""
    with tempfile.TemporaryDirectory(prefix="gate-reduce-parse-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {DOOR_PATH: COUNTED_DOORS})
        commit = commit_change(root, "notes.txt", "a change that removes one door\n", "Remove a door")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        names = tuple(item.name for item in gate.CONFIGURATIONS if item.is_parse)
        every = tuple(item.name for item in gate.CONFIGURATIONS)
        planted = every_gate(work, root, tree, {gate.BY_NAME[names[0]].build: "parse-falls"})
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",)), False))
        holds = (
            not verdict.is_accepted
            and verdict.configurations[: len(every)] == every
            and names[0] in verdict.configurations[len(every) :]
            and all(problem.startswith(f"{names[0]}: the rule ") for problem in verdict.problems)
            and verdict.focus == (gate.Focus(names[0]),)
        )
        label = (
            f"after the fold, a parse configuration checks again, and a folded ledger that makes its check red rejects "
            f"the commit, with the focus of that configuration: the gate runs {verdict.configurations} and gives "
            f"{verdict.problems[:1]}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def calls_of(build: Path) -> list[str]:
    """Return the first words of each call of the planted master loop in a build, from calls.txt, and remove the file."""
    calls = build / "calls.txt"
    found = calls.read_text(encoding="utf-8").splitlines() if calls.is_file() else []
    calls.unlink(missing_ok=True)
    return found


def built_cases(expect: testing.Expect) -> None:
    """Hold THE BUILT FILES: the gate writes a merged generated file from the build of default before the suite."""
    with tempfile.TemporaryDirectory(prefix="gate-built-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {INVENTORY: STALE_INVENTORY, DOOR_PATH: COUNTED_DOORS})
        commit = commit_change(root, "notes.txt", "a change with a merged inventory\n", "Add a fixture\n\nThe body.")
        candidate = gate.Candidate(head, commit, (INVENTORY, "notes.txt"), built=(INVENTORY,))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "clean")
        build = tree / "build"
        (build / "inventory.txt").write_text(BUILT_INVENTORY, encoding="utf-8")
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        calls = calls_of(build)
        builds = (build / "cmake.txt").read_text(encoding="utf-8").splitlines()
        holds = (
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and calls == ["derive mint-inventory --write", "test"]
            and builds[:1] == [""]
        )
        label = (
            f"the gate builds the target all of default with no -j, writes the merged inventory, and then runs the "
            f"suite, which judges the written inventory: the calls {calls} and the builds {builds}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        note = f"The gate wrote {INVENTORY} from the build of the composed tree."
        expect(
            "the gate folds the written inventory into a new commit with the parents of the commit, the message of the "
            "commit tells that the gate wrote the inventory, the warm tree holds that commit, and a line tells it",
            verdict.final not in ("", commit)
            and git.show(root, verdict.final, INVENTORY) == BUILT_INVENTORY.encode()
            and git.show(root, verdict.final, "notes.txt") == b"a change with a merged inventory\n"
            and git.parent(root, verdict.final) == head
            and git.message_of(root, verdict.final) == f"{git.message_of(root, commit).rstrip()}\n\n{note}\n"
            and git.head(tree) == verdict.final
            and f"the gate wrote {INVENTORY} from the build of default" in printed,
        )
        every = every_gate(work, root, tree)
        verdict, printed = testing.captured(lambda: every.check(candidate, False))
        calls_of(build)
        holds = (
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and verdict.ledgers == (DOOR_PATH,)
            and git.show(root, verdict.final, INVENTORY) == BUILT_INVENTORY.encode()
            and git.show(root, verdict.final, DOOR_PATH) == COUNTED_DOORS.replace("| 2 |", "| 1 |").encode()
            and git.message_of(root, verdict.final).count(note) == 1
        )
        label = (
            f"with THE REDUCE, the last commit holds the written inventory and the lowered ledger, and its message "
            f"names the write one time: the gate writes {verdict.built} and folds {verdict.ledgers}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        unwritten = planted_gate(work, root, tree, "unwritten")
        verdict, printed = testing.captured(lambda: unwritten.check(candidate, False))
        calls = calls_of(build)
        holds = (
            not verdict.is_accepted
            and verdict.problems[:1] != ()
            and verdict.problems[0].startswith(f"default: the gate cannot write {INVENTORY}")
            and verdict.focus == (gate.Focus("default"),)
            and not verdict.built
            and calls == ["derive mint-inventory --write"]
        )
        label = (
            f"a write that gives an error rejects the commit with the focus of default, and the suite does not run: "
            f"the gate gives {verdict.problems} after the calls {calls}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        stale = commit_change(root, INVENTORY, "| mint_a | HS14: 3 |\n", "Change the inventory by hand")
        clean = planted_gate(work, root, tree, "clean")
        verdict, printed = testing.captured(lambda: clean.check(gate.Candidate(commit, stale, (INVENTORY,)), False))
        calls = calls_of(build)
        holds = (
            not verdict.is_accepted
            and any("red alone too: test_built" in problem for problem in verdict.problems)
            and not verdict.built
            and calls == ["test"]
        )
        label = (
            f"a candidate that names no built file gets no write, and the suite rejects an inventory that the build "
            f"does not give: the calls {calls} and the problems {verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (build / "inventory.txt").write_text(STALE_INVENTORY, encoding="utf-8")
        verdict, printed = testing.captured(lambda: clean.check(candidate, False))
        holds = verdict.is_accepted and verdict.final == commit and not verdict.built
        label = (
            f"when the build gives the bytes of the commit, the gate folds nothing and keeps the commit: the gate "
            f"writes {verdict.built}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        try:
            clean.check(replace(candidate, built=("misc/other.md",)), False)
            refusal = ""
        except gate.GateError as problem:
            refusal = str(problem)
        expect(
            f"a candidate that names a file that no row of BUILT_FILES gives is an error of the gate: {refusal}",
            "misc/other.md" in refusal,
        )


def changed_tests_cases(expect: testing.Expect) -> None:
    """Hold a configuration of the tests of the changed inputs: no --all, and a red test rejects."""
    with tempfile.TemporaryDirectory(prefix="gate-changed-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        verdicts: dict[str, gate.Verdict] = {}
        words: dict[str, list[str]] = {}
        for mode in ("clean", "broken"):
            others = {**OTHER_BUILDS, "build-bench-fuzz": mode}
            planted = planted_gate(work, root, tree, "clean", others)
            candidate = gate.Candidate(head, commit, ("test/a.cpp",))

            def run(planted: gate.Gate = planted, candidate: gate.Candidate = candidate) -> gate.Verdict:
                return planted.check(candidate, False)

            verdicts[mode], _ = testing.captured(run)
            if mode == "clean":
                for name in ("build", "build-bench-fuzz"):
                    words[name] = (tree / name / "words.txt").read_text(encoding="utf-8").splitlines()
        expect(
            "a change of a C++ test runs the suite of its configurations with --all, and the tests of the changed "
            "inputs in UBSan-strict and in the bench and fuzz build with no --all",
            verdicts["clean"].is_accepted
            and verdicts["clean"].configurations == ("default", "release", "tsan", "ubsan-strict", "bench-fuzz")
            and words["build"][0].endswith("--all")
            and not any("--all" in line.split() for line in words["build-bench-fuzz"]),
        )
        expect(
            "default runs each test of the master guard that reads no build, and each other configuration gets "
            "--build-only",
            not any("--build-only" in line.split() for line in words["build"])
            and bool(words["build-bench-fuzz"])
            and all(line.endswith("--build-only") for line in words["build-bench-fuzz"]),
        )
        broken = verdicts["broken"]
        expect(
            f"a test that is red alone too in that configuration rejects the commit, with the focus of its test, and "
            f"the stop ends no run with a problem of its own: {broken.problems}",
            not broken.is_accepted
            and broken.problems == (broken.problems[0],)
            and broken.problems[0].startswith("bench-fuzz: red alone too: test_broken")
            and broken.focus == (gate.Focus("bench-fuzz", ("test_broken",)),)
            and "bench-fuzz" in broken.configurations,
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
            ("a module of the test launcher", LAUNCHER_MODULE, objects, f"-j {POOL_SLOTS} --build-only"),
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
            checked = tree / "build-bench-fuzz" / "checks.txt"
            checks = checked.read_text(encoding="utf-8").splitlines() if checked.is_file() else []
            checked.unlink(missing_ok=True)
            only = planted.logs / "bench-fuzz.only.txt"
            last = lines[-1] if lines else ""
            if last.startswith(f"-j {POOL_SLOTS} --only ") and only.is_file():
                last = f"--only {only.read_text(encoding='utf-8').strip()}"
            leases = printed.count("memory: ")
            reconfigured = ["reconfigure-noop -"] if lines else []
            holds = (
                verdict.is_accepted
                and verdict.configurations == expected
                and last == shown
                and leases == len(expected)
                and checks == reconfigured
            )
            label_text = (
                f"a change of {label} runs in {expected} with the words {shown!r} in the bench and fuzz build, and the "
                f"rule reconfigure-noop after each run in that build, each configuration with a lease.  The gate gives "
                f"{verdict.configurations}, {last!r} and the checks {checks} with {leases} leases"
            )
            expect(label_text if holds else f"{label_text}.  The output of the gate:\n{printed}", holds)
            previous = commit


def times_of(build: Path) -> list[tuple[float, float]]:
    """Return the start and the end of each run of the planted master loop in a build, from times.txt."""
    path = build / "times.txt"
    if not path.is_file():
        return []
    return [(float(first), float(last)) for first, last in (line.split() for line in path.read_text().splitlines())]


def thread_cases(expect: testing.Expect) -> None:
    """Hold THE JOBSERVER and THE THREADS: each configuration gets MAKEFLAGS, and the configurations overlap."""
    with tempfile.TemporaryDirectory(prefix="gate-threads-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        builds = ("build", "build-release", "build-tsan", "build-ubsan-strict", "build-bench-fuzz")
        planted = planted_gate(work, root, tree, "slow", dict.fromkeys(builds[1:], "slow"))
        wide = replace(planted, jobs=8)
        verdict, printed = testing.captured(lambda: wide.check(gate.Candidate(head, commit, ("test/a.cpp",)), False))
        flags = [(tree / name / "makeflags.txt").read_text(encoding="utf-8") for name in builds]
        fifos = [Path(text.split("fifo:", 1)[1]) for text in flags if "fifo:" in text]
        spans = [span for name in builds for span in times_of(tree / name)]
        expect(
            f"each configuration gets MAKEFLAGS of the jobserver of a lease of its own with the slice of the gate, "
            f"and each FIFO goes at the end of the check: {flags[0]}",
            verdict.is_accepted
            and all(text.startswith(f"-j{POOL_SLOTS} --jobserver-auth=fifo:") for text in flags)
            and len(set(fifos)) == len(builds)
            and not any(fifo.exists() for fifo in fifos)
            and len(verdict.wait_seconds) == len(verdict.slot_seconds) == len(builds),
        )
        expect(
            f"the configurations of the full suite and the other configurations run at the same time, each with a "
            f"lease: {len(spans)} runs, {printed.count('memory: ')} reads of the memory",
            len(spans) == len(builds)
            and max(first for first, _ in spans) < min(last for _, last in spans)
            and printed.count("memory: ") == len(builds),
        )
        greedy = replace(planted_gate(work, root, tree, "greedy", {"build-release": "count"}), jobs=8)
        verdict, printed = testing.captured(lambda: greedy.check(gate.Candidate(head, commit, ("test/a.cpp",)), False))
        taken = [(tree / name / "tokens.txt").read_text(encoding="utf-8") for name in ("build", "build-release")]
        holds = verdict.is_accepted and int(taken[0]) < POOL_SLOTS and int(taken[1]) >= heavy.START_SLOTS - 1
        label = (
            f"a configuration that takes each token that it can does not take the tokens of a different "
            f"configuration: the greedy one holds {taken[0]}, and the other one gets {taken[1]}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        rule_commit = commit_change(root, BUILD_RULE_MODULE, "VALUE = 3\n", "Change the rule")
        early = replace(planted_gate(work, root, tree, "slow", dict.fromkeys(builds[1:], "clean")), jobs=8)
        for name in builds:
            (tree / name / "times.txt").unlink(missing_ok=True)
        verdict, printed = testing.captured(
            lambda: early.check(gate.Candidate(commit, rule_commit, (BUILD_RULE_MODULE,)), False)
        )
        always = times_of(tree / "build")
        bench = times_of(tree / "build-bench-fuzz")
        regenerated = (tree / "build" / "cmake.txt").read_text(encoding="utf-8").splitlines()
        holds = (
            verdict.is_accepted
            and regenerated[-1:] == ["--target build.ninja"]
            and len(always) == 1
            and len(bench) == 1
            and bench[0][0] < always[0][1]
        )
        label = (
            "the touched tests of the other configurations start when the build of ALWAYS holds its configure, before "
            f"the run of ALWAYS ends: the targets {regenerated}, the run of default {always} and of bench-fuzz {bench}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def memory_cases(expect: testing.Expect) -> None:
    """Hold THE JOBSERVER with a small memory: the jobserver and each -j get the jobs that the memory holds."""
    with tempfile.TemporaryDirectory(prefix="gate-memory-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "notes.txt", "a change of the notes\n")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = replace(planted_gate(work, root, tree, "clean"), jobs=8, read=lambda: SMALL_MEMORY_GB)
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("notes.txt",)), False))
        build = tree / "build"
        flags = (build / "makeflags.txt").read_text(encoding="utf-8")
        words = (build / "words.txt").read_text(encoding="utf-8").splitlines()
        holds = (
            verdict.is_accepted
            and flags.startswith(f"-j{SMALL_MEMORY_JOBS} --jobserver-auth=fifo:")
            and words[-1:] == [f"-j {SMALL_MEMORY_JOBS} --all"]
            and f"memory: available {SMALL_MEMORY_GB:.1f} GB, {SMALL_MEMORY_JOBS} of 8 jobs" in printed
        )
        label = (
            f"with {SMALL_MEMORY_GB} GB of available memory and 8 jobs of the gate, the jobserver and `loop test` get "
            f"{SMALL_MEMORY_JOBS} jobs: MAKEFLAGS {flags!r} and the words {words[-1:]}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def stop_cases(expect: testing.Expect) -> None:
    """Hold THE STOP: the first problem stops each configuration that runs, and only its problem is in the verdict."""
    with tempfile.TemporaryDirectory(prefix="gate-stop-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        others = {**dict.fromkeys(OTHER_BUILDS, "hang"), "build-bench-fuzz": "broken-after-deep"}
        planted = replace(planted_gate(work, root, tree, "deep", others), jobs=8)
        started = time.monotonic()
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("test/a.cpp",)), False))
        elapsed_s = time.monotonic() - started
        hung = [
            name for name in ("build", "build-release", "build-tsan", "build-ubsan-strict") if times_of(tree / name)
        ]
        pid_file = tree / "build" / "deep.pid"
        grandchild = int(pid_file.read_text(encoding="utf-8")) if pid_file.is_file() else -1
        holds = (
            not verdict.is_accepted
            and verdict.configurations == ("bench-fuzz",)
            and verdict.problems == (verdict.problems[0],)
            and verdict.problems[0].startswith("bench-fuzz: red alone too: test_broken")
            and elapsed_s < 60.0
            and not hung
            and "the stop ends the run" in printed
            and grandchild > 0
            and testing.is_gone(grandchild)
        )
        label = (
            f"a red test stops each configuration that runs at that time, also the child of a master in a process "
            f"group of its own, and the verdict gives only its problem: the gate gives {verdict.configurations} and "
            f"{verdict.problems[:1]} after {elapsed_s:.1f} s"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def masked_error_cases(expect: testing.Expect) -> None:
    """Hold THE STOP: a thread that an error stopped does not hide the error, and a stop with no problem is an error.

    A stop also ends the wait of a configuration for its first slot (THE JOBSERVER).
    """
    with tempfile.TemporaryDirectory(prefix="gate-masked-") as work_text:
        work = Path(work_text)
        pool = heavy.Pool(work / "slots", 1)
        planted = gate.Gate(work, work / "land", 2, False, pool=pool)
        step = gate.Step(planted, gate.Candidate("a" * 40, "b" * 40, ()), {})

        def work_of(index: int) -> int:
            """Wait for the stop in the first item and give Stopped, and give an error in the second item."""
            if index == 0:
                while not step.stop.is_set():
                    time.sleep(0.01)
                raise gate.Stopped
            raise gate.GateError("a planted error of a step")

        try:
            step.concurrently([0, 1], work_of)
            raised = ""
        except gate.Stopped:
            raised = "Stopped"
        except gate.GateError as problem:
            raised = str(problem)
        expect(
            f"the error of a thread goes to the caller, also when an earlier item gives Stopped: {raised!r}",
            raised == "a planted error of a step" and step.stop.is_set(),
        )
        try:
            step.require_problem([gate.Outcome("default", (), (), 1.0, True)])
            refused = ""
        except gate.GateError as problem:
            refused = str(problem)
        expect(
            f"a check that the stop ended with no problem of a configuration is an error: {refused!r}",
            "stopped with no problem" in refused,
        )
        outcomes = [gate.Outcome("default", ("default: a problem",), (), 1.0, False)]
        step.require_problem(outcomes)
        expect("a stop with a problem of a configuration is no error", True)

        def stopped_wait() -> str:
            """Hold the one slot of the pool, and wait for a lease of the step with its stop event set."""
            with heavy.leased(work, 1, pool, lambda: MUCH_MEMORY_GB) as busy:
                if busy is None:
                    return "no slot for the holder"
                try:
                    with step.leased():
                        return "a slot"
                except gate.Stopped:
                    return "Stopped"

        waited, _ = testing.captured(stopped_wait)
        expect(
            f"with each slot of the pool held, a set stop event ends the wait for the first slot: {waited}",
            waited == "Stopped",
        )


def base_run_cases(expect: testing.Expect) -> None:
    """Hold THE RUN OF THE BASE: the gate runs only the focus, with the words of the problems of the batch."""
    with tempfile.TemporaryDirectory(prefix="gate-base-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        planted_repository(root)
        head = git.head(root)
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        others = {**OTHER_BUILDS, "build-release": "broken"}
        batch = planted_gate(work, root, tree, "clean", others)
        batch_verdict, _ = testing.captured(lambda: batch.check(gate.Candidate(head, commit, ("test/a.cpp",)), False))
        for name in ("build", "build-release", "build-tsan", "build-ubsan-strict", "build-bench-fuzz"):
            (tree / name / "words.txt").unlink(missing_ok=True)
        focus = batch_verdict.focus + (gate.Focus("bench-fuzz"),)
        base = planted_gate(work, root, tree, "clean", others)
        base_verdict, printed = testing.captured(
            lambda: base.check(gate.Candidate(head, head, ("test/a.cpp",), focus), True)
        )
        ran = {
            name: (tree / name / "words.txt").read_text(encoding="utf-8").splitlines()
            for name in ("build", "build-release", "build-tsan", "build-bench-fuzz")
            if (tree / name / "words.txt").is_file()
        }
        holds = (
            batch_verdict.focus == (gate.Focus("release", ("test_broken",)),)
            and set(ran) == {"build-release", "build-bench-fuzz"}
            and ran["build-release"][-1].startswith(f"-j {POOL_SLOTS} --only ")
            and ran["build-bench-fuzz"] == [f"-j {POOL_SLOTS}"]
            and base_verdict.configurations == ("release", "bench-fuzz")
            and base_verdict.problems == batch_verdict.problems
            and base_verdict.final == head
            and not base_verdict.ledgers
            and printed.count("memory: ") == len(base_verdict.configurations)
        )
        label = (
            f"the run of the base runs the red tests of the focus with --only, a configuration with no red test in the "
            f"mode of the changed inputs, and no other configuration.  Its problems are those of the batch: the batch "
            f"gives {batch_verdict.focus}, and the base runs {sorted(ran)} with {base_verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        notes = commit_change(root, "notes.txt", "a change of the notes\n")
        reconfigured = planted_gate(work, root, tree, "reconfigure", OTHER_BUILDS)
        batch_verdict, _ = testing.captured(
            lambda: reconfigured.check(gate.Candidate(commit, notes, ("notes.txt",)), False)
        )
        base_verdict, printed = testing.captured(
            lambda: reconfigured.check(gate.Candidate(commit, commit, ("notes.txt",), batch_verdict.focus), True)
        )
        holds = (
            batch_verdict.focus == (gate.Focus("default"),)
            and base_verdict.configurations == ("default",)
            and base_verdict.problems == batch_verdict.problems
            and base_verdict.problems[0].startswith("default: the rule reconfigure-noop gives findings")
        )
        label = (
            f"a problem of the rule reconfigure-noop gives the focus of its configuration, and the run of the base "
            f"runs the rule again with the words of the batch: the batch gives {batch_verdict.problems}, and the base "
            f"gives {base_verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def careful_cases(expect: testing.Expect) -> None:
    """Hold THE CAREFUL SELECTION: a file that the code of the gate cannot read gives the widest result of its read."""
    with tempfile.TemporaryDirectory(prefix="gate-careful-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {rule_registry.REGISTRY_PATH: QUARANTINED_REGISTRY, "src/unit.cpp": "int a;\n"})
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = replace(every_gate(work, root, tree), is_every=False)
        table = commit_change(root, RULE_TABLE, UNREADABLE_TABLE, "Add a row of a new kind to the rule table")
        candidate = gate.Candidate(head, table, (RULE_TABLE,))
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        every = {item.name for item in gate.CONFIGURATIONS}
        holds = (
            verdict.is_accepted
            and set(verdict.configurations) == every
            and "the code of the gate cannot read the inputs of the rules of the parse builds" in printed
        )
        label = (
            f"a rule table with a row that the code of the gate cannot read gives each parse configuration, and the "
            f"commit gets its verdict: the gate runs {verdict.configurations}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        commit = commit_change(root, rule_registry.REGISTRY_PATH, UNREADABLE_REGISTRY, "Add a row of a new kind")
        candidate = gate.Candidate(table, commit, (rule_registry.REGISTRY_PATH,))
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        writes = [tree / item.build / "writes.txt" for item in gate.CONFIGURATIONS]
        holds = (
            verdict.is_accepted
            and "the code of the gate cannot read the counted ledgers" in printed
            and not any(path.is_file() for path in writes)
            and verdict.final == commit
        )
        label = (
            f"a registry with a row that the code of the gate cannot read gives no reduce, and the commit gets its "
            f"verdict: the gate runs {verdict.configurations}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


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
        lanes: list[int | None] = []
        with gate.free_lane(work / "land") as first_lane:
            lanes.append(first_lane)
            with gate.free_lane(work / "land") as second_lane:
                lanes.append(second_lane)
                with gate.free_lane(work / "land") as third_lane, gate.free_lane(work / "land") as no_lane:
                    lanes += [third_lane, no_lane]
        with open(work / "land" / gate.LOCK, "a", encoding="ascii") as earlier:
            fcntl.flock(earlier, fcntl.LOCK_EX)
            with gate.free_lane(work / "land") as beside_earlier:
                lanes.append(beside_earlier)
        expect(
            f"each landing takes the first free lane, the lanes end at MAX_LANES, and a holder of the first lane with "
            f"no lock of PRIMARY, such as an earlier copy, leaves no lane free: {lanes}",
            lanes == [0, 1, 2, None, None]
            and gate.Gate(work, work / "land", 1, False, lane=2).tree == work / "land" / "tree-3",
        )
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
            change_cases,
            run_cases,
            reduce_cases,
            partial_cases,
            counted_cases,
            reduce_parse_cases,
            built_cases,
            changed_tests_cases,
            touched_cases,
            thread_cases,
            memory_cases,
            stop_cases,
            masked_error_cases,
            base_run_cases,
            careful_cases,
            lock_cases,
        ),
    )
