"""guard write --all-configurations — write each counted ledger in the build directory of each of its configurations.

usage: guard write --all-configurations [--rule ID]... [--root DIR] [--jobs N] [--no-build]

THE CONFIGURATIONS
    A counted rule is a rule of the engine plugin or elf with the policy
    count and a ledger of configurations.  THE COUNTED RULES of
    assay.loop.gate gives them.  With no --rule, the command writes each
    counted rule.
    It takes each configuration of their ledgers that the table of
    assay.loop.gate holds.  The base configuration of a ledger comes first,
    and then the sequence of that table applies.  A configuration that the
    host cannot build gets a note, such as an aarch64 configuration of
    objects.  Its rows come from the log of its CI leg (`guard write --rule
    ID --import LOG`).

THE STEPS
    For each configuration, the command does these steps:
      1. The build directory of the configuration in the tree is the build
         directory of the gate, such as build, build-release or
         build-syntax-aarch64.  A directory with no graph of Ninja gets its
         configure, with the cache variables of the configuration.
      2. A configuration of objects builds the target all, and a parse
         configuration runs `loop syntax`.  The configure and the build run
         in one heavy step of assay.loop.heavy.  That step takes the shared
         lock of the heavy steps and waits for the memory.  Do not run the
         command with that lock, for example through flock(1).  --no-build
         does no configure and no build.  Each build directory must then
         have its graph.
      3. `guard write --rule ID --build-dir BUILD` of each chosen rule with a
         ledger that holds the configuration, in the mode lower.  That write
         refuses a count above its row and a new row, as each plain write
         does.
    A write in the build of a configuration changes only the counts of that
    configuration (THE WRITE of assay.core.ledger).  The writes of the
    configurations then give one ledger.  The first step that fails stops
    the command.  Each ledger of the call then gets its bytes from before
    the call again, and the ledgers change in each configuration or in none.

Exit 0 when each ledger is written, 1 when a step fails, 2 on a usage error or an unreadable registry or ledger.
`guard selftest tests.guard.test_write_configurations` does a test of the module on a planted tree.
"""

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cache, proc
from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard import write as guard_write
from assay.loop import gate, heavy
from assay.readers import ninja as ninja_reader


@dataclass(frozen=True, slots=True)
class Steps:
    """The build of a configuration and the write of one rule (THE STEPS), which a test can plant.

    `build` takes the tree, the configuration and the jobs, and gives the exit of steps 1 and 2.  `write` takes the
    tree, the rule and the build directory, and gives the exit of step 3.
    """

    build: Callable[[Path, gate.Configuration, int], int]
    write: Callable[[Path, str, Path], int]


def run_words(words: Sequence[str], root: Path) -> int:
    """Run one command in the tree with the outputs of this process, and give its exit."""
    try:
        completed = proc.run(
            proc.Command(
                list(words), timeout_s=proc.BUILD_S, cwd=root, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT
            )
        )
    except proc.ToolFailed as problem:
        check_report.write_line(f"guard write: {words[0]} cannot run: {problem}", is_error=True)
        return ExitCode.TOOL_FAILED
    return heavy.exit_of(completed)


def build_configuration(root: Path, configuration: gate.Configuration, jobs: int) -> int:
    """Configure the build directory of a configuration when it has no graph, and build it (THE STEPS, 1 and 2)."""
    from assay.loop import syntax

    build = gate.build_directory(root, configuration)
    with heavy.heavy_step(heavy.FULL_LEVEL_GB, root):
        if not ninja_reader.graph_file(build).is_file():
            status = run_words(gate.configure_words(root, configuration), root)
            if status != 0:
                return status
        if configuration.is_parse:
            return syntax.run(build, root, jobs)
        try:
            words = gate.build_words(root, configuration, jobs)
        except gate.GateError as problem:
            check_report.write_line(f"guard write: {problem}", is_error=True)
            return ExitCode.TOOL_FAILED
        return run_words(words, root)


def require_graph(root: Path, configuration: gate.Configuration, _jobs: int) -> int:
    """Give 0 when the build directory of a configuration has its graph, for --no-build, else write why and give 1."""
    build = gate.build_directory(root, configuration)
    if ninja_reader.graph_file(build).is_file():
        return 0
    check_report.write_line(
        f"guard write: {build} has no graph of Ninja.  Configure and build it, or give no --no-build", is_error=True
    )
    return ExitCode.FINDINGS


def write_rule(root: Path, rule_id: str, build: Path) -> int:
    """Write the ledger of one rule from one build directory with `guard write`, and give its exit (THE STEPS, 3)."""
    return guard_write.main(["--rule", rule_id, "--root", str(root), "--build-dir", str(build)])


STEPS = Steps(build_configuration, write_rule)


def planned(
    counted: Mapping[str, Sequence[str]], rules: Sequence[str]
) -> tuple[tuple[gate.Configuration, ...], tuple[str, ...]]:
    """Return the configurations of the ledgers of some rules, the bases first, and the names that the host cannot build.

    Complexity: linear in the names times the configurations of the table.
    """
    names = {name for rule in rules for name in counted[rule]}
    bases = {counted[rule][0] for rule in rules if counted[rule]}
    table = list(gate.CONFIGURATIONS)
    found = sorted(
        (item for item in table if item.ledger_name in names),
        key=lambda item: (item.ledger_name not in bases, table.index(item)),
    )
    return tuple(found), tuple(sorted(name for name in names if name not in gate.BY_LEDGER_NAME))


def restore(saved: Mapping[Path, bytes | None]) -> None:
    """Give each ledger file its bytes from before the call, or remove a file that was not there before the call."""
    for path, data in saved.items():
        if data is None:
            path.unlink(missing_ok=True)
        else:
            cache.write_atomic(path, data)


def run(root: Path, rules: Sequence[str], jobs: int, steps: Steps = STEPS) -> int:
    """Write the counted ledgers of some rules, or of each counted rule, in each of their configurations.

    Returns:
        0 when each ledger is written, 1 when a step fails.  2 for a rule that is not counted, or for a ledger that
        does not read

    Complexity: one build of each configuration, and one write of each rule in each configuration of its ledger.
    """
    try:
        counted = gate.counted_ledgers(root)
    except gate.GateError as problem:
        check_report.write_line(f"guard write: {problem}", is_error=True)
        return ExitCode.USAGE
    chosen = list(dict.fromkeys(rules)) or sorted(counted)
    unknown = [rule for rule in chosen if rule not in counted]
    if unknown:
        check_report.write_line(
            f"guard write: {', '.join(unknown)} is not a counted rule.  --all-configurations writes a rule of the "
            f"engine plugin or elf with the policy count and a ledger of configurations: {', '.join(sorted(counted))}",
            is_error=True,
        )
        return ExitCode.USAGE
    configurations, unbuilt = planned(counted, chosen)
    for name in unbuilt:
        check_report.write_line(
            f"guard write: the host does not build the configuration {name}.  Its rows come from the log of its CI "
            f"leg: `python3 utils/bin/guard write --rule ID --import LOG`"
        )
    own = {rule: counted[rule] for rule in chosen}
    saved = {
        path: path.read_bytes() if path.is_file() else None
        for path in (rule_ledger.ledger_path(rule, root) for rule in chosen)
    }
    for configuration in configurations:
        build = gate.build_directory(root, configuration)
        check_report.write_line(f"guard write: the configuration {configuration.ledger_name} in {build}")
        status = steps.build(root, configuration, jobs)
        if status != 0:
            restore(saved)
            check_report.write_line(
                f"guard write: the build of {configuration.name} exits {status}.  No ledger is written", is_error=True
            )
            return ExitCode.FINDINGS
        for rule in gate.holding(own, configuration):
            status = steps.write(root, rule, build)
            if status != 0:
                restore(saved)
                check_report.write_line(
                    f"guard write: the write of {rule} in {configuration.name} exits {status}.  No ledger is written",
                    is_error=True,
                )
                return ExitCode.FINDINGS
    shown = " ".join(item.ledger_name for item in configurations)
    check_report.write_line(f"guard write: wrote {' '.join(chosen)} in the configurations {shown}")
    return ExitCode.OK


def main(argv: list[str]) -> int:
    """Write each counted ledger of the command line in each of its configurations (THE STEPS).

    Returns:
        The exit of run(), or 2 on a usage error
    """
    from assay.core import cli

    parser = cli.parser("guard write", "write each counted ledger in the build directory of each of its configurations")
    parser.add_argument(guard_write.CONFIGURATIONS_FLAG, dest="is_every", action="store_true", required=True)
    parser.add_argument("--rule", dest="rules", action="append", default=[], metavar="ID")
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    parser.add_argument("--jobs", type=int, default=proc.DEFAULT_JOBS)
    parser.add_argument("--no-build", dest="has_no_build", action="store_true")
    options = parser.parse_args(argv)
    if options.jobs < 1:
        parser.error(f"--jobs takes a number above 0, not {options.jobs}")
    steps = Steps(require_graph, write_rule) if options.has_no_build else STEPS
    return run(options.root.resolve(), options.rules, options.jobs, steps)
