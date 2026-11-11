"""guard write — write the ledger of one rule through assay.core.ledger, with the checks of its policy.

usage: guard write --rule ID [--raise | --admit] [--reason TEXT] [--root DIR] [--build-dir DIR]... [--path PATH]...
       guard write --rule ID [ARG]...      a rule with a legacy module

A rule with a module of its own: the command runs the rule and writes its
ledger in the mode of the call (assay.core.ledger, THE WRITE).  The default
mode lowers each count, removes each row that admits nothing, and refuses a
new finding.  A write from one build of THE BUILDS lowers no row.  --raise
also writes a count above its row: use it only when the rule changed what it
reports, and give the reason in the commit.
--admit also writes a row for each new finding, with the reason of --reason.
A rule of the policy max or baseline judges its ledger itself, so its module
writes the ledger: the function written(context, ledger, reason) of the module
gives the new ledger, and --reason gives the reason of a changed row.  A held
rule of such a policy has no module of its own.  The command then calls
written() of assay.guard.engines.buildsys.held (THE WRITE of that module).

The module of a rule can name kinds in REFUSED_KINDS, and no row admits a
finding of such a kind (assay.guard.engines.meta.runner, THE REFUSED KINDS).
The command refuses each such finding in each mode.

--path names paths of the rows of the ledger.  The command then writes only
the rows of those paths, and it keeps each other row.  It applies to a rule
of the engine build, meta or tsast with the policy count or admit.  With --reason and no mode, each row of those paths
that the write keeps gets the reason.  A row that must stay then tells why.

A rule of the policy none has no ledger.  Its module can write a data file
from a build.  Then the function refresh(context) of the module writes the
file again, and the command prints the line that the function returns.  The
list test/layer/header-constexpr-ops.txt of the rule header-constexpr-ops is
such a file.

THE BUILDS
    A rule of the engine plugin writes through
    assay.guard.engines.plugin.runner, and a rule of the engine elf through
    the function write(rule, request) of its module
    (assay.guard.engines.elf.runner).  A module of the engine build that
    judges its ledger itself gives write(rule, request), with the
    WriteRequest of assay.guard.engines.plugin.runner
    (assay.guard.engines.buildsys.runner, THE MODULES THAT JUDGE THEIR
    LEDGER).  The rule fixture-quarantine writes its ledger in this way.
    Each such write reads the builds of --build-dir.

    One --build-dir gives the counts of one build, and the write lowers no
    row (THE WRITE of assay.core.ledger).  More than one --build-dir is a
    reduce (THE REDUCE of assay.core.ledger).  The builds must be one build
    of each configuration of the landing gate that checks the rule
    (assay.loop.gate, THE COUNTED RULES).  The write then lowers each row to
    the largest count over the builds.  The landing gate makes this write.
    The command exits 77 when the rule does not apply to a build.

A rule with a legacy module: the command calls the module with the words of
its mode, then --write, then each ARG, as `guard check --rule ID --write`
does.
"""

import sys
from collections.abc import Sequence
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


def reduce_refusal(rule_id: str, root: Path, builds: Sequence[Path]) -> str:
    """Return the reason that the builds of a reduce are not one build of each configuration of the gate, or "".

    The configurations are those of the landing gate that check the rule (THE BUILDS).  Complexity: one read of the
    build kind and the CMake cache of each build, and one read of the presets of the tree.
    """
    from assay.core import configuration as build_config
    from assay.loop import gate

    names: list[str] = []
    for build in builds:
        try:
            names.append(build_config.read_configuration(build).name)
        except build_config.ConfigurationError as problem:
            return str(problem)
    try:
        required = sorted(
            item.configuration_name for item in gate.configurations(root) if gate.is_checked(item, rule_id)
        )
    except gate.GateError as problem:
        return str(problem)
    if sorted(names) != required:
        return (
            f"a write with more than one --build-dir lowers the rows, and it takes one build of each configuration of "
            f"the landing gate that checks the rule: {', '.join(required)}.  The builds have the configurations "
            f"{', '.join(sorted(names))}"
        )
    return ""


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
    parser.add_argument("--build-dir", dest="build_dirs", type=Path, action="append", default=[], metavar="DIR")
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
        check_report.write_line(
            "guard write: --path applies only to a rule of the engine build, meta or tsast with "
            "the policy count or admit",
            is_error=True,
        )
        return 2
    root = options.root.resolve()
    builds = [path.resolve() for path in options.build_dirs]
    build_dir = builds[0] if builds else None
    own_write = None
    if rule.engine == rule_registry.Engine.BUILD and not is_held_write:
        own_write = getattr(runner.rule_module(rule), "write", None)
    is_build_write = rule.engine in OWN_WRITE_ENGINES or own_write is not None
    if is_build_write and not builds:
        check_report.write_line(
            f"guard write: the rule {rule.id} reads the objects of a build: give --build-dir DIR", is_error=True
        )
        return 2
    if len(builds) > 1:
        refusal = (
            reduce_refusal(rule.id, root, builds)
            if is_build_write
            else (
                "more than one --build-dir applies only to a rule of the engine plugin or elf, or to a rule of the "
                "engine build whose module writes its ledger"
            )
        )
        if refusal:
            check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {refusal}", is_error=True)
            return 2
    mode = options.mode or rule_ledger.WriteMode.LOWER
    if rule.engine == rule_registry.Engine.ELF:
        from assay.guard.engines.elf import runner as elf_runner

        return elf_runner.write(rule, elf_runner.WriteRequest(root, build_dir, mode, options.reason, tuple(builds[1:])))
    if is_build_write:
        from assay.guard.engines.plugin import runner as plugin_runner

        request = plugin_runner.WriteRequest(root, build_dir, mode, options.reason, tuple(builds[1:]))
        if own_write is not None:
            return int(own_write(rule, request))
        return plugin_runner.write(rule, request)
    context = check.rule_context(rule, root, build_dir, options.paths)
    try:
        if rule.policy == rule_registry.Policy.NONE:
            refresh = getattr(runner.rule_module(rule), "refresh", None)
            if refresh is None:
                check_report.write_line(
                    f"guard write: the rule {rule.id} has no ledger, and its module writes no data file", is_error=True
                )
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
                check_report.write_line(
                    f"guard write: the ledger of {rule.id} is not written: {len(unread)} findings "
                    f"of a kind that no row admits.  Correct them first",
                    is_error=True,
                )
                return 1
            named = set(options.paths)
            judged = replace(
                ledger, base={key: entry for key, entry in ledger.base.items() if not named or key.path in named}
            )
            found = [item for item in found if not named or item.path in named]
            moves = git.change_moves(root)
            before = rule_ledger.ledger_before(
                rule, moves.base, git.base_file(root, moves, rule_ledger.shown_path(rule.id))
            )
            written = rule_ledger.write(
                rule,
                judged,
                found,
                mode=mode,
                reason=options.reason,
                moved=moves.renamed,
                flows=moves.flows,
                before=before,
            )
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
    print(
        f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows"
        + ("" if changed else ", with no change")
    )
    return 0
