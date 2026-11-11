"""guard list — print the rules of the registry, or the ledger rows of each rule for one file.

usage: guard list [--engine E]
       guard list --path FILE|DIRECTORY/
       guard list --build-dir DIR [--path FILE|DIRECTORY|DIRECTORY/] [--kind KIND]

The first form prints one line for each rule: its ID, its engine, its
policy, its limit, and the module that holds it (its own module, its
legacy module or its holder).  The second form prints each ledger row of each
rule of utils/ledgers/ whose path is FILE, or the directory of FILE, so a
reader at the code sees each admission of that file.  The rule quarantine
keys its rows by the directory of a file.  A path with '/' at its end names
the rows of that directory.

With --build-dir, the command prints each finding line and each region line
of the sections of the objects of the build
(assay.guard.engines.plugin.runner).  --path selects a file, or a directory
and each directory under it.  A path with '/' at its end selects the files of
that one directory.  --kind selects a kind.  The command exits 77 when the
build does not apply.
"""

import sys
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.core.registry import Rule
from assay.guard import check


def holder(rule: Rule) -> str:
    """Return the words that tell what holds a rule."""
    if rule.legacy is not None:
        return f"legacy assay.{rule.legacy.module}" + (f" {' '.join(rule.legacy.words)}" if rule.legacy.words else "")
    if rule.held_by is not None:
        return f"held by {rule.held_by}" + (f", function {rule.holder.text}" if rule.holder is not None else "")
    from assay.guard.engines.meta import runner

    return f"module {runner.module_name_of(rule)}"


def main(argv: list[str]) -> int:
    """Print the rules, or the ledger rows of one file.

    Returns:
        0, or 2 on a usage error
    """
    from assay.core import cli

    parser = cli.parser("guard list", "print the rules of the registry, or the ledger rows of one file")
    parser.add_argument("--engine", default=None)
    parser.add_argument("--path", default=None, metavar="FILE")
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--kind", default=None)
    options = parser.parse_args(argv)
    if options.build_dir is not None:
        from assay.guard.engines.plugin import runner as plugin_runner

        return plugin_runner.print_findings(REPO_ROOT, options.build_dir.resolve(), options.path, options.kind)
    if options.kind is not None:
        check_report.write_line("guard list: --kind selects the findings of a build: give --build-dir DIR",
                                is_error=True)
        return 2
    registry = check.load_registry()
    if registry is None:
        return 2
    if options.path is not None:
        from assay.guard.engines.plugin import quarantine

        wanted = Path(options.path).as_posix().removeprefix("./")
        names = {f"{wanted}/"} if options.path.endswith("/") else {wanted, quarantine.directory_of(wanted)}
        shown = 0
        for rule in registry.rules.values():
            if not rule_ledger.ledger_path(rule.id).is_file():
                continue
            try:
                ledger = rule_ledger.read(rule)
            except (rows_grammar.RowsError, OSError) as problem:
                print(f"guard list: the ledger of {rule.id} cannot be read: {problem}", file=sys.stderr)
                continue
            for entry in sorted(ledger.base.values(), key=lambda item: item.line):
                key = entry.key
                if key.path in names:
                    shown += 1
                    print(f"{rule_ledger.shown_path(rule.id)}:{entry.line}: {rule.id} {key.kind or '-'} "
                          f"{rule_ledger.value_text(entry.value)} {key.entity or '-'}: {entry.reason or 'no reason'}")
        print(f"guard list: {shown} ledger rows of {rule_ledger.LEDGER_DIRECTORY}/ name {wanted}")
        return 0
    listed = 0
    for rule in sorted(registry.rules.values(), key=lambda item: item.id):
        if options.engine is not None and rule.engine.value != options.engine:
            continue
        listed += 1
        threshold = (f" limit {check_report.threshold_text(rule.threshold.limit)} {rule.threshold.unit}"
                     if rule.threshold is not None else "")
        print(f"{rule.id:28} {rule.engine.value:6} {rule.policy.value:8}{threshold}; {holder(rule)}")
    selection = f" of the engine {options.engine}" if options.engine is not None else ""
    print(f"guard list: {listed} rules{selection} of the {len(registry.rules)} rules in "
          f"{REPO_ROOT / 'utils' / 'registry.txt'}")
    return 0
