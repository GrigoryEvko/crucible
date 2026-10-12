"""guard write — write the ledger of one rule through assay.core.ledger, with the checks of its policy.

usage: guard write --rule ID [--raise | --admit] [--reason TEXT] [--root DIR] [--build-dir DIR] [--path PATH]...
       guard write --rule ID --build-dir DIR [--raise | --admit] [--reason TEXT] [--add-configuration]
                                           a rule of the engine plugin or elf, or a rule
                                           of the engine build with configurations
       guard write --rule ID --import LOG  a rule of the engine plugin or elf with configurations, or a
                                           rule of the engine build with a module that writes its ledger
       guard write --rule ID [ARG]...      a rule with a legacy module
       guard write --all-configurations [--rule ID]... [--root DIR] [--jobs N] [--no-build]
                                           each counted rule, or each rule of --rule, in the build of each
                                           configuration of its ledger (assay.guard.write_configurations)

A rule with a module of its own: the command runs the rule and writes its
ledger in the mode of the call (assay.core.ledger, THE WRITE).  The default
mode lowers each count and removes each row that admits nothing, and it
refuses a new finding.  --raise also writes a count above its row: use it only
when the rule changed what it reports, and give the reason in the commit.
--admit also writes a row for each new finding, with the reason of --reason.
A rule of the policy max or baseline judges its ledger itself, so its module
writes the ledger: the function written(context, ledger, reason) of the module
gives the new ledger, and --reason gives the reason of a changed row.  A held
rule of such a policy has no module of its own.  The command then calls
written() of assay.guard.engines.buildsys.held (THE WRITE of that module).

The module of a rule can name kinds in REFUSED_KINDS, and no row admits a
finding of such a kind (assay.guard.engines.meta.runner, THE REFUSED KINDS).
The command refuses each such finding in each mode.

--path names the paths that a rule of the engine build reads, such as the
headers of gate-mutants (assay.guard.engines.buildsys.runner, THE PATHS).
The command then writes only the rows of those paths, and it keeps each
other row.  It applies to a rule of the engine build, meta or tsast with the
policy count or admit.  With --reason and no mode, each row of those paths
that the write keeps gets the reason.  A row that must stay then tells why.

A rule of the policy none has no ledger.  Its module can write a data file
from a build.  Then the function refresh(context) of the module writes the
file again, and the command prints the line that the function returns.  The
list cmake/compile-first.txt of the rule compile-first is such a file.

A rule of the engine plugin writes through assay.guard.engines.plugin.runner,
which reads the build of --build-dir.  A module that writes its ledger itself
gives the function write(rule, request), and its head comment tells its modes.
The module of the rule quarantine does that.  --add-configuration writes the
configuration of the build with its name (THE POLICY of
assay.core.configuration).  --import LOG puts the rows of a configuration
that a CI leg wrote into the ledger.  The command exits 77 when the rule does
not apply to the build.

A rule of the engine elf writes through the function write(rule, request) of
its module (assay.guard.engines.elf.runner), which reads the build of
--build-dir.  The module of the rule artifact-symbols writes the
configuration of the build with its rows.  --add-configuration and --import
LOG apply to it as to a rule of the engine plugin.

A rule of a family module of the engine build with `configured`, such as
the census totals, holds the rows of each configuration.
--add-configuration applies to it.  A module of the engine build that judges
its ledger itself gives write(rule, request), with the WriteRequest of
assay.guard.engines.plugin.runner (assay.guard.engines.buildsys.runner, THE
MODULES THAT JUDGE THEIR LEDGER).  --add-configuration and --import LOG apply
to it as to a rule of the engine plugin.  The rule fixture-quarantine writes
its ledger in this way.

A rule with a legacy module: the command calls the module with the words of
its mode, then --write, then each ARG, as `guard check --rule ID --write`
does.
"""

import sys
from dataclasses import replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard import check

# The policies with rows that --path can divide by their path, and the engines that write the ledger in the runner.
PARTIAL_POLICIES = (rule_registry.Policy.COUNT, rule_registry.Policy.ADMIT)
OWN_WRITE_ENGINES = (rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF)
# The policies of a rule that judges its ledger itself.  The module of such a rule writes the ledger.
JUDGED_POLICIES = (rule_registry.Policy.MAX, rule_registry.Policy.BASELINE)
# The word that writes each configuration of the counted ledgers (assay.guard.write_configurations).
CONFIGURATIONS_FLAG = "--all-configurations"


def main(argv: list[str]) -> int:
    """Write the ledger of the rule that the command line names.

    Returns:
        0 when the ledger is written, 1 when the mode refuses a finding, 2 on a usage error
    """
    if CONFIGURATIONS_FLAG in argv:
        from assay.guard import write_configurations

        return write_configurations.main(argv)
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
    parser.add_argument("--path", dest="paths", action="append", default=[], metavar="PATH")
    options = parser.parse_args(argv)
    requested = registry.rules.get(options.rule)
    is_held_write = requested is not None and requested.held_by is not None and requested.policy in JUDGED_POLICIES
    chosen = [requested] if requested is not None and is_held_write else check.selected(registry, [options.rule], [])
    if isinstance(chosen, str):
        print(f"guard write: {chosen}", file=sys.stderr)
        return 2
    rule = chosen[0]
    if options.paths and (rule.engine in OWN_WRITE_ENGINES or rule.policy not in PARTIAL_POLICIES):
        check_report.write_line("guard write: --path applies only to a rule of the engine build, meta or tsast with "
                                "the policy count or admit", is_error=True)
        return 2
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
    if rule.engine == rule_registry.Engine.ELF:
        from assay.guard.engines.elf import runner as elf_runner

        if build_dir is None and options.import_log is None:
            check_report.write_line(f"guard write: the rule {rule.id} reads the objects of a build: give --build-dir "
                                    f"DIR", is_error=True)
            return 2
        return elf_runner.write(rule, elf_runner.WriteRequest(
            root, build_dir, options.mode or rule_ledger.WriteMode.LOWER, options.reason,
            options.import_log.resolve() if options.import_log is not None else None, options.add_configuration))
    own_write = None
    if rule.engine == rule_registry.Engine.BUILD and not is_held_write:
        own_write = getattr(runner.rule_module(rule), "write", None)
    if own_write is not None:
        from assay.guard.engines.plugin import runner as plugin_runner

        if build_dir is None and options.import_log is None:
            check_report.write_line(f"guard write: the rule {rule.id} reads a build: give --build-dir DIR",
                                    is_error=True)
            return 2
        return int(own_write(rule, plugin_runner.WriteRequest(
            root, build_dir, options.mode or rule_ledger.WriteMode.LOWER, options.reason, options.add_configuration,
            options.import_log.resolve() if options.import_log is not None else None)))
    is_configured = options.add_configuration and rule.engine == rule_registry.Engine.BUILD
    if is_configured and is_held_write:
        from assay.guard.engines.buildsys import held

        is_configured = held.is_configured(rule)
    else:
        is_configured = is_configured and runner.is_configured(rule)
    if (options.add_configuration and not is_configured) or options.import_log is not None:
        check_report.write_line("guard write: --add-configuration applies only to a rule with the rows of each "
                                "configuration of a build, and --import to a rule of the engine plugin or elf",
                                is_error=True)
        return 2
    context = check.rule_context(rule, root, build_dir, options.paths)
    if is_configured:
        from assay.guard.engines.buildsys import runner as build_runner

        if isinstance(context, build_runner.BuildContext):
            context.add_configuration = options.add_configuration
    try:
        if rule.policy == rule_registry.Policy.NONE:
            refresh = getattr(runner.rule_module(rule), "refresh", None)
            if refresh is None:
                check_report.write_line(f"guard write: the rule {rule.id} has no ledger, and its module writes no "
                                        f"data file", is_error=True)
                return 2
            check_report.write_line(f"guard write: {refresh(context)}")
            return 0
        ledger = rule_ledger.read(rule, root)
        if rule.policy in JUDGED_POLICIES and rule.held_by is not None:
            from assay.guard.engines.buildsys import held

            written = held.written(rule, context, ledger, options.reason)
        elif rule.policy in JUDGED_POLICIES:
            written = runner.rule_module(rule).written(context, ledger, options.reason)
        else:
            found = context.findings_of(rule)
            refused = runner.refused_kinds(rule)
            unread = [item for item in found if item.level == "error" and item.kind in refused]
            if unread:
                check_report.emit(unread, rule.id, None)
                check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {len(unread)} findings "
                                        f"of a kind that no row admits.  Correct them first", is_error=True)
                return 1
            named = set(options.paths)
            judged = replace(ledger, base={key: entry for key, entry in ledger.base.items()
                                           if not named or key.path in named})
            found = [item for item in found if not named or item.path in named]
            written = rule_ledger.write(rule, judged, found, mode=options.mode or rule_ledger.WriteMode.LOWER,
                                        reason=options.reason, moved=git.moves(root))
            if named:
                kept = {key: entry for key, entry in ledger.base.items() if key.path not in named}
                own = written.base
                if options.reason and options.mode is None:
                    own = {key: replace(entry, reason=options.reason) for key, entry in own.items()}
                written = replace(written, base={**kept, **own})
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
