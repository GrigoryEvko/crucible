"""test-registration — each test that runs a master carries the label ci_guard, and each rule has its test with its input.

`ctest -L ci_guard` is the local gate.  A guard that is registered without the
label is invisible to that command, so the gate reports a pass and never runs
the guard.  On 2026-05 twelve of 42 script tests had no label.

THE INPUT
    The rule reads the registered tests of a configured build directory
    through the reader assay.readers.ctest.  CMake has evaluated each
    function, loop, subdirectory and property call, so the command and the
    properties of each test are the values that ctest uses.  The backtrace of
    a test gives the CMake file and the line of its add_test.

THE RULE
    A test is a script test when one argument of its command is a master of
    ROOT/utils/bin/ (guard, loop or setup), or a path under
    ROOT/utils/scripts/.  The rule refuses:
      label         a script test with no label ci_guard.  The launcher `loop test-step`, which CMake
                    puts in front of each executable test, and the driver `loop fixture` of a negative fixture are
                    not script tests
      unknown-rule  a test that runs `guard check --rule ID` or `guard witness --rule ID` for an ID that the registry
                    does not hold
      no-test       a rule of the registry that no test with the label ci_guard runs with `guard check --rule ID`.  A
                    measured rule with a holder has no run of its own, and the rule does not read it.  The ledger
                    admits each rule that a CI step or a person runs
      fixture       a test that runs `guard check --rule ID`, with no --self-test, for a rule whose module reads the
                    parse trees, and that does not require the fixture tsast_trees.  The module reads the parse
                    trees when its import closure in the package holds assay.readers.tsast.  With no fixture, the test
                    can read a cold tree cache under load.  A test that sets up the fixture fills the cache, and the
                    rule does not read it

Complexity: O(t * a) for t tests of a arguments, and linear in the size of the modules of the package.
"""

import ast
from collections.abc import Sequence
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core import registry as rule_registry
from assay.guard.engines.meta.runner import MetaContext, NotApplicable, copy_tree
from assay.readers import ctest as ctest_reader

RULE = "test-registration"
LABEL = "ci_guard"
MASTERS = ("guard", "loop", "setup")
# The master and the subcommand of the two commands that are not script tests.
NOT_SCRIPT = (("loop", "test-step"), ("loop", "fixture"))
TREE_FIXTURE = "tsast_trees"
TREE_READER = "assay.readers.tsast"
CMAKE_TIMEOUT_S = 300.0


def master_of(test: ctest_reader.CtestTest, root: Path) -> tuple[str, list[str]] | None:
    """Return the master and the words after it, when the command of a test runs a master of ROOT/utils/bin.

    A command that runs a script of ROOT/utils/scripts/ gives the path of the
    script relative to the root, and the words after it: a shell script of
    that directory is a script test too.
    """
    bin_dir = (root / "utils" / "bin").resolve()
    scripts_dir = (root / "utils" / "scripts").resolve()
    command = list(test.command)
    for index, argument in enumerate(command):
        path = Path(argument)
        if not path.is_absolute():
            continue
        resolved = path.resolve()
        if resolved.parent == bin_dir and path.name in MASTERS:
            return path.name, command[index + 1:]
        if resolved.is_relative_to(scripts_dir):
            return resolved.relative_to(root).as_posix(), command[index + 1:]
    return None


def place_of(test: ctest_reader.CtestTest, root: Path) -> tuple[str, int]:
    """Return the CMake file and the line of the add_test of a test, relative to the root when it is in the tree."""
    if not test.file:
        return "CMakeLists.txt", 0
    path = Path(test.file)
    return (path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path)), test.line


def rule_words(words: Sequence[str]) -> list[str]:
    """Return each ID after --rule in the words of a command of guard."""
    return [words[index + 1] for index, word in enumerate(words[:-1]) if word == "--rule"]


def module_file(root: Path, module: str) -> Path | None:
    """Return the file of a module of the package assay under the root, or None."""
    base = root / "utils" / Path(*module.split("."))
    for candidate in (base.with_suffix(".py"), base / "__init__.py"):
        if candidate.is_file():
            return candidate
    return None


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


def reads_trees(root: Path, module: str, cache: dict[str, bool]) -> bool:
    """Whether the import closure of a module of the package holds the reader of the parse trees.

    Complexity: linear in the size of the modules of the closure, each read one time through the cache.
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
        path = module_file(root, current)
        if path is None:
            continue
        for name in imports_of(path):
            if name not in seen:
                seen.add(name)
                pending.append(name)
    return False


def implementing_module(rule: rule_registry.Rule) -> str | None:
    """Return the module that runs a rule: its legacy module or its own module, or None for a measured rule with a holder."""
    if rule.legacy is not None:
        return rule.legacy.qualified
    if rule.held_by is not None:
        return None
    return f"assay.guard.engines.{rule.engine.value}.{rule.id.replace('-', '_')}"


def ctest_program(build_dir: Path) -> str:
    """Return the ctest of a build directory.

    Raises:
        NotApplicable: If the build directory names no usable ctest
    """
    from assay.setup import pins

    try:
        return pins.configured_program(build_dir, "ctest")
    except pins.PinError as problem:
        raise NotApplicable(str(problem)) from None


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each test with a master and no label, each unknown rule, each rule with no test and each missing fixture."""
    if context.build_dir is None:
        raise NotApplicable("the rule reads the tests of a build directory.  Give --build-dir DIR")
    try:
        tests = ctest_reader.listing(ctest_program(context.build_dir), context.build_dir)
    except ctest_reader.CtestError as problem:
        raise NotApplicable(str(problem)) from None
    registry = context.registry
    root = context.root.resolve()
    found: list[check_report.Finding] = []
    covered: set[str] = set()
    tree_cache: dict[str, bool] = {}
    masters = 0
    for test in tests:
        run = master_of(test, root)
        if run is None:
            continue
        master, words = run
        if (master, words[0] if words else "") in NOT_SCRIPT:
            continue
        masters += 1
        name = test.name
        path, line = place_of(test, root)
        labels = test.words_of("LABELS")
        if LABEL not in labels:
            found.append(check_report.Finding(
                "error", path, line, RULE, f"the test {name} runs `{' '.join([master, *words[:3]])}` and has no label "
                f"{LABEL}, so `ctest -L {LABEL}` does not run it.  Add the label beside its add_test", "label", name))
        if master != "guard" or not words or words[0] not in ("check", "witness"):
            continue
        for rule_id in rule_words(words):
            rule = registry.rules.get(rule_id)
            if rule is None:
                found.append(check_report.Finding(
                    "error", path, line, RULE, f"the test {name} runs the rule {rule_id}, and the registry has no "
                    f"such rule", "unknown-rule", name))
                continue
            if words[0] != "check" or "--self-test" in words:
                continue
            if LABEL in labels:
                covered.add(rule_id)
            module = implementing_module(rule)
            if (module is not None and reads_trees(root, module, tree_cache)
                    and TREE_FIXTURE not in test.words_of("FIXTURES_REQUIRED")
                    and TREE_FIXTURE not in test.words_of("FIXTURES_SETUP")):
                found.append(check_report.Finding(
                    "error", path, line, RULE, f"the test {name} runs the rule {rule_id}, whose module {module} reads "
                    f"the parse trees, and it does not require the fixture {TREE_FIXTURE}.  Give it "
                    f"FIXTURES_REQUIRED {TREE_FIXTURE}", "fixture", name))
    if masters == 0:
        raise NotApplicable(f"{context.build_dir} registers no test that runs a master of {root}/utils/bin.  The build "
                            f"directory or the root is wrong")
    for rule in registry.rules.values():
        if rule.held_by is None and rule.id not in covered:
            found.append(check_report.Finding(
                "error", rule_registry.REGISTRY_PATH, rule.line, RULE, f"no test with the label {LABEL} runs "
                f"`guard check --rule {rule.id}`.  Register its test", "no-test", rule.id))
    return found


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a planted CMake project, configure it with the pinned cmake, and give its context with its build directory.

    Raises:
        NotApplicable: If the pinned cmake is absent, or the project does not configure
    """
    from assay.setup import pins

    context = copy_tree(plant, scratch)
    build = scratch / "build"
    try:
        cmake = pins.pinned_program("cmake")
    except pins.PinError as problem:
        raise NotApplicable(str(problem)) from None
    completed = proc.run(proc.Command([cmake, "-S", str(scratch), "-B", str(build)], timeout_s=CMAKE_TIMEOUT_S))
    if completed.exit != 0:
        raise NotApplicable(f"the planted project does not configure: {completed.err.strip()}")
    return MetaContext(context.root, build)
