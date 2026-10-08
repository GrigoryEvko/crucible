"""guard witness — run each rule on its plants and its controls.

usage: guard witness [--rule ID]... [--engine E]... [--cxx PATH] [--plugin PATH] [--build-dir DIR] [--jobs N]

A plant is a tree in utils/plants/RULE/ that the rule must refuse, and a
control is a tree that it must accept (assay.guard.engines.meta.runner, THE
WITNESS).  The command runs the witness of each rule that the call names and
that has a module of its own.  The rule rule-witness holds that each rule has a
minimum of one plant and one control.

The witness of the engine plugin compiles each plant and each control with the
quarantine plugin (assay.guard.engines.plugin.witness).  It reads four options:
  --cxx         the compiler
  --plugin      the plugin that the configure built
  --build-dir   a build directory with a compile database
  --jobs        the most compiler processes at one time.
That witness does not use the runner of the engine, and the command selects
its rules itself.  It selects each rule of the engine plugin that the call
names.  With --engine plugin, it also selects each rule of the engine with a
module or with plants.  The witness of the engine elf builds each plant with
the patched compiler that ASSAY_TEST_CXX names
(assay.guard.engines.elf.runner, THE PLANTS).  A measured rule with a holder
has no run of its own.  The command selects it when the call names it, or
when the call names its engine and the rule has plants.  Its witness runs the
judge of its holder on each plant (assay.guard.engines.buildsys.held).

The status is 1 when a witness fails, 2 on a usage error, 77 when each witness
cannot run, and else 0.
"""

import sys
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard import check

PLUGIN_ENGINE = rule_registry.Engine.PLUGIN


def plugin_rules(registry: rule_registry.Registry, ids: list[str], engines: list[str]) -> list[rule_registry.Rule]:
    """Return the rules of the engine plugin that the call names, and each rule of the engine with --engine plugin.

    A rule of the engine with no module joins the selection of the engine
    only when it has plants.  A rule with a legacy module has no module of
    its own while it moves to the engine.
    """
    from assay.guard.engines.meta import runner

    chosen = [
        registry.rules[rule_id]
        for rule_id in ids
        if rule_id in registry.rules and registry.rules[rule_id].engine == PLUGIN_ENGINE
    ]
    if PLUGIN_ENGINE.value in engines:
        chosen += [
            rule
            for rule in registry.rules.values()
            if rule.engine == PLUGIN_ENGINE
            and rule.held_by is None
            and rule not in chosen
            and (rule.has_module or runner.plants_of(rule.id, REPO_ROOT))
        ]
    return chosen


def held_rules(registry: rule_registry.Registry, ids: list[str], engines: list[str]) -> list[rule_registry.Rule]:
    """Return each rule with a holder that the call names, and each such rule with plants of an engine of --engine.

    The witness of these rules is in assay.guard.engines.buildsys.held.
    """
    from assay.guard.engines.meta import runner

    chosen = [
        registry.rules[rule_id]
        for rule_id in ids
        if rule_id in registry.rules and registry.rules[rule_id].held_by is not None
    ]
    chosen += [
        rule
        for rule in registry.rules.values()
        if rule.held_by is not None
        and rule.engine.value in engines
        and rule not in chosen
        and runner.plants_of(rule.id, REPO_ROOT)
    ]
    return chosen


def combined(statuses: list[int]) -> int:
    """Return the status of the call: the worst failure, else 77 when each witness cannot run, else 0."""
    failures = [status for status in statuses if status not in (ExitCode.OK, ExitCode.SKIPPED)]
    if failures:
        return max(failures)
    return ExitCode.SKIPPED if statuses and all(status == ExitCode.SKIPPED for status in statuses) else ExitCode.OK


def main(argv: list[str]) -> int:
    """Run the witness of each rule that the command line names.

    Returns:
        0 when each witness holds, 1 when one fails, 2 on a usage error, 77 when each witness cannot run
    """
    from assay.core import cli
    from assay.guard.engines.meta import runner

    parser = cli.parser("guard witness", "run each rule on its plants and its controls")
    parser.add_argument("--rule", dest="rules", action="append", default=[], metavar="ID")
    parser.add_argument("--engine", dest="engines", action="append", default=[], metavar="E")
    parser.add_argument("--cxx", default=None, metavar="PATH", help="the compiler of the engine plugin")
    parser.add_argument("--plugin", type=Path, default=None, metavar="PATH", help="the quarantine plugin")
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        metavar="DIR",
        help="a build directory with a compile database, for the engine plugin",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=16,
        metavar="N",
        help="the most compiler processes at one time, for the engine plugin",
    )
    options = parser.parse_args(argv)
    if options.jobs < 1:
        parser.error(f"--jobs is {options.jobs}, and it must be 1 or more")
    registry = check.load_registry()
    if registry is None:
        return ExitCode.USAGE
    compiled = plugin_rules(registry, options.rules, options.engines)
    held = held_rules(registry, options.rules, options.engines)
    taken = {rule.id for rule in (*compiled, *held)}
    other_ids = [rule_id for rule_id in options.rules if rule_id not in taken]
    other_engines = [engine for engine in options.engines if engine != PLUGIN_ENGINE.value]
    chosen: list[rule_registry.Rule] | str = []
    if other_ids or other_engines or not (compiled or held):
        chosen = check.selected(registry, other_ids, other_engines)
    strangers = [rule.id for rule in held if rule.engine != rule_registry.Engine.BUILD]
    if strangers:
        chosen = f"the rule {', '.join(strangers)} has a holder and no engine build, and its witness has no judge"
    if isinstance(chosen, str):
        print(f"guard witness: {chosen}", file=sys.stderr)
        return ExitCode.USAGE
    statuses = [runner.witness(rule, REPO_ROOT) for rule in chosen if rule.engine == rule_registry.Engine.META]
    tree_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.TSAST]
    if tree_rules:
        from assay.guard.engines.tsast import runner as tree_runner

        statuses += [tree_runner.witness(rule, REPO_ROOT) for rule in tree_rules]
    elf_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.ELF]
    if elf_rules:
        from assay.guard.engines.elf import runner as elf_runner

        statuses += [elf_runner.witness(rule, REPO_ROOT) for rule in elf_rules]
    build_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.BUILD]
    if build_rules:
        from assay.guard.engines.buildsys import runner as build_runner

        statuses += [build_runner.witness(rule, REPO_ROOT) for rule in build_rules]
    if held:
        from assay.guard.engines.buildsys import held as held_witness

        statuses += [held_witness.witness(rule, REPO_ROOT) for rule in held]
    if compiled:
        from assay.guard.engines.plugin import witness as plugin_witness

        build_dir = options.build_dir.resolve() if options.build_dir is not None else None
        statuses += [
            plugin_witness.witness(rule.id, REPO_ROOT, options.cxx, options.plugin, build_dir, options.jobs)
            for rule in compiled
        ]
    return combined(statuses)
