"""library-code — a base file adds no new code of a library to the objects of the tree.

THE RECORDS
    The quarantine plugin reads the instructions of each function that a
    unit compiles, in a step of the back end of GCC after
    `pass_shorten_branches` (utils/tools/quarantine/body.cpp, THE RECORDS).
    Each compile of the tree gives the plugin the argument library-code=on.
    The plugin then writes four kinds of records of this rule into the
    section of the object:
      inlined    a body of a library function that the code of the tree
                 inlines, at the call site.  The entity is the name of the
                 function with no template arguments
      call       a call of a library function by the code of the tree.  The
                 entity is the symbol and that name
      reference  a library variable or the address of a library function
                 that the code of the tree names, with the same entity
      symbol     each other symbol that the code of the tree can import,
                 with the symbol as the entity.
    The runner reads the records of each object of the target all
    (assay.guard.engines.plugin.runner, THE SECTIONS).  The same record from
    two objects counts one time.

THE COUNTS
    The rule counts the records inlined, call and reference of the base
    files: the scope of its row in utils/registry.txt
    (assay.guard.engines.plugin.declarations, THE SCOPE).  A record of a base
    header counts also when a quarantined unit compiles the header.  The key
    of a count is the file, the kind and the name of the library function
    or variable (name_of).  The symbol stays out of the key, because each
    template instance has a symbol of its own.  A count is the number of the
    locations of the file with a record of that kind and name.  A record
    symbol is no count.  The rule artifact-symbols reads it (THE HIDDEN
    IMPORTS of assay.guard.engines.elf.artifact_symbols).

THE LEDGER
    utils/ledgers/library-code.txt holds the count of each file, kind and
    name in each configuration of a build (assay.guard.engines.plugin.counted,
    THE COUNTED RULES).  The inlining changes with the build kind, and each
    configuration holds counts of its own.

THE VERDICT
    * A count more than its row is an error: a base file adds more code of a
      library.  A file that the ledger does not hold and that has a record is
      an error too.
    * A count less than its row is an error, until the commit writes the
      ledger again.
    * Each directory of the base with rows gives one warning
      (assay.guard.engines.plugin.quarantine.directory_of).
    * Each problem of the census of the runner is an error.

THE WRITE
    `guard write --rule library-code --build-dir DIR` writes the rows of the
    configuration of the build.  It refuses a count more than its row.
    --raise writes such a count too.  Use it only for a new toolchain, or for
    a change of the plugin that changes its records.  Give the reason in the
    commit.

NOT APPLICABLE
    The rule exits 77 when the runner does not apply to the build (THE STATUS
    of the runner).  A build that only parses gives no record, because a
    parse compiles no function (assay.core.configuration.is_parse_build).
    The rule does not apply to such a build.

Usage
    guard check --rule library-code --build-dir DIR [--warnings-dir DIR]
    guard write --rule library-code --build-dir DIR [--raise --reason TEXT] [--add-configuration]
    guard write --rule library-code --import LOG

Complexity: linear in the records of the build, and O(k log k) for k rows of the ledger and counts of the build.
"""

from collections import Counter
from collections.abc import Mapping
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import counted, declarations, quarantine, runner

RULE = "library-code"
INLINED = "inlined"
CALL = "call"
REFERENCE = "reference"
SYMBOL = "symbol"
# The kinds of a record that the rule counts (THE COUNTS).
COUNTED_KINDS = frozenset({INLINED, CALL, REFERENCE})
# The kinds of a record with the symbol before the name in its entity.
SYMBOL_KINDS = frozenset({CALL, REFERENCE, SYMBOL})
REFUSAL = (
    "Remove the new library code from the base file: use a type of fixy or foundation.  Give --raise only for a new "
    "toolchain, or when the plugin changed what it records"
)
PARSE_REASON = "the build only parses, and a parse compiles no function and gives no record"

Key = rule_ledger.Key


def name_of(kind: str, entity: str) -> str:
    """Return the name of the entity of a record: the entity with no symbol, with one space between its words.

    The entity of a record call or reference is the symbol and the name, and the name can hold a space, as in
    operator new.  The symbol holds no space.
    """
    words = entity.split()
    return " ".join(words[1:] if kind in SYMBOL_KINDS else words)


def is_parse(build_dir: Path | None) -> bool:
    """Return True for a build that only parses (NOT APPLICABLE), and False with no build directory."""
    return build_dir is not None and build_config.is_parse_build(build_dir)


def parse_status(build_dir: Path | None) -> int | None:
    """Return SKIPPED after one line for a build that only parses (NOT APPLICABLE), else None."""
    if not is_parse(build_dir):
        return None
    check_report.write_line(f"{RULE}: {PARSE_REASON}.  The rule does not apply")
    return runner.SKIPPED


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one finding for each location, kind and name of a counted record of a base file, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build, or the build only parses
        BuildError: If the build cannot be read
    """
    if is_parse(context.build_dir):
        raise runner.NotApplicable(PARSE_REASON)
    rule = declarations.rule_of(context, RULE)
    table = declarations.table_of(context)
    census = context.census()
    places: dict[tuple[str, int, int, str, str], check_report.Finding] = {}
    for item in census.of_rule(RULE):
        if item.kind not in COUNTED_KINDS or not declarations.in_scope(rule, item.file, table):
            continue
        name = name_of(item.kind, item.entity)
        places.setdefault(
            (item.file, item.line, item.column, item.kind, name),
            check_report.Finding("error", item.file, item.line, RULE, item.text(), item.kind, name),
        )
    return [*census.errors(RULE), *places.values()]


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, name: str | None, rows: list[str], build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its row, and give one warning for each directory with rows (THE VERDICT).

    `rows` are the directory rows of quarantine.load_rows.  Complexity: O(k log k) for k counts of the build and the
    ledger.
    """
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    expected = ledger.expected(name)
    held = {key.path for key in expected}
    is_bound = name == rule_ledger.MAXIMUM
    errors: list[check_report.Finding] = []
    failed: set[str] = set()
    for key in sorted(set(counts) | set(expected)):
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured == admitted or (is_bound and measured < admitted):
            continue
        failed.add(quarantine.directory_of(key.path, rows))
        listing = f"`python3 utils/bin/guard list --build-dir {build_shown} --path {key.path} --kind {key.kind}`"
        if measured < admitted:
            said = f"the ledger holds {admitted}.  Write the ledger again in the same commit: `{command}`"
        elif key.path not in held:
            said = (
                f"the ledger holds no row of the file.  A new base file adds no code of a library.  List the "
                f"records with {listing}, and use a type of fixy or foundation"
            )
        else:
            said = (
                f"the ledger admits {admitted}.  A change can only lower a count.  List the records with {listing}, "
                f"and use a type of fixy or foundation"
            )
        errors.append(
            check_report.Finding(
                "error",
                shown,
                ledger.line_of(key, name),
                RULE,
                f"{key.path} has {measured} records {key.kind} of {key.entity}, and {said}",
            )
        )
    kept: dict[str, Counter[str]] = {}
    kept_keys: dict[str, list[Key]] = {}
    for key, admitted in expected.items():
        directory = quarantine.directory_of(key.path, rows)
        count = min(admitted, counts.get(key, 0)) if is_bound else admitted
        if count and directory not in failed:
            kept.setdefault(directory, Counter())[key.kind] += count
            kept_keys.setdefault(directory, []).append(key)
    warnings: list[check_report.Finding] = []
    for directory, kinds in sorted(kept.items()):
        listed = ", ".join(f"{kind} {count}" for kind, count in sorted(kinds.items()))
        state, tail = rule_ledger.kept_words(ledger, kept_keys[directory], name)
        warnings.append(
            check_report.Finding(
                "warning",
                shown,
                0,
                RULE,
                f"{directory} keeps {sum(kinds.values())} records of library code{state}: {listed}{tail}",
            )
        )
    return [*errors, *warnings]


def summary(counts: Mapping[Key, int], configuration: build_config.BuildConfiguration, objects: int, verb: str) -> str:
    """Return the line that tells the records, the rows and the files of one run or one write."""
    kinds = Counter[str]()
    for key, count in counts.items():
        kinds[key.kind] += count
    shown = ", ".join(f"{kind} {kinds[kind]}" for kind in sorted(COUNTED_KINDS)) or "none"
    return (
        f"{RULE}: {verb} {sum(counts.values())} records of library code ({shown}) in {len(counts)} rows of "
        f"{len({key.path for key in counts})} base files, configuration {configuration.name} on "
        f"{configuration.tier}, from {objects} objects"
    )


SPEC = counted.CountedRule(RULE, findings, quarantine.load_rows, evaluate, summary, frozenset(), REFUSAL)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context (THE VERDICT).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return counted.judge(SPEC, rule, context)


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge its counts against the ledger, and write the findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    status = parse_status(context.build_dir)
    return counted.run(SPEC, rule, context, warnings_dir) if status is None else status


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (THE WRITE), or import the rows of a log.

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  1 also when the ledger does not hold the configuration of the build and the write does not add it.
        SKIPPED when the rule does not apply
    """
    status = parse_status(request.build_dir) if request.import_log is None else None
    return counted.write(SPEC, rule, request) if status is None else status
