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
    file or a preprocessor arm that no build of the landing gate compiles has
    no row.

THE ENTITY
    The ledger counts the findings of each file, kind and entity.  The entity
    of a row is the part of the entity of a finding that names what the
    plugin refuses (entity_key).  The reader gives the parts of each entity
    (assay.readers.sections, THE ENTITIES OF THE RECORDS), and each part has
    one space between each two words:
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
        reason PREFIX | REASON                 The reason of each count row of
                                               a file under PREFIX
        FILE | KIND | COUNT | ENTITY | -       The count of the key.  A kind
                                               with no entity has the cell "-"

    One set of rows judges each build.  The count of a key is its largest
    count over the configurations of the landing gate (THE REDUCE of
    assay.core.ledger).  The run and the write are those of
    assay.guard.engines.plugin.counted, which the rule heavy-include also
    uses.

THE VERDICT
    * A count more than its ledger count is an error.  A change can only
      lower a count, as rule R11 of misc/01_10_2026_quarantine.md says.  A new
      finding cannot stay when a finding of a different entity or of a
      different file goes.
    * A file that the ledger does not hold and that has a finding is an error:
      a new file has no finding (CLAUDE.md section XXII).
    * A count less than its ledger count gives no finding, because a ledger
      fails only on a growth (THE POLICIES of assay.core.ledger).  The landing
      gate writes the lower count (THE REDUCE of assay.loop.gate).
    * An opted-out finding of a file with no region line is an error, because
      the rule cannot give it its region.
    * A count at its row gives no output.  `guard list --build-dir DIR --path
      DIRECTORY` prints the findings and the regions that stay.
    * Each problem of the census of the runner is an error.

WRITE THE LEDGER
    `guard write --rule quarantine --build-dir DIR` writes the counts of one
    build.  It lowers no row, because one build cannot see the findings of a
    different build (THE WRITE of assay.core.ledger).  Only the landing gate
    lowers a row (THE REDUCE of assay.core.ledger).  The write gives the
    ledger its head comment.  It refuses a count more than its row: remove
    the new finding.
    * --raise writes such a count too.  Use it only when the plugin or the
      rule table changed what the plugin reports.  Give the reason in the
      commit.
    * --admit writes a count of opted-out or of region more than its row, and
      no other count.  Use it only for a region of a reason class of CLAUDE.md
      section XXII.  Such a region must cover more, or it is a new region.
      Give the reason of the region, with its class, in the message of the
      commit.
    * --reason TEXT gives the reason cell of each new row.
    * The write reads no git history.  A change that moves a finding to a
      file with no row raises the count of that file: write it with --raise,
      and give the reason in the commit.  The landing gate then lowers the row
      of the file that the finding left.

NOT APPLICABLE
    The rule exits 77 when the runner does not apply to the build (THE STATUS
    of the runner).

Usage
    guard check --rule quarantine --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]
    guard write --rule quarantine --build-dir DIR [--raise | --admit] [--reason TEXT]
    guard list --build-dir DIR [--path FILE|DIRECTORY] [--kind KIND]

Complexity: linear in the finding lines of the build, and O(k log k) for k rows of the ledger and counts of the build.
"""

import bisect
from collections.abc import Mapping, Sequence
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import counted, runner
from assay.readers import sections as quarantine_sections

RULE = "quarantine"
OPTED_OUT = quarantine_sections.OPTED_OUT
REGION = quarantine_sections.REGION
# The kinds that --admit writes with a count more than their rows: a region that covers more, and a new region.
ADMITTED_KINDS = frozenset((OPTED_OUT, REGION))
# The kinds with the name of a declaration as the entity: a row has no entity (THE ENTITY).
DECLARATION_KINDS = frozenset(("raw-pointer-object", "raw-function-pointer", "c-array-object"))
# The separator between the reason of a region and the finding in it, in the entity of an opted-out row.
REASON_SEPARATOR = " | "
# The highest character, which sorts after each reason of a region.
LAST_CHARACTER = chr(0x10FFFF)
# The advice of a write that refuses a count above its row.
REFUSAL = (
    "Remove the new findings.  Give --admit only for a region that must cover more or a new region, and --raise only "
    "when the plugin or the rule table changed what it reports"
)
# The head comment that each write gives the ledger (assay.core.ledger, THE HEAD COMMENT).
LEDGER_COMMENT = (
    "# utils/ledgers/quarantine.txt — the findings of the quarantine plugin and the opt-out regions in each\n"
    "# file, kind and entity.\n"
    "#\n"
    "# The rule quarantine reads this ledger (`python3 utils/bin/guard explain quarantine`).  Its module\n"
    "# utils/assay/guard/engines/plugin/quarantine.py writes each count row and this head through\n"
    "# utils/assay/core/ledger.py.  Do not edit a count row by hand.  The rule reads the section\n"
    "# .crucible.quarantine of each object of the target all, and it counts each finding line one time.\n"
    "#\n"
    "# A count above its row fails the rule, because a change can only lower a count (rule R11 of\n"
    "# misc/01_10_2026_quarantine.md).  So a change that removes std::vector from a file and adds std::map\n"
    "# to it fails.  A file that the ledger does not hold and that has a finding fails too: a new file has\n"
    "# no finding.  A count below its row passes.\n"
    "# On a merge conflict in this file, take the version of main, and write each raise of the change again.\n"
    "#\n"
    "# THE ROWS (the rows grammar of utils/assay/core/rows.py)\n"
    "#   reason PREFIX | REASON         The reason of each count row of a file under PREFIX: the step of\n"
    "#                                  the plan that removes its findings.  A person writes these rows,\n"
    "#                                  and a write removes a row that holds no count row (THE REASON OF\n"
    "#                                  A PREFIX of utils/assay/core/ledger.py).\n"
    "#   FILE | KIND | COUNT | ENTITY | -\n"
    "#                                  The number of findings of the kind KIND and the entity ENTITY in\n"
    "#                                  the file.  A file of the build directory has the prefix @build/.\n"
    "#                                  The head comment of utils/tools/quarantine/plugin.h gives the\n"
    "#                                  kinds, and THE ENTITY of\n"
    "#                                  utils/assay/guard/engines/plugin/quarantine.py gives the entity of\n"
    "#                                  each kind.  The entity holds no line, so a move of code keeps each\n"
    '#                                  row.  A kind with no entity has the cell "-".\n'
    "#\n"
    "# THE OPT-OUT REGIONS\n"
    '#   A finding in an opt-out region has the kind opted-out, and its entity is "REASON | KIND\n'
    '#   ENTITY": the reason of the region, and the kind and the entity of the finding.  A cell writes\n'
    "#   the bar of that entity as \\|.  So these rows hold what each region covers.  A row of the kind\n"
    "#   region counts the regions of the file with the reason of its entity.  A region that covers\n"
    "#   more, and a new region, fail the rule until\n"
    "#       python3 utils/bin/guard write --rule quarantine --build-dir build --admit\n"
    "#   writes the new count.  Use --admit only for a region of a reason class of CLAUDE.md section\n"
    "#   XXII, and give the reason in the commit.  Only a region that a build compiles has a row.\n"
    "#\n"
    "# ONE SET OF ROWS\n"
    "#   One set of rows judges each build.  The count of a key is its largest count over the\n"
    "#   configurations of the landing gate: each configure preset with the tier gate, the builds of\n"
    "#   objects and the two parse builds (utils/assay/loop/gate.py).  A build of a different\n"
    "#   configuration gets the same verdict, for example an aarch64 build of objects in CI.  A count\n"
    "#   above its row fails it.\n"
    "#   python3 utils/bin/guard write --rule quarantine --build-dir build\n"
    "#   writes the counts of one build.  It lowers no row, because one build cannot see the findings of\n"
    "#   a different build.  A landing of `python3 utils/bin/loop land` that builds each configuration of\n"
    "#   the gate lowers each row to that largest count, and it folds the ledger into the commit (THE\n"
    "#   REDUCE of utils/assay/loop/gate.py).\n"
    "#   The vessel units that need PyTorch compile only when TORCH_DIR is set.  No configuration of the\n"
    "#   gate sets TORCH_DIR, so the ledger does not count those units, and their findings fail a build\n"
    "#   with TORCH_DIR.\n"
    "#   No configuration turns on CRUCIBLE_SENSE_HUB_V2.  Each build compiles the SenseHub v2 sources\n"
    "#   in two object libraries that no target links, with and without CRUCIBLE_SENSE_HUB_EXTENDED\n"
    "#   (CMakeLists.txt), so these rows count each arm of those sources.  A finding of an arm that only\n"
    "#   a preset outside the gate compiles, such as the verify preset or a PGO preset, fails its build.\n"
)

# The file, the kind and the entity of one count: the key of a row of the ledger.
Key = rule_ledger.Key


# ── The keys ───────────────────────────────────────────────────────


def joined(*fields: str) -> str:
    """Return the fields with one space between them.  An empty field at the end gives no space."""
    return " ".join(fields).rstrip(" ")


def row_entity(kind: str, parsed: quarantine_sections.FindingEntity) -> str:
    """Return the entity of the row of one finding from the record of its entity (THE ENTITY)."""
    if kind in DECLARATION_KINDS:
        return ""
    if isinstance(parsed, quarantine_sections.LayerHeader):
        return parsed.header
    if isinstance(parsed, quarantine_sections.UpwardInclude):
        return parsed.target
    return parsed.text


def entity_key(kind: str, entity: str) -> str:
    """Return the entity of the row of one finding: the part of its entity that names what the plugin refuses.

    THE ENTITY in the head comment gives the rule for each kind.
    """
    return row_entity(kind, quarantine_sections.quarantine_entity(kind, entity))


def opted_out_key(reason: str, entity: str) -> str:
    """Return the entity of the row of an opted-out finding: the reason of its region, the inner kind and entity.

    `reason` is the reason of the region as the reader gives it, with one space between each two words.
    """
    parsed = quarantine_sections.opted_out(entity)
    return joined(f"{reason}{REASON_SEPARATOR}{parsed.kind}", row_entity(parsed.kind, parsed.inner))


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
        reason = reason_at(census.regions.get(item.file, ()), item.line)
        if reason is None:
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
        reasons[item] = reason
    return reasons, problems


def reason_at(starts: Sequence[tuple[int, str]], line: int) -> str | None:
    """Return the reason of the region with the last first line at or before `line`, or None with no such region.

    `starts` holds the first line and the reason of each region of one file, in sorted sequence (THE OPT-OUT
    REGIONS).  The rule fixture-quarantine gives each opted-out finding of a census its reason with it too.
    Complexity: O(log r) for r regions.
    """
    index = bisect.bisect_right(starts, (line, LAST_CHARACTER)) - 1
    return starts[index][1] if index >= 0 else None


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


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, _inputs: None, build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count, and give an error for each count above its row.

    Complexity: O(k log k) for k counts of the build and the ledger.
    """
    findings_found: list[check_report.Finding] = []
    expected = ledger.expected()
    held = {key.path for key in expected}
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    for key in sorted(set(counts) | set(expected)):
        file, kind, entity = key.path, key.kind, key.entity
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured <= admitted:
            continue
        line = ledger.line_of(key)
        listing = f"`python3 utils/bin/guard list --build-dir {build_shown} --path {file} --kind {kind}`"
        what = counted.described(kind, entity)
        if kind == OPTED_OUT:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The region "
                f"covers more, and a region can only shrink.  List them with {listing}, and move the code out "
                f"of the region.  If the reason class of the region (CLAUDE.md section XXII) makes the code "
                f"necessary, write the ledger with `{command} --admit` and give the reason in the commit"
            )
        elif kind == REGION:
            message = (
                f"{file} has {measured} opt-out regions with the reason {entity!r}, and the ledger admits "
                f"{admitted}.  The number of regions can only fall: use a type of fixy or foundation, and "
                f"remove the region.  If the reason class of the region (CLAUDE.md section XXII) makes the "
                f"region necessary, write the ledger with `{command} --admit` and give the reason in the "
                f"commit"
            )
        elif file not in held:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger holds no row of the file.  A new "
                f"file has no finding (CLAUDE.md section XXII).  List them with {listing}, and use a type of "
                f"fixy or foundation.  If the change moves code to this file, write the ledger with "
                f"`{command} --raise`, and give the reason in the commit"
            )
        else:
            message = (
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The change adds "
                f"{measured - admitted}, and a change can only lower a count, also when a finding of a "
                f"different entity goes.  List them with {listing}, and use a type of fixy or foundation"
            )
        findings_found.append(check_report.Finding("error", shown, line, RULE, message))
    return findings_found


def summary(counts: Mapping[Key, int], objects: int, verb: str) -> str:
    """Return the line that tells the findings, the regions, the rows and the files of one run or one write."""
    regions = sum(count for key, count in counts.items() if key.kind == REGION)
    files = {key.path for key in counts}
    return (
        f"{RULE}: {verb} {sum(counts.values()) - regions} findings and {regions} regions in {len(counts)} rows "
        f"of {len(files)} files, from {objects} objects"
    )


# ── The run and the write ──────────────────────────────────────────


SPEC = counted.CountedRule(
    RULE, findings, counted.no_inputs, evaluate, summary, ADMITTED_KINDS, REFUSAL, comment=LEDGER_COMMENT
)


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
    return counted.run(SPEC, rule, context, warnings_dir)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the counts of the builds of the request (WRITE THE LEDGER).

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  SKIPPED when the rule does not apply to a build
    """
    return counted.write(SPEC, rule, request)
