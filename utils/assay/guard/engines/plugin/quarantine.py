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
      * raw-pointer-object, raw-function-pointer and c-array-object: no
        entity.  The plugin gives the name of the declaration, and the row
        counts the findings of the file and the kind
      * `layer-header`: the header that the include names, for example
        <vector>, without the layer of the file
      * `upward-include`: the file that the include names, without the two
        layers
      * opted-out: "REASON | KIND ENTITY", the reason of the region, and the
        kind and the entity of the finding in it
      * region: the reason of the region
      * each other kind: the full text, for example std::span,
        std::span::operator[], memcpy, new, or the header of a `door-header`
        include
    The entity holds no line, and a change that only moves code keeps each
    row.  A change that removes std::vector from a file and adds std::map to
    it increases the count of std::map, and the rule refuses that change.
    The layer of a file is a fact of its path, and the row keys by the path.
    A row of an include then holds no layer.  A file that git moves to a
    different layer keeps the rows of its includes.  The message of each
    finding keeps the full text, with the layers.

    The rule counts each finding line of the build one time.  One
    declaration must then give one line in each unit.  Because of this, the
    entity of an object finding holds no printed type.  GCC prints a type with
    the template arguments and the parameter names of the first
    specialization that the unit made with the same arguments.  One
    declaration of a header then gave std::span<unsigned char> in one unit and
    std::span<unsigned char, 18446744073709551615> in a different unit, and
    the count of the file changed with the other units of the build.  The
    plugin writes these entities (utils/tools/quarantine/declaration.cpp, THE
    ENTITY OF AN OBJECT):
      * std-object and c-library-object: the library class with no template
        argument, for example std::span
      * raw-pointer-object, raw-function-pointer and c-array-object: the name
        of the declaration
      * raw-new-delete: the operator, for example new or placement new.

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
    then writes only the base rows.  The run, the write and the import are
    those of assay.guard.engines.plugin.counted, which the rule heavy-include
    also uses.

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
      each configuration.  Git reports a move after `git mv` or `git add`,
      when the similarity of the two files is a minimum of
      assay.core.git.MOVE_SIMILARITY percent (THE MOVES of assay.core.git).  A
      rewrite below it is a new file.
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
from collections import Counter
from collections.abc import Mapping
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import counted, runner
from assay.readers import sections as quarantine_sections

RULE = "quarantine"
OPTED_OUT = quarantine_sections.OPTED_OUT
REGION = quarantine_sections.REGION
# The kinds that --admit writes with a count more than their rows: a region that covers more, and a new region.
ADMITTED_KINDS = frozenset((OPTED_OUT, REGION))
# The kinds with the name of a declaration as the entity: a row has no entity (THE ENTITY).
DECLARATION_KINDS = frozenset(("raw-pointer-object", "raw-function-pointer", "c-array-object"))
# The kinds of an include.  Each kind gives the text after the header or the target in its entity, and the start of
# that text that the key keeps.  The plugin writes "<HEADER> in ALLOWANCE" and "TARGET (LAYER) from the layer LAYER"
# (utils/tools/quarantine/include.cpp).
INCLUDE_KIND_ENDS = {"layer-header": ("> in ", ">"), "upward-include": (" (", "")}
# The separator between the reason of a region and the finding in it, in the entity of an opted-out row.
REASON_SEPARATOR = " | "
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
# The highest character, which sorts after each reason of a region.
LAST_CHARACTER = chr(0x10FFFF)
# The advice of a write that refuses a count above its row.
REFUSAL = (
    "Remove the new findings.  Give --admit only for a region that must cover more or a new region, and --raise only "
    "when the plugin or the rule table changed what it reports"
)

# The file, the kind and the entity of one count: the key of a row of the ledger.
Key = rule_ledger.Key


# ── The keys ───────────────────────────────────────────────────────


def joined(*fields: str) -> str:
    """Return the fields with one space between them.  An empty field at the end gives no space."""
    return " ".join(fields).rstrip(" ")


def include_key(kind: str, entity: str) -> str:
    """Return the entity of the row of an include: the header or the target with no layer.  Each other kind keeps it.

    The function keeps an entity that holds no layer.  A row that it wrote then reads back as the same key.  The rule
    history reads each earlier version of the ledger through it.
    """
    end = INCLUDE_KIND_ENDS.get(kind)
    if end is None:
        return entity
    after, kept = end
    head, separator, _ = entity.partition(after)
    return f"{head}{kept}" if separator else entity


def entity_key(kind: str, entity: str) -> str:
    """Return the entity of the row of one finding: the part of its entity that names what the plugin refuses.

    THE ENTITY in the head comment gives the rule for each kind.  The text has one space between its words.
    """
    text = " ".join(entity.split())
    if kind in INCLUDE_KIND_ENDS:
        return include_key(kind, text)
    return "" if kind in DECLARATION_KINDS else text


def opted_out_key(reason: str, entity: str) -> str:
    """Return the entity of the row of an opted-out finding: the reason of its region, the inner kind and entity."""
    inner, _, rest = " ".join(entity.split()).partition(" ")
    return joined(f"{' '.join(reason.split())}{REASON_SEPARATOR}{inner}", entity_key(inner, rest))


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


# ── The verdict ────────────────────────────────────────────────────


def load_table(root: Path) -> ruletable.RuleTable:
    """Read the rule table of the tree of the root.

    Raises:
        ruletable.TableError: If the table does not obey its grammar
        OSError: If the table cannot be read
    """
    return ruletable.load(root / TABLE)


def load_rows(root: Path) -> list[str]:
    """Return the directory rows of the rule table of the tree of the root (directory_rows).

    Raises:
        ruletable.TableError: If the table does not obey its grammar
        OSError: If the table cannot be read
    """
    return directory_rows(load_table(root))


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, name: str, rows: list[str], build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count, and give one warning for each directory with no error.

    `rows` are the directory rows of load_rows.  Complexity: O(k log k) for k counts of the build and the ledger.
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
        what = counted.described(kind, entity)
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


def summary(counts: Mapping[Key, int], configuration: runner.BuildConfiguration, objects: int, verb: str) -> str:
    """Return the line that tells the findings, the regions, the rows and the files of one run or one write."""
    regions = sum(count for key, count in counts.items() if key.kind == REGION)
    files = {key.path for key in counts}
    return (
        f"{RULE}: {verb} {sum(counts.values()) - regions} findings and {regions} regions in {len(counts)} rows "
        f"of {len(files)} files, configuration {configuration.name} on {configuration.tier}, from {objects} "
        f"objects"
    )


# ── The run and the write ──────────────────────────────────────────


SPEC = counted.CountedRule(RULE, findings, load_rows, evaluate, summary, ADMITTED_KINDS, REFUSAL)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context (THE VERDICT).

    Raises:
        NotApplicable: If the rule does not apply to the build, or the ledger does not hold its configuration
        BuildError: If the build cannot be read
    """
    return counted.judge(SPEC, rule, context)


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge its counts against the ledger, and write the findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return counted.run(SPEC, rule, context, warnings_dir)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (WRITE THE LEDGER), or import the rows of a log.

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  SKIPPED when the rule does not apply or the ledger does not hold the configuration
    """
    return counted.write(SPEC, rule, request)
