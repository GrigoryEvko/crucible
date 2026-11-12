"""The part tests of the rule registry: each test that runs a master has the label ci_guard, and each rule has its test.

`ctest -L ci_guard` is the local gate.  A guard that is registered without the
label is invisible to that command, so the gate reports a pass and never runs
the guard.  On 2026-05 twelve of 42 script tests had no label.

THE INPUT
    The part reads the registered tests of a configured build directory
    through the reader assay.readers.ctest.  CMake has evaluated each
    function, loop, subdirectory and property call, so the command and the
    properties of each test are the values that ctest uses.  The backtrace of
    a test gives the CMake file and the line of its add_test.  The part does
    not apply with no build directory, with a build directory that ctest
    cannot read, or with a build that registers no test of a master
    (registered_tests()).

THE PART
    A test is a script test when one argument of its command is a master of
    ROOT/utils/bin/ (guard, loop or setup), or a path under
    ROOT/utils/scripts/.  The test launcher `loop test-step` and its options
    come in front of a command, and the part reads the first master after
    them.  The part refuses:
      label         a script test with no label ci_guard.  The launcher in front of an executable alone, and the
                    driver `loop fixture` of a negative fixture, are not script tests
      unknown-rule  a test that runs `guard check --rule ID` or `guard witness --rule ID` for an ID that the registry
                    does not hold
      no-test       a rule of the registry that no test with the label ci_guard runs with `guard check --rule ID`, or
                    with `guard check --engine E` for a rule of the engine E with a module of its own.  A measured
                    rule with a holder has no run of its own, and the part does not read it.  The ledger admits each
                    rule that a CI step or a person runs
      fixture       a test that runs `guard check --rule ID`, with no --self-test, for a rule whose module reads the
                    parse trees, and that does not require the fixture tsast_trees.  The module reads the parse
                    trees when its import closure in the package holds assay.readers.tsast.  With no fixture, the test
                    can read a cold tree cache under load.  A test that sets up the fixture fills the cache, and the
                    part does not read it
      unread-fixture  a test with FIXTURES_REQUIRED tsast_trees that reads no tree of the tree: a test of `guard
                    check` with no rule that has a module that reads the parse trees, or a test of `guard witness`.
                    A witness copies each plant to a scratch repository and reads only the plant.  Such a test waits
                    for the eight parts of the fill with no use
      environment   a test behind the test launcher with a property ENVIRONMENT that sets a variable that the
                    launcher drops (assay.loop.teststep, THE ENVIRONMENT).  The test does not get the variable.
                    An item of ENVIRONMENT_MODIFICATION with such a variable is an error too, unless its operation
                    is `unset` or `reset`
      unjudged      a test with no test launcher and no fixture driver in its command: no word of the command is
                    the master loop with the subcommand test-step or fixture.  The test then gets no record, no
                    judge of its memory and its instructions, and no clean environment.  CMake gives the launcher
                    to a test of an executable target, and crucible_script_test() of cmake/TestLauncher.cmake
                    gives it to a script test

THE PARTS OF A RULE
    A test of `guard check --rule ID --part K/N` runs part K of N of the
    files of the rule (THE PARTS of assay.guard.engines.meta.runner).  The
    parts of one count N are disjoint, and together they hold each file.  A
    lost part thus leaves files with no check, and the run of each other
    part gives no sign of it.  The part reads the tests with the label
    ci_guard, and it refuses:
      bad-part      a test whose word after --part is not K/N with 1 <= K <= N
      missing-part  a part K of a count N of a rule that no such test runs
      part-count    a rule that such tests run with two counts N.  Part K of
                    each count writes the warnings file RULE.partK.txt, so
                    the two counts write over each other
    No ledger row admits a finding of these kinds (PART_KINDS).

Complexity: O(t * a) for t tests of a arguments, and linear in the size of the modules of the package.
"""

import ast
import itertools
from collections import defaultdict
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import (
    MetaContext,
    NotApplicable,
    copy_tree,
    module_file,
    module_name_of,
    parsed_part,
)
from assay.loop import teststep as test_launcher
from assay.readers import ctest as ctest_reader

PART = "tests"
LABEL = "ci_guard"
MASTERS = ("guard", "loop", "setup")
# The master and the subcommand of the test launcher, which comes in front of a command.
LAUNCHER = ("loop", "test-step")
# The master and the subcommand of the driver of a negative fixture, which is not a script test.
FIXTURE = ("loop", "fixture")
TREE_FIXTURE = "tsast_trees"
TREE_READER = "assay.readers.tsast"
CMAKE_TIMEOUT_S = 300.0
# The operations of ENVIRONMENT_MODIFICATION that give the test no value of a variable.
CLEARING_OPERATIONS = ("unset", "reset")
# The option of a part of the files of a rule, and the kinds of THE PARTS OF A RULE, which no ledger row admits.
PART_OPTION = "--part"
BAD_PART = "bad-part"
MISSING_PART = "missing-part"
PART_COUNT = "part-count"
PART_KINDS = frozenset({BAD_PART, MISSING_PART, PART_COUNT})


def master_of(test: ctest_reader.TestEntry, root: Path) -> tuple[str, list[str]] | None:
    """Return the master and the words after it, when the command of a test runs a master of ROOT/utils/bin.

    A command that runs a script of ROOT/utils/scripts/ gives the path of the
    script relative to the root, and the words after it: a shell script of
    that directory is a script test too.  The test launcher is not the master
    of a command: the search goes on after it.
    """
    bin_dir = (root / "utils" / "bin").resolve()
    scripts_dir = (root / "utils" / "scripts").resolve()
    command = list(test.command)
    for index, argument in enumerate(command):
        path = Path(argument)
        if not path.is_absolute():
            continue
        resolved = path.resolve()
        words = command[index + 1 :]
        is_launcher = (path.name, words[:1]) == (LAUNCHER[0], [LAUNCHER[1]])
        if resolved.parent == bin_dir and path.name in MASTERS and not is_launcher:
            return path.name, words
        if resolved.is_relative_to(scripts_dir):
            return resolved.relative_to(root).as_posix(), words
    return None


def is_judged(command: Sequence[str]) -> bool:
    """Return True when a command starts the test launcher or the fixture driver: the master loop and its subcommand.

    The master is the file name of a word, so a command of the master of a different tree counts too.  Complexity:
    linear in the words of the command.
    """
    return any((Path(word).name, following) in (LAUNCHER, FIXTURE) for word, following in itertools.pairwise(command))


def place_of(test: ctest_reader.TestEntry, root: Path) -> tuple[str, int]:
    """Return the CMake file and the line of the add_test of a test, relative to the root when it is in the tree."""
    if not test.file:
        return "CMakeLists.txt", 0
    path = Path(test.file)
    return (path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path)), test.line


def rule_words(words: Sequence[str]) -> list[str]:
    """Return each ID after --rule in the words of a command of guard."""
    return [words[index + 1] for index, word in enumerate(words[:-1]) if word == "--rule"]


def engine_rules(words: Sequence[str], registry: rule_registry.Registry) -> list[rule_registry.Rule]:
    """Return each rule that `guard check --engine E` runs for each engine E of the words: a rule with a module."""
    engines = {words[index + 1] for index, word in enumerate(words[:-1]) if word == "--engine"}
    return [rule for rule in registry.rules.values() if rule.engine.value in engines and rule.has_module]


def imports_of(path: Path) -> set[str]:
    """Return the full dotted name of each module of the package that a file imports, also inside a function."""
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), str(path))
    except SyntaxError:
        return set()
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module is not None and node.level == 0:
            names.add(node.module)
            names.update(f"{node.module}.{alias.name}" for alias in node.names)
    return {name for name in names if name == "assay" or name.startswith("assay.")}


def reads_trees(root: Path, module: str, cache: dict[str, bool], imports: dict[str, frozenset[str]]) -> bool:
    """Whether the import closure of a module of the package holds the reader of the parse trees.

    `cache` holds the answer of each module, and `imports` holds the imports of each module that the call parsed.
    The rules of one call share the two tables.  The call then parses each module of the package one time, and not
    one time for each rule.

    Complexity: linear in the size of the modules of the closure, with one parse of each module.  For each rule, linear
    in the edges of its closure.
    """
    if module in cache:
        return cache[module]
    cache[module] = False
    seen = {module}
    pending = [module]
    while pending:
        current = pending.pop()
        if current == TREE_READER:
            cache[module] = True
            return True
        if current not in imports:
            path = module_file(root, current)
            imports[current] = frozenset(imports_of(path)) if path is not None else frozenset()
        for name in imports[current]:
            if name not in seen:
                seen.add(name)
                pending.append(name)
    return False


def implementing_module(rule: rule_registry.Rule, root: Path) -> str | None:
    """Return the module that runs a rule: its legacy module, its own module or its family module, or None for a rule
    with a holder."""
    if rule.legacy is not None:
        return rule.legacy.qualified
    if rule.held_by is not None:
        return None
    return module_name_of(rule, root)


def unread_fixture(test: ctest_reader.TestEntry, path: str, line: int, why: str) -> check_report.Finding:
    """Return the error of a test with FIXTURES_REQUIRED tsast_trees that reads no tree of the tree (THE PART)."""
    message = (
        f"the test {test.name} requires the fixture {TREE_FIXTURE}, and {why}.  It waits for the fill of the tree "
        f"cache with no use.  Remove FIXTURES_REQUIRED {TREE_FIXTURE}"
    )
    return check_report.Finding("error", path, line, PART, message, "unread-fixture", test.name)


def environment_findings(test: ctest_reader.TestEntry, root: Path) -> list[check_report.Finding]:
    """Return an error for each variable that a property gives a test behind the launcher and the launcher drops.

    THE PART gives the kind environment.  Complexity: linear in the items of the two properties.
    """
    command = list(test.command)
    if test_launcher.launched(command) == command:
        return []
    path, line = place_of(test, root)
    found: list[check_report.Finding] = []
    for item in test.words_of("ENVIRONMENT"):
        name = item.partition("=")[0]
        if not test_launcher.is_kept(name):
            found.append(dropped_variable(test, path, line, "ENVIRONMENT", name))
    for item in test.words_of("ENVIRONMENT_MODIFICATION"):
        name, _, change = item.partition("=")
        if change.partition(":")[0] not in CLEARING_OPERATIONS and not test_launcher.is_kept(name):
            found.append(dropped_variable(test, path, line, "ENVIRONMENT_MODIFICATION", name))
    return found


def dropped_variable(test: ctest_reader.TestEntry, path: str, line: int, prop: str, name: str) -> check_report.Finding:
    """Return the error of a variable that a property gives a test behind the launcher and the launcher drops."""
    message = (
        f"the test {test.name} runs behind the test launcher, and its property {prop} gives it the variable {name}.  "
        f"The launcher drops that variable (assay.loop.teststep, THE ENVIRONMENT), so the test does not get it.  Give "
        f"the variable a name that the launcher keeps, such as CRUCIBLE_{name}"
    )
    return check_report.Finding("error", path, line, PART, message, "environment", f"{test.name}: {name}")


@dataclass(frozen=True, slots=True)
class PartRun:
    """One test that runs part K of N of a rule (THE PARTS OF A RULE): the part, the test and its place."""

    index: int
    count: int
    test: str
    path: str
    line: int


def part_words(words: Sequence[str]) -> list[str]:
    """Return each word after --part in the words of a command of guard."""
    return [words[index + 1] for index, word in enumerate(words[:-1]) if word == PART_OPTION]


def part_findings(parts: dict[str, list[PartRun]]) -> list[check_report.Finding]:
    """Return an error for each rule with a part K of N that no test runs, or with two counts (THE PARTS OF A RULE).

    `parts` gives the part runs of the tests with the label ci_guard for each rule.  Complexity: linear in the part
    runs and in the counts N.
    """
    found: list[check_report.Finding] = []
    for rule_id, runs in sorted(parts.items()):
        if not runs:
            continue
        first = min(runs, key=lambda run: (run.path, run.line, run.test))
        counts = sorted({run.count for run in runs})
        if len(counts) > 1:
            shown = ", ".join(
                f"{count} ({', '.join(sorted(run.test for run in runs if run.count == count))})" for count in counts
            )
            message = (
                f"the tests of the rule {rule_id} run it in parts of {len(counts)} counts: {shown}.  Part K of each "
                f"count writes the warnings file {rule_id}.partK.txt, so the counts write over each other.  Register "
                f"the parts of one count"
            )
            found.append(check_report.Finding("error", first.path, first.line, PART, message, PART_COUNT, rule_id))
        for count in counts:
            held = {run.index for run in runs if run.count == count}
            for index in sorted(set(range(1, count + 1)) - held):
                message = (
                    f"the rule {rule_id} runs in {count} parts, and no test with the label {LABEL} runs `guard check "
                    f"--rule {rule_id} --part {index}/{count}`.  The files of that part get no check.  Register a test "
                    f"for each part K from 1 to {count}"
                )
                found.append(
                    check_report.Finding(
                        "error", first.path, first.line, PART, message, MISSING_PART, f"{rule_id}: {index}/{count}"
                    )
                )
    return found


def script_run(test: ctest_reader.TestEntry, root: Path) -> tuple[str, list[str]] | None:
    """Return the master and the words of a script test (THE PART), or None for each other test.

    The driver of a negative fixture is not a script test.
    """
    run = master_of(test, root)
    if run is None or (run[0], run[1][0] if run[1] else "") == FIXTURE:
        return None
    return run


def registered_tests(context: MetaContext) -> tuple[ctest_reader.TestEntry, ...]:
    """Return the tests that ctest registers in the build of a context (THE INPUT).

    Raises:
        NotApplicable: If the context gives no build directory, ctest cannot read the build, or no test of the build
            runs a master of ROOT/utils/bin
    """
    if context.build_dir is None:
        raise NotApplicable("the part reads the tests of a build directory.  Give --build-dir DIR")
    try:
        tests = ctest_reader.tests(context.build_dir)
    except ctest_reader.CtestError as problem:
        raise NotApplicable(str(problem)) from None
    root = context.root.resolve()
    if not any(script_run(test, root) is not None for test in tests):
        raise NotApplicable(
            f"{context.build_dir} registers no test that runs a master of {root}/utils/bin.  The build directory or "
            f"the root is wrong"
        )
    return tests


def tests_findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each test with a master and no label, each unknown rule, each rule with no test, each missing or unread
    fixture, each variable that the launcher drops, each test with no judge, and each lost part of a rule.

    Raises:
        NotApplicable: If the part does not apply to the build (registered_tests())
    """
    tests = registered_tests(context)
    registry = context.registry
    root = context.root.resolve()
    found: list[check_report.Finding] = []
    covered: set[str] = set()
    parts: defaultdict[str, list[PartRun]] = defaultdict(list)
    tree_cache: dict[str, bool] = {}
    module_imports: dict[str, frozenset[str]] = {}
    for test in tests:
        found += environment_findings(test, root)
        if not is_judged(test.command):
            path, line = place_of(test, root)
            # ctest gives no command for a test whose program it does not find.
            shown = f"`{' '.join(test.command[:3])}`" if test.command else "a program that ctest does not find"
            message = (
                f"the test {test.name} runs {shown} with no test launcher and no fixture driver, so it gets no "
                f"record, no judge of its cost and no clean environment.  Register it with crucible_script_test() of "
                f"cmake/TestLauncher.cmake"
            )
            found.append(check_report.Finding("error", path, line, PART, message, "unjudged", test.name))
        run = script_run(test, root)
        if run is None:
            continue
        master, words = run
        name = test.name
        path, line = place_of(test, root)
        labels = test.words_of("LABELS")
        if LABEL not in labels:
            message = (
                f"the test {name} runs `{' '.join([master, *words[:3]])}` and has no label {LABEL}, so `ctest -L "
                f"{LABEL}` does not run it.  Add the label beside its registration"
            )
            found.append(check_report.Finding("error", path, line, PART, message, "label", name))
        if master != "guard" or not words or words[0] not in ("check", "witness"):
            continue
        run_rules: list[rule_registry.Rule] = []
        for rule_id in rule_words(words):
            rule = registry.rules.get(rule_id)
            if rule is None:
                message = f"the test {name} runs the rule {rule_id}, and the registry has no such rule"
                found.append(check_report.Finding("error", path, line, PART, message, "unknown-rule", name))
                continue
            run_rules.append(rule)
        requires_trees = TREE_FIXTURE in test.words_of("FIXTURES_REQUIRED")
        if words[0] == "witness" and requires_trees:
            found.append(unread_fixture(test, path, line, "it runs `guard witness`, which reads only the plants"))
        if words[0] != "check" or "--self-test" in words:
            continue
        reads_any = False
        part = None
        for word in part_words(words):
            part = parsed_part(word)
            if part is None:
                message = (
                    f"the test {name} runs the part {word}, which is not K/N with 1 <= K <= N, so `guard check` "
                    f"refuses it.  Give --part K/N"
                )
                found.append(check_report.Finding("error", path, line, PART, message, BAD_PART, name))
        for rule in [*run_rules, *engine_rules(words, registry)]:
            rule_id = rule.id
            if LABEL in labels:
                covered.add(rule_id)
                if part is not None:
                    parts[rule_id].append(PartRun(part[0], part[1], name, path, line))
            module = implementing_module(rule, root)
            reads = module is not None and reads_trees(root, module, tree_cache, module_imports)
            reads_any = reads_any or reads
            if reads and not requires_trees and TREE_FIXTURE not in test.words_of("FIXTURES_SETUP"):
                message = (
                    f"the test {name} runs the rule {rule_id}, whose module {module} reads the parse trees, and it "
                    f"does not require the fixture {TREE_FIXTURE}.  Give it FIXTURES_REQUIRED {TREE_FIXTURE}"
                )
                found.append(check_report.Finding("error", path, line, PART, message, "fixture", name))
        if requires_trees and not reads_any:
            found.append(unread_fixture(test, path, line, "no module of its rules reads the parse trees"))
    for rule in registry.rules.values():
        if rule.held_by is None and rule.id not in covered:
            message = f"no test with the label {LABEL} runs `guard check --rule {rule.id}`.  Register its test"
            found.append(
                check_report.Finding("error", rule_registry.REGISTRY_PATH, rule.line, PART, message, "no-test", rule.id)
            )
    return [*found, *part_findings(parts)]


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a planted CMake project, configure it with the pinned cmake, and give its context with its build directory.

    Raises:
        NotApplicable: If the pinned cmake is missing, or the project does not configure
    """
    return configured(copy_tree(plant, scratch), scratch)


def configured(context: MetaContext, scratch: Path) -> MetaContext:
    """Configure the planted CMake project of a copied plant in SCRATCH/build, and give the context with that build.

    The plant of a different rule that reads the tests of a build uses it too, after its own copy of the plant.

    Raises:
        NotApplicable: If the pinned cmake is missing, or the project does not configure
    """
    from assay.readers import cmake as cmake_reader
    from assay.setup import pins

    build = scratch / "build"
    try:
        cmake = pins.pinned_program("cmake")
    except pins.PinError as problem:
        raise NotApplicable(str(problem)) from None
    # The planted project has no cmake_file_api call.  Its build directory gets the query of the reply of the
    # cache, which gives the ctest of the build.
    cmake_reader.request(build)
    completed = proc.run(proc.Command([cmake, "-S", str(scratch), "-B", str(build)], timeout_s=CMAKE_TIMEOUT_S))
    if completed.exit != 0:
        raise NotApplicable(f"the planted project does not configure: {completed.err.strip()}")
    return MetaContext(context.root, build)
