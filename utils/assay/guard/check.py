"""guard check — give the findings of the rules of the registry, and judge them against their ledgers.

usage:
    guard check --rule ID [ARG]...                        a rule with a legacy module
    guard check [--rule ID]... [--engine E]... [--part K/N] [--build-dir DIR] [--warnings-dir DIR] [--root DIR]
                [--path PATH]...

utils/registry.txt (assay.core.registry) gives each rule.  A rule with a
legacy module runs alone: the command calls the function main of the module
with the words of its mode (legacy-args) and then each ARG, unchanged, and it
exits with the value that main returns.  A rule with a module of its own runs
through the runner of its engine, which judges its findings against its ledger
(assay.core.ledger).  The engines meta, tsast, plugin, elf and build have a
runner.
The tsast rules of one call share the trees that they read
(assay.guard.engines.tsast.runner, THE SHARED TREES), and --part K/N gives
them part K of N of their files (THE PARTS).  The plugin rules of one call
read the sections of the objects of the build one time
(assay.guard.engines.plugin.runner, THE SECTIONS).  The elf rules of one call
read each fact of the build one time (assay.guard.engines.elf.runner, THE
BUILD).  A plugin rule or an elf rule with no --build-dir is a usage error.
The build rules of one call read each input of the build one time
(assay.guard.engines.buildsys.runner, THE CONTEXT).  --path names the paths
that a build rule reads, and the run then judges only their ledger rows (THE
PATHS of that module).  A rule with a holder has no run of its own: the
holder applies it.

Each run also holds the cache of the compiled modules of the package under its
limit, when its turn is due (assay.core.cache, THE COMPILED MODULES).  A
failure of that work changes no verdict of a rule.
"""

import importlib
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import TYPE_CHECKING

from assay.core import cache
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

if TYPE_CHECKING:
    from assay.guard.engines.meta.runner import RuleContext

USAGE = ("usage: guard check --rule ID [ARG]...  |  guard check [--rule ID]... [--engine E]... [--part K/N] "
         "[--build-dir DIR] [--warnings-dir DIR] [--root DIR] [--path PATH]...")
# The engines with a runner at this time.
RUNNERS = (rule_registry.Engine.META, rule_registry.Engine.TSAST, rule_registry.Engine.PLUGIN,
           rule_registry.Engine.ELF, rule_registry.Engine.BUILD)
# The engines of the rules that read the objects of a build.
BUILD_ENGINES = (rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF)


def load_registry() -> rule_registry.Registry | None:
    """Read the registry of this tree, or print why it cannot and give None."""
    try:
        return rule_registry.load()
    except (rows_grammar.RowsError, OSError) as problem:
        print(f"guard: the registry cannot be read: {problem}.  `guard check --rule registry-shape` gives each "
              f"problem", file=sys.stderr)
        return None


def run_legacy(rule: rule_registry.Rule, words: list[str]) -> int:
    """Call the function main of the legacy module of a rule with the words of its mode and the words of the call."""
    assert rule.legacy is not None
    module = importlib.import_module(rule.legacy.qualified)
    return int(module.main([*rule.legacy.words, *words]))


def selected(registry: rule_registry.Registry, ids: list[str], engines: list[str]) -> list[rule_registry.Rule] | str:
    """Return the rules of the IDs and the engines, or the reason that the selection cannot run."""
    unknown = [rule_id for rule_id in ids if rule_id not in registry.rules]
    if unknown:
        return f"the registry has no rule {', '.join(unknown)}"
    bad_engines = [engine for engine in engines if engine not in {item.value for item in rule_registry.Engine}]
    if bad_engines:
        return f"the engine {', '.join(bad_engines)} is not one of {', '.join(item.value for item in rule_registry.Engine)}"
    chosen = [registry.rules[rule_id] for rule_id in ids]
    chosen += [rule for rule in registry.rules.values() if rule.engine.value in engines and rule.has_module
               and rule not in chosen]
    if not chosen:
        return "give a rule or an engine"
    for rule in chosen:
        if rule.legacy is not None:
            return f"the rule {rule.id} has the legacy module assay.{rule.legacy.module}, and it runs alone: guard check --rule {rule.id} [ARG]..."
        if rule.held_by is not None:
            return f"the rule {rule.id} has no run of its own: {rule.held_by} applies it"
        if rule.engine not in RUNNERS:
            return f"the engine {rule.engine.value} of the rule {rule.id} has no runner at this time"
    return chosen


def rule_context(rule: rule_registry.Rule, root: Path, build_dir: Path | None,
                 paths: Sequence[str] = ()) -> RuleContext:
    """Return the context of the engine of a rule on a tree, for a command that runs the module of one rule.

    A rule of the engine tsast or build gets the context of its engine, with
    the rule that its scope reads.  A rule of the engine build also gets the
    paths that the command names (assay.guard.engines.buildsys.runner, THE
    PATHS).  Each other rule gets the context of the engine meta.
    """
    from assay.guard.engines.meta import runner

    if rule.engine == rule_registry.Engine.TSAST:
        from assay.guard.engines.tsast import runner as tree_runner

        tree_context = tree_runner.TsastContext(root, build_dir)
        tree_context.rule = rule
        return tree_context
    if rule.engine == rule_registry.Engine.BUILD:
        from assay.guard.engines.buildsys import runner as build_runner

        build_context = build_runner.BuildContext(root, build_dir, paths=paths)
        build_context.rule = rule
        return build_context
    return runner.MetaContext(root, build_dir)


def parsed_part(text: str) -> tuple[int, int] | None:
    """Return (K, N) of a part K/N with 1 <= K <= N, or None when the text is not such a part."""
    first, slash, second = text.partition("/")
    if not slash or not first.isascii() or not first.isdigit() or not second.isascii() or not second.isdigit():
        return None
    index, count = int(first), int(second)
    return (index, count) if 1 <= index <= count else None


def main(argv: list[str]) -> int:
    """Run the rules that the command line names.

    Args:
        argv: The words after the subcommand

    Returns:
        The status of the legacy module, or 0 with no error, 1 with an error, 2 on a usage error, 77 when each rule
        does not apply
    """
    registry = load_registry()
    if registry is None:
        return 2
    try:
        cache.hold_compiled_bound()
    except OSError:
        pass
    if len(argv) >= 2 and argv[0] == "--rule":
        rule = registry.rules.get(argv[1])
        if rule is not None and rule.legacy is not None:
            return run_legacy(rule, argv[2:])
    from assay.core import cli
    from assay.guard.engines.meta import runner

    parser = cli.parser("guard check", "give the findings of the rules, and judge them against their ledgers")
    parser.add_argument("--rule", dest="rules", action="append", default=[], metavar="ID")
    parser.add_argument("--engine", dest="engines", action="append", default=[], metavar="E")
    parser.add_argument("--part", default=None, metavar="K/N")
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--warnings-dir", type=Path, default=None)
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    parser.add_argument("--path", dest="paths", action="append", default=[], metavar="PATH")
    options = parser.parse_args(argv)
    chosen = selected(registry, options.rules, options.engines)
    part = parsed_part(options.part) if options.part is not None else None
    if not isinstance(chosen, str) and options.part is not None and part is None:
        chosen = f"the part {options.part} is not K/N with 1 <= K <= N"
    if not isinstance(chosen, str) and part is not None \
            and any(rule.engine != rule_registry.Engine.TSAST for rule in chosen):
        chosen = "only a rule of the engine tsast reads a part of its files"
    if not isinstance(chosen, str) and options.paths \
            and any(rule.engine != rule_registry.Engine.BUILD for rule in chosen):
        chosen = "only a rule of the engine build reads the paths of --path"
    if not isinstance(chosen, str) and options.build_dir is None \
            and any(rule.engine in BUILD_ENGINES for rule in chosen):
        chosen = "a rule of the engine plugin or elf reads the objects of a build: give --build-dir DIR"
    if isinstance(chosen, str):
        print(f"guard check: {chosen}\n{USAGE}", file=sys.stderr)
        return 2
    root = options.root.resolve()
    build_dir = options.build_dir.resolve() if options.build_dir is not None else None
    context = runner.MetaContext(root, build_dir)
    statuses = [runner.run(rule, context, options.warnings_dir) for rule in chosen
                if rule.engine == rule_registry.Engine.META]
    tree_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.TSAST]
    if tree_rules:
        from assay.guard.engines.tsast import runner as tree_runner

        statuses += tree_runner.run(tree_rules, root, build_dir, options.warnings_dir, part)
    plugin_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.PLUGIN]
    if plugin_rules:
        from assay.guard.engines.plugin import runner as plugin_runner

        statuses += plugin_runner.run(plugin_rules, root, build_dir, options.warnings_dir)
    elf_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.ELF]
    if elf_rules:
        from assay.guard.engines.elf import runner as elf_runner

        statuses += elf_runner.run(elf_rules, root, build_dir, options.warnings_dir)
    build_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.BUILD]
    if build_rules:
        from assay.guard.engines.buildsys import runner as build_runner

        statuses += build_runner.run(build_rules, root, build_dir, options.warnings_dir, options.paths)
    if any(status == 1 for status in statuses):
        return 1
    return runner.SKIPPED if all(status == runner.SKIPPED for status in statuses) else 0
