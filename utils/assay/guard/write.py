"""guard write — write the ledger of one rule through assay.core.ledger, with the checks of its policy.

usage: guard write --rule ID [--raise | --admit] [--reason TEXT] [--root DIR] [--build-dir DIR]
       guard write --rule ID --build-dir DIR [--raise | --admit] [--reason TEXT] [--add-configuration]
                                           a rule of the engine plugin
       guard write --rule ID --import LOG  a rule of the engine plugin or elf with configurations
       guard write --rule ID [ARG]...      a rule with a legacy module

A rule with a module of its own: the command runs the rule and writes its
ledger in the mode of the call (assay.core.ledger, THE WRITE).  The default
mode lowers each count and removes each row that admits nothing, and it
refuses a new finding.  --raise also writes a count above its row: use it only
when the rule changed what it reports, and give the reason in the commit.
--admit also writes a row for each new finding, with the reason of --reason.
A rule of the policy max or baseline judges its ledger itself, so its module
writes the ledger: the function written(context, ledger, reason) of the module
gives the new ledger, and --reason gives the reason of a changed row.

A rule of the engine plugin writes through assay.guard.engines.plugin.runner,
which reads the build of --build-dir.  A module that writes its ledger itself
gives the function write(rule, request), and its head comment tells its modes.
The module of the rule quarantine does that.  --add-configuration writes the
configuration of the build with its name.  --import LOG puts the rows of a
configuration that a CI leg wrote into the ledger.  The two options apply only
to such a rule.  The command exits 77 when the rule does not apply to the
build.

A rule of the engine elf writes through the function write(rule, request) of
its module (assay.guard.engines.elf.runner), which reads the build of
--build-dir.  The module of the rule artifact-symbols writes the
configuration of the build with its rows.  --import LOG puts the rows of a
configuration that a CI leg printed into the ledger.

A rule with a legacy module: the command calls the module with the words of
its mode, then --write, then each ARG, as `guard check --rule ID --write`
does.
"""

import sys
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard import check


def main(argv: list[str]) -> int:
    """Write the ledger of the rule that the command line names.

    Returns:
        0 when the ledger is written, 1 when the mode refuses a finding, 2 on a usage error
    """
    registry = check.load_registry()
    if registry is None:
        return 2
    if len(argv) >= 2 and argv[0] == "--rule":
        rule = registry.rules.get(argv[1])
        if rule is not None and rule.legacy is not None:
            return check.run_legacy(rule, ["--write", *argv[2:]])
    from assay.core import cli
    from assay.core import git
    from assay.guard.engines.meta import runner

    parser = cli.parser("guard write", "write the ledger of one rule")
    parser.add_argument("--rule", required=True, metavar="ID")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--raise", dest="mode", action="store_const", const=rule_ledger.WriteMode.RAISE)
    modes.add_argument("--admit", dest="mode", action="store_const", const=rule_ledger.WriteMode.ADMIT)
    parser.add_argument("--reason", default=None)
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--add-configuration", dest="add_configuration", action="store_true")
    parser.add_argument("--import", dest="import_log", type=Path, default=None, metavar="LOG")
    options = parser.parse_args(argv)
    chosen = check.selected(registry, [options.rule], [])
    if isinstance(chosen, str):
        print(f"guard write: {chosen}", file=sys.stderr)
        return 2
    rule = chosen[0]
    root = options.root.resolve()
    build_dir = options.build_dir.resolve() if options.build_dir is not None else None
    if rule.engine == rule_registry.Engine.PLUGIN:
        from assay.guard.engines.plugin import runner as plugin_runner

        if build_dir is None and options.import_log is None:
            check_report.write_line(f"guard write: the rule {rule.id} reads the objects of a build: give --build-dir "
                                    f"DIR", is_error=True)
            return 2
        return plugin_runner.write(rule, plugin_runner.WriteRequest(
            root, build_dir, options.mode or rule_ledger.WriteMode.LOWER, options.reason, options.add_configuration,
            options.import_log.resolve() if options.import_log is not None else None))
    if rule.engine == rule_registry.Engine.ELF and not options.add_configuration:
        from assay.guard.engines.elf import runner as elf_runner

        if build_dir is None and options.import_log is None:
            check_report.write_line(f"guard write: the rule {rule.id} reads the objects of a build: give --build-dir "
                                    f"DIR", is_error=True)
            return 2
        return elf_runner.write(rule, elf_runner.WriteRequest(
            root, build_dir, options.mode or rule_ledger.WriteMode.LOWER, options.reason,
            options.import_log.resolve() if options.import_log is not None else None))
    if options.add_configuration or options.import_log is not None:
        check_report.write_line("guard write: --add-configuration applies only to a rule of the engine plugin, and "
                                "--import to a rule of the engine plugin or elf", is_error=True)
        return 2
    context: runner.RuleContext = runner.MetaContext(root, build_dir)
    if rule.engine == rule_registry.Engine.TSAST:
        from assay.guard.engines.tsast import runner as tree_runner

        tree_context = tree_runner.TsastContext(root, build_dir)
        tree_context.rule = rule
        context = tree_context
    try:
        ledger = rule_ledger.read(rule, root)
        if rule.policy in (rule_registry.Policy.MAX, rule_registry.Policy.BASELINE):
            written = runner.rule_module(rule).written(context, ledger, options.reason)
        else:
            written = rule_ledger.write(rule, ledger, context.findings_of(rule),
                                        mode=options.mode or rule_ledger.WriteMode.LOWER, reason=options.reason,
                                        moved=git.moves(root))
    except runner.NotApplicable as reason:
        print(f"guard write: the rule {rule.id} does not apply: {reason}", file=sys.stderr)
        return 2
    except (rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        print(f"guard write: the ledger of {rule.id} is not written: {problem}", file=sys.stderr)
        return 1
    changed = rule_ledger.store(written, root)
    rows = len(written.base) + sum(len(table) for table in written.deltas.values())
    print(f"guard write: {rule_ledger.shown_path(rule.id)} holds {rows} rows"
          + ("" if changed else ", with no change"))
    return 0
