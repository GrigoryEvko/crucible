"""quarantine — the count of quarantine findings and opt-out regions in each file, kind and entity can only fall.

THE FINDINGS
    The rule reads the finding lines of the rule quarantine in the sections
    of the objects of the target all of one build.  The runner reads them
    (assay.guard.engines.plugin.runner, THE SECTIONS).  The same line from
    two objects counts one time, for example a finding of a header that two
    units include.

THE OPT-OUT REGIONS
    A finding in an opt-out region has the kind opted-out (CLAUDE.md section
    XXII, R7).  Its row holds the reason of its region, and the ledger then
    holds what each region covers.  A line of the kind region gives the
    file, the first line and the reason of each region of a unit.  The plugin
    refuses a region that opens in a different region, and a region that a
    different file closes.  The region of an opted-out finding is then the
    region of its file with the last first line at or before its line.

    The ledger also counts the regions of each file and reason, with the kind
    region and the reason as the entity.  A region that covers more
    increases a count of opted-out, and a new region increases a count of
    region.  The rule refuses each one until `guard write --admit` writes it.
    The rule counts only the regions that a build compiles.  A region in a
    file or a preprocessor arm that no configuration compiles has no row.

THE ENTITY
    The ledger counts the findings of each file, kind and entity.  The entity
    of a row is the part of the entity of a finding that names what the
    plugin refuses (entity_key):
      * std-object and c-library-object: the library class without the type
        in parentheses, for example std::vector
      * raw-pointer-object, raw-function-pointer and c-array-object: no
        entity.  Their text is a type of the code.  An alias in a different
        file can change that text in a file that no change touched
      * raw-new-delete: the operator without the type of the expression
      * opted-out: "REASON | KIND ENTITY", the reason of the region, and the
        kind and the entity of the finding in it
      * region: the reason of the region
      * each other kind: the full text, for example std::span::operator[],
        memcpy, or the header of an include
    The entity holds no line, and a change that only moves code keeps each
    row.  A change that removes std::vector from a file and adds std::map to
    it increases the count of std::map, and the rule refuses that change.

THE LEDGER
    utils/ledgers/quarantine.txt holds the count of each file, kind and
    entity, in the rows of assay.core.ledger (the policy count).  `guard
    write` writes each row through assay.core.ledger.  The rows are sorted
    and each row holds one fact.  The resolution of a merge conflict is then
    a new write.  The rows after the head comment:

        format 1
        rule quarantine
        configuration NAME KEY VALUE           One row for each key of the
                                               configuration NAME
        FILE | KIND | COUNT | ENTITY | -       The count of the base
                                               configuration.  A kind with
                                               no entity has the cell "-"
        @NAME | FILE | KIND | DELTA | ENTITY | -
                                               The count of the configuration
                                               NAME, as a difference from the
                                               base count: +N or -N

    The first configuration of the ledger is the base (THE CONFIGURATION of
    the runner).  The count of a different configuration is the base count
    plus its difference.  A change that lowers a count in each configuration
    then writes only the base rows.

THE VERDICT
    * A count more than its ledger count is an error.  A change can only
      lower a count, as rule R11 of misc/01_10_2026_quarantine.md says.  A new
      finding cannot stay when a finding of a different entity or of a
      different file goes.
    * A file that the ledger does not hold and that has a finding is an error:
      a new file has no finding (CLAUDE.md section XXII).
    * A count less than its ledger count is an error too.  The commit that
      removes a finding writes the ledger again, and no row keeps a count
      that is too high.
    * An opted-out finding of a file with no region line is an error, because
      the rule cannot give it its region.
    * Each directory gives one warning, and the findings and the regions that
      stay in the quarantine are then in the output of each run.  The
      directory of a file starts with the longest path of the rule table that
      holds the file.  That path is the path of a layer row or a quarantine
      row, and it ends with '/'.  The directory is that path and the first
      directory after it.
    * Each problem of the census of the runner is an error.

WRITE THE LEDGER
    `guard write --rule quarantine --build-dir DIR` writes the rows of the
    configuration of the build.  For the base, it writes the counts.  For a
    different configuration, it writes the differences from the base rows.
    Write the base rows first.  The write keeps the head comment, the other
    configurations and their rows.  It refuses a count more than its row:
    remove the new finding.
    * --raise writes such a count too.  Use it only when the plugin or the
      rule table changed what the plugin reports.  Give the reason in the
      commit.
    * --admit writes a count of opted-out or of region more than its row, and
      no other count.  Use it only for a region of a reason class of CLAUDE.md
      section XXII.  Such a region must cover more, or it is a new region.
      Give the reason in the commit.  The rule history gives a warning for
      each such count on each run.
    * --reason TEXT gives the reason cell of each new row.
    * A file that git reports as moved takes the rows of its earlier path in
      each configuration.  Git reports a move with `git diff --find-renames
      HEAD`, after `git mv` or `git add`.
    * The write removes the rows of a source file that the tree does not hold
      in each configuration, because no build compiles it.  A file of the
      build directory keeps its rows, because a build of a different
      configuration can write it.  The run of that build compares those rows
      as it compares each other row.
    * --add-configuration writes the configuration of the build with its
      name.  It replaces the keys and the rows of a configuration with the
      same name that is not the base.  The first configuration of an empty
      ledger is the base.  A change of the keys of the base makes the ledger
      again.  Keep its head and remove its rows.  Then write each
      configuration with --add-configuration in its build, the base first.

A CONFIGURATION THAT ONLY CI BUILDS
    No aarch64 compiler is on the build host, and only a CI leg can count an
    aarch64 configuration.  For a configuration other than the base, the run
    writes each row of the build with the prefix "ledger-row: "
    (assay.core.ledger.rows_of_counts).  It writes them after a failure and
    when the ledger does not hold the configuration.  `guard write --rule
    quarantine --import LOG` puts those rows into the ledger
    (assay.core.ledger.import_rows).  They replace the earlier rows of the
    configuration.  Import the log of the commit that you merge, because a
    difference row is relative to the base rows of that commit.

    Import only a configuration that no build of the host counts.  The name
    of a configuration does not hold its tier.  The rows of an x86_64 CI
    runner of a different tier then replace the rows that the local build
    counts.

NOT APPLICABLE
    The rule exits 77 when the runner does not apply to the build (THE STATUS
    of the runner).  It also exits 77 when the ledger does not hold the
    configuration of the build.  It then writes one line with the tier of the
    build and the tiers of the ledger (target.not_held).  Then it writes the
    rows of the build.

Usage
    guard check --rule quarantine --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]
    guard write --rule quarantine --build-dir DIR [--raise | --admit] [--reason TEXT] [--add-configuration]
    guard write --rule quarantine --import LOG
    guard list --build-dir DIR [--path FILE|DIRECTORY] [--kind KIND]

Complexity: linear in the finding lines of the build, and O(k log k) for k rows of the ledger and counts of the build.
"""

import bisect
import dataclasses
from collections import Counter
from collections.abc import Iterable, Mapping
from pathlib import Path

from assay.core import git, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import runner
from assay.readers import sections as quarantine_sections

RULE = "quarantine"
OPTED_OUT = quarantine_sections.OPTED_OUT
REGION = quarantine_sections.REGION
# The kinds that --admit writes with a count more than their rows: a region that covers more, and a new region.
ADMITTED_KINDS = frozenset((OPTED_OUT, REGION))
# The kinds with the entity "NAME (TYPE)": the entity of a row is the library name (THE ENTITY).
NAME_KINDS = frozenset(("std-object", "c-library-object"))
# The kinds with an entity that is a type of the code: a row has no entity.
TYPE_KINDS = frozenset(("raw-pointer-object", "raw-function-pointer", "c-array-object"))
NEW_DELETE_KIND = "raw-new-delete"
# The separator between the reason of a region and the finding in it, in the entity of an opted-out row.
REASON_SEPARATOR = " | "
# The operators of a new-expression, the longest first.  The type after the operator is not part of the entity.
NEW_OPERATORS = ("placement new", "new")
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
# The highest character, which sorts after each reason of a region.
LAST_CHARACTER = chr(0x10FFFF)

# The file, the kind and the entity of one count: the key of a row of the ledger.
Key = rule_ledger.Key


# ── The keys ───────────────────────────────────────────────────────


def joined(*fields: str) -> str:
    """Return the fields with one space between them.  An empty field at the end gives no space."""
    return " ".join(fields).rstrip(" ")


def entity_key(kind: str, entity: str) -> str:
    """Return the entity of the row of one finding: the part of its entity that names what the plugin refuses.

    THE ENTITY in the head comment gives the rule for each kind.  The text has one space between its words.
    """
    text = " ".join(entity.split())
    if kind in NAME_KINDS:
        return text.split(" (", 1)[0]
    if kind in TYPE_KINDS:
        return ""
    if kind == NEW_DELETE_KIND:
        return next(
            (operator for operator in NEW_OPERATORS if text == operator or text.startswith(f"{operator} ")), text
        )
    return text


def opted_out_key(reason: str, entity: str) -> str:
    """Return the entity of the row of an opted-out finding: the reason of its region, the inner kind and entity."""
    inner, _, rest = " ".join(entity.split()).partition(" ")
    return joined(f"{' '.join(reason.split())}{REASON_SEPARATOR}{inner}", entity_key(inner, rest))


def described(kind: str, entity: str) -> str:
    """Return the kind and the entity of a row as words for a message."""
    return f"the kind {kind} and the entity {entity}" if entity else f"the kind {kind}"


def directory_rows(table: ruletable.RuleTable) -> list[str]:
    """Return the paths of the layer rows and quarantine rows that end with '/', the longest first."""
    rows = {path for layer in table.layers for path in layer.paths if path.endswith("/")}
    rows |= {path for path in table.quarantines if path.endswith("/")}
    return sorted(rows, key=lambda path: (-len(path), path))


def directory_of(rel: str, rows: list[str]) -> str:
    """Return the directory of a file: the longest directory row that holds it, and the first directory after it."""
    for row in rows:
        if rel.startswith(row):
            rest = rel[len(row) :].split("/")
            return row.rstrip("/") + (f"/{rest[0]}" if len(rest) > 1 else "")
    return rel.rsplit("/", 1)[0] if "/" in rel else "."


# ── The findings ───────────────────────────────────────────────────


def reasons_of(census: runner.Census) -> tuple[dict[quarantine_sections.Finding, str], list[check_report.Finding]]:
    """Give each opted-out finding the reason of its region, and an error to each one with no region.

    Complexity: O(f log r) for f opted-out findings and r regions of a file.
    """
    reasons: dict[quarantine_sections.Finding, str] = {}
    problems: list[check_report.Finding] = []
    for item in census.of_rule(RULE):
        if item.kind != OPTED_OUT:
            continue
        starts = census.regions.get(item.file, ())
        index = bisect.bisect_right(starts, (item.line, LAST_CHARACTER)) - 1
        if index < 0:
            problems.append(
                check_report.Finding(
                    "error",
                    item.file,
                    item.line,
                    RULE,
                    f"the finding {item.text()!r} is opted out, and no section gives a region of {item.file} at or "
                    f"before its line.  The plugin writes a line of the kind region for each region: compile the unit "
                    f"again",
                )
            )
            continue
        reasons[item] = starts[index][1]
    return reasons, problems


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line and each region of the build, keyed by its row, and each problem.

    A problem has no kind.  An opted-out finding with no region gives a problem and no count.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    reasons, problems = reasons_of(census)
    found = [*census.errors(RULE), *problems]
    for item in census.of_rule(RULE):
        if item.kind != OPTED_OUT:
            found.append(
                check_report.Finding(
                    "error", item.file, item.line, RULE, item.text(), item.kind, entity_key(item.kind, item.entity)
                )
            )
        elif item in reasons:
            found.append(
                check_report.Finding(
                    "error",
                    item.file,
                    item.line,
                    RULE,
                    item.text(),
                    OPTED_OUT,
                    opted_out_key(reasons[item], item.entity),
                )
            )
    for file, items in sorted(census.regions.items()):
        for line, reason in items:
            found.append(
                check_report.Finding("error", file, line, RULE, f"an opt-out region: {reason}", REGION, reason)
            )
    return found


def counts_of(found: Iterable[check_report.Finding]) -> Counter[Key]:
    """Return the number of findings of each file, kind and entity, with no problem.  Complexity: linear."""
    return Counter(Key(item.path, item.kind, item.entity) for item in found if item.kind)


def problems_of(found: Iterable[check_report.Finding]) -> list[check_report.Finding]:
    """Return the problems of the findings: the findings with no kind."""
    return [item for item in found if not item.kind]


# ── The verdict ────────────────────────────────────────────────────


def load_table(root: Path) -> ruletable.RuleTable:
    """Read the rule table of the tree of the root.

    Raises:
        ruletable.TableError: If the table does not obey its grammar
        OSError: If the table cannot be read
    """
    return ruletable.load(root / TABLE)


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, name: str, rows: list[str], build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count, and give one warning for each directory with no error.

    Complexity: O(k log k) for k counts of the build and the ledger.
    """
    findings_found: list[check_report.Finding] = []
    expected = ledger.expected(name)
    held = {key.path for key in expected}
    failed: set[str] = set()
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    for key in sorted(set(counts) | set(expected)):
        file, kind, entity = key.path, key.kind, key.entity
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured == admitted:
            continue
        failed.add(directory_of(file, rows))
        line = ledger.line_of(key, name)
        listing = f"`python3 utils/bin/guard list --build-dir {build_shown} --path {file} --kind {kind}`"
        what = described(kind, entity)
        if measured > admitted and kind == OPTED_OUT:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The region "
                f"covers more, and a region can only shrink.  List them with {listing}, and move the code out "
                f"of the region.  If the reason class of the region (CLAUDE.md section XXII) makes the code "
                f"necessary, write the ledger with `{command} --admit` and give the reason in the commit"
            )
        elif measured > admitted and kind == REGION:
            message = (
                f"{file} has {measured} opt-out regions with the reason {entity!r}, and the ledger admits "
                f"{admitted}.  The number of regions can only fall: use a type of fixy or foundation, and "
                f"remove the region.  If the reason class of the region (CLAUDE.md section XXII) makes the "
                f"region necessary, write the ledger with `{command} --admit` and give the reason in the "
                f"commit"
            )
        elif measured > admitted and file not in held:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger holds no row of the file.  A new "
                f"file has no finding (CLAUDE.md section XXII).  List them with {listing}, and use a type of "
                f"fixy or foundation.  If the change moves the file, stage the move with `git mv` and write "
                f"the ledger again"
            )
        elif measured > admitted:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The change adds "
                f"{measured - admitted}, and a change can only lower a count, also when a finding of a "
                f"different entity goes.  List them with {listing}, and use a type of fixy or foundation"
            )
        else:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger holds {admitted}.  Write the ledger "
                f"again in the same commit: `{command}`"
            )
        findings_found.append(check_report.Finding("error", shown, line, RULE, message))
    kept: dict[str, Counter[str]] = {}
    first_line: dict[str, int] = {}
    for key, admitted in sorted(expected.items()):
        directory = directory_of(key.path, rows)
        if directory in failed:
            continue
        kept.setdefault(directory, Counter())[key.kind] += admitted
        first_line.setdefault(directory, ledger.line_of(key, name))
    for directory, kinds in sorted(kept.items()):
        listed = ", ".join(f"{kind} {count}" for kind, count in sorted(kinds.items()))
        findings_found.append(
            check_report.Finding(
                "warning",
                shown,
                first_line[directory],
                RULE,
                f"{directory} keeps {sum(kinds.values())} quarantine findings and regions: {listed}",
            )
        )
    return findings_found


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context (THE VERDICT).

    Raises:
        NotApplicable: If the rule does not apply to the build, or the ledger does not hold its configuration
        BuildError: If the build cannot be read
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        table = load_table(context.root)
    except (OSError, rows_grammar.RowsError, ruletable.TableError) as problem:
        return [input_problem(problem)]
    configuration = context.configuration()
    if not runner.is_held(ledger, configuration):
        raise runner.NotApplicable(runner.held_line(RULE, ledger, configuration))
    found = findings(context)
    return [
        *problems_of(found),
        *evaluate(counts_of(found), ledger, configuration.name, directory_rows(table), context.build_shown()),
    ]


def input_problem(problem: Exception) -> check_report.Finding:
    """Return the error of a ledger or a rule table that cannot be read, at the line of the problem."""
    return check_report.Finding("error", rule_ledger.shown_path(RULE), getattr(problem, "line", 0), RULE, str(problem))


def summary(counts: Mapping[Key, int], configuration: runner.BuildConfiguration, objects: int, verb: str) -> str:
    """Return the line that tells the findings, the regions, the rows and the files of one run or one write."""
    regions = sum(count for key, count in counts.items() if key.kind == REGION)
    files = {key.path for key in counts}
    return (
        f"{RULE}: {verb} {sum(counts.values()) - regions} findings and {regions} regions in {len(counts)} rows "
        f"of {len(files)} files, configuration {configuration.name} on {configuration.tier}, from {objects} "
        f"objects"
    )


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge its counts against the ledger, and write the findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        table = load_table(context.root)
    except (OSError, rows_grammar.RowsError, ruletable.TableError) as problem:
        return check_report.emit([input_problem(problem)], RULE, warnings_dir)
    census = runner.census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    try:
        configuration = context.configuration()
    except runner.BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, RULE, str(problem))], RULE, warnings_dir
        )
    found = findings(context)
    counts, problems = counts_of(found), problems_of(found)
    if not runner.is_held(ledger, configuration):
        check_report.write_line(runner.held_line(RULE, ledger, configuration))
        print_rows(ledger, configuration, counts, bool(problems))
        return runner.SKIPPED
    status = check_report.emit(
        [*problems, *evaluate(counts, ledger, configuration.name, directory_rows(table), context.build_shown())],
        RULE,
        warnings_dir,
    )
    check_report.write_line(summary(counts, configuration, census.objects, "counted"))
    if status != 0:
        print_rows(ledger, configuration, counts, bool(problems))
    return status


def print_rows(
    ledger: rule_ledger.Ledger, configuration: runner.BuildConfiguration, counts: Mapping[Key, int], has_problems: bool
) -> None:
    """Write the rows of the configuration of the build, as --import reads them from a log.

    The base has no such rows, because a write writes it, and a build with a problem has no rows to write.  Each
    row has the prefix assay.core.ledger.ROW_MARK.
    """
    if configuration.name == ledger.base_name or ledger.base_name is None or has_problems:
        return
    for line in rule_ledger.rows_of_counts(ledger, configuration.name, list(configuration.facts), counts):
        check_report.write_line(line)
    check_report.write_line(
        f"{RULE}: the lines above give the rows of the configuration {configuration.name}.  "
        f"Import them from the log with `python3 utils/bin/guard write --rule {RULE} --import "
        f"LOG`"
    )


# ── The write ──────────────────────────────────────────────────────


def import_rows(rule: rule_registry.Rule, root: Path, source: Path) -> int:
    """Put the rows of one configuration that print_rows wrote into the ledger.  They replace its earlier rows.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = rule_ledger.import_rows(
            rule_ledger.read(rule, root), source.read_text(encoding="utf-8", errors="replace")
        )
    except (OSError, rows_grammar.RowsError, rule_ledger.LedgerError) as problem:
        check_report.write_line(f"{RULE}: the ledger is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(ledger, root)
    name = ledger.configurations[-1].name
    check_report.write_line(
        f"{RULE}: imported the configuration {name}: {len(ledger.configurations[-1].facts)} keys "
        f"and {len(ledger.deltas.get(name, {}))} difference rows, to "
        f"{rule_ledger.ledger_path(RULE, root)}"
    )
    return 0


def add_refusal(ledger: rule_ledger.Ledger, held: str | None, name: str) -> str:
    """Return the reason that --add-configuration cannot write the configuration of the build, or an empty text."""
    if not rule_ledger.rows_grammar_name(name):
        return f"the name {name!r} of the configuration is not a lowercase word"
    if held is not None:
        return f"the ledger holds the configuration {name} of the build.  Write its rows with no --add-configuration"
    if name == ledger.base_name:
        return (
            f"the build has other keys than the base configuration {name}.  A change of the base keys makes the "
            f"ledger again: keep its head, remove its rows, then write each configuration with "
            f"--add-configuration in its build, the base first"
        )
    return ""


def drop_gone_files(ledger: rule_ledger.Ledger, root: Path, counts: Mapping[Key, int]) -> rule_ledger.Ledger:
    """Remove the difference rows of each source file that the tree does not hold, and keep each count at 0 or above.

    A file of the build directory (ruletable.BUILD_PREFIX) is in no source tree.  A write of the base cannot know if
    a build of a different configuration writes the file.  The rows of the file stay, and the run of that
    configuration compares them as it compares each other row.  When a build of the configuration does not write
    the file, its run fails on the row until its own write removes the row.  Complexity: linear in the difference
    rows.
    """
    deltas: dict[str, dict[Key, rule_ledger.Entry]] = {}
    for name, table in ledger.deltas.items():
        kept: dict[Key, rule_ledger.Entry] = {}
        for key, entry in table.items():
            base = int(ledger.base[key].value) if key in ledger.base else 0
            delta = int(entry.value)
            is_source_gone = not key.path.startswith(ruletable.BUILD_PREFIX) and not (root / key.path).exists()
            if key not in counts and is_source_gone:
                continue
            if base + delta < 0:
                delta = -base
            if delta != 0:
                kept[key] = dataclasses.replace(entry, value=delta)
        deltas[name] = kept
    return dataclasses.replace(ledger, deltas=deltas)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (WRITE THE LEDGER), or import the rows of a log.

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  SKIPPED when the rule does not apply or the ledger does not hold the configuration
    """
    if request.import_log is not None:
        return import_rows(rule, request.root, request.import_log)
    context = runner.PluginContext(request.root, request.build_dir)
    try:
        ledger = rule_ledger.read(rule, request.root)
        census = context.census()
        configuration = context.configuration()
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    except (OSError, rows_grammar.RowsError, runner.BuildError) as problem:
        check_report.write_line(f"{RULE}: the ledger is not written: {problem}", is_error=True)
        return 1
    found = findings(context)
    problems = problems_of(found)
    if problems:
        check_report.write_line(f"{RULE}: the ledger is not written, because an object has a problem", is_error=True)
        return check_report.emit(problems, RULE, None)
    counts = counts_of(found)
    name = configuration.name
    held = name if runner.is_held(ledger, configuration) else None
    moves: dict[str, str] = {}
    mode = rule_ledger.WriteMode.LOWER
    if request.add_configuration:
        refusal = add_refusal(ledger, held, name)
        if refusal:
            check_report.write_line(f"{RULE}: the ledger is not written: {refusal}", is_error=True)
            return 1
        others = tuple(item for item in ledger.configurations if item.name != name)
        ledger = dataclasses.replace(
            ledger,
            configurations=(*others, rule_ledger.Configuration(name, configuration.facts)),
            deltas={key: table for key, table in ledger.deltas.items() if key != name},
        )
        mode = rule_ledger.WriteMode.RAISE
    elif held is None:
        check_report.write_line(runner.held_line(RULE, ledger, configuration))
        check_report.write_line(
            f"{RULE}: the ledger is not written.  Give --add-configuration to write the configuration of the build",
            is_error=True,
        )
        return runner.SKIPPED
    else:
        moves = git.moves(request.root)
        increased = increases_permitted(ledger, name, counts, moves, request.mode)
        if increased is None:
            return 1
        mode = rule_ledger.WriteMode.RAISE if increased else rule_ledger.WriteMode.LOWER
    written = rule_ledger.write_counts(rule, ledger, counts, name, mode, request.reason, moved=moves)
    if name == written.base_name:
        written = drop_gone_files(written, request.root, counts)
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"{summary(counts, configuration, census.objects, 'wrote')}, to {rule_ledger.ledger_path(RULE, request.root)}"
    )
    return 0


def increases_permitted(
    ledger: rule_ledger.Ledger,
    name: str,
    counts: Mapping[Key, int],
    moves: Mapping[str, str],
    mode: rule_ledger.WriteMode,
) -> bool | None:
    """Write each count of the build that is more than its row, and tell if the mode of the write permits each one.

    --raise permits each increase, and --admit permits an increase of opted-out and of region.

    Returns:
        True when a count increased and the mode permits each increase.  False when no count increased.  None when
        the mode refuses an increase
    """
    expected = rule_ledger.moved_counts(ledger.expected(name), moves)
    held_files = {key.path for key in expected}
    increases = sorted(
        (key, count, expected.get(key, 0)) for key, count in counts.items() if count > expected.get(key, 0)
    )
    for key, count, admitted in increases:
        words = "a file that the ledger does not hold" if key.path not in held_files else f"rises from {admitted}"
        check_report.write_line(
            f"{RULE}: {key.path} has {count} findings of {described(key.kind, key.entity)}, {words}", is_error=True
        )
    refused = [
        increase
        for increase in increases
        if mode != rule_ledger.WriteMode.RAISE
        and not (mode == rule_ledger.WriteMode.ADMIT and increase[0].kind in ADMITTED_KINDS)
    ]
    if refused:
        check_report.write_line(
            f"{RULE}: the ledger is not written: {len(refused)} counts rose.  Remove the new "
            f"findings.  Give --admit only for a region that must cover more or a new region, "
            f"and --raise only when the plugin or the rule table changed what it reports",
            is_error=True,
        )
        return None
    return bool(increases)
