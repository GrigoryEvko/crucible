"""library-bytes — the bytes of library code in each static library of the root stay near the baseline of the build.

THE MEASURE
    The rule reads the code bodies of each object of each static library of
    the root (assay.readers.dwarf, THE CODE BODIES).  The declaring file of a
    body gives its class (CLASSES):
      `ours`        a relative path, or a file in the source root or the build
                    directory
      `libstdc++`   a file in an include/c++ directory of the toolchain
      `compiler`    a different file of the toolchain, such as a header of GCC
      `system`      a different absolute path, such as a header of the C
                    library
      `generated`   no file, for a body that the compiler makes, such as the
                    handler of a contract violation (DW_AT_artificial)
    The toolchain is the prefix of the compiler of the build
    (assay.guard.engines.elf.runner.toolchain_prefix).  A body with no file
    that the compiler does not make is an error of the kind no-file.  The
    library bytes of an archive are the exclusive bytes of its bodies of the
    classes libstdc++, compiler and system (LIBRARY_CLASSES).

THE COMDAT GROUPS
    A body in a section of a COMDAT group counts only in the first member of
    the archive with a group of that signature.  A link keeps that copy.  A
    body of each other section counts in each member.

THE LEDGER
    utils/ledgers/library-bytes.txt has the policy baseline, in format 1 of
    assay.core.ledger:
        configuration KIND kind KIND
        configuration KIND tier TIER
        @build/ARCHIVE | KIND | BYTES | - | REASON
    A row gives the library bytes of one archive in the configuration KIND at
    the last write, with its reason.  The configuration of a build is its
    build kind and its tier (assay.guard.engines.elf.configured).

THE VERDICT
    The measured rule of the registry gives the two thresholds in percent of
    a row.  THE POLICY of assay.core.configuration gives the rows that judge
    the build:
      growth       library bytes more than the row by more than the warning
                   threshold give a warning, and by more than the error
                   threshold an error.  A row of 0 bytes and an archive with
                   library bytes give an error
      fall         library bytes less than the row by more than the warning
                   threshold give an error.  The baseline then does not stay
                   more than the measure
      new-archive  an archive with no row
      no-archive   a row of an archive that the build does not make
      no-file      a body with no declaring file that the compiler does not
                   make
      ledger-row   a row or a configuration that does not have the shape of
                   THE LEDGER
    A build of a configuration that the ledger does not hold gets the largest
    row of each archive.  It then gives no fall and no no-archive.  A finding
    of growth or fall names the largest library bodies of the archive.

THE WRITE
    `guard write --rule library-bytes --build-dir DIR` writes the
    configuration rows and the rows of the configuration of the build again,
    and it keeps the rows of the other configurations.  The write refuses a
    growth of more than the warning threshold with no --reason TEXT.  A new
    row with no --reason gets DEFAULT_REASON, and each other row keeps its
    reason.  The mode admit does not apply.

NOT APPLICABLE
    The rule does not apply (exit 77) to these builds:
      * A build with no build-kind.txt
      * A build that only parses its units
        (assay.core.configuration.is_parse_build)
      * A build that has not made each static library of the root.

Complexity: linear in the debug sections of the archives, and one run of c++filt for the names of a finding.
"""

import posixpath
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import configured, runner
from assay.readers import census as build_census
from assay.readers import dwarf, elf

RULE = "library-bytes"
GROUP_PREFIX = "@build/"
OURS = "ours"
LIBSTDCXX = "libstdc++"
COMPILER = "compiler"
SYSTEM = "system"
GENERATED = "generated"
CLASSES = (OURS, LIBSTDCXX, COMPILER, SYSTEM, GENERATED)
LIBRARY_CLASSES = (LIBSTDCXX, COMPILER, SYSTEM)
# The directory of the headers of libstdc++ in the toolchain.
LIBSTDCXX_PART = "/include/c++/"
DEFAULT_REASON = "the measure at the last write of the ledger"
# The number of library bodies that a finding of growth or fall names.
NAMED = 6
GROWTH, FALL, NEW_ARCHIVE, NO_ARCHIVE, NO_FILE, LEDGER_ROW = (
    "growth",
    "fall",
    "new-archive",
    "no-archive",
    "no-file",
    "ledger-row",
)
ROW_SHAPE = (
    "a row is '@build/ARCHIVE | KIND | BYTES | - | REASON', with the name of a configuration of the ledger, a whole "
    "number and a reason"
)


@dataclass(frozen=True, slots=True)
class Places:
    """The directories that give the class of a declaring file (THE MEASURE): ours, and the toolchain."""

    ours: tuple[str, ...]
    toolchain: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Measure:
    """The bytes of one archive by the class of each body, and the library bytes of each library body by its name.

    `nameless` holds the member and the bytes of each body with no declaring file that the compiler does not make.
    """

    archive: str
    classes: Mapping[str, int]
    bodies: Mapping[str, int]
    nameless: tuple[tuple[str, int], ...]

    @property
    def library(self) -> int:
        """The library bytes of the archive (THE MEASURE)."""
        return sum(self.classes.get(name, 0) for name in LIBRARY_CLASSES)


@dataclass(frozen=True, slots=True)
class Baseline:
    """The rows of one configuration: the bytes, the line and the reason of each archive."""

    rows: Mapping[str, tuple[int, int, str]]


# ── The measure ────────────────────────────────────────────────────


def under(path: str, directories: Sequence[str]) -> str:
    """Return the first directory that holds a path, or an empty text."""
    return next((item for item in directories if path == item or path.startswith(f"{item}/")), "")


def class_of(file: str, is_artificial: bool, places: Places) -> str | None:
    """Return the class of the declaring file of a body, or None for a body with no file that the compiler does not make.

    THE MEASURE gives the classes.
    """
    if not file:
        return GENERATED if is_artificial else None
    if not posixpath.isabs(file):
        return OURS
    path = posixpath.normpath(file)
    if under(path, places.ours):
        return OURS
    prefix = under(path, places.toolchain)
    if prefix:
        return LIBSTDCXX if LIBSTDCXX_PART in path[len(prefix) :] else COMPILER
    return SYSTEM


def places_of(context: runner.ElfContext) -> Places:
    """Return the directories of the tree and the toolchain of the build, each also with its links resolved.

    Raises:
        runner.NotApplicable: If the call gives no build directory and no plant
        runner.BuildError: If the CMake cache cannot be read or names no compiler
    """
    roots = [Path(context.root)] + ([context.build_dir] if context.build_dir is not None else [])
    prefix = runner.toolchain_prefix(context.compiler())
    ours = sorted({text for item in roots for text in (str(item), str(item.resolve()))}, key=len, reverse=True)
    toolchain = sorted({str(prefix), str(prefix.resolve())}, key=len, reverse=True)
    return Places(tuple(ours), tuple(toolchain))


def comdat_signatures(member: elf.Member) -> dict[int, str]:
    """Return the signature of the COMDAT group of each section of a member that has one.

    Raises:
        elf.ElfError: If a group section is damaged
    """
    return {section: group.signature for group in member.groups() if group.is_comdat for section in group.sections}


def read_measure(path: Path, places: Places) -> Measure:
    """Read the code bodies of each member of an archive, and give its measure (THE MEASURE, THE COMDAT GROUPS).

    Complexity: linear in the debug sections of the members.

    Raises:
        runner.BuildError: If the archive, a member or its debug sections cannot be read
    """
    classes: Counter[str] = Counter()
    bodies: Counter[str] = Counter()
    nameless: list[tuple[str, int]] = []
    seen: set[str] = set()
    try:
        for member in elf.members(path):
            signatures = comdat_signatures(member)
            fresh: set[str] = set()
            for body in dwarf.code_bodies(member):
                signature = signatures.get(body.section, "")
                if signature in seen:
                    continue
                if signature:
                    fresh.add(signature)
                kind = class_of(body.file, body.is_artificial, places)
                if kind is None:
                    nameless.append((f"{member.name}: {body.name or '?'}", body.exclusive))
                    continue
                classes[kind] += body.exclusive
                if kind in LIBRARY_CLASSES and body.exclusive > 0:
                    bodies[body.name] += body.exclusive
            seen |= fresh
    except (OSError, elf.ElfError, dwarf.DwarfError) as problem:
        raise runner.BuildError(f"the DWARF of {path} cannot be read: {problem}") from None
    return Measure(path.name, dict(classes), dict(bodies), tuple(nameless))


def read_measures(context: runner.ElfContext) -> list[Measure]:
    """Read the measure of each static library of the root, in the sequence of the archive names.

    Raises:
        runner.NotApplicable: If the build has not made each library
        runner.BuildError: If the root makes no static library, or an archive cannot be read
    """
    libraries = context.libraries()
    if not libraries:
        raise runner.BuildError("the root makes no static library, and the rule then reads nothing")
    unbuilt = sorted(build_census.shown(path, context.root) for path in libraries.values() if not path.is_file())
    if unbuilt:
        raise runner.NotApplicable(f"the build has not made {', '.join(unbuilt)}.  Build the target all")
    places = places_of(context)
    return [read_measure(path, places) for path in sorted(libraries.values(), key=lambda item: item.name)]


# ── The ledger ─────────────────────────────────────────────────────


def is_row(key: rule_ledger.Key, entry: rule_ledger.Entry) -> bool:
    """Return True for a base row with the shape of THE LEDGER, apart from the name of its configuration."""
    is_whole = isinstance(entry.value, int) or (isinstance(entry.value, float) and entry.value.is_integer())
    return key.path.startswith(GROUP_PREFIX) and bool(key.kind) and not key.entity and is_whole and bool(entry.reason)


def row_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Return an error for each configuration and each row that does not have the shape of THE LEDGER."""
    names = {item.name for item in ledger.configurations}
    problems = configured.ledger_problems(
        RULE, ledger, lambda key, entry: is_row(key, entry) and key.kind in names, ROW_SHAPE, lambda _key: False
    )
    return [replace(item, kind=LEDGER_ROW, entity=RULE) for item in problems]


def baselines(ledger: rule_ledger.Ledger) -> dict[str, Baseline]:
    """Return the rows of each configuration of the ledger, by its name.  Complexity: linear in the rows."""
    found: dict[str, dict[str, tuple[int, int, str]]] = {item.name: {} for item in ledger.configurations}
    for key, entry in ledger.base.items():
        if key.kind in found and is_row(key, entry):
            found[key.kind][key.path.removeprefix(GROUP_PREFIX)] = (int(entry.value), entry.line, entry.reason)
    return {name: Baseline(rows) for name, rows in found.items()}


def judging_rows(judged: build_config.Judging, held: Mapping[str, Baseline]) -> Baseline:
    """Return the rows that judge the build: of its configuration, else the largest row of each archive (THE VERDICT)."""
    if judged.name is not None and judged.name in held:
        return held[judged.name]
    largest: dict[str, tuple[int, int, str]] = {}
    for baseline in held.values():
        for archive, row in baseline.rows.items():
            if archive not in largest or row[0] > largest[archive][0]:
                largest[archive] = row
    return Baseline(largest)


# ── The verdict ────────────────────────────────────────────────────


def named_bodies(measure: Measure) -> str:
    """Return the largest library bodies of an archive, with their demangled names, as words of a finding.

    Raises:
        runner.BuildError: If c++filt does not demangle the names
    """
    largest = sorted(measure.bodies.items(), key=lambda item: (-item[1], item[0]))[:NAMED]
    try:
        names = elf.demangle([name for name, _ in largest])
    except elf.ElfError as problem:
        raise runner.BuildError(str(problem)) from None
    listed = "; ".join(f"{name} {size} bytes" for name, (_, size) in zip(names, largest, strict=True))
    return f"The largest library bodies: {listed}" if listed else "The archive has no library body"


def change_finding(
    measure: Measure, row: tuple[int, int, str], budget: check_report.Budget, is_bound: bool, command: str
) -> check_report.Finding | None:
    """Return the finding of growth or fall of one archive against its row, or None (THE VERDICT).

    Raises:
        runner.BuildError: If c++filt does not demangle the names of a finding
    """
    value, (baseline, line, _) = measure.library, row
    path = rule_ledger.shown_path(RULE)
    said = f"{measure.archive} has {value} bytes of library code"
    if baseline <= 0:
        if value <= 0:
            return None
        message = f"{said}, and its row holds 0.  Remove the library code, or write the row: {command} --reason TEXT"
        return check_report.Finding("error", path, line, RULE, message, GROWTH, measure.archive)
    change = (value - baseline) / baseline * 100.0
    level = check_report.classify(change, budget)
    if level is not None:
        limit = budget.error if level == "error" else budget.warn
        message = (
            f"{said}, {change:.2f}% more than its row of {baseline}, over the {level} threshold {limit:g}%.  Remove "
            f"the library code, or raise the row in the same commit with a reason: {command} --reason TEXT"
        )
        kind = GROWTH
    elif -change > budget.warn and not is_bound:
        level = "error"
        message = (
            f"{said}, {-change:.2f}% less than its row of {baseline}, more than the {budget.warn:g}% that a row can "
            f"hold.  Lower the row in the same commit: {command}"
        )
        kind = FALL
    else:
        return None
    return check_report.Finding(level, path, line, RULE, f"{message}.  {named_bodies(measure)}", kind, measure.archive)


def verdict(
    measures: Sequence[Measure],
    ledger: rule_ledger.Ledger,
    judged: build_config.Judging,
    budget: check_report.Budget,
    command: str,
) -> list[check_report.Finding]:
    """Return the findings of the measures against the rows that judge the build (THE VERDICT).

    Raises:
        runner.BuildError: If c++filt does not demangle the names of a finding
    """
    path = rule_ledger.shown_path(RULE)
    is_bound = judged.name == rule_ledger.MAXIMUM
    rows = judging_rows(judged, baselines(ledger)).rows
    found: list[check_report.Finding] = []
    for measure in measures:
        for where, size in measure.nameless:
            message = (
                f"{measure.archive}: the body {where} of {size} bytes has no declaring file, and the compiler did not "
                f"make it.  The rule cannot give it a class"
            )
            found.append(check_report.Finding("error", path, 0, RULE, message, NO_FILE, measure.archive))
        row = rows.get(measure.archive)
        if row is None:
            message = (
                f"{measure.archive} has {measure.library} bytes of library code and no row.  Write its row: {command}"
            )
            found.append(check_report.Finding("error", path, 0, RULE, message, NEW_ARCHIVE, measure.archive))
            continue
        change = change_finding(measure, row, budget, is_bound, command)
        if change is not None:
            found.append(change)
    made = {measure.archive for measure in measures}
    if not is_bound:
        for archive, (_, line, _) in sorted(rows.items()):
            if archive not in made:
                message = f"the build makes no archive {archive}, and the ledger has its row.  Write the rows again"
                found.append(
                    check_report.Finding("error", path, line, RULE, f"{message}: {command}", NO_ARCHIVE, archive)
                )
    return found


def budget_of(rule: rule_registry.Rule) -> check_report.Budget:
    """Return the thresholds of the measured rule.

    Raises:
        runner.BuildError: If the registry gives the rule no thresholds
    """
    if rule.threshold is None:
        raise runner.BuildError(f"the registry gives the rule {rule.id} no thresholds, and the rule reads them there")
    threshold = rule.threshold
    return check_report.Budget(
        rule.id, threshold.warn, threshold.error, check_report.unit_text(threshold.unit), rule.refuses
    )


def write_command(context: runner.ElfContext, is_bound: bool) -> str:
    """Return the command that writes the rows of the configuration of the build."""
    words = " --add-configuration" if is_bound else ""
    return f"python3 utils/bin/guard write --rule {RULE} --build-dir {context.build_shown()}{words}"


@dataclass(frozen=True, slots=True)
class Reading:
    """The measures of one build, its configuration, the ledger and the rows that judge the build."""

    measures: list[Measure]
    configuration: build_config.BuildConfiguration
    ledger: rule_ledger.Ledger
    judged: build_config.Judging


def read_build(rule: rule_registry.Rule, context: runner.ElfContext, ledger: rule_ledger.Ledger) -> Reading:
    """Read the measures and the configuration of the build, and the rows that judge it.

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    context.kind()
    if context.build_dir is not None and build_config.is_parse_build(context.build_dir):
        raise runner.NotApplicable("the build only parses its units, and it makes no static library")
    measures = read_measures(context)
    configuration = configured.configuration_of(context)
    return Reading(measures, configuration, ledger, build_config.judging(rule.id, ledger, configuration))


def summary_of(reading: Reading, context: runner.ElfContext) -> str:
    """Return the summary line of a run: the library bytes of each archive and the rows that judge the build."""
    total: Counter[str] = Counter()
    for measure in reading.measures:
        total.update(measure.classes)
    classes = ", ".join(f"{name} {total.get(name, 0)}" for name in CLASSES)
    archives = ", ".join(f"{measure.archive} {measure.library}" for measure in reading.measures)
    return (
        f"{RULE}: {len(reading.measures)} archives, {sum(item.library for item in reading.measures)} bytes of library "
        f"code of {reading.configuration.name} on {context.tier()} ({classes}): {archives}.  Judged against "
        f"{reading.judged.words}"
    )


def examine(rule: rule_registry.Rule, context: runner.ElfContext) -> configured.Inspection:
    """Read the archives of the build, and judge their library bytes against the ledger (THE VERDICT).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    context.kind()
    ledger = configured.read_ledger(rule, context.root)
    if isinstance(ledger, configured.Inspection):
        return ledger
    reading = read_build(rule, context, ledger)
    problems = row_problems(ledger)
    is_bound = reading.judged.name == rule_ledger.MAXIMUM
    found = verdict(reading.measures, ledger, reading.judged, budget_of(rule), write_command(context, is_bound))
    return configured.Inspection(
        (*problems, *found), ledger, reading.configuration, reading.judged, summary=summary_of(reading, context)
    )


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the findings of the archives of the build, judged against the ledger in the tree of the context."""
    return list(examine(rule_ledger.rule_of(RULE), context).findings)


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger in the tree of the context (THE VERDICT).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build
        runner.BuildError: If the build cannot be read
    """
    return list(examine(rule, context).findings)


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, and write its findings and its summary (assay.guard.engines.elf.configured).

    A row of a baseline is a measure and no count.  The run then prints no row for an import.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return configured.run_inspection(RULE, lambda: examine(rule, context), warnings_dir, prints_rows=False)


# ── The write ──────────────────────────────────────────────────────


def written_ledger(
    rule: rule_registry.Rule, request: runner.WriteRequest, reading: Reading
) -> rule_ledger.Ledger | str:
    """Return the ledger with the rows of the configuration of the build, or the line of a refusal (THE WRITE)."""
    ledger, configuration = reading.ledger, reading.configuration
    problems = row_problems(ledger)
    if problems:
        return f"the ledger of {RULE} is not written: {problems[0].message}"
    target = build_config.write_target(RULE, ledger, configuration, request.add_configuration)
    if isinstance(target, str):
        return target
    configurations = target.ledger.configurations or (
        rule_ledger.Configuration(configuration.name, configuration.facts),
    )
    old = baselines(ledger).get(configuration.name, Baseline({})).rows
    warn = budget_of(rule).warn
    changes = {
        measure.archive: (measure.library - old[measure.archive][0]) / old[measure.archive][0] * 100.0
        for measure in reading.measures
        if measure.archive in old and old[measure.archive][0] > 0
    }
    grown = [f"{archive} grows {change:.2f}%" for archive, change in sorted(changes.items()) if change > warn]
    if grown and not request.reason:
        return (
            f"the ledger of {RULE} is not written: {'; '.join(grown)}, more than the warning threshold {warn:g}%.  "
            f"Give the reason of the growth with --reason TEXT"
        )
    kept = {key: entry for key, entry in ledger.base.items() if key.kind != configuration.name}
    for measure in reading.measures:
        reason = request.reason or (old[measure.archive][2] if measure.archive in old else DEFAULT_REASON)
        key = rule_ledger.Key(f"{GROUP_PREFIX}{measure.archive}", configuration.name, "")
        kept[key] = rule_ledger.Entry(key, measure.library, reason)
    return replace(ledger, configurations=configurations, base=kept)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the configuration rows and the rows of the configuration of the build (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when the write refuses the rows or the build cannot be read.  2 for the mode
        admit and for an import.  SKIPPED when the rule does not apply
    """
    if request.mode == rule_ledger.WriteMode.ADMIT or request.import_log is not None:
        check_report.write_line(
            f"guard write: a row of {RULE} is a measure of a build, so the mode admit and an import do not apply",
            is_error=True,
        )
        return configured.ADMIT_STATUS
    context = runner.ElfContext(request.root, request.build_dir)
    try:
        ledger = rule_ledger.read(rule, request.root)
        reading = read_build(rule, context, ledger)
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {RULE} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except (runner.BuildError, OSError, rows_grammar.RowsError) as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    written = written_ledger(rule, request, reading)
    if isinstance(written, str):
        check_report.write_line(f"guard write: {written}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"guard write: wrote {len(reading.measures)} rows of {reading.configuration.name} on "
        f"{reading.configuration.tier} to {rule_ledger.shown_path(RULE)}"
    )
    return 0
