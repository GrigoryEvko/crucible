"""guard write — write the ledger of one rule through assay.core.ledger, with the checks of its policy.

usage: guard write --rule ID [--raise | --admit] [--reason TEXT] [--root DIR] [--build-dir DIR]... [--path PATH]...
       guard write --rule ID --row ITEM... [--kind KIND] --reason TEXT [--root DIR]
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

A module of a rule with more than one part can give the function
unread_parts(context): the parts of the rule that do not apply to the
context, such as a part that reads a build in a call with no --build-dir.
The command then refuses the write, because the write would remove the rows
of those parts (assay.guard.engines.meta.registry, THE BUILD).

--path names paths of the rows of the ledger.  The command then writes only
the rows of those paths, and it keeps each other row (THE PATHS of
assay.core.ledger).  It applies to a rule of the engine build, meta, tsast or
plugin with the policy count or admit.  A plugin rule whose module writes its
ledger itself refuses it.  With --reason and no mode, each row of those paths
that the write keeps gets the reason, and its count does not change.  A row
that must stay then tells why, and a stale reason gets a correct one.

--row names rows of the ledger by their path or their entity, and --kind
narrows them to one kind.  The command then writes the reason of --reason
into each named row, and it changes no value and no other row (THE REASON
OF A ROW of assay.core.ledger).  It reads no build, so it takes no
--build-dir, no mode and no --path, and it applies to each rule with a
ledger, also of the policy max.  An item that names no row is refused.  This
write corrects the reason of a row whose value holds, so no change of a
ledger needs a hand edit.

A rule of the policy none has no ledger.  Its module can write a data file
from a build.  Then the function refresh(context) of the module writes the
file again, and the command prints the line that the function returns.  The
list test/layer/header-constexpr-ops.txt of the rule header-constexpr-ops is
such a file.

THE BUILDS
    A rule of the engine plugin writes through
    assay.guard.engines.plugin.runner, and a rule of the engine elf through
    the function write(rule, request) of its module
    (assay.guard.engines.elf.runner).  Each such write reads the builds of
    --build-dir.

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


def write_reasons(rule: rule_registry.Rule, root: Path, request: rule_ledger.RowReason) -> int:
    """Write the reason of each row that the request names into the ledger of a rule (THE REASON OF A ROW).

    Returns:
        0 when the ledger is written, 1 when the request names no row or gives no reason, 2 for a rule with no ledger
    """
    if rule.policy == rule_registry.Policy.NONE:
        check_report.write_line(f"guard write: the rule {rule.id} has no ledger", is_error=True)
        return 2
    try:
        written = rule_ledger.reasoned(rule_ledger.read(rule, root), request)
    except (rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        print(f"guard write: the ledger of {rule.id} is not written: {problem}", file=sys.stderr)
        return 1
    changed = rule_ledger.store(written, root)
    print(
        f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows"
        + ("" if changed else ", with no change")
    )
    return 0


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
    parser.add_argument("--row", dest="rows", action="append", default=[], metavar="ITEM")
    parser.add_argument("--kind", default="", metavar="KIND")
    options = parser.parse_args(argv)
    requested = registry.rules.get(options.rule)
    is_held_write = requested is not None and requested.held_by is not None and requested.policy in JUDGED_POLICIES
    chosen = [requested] if requested is not None and is_held_write else check.selected(registry, [options.rule], [])
    if isinstance(chosen, str):
        print(f"guard write: {chosen}", file=sys.stderr)
        return 2
    rule = chosen[0]
    if options.rows or options.kind:
        if not options.rows or options.mode is not None or options.paths or options.build_dirs:
            check_report.write_line(
                "guard write: --row writes only the reason of the rows that it names: give one --row ITEM or more, "
                "--reason TEXT and at most --kind KIND, with no mode, no --path and no --build-dir",
                is_error=True,
            )
            return 2
        named_rows = rule_ledger.RowReason(tuple(options.rows), options.kind, options.reason or "")
        return write_reasons(rule, options.root.resolve(), named_rows)
    if options.paths and (rule.engine == rule_registry.Engine.ELF or rule.policy not in PARTIAL_POLICIES):
        check_report.write_line(
            "guard write: --path applies only to a rule of the engine build, meta, tsast or plugin with "
            "the policy count or admit",
            is_error=True,
        )
        return 2
    root = options.root.resolve()
    builds = [path.resolve() for path in options.build_dirs]
    build_dir = builds[0] if builds else None
    is_build_write = rule.engine in OWN_WRITE_ENGINES
    if is_build_write and not builds:
        check_report.write_line(
            f"guard write: the rule {rule.id} reads the objects of a build: give --build-dir DIR", is_error=True
        )
        return 2
    if len(builds) > 1:
        refusal = (
            reduce_refusal(rule.id, root, builds)
            if is_build_write
            else "more than one --build-dir applies only to a rule of the engine plugin or elf"
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

        request = plugin_runner.WriteRequest(
            root, build_dir, mode, options.reason, tuple(builds[1:]), tuple(options.paths)
        )
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
            unread_parts = getattr(runner.rule_module(rule), "unread_parts", None)
            unread = list(unread_parts(context)) if unread_parts is not None else []
            if unread:
                check_report.write_line(
                    f"guard write: the ledger of {rule.id} is not written: the parts {', '.join(unread)} do not "
                    f"apply, and the write would remove their rows",
                    is_error=True,
                )
                return 2
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
            named = frozenset(options.paths)
            found = [item for item in found if not named or item.path in named]
            written = rule_ledger.write(
                rule, rule_ledger.rows_of_paths(ledger, named), found, mode=mode, reason=options.reason
            )
            written = rule_ledger.merged_paths(ledger, written, named, mode, options.reason)
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
