"""quarantine — the count of quarantine findings and opt-out regions in each directory and kind can only fall.

THE FINDINGS
    The rule reads the finding lines of the rule quarantine in the sections
    of the objects of the target all of one build.  The runner reads them
    (assay.guard.engines.plugin.runner, THE SECTIONS).  The same line from
    two objects counts one time, for example a finding of a header that two
    units include.

THE KEY
    The ledger counts the findings of each directory and kind.  The directory
    of a file is its parent directory with '/' at its end, as the ledger names
    files (directory_of()).  include/fixy/os/Time.h has the directory
    include/fixy/os/, and a file of the build directory has @build/ or a
    directory under it.  A file at the root has the directory ./.  A row
    holds no entity and no line, so a change that only moves code in a
    directory keeps each row.  A kind such as replace-pending:std::bit_cast
    holds a name, and that name is part of the key.

    The rule cannot see a change that replaces a finding with a different
    finding of the same kind in one directory.  The owner accepts that loss.
    `guard list --build-dir DIR --path DIRECTORY/` prints each finding of a
    directory with its file, its line and its entity, for a person who
    removes the findings of a directory.

THE OPT-OUT REGIONS
    A finding in an opt-out region has the kind opted-out (CLAUDE.md section
    XXII, R7).  A line of the kind region gives the file, the first line and
    the reason of each region of a unit.  The plugin refuses a region that
    opens in a different region, and a region that a different file closes.
    The region of an opted-out finding is then the region of its file with
    the last first line at or before its line.

    Each region is a reviewed exception, so its rows keep its reason as the
    entity of the key.  A row of the kind region counts the regions of a
    directory with one reason.  A row of the kind opted-out counts the
    findings that those regions cover.  A region that covers more increases a
    count of opted-out, and a new region increases a count of region.  The
    rule refuses each one until `guard write --admit` writes it.  The rule
    counts only the regions that a build compiles.  A region in a file or a
    preprocessor arm that no build of the landing gate compiles has no row.

THE LEDGER
    utils/ledgers/quarantine.txt holds the count of each directory and kind,
    in the rows of assay.core.ledger (the policy count).  `guard write`
    writes each row through assay.core.ledger.  The rows are sorted and each
    row holds one fact.  The resolution of a merge conflict is then a new
    write.  The rows after the head comment:

        format 1
        rule quarantine
        reason PREFIX | REASON                 The reason of each count row of
                                               a directory under PREFIX
        DIRECTORY | KIND | COUNT | - | -       The count of the key
        DIRECTORY | KIND | COUNT | REASON | -  The count of a key of the kind
                                               region or opted-out

    One set of rows judges each build.  The count of a key is its largest
    count over the configurations of the landing gate (THE REDUCE of
    assay.core.ledger).  The run and the write are those of
    assay.guard.engines.plugin.counted, which the rule heavy-include also
    uses.

THE VERDICT
    * A count more than its ledger count is an error.  A change can only
      lower a count, as rule R11 of misc/01_10_2026_quarantine.md says.  A new
      finding cannot stay when a finding of a different kind or of a
      different directory goes.
    * A directory that the ledger does not hold and that has a finding is an
      error: a new directory has no finding (CLAUDE.md section XXII).
    * A count less than its ledger count gives no finding, because a ledger
      fails only on a growth (THE POLICIES of assay.core.ledger).  The landing
      gate writes the lower count (THE REDUCE of assay.loop.gate).
    * An opted-out finding of a file with no region line is an error, because
      the rule cannot give it its region.
    * A count at its row gives no output.  `guard list --build-dir DIR --path
      DIRECTORY/` prints the findings and the regions that stay.
    * Each problem of the census of the runner is an error.

WRITE THE LEDGER
    `guard write --rule quarantine --build-dir DIR` writes the counts of one
    build.  It lowers no row, because one build cannot see the findings of a
    different build (THE WRITE of assay.core.ledger).  Only the landing gate
    lowers a row (THE REDUCE of assay.core.ledger).  The write gives the
    ledger its head comment.  It refuses a count more than its row: remove
    the new finding.
    * --raise writes such a count too.  Use it only when the plugin or the
      rule table changed what the plugin reports, or when a change moves a
      finding to a different directory.  Give the reason in the commit.
    * --admit writes a count of opted-out or of region more than its row, and
      no other count.  Use it only for a region of a reason class of CLAUDE.md
      section XXII.  Such a region must cover more, or it is a new region.
      Give the reason of the region, with its class, in the message of the
      commit.
    * --reason TEXT gives the reason cell of each new row.
    * The write reads no git history.  A change that moves a finding to a
      different directory raises the count of that directory: write it with
      --raise.  The landing gate then lowers the row of the directory that the
      finding left.

NOT APPLICABLE
    The rule exits 77 when the runner does not apply to the build (THE STATUS
    of the runner).

Usage
    guard check --rule quarantine --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]
    guard write --rule quarantine --build-dir DIR [--raise | --admit] [--reason TEXT]
    guard list --build-dir DIR [--path FILE|DIRECTORY|DIRECTORY/] [--kind KIND]

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
# The directory of a file at the root of the tree (THE KEY).
ROOT_DIRECTORY = "./"
# The highest character, which sorts after each reason of a region.
LAST_CHARACTER = chr(0x10FFFF)
# The advice of a write that refuses a count above its row.
REFUSAL = (
    "Remove the new findings.  Give --admit only for a region that must cover more or a new region, and --raise only "
    "when the plugin or the rule table changed what it reports, or when the change moves a finding to a different "
    "directory"
)
# The head comment that each write gives the ledger (assay.core.ledger, THE HEAD COMMENT).
LEDGER_COMMENT = (
    "# utils/ledgers/quarantine.txt — the findings of the quarantine plugin and the opt-out regions in each\n"
    "# directory and kind.\n"
    "#\n"
    "# The rule quarantine reads this ledger (`python3 utils/bin/guard explain quarantine`).  Its module\n"
    "# utils/assay/guard/engines/plugin/quarantine.py writes each count row and this head through\n"
    "# utils/assay/core/ledger.py.  Do not edit a count row by hand.  The rule reads the section\n"
    "# .crucible.quarantine of each object of the target all, and it counts each finding line one time.\n"
    "#\n"
    "# A count above its row fails the rule, because a change can only lower a count (rule R11 of\n"
    "# misc/01_10_2026_quarantine.md).  A directory that the ledger does not hold and that has a finding\n"
    "# fails too: a new directory has no finding.  A count below its row passes.  A row holds no entity,\n"
    "# so a change that replaces a finding with a different finding of the same kind in one directory\n"
    "# passes.  A move of code to a different directory needs `guard write --raise` with the reason in\n"
    "# the commit.\n"
    "#     python3 utils/bin/guard list --build-dir build --path DIRECTORY/\n"
    "# prints each finding of a directory with its file, its line and its entity.\n"
    "# On a merge conflict in this file, take the version of main, and write each raise of the change again.\n"
    "#\n"
    "# THE ROWS (the rows grammar of utils/assay/core/rows.py)\n"
    "#   reason PREFIX | REASON         The reason of each count row of a directory under PREFIX: the\n"
    "#                                  step of the plan that removes its findings.  A person writes these\n"
    "#                                  rows, and a write removes a row that holds no count row (THE\n"
    "#                                  REASON OF A PREFIX of utils/assay/core/ledger.py).\n"
    "#   DIRECTORY | KIND | COUNT | - | -\n"
    "#                                  The number of findings of the kind KIND in the files of the\n"
    "#                                  directory, with no file of a directory under it.  A directory has\n"
    "#                                  '/' at its end, and the build directory has the prefix @build/.\n"
    "#                                  The head comment of utils/tools/quarantine/plugin.h gives the\n"
    "#                                  kinds.\n"
    "#\n"
    "# THE OPT-OUT REGIONS\n"
    "#   DIRECTORY | region | COUNT | REASON | -\n"
    "#   DIRECTORY | opted-out | COUNT | REASON | -\n"
    "#   Each region is a reviewed exception, so its rows keep its reason.  A row of the kind region\n"
    "#   counts the regions of the directory with the reason REASON.  A row of the kind opted-out counts\n"
    "#   the findings that those regions cover.  A region that covers more, and a new region, fail the\n"
    "#   rule until\n"
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

# The directory, the kind and the entity of one count: the key of a row of the ledger (THE KEY).
Key = rule_ledger.Key


# ── The keys ───────────────────────────────────────────────────────


def directory_of(path: str) -> str:
    """Return the directory of a file of the ledger: its parent directory with '/' at its end (THE KEY).

    A file at the root has the directory ROOT_DIRECTORY.
    """
    parent, slash, _ = path.rpartition("/")
    return f"{parent}/" if slash else ROOT_DIRECTORY


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

    The path of a finding is the directory of its file, and its entity is the reason of its region for the kinds
    region and opted-out (THE KEY).  The message keeps the file, the line and the entity of the line.  A problem has
    no kind.  An opted-out finding with no region gives a problem and no count.

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
                check_report.Finding("error", directory_of(item.file), item.line, RULE, item.text(), item.kind)
            )
        elif item in reasons:
            found.append(
                check_report.Finding(
                    "error", directory_of(item.file), item.line, RULE, item.text(), OPTED_OUT, reasons[item]
                )
            )
    for file, items in sorted(census.regions.items()):
        for line, reason in items:
            found.append(
                check_report.Finding(
                    "error", directory_of(file), line, RULE, f"an opt-out region of {file}: {reason}", REGION, reason
                )
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
        directory, kind, reason = key.path, key.kind, key.entity
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured <= admitted:
            continue
        listing = f"`python3 utils/bin/guard list --build-dir {build_shown} --path {directory} --kind {kind}`"
        if kind == OPTED_OUT:
            message = (
                f"{directory} has {measured} findings in opt-out regions with the reason {reason!r}, and the ledger "
                f"admits {admitted}.  A region can only shrink.  List them with {listing}, and move the code out of "
                f"the region.  If the reason class of the region (CLAUDE.md section XXII) makes the code necessary, "
                f"write the ledger with `{command} --admit` and give the reason in the commit"
            )
        elif kind == REGION:
            message = (
                f"{directory} has {measured} opt-out regions with the reason {reason!r}, and the ledger admits "
                f"{admitted}.  The number of regions can only fall: use a type of fixy or foundation, and remove the "
                f"region.  If the reason class of the region (CLAUDE.md section XXII) makes the region necessary, "
                f"write the ledger with `{command} --admit` and give the reason in the commit"
            )
        elif directory not in held:
            message = (
                f"{directory} has {measured} findings of the kind {kind}, and the ledger holds no row of the "
                f"directory.  A new directory has no finding (CLAUDE.md section XXII).  List them with {listing}, and "
                f"use a type of fixy or foundation.  If the change moves code to this directory, write the ledger "
                f"with `{command} --raise`, and give the reason in the commit"
            )
        else:
            message = (
                f"{directory} has {measured} findings of the kind {kind}, and the ledger admits {admitted}.  The "
                f"change adds {measured - admitted}, and a change can only lower a count.  List them with {listing}, "
                f"and use a type of fixy or foundation"
            )
        findings_found.append(check_report.Finding("error", shown, ledger.line_of(key), RULE, message))
    return findings_found


def summary(counts: Mapping[Key, int], objects: int, verb: str) -> str:
    """Return the line that tells the findings, the regions, the rows and the directories of one run or one write."""
    regions = sum(count for key, count in counts.items() if key.kind == REGION)
    directories = {key.path for key in counts}
    return (
        f"{RULE}: {verb} {sum(counts.values()) - regions} findings and {regions} regions in {len(counts)} rows "
        f"of {len(directories)} directories, from {objects} objects"
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
