"""guard write — write the ledger of one rule through assay.core.ledger, with the checks of its policy.

usage: guard write --rule ID [--raise | --admit] [--reason TEXT] [--root DIR] [--build-dir DIR]
       guard write --rule ID [ARG]...      a rule with a legacy module

A rule with a module of its own: the command runs the rule and writes its
ledger in the mode of the call (assay.core.ledger, THE WRITE).  The default
mode lowers each count and removes each row that admits nothing, and it
refuses a new finding.  --raise also writes a count above its row: use it only
when the rule changed what it reports, and give the reason in the commit.
--admit also writes a row for each new finding, with the reason of --reason.

A rule with a legacy module: the command calls the module with the words of
its mode, then --write, then each ARG, as `guard check --rule ID --write`
does.
"""

import sys
from pathlib import Path

from assay.core import ledger as rule_ledger
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
    options = parser.parse_args(argv)
    chosen = check.selected(registry, [options.rule], [])
    if isinstance(chosen, str):
        print(f"guard write: {chosen}", file=sys.stderr)
        return 2
    rule = chosen[0]
    root = options.root.resolve()
    context = runner.MetaContext(root, options.build_dir.resolve() if options.build_dir is not None else None)
    try:
        findings = runner.rule_module(rule).findings(context)
        ledger = rule_ledger.read(rule, root)
        written = rule_ledger.write(rule, ledger, findings, mode=options.mode or rule_ledger.WriteMode.LOWER,
                                    reason=options.reason, moved=git.moves(root))
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
