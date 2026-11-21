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
of the write into its commit.  A change that adds a fixture must get that
write too, while the other configurations build, and each configuration
must run its suite after the fold.  A focus with no run of default must get
the write before each configuration, and the run of the base must get no
write.  A red test of the plugin rules must give the rows that the build
gives for each counted plugin rule, with no row of a refused kind, and the
ledgers of the warm tree must keep the bytes of the commit.  A change of a
header that renames a function
that the planted vessel calls must fail the build of the vessel, and the gate
must reject it.  The planted cmake compiles that vessel.
"""

import fcntl
import json
import os
import sys
import tempfile
import time
from collections.abc import Callable, Mapping, Sequence
from dataclasses import replace
from pathlib import Path
from unittest import mock

from assay.core import git, paths, proc, testing
from assay.core import registry as rule_registry
from assay.loop import gate, heavy, syntax
from assay.loop import inputs as guard_inputs
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

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
    "build-release-v3": "clean",
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
# The planted cmake on PATH for a configure: `cmake --preset NAME -B BUILD [--fresh]` writes its words to
# configures.txt of the build, and removes stale.txt of the build, as a configure that heals the cache.
CONFIGURE_CMAKE = """import pathlib
import sys

words = sys.argv[1:]
build = pathlib.Path(words[words.index("-B") + 1])
with open(build / "configures.txt", "a", encoding="utf-8") as handle:
    handle.write(" ".join(words) + "\\n")
(build / "stale.txt").unlink(missing_ok=True)
"""
# The planted master loop: `syntax --changed` names no preset, `heal BUILD NAME` finds a stale compiler only in a build
# with stale.txt, and `test
# BUILD` writes the report of the mode of the build directory.  The mode build gives a build that fails, and the mode late gives an error after a green run.  The
# master writes MAKEFLAGS to makeflags.txt, the words of the mode to words.txt, the words for ctest after -- to
# ctest-words.txt, and the times of its start and its end to times.txt.  The mode hang runs
# until a stop ends it.  The mode plugin makes the test guard_plugin red.
# `derive TARGET --write --build-dir BUILD` writes inventory.txt of the build into the tree as the
# mint inventory, or writes nothing for a build with no inventory.txt.  In the mode overlap, it first waits for
# cmake.txt of each other planted build of the tree, so each other configuration builds before the write ends.  In the
# mode after-vessel, it first waits for ran.txt of the planted ctest in the build of the vessel.  It exits 3 in the mode
# unwritten, and in the modes overlap and after-vessel when a wait stops.  With inventory.txt in the build, the test
# test_built, the planted test of the rule derived-files, is red when the tree holds a different inventory.  Each call
# writes its first words to calls.txt of its build.
#
# The modes that follow wait for a file of a different run, and not for a time.  Each wait stops after BOUND_S, only as
# a bound for a hang.  The bound is far above the time that a run of the gate on a busy host takes to start a different
# configuration.  A wait that stops makes its case fail: seen.txt then gives fewer runs, tokens.txt gives the word late,
# and deep.pid gives no pid.
#   * meet: write met.txt, and wait until each build of the mode meet holds met.txt.  Then write the count of these
#     builds that hold met.txt to seen.txt.  So each run of the mode meet sees the start of each other one before it
#     ends.
#   * deep: start a child in a process group of its own with assay.core.proc of this tree (UTILS_MARK), and wait for
#     it.  The child writes more bytes to its output than a pipe holds, then its pid to deep.pid.  That write of the
#     output ends only when the master reads it in its wait.  So the stop that deep.pid causes comes in the wait of the
#     master (AN INTERRUPT of assay.core.proc).
#   * broken-after-deep: wait for deep.pid of the build of default, and then be the mode broken.
#   * count: write ready.txt, and wait for greedy.held of the build of default.  Then read each idle token of the
#     jobserver of the lease, and write their count to tokens.txt.
#   * greedy: wait for ready.txt of the release build.  Then read each token that the jobserver gives for 1.5 s, and
#     write their count to tokens.txt.  Then write greedy.held, and hold the tokens until the release build writes
#     tokens.txt.
# The mode killed-once ends the first run of `test BUILD` with the exit 137 of a kill for lack of memory, and writes
# killed.txt.  Each later run is clean.
LOOP_MASTER = """import os
import pathlib
import sys
import time

BOUND_S = 600.0


def is_written(path):
    deadline = time.monotonic() + BOUND_S
    while not path.is_file() and time.monotonic() < deadline:
        time.sleep(0.02)
    return path.is_file()


words = sys.argv[1:]
if words[:2] == ["syntax", "--changed"]:
    sys.exit(0)
if words[0] == "heal":
    if (pathlib.Path(words[1]) / "stale.txt").is_file():
        print(f"the cache of {words[1]} names a stale compiler")
    sys.exit(0)
if words[0] == "derive":
    build = pathlib.Path(words[words.index("--build-dir") + 1])
    with open(build / "calls.txt", "a") as handle:
        handle.write(" ".join(words[:3]) + "\\n")
    mode = (build / "mode.txt").read_text().strip()
    if mode == "unwritten":
        sys.exit(3)
    if mode == "overlap":
        peers = [path.parent for path in build.parent.glob("*/mode.txt") if path.parent != build]
        began = time.monotonic()
        is_seen = all(is_written(peer / "cmake.txt") for peer in peers)
        (build / "waited.txt").write_text(f"{time.monotonic() - began:.1f}")
        if not is_seen:
            sys.exit(3)
    if mode == "after-vessel" and not is_written(build.parent / "build-vessel" / "ran.txt"):
        sys.exit(3)
    inventory = build / "inventory.txt"
    if inventory.is_file():
        pathlib.Path("misc").mkdir(exist_ok=True)
        pathlib.Path("misc/mint-inventory.md").write_text(inventory.read_text())
    sys.exit(0)
build = pathlib.Path(words[1])
with open(build / "calls.txt", "a") as handle:
    handle.write(words[0] + "\\n")
began = time.monotonic()
(build / "base.txt").write_text(os.environ.get("CRUCIBLE_CHANGE_BASE", "-"))
(build / "makeflags.txt").write_text(os.environ.get("MAKEFLAGS", "-"))
split = words.index("--") if "--" in words else len(words)
with open(build / "words.txt", "a") as handle:
    handle.write(" ".join(words[2:split]) + "\\n")
with open(build / "ctest-words.txt", "a") as handle:
    handle.write(" ".join(words[split + 1:]) + "\\n")
mode = (build / "mode.txt").read_text().strip()
if mode == "killed-once" and not (build / "killed.txt").is_file():
    (build / "killed.txt").write_text("1")
    print("Killed")
    sys.exit(137)
if mode == "meet":
    (build / "met.txt").write_text("1")
    peers = [path.parent for path in build.parent.glob("*/mode.txt") if path.read_text().strip() == "meet"]
    deadline = time.monotonic() + BOUND_S
    while not all((peer / "met.txt").is_file() for peer in peers) and time.monotonic() < deadline:
        time.sleep(0.02)
    (build / "seen.txt").write_text(str(sum((peer / "met.txt").is_file() for peer in peers)))
if mode == "hang":
    time.sleep(600)
if mode == "deep":
    sys.path.insert(0, "@UTILS@")
    from assay.core import proc

    filled = 'head -c 2097152 /dev/zero && echo $$ > "$1" && exec sleep 600'
    running = proc.start(proc.Command(["sh", "-c", filled, "sh", str(build / "deep.pid")], timeout_s=900.0))
    running.wait()
if mode == "broken-after-deep":
    is_written(build.parent / "build" / "deep.pid")
    mode = "broken"
if mode in ("greedy", "count"):
    descriptor = os.open(os.environ["MAKEFLAGS"].split("fifo:", 1)[1], os.O_RDWR | os.O_NONBLOCK)
    held = b""
if mode == "count":
    (build / "ready.txt").write_text("1")
    is_after = is_written(build.parent / "build" / "greedy.held")
    while True:
        try:
            held += os.read(descriptor, 64)
        except BlockingIOError:
            break
    (build / "tokens.txt").write_text(str(len(held)) if is_after else "late")
if mode == "greedy":
    is_after = is_written(build.parent / "build-release" / "ready.txt")
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        try:
            held += os.read(descriptor, 64)
        except BlockingIOError:
            time.sleep(0.01)
    (build / "tokens.txt").write_text(str(len(held)) if is_after else "late")
    (build / "greedy.held").write_text("1")
    is_written(build.parent / "build-release" / "tokens.txt")
if mode in ("greedy", "count"):
    os.write(descriptor, held)
    os.close(descriptor)
with open(build / "times.txt", "a") as handle:
    handle.write(f"{began} {time.monotonic()}\\n")
if mode == "build":
    print("the build gives an error")
    sys.exit(1)
statuses = {"test_passes": "run"}
if mode in ("flaky", "broken"):
    statuses["test_" + mode] = "fail"
if mode == "plugin":
    statuses["guard_plugin"] = "fail"
if mode == "both":
    statuses["test_flaky"] = statuses["test_broken"] = "fail"
if mode == "moves":
    ledger = pathlib.Path("utils/ledgers/home.txt").read_text()
    statuses["test_ledger"] = "fail" if "include/old.h" in ledger else "run"
if mode in ("budget", "budget-plugin"):
    ledger = pathlib.Path("utils/ledgers/header-alone.txt").read_text()
    statuses["guard_header_alone"] = "fail" if "| 3.1 |" in ledger else "run"
if mode == "budget-plugin":
    statuses["guard_plugin"] = "fail"
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
# gate does.  `write --rule header-alone --reason TEXT` raises the row of include/a.h from 3.1 to 3.3 with that reason,
# as a raise of a budget does.  `write --admit --reason TEXT` adds a row of door-unit, or two rows of owners, with that
# reason.  Each write writes its rule, the count of its build directories, and the word admit and the reason of the
# mode admit to writes.txt of each of them.
UTILS_MARK = "@UTILS@"
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
    if "--admit" in words:
        reason = words[words.index("--reason") + 1]
        ledger = pathlib.Path("utils/ledgers") / f"{words[2]}.txt"
        rows = {
            "door-unit": [f"src/new.cpp | header-door | 1 | new.h | {reason}"],
            "owners": [
                f"src/a.cpp | door-use | 1 | a: b | {reason}",
                f"src/a.cpp | lifetime-start | 1 | a: c | {reason}",
            ],
        }
        ledger.write_text(ledger.read_text() + "".join(f"{row}\\n" for row in rows.get(words[2], [])))
    elif words[2] == "header-alone":
        ledger = pathlib.Path("utils/ledgers/header-alone.txt")
        raised = f"| 3.3 | - | {words[words.index('--reason') + 1]}"
        ledger.write_text(ledger.read_text().replace("| 3.1 | - | a kept header", raised))
    elif words[2] == "door-unit":
        ledger = pathlib.Path("utils/ledgers/door-unit.txt")
        ledger.write_text(ledger.read_text().replace("| 2 |", "| 1 |"))
    for build in builds:
        with open(build / "writes.txt", "a", encoding="utf-8") as handle:
            handle.write(f"{words[2]} {len(builds)}{' admit ' + reason if '--admit' in words else ''}\\n")
    sys.exit(0)
sys.exit(0 if words[0] == "warnings" else 77)
"""
# The calls of the planted vessel, and the header that declares the functions of the calls.
VESSEL_CALLS = "vessel/torch/calls.txt"
VESSEL_HEADER = "include/record.h"
# The planted cmake: `cmake --build BUILD --target TARGET` writes its target to cmake.txt of the build, and exits 0.
# `cmake --build BUILD` of a build with the mode build gives an error and exits 1.
# A build of the target crucible_dispatch compiles the planted vessel: each name of VESSEL_CALLS of the tree must be a
# function of VESSEL_HEADER, else the build gives an error and exits 1.
CMAKE = """import pathlib
import sys

words = sys.argv[1:]
build = pathlib.Path(words[words.index("--build") + 1])
with open(build / "cmake.txt", "a", encoding="utf-8") as handle:
    handle.write(" ".join(words[words.index("--build") + 2 :]) + "\\n")
mode = build / "mode.txt"
if words[-1] == str(build) and mode.is_file() and mode.read_text().strip() == "build":
    print("the build gives an error")
    sys.exit(1)
calls, header = pathlib.Path("@CALLS@"), pathlib.Path("@HEADER@")
if "crucible_dispatch" in words and calls.is_file():
    declared = header.read_text() if header.is_file() else ""
    for name in calls.read_text().split():
        if f"{name}(" not in declared:
            print(f"{calls}: error: '{name}' was not declared in this scope")
            sys.exit(1)
""".replace("@CALLS@", VESSEL_CALLS).replace("@HEADER@", VESSEL_HEADER)
# The planted ctest: the listing of a build is the file tests.json of the build.  A test that runs alone is green,
# unless it is test_broken, test_ledger before the write of its ledger, or test_built before the write of the inventory.  A fall of door-unit makes no test red,
# because a ledger fails only on a growth (THE POLICIES of assay.core.ledger).  Each run of red tests adds one line to
# alone-runs.txt of the build: the value of -j and the names of the tests.  A run of --label-regex runs each test of
# the listing with that label, and writes the label to ran.txt of the build.  Each name in red.txt of the build is red
# too.  The report gives the text of NAME.out of the build as the output of the test NAME.  The value of
# --test-output-size-failed goes to output-size.txt of the build.
CTEST = """import json
import pathlib
import sys
from xml.sax.saxutils import escape

words = sys.argv[1:]
tests = pathlib.Path(words[words.index("--test-dir") + 1])
if "--show-only=json-v1" in words:
    print((tests / "tests.json").read_text())
    sys.exit(0)
if "--label-regex" in words:
    label = words[words.index("--label-regex") + 1].strip("^$")
    listed = json.loads((tests / "tests.json").read_text())["tests"]
    labelled = [test for test in listed if {"name": "LABELS", "value": [label]} in test["properties"]]
    names = [test["name"] for test in labelled]
    (tests / "ran.txt").write_text(label)
else:
    names = pathlib.Path(words[words.index("--tests-from-file") + 1]).read_text().split()
    jobs = words[words.index("-j") + 1] if "-j" in words else "-"
    with open(tests / "alone-runs.txt", "a") as handle:
        handle.write(" ".join([jobs, *names]) + "\\n")
report = pathlib.Path(words[words.index("--output-junit") + 1])
red = {"test_broken"}
red |= set((tests / "red.txt").read_text().split()) if (tests / "red.txt").is_file() else set()
reads = pathlib.Path("utils/ledgers/home.txt")
red |= {"test_ledger"} if reads.is_file() and "include/old.h" in reads.read_text() else set()
inventory = pathlib.Path(words[words.index("--test-dir") + 1], "inventory.txt")
held = pathlib.Path("misc/mint-inventory.md")
red |= {"test_built"} if inventory.is_file() and held.read_text() != inventory.read_text() else set()
if "--test-output-size-failed" in words:
    (tests / "output-size.txt").write_text(words[words.index("--test-output-size-failed") + 1])


def case(name):
    output = tests / f"{name}.out"
    text = f"<system-out>{escape(output.read_text())}</system-out>" if output.is_file() else ""
    return f'<testcase name="{name}" status="{"fail" if name in red else "run"}">{text}</testcase>'


report.write_text(f'<?xml version="1.0"?><testsuite name="planted">{"".join(case(name) for name in names)}</testsuite>')
"""
PLANTED = {
    ".gitignore": "build/\nbuild-*/\n.venv-torch\n",
    "notes.txt": "notes at head\n",
    "utils/registry.txt": REGISTRY,
    DOOR_PATH: DOOR_LEDGER,
    "utils/bin/loop": LOOP_MASTER,
    "utils/bin/guard": GUARD_MASTER,
    BUILD_RULE_MODULE: "VALUE = 1\n",
}
# The available memory of the heavy step of a run.  With much memory, the step gets the jobs of the gate.  With a small
# memory, the step of default gets (9.5 - RESERVE_GB) / 0.6 = 2 jobs (THE MEMORY of assay.loop.heavy, and
# KIND_GB_PER_JOB of assay.loop.gate).
MUCH_MEMORY_GB = 1e6
SMALL_MEMORY_GB = 9.5
SMALL_MEMORY_JOBS = 2
# The slots of the planted pool of a case, and the jobs of a planted gate, which are the slice of each lease (THE
# JOBSERVER of assay.loop.gate).
POOL_SLOTS = 64
PLANTED_JOBS = 1
# The idle ticks of the pump in the case of the greedy configuration: more than a case can run.  The idle tokens of a
# lease then stay in its jobserver until its job reads them.  The tokens that go back to the pool after IDLE_TICKS are a
# rule of time, and the test of assay.loop.heavy holds it (THE POOL of assay.loop.heavy).
NO_IDLE_RETURN_TICKS = 10**9
# The wall guard of a test of the gate of the case of the hang, and the sleep of its test that never ends, in seconds
# (THE HANG GUARD of assay.loop.gate).  The verdict reads the status of the test, and not a time.
HANG_GUARD_S = 2
HANG_SLEEP_S = 600


def planted_repository(root: Path, files: Mapping[str, str] | None = None) -> str:
    """Plant the repository in root with one commit, and return the commit.

    `files` replaces or adds some files of PLANTED.
    """
    root.mkdir(parents=True, exist_ok=True)
    git.init(root)
    for name, text in {**PLANTED, **(files or {})}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        marked = text.replace(UTILS_MARK, str(paths.REPO_ROOT / "utils"))
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
        "a change of a C++ test selects the default build, the two release builds and ThreadSanitizer",
        names_of(gate.select(["test/test_arena.cpp"], [], False)) == ["default", "release", "release-v3", "tsan"],
    )
    expect(
        "a change of include/ or of a top build file adds ThreadSanitizer and UBSan-strict",
        names_of(gate.select(["include/foundation/Platform.h"], [], False))
        == names_of(gate.select(["CMakeLists.txt"], [], False))
        == ["default", "release", "release-v3", "tsan", "ubsan-strict"],
    )
    expect(
        "a change of a header of the Simd family or of a body in the inline namespace CRUCIBLE_SIMD_TARGET selects "
        "the release build at x86-64-v3, which runs the AVX2 arms at the width of 256 bits",
        all(
            "release-v3" in names_of(gate.select([path], [], False))
            for path in ("include/foundation/Simd.h", "include/fixy/atoms/Simd.h", "include/foundation/core/Reduce.h")
        ),
    )
    expect(
        "a change of bench/ or test/fuzz/ adds the bench and fuzz build",
        names_of(gate.select(["test/fuzz/CMakeLists.txt"], [], False))
        == ["default", "release", "release-v3", "tsan", "bench-fuzz"],
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
        and len(others) == 5,
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
        # A stop of the host can leave the index with no bytes, and git then gives "index file smaller than expected".
        index = tree / ".git" / "index"
        index.write_bytes(b"")
        third = commit_change(root, "notes.txt", "notes of the third commit\n")
        _, printed = testing.captured(lambda: gate.move_tree(root, tree, third))
        expect(
            "a move on an index that git cannot read, such as an index with no bytes after a stop of the host, makes "
            "the index again from HEAD and gives the tree the commit, and each other file keeps its time of change",
            git.head(tree) == third
            and (tree / "notes.txt").read_text(encoding="utf-8") == "notes of the third commit\n"
            and git.has_no_change(tree)
            and (tree / "utils/registry.txt").stat().st_mtime_ns == before
            and "git cannot read the index of the warm tree" in printed,
        )
        (tree / ".git" / "HEAD").write_bytes(b"")
        index.write_bytes(b"")
        fourth = commit_change(root, "notes.txt", "notes of the fourth commit\n")
        _, printed = testing.captured(lambda: gate.move_tree(root, tree, fourth))
        expect(
            "a move on a HEAD and an index with no bytes, as a stop of the host leaves them, takes HEAD from the "
            "reflog, makes the index from it, and gives the tree the commit with each other time of change kept",
            git.head(tree) == fourth
            and git.has_no_change(tree)
            and (tree / "utils/registry.txt").stat().st_mtime_ns == before
            and "names no commit" in printed,
        )
        (tree / ".git" / "HEAD").write_bytes(b"")
        index.write_bytes(b"")
        (tree / ".git" / "logs" / "HEAD").unlink()
        fifth = commit_change(root, "notes.txt", "notes of the fifth commit\n")
        testing.captured(lambda: gate.move_tree(root, tree, fifth))
        expect(
            "with no reflog, a HEAD with no bytes names a branch with no commit, and the move still gives the tree the "
            "commit",
            git.head(tree) == fifth
            and (tree / "notes.txt").read_text(encoding="utf-8") == "notes of the fifth commit\n"
            and git.has_no_change(tree),
        )
        kept_index = index.read_bytes()
        try:
            gate.move_tree(root, tree, "0" * 40)
            is_refused = False
        except git.GitError:
            is_refused = True
        expect(
            "a read-tree that fails for a different cause, such as a commit that is not there, stays an error, and the "
            "readable index stays as it was",
            is_refused and index.read_bytes() == kept_index and git.head(tree) == fifth,
        )
        stray = tree / "test" / "fixy" / "neg" / "neg_left_by_a_stop.cpp"
        stray.parent.mkdir(parents=True, exist_ok=True)
        stray.write_text("int left = 0;\n", encoding="utf-8")
        (tree / "build").mkdir(exist_ok=True)
        (tree / "build" / "kept.o").write_bytes(b"\x7fELF")
        _, printed = testing.captured(lambda: gate.remove_untracked(tree))
        expect(
            "a file that the commit does not hold and that no ignore rule names goes, such as a fixture that a stop "
            "left, and an ignored build directory stays",
            not stray.exists() and (tree / "build" / "kept.o").is_file() and "neg_left_by_a_stop.cpp" in printed,
        )
        try:
            gate.move_tree(root, root / "inner", head)
            expect("the gate refuses a warm tree in the work tree of the repository", False)
        except gate.GateError:
            expect("the gate refuses a warm tree in the work tree of the repository", True)


def repair_cases(expect: testing.Expect) -> None:
    """Hold repair_builds(): outputs with no bytes and a reply that does not read go, and a whole build stays."""
    with tempfile.TemporaryDirectory(prefix="gate-repair-") as work_text:
        tree = Path(work_text)
        default, release = gate.BY_NAME["default"], gate.BY_NAME["release"]
        cut, whole = tree / default.build, tree / release.build
        for build in (cut, whole):
            (build / "CMakeFiles" / "x.dir").mkdir(parents=True)
            (build / "lib").mkdir()
            (build / "build.ninja").write_text("rule cc\n", encoding="utf-8")
            reply = build.joinpath(*gate.REPLY_DIRECTORY)
            reply.mkdir(parents=True)
            (reply / "index-1.json").write_text('{"objects": []}', encoding="utf-8")
            (build / "CMakeFiles" / "x.dir" / "kept.cpp.o").write_bytes(b"\x7fELF")
            (build / "lib" / "libkept.a").write_bytes(b"!<arch>\n")
        (cut / "CMakeFiles" / "x.dir" / "cut.cpp.o").write_bytes(b"")
        (cut / "lib" / "libcut.so").write_bytes(b"")
        (cut / "lib" / "libcut.so.1").write_bytes(b"")
        (cut / "lib" / "liblink.so").symlink_to("libcut.so.1")
        (cut / "notes.txt").write_bytes(b"")
        (cut.joinpath(*gate.REPLY_DIRECTORY) / "index-2.json").write_text("{", encoding="utf-8")
        _, printed = testing.captured(lambda: gate.repair_builds(tree, (default, release)))
        left = sorted(str(path.relative_to(cut)) for path in cut.rglob("*") if path.is_file() or path.is_symlink())
        expect(
            "the repair removes each object, archive and shared library with no bytes, keeps a link, a text file "
            "and each output with bytes, and removes a reply whose index does not read with the graph build.ninja: "
            f"{left}",
            left == ["CMakeFiles/x.dir/kept.cpp.o", "lib/libkept.a", "lib/liblink.so", "notes.txt"]
            and "removes 3 outputs with no bytes" in printed
            and "configures" in printed,
        )
        expect(
            "a whole build keeps each file and its graph, and gives no line",
            (whole / "build.ninja").is_file()
            and (whole.joinpath(*gate.REPLY_DIRECTORY) / "index-1.json").is_file()
            and f"{release.name}:" not in printed,
        )


def plan_cases(expect: testing.Expect) -> None:
    """Hold configure_plan(): no graph configures as today, and a line of `loop heal` configures with --fresh."""
    with tempfile.TemporaryDirectory(prefix="gate-plan-") as work_text:
        tree = Path(work_text)
        default = gate.BY_NAME["default"]
        build = tree / default.build
        asked: list[Path] = []

        def heal_of(line: str) -> Callable[[Path], str]:
            def heal(asked_build: Path) -> str:
                asked.append(asked_build)
                return line

            return heal

        words = gate.configure_words(tree, default)
        expect(
            "a build directory with no graph of Ninja takes its configure of today, and the gate asks no heal",
            gate.configure_plan(tree, default, heal_of("planted line")) == (words, "") and not asked,
        )
        build.mkdir(parents=True)
        ninja_reader.plant_graph(build, "")
        expect(
            "a build directory with a graph and a cache that agrees takes no configure",
            gate.configure_plan(tree, default, heal_of("")) is None and asked == [build],
        )
        expect(
            "a line of the heal gives the configure with --fresh, and the gate writes the line",
            gate.configure_plan(tree, default, heal_of("planted line")) == ([*words, gate.FRESH_WORD], "planted line"),
        )


def parse_heal_cases(expect: testing.Expect) -> None:
    """Hold the heal of the parse builds: the gate configures a stale parse build that `loop test` parses (THE COMPILER
    HEAL).

    A change of a text file runs only default, and `loop test` of default parses each configured parse build.  The
    first parse build has a graph and a stale compiler, and the second has no graph.  The gate must configure the first
    one again with --fresh before the runs, and leave the second one as it is.
    """
    with tempfile.TemporaryDirectory(prefix="gate-parse-heal-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "notes.txt", "a change of the notes\n")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        stale_parse, unconfigured_parse = (item for item in gate.CONFIGURATIONS if item.is_parse)
        planted = planted_gate(work, root, tree, "clean", {stale_parse.build: "clean"})
        stale_build = tree / stale_parse.build
        (stale_build / "stale.txt").write_text("1\n", encoding="utf-8")
        unconfigured = tree / unconfigured_parse.build
        unconfigured.mkdir(parents=True)
        bin_directory = work / "bin"
        bin_directory.mkdir()
        testing.planted_program(bin_directory, "cmake", CONFIGURE_CMAKE)

        def run() -> gate.Verdict:
            with testing.environment_variable("PATH", f"{bin_directory}{os.pathsep}{os.environ.get('PATH', '')}"):
                return planted.check(gate.Candidate(head, commit, ("notes.txt",)), False)

        verdict, output = testing.captured(run)
        configures = stale_build / "configures.txt"
        lines = configures.read_text(encoding="utf-8").splitlines() if configures.is_file() else []
        expect(
            f"a run with no parse configuration configures a stale parse build again with --fresh before the runs, "
            f"writes the line of the heal, and leaves a parse build with no graph as it is: {lines}, "
            f"{verdict.configurations}",
            lines == [f"--preset {stale_parse.name} -B {stale_build} {gate.FRESH_WORD}"]
            and f"the cache of {stale_build} names a stale compiler" in output
            and not (unconfigured / "configures.txt").exists()
            and verdict.is_accepted
            and verdict.configurations == ("default",),
        )


def hang_guard_cases(expect: testing.Expect) -> None:
    """Hold THE HANG GUARD: ctest stops a test that never ends at the guard, and the gate reads the test as red.

    The gate of the case has a guard of HANG_GUARD_S.  The run of the red tests alone uses the pinned ctest on a planted
    build whose test test_hang sleeps for HANG_SLEEP_S, far past the guard.  The verdict reads the status of the test
    and no time: with no guard, the sleep ends and the test is green.  The case has no test that ends, because the
    start of such a test on a loaded host can take longer than the small guard.  The planted loop master of a full run
    gets the guard after -- too.
    """
    with tempfile.TemporaryDirectory(prefix="gate-hang-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "notes.txt", "a change of the notes\n")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = replace(planted_gate(work, root, tree, "clean"), test_timeout_s=HANG_GUARD_S)
        candidate = gate.Candidate(head, commit, ("notes.txt",))
        verdict, _ = testing.captured(lambda: planted.check(candidate, False))
        build = tree / "build"
        words = (build / "ctest-words.txt").read_text(encoding="utf-8").splitlines()
        expect(
            f"`loop test` of a run gets the guard of the gate after --: {words}",
            verdict.is_accepted and words == [f"--timeout {HANG_GUARD_S}"],
        )
        sleeper = f'"{sys.executable}" "-c" "import time; time.sleep({HANG_SLEEP_S})"'
        (build / "CTestTestfile.cmake").write_text(f"add_test(test_hang {sleeper})\n", encoding="utf-8")
        cmake_reader.plant(build, cache={"CMAKE_CTEST_COMMAND": cmake_pin.pinned_program("ctest")})
        step = gate.Step(planted, candidate, proc.environment())
        always = next(item for item in gate.CONFIGURATIONS if item.name == gate.ALWAYS)

        def alone() -> list[str]:
            with step.leased() as share:
                return step.red_alone(always, ["test_hang"], share)

        red, _ = testing.captured(alone)
        report = planted.logs / gate.log_name(always, "alone.junit.xml", False)
        text = report.read_text(encoding="utf-8") if report.is_file() else ""
        log = planted.logs / gate.log_name(always, "alone.log", False)
        text += log.read_text(encoding="utf-8")[-600:] if log.is_file() and not text else ""
        state = "holds the timeout" if 'message="Timeout"' in text else f"does not hold the timeout: {text[-300:]!r}"
        expect(
            f"a test that never ends stops at the guard of {HANG_GUARD_S} s, ctest reports its timeout, and the gate "
            f"reads it as red: red {red}, and the report {state}",
            red == ["test_hang"] and 'message="Timeout"' in text,
        )


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

    `others` maps the name of a build directory, such as build-bench-fuzz, to its mode.  The gate has no vessel, and
    vessel_cases() gives one to its gates (THE VESSEL).
    """
    for name, build_mode in {"build": mode, **(others or {})}.items():
        plant_build(work, tree, name, build_mode)
    return gate.Gate(
        root, work / "land", PLANTED_JOBS, False, read=lambda: MUCH_MEMORY_GB, pool=planted_pool(work), vessel=None
    )


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
        suite_reports: dict[str, bool] = {}
        for mode in ("clean", "flaky", "broken", "build", "late", "reconfigure"):
            planted = planted_gate(work, root, tree, mode)

            def run(planted: gate.Gate = planted) -> gate.Verdict:
                return planted.check(candidate, False)

            verdicts[mode], outputs[mode] = testing.captured(run)
            suite_reports[mode] = (planted.logs / f"{gate.ALWAYS}.{gate.SUITE_REPORT}").is_file()
        expect(
            "a run with a red test keeps a copy of the report of the suite in its logs, and a run with no red test "
            "keeps none",
            suite_reports["flaky"]
            and suite_reports["broken"]
            and not suite_reports["clean"]
            and gate.SUITE_REPORT not in outputs["clean"]
            and gate.SUITE_REPORT in outputs["flaky"],
        )
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


def built_targets(build: Path) -> list[str]:
    """Return the targets of each call of the planted cmake in a build, from cmake.txt, or no target with no call.

    A stop can end the run of a configuration before its build, so a build with no cmake.txt makes its case fail with the
    output of the gate, and not with an error of the test.
    """
    record = build / "cmake.txt"
    return record.read_text(encoding="utf-8").splitlines() if record.is_file() else []


def built_cases(expect: testing.Expect) -> None:
    """Hold THE BUILT FILES: the gate writes a merged generated file from the build of default before the suite."""
    with tempfile.TemporaryDirectory(prefix="gate-built-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {INVENTORY: STALE_INVENTORY, DOOR_PATH: COUNTED_DOORS})
        commit = commit_change(root, "notes.txt", "a change with a merged inventory\n", "Add a fixture\n\nThe body.")
        folds: list[str] = []
        candidate = gate.Candidate(head, commit, (INVENTORY, "notes.txt"), built=(INVENTORY,), on_fold=folds.append)
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "clean")
        build = tree / "build"
        (build / "inventory.txt").write_text(BUILT_INVENTORY, encoding="utf-8")
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        expect(
            f"the gate gives the commit of the fold of the built files to the candidate before the tests: {folds}",
            folds == [verdict.final],
        )
        calls = calls_of(build)
        builds = built_targets(build)
        holds = (
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and calls == ["derive mint-inventory --write", "test"]
            and builds == ["--target build.ninja", ""]
        )
        label = (
            f"the run of default builds build.ninja for the touched tests, then the target all with no -j, writes the "
            f"merged inventory, and then runs the suite, which judges the written inventory: the calls {calls} and the "
            f"builds {builds}"
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
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and git.show(root, verdict.final, INVENTORY) == BUILT_INVENTORY.encode()
            and calls == ["derive mint-inventory --write", "test"]
        )
        label = (
            f"a hand change of the inventory is a change that can change it, so the gate writes the bytes of the build "
            f"in its place: the calls {calls} and the gate writes {verdict.built}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        notes = commit_change(root, "notes.txt", "a change that no built file reads\n", "Change the notes")
        verdict, printed = testing.captured(lambda: clean.check(gate.Candidate(stale, notes, ("notes.txt",)), False))
        calls = calls_of(build)
        holds = (
            not verdict.is_accepted
            and any("red alone too: test_built" in problem for problem in verdict.problems)
            and not verdict.built
            and calls == ["test"]
        )
        label = (
            f"a change that can change no built file gets no write, and the suite rejects an inventory that the build "
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


def built_fixture_cases(expect: testing.Expect) -> None:
    """Hold THE BUILT FILES for a change that adds a fixture and holds no inventory: the selection and the wait.

    The run of default writes the inventory after its build, and the planted derive waits for the build of each other
    configuration.
    Each configuration of objects runs the planted test of the rule derived-files after the fold.
    """
    expect(
        "a change of a header, of a fixture, of the ledger of the rule mint or of a module of the deriver can change "
        "the inventory, and a change of a source file, of a different test or of the notes cannot",
        gate.built_selection(paths.REPO_ROOT, ["include/fixy/Core.h"]) == (INVENTORY,)
        and gate.built_selection(paths.REPO_ROOT, ["test/foundation/neg/mint_refused.cpp"]) == (INVENTORY,)
        and gate.built_selection(paths.REPO_ROOT, ["utils/ledgers/mint.txt"]) == (INVENTORY,)
        and gate.built_selection(paths.REPO_ROOT, ["utils/assay/guard/engines/plugin/declarations.py"]) == (INVENTORY,)
        and gate.built_selection(paths.REPO_ROOT, ["src/fixy/Arena.cpp", "test/test_arena.cpp", "notes.txt"]) == (),
    )
    with tempfile.TemporaryDirectory(prefix="gate-fixture-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {INVENTORY: STALE_INVENTORY})
        fixture = "test/fixy/neg/mint_a_refused.cpp"
        commit = commit_change(root, fixture, "int refused = mint_a();\n", "Add a fixture of mint_a")
        candidate = gate.Candidate(head, commit, (fixture,))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "overlap", OTHER_BUILDS)
        objects = ["build", *OTHER_BUILDS]
        for name in objects:
            (tree / name / "inventory.txt").write_text(BUILT_INVENTORY, encoding="utf-8")
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        calls = {name: calls_of(tree / name) for name in objects}
        builds = {name: built_targets(tree / name) for name in objects}
        waited = tree / "build" / "waited.txt"
        waited_s = waited.read_text(encoding="utf-8") if waited.is_file() else "-"
        holds = (
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and git.show(root, verdict.final, INVENTORY) == BUILT_INVENTORY.encode()
            and git.parent(root, verdict.final) == head
            and calls["build"] == ["derive mint-inventory --write", "test"]
            and all(calls[name] == ["test"] for name in OTHER_BUILDS)
            and all(builds[name] == [""] for name in objects)
        )
        label = (
            f"a change that adds a fixture and holds no inventory gets the inventory from the build of default.  Each "
            f"other configuration builds while default builds and writes, and it runs its suite after the fold, so the "
            f"planted test of the rule derived-files is green in each one: the gate writes {verdict.built}, with the "
            f"calls {calls} and the builds {builds}"
        )
        failed = f"{label}.  The write waited {waited_s} s for the other builds.  The output of the gate:\n{printed}"
        expect(label if holds else failed, holds)
        focused = planted_gate(work, root, tree, "clean", OTHER_BUILDS)
        alone = replace(candidate, focus=(gate.Focus("release", ("test_built",)),))
        verdict, printed = testing.captured(lambda: focused.check(alone, False))
        calls = {name: calls_of(tree / name) for name in ("build", "build-release")}
        holds = (
            verdict.is_accepted
            and verdict.built == (INVENTORY,)
            and verdict.configurations == ("release",)
            and calls == {"build": ["derive mint-inventory --write"], "build-release": ["test"]}
        )
        label = (
            f"a focus with no run of default writes the inventory before each configuration, so the request that runs "
            f"alone gets the inventory too: the gate runs {verdict.configurations}, writes {verdict.built} and calls "
            f"{calls}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        base = gate.Candidate(commit, commit, (fixture,), focus=(gate.Focus("release", ("test_passes",)),))
        verdict, printed = testing.captured(lambda: focused.check(base, False))
        calls = {name: calls_of(tree / name) for name in ("build", "build-release")}
        holds = verdict.final == commit and not verdict.built and calls == {"build": [], "build-release": ["test"]}
        label = f"the run of the base writes no built file: the gate writes {verdict.built} and calls {calls}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        broken = planted_gate(work, root, tree, "build", OTHER_BUILDS)
        verdict, printed = testing.captured(lambda: broken.check(candidate, False))
        base = gate.Candidate(head, head, (fixture,), focus=verdict.focus)
        base_verdict, base_printed = testing.captured(lambda: broken.check(base, True))
        calls_of(tree / "build")

        def shown(item: gate.Verdict) -> set[str]:
            """Return the problems of a verdict with a mark in place of the directory of its logs."""
            return {problem.replace(item.logs, "LOGS") for problem in item.problems}

        holds = (
            not verdict.is_accepted
            and verdict.focus == (gate.Focus(gate.ALWAYS),)
            and not base_verdict.is_accepted
            and shown(verdict) == shown(base_verdict)
        )
        label = (
            f"a build error of default before the write of a built file gives the problem of a build error of `loop "
            f"test`, so the run of the base, which writes no built file, gives the same problem: {verdict.problems} "
            f"and {base_verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}\n{base_printed}", holds)
    built_touch_cases(expect)


def built_touch_cases(expect: testing.Expect) -> None:
    """Hold the touched tests of a change with no compiled file that can change a built file (THE BUILT FILES).

    The planted rule plan-check has the inventory in its scope, as the rule derived-files has.  A change of the ledger
    of the rule mint touches no input of it, but the written inventory does.
    """
    with tempfile.TemporaryDirectory(prefix="gate-touch-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        registry = REGISTRY.replace(
            "plan-check engine=meta scope=builddir", f"plan-check engine=meta scope=builddir,{INVENTORY}"
        )
        head = planted_repository(root, {INVENTORY: STALE_INVENTORY, rule_registry.REGISTRY_PATH: registry})
        ledger = "utils/ledgers/mint.txt"
        commit = commit_change(root, ledger, "format 1\nrule mint\n", "Change the ledger of the rule mint")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "clean", OTHER_BUILDS)
        (tree / "build" / "inventory.txt").write_text(BUILT_INVENTORY, encoding="utf-8")
        verdict, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, (ledger,)), False))
        only = planted.logs / gate.log_name(gate.BY_NAME["release"], "only.txt")
        named = only.read_text(encoding="utf-8").split() if only.is_file() else []
        holds = verdict.is_accepted and verdict.built == (INVENTORY,) and named == [BUILD_RULE_TEST]
        label = (
            f"a change of the ledger of the rule mint writes the inventory, and each other configuration runs the "
            f"tests that read the written inventory: the gate writes {verdict.built}, and release runs {named}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


# The registry, the ledger and the path of the ledger of the rule owners of the cases of THE RATCHET ROWS.  The module
# of the rule owners of this tree gives its refused kinds, such as lifetime-start.
RATCHET_REGISTRY = (
    f"{REGISTRY}rule owners engine=plugin scope=tracked ledger=count kinds=door-use,lifetime-start doc=L0 | a use | "
    f"use the door\n"
)
OWNERS_PATH = "utils/ledgers/owners.txt"
OWNERS_LEDGER = "format 1\nrule owners\nsrc/old.cpp | door-use | 1 | a: z | a reviewed use\n"


def ratchet_cases(expect: testing.Expect) -> None:
    """Hold THE RATCHET ROWS: a red test of the plugin rules gives the rows of the build, and a different one gives none."""
    with tempfile.TemporaryDirectory(prefix="gate-ratchet-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        ledgers = {DOOR_PATH: COUNTED_DOORS, OWNERS_PATH: OWNERS_LEDGER}
        head = planted_repository(root, {rule_registry.REGISTRY_PATH: RATCHET_REGISTRY, **ledgers})
        commit = commit_change(root, "src/a.cpp", "int a;\n", "Add a use of a door")
        candidate = gate.Candidate(head, commit, ("src/a.cpp",))
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "plugin", OTHER_BUILDS)
        (tree / "build" / "red.txt").write_text(f"{gate.PLUGIN_TEST}\n", encoding="utf-8")
        verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        writes = tree / "build" / "writes.txt"
        written = sorted(writes.read_text(encoding="utf-8").splitlines()) if writes.is_file() else []
        reason = gate.ROW_REASON
        head_line = (
            "loop land: default: the build gives these rows of {path}.  Put each row in the ledger of the change, and "
            f"write the reason of a new row in place of {reason}.  The rows come from one build, and a different "
            "configuration can give a higher count:"
        )
        rows = (
            head_line.format(path=DOOR_PATH),
            f"  src/new.cpp | header-door | 1 | new.h | {reason}",
            head_line.format(path=OWNERS_PATH),
            f"  src/a.cpp | door-use | 1 | a: b | {reason}",
            (
                f"loop land: default: no row of {OWNERS_PATH} admits a finding of the kind lifetime-start, so correct "
                f"the code: src/a.cpp | lifetime-start | 1 | a: c | {reason}"
            ),
        )
        held = {path: (tree / path).read_text(encoding="utf-8") for path in ledgers}
        holds = (
            not verdict.is_accepted
            and any(f"red alone too: {gate.PLUGIN_TEST}" in problem for problem in verdict.problems)
            and verdict.rows == rows
            and written == [f"door-unit 1 admit {reason}", f"owners 1 admit {reason}"]
            and held == ledgers
            and not any((tree / name / "writes.txt").is_file() for name in OTHER_BUILDS)
        )
        label = (
            f"a red test of the plugin rules gives the rows that the build of its configuration gives for each counted "
            f"plugin rule, with no row of a refused kind, and the ledgers of the warm tree keep the bytes of the commit: "
            f"the writes {written} and the rows {verdict.rows}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        planted_error = RuntimeError("a planted error of the rows")
        with mock.patch.object(gate.Step, "ratchet_rows", side_effect=planted_error):
            verdict, printed = testing.captured(lambda: planted.check(candidate, False))
        holds = (
            not verdict.is_accepted
            and any(f"red alone too: {gate.PLUGIN_TEST}" in problem for problem in verdict.problems)
            and verdict.rows == ()
            and f"the gate cannot give the rows of the ratchet ledgers: {planted_error}" in printed
        )
        label = f"an error of the rows gives one line and no row, and the problems stay: {verdict.problems}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (tree / "build" / "red.txt").unlink()
        writes.unlink(missing_ok=True)
        broken = planted_gate(work, root, tree, "broken", OTHER_BUILDS)
        verdict, printed = testing.captured(lambda: broken.check(candidate, False))
        holds = not verdict.is_accepted and verdict.rows == () and not writes.is_file()
        label = f"a different red test gives no row and no write: the rows {verdict.rows}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


BUDGET_PATH = "utils/ledgers/header-alone.txt"
BUDGET_LEDGER = "format 1\nrule header-alone\ninclude/a.h | - | 3.1 | - | a kept header\n"
BUDGET_REASON = "the owner decision of 2026-11-15: the header grows with a new door"


def budget_cases(expect: testing.Expect) -> None:
    """Hold THE RAISE OF A BUDGET: a crossed row of header-alone rises with the reason, and a different problem stays.

    With no budget reason, the crossed row refuses the commit.  With a budget reason, the gate writes the row with the
    reason, folds it into the commit, runs the round again with the checks of the red configuration, and gives the line
    of the raise.  A red test of a rule of correctness next to the crossed row refuses the commit with the reason too.
    """
    with tempfile.TemporaryDirectory(prefix="gate-budget-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {BUDGET_PATH: BUDGET_LEDGER})
        commit = commit_change(root, "src/a.cpp", "int a;\n", "Grow a header")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        planted = planted_gate(work, root, tree, "budget", OTHER_BUILDS)
        build = tree / "build"
        (build / "red.txt").write_text("guard_header_alone\n", encoding="utf-8")
        finding = f"{tree}/include/a.h:0: error: [header-alone] include/a.h includes 3.3 MB, above its row\n"
        (build / "guard_header_alone.out").write_text(finding, encoding="utf-8")
        plain = gate.Candidate(head, commit, ("src/a.cpp",))
        verdict, printed = testing.captured(lambda: planted.check(plain, False))
        holds = (
            not verdict.is_accepted
            and any("red alone too: guard_header_alone" in problem for problem in verdict.problems)
            and verdict.raised == ()
            and git.show(root, verdict.final, BUDGET_PATH) == BUDGET_LEDGER.encode()
        )
        label = f"with no budget reason, a crossed row of header-alone refuses the commit: {verdict.problems}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        checks = build / "checks.txt"
        checks.unlink(missing_ok=True)
        reasoned = replace(plain, budget_reason=BUDGET_REASON)
        verdict, printed = testing.captured(lambda: planted.check(reasoned, False))
        line = f"{BUDGET_PATH}: include/a.h from 3.1 to 3.3"
        landed_ledger = git.show(root, verdict.final, BUDGET_PATH) if verdict.final else None
        holds = (
            verdict.is_accepted
            and verdict.raised == (line,)
            and verdict.final not in ("", commit)
            and git.parent(root, verdict.final) == head
            and verdict.ledgers == (BUDGET_PATH,)
            and landed_ledger
            == BUDGET_LEDGER.replace("| 3.1 | - | a kept header", f"| 3.3 | - | {BUDGET_REASON}").encode()
            and f"loop land: the gate raised the row {line}" in printed
            and "reconfigure-noop" in (checks.read_text(encoding="utf-8") if checks.is_file() else "")
        )
        label = (
            f"with a budget reason, the gate raises the crossed row with the reason, folds it into the commit, runs the "
            f"checks of the red configuration in the round again, and gives the line of the raise: {verdict.raised}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (build / "mode.txt").write_text("budget-plugin\n", encoding="utf-8")
        (build / "red.txt").write_text("guard_header_alone guard_plugin\n", encoding="utf-8")
        (build / "guard_plugin.out").write_text(f"{tree}/src/a.cpp:1: error: [quarantine] a raw pointer\n")
        verdict, printed = testing.captured(lambda: planted.check(reasoned, False))
        holds = (
            not verdict.is_accepted
            and any("guard_plugin" in problem for problem in verdict.problems)
            and verdict.raised == ()
            and git.show(root, verdict.final, BUDGET_PATH) == BUDGET_LEDGER.encode()
            and (tree / BUDGET_PATH).read_text(encoding="utf-8") == BUDGET_LEDGER
        )
        label = (
            f"a count of a rule of correctness next to a crossed row refuses the commit with the reason too: {verdict}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        outcome = gate.Outcome(
            "default", ("default: red alone too: guard_step_cost",), ("guard_step_cost",), 1.0, False
        )
        items = ("default: guard_step_cost: test/a.cpp:0 [step-cost]",)
        expect(
            "an outcome of a budget needs a finding line of a budget rule for each item of each test of BUDGET_TESTS",
            gate.is_budget_outcome(replace(outcome, items=items))
            and not gate.is_budget_outcome(replace(outcome, items=("default: guard_step_cost",)))
            and not gate.is_budget_outcome(
                replace(outcome, items=(*items, "default: guard_step_cost: b:0 [quarantine]"))
            )
            and not gate.is_budget_outcome(replace(outcome, red=("test_vigil",), items=items))
            and not gate.is_budget_round([replace(outcome, items=items), replace(outcome, is_stopped=True)]),
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
            and verdicts["clean"].configurations
            == ("default", "release", "release-v3", "tsan", "ubsan-strict", "bench-fuzz")
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
        objects = ("default", "release", "release-v3", "tsan", "ubsan-strict", "bench-fuzz")
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


def seen_of(tree: Path, builds: tuple[str, ...]) -> list[str]:
    """Return seen.txt of each build of the tree: the count of the runs of the mode meet that its run saw, or "-"."""
    return [
        (tree / name / "seen.txt").read_text(encoding="utf-8") if (tree / name / "seen.txt").is_file() else "-"
        for name in builds
    ]


def thread_cases(expect: testing.Expect) -> None:
    """Hold THE JOBSERVER and THE THREADS: each configuration gets MAKEFLAGS, and the configurations overlap.

    Each run of a planted master waits for a file of a different run, and not for a time (LOOP_MASTER).
    """
    with tempfile.TemporaryDirectory(prefix="gate-threads-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "test/a.cpp", "int a;\n", "Change a test")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        builds = ("build", "build-release", "build-release-v3", "build-tsan", "build-ubsan-strict", "build-bench-fuzz")
        planted = planted_gate(work, root, tree, "meet", dict.fromkeys(builds[1:], "meet"))
        wide = replace(planted, jobs=8)
        verdict, printed = testing.captured(lambda: wide.check(gate.Candidate(head, commit, ("test/a.cpp",)), False))
        flags = [(tree / name / "makeflags.txt").read_text(encoding="utf-8") for name in builds]
        fifos = [Path(text.split("fifo:", 1)[1]) for text in flags if "fifo:" in text]
        spans = [span for name in builds for span in times_of(tree / name)]
        seen = seen_of(tree, builds)
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
            f"lease: each of {len(spans)} runs sees the start of each run before it ends {seen}, and "
            f"{printed.count('memory: ')} reads of the memory",
            len(spans) == len(builds)
            and seen == [str(len(builds))] * len(builds)
            and printed.count("memory: ") == len(builds),
        )
        modes = {"build-release": "count", **dict.fromkeys(builds[2:], "clean")}
        with mock.patch.object(heavy, "IDLE_TICKS", NO_IDLE_RETURN_TICKS):
            greedy = replace(planted_gate(work, root, tree, "greedy", modes), jobs=8)
            verdict, printed = testing.captured(
                lambda: greedy.check(gate.Candidate(head, commit, ("test/a.cpp",)), False)
            )
        taken = [(tree / name / "tokens.txt").read_text(encoding="utf-8") for name in ("build", "build-release")]
        holds = (
            verdict.is_accepted
            and all(text.isdigit() for text in taken)
            and int(taken[0]) < POOL_SLOTS
            and int(taken[1]) >= heavy.START_SLOTS - 1
        )
        label = (
            f"a configuration that takes each token that it can does not take the tokens of a different "
            f"configuration: the greedy one holds {taken[0]}, and the other one then gets {taken[1]}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        rule_commit = commit_change(root, BUILD_RULE_MODULE, "VALUE = 3\n", "Change the rule")
        modes = {**dict.fromkeys(builds[1:], "clean"), "build-bench-fuzz": "meet"}
        early = replace(planted_gate(work, root, tree, "meet", modes), jobs=8)
        for name in builds:
            for mark in ("times.txt", "met.txt", "seen.txt"):
                (tree / name / mark).unlink(missing_ok=True)
        verdict, printed = testing.captured(
            lambda: early.check(gate.Candidate(commit, rule_commit, (BUILD_RULE_MODULE,)), False)
        )
        always = times_of(tree / "build")
        bench = times_of(tree / "build-bench-fuzz")
        seen = seen_of(tree, ("build", "build-bench-fuzz"))
        regenerated = built_targets(tree / "build")
        holds = (
            verdict.is_accepted
            and regenerated[-1:] == ["--target build.ninja"]
            and len(always) == 1
            and len(bench) == 1
            and seen == ["2", "2"]
        )
        label = (
            "the touched tests of the other configurations start when the build of ALWAYS holds its configure, before "
            f"the run of ALWAYS ends: the targets {regenerated}, and the runs of default and of bench-fuzz each see the "
            f"start of the other before they end {seen}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)


def oom_cases(expect: testing.Expect) -> None:
    """Hold THE KILL FOR MEMORY: a red run with a rise of the kill count runs one time again, and a red run with no
    rise stays red."""
    with tempfile.TemporaryDirectory(prefix="gate-oom-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "notes.txt", "a change of the notes\n")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        build = tree / "build"

        def kills() -> int:
            """Give 1 after the planted kill of the first run, as the kernel counts it, and 0 before it."""
            return int((build / "killed.txt").is_file())

        candidate = gate.Candidate(head, commit, ("notes.txt",))
        killed = replace(planted_gate(work, root, tree, "killed-once"), oom_kills=kills)
        verdict, printed = testing.captured(lambda: killed.check(candidate, False))
        calls = calls_of(build)
        logged = [json.loads(line) for line in (work / "land" / gate.KILL_LOG).read_text(encoding="utf-8").splitlines()]
        holds = (
            verdict.is_accepted
            and calls.count("test") == 2
            and "for lack of memory during the run" in printed
            and [(kill["configuration"], kill["gb_per_job"]) for kill in logged] == [(gate.ALWAYS, 0.6)]
            and logged[0]["problem"].startswith(f"{gate.ALWAYS}: ")
        )
        label = (
            f"a run that the kernel ends for lack of memory, with exit 137 and a rise of the kill count, runs one time "
            f"again and gives the verdict of the second run, with a line of the kill and one line of KILL_LOG with its "
            f"configuration and the memory of a job of its lease: the calls {calls}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (build / "killed.txt").unlink()
        flat = replace(planted_gate(work, root, tree, "killed-once"), oom_kills=lambda: 0)
        flat_verdict, printed = testing.captured(lambda: flat.check(candidate, False))
        flat_calls = calls_of(build)
        holds = not flat_verdict.is_accepted and flat_calls.count("test") == 1 and "lack of memory" not in printed
        label = (
            f"the same red run with no rise of the kill count does not run again, and the commit is red: the calls "
            f"{flat_calls} and the problems {flat_verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (build / "killed.txt").unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="gate-oom-count-") as work_text:
        work = Path(work_text)
        groups = work / "cgroup"
        group = groups / "user.slice"
        group.mkdir(parents=True)
        (group / "memory.max").write_text("max\n", encoding="ascii")
        (group / "memory.events").write_text("low 0\nhigh 0\nmax 4\noom 2\noom_kill 3\n", encoding="ascii")
        own = work / "self-cgroup"
        own.write_text("0::/user.slice\n", encoding="ascii")
        vmstat = work / "vmstat"
        vmstat.write_text("pgfault 10\noom_kill 1\n", encoding="ascii")
        counts = [heavy.oom_kills(vmstat, own, groups), heavy.oom_kills(work / "missing", work / "missing", groups)]
        expect(
            f"the kill count is the sum of oom_kill of /proc/vmstat and of memory.events of each group of the chain, "
            f"and missing files give 0: {counts}",
            counts == [4, 0],
        )


def kind_cases(expect: testing.Expect) -> None:
    """Hold the memory of a job of each lease of the gate: the largest kind of its step, rounded up to GB_STEP."""
    values = {
        "default": gate.gb_per_job_of("default"),
        "release": gate.gb_per_job_of("release"),
        "release-v3": gate.gb_per_job_of("release-v3"),
        "tsan": gate.gb_per_job_of("tsan"),
        "ubsan-strict": gate.gb_per_job_of("ubsan-strict"),
        "bench-fuzz": gate.gb_per_job_of("bench-fuzz"),
        "vessel build": gate.gb_per_job_of(gate.VESSEL, gate.BUILD_KINDS),
        "vessel tests": gate.gb_per_job_of(gate.VESSEL, gate.TEST_KINDS),
        "parse": gate.gb_per_job_of("syntax-x86-64"),
    }
    expect(
        f"a lease of `loop test` reserves the largest kind of its configuration, the vessel reserves a shard to build "
        f"and a vessel test to test, and a configuration with no row reserves GB_PER_JOB: {values}",
        values
        == {
            "default": 0.6,
            "release": 0.6,
            "release-v3": 0.6,
            "tsan": 0.5,
            "ubsan-strict": 0.6,
            "bench-fuzz": 0.9,
            "vessel build": 1.7,
            "vessel tests": 1.2,
            "parse": heavy.GB_PER_JOB,
        },
    )


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
            name
            for name in ("build", "build-release", "build-release-v3", "build-tsan", "build-ubsan-strict")
            if times_of(tree / name)
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
        for name in ("build", *OTHER_BUILDS):
            (tree / name / "words.txt").unlink(missing_ok=True)
        focus = batch_verdict.focus + (gate.Focus("bench-fuzz"),)
        base = planted_gate(work, root, tree, "clean", others)
        base_verdict, printed = testing.captured(
            lambda: base.check(gate.Candidate(head, head, ("test/a.cpp",), focus), True)
        )
        ran = {
            name: (tree / name / "words.txt").read_text(encoding="utf-8").splitlines()
            for name in ("build", *OTHER_BUILDS)
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


def item_cases(expect: testing.Expect) -> None:
    """Hold THE FAILING ITEMS: a red test gives the items of its output, and a run of the base can give other items."""
    with tempfile.TemporaryDirectory(prefix="gate-items-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root)
        commit = commit_change(root, "src/a.cpp", "int a;\n", "Change a source")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        build = tree / "build"
        planted = planted_gate(work, root, tree, "plugin", OTHER_BUILDS)
        (build / "red.txt").write_text(f"{gate.PLUGIN_TEST}\n", encoding="utf-8")
        output = build / f"{gate.PLUGIN_TEST}.out"
        output.write_text(
            "door-unit: 0 error(s), 0 warning(s)\n"
            "include/a.h:3: error: [must-consume] the first finding, and the path has 201 findings\n"
            f"{tree}/src/b.cpp:7: error: [owners] a finding with the path of the warm tree\n"
            "include/a.h:3: error: [must-consume] a second finding on the same line\n"
            "src/c.cpp:9: warning: [owners] a warning, which fails nothing\n",
            encoding="utf-8",
        )
        batch, printed = testing.captured(lambda: planted.check(gate.Candidate(head, commit, ("src/a.cpp",)), False))
        output.write_text("include/c.h:5: error: [must-consume] a finding of the base\n", encoding="utf-8")
        title = f"the run of the base at {head[:9]}"
        base, base_printed = testing.captured(
            lambda: planted.check(gate.Candidate(head, head, ("src/a.cpp",), batch.focus, title=title), True)
        )
        heads = (
            f"loop land: the commit {commit[:9]} runs in ",
            f"loop land: {title} runs only default {gate.PLUGIN_TEST}, logs in ",
        )
        holds = heads[0] in printed and heads[1] in base_printed
        label = (
            f"the first line of a check names the run with the title of the candidate, else with its commit: {heads}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}\n{base_printed}", holds)
        size = build / "output-size.txt"
        test = f"default: {gate.PLUGIN_TEST}"
        holds = (
            batch.items == (f"{test}: include/a.h:3 [must-consume]", f"{test}: src/b.cpp:7 [owners]")
            and base.items == (f"{test}: include/c.h:5 [must-consume]",)
            and base.problems == batch.problems
            and batch.problems[0].startswith(f"default: red alone too: {gate.PLUGIN_TEST}, see ")
            and size.is_file()
            and size.read_text(encoding="utf-8") == str(gate.ALONE_OUTPUT_BYTES)
        )
        label = (
            f"a test that is red alone too gives one item for each error line of its output, with the path of the tree "
            f"cut and no message, and the run alone keeps {gate.ALONE_OUTPUT_BYTES} bytes of the output.  A run of the "
            f"base with a finding on a different file gives the same problems and different items: the batch gives "
            f"{batch.items}, and the base gives {base.items}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}\n{base_printed}", holds)
        output.unlink()
        broken = planted_gate(work, root, tree, "broken", OTHER_BUILDS)
        verdict, printed = testing.captured(lambda: broken.check(gate.Candidate(head, commit, ("src/a.cpp",)), False))
        holds = verdict.items == ("default: test_broken",) and verdict.problems[0].startswith(
            "default: red alone too: test_broken, see "
        )
        label = f"a red test with no error line in its output gives the item of its name: {verdict.items}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        (build / "red.txt").unlink()
        failed = planted_gate(work, root, tree, "build", OTHER_BUILDS)
        verdict, printed = testing.captured(lambda: failed.check(gate.Candidate(head, commit, ("src/a.cpp",)), False))
        holds = bool(verdict.problems) and verdict.items == verdict.problems
        label = f"a problem that is not a red test is its own item: {verdict.items}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
    outputs = {"test_cut": f"src/a.cpp:1: error: [owners] text\n[This part of the {gate.CUT_MARK} of 307200 bytes.]"}
    items = gate.failing_items("debug", ("test_cut", "test_lost"), outputs, Path("/tree"), "c" * 40)
    expect(
        f"a test with an output that ctest cut, and a test with no output in the report, give an item with the commit "
        f"of the run, so the items of two commits do not compare equal: {items}",
        items
        == (
            f"debug: test_cut: the items are not known in the run of the commit {'c' * 40}",
            f"debug: test_lost: the items are not known in the run of the commit {'c' * 40}",
        ),
    )


def vessel_select_cases(expect: testing.Expect) -> None:
    """Hold the selection of the vessel, its configuration in this tree, and the focus of its run of the base."""
    expect(
        "the vessel runs for a change that can change each build and for a change under vessel/, and not for a "
        "different change",
        gate.reaches_vessel(["include/record.h"], True)
        and gate.reaches_vessel(["vessel/torch/crucible_mode.py"], False)
        and not gate.reaches_vessel(["notes.txt", "utils/assay/loop/land.py"], False),
    )
    vessel = gate.VESSEL_CONFIGURATION
    expect(
        f"the vessel of this tree is the preset vessel in build-vessel, and no configuration of the gate: {vessel}",
        vessel is not None
        and vessel.name == gate.VESSEL
        and vessel.build == "build-vessel"
        and not vessel.is_parse
        and gate.VESSEL not in gate.BY_NAME,
    )
    libraries = (paths.REPO_ROOT / "vessel/torch/CMakeLists.txt").read_text(encoding="utf-8")
    tests = (paths.REPO_ROOT / "test/CMakeLists.txt").read_text(encoding="utf-8")
    expect(
        "each target of VESSEL_TARGETS is a library of vessel/torch/CMakeLists.txt, and test/CMakeLists.txt gives the "
        "vessel tests the label VESSEL_LABEL",
        all(f"add_library({target} " in libraries for target in gate.VESSEL_TARGETS)
        and f"LABELS {gate.VESSEL_LABEL} " in tests,
    )
    if vessel is not None:
        focus = (gate.Focus(gate.VESSEL, ("vessel_test_mlp",)), gate.Focus(gate.VESSEL))
        expect(
            "the run of the base runs the vessel in its mode, with the red tests of its focus",
            gate.focused_runs(focus, vessel)
            == (gate.Run(vessel, gate.Mode.VESSEL, ("vessel_test_mlp",)), gate.Run(vessel, gate.Mode.VESSEL)),
        )


def vessel_listing(labelled: Sequence[str]) -> str:
    """Return the ctest listing of the planted build of the vessel: each name with the vessel label, and one other test."""
    label = [{"name": "LABELS", "value": [gate.VESSEL_LABEL]}]
    tests = [{"name": name, "command": ["true"], "properties": label} for name in labelled]
    tests.append({"name": "test_unlabelled", "command": ["true"], "properties": []})
    return json.dumps({"tests": tests})


def vessel_run_cases(expect: testing.Expect) -> None:
    """Hold the run of the vessel (THE VESSEL).

    A change of a header that renames a function that the vessel calls fails the build of the vessel, and the gate
    rejects it.  A gate with no vessel accepts the same commit.  A red vessel test, a build with no vessel test, and a
    main work tree with no patched PyTorch reject a commit too.
    """
    vessel = gate.VESSEL_CONFIGURATION
    if vessel is None:
        expect("this tree gives the preset vessel", False)
        return
    with tempfile.TemporaryDirectory(prefix="gate-vessel-") as work_text:
        work = Path(work_text)
        root = work / "repo"
        head = planted_repository(root, {VESSEL_HEADER: "int lanes();\n", VESSEL_CALLS: "lanes\n"})
        torch = root / gate.TORCH_VENV
        torch.mkdir()
        notes = commit_change(root, "notes.txt", "a change of the notes\n")
        script = "vessel/torch/crucible_mode.py"
        moded = commit_change(root, script, "MODE = 1\n", "Change a script of the vessel")
        renamed = commit_change(root, VESSEL_HEADER, "int lanes_of();\n", "Rename a function that the vessel calls")
        tree = work / "land" / gate.TREE
        gate.move_tree(root, tree, head)
        build = tree / vessel.build
        listing_of = [vessel_listing(("vessel_test_mlp", "vessel_test_red"))]

        def checked(candidate: gate.Candidate, has_vessel: bool = True) -> tuple[gate.Verdict, str]:
            """Plant each build, the build of the vessel with its listing, and check a candidate."""
            planted = planted_gate(work, root, tree, "clean", OTHER_BUILDS)
            plant_build(work, tree, vessel.build, "clean")
            (build / "tests.json").write_text(listing_of[0], encoding="utf-8")
            chosen = replace(planted, vessel=vessel) if has_vessel else planted
            return testing.captured(lambda: chosen.check(candidate, False))

        rename = gate.Candidate(moded, renamed, (VESSEL_HEADER,))
        verdict, printed = checked(rename)
        holds = (
            not verdict.is_accepted
            and any(
                problem.startswith(f"vessel: the build of {' and '.join(gate.VESSEL_TARGETS)} gives an error")
                for problem in verdict.problems
            )
            and gate.Focus(gate.VESSEL) in verdict.focus
            and (tree / gate.TORCH_VENV).is_symlink()
            and (tree / gate.TORCH_VENV).resolve() == torch.resolve()
        )
        label = (
            f"a change of a header that renames a function that the vessel calls fails the build of the vessel, and "
            f"the gate rejects the commit with the focus of the vessel: {verdict.problems}"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        blind, _ = checked(rename, has_vessel=False)
        expect(
            "a gate with no vessel accepts the same commit, because no configuration of the gate builds the vessel",
            blind.is_accepted and gate.VESSEL not in blind.configurations,
        )
        (build / "cmake.txt").unlink(missing_ok=True)
        text_verdict, _ = checked(gate.Candidate(head, notes, ("notes.txt",)))
        expect(
            "a change of a text file runs no vessel",
            text_verdict.is_accepted
            and text_verdict.configurations == (gate.ALWAYS,)
            and not (build / "cmake.txt").is_file(),
        )
        script_verdict, printed = checked(gate.Candidate(notes, moded, (script,)))
        built = (build / "cmake.txt").read_text(encoding="utf-8") if (build / "cmake.txt").is_file() else ""
        holds = (
            script_verdict.is_accepted
            and script_verdict.configurations == (gate.ALWAYS, gate.VESSEL)
            and built == f"--target {' '.join(gate.VESSEL_TARGETS)}\n"
        )
        label = f"a change of a script of the vessel builds the vessel targets and runs the vessel tests: {built!r}"
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        widened = commit_change(root, VESSEL_HEADER, "int lanes();\nint widths();\n", "Declare one more function")
        (build / "ran.txt").unlink(missing_ok=True)
        ordered = replace(planted_gate(work, root, tree, "after-vessel", OTHER_BUILDS), vessel=vessel)
        plant_build(work, tree, vessel.build, "clean")
        (build / "tests.json").write_text(listing_of[0], encoding="utf-8")
        header = gate.Candidate(renamed, widened, (VESSEL_HEADER,))
        header_verdict, printed = testing.captured(lambda: ordered.check(header, False))
        holds = (
            header_verdict.is_accepted
            and f"the change can change {INVENTORY}" in printed
            and gate.VESSEL in header_verdict.configurations
            and (build / "ran.txt").is_file()
        )
        label = (
            "the vessel reads no file that the gate writes, so its tests run before the write of the inventory ends: "
            "the planted derive of default waits for the vessel tests, and the gate accepts the change"
        )
        expect(label if holds else f"{label}.  The output of the gate:\n{printed}", holds)
        rows = {
            **gate.BUILT_FILES,
            "vessel/torch/table.py": gate.BuiltFile("table", "assay.loop.derive.mint_inventory", is_vessel_input=True),
        }
        expect(
            "the tests of the vessel wait for the write only when a written file is an input of the vessel",
            not gate.is_read_by_vessel((INVENTORY,))
            and not gate.is_read_by_vessel(())
            and gate.is_read_by_vessel((INVENTORY, "vessel/torch/table.py"), rows),
        )
        (build / "red.txt").write_text("vessel_test_red\n", encoding="utf-8")
        red_verdict, _ = checked(gate.Candidate(notes, moded, (script,)))
        base_verdict, _ = testing.captured(
            lambda: replace(planted_gate(work, root, tree, "clean"), vessel=vessel).check(
                gate.Candidate(notes, notes, (script,), red_verdict.focus), True
            )
        )
        alone = (build / "alone-runs.txt").read_text(encoding="utf-8").splitlines()
        expect(
            f"a vessel test that is red alone too rejects the commit with its name, and the run of the base runs only "
            f"that test.  The capped lease of the vessel gives ctest the jobs of the slice of the gate times "
            f"VESSEL_WEIGHT, and not the {POOL_SLOTS} slots of the pool: {red_verdict.problems}, {alone}",
            not red_verdict.is_accepted
            and red_verdict.problems[0].startswith("vessel: red alone too: vessel_test_red, see ")
            and red_verdict.focus == (gate.Focus(gate.VESSEL, ("vessel_test_red",)),)
            and base_verdict.configurations == (gate.VESSEL,)
            and base_verdict.problems == red_verdict.problems
            and alone == [f"{PLANTED_JOBS * gate.VESSEL_WEIGHT} vessel_test_red"] * len(alone)
            and PLANTED_JOBS * gate.VESSEL_WEIGHT < POOL_SLOTS,
        )
        (build / "red.txt").unlink()
        listing_of[0] = vessel_listing(())
        empty_verdict, _ = checked(gate.Candidate(notes, moded, (script,)))
        expect(
            f"a build with no vessel test rejects the commit: {empty_verdict.problems}",
            not empty_verdict.is_accepted
            and empty_verdict.problems[0].startswith(f"vessel: no test with the label {gate.VESSEL_LABEL} runs"),
        )
        listing_of[0] = vessel_listing(("vessel_test_mlp",))
        torch.rmdir()
        bare_verdict, _ = checked(gate.Candidate(notes, moded, (script,)))
        expect(
            f"a main work tree with no patched PyTorch rejects the commit, and the problem names the directory: "
            f"{bare_verdict.problems}",
            not bare_verdict.is_accepted
            and bare_verdict.problems[0].startswith("vessel: the main work tree ")
            and f"has no directory {gate.TORCH_VENV}" in bare_verdict.problems[0]
            and "refuses the commit" in bare_verdict.problems[0],
        )


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
        with gate.free_lane(work / "land", 3) as first_lane:
            lanes.append(first_lane)
            with gate.free_lane(work / "land", 3) as second_lane:
                lanes.append(second_lane)
                with gate.free_lane(work / "land", 3) as third_lane, gate.free_lane(work / "land", 3) as no_lane:
                    lanes += [third_lane, no_lane]
        with open(work / "land" / gate.LOCK, "a", encoding="ascii") as earlier:
            fcntl.flock(earlier, fcntl.LOCK_EX)
            with gate.free_lane(work / "land", 3) as beside_earlier:
                lanes.append(beside_earlier)
        expect(
            f"each landing takes the first free lane, the lanes end at their count, and a holder of the first lane with "
            f"no lock of PRIMARY, such as an earlier copy, leaves no lane free: {lanes}",
            lanes == [0, 1, 2, None, None]
            and gate.Gate(work, work / "land", 1, False, lane=2).tree == work / "land" / "tree-3"
            and gate.LANES == min(gate.MOST_LANES, max(1, proc.DEFAULT_JOBS // heavy.SLICE))
            and gate.LANES <= 5,
        )
        names: list[str] = []
        for _ in range(gate.RUNS_KEPT + 2):
            with gate.hold_run(work / "land") as name:
                names.append(name)
        kept = [path.name for path in sorted((work / "land" / gate.LOGS).iterdir())]
        expect(
            "each run gets a new directory of logs one above the highest, and only the newest run and the last ended "
            "runs of the rule before it stay",
            names[0] == f"{gate.RUN_MARK}{1:0{gate.RUN_WIDTH}d}"
            and len(set(names)) == len(names)
            and kept == names[-(gate.RUNS_KEPT + 1) :],
        )
        planted = gate.Gate(work, work / "land", 1, False, run=names[-1])
        expect(
            "a gate of a run writes its logs into the directory of the run",
            planted.logs == work / "land" / gate.LOGS / names[-1]
            and gate.Gate(work, work / "land", 1, False).logs == work / "land" / gate.LOGS,
        )


def cited_run_cases(expect: testing.Expect) -> None:
    """Hold the ended runs that a verdict cites: they stay past RUNS_KEPT in the bounds of age and bytes (THE LOGS)."""

    def ended_runs(land: Path, count: int, size: int) -> list[str]:
        """Make `count` ended runs from run 1, each with one log of `size` bytes, and give their names."""
        names = [f"{gate.RUN_MARK}{number:0{gate.RUN_WIDTH}d}" for number in range(1, count + 1)]
        for name in names:
            (land / gate.LOGS / name).mkdir(parents=True)
            (land / gate.LOGS / name / "default.test.log").write_bytes(b"x" * size)
        return names

    def kept(land: Path) -> set[str]:
        """Give the name of each run directory that stays."""
        return {path.name for path in paths.listing(land / gate.LOGS, f"{gate.RUN_MARK}*")}

    with tempfile.TemporaryDirectory(prefix="gate-cited-") as work_text:
        land = Path(work_text) / "land"
        names = ended_runs(land, gate.RUNS_KEPT + 2, 10)
        with gate.hold_run(land, frozenset({names[0]})):
            stays = kept(land)
        expect(
            "an ended run that a verdict cites stays past the last RUNS_KEPT ended runs, and the run after it goes",
            names[0] in stays and names[1] not in stays,
        )
        with gate.hold_run(land, frozenset()):
            stays = kept(land)
        expect("after the read of the verdict, the next sweep removes the run", names[0] not in stays)
    with tempfile.TemporaryDirectory(prefix="gate-cited-age-") as work_text:
        land = Path(work_text) / "land"
        names = ended_runs(land, gate.RUNS_KEPT + 1, 10)
        past = time.time() - gate.CITED_RUN_AGE_S - 3600.0
        os.utime(land / gate.LOGS / names[0], (past, past))
        with gate.hold_run(land, frozenset({names[0]})):
            stays = kept(land)
        expect("a cited run older than CITED_RUN_AGE_S goes", names[0] not in stays)
    with tempfile.TemporaryDirectory(prefix="gate-cited-bytes-") as work_text:
        land = Path(work_text) / "land"
        names = ended_runs(land, gate.RUNS_KEPT + 2, 1000)
        with gate.hold_run(land, frozenset(names[:2]), max_bytes=1500):
            stays = kept(land)
        expect(
            "over the byte bound of the cited runs, the oldest cited run goes first, and the next one stays",
            names[0] not in stays and names[1] in stays,
        )


def live_run_cases(expect: testing.Expect) -> None:
    """Hold a run live through a burst of short runs: its logs stay, and they go as an ended run after it (THE LOGS)."""
    with tempfile.TemporaryDirectory(prefix="gate-live-") as work_text:
        land = Path(work_text) / "land"
        logs = land / gate.LOGS
        counts: list[int] = []
        with gate.hold_run(land) as live:
            is_live = heavy.is_held(logs / live / gate.LIVE)
            for _ in range(gate.RUNS_KEPT + 2):
                with gate.hold_run(land):
                    counts.append(len(paths.listing(logs, f"{gate.RUN_MARK}*")))
            is_kept = (logs / live).is_dir()
        expect(
            f"a run holds the lock of its directory, a burst of {gate.RUNS_KEPT + 2} short runs keeps the directory of "
            f"the live run, and the bound counts only the ended runs: {counts}",
            is_live and is_kept and counts[-1] == gate.RUNS_KEPT + 2 and max(counts) == gate.RUNS_KEPT + 2,
        )
        is_ended = not heavy.is_held(logs / live / gate.LIVE)
        with gate.hold_run(land):
            is_gone = not (logs / live).exists()
        expect(
            "after the holder ends, its lock is free, and the next run removes the directory as the oldest ended run",
            is_ended and is_gone,
        )

    with tempfile.TemporaryDirectory(prefix="gate-earlier-") as work_text:
        land = Path(work_text) / "land"
        count = gate.RUNS_KEPT + 2
        earlier = [land / gate.LOGS / f"{gate.RUN_MARK}{number:0{gate.RUN_WIDTH}d}" for number in range(1, count + 1)]
        for path in earlier:
            path.mkdir(parents=True)
        with gate.hold_run(land) as name:
            kept = [path.name for path in paths.listing(land / gate.LOGS, f"{gate.RUN_MARK}*")]
        expect(
            "a directory of a run with no file of the lock, such as a run of an earlier copy of the module, is an ended "
            f"run: {name}, {kept[0]}",
            name == f"{gate.RUN_MARK}{count + 1:0{gate.RUN_WIDTH}d}"
            and kept == [path.name for path in earlier[-gate.RUNS_KEPT :]] + [name],
        )

    with tempfile.TemporaryDirectory(prefix="gate-check-") as work_text:
        planted = gate.Gate(Path(work_text), Path(work_text) / "land", 1, False)
        seen: list[tuple[str, bool]] = []

        def probed(self: gate.Gate, candidate: gate.Candidate, is_retry: bool) -> gate.Verdict:
            """Record the run of the check and if its directory is live, and give an accepted verdict."""
            seen.append((self.run, heavy.is_held(self.logs / gate.LIVE)))
            if is_retry:
                raise gate.GateError("a planted error of the check")
            return gate.Verdict(("debug",), (), 1.0, logs=str(self.logs))

        candidate = gate.Candidate("a" * 40, "b" * 40, ())
        with mock.patch.object(gate.Gate, "check", probed):
            verdict = planted.check_in_run(candidate, False)
            try:
                planted.check_in_run(candidate, True)
                is_raised = False
            except gate.GateError:
                is_raised = True
        runs = [run for run, _ in seen]
        expect(
            "each check of a landing runs in a new run that is live for the whole check, and free after it, also "
            f"after an error: {seen}",
            [is_live for _, is_live in seen] == [True, True]
            and len(set(runs)) == 2
            and verdict.logs == str(planted.work / gate.LOGS / runs[0])
            and is_raised
            and not any(heavy.is_held(planted.work / gate.LOGS / run / gate.LIVE) for run in runs),
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
            repair_cases,
            plan_cases,
            parse_heal_cases,
            hang_guard_cases,
            change_cases,
            run_cases,
            reduce_cases,
            partial_cases,
            counted_cases,
            reduce_parse_cases,
            built_cases,
            built_fixture_cases,
            ratchet_cases,
            budget_cases,
            changed_tests_cases,
            touched_cases,
            thread_cases,
            memory_cases,
            kind_cases,
            oom_cases,
            stop_cases,
            masked_error_cases,
            base_run_cases,
            item_cases,
            vessel_select_cases,
            vessel_run_cases,
            careful_cases,
            lock_cases,
            live_run_cases,
            cited_run_cases,
        ),
    )
