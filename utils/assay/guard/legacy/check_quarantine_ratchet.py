"""check-quarantine-ratchet — the count of quarantine findings in each file, kind and entity can only fall.

THE FINDINGS
    The quarantine plugin of utils/tools/quarantine/ writes the findings of
    each unit into the section .crucible.quarantine of its object
    (utils/assay/readers/sections.py).  The check reads the section of
    each object of the target all of one build (utils/assay/readers/census.py,
    objects_of_all) whose compile loads the quarantine plugin.  A finding is
    one distinct finding line.  The same line from two objects, as a finding
    of a header that two units include, counts one time.  A line of the kind
    region is no finding: the rule quarantine-regions
    (`guard check --rule quarantine-regions`) and its ledger hold each
    opt-out region.

THE OPT-OUT REGIONS
    A finding inside an opt-out region has the kind opted_out (CLAUDE.md
    section XXII, R7).  Its row holds the reason of its region, so the
    ledger holds what each region covers.  A line of the kind region gives
    the file, the first line and the reason of each region of a unit.  The
    plugin refuses a region that opens inside another region, and a region
    that another file closes.  So the region of an opted_out finding is the
    region of its file with the last first line at or before its line.  A
    region that covers more raises a count of opted_out, and the check
    refuses it until --write --admit writes it.

THE ENTITY
    The ledger counts the findings of each file, kind and entity.  The entity
    of a row is the part of the entity of a finding that names what the
    plugin refuses (entity_key):
      * std_object and c_library_object: the library class without the type
        in parentheses, for example std::vector
      * raw_pointer_object, raw_function_pointer and c_array_object: no
        entity.  Their text is a type of the code, and an alias in another
        file can change that text in a file that no change touched
      * raw_new_delete: the operator without the type of the expression
      * opted_out: "REASON | KIND ENTITY", the reason of the region, and the
        kind and the entity of the finding inside it
      * each other kind: the whole text, for example std::span::operator[],
        memcpy, or the header of an include
    The entity holds no line, so a change that only moves code keeps each
    row.  A change that removes std::vector from a file and adds std::map to
    it raises the count of std::map, and the check refuses that change.

THE LEDGER
    utils/scripts/quarantine-ledger.txt holds the count of each file, kind
    and entity with a finding.  --write generates each row.  The rows are
    sorted and each row holds one fact, so the resolution of a merge conflict
    is a new --write.  The rows after the head comment:

        format 4
        configuration NAME KEY VALUE   One row for each key of the
                                       configuration NAME
        FILE KIND COUNT ENTITY         The count of the base configuration.
                                       ENTITY is the rest of the line, and
                                       a kind with no entity has none
        NAME FILE KIND DELTA ENTITY    The count of the configuration NAME,
                                       as a difference from the base count:
                                       +N or -N

    The first configuration of the ledger is the base.  The count of another
    configuration is the base count plus its difference, so a change that
    lowers a count in each configuration writes only the base rows.
    parse_ledger also reads the rows of format 1 (a directory and a kind),
    of format 2 (a file and a kind, with no entity) and of format 3 (no row
    of opted_out), for the history that the rule history
    (`guard check --rule history`) reads.

THE CONFIGURATION
    A configuration is the build kind and the target tier of
    utils/assay/core/target.py, and each cache variable of CONFIGURATION.
    The tier also holds the native tier of an object whose target adds a
    native flag to its own compile, as each bench target adds -march=native.
    The units of test/layer/admitted_flag_matrix compile with the release
    flags, so the tier of each x86_64 build holds the native tier of its host.
    A different configuration compiles other objects or other preprocessor
    arms, so its counts are different: a bench object on a host with
    AVX512BW compiles the AVX-512 arm of include/crucible/SwissTable.h, and
    on a host with no AVX512BW it compiles the AVX2 arm.  The name of a configuration is its
    build kind, with -bench when CRUCIBLE_BENCH is on and -fuzz when
    CRUCIBLE_FUZZ is on, for example x86_64-debug-asan or
    x86_64-debug-asan-bench-fuzz.  CRUCIBLE_SENSE_HUB_V2 is no key: each
    build compiles the SenseHub v2 sources in two object libraries, with and
    without CRUCIBLE_SENSE_HUB_EXTENDED, so that option changes no counted
    line.

THE VERDICT
    * A count above its ledger count is an error.  A change can only lower
      a count, as rule R11 of misc/01_10_2026_quarantine.md says.  So a new
      finding cannot pass because a finding of a different entity or of a
      different file went away.
    * A file that the ledger does not hold and that has a finding is an
      error: a new file has no finding (CLAUDE.md section XXII).
    * A count below its ledger count is an error too.  The commit that
      removes a finding writes the ledger again (--write), so the ledger
      keeps no slack.
    * An opted_out finding of a file with no region line is an error, because
      the check cannot give it its region.
    * Each directory gives one warning, so the findings that stay in the
      quarantine are in the output of each run.  The directory of a file is
      the longest path of a layer row or a quarantine row of the rule table
      that ends with '/' and holds the file, and the first directory under
      that path.
    * An object whose compile loads the plugin and that holds no section, and
      a section whose stamp is not the stamp of the compile command of its
      object, are errors.  The stamp holds the plugin and the rule table, so a
      compiler cache that does not hash it gives the findings of another
      plugin or another table.

WRITE THE LEDGER
    --write writes the rows of the configuration of the build: the counts of
    the base, or the differences of another configuration from the base rows.
    Write the base rows first.  It keeps the head comment, the other
    configurations and their rows.  It refuses a count above its row: remove
    the new finding.  --raise writes such a count too.  Use it only when the
    plugin or the rule table changed what the plugin reports, and give the
    reason in the commit.  --admit writes a count of opted_out above its row,
    and no other count.  Use it only for a region of a reason class of
    CLAUDE.md section XXII that must cover more, and give the reason in the
    commit.  The rule history (`guard check --rule history`) gives a warning
    for each such count on each run.
    * A file that git reports as moved (`git diff --find-renames HEAD`, after
      `git mv` or `git add`) takes the rows of its old path in each
      configuration.
    * A source file that the tree does not hold loses its rows in each
      configuration, because no build compiles it.  A file of the build
      directory keeps its rows, because a build of a different configuration
      can write it.  The check of that build compares those rows as it
      compares each other row.
    * --add-configuration writes the configuration of the build under its
      name, in place of the keys and rows of a configuration with the same
      name that is not the base.  The first configuration of an empty ledger
      is the base.  A change of the keys of the base generates the ledger
      again: keep its head, remove its rows, and run --write
      --add-configuration in the build of each configuration, the base first.

A CONFIGURATION THAT ONLY CI BUILDS
    No aarch64 compiler is on the build host, so only a CI leg can count an
    aarch64 configuration.  For a configuration other than the base, the check
    prints each row of the build with the prefix "quarantine-row: " after a
    failure and when the ledger does not hold the configuration.  --import
    LOG puts those rows into the ledger in place of the old rows of the
    configuration.  Import the log of the commit that you merge, because a
    difference row is relative to the base rows of that commit.  Import only
    a configuration that no build of the host counts.  The name of a
    configuration does not hold its tier, so the rows of an x86_64 CI runner
    of another tier replace the rows that the local build counts.

NOT APPLICABLE
    The check exits 3, with the reason, when the build has no build kind,
    when the build made only some targets or its generator is not Ninja,
    when no compile of the target all loads the quarantine plugin, or when the
    ledger does not hold the configuration of the build.  In the last case it
    prints one line with the tier of the build and the tiers of the ledger
    (build_target.not_held), and then the rows of the build.

Usage
    guard check --rule quarantine-ratchet --build-dir DIR [--warnings-dir DIR]
    guard check --rule quarantine-ratchet --build-dir DIR --write [--raise | --admit] [--add-configuration]
    guard check --rule quarantine-ratchet --build-dir DIR --list [FILE|DIRECTORY [KIND]]
    guard check --rule quarantine-ratchet --import LOG
    guard check --rule quarantine-ratchet --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage error
or a failed self-test, 3 when the check does not apply to the build.
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from collections.abc import Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import git as throwaway_repo
from assay.core import report as check_report
from assay.core import ruletable as layer_rules
from assay.core import target as build_target
from assay.core.paths import REPO_ROOT
from assay.readers import census as build_census
from assay.readers import sections as quarantine_sections

CHECK = "quarantine-ratchet"
LEDGER = "utils/scripts/quarantine-ledger.txt"
RULES = "utils/scripts/layer-rules.txt"
NOT_APPLICABLE = build_target.NOT_APPLICABLE
FORMAT = 4
# The formats that parse_ledger reads.  The check and --write use only FORMAT.
READ_FORMATS = (2, 3, FORMAT)
FORMAT_ROW = "format"
CONFIGURATION_ROW = "configuration"
KIND_KEY = "kind"
TIER_KEY = "tier"
# The cache variables that change the objects of all or their preprocessor arms.
CONFIGURATION = ("CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "CRUCIBLE_SANITIZE", "CRUCIBLE_TSAN",
                 "CRUCIBLE_UBSAN_STRICT", "CRUCIBLE_ANALYZER", "CRUCIBLE_VERIFY", "CRUCIBLE_PGO", "CRUCIBLE_BENCH",
                 "CRUCIBLE_FUZZ", "CRUCIBLE_EXAMPLES", "CRUCIBLE_HAVE_BPF", "CRUCIBLE_SENSE_HUB_EXTENDED",
                 "CRUCIBLE_VALGRIND_INCLUDE", "TORCH_DIR")
PLUGIN_ARGUMENT = "-fplugin-arg-crucible_quarantine-"
STAMP = re.compile(re.escape(PLUGIN_ARGUMENT) + r"stamp=(\S*)")
CMAKE_FALSE = {"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"}
NAME_PATTERN = re.compile(r"[a-z][a-z0-9_.-]*")
ROW_MARK = "quarantine-row: "
DELTA_PATTERN = re.compile(r"[+-][1-9][0-9]*")
READERS = 16
# The kinds whose entity is "NAME (TYPE)": the entity of a row is the library name (THE ENTITY).
NAME_KINDS = frozenset(("std_object", "c_library_object"))
# The kinds whose entity is a type of the code: a row has no entity.
TYPE_KINDS = frozenset(("raw_pointer_object", "raw_function_pointer", "c_array_object"))
NEW_DELETE_KIND = "raw_new_delete"
# The separator between the reason of a region and the finding inside it, in the entity of an opted_out row.
REASON_SEPARATOR = " | "
# The operators of a new-expression, the longest first.  The type after the operator is not part of the entity.
NEW_OPERATORS = ("placement new", "new")

# The file, the kind and the entity of one count.
Key = tuple[str, str, str]


class LedgerError(ValueError):
    """A row of the ledger does not have the format."""


@dataclass(slots=True)
class Ledger:
    """The rows of the ledger: the configurations, the base counts, the differences and the line of each row.

    `configurations` keeps the order of the ledger, and its first name is the
    base.  A format 1 ledger counts directories, and its one configuration has
    the name "default".  A count of format 1 or 2 has an empty entity.
    """

    head: list[str] = field(default_factory=list)
    format: int = FORMAT
    configurations: dict[str, dict[str, str]] = field(default_factory=dict)
    counts: dict[Key, int] = field(default_factory=dict)
    deltas: dict[str, dict[Key, int]] = field(default_factory=dict)
    lines: dict[tuple[str, str, str], int] = field(default_factory=dict)

    @property
    def base(self) -> str | None:
        """The name of the base configuration, or None for a ledger with no configuration."""
        return next(iter(self.configurations), None)

    def expected(self, name: str) -> dict[Key, int]:
        """Return the count of each file, kind and entity of one configuration, with no zero count.

        Complexity: linear in the rows of the base and of the configuration.
        """
        if name == self.base:
            return dict(self.counts)
        delta = self.deltas.get(name, {})
        counts = {key: self.counts.get(key, 0) + delta.get(key, 0) for key in set(self.counts) | set(delta)}
        return {key: count for key, count in counts.items() if count != 0}

    def line_of(self, name: str, key: Key) -> int:
        """Return the line of the row of one count: the difference row, else the base row, else 0.

        A count with no row gives the first row of its file, so a new entity of a file points at the file.
        Complexity: O(1) for a count with a row, else linear in the rows.
        """
        exact = self.lines.get((name, *key)) or self.lines.get((self.base or "", *key), 0)
        if exact:
            return exact
        return min((line for (row_name, file, _kind, _entity), line in self.lines.items()
                    if file == key[0] and row_name in (name, self.base)), default=0)


@dataclass(slots=True)
class Census:
    """The distinct findings of the objects of all, the opt-out regions, and the problems of the objects.

    `regions` holds the first line and the reason of each region of each file.  `reasons` gives each opted_out
    finding the reason of its region (THE OPT-OUT REGIONS).
    """

    findings: set[quarantine_sections.Finding] = field(default_factory=set)
    objects: int = 0
    problems: list[check_report.Finding] = field(default_factory=list)
    regions: dict[str, list[tuple[int, str]]] = field(default_factory=dict)
    reasons: dict[quarantine_sections.Finding, str] = field(default_factory=dict)


# ── The ledger ─────────────────────────────────────────────────────


def parse_ledger(text: str, where: str) -> Ledger:
    """Read the text of a ledger of format 1, 2 or 3.  The leading comment lines are its head.

    Raises:
        LedgerError: If a row does not have the format, a row has a second copy, or a count of a configuration is
        below 0
    """
    ledger = Ledger()
    is_head = True
    rows: list[tuple[int, str]] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            if is_head:
                ledger.head.append(raw)
            continue
        is_head = False
        rows.append((number, stripped))
    # A ledger with rows and no format row has format 1.  An empty ledger has the format of --write.
    ledger.format = 1 if rows else FORMAT
    if rows and rows[0][1].split()[0] == FORMAT_ROW:
        number, row = rows.pop(0)
        words = row.split()
        if len(words) != 2 or words[1] not in {str(item) for item in READ_FORMATS}:
            raise LedgerError(f"{where}:{number}: the format row is 'format {FORMAT}'")
        ledger.format = int(words[1])
    for number, row in rows:
        if ledger.format == 1:
            parse_row_format_1(ledger, number, row.split(), where)
        else:
            parse_row(ledger, number, row, where)
    for name in ledger.configurations:
        if any(count < 0 for count in ledger.expected(name).values()):
            raise LedgerError(f"{where}: a difference of the configuration {name} makes a count below 0")
    return ledger


def parse_row_format_1(ledger: Ledger, number: int, words: list[str], where: str) -> None:
    """Read one row of a format 1 ledger: a configuration row or a count of a directory and kind."""
    if words[0] == CONFIGURATION_ROW and len(words) == 3:
        ledger.configurations.setdefault("default", {})[words[1]] = words[2]
        return
    if len(words) != 3 or not words[2].isdigit():
        raise LedgerError(f"{where}:{number}: a format 1 row is 'DIRECTORY KIND COUNT'")
    ledger.configurations.setdefault("default", {})
    ledger.counts[(words[0], words[1], "")] = int(words[2])
    ledger.lines[("default", words[0], words[1], "")] = number


def parse_row(ledger: Ledger, number: int, row: str, where: str) -> None:
    """Read one row of a format 2 or format 3 ledger.  The entity of a format 3 row is the rest of the line.

    Raises:
        LedgerError: If the row does not have the format, or it has a second copy
    """
    words = row.split()
    if words[0] == CONFIGURATION_ROW:
        if len(words) != 4 or not NAME_PATTERN.fullmatch(words[1]):
            raise LedgerError(f"{where}:{number}: a configuration row is 'configuration NAME KEY VALUE', with a "
                              f"lowercase NAME")
        keys = ledger.configurations.setdefault(words[1], {})
        if words[2] in keys:
            raise LedgerError(f"{where}:{number}: the configuration {words[1]} has a second row for {words[2]}")
        keys[words[2]] = words[3]
        return
    base = ledger.base
    is_delta = words[0] in ledger.configurations
    if ledger.format == 2:
        fields = words if len(words) == (4 if is_delta else 3) else []
    else:
        # The entity of a format 3 row is the rest of the line, with its spaces.  An empty field is no field.
        fields = row.split(" ", 4 if is_delta else 3)
    if is_delta:
        if words[0] == base or len(fields) < 4 or "" in fields or not DELTA_PATTERN.fullmatch(fields[3]):
            raise LedgerError(f"{where}:{number}: a difference row is 'NAME FILE KIND +N|-N ENTITY', for a "
                              f"configuration other than the base {base}")
        name, key, value = fields[0], (fields[1], fields[2], fields[4] if len(fields) == 5 else ""), int(fields[3])
        table = ledger.deltas.setdefault(name, {})
    else:
        if base is None or len(fields) < 3 or "" in fields or not fields[2].isdigit() or int(fields[2]) == 0:
            raise LedgerError(f"{where}:{number}: a count row is 'FILE KIND COUNT ENTITY', with a COUNT above 0, "
                              f"after the configuration rows")
        name, key, value = base, (fields[0], fields[1], fields[3] if len(fields) == 4 else ""), int(fields[2])
        table = ledger.counts
    if key[2] != " ".join(key[2].split()):
        raise LedgerError(f"{where}:{number}: the entity {key[2]!r} has more than one space between two words")
    if key in table:
        raise LedgerError(f"{where}:{number}: the file {key[0]}, the kind {key[1]} and the entity {key[2]!r} have a "
                          f"second row in the configuration {name}")
    table[key] = value
    ledger.lines[(name, *key)] = number


def read_ledger(path: Path) -> Ledger:
    """Read the ledger file.

    Raises:
        LedgerError: If a row does not have the format
        OSError: If the file cannot be read
    """
    return parse_ledger(path.read_text(encoding="utf-8"), str(path))


def row_text(*fields: str) -> str:
    """Return one row: its fields with one space between them.  An empty entity at the end gives no field."""
    return " ".join(fields).rstrip(" ")


def ledger_text(ledger: Ledger) -> str:
    """Return the text of a format 3 ledger: the head, the configurations, the base rows, then the differences.

    Complexity: O(r log r) for r rows.
    """
    lines = [*ledger.head, f"{FORMAT_ROW} {FORMAT}"]
    lines += [f"{CONFIGURATION_ROW} {name} {key} {value}" for name, keys in ledger.configurations.items()
              for key, value in keys.items()]
    lines += [row_text(file, kind, str(count), entity) for (file, kind, entity), count in sorted(ledger.counts.items())
              if count > 0]
    lines += [row_text(name, file, kind, f"{delta:+d}", entity) for name in ledger.configurations
              if name in ledger.deltas for (file, kind, entity), delta in sorted(ledger.deltas[name].items())
              if delta != 0]
    return "\n".join(lines) + "\n"


# ── The configuration ──────────────────────────────────────────────


def cache_entries(build_dir: Path) -> dict[str, tuple[str, str]]:
    """Return the type and the value of each entry of the CMakeCache.txt of a build."""
    entries: dict[str, tuple[str, str]] = {}
    with contextlib.suppress(OSError):
        for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_.+-]*):([A-Z]+)=(.*)", line)
            if match:
                entries[match[1]] = (match[2], match[3])
    return entries


def normalized(kind: str, value: str) -> str:
    """Return a cache value as one word: on or off for a BOOL, set or unset for a path, else the value or unset."""
    is_false = value.upper() in CMAKE_FALSE or value.upper().endswith("-NOTFOUND")
    if kind == "BOOL":
        return "off" if is_false else "on"
    if kind in ("PATH", "FILEPATH"):
        return "unset" if is_false else "set"
    return value if value.strip() else "unset"


class ConfigurationError(ValueError):
    """The build has no build kind, or its compiler does not print its target."""


def build_configuration(build_dir: Path, commands: Iterable[str]) -> tuple[str, dict[str, str]]:
    """Return the name and the configuration of a build: the build kind, the tier and each name of CONFIGURATION.

    The tier holds the native tier of each native flag of the compile commands of the objects, so a bench target
    with its own -march=native gives the configuration the tier of the host.

    Raises:
        ConfigurationError: If the build has no build-kind.txt, or the compiler does not print its target
    """
    kind = build_target.kind_of(build_dir)
    if kind is None:
        raise ConfigurationError(f"{build_dir} has no build-kind.txt, which cmake/BuildLauncher.cmake writes")
    entries = cache_entries(build_dir)
    try:
        tier = build_target.build_tier(build_dir, entries.get("CMAKE_CXX_COMPILER", ("FILEPATH", ""))[1], commands)
    except build_target.TierError as problem:
        raise ConfigurationError(str(problem)) from None
    configuration = {KIND_KEY: kind, TIER_KEY: tier}
    for name in CONFIGURATION:
        cache_type, value = entries.get(name, ("STRING", ""))
        configuration[name] = normalized(cache_type, value).replace(" ", "_")
    suffixes = [word for word, key in (("bench", "CRUCIBLE_BENCH"), ("fuzz", "CRUCIBLE_FUZZ"))
                if configuration[key] == "on"]
    return "-".join([kind, *suffixes]), configuration


def configuration_difference(build: dict[str, str], ledger: dict[str, str]) -> list[str]:
    """Return a text for each key whose value in the build is not its value in the ledger."""
    return [f"{name} is {build.get(name, 'absent')} in the build and {ledger.get(name, 'absent')} in the ledger"
            for name in sorted(set(build) | set(ledger)) if build.get(name) != ledger.get(name)]


def held_line(ledger: Ledger, name: str, configuration: dict[str, str]) -> str:
    """Return the one line of a build whose configuration the ledger does not hold (build_target.not_held)."""
    held = [(held_name, keys.get(TIER_KEY, "?")) for held_name, keys in ledger.configurations.items()]
    difference = configuration_difference(configuration, ledger.configurations.get(name, {}))
    detail = (f"Its configuration {name} differs: {'; '.join(difference)}" if name in ledger.configurations else "")
    return build_target.not_held(CHECK, name, configuration[TIER_KEY], held, detail)


# ── The findings of the build ──────────────────────────────────────


def plugin_objects(build_dir: Path, root: Path) -> list[tuple[Path, str, str]]:
    """Return each object of all whose compile loads the quarantine plugin, with the stamp and the compile command.

    Raises:
        build_census.NotApplicable: If the build made only some targets, or its generator is not Ninja
        build_census.CensusError: If the compile database or the inputs of all cannot be read
    """
    ninja, _ = build_census.tools_of(build_dir)
    objects = {str(path) for path, _ in build_census.objects_of_all(build_dir, ninja, root)}
    rows = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    found: list[tuple[Path, str, str]] = []
    for row in rows:
        output = os.path.normpath(os.path.join(row["directory"], row.get("output", "")))
        command = row.get("command") or " ".join(row.get("arguments", []))
        stamp = STAMP.search(command)
        if output in objects and stamp is not None:
            found.append((Path(output), stamp[1], command))
    return sorted(found)


def read_census(objects: list[tuple[Path, str, str]], build_dir: Path) -> Census:
    """Read the section of each object, and check its stamp against the stamp of its command.

    Complexity: linear in the number of sections and finding lines of the objects.
    """
    census = Census(objects=len(objects))

    def read_one(item: tuple[Path, str, str]) -> tuple[Path, str, quarantine_sections.Section | None, str]:
        path, stamp, _command = item
        try:
            data = quarantine_sections.read_section(path)
            return path, stamp, None if data is None else quarantine_sections.parse_section(data, str(path)), ""
        except (OSError, quarantine_sections.SectionError) as problem:
            return path, stamp, None, str(problem)

    with ThreadPoolExecutor(max_workers=READERS) as pool:
        results = list(pool.map(read_one, objects))
    # The file, the first line and the reason of each region of each unit.
    regions: set[tuple[str, int, str]] = set()
    for path, stamp, section, problem in results:
        shown = build_census.shown(path, build_dir)
        if problem:
            census.problems.append(check_report.Finding("error", shown, 0, CHECK, problem))
        elif section is None:
            census.problems.append(check_report.Finding(
                "error", shown, 0, CHECK, "the compile of the object loads the quarantine plugin, and the object "
                "holds no section .crucible.quarantine.  Compile it again"))
        elif section.stamp != stamp:
            census.problems.append(check_report.Finding(
                "error", shown, 0, CHECK, f"the section holds the stamp {section.stamp!r}, and the compile command "
                f"has the stamp {stamp!r}.  The compiler cache gave the findings of another plugin or rule table: "
                f"its key must hold the stamp argument"))
        else:
            for item in section.findings:
                if item.kind == quarantine_sections.REGION:
                    regions.add((item.file, item.line, " ".join(item.entity.split())))
                else:
                    census.findings.add(item)
    for file, line, reason in sorted(regions):
        census.regions.setdefault(file, []).append((line, reason))
    give_reasons(census)
    return census


def give_reasons(census: Census) -> None:
    """Give each opted_out finding the reason of its region, and an error to each one with no region.

    Complexity: O(f log r) for f opted_out findings and r regions of a file.
    """
    for item in sorted(census.findings):
        if item.kind != quarantine_sections.OPTED_OUT:
            continue
        starts = census.regions.get(item.file, [])
        index = bisect.bisect_right(starts, (item.line, chr(0x10FFFF))) - 1
        if index < 0:
            census.problems.append(check_report.Finding(
                "error", item.file, item.line, CHECK,
                f"the finding {item.text()!r} is opted out, and no section gives a region of {item.file} at or "
                f"before its line.  The plugin writes a line of the kind region for each region: compile the unit "
                f"again"))
            continue
        census.reasons[item] = starts[index][1]


def directory_rows(table: layer_rules.RuleTable) -> list[str]:
    """Return the paths of the layer rows and quarantine rows that end with '/', the longest first."""
    rows = {path for layer in table.layers for path in layer.paths if path.endswith("/")}
    rows |= {path for path in table.quarantines if path.endswith("/")}
    return sorted(rows, key=lambda path: (-len(path), path))


def directory_of(rel: str, rows: list[str]) -> str:
    """Return the directory of a file: the longest directory row that holds it, and the first directory under it."""
    for row in rows:
        if rel.startswith(row):
            rest = rel[len(row):].split("/")
            return row.rstrip("/") + (f"/{rest[0]}" if len(rest) > 1 else "")
    return rel.rsplit("/", 1)[0] if "/" in rel else "."


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
        return next((operator for operator in NEW_OPERATORS if text == operator or text.startswith(f"{operator} ")),
                    text)
    return text


def opted_out_key(reason: str, entity: str) -> str:
    """Return the entity of the row of an opted_out finding: the reason of its region, the inner kind and entity."""
    inner, _, rest = " ".join(entity.split()).partition(" ")
    return row_text(f"{' '.join(reason.split())}{REASON_SEPARATOR}{inner}", entity_key(inner, rest))


def count_findings(findings: Iterable[quarantine_sections.Finding],
                   reasons: dict[quarantine_sections.Finding, str]) -> Counter[Key]:
    """Return the number of findings of each file, kind and entity.

    An opted_out finding counts with the reason of its region from `reasons`.  One with no reason gives a problem of
    the census (give_reasons) and no count.  Complexity: linear in the findings.
    """
    counts: Counter[Key] = Counter()
    for item in findings:
        if item.kind != quarantine_sections.OPTED_OUT:
            counts[(item.file, item.kind, entity_key(item.kind, item.entity))] += 1
        elif item in reasons:
            counts[(item.file, item.kind, opted_out_key(reasons[item], item.entity))] += 1
    return counts


def described(kind: str, entity: str) -> str:
    """Return the kind and the entity of a row as words for a message."""
    return f"the kind {kind} and the entity {entity}" if entity else f"the kind {kind}"


def git_moves(root: Path) -> dict[str, str]:
    """Return the old path of each file that git reports as moved between HEAD and the work tree.

    A tree that is not a git work tree, or a failed git command, gives no move.
    """
    try:
        proc = subprocess.run(["git", "-C", str(root), "diff", "--find-renames", "--name-status", "-z", "HEAD",
                               "--"], capture_output=True, check=False)
    except OSError:
        return {}
    if proc.returncode != 0:
        return {}
    moves: dict[str, str] = {}
    words = proc.stdout.decode("utf-8", "surrogateescape").split("\0")
    index = 0
    while index < len(words):
        status = words[index]
        if status[:1] in ("R", "C") and index + 2 < len(words):
            if status[0] == "R":
                moves[words[index + 2]] = words[index + 1]
            index += 3
        else:
            index += 2 if status else 1
    return moves


# ── The verdict ────────────────────────────────────────────────────


def evaluate(counts: Counter[Key], ledger: Ledger, name: str, rows: list[str], ledger_shown: str,
             build_shown: str) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count, and give one warning for each directory that passes.

    Complexity: O(k log k) for k counts of the build and the ledger.
    """
    findings: list[check_report.Finding] = []
    expected = ledger.expected(name)
    held = {file for file, _, _ in expected}
    failed: set[str] = set()
    script = f"python3 utils/bin/guard check --rule quarantine-ratchet --build-dir {build_shown}"
    for key in sorted(set(counts) | set(expected)):
        file, kind, entity = key
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured == admitted:
            continue
        failed.add(directory_of(file, rows))
        line = ledger.line_of(name, key)
        listing = f"`{script} --list {file} {kind}`"
        what = described(kind, entity)
        if measured > admitted and kind == quarantine_sections.OPTED_OUT:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The region covers "
                f"more, and a region can only shrink.  List them with {listing}, and move the code out of the "
                f"region.  If the reason class of the region (CLAUDE.md section XXII) needs the code, write the "
                f"ledger with `{script} --write --admit` and give the reason in the commit"))
        elif measured > admitted and file not in held:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{file} has {measured} findings of {what}, and the ledger holds no row of the file.  A new file has "
                f"no finding (CLAUDE.md section XXII).  List them with {listing}, and use a type of fixy or "
                f"foundation.  If the change moves the file, stage the move with `git mv` and write the ledger "
                f"again"))
        elif measured > admitted:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{file} has {measured} findings of {what}, and the ledger admits {admitted}.  The change adds "
                f"{measured - admitted}, and a change can only lower a count, also when a finding of another entity "
                f"goes.  List them with {listing}, and use a type of fixy or foundation"))
        else:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{file} has {measured} findings of {what}, and the ledger holds {admitted}.  Write the ledger again "
                f"in the same commit: `{script} --write`"))
    kept: dict[str, Counter[str]] = {}
    first_line: dict[str, int] = {}
    for key, admitted in sorted(expected.items()):
        directory = directory_of(key[0], rows)
        if directory in failed:
            continue
        kept.setdefault(directory, Counter())[key[1]] += admitted
        first_line.setdefault(directory, ledger.line_of(name, key))
    for directory, kinds in sorted(kept.items()):
        shown = ", ".join(f"{kind} {count}" for kind, count in sorted(kinds.items()))
        findings.append(check_report.Finding("warning", ledger_shown, first_line[directory], CHECK,
                                             f"{directory} keeps {sum(kinds.values())} quarantine findings: {shown}"))
    return findings


# ── The command ────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Request:
    """What one run does: check, write the ledger or list findings."""

    write: bool = False
    allow_raise: bool = False
    add_configuration: bool = False
    listed: list[str] | None = None
    # With write: also write a count of opted_out above its row (WRITE THE LEDGER).
    admit: bool = False


def run(build_dir: Path, root: Path, ledger_path: Path, rules_path: Path, warnings_dir: Path | None,
        request: Request = Request()) -> int:
    """Run the check, write the ledger or list findings for one build.

    Returns:
        The exit status
    """
    ledger_shown = build_census.shown(ledger_path, root)
    build_shown = build_census.shown(build_dir, root)
    try:
        ledger = read_ledger(ledger_path)
        table = layer_rules.load(rules_path)
    except (OSError, LedgerError, layer_rules.TableError) as problem:
        return check_report.emit([check_report.Finding("error", ledger_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    if ledger.format != FORMAT:
        return check_report.emit([check_report.Finding(
            "error", ledger_shown, 0, CHECK, f"the ledger has format {ledger.format}, and the check reads format "
            f"{FORMAT}.  Generate it again with --write --add-configuration")], CHECK, warnings_dir)
    if build_target.kind_of(build_dir) is None:
        print(f"{CHECK}: {build_shown} has no build-kind.txt.  The check does not apply")
        return NOT_APPLICABLE
    try:
        objects = plugin_objects(build_dir, root)
    except build_census.NotApplicable as problem:
        print(f"{CHECK}: {problem}.  The check does not apply")
        return NOT_APPLICABLE
    except (build_census.CensusError, OSError, ValueError) as problem:
        return check_report.emit([check_report.Finding("error", build_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    if not objects:
        print(f"{CHECK}: no compile of the target all loads the quarantine plugin.  The check does not apply")
        return NOT_APPLICABLE
    try:
        name, configuration = build_configuration(build_dir, (command for _, _, command in objects))
    except ConfigurationError as problem:
        return check_report.emit([check_report.Finding("error", build_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    census = read_census(objects, build_dir)
    rows = directory_rows(table)
    counts = count_findings(census.findings, census.reasons)
    if request.listed is not None:
        return list_findings(census, request.listed)
    is_held = ledger.configurations.get(name) == configuration
    if request.write:
        return write_ledger(root, ledger_path, ledger, name if is_held else None, name, configuration, counts,
                            census, request, warnings_dir)
    if not is_held:
        print(held_line(ledger, name, configuration))
        print_rows(ledger, name, configuration, counts, census)
        return NOT_APPLICABLE
    findings = census.problems + evaluate(counts, ledger, name, rows, ledger_shown, build_shown)
    status = check_report.emit(findings, CHECK, warnings_dir)
    files = {file for file, _, _ in counts}
    print(f"{CHECK}: {sum(counts.values())} findings in {len(counts)} rows of {len(files)} files, configuration "
          f"{name} on {configuration[TIER_KEY]}, from {census.objects} objects")
    if status != 0:
        print_rows(ledger, name, configuration, counts, census)
    return status


def list_findings(census: Census, listed: list[str]) -> int:
    """Print the findings of a file or of a directory, and of one kind when the second word names it."""
    place = listed[0].rstrip("/") if listed else ""
    selected = sorted(item for item in census.findings
                      if (not place or item.file == place or item.file.startswith(f"{place}/"))
                      and (len(listed) < 2 or item.kind == listed[1]))
    for item in selected:
        print(item.text())
    print(f"{CHECK}: {len(selected)} findings")
    return 0


def print_rows(ledger: Ledger, name: str, configuration: dict[str, str], counts: Counter[Key],
               census: Census) -> None:
    """Print the rows of the configuration of the build, as --import reads them from a log.

    The base has no such rows, because --write writes it, and a build with a
    problem object has no rows to print.  Each row has the prefix ROW_MARK.
    """
    if name == ledger.base or ledger.base is None or census.problems:
        return
    rows = [f"{CONFIGURATION_ROW} {name} {key} {value}" for key, value in configuration.items()]
    rows += [row_text(name, file, kind, f"{counts.get(key, 0) - ledger.counts.get(key, 0):+d}", entity)
             for key in sorted(set(counts) | set(ledger.counts)) for file, kind, entity in [key]
             if counts.get(key, 0) != ledger.counts.get(key, 0)]
    build_target.print_rows(ROW_MARK, rows)
    print(f"{CHECK}: the lines above give the rows of the configuration {name}.  Import them from the log with "
          f"`python3 utils/bin/guard check --rule quarantine-ratchet --import LOG`")


def import_rows(ledger_path: Path, source: Path) -> int:
    """Put the rows of one configuration that print_rows printed into the ledger, in place of its old rows.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = read_ledger(ledger_path)
        rows = [row for row in build_target.rows_in_log(source.read_text(encoding="utf-8", errors="replace"),
                                                        ROW_MARK) if row.split()]
        names = {words[1] if words[0] == CONFIGURATION_ROW and len(words) > 1 else words[0]
                 for words in (row.split() for row in rows)}
        if len(names) != 1:
            raise LedgerError(f"{source}: the rows name {len(names)} configurations, and an import takes one")
        name = names.pop()
        if name == ledger.base:
            raise LedgerError(f"{source}: the rows are of the base configuration {name}.  Write it with --write")
        keys: dict[str, str] = {}
        deltas: dict[Key, int] = {}
        for row in rows:
            words = row.split()
            # The entity of a difference row is the rest of the line, with its spaces.
            fields = row.split(" ", 4)
            if words[0] == CONFIGURATION_ROW and len(words) == 4:
                if keys.setdefault(words[2], words[3]) != words[3]:
                    raise LedgerError(f"{source}: the key {words[2]} has two values")
            elif len(fields) >= 4 and "" not in fields and DELTA_PATTERN.fullmatch(fields[3]):
                key = (fields[1], fields[2], fields[4] if len(fields) == 5 else "")
                if deltas.setdefault(key, int(fields[3])) != int(fields[3]):
                    raise LedgerError(f"{source}: the file {key[0]}, the kind {key[1]} and the entity {key[2]!r} have "
                                      f"two values")
            else:
                raise LedgerError(f"{source}: the row {row!r} is not a row of a configuration")
        ledger.configurations[name] = keys
        ledger.deltas[name] = deltas
        text = ledger_text(ledger)
        parse_ledger(text, str(source))
    except (OSError, LedgerError) as problem:
        print(f"{CHECK}: the ledger is not written: {problem}", file=sys.stderr)
        return 1
    ledger_path.write_text(text, encoding="utf-8")
    print(f"{CHECK}: imported the configuration {name}: {len(keys)} keys and {len(deltas)} difference rows, to "
          f"{ledger_path}")
    return 0


def moved_expectation(expected: dict[Key, int], moves: dict[str, str]) -> dict[Key, int]:
    """Return the expected counts with the rows of each moved file under its new path.

    A move counts only from a file that the ledger holds to a file that it does not hold.
    """
    held = {file for file, _, _ in expected}
    renamed = {new: old for new, old in moves.items() if old in held and new not in held}
    result = dict(expected)
    for (file, kind, entity), count in expected.items():
        for new, old in renamed.items():
            if old == file:
                result[(new, kind, entity)] = count
    return result


def write_ledger(root: Path, ledger_path: Path, ledger: Ledger, held: str | None, name: str,
                 configuration: dict[str, str], counts: Counter[Key], census: Census, request: Request,
                 warnings_dir: Path | None) -> int:
    """Write the rows of the configuration of the build, unless an object has a problem or a count rose.

    `held` is the name of the configuration when the ledger holds it, else None.  Complexity: O(k log k) for k
    rows of the ledger and counts of the build.
    """
    if census.problems:
        print(f"{CHECK}: the ledger is not written, because an object has a problem", file=sys.stderr)
        return check_report.emit(census.problems, CHECK, warnings_dir)
    if request.add_configuration:
        refusal = add_refusal(ledger, held, name)
        if refusal:
            print(f"{CHECK}: the ledger is not written: {refusal}", file=sys.stderr)
            return 1
        ledger.configurations[name] = dict(configuration)
    elif held is None:
        print(held_line(ledger, name, configuration))
        print(f"{CHECK}: the ledger is not written.  Give --add-configuration to write the configuration of the "
              f"build", file=sys.stderr)
        return NOT_APPLICABLE
    else:
        moves = git_moves(root)
        expected = moved_expectation(ledger.expected(name), moves)
        held = {file for file, _, _ in expected}
        rises = sorted((key, count, expected.get(key, 0)) for key, count in counts.items()
                       if count > expected.get(key, 0))
        for (file, kind, entity), count, admitted in rises:
            words = "a file that the ledger does not hold" if file not in held else f"rises from {admitted}"
            print(f"{CHECK}: {file} has {count} findings of {described(kind, entity)}, {words}", file=sys.stderr)
        # --admit writes a region that covers more, and --raise writes each count.
        refused = [rise for rise in rises if not request.allow_raise
                   and not (request.admit and rise[0][1] == quarantine_sections.OPTED_OUT)]
        if refused:
            print(f"{CHECK}: the ledger is not written: {len(refused)} counts rose.  Remove the new findings.  Give "
                  f"--admit only for a region that must cover more, and --raise only when the plugin or the rule "
                  f"table changed what it reports", file=sys.stderr)
            return 1
        move_rows(ledger, moves)
    base = ledger.base
    if name == base:
        ledger.counts = dict(counts)
        drop_gone_files(ledger, root, counts)
    else:
        ledger.deltas[name] = {key: counts.get(key, 0) - ledger.counts.get(key, 0)
                               for key in set(counts) | set(ledger.counts)
                               if counts.get(key, 0) != ledger.counts.get(key, 0)}
    ledger_path.write_text(ledger_text(ledger), encoding="utf-8")
    files = {file for file, _, _ in counts}
    print(f"{CHECK}: wrote the configuration {name}: {sum(counts.values())} findings in {len(counts)} rows of "
          f"{len(files)} files, to {ledger_path}")
    return 0


def add_refusal(ledger: Ledger, held: str | None, name: str) -> str:
    """Return the reason that --add-configuration cannot write the configuration of the build, or an empty text."""
    if not NAME_PATTERN.fullmatch(name):
        return f"the name {name!r} of the configuration is not a lowercase word"
    if held is not None:
        return f"the ledger holds the configuration {name} of the build.  Write its rows with --write"
    if name == ledger.base:
        return (f"the build has other keys than the base configuration {name}.  A change of the base keys "
                f"generates the ledger again: keep its head, remove its rows, then run --write "
                f"--add-configuration in the build of each configuration, the base first")
    return ""


def move_rows(ledger: Ledger, moves: dict[str, str]) -> None:
    """Move the difference rows of each moved file to its new path, in each configuration."""
    for table in ledger.deltas.values():
        for (file, kind, entity), delta in list(table.items()):
            new = next((new for new, old in moves.items() if old == file), None)
            if new is not None and (new, kind, entity) not in table:
                del table[(file, kind, entity)]
                table[(new, kind, entity)] = delta


def drop_gone_files(ledger: Ledger, root: Path, counts: Counter[Key]) -> None:
    """Remove the difference rows of each source file that the tree does not hold, and keep each count at 0 or above.

    A file of the build directory (layer_rules.BUILD_PREFIX) is in no source tree.  A write of the base cannot know
    if a build of a different configuration writes the file.  The rows of the file stay, and the check of that
    configuration compares them as it compares each other row.  When a build of the configuration does not write the
    file, its check fails on the row until its own --write removes the row.  Complexity: linear in the difference
    rows.
    """
    for table in ledger.deltas.values():
        for key, delta in list(table.items()):
            base = ledger.counts.get(key, 0)
            is_source_gone = not key[0].startswith(layer_rules.BUILD_PREFIX) and not (root / key[0]).exists()
            if key not in counts and is_source_gone:
                del table[key]
            elif base + delta < 0:
                table[key] = -base
                if table[key] == 0:
                    del table[key]


# ── The self-test ──────────────────────────────────────────────────


FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / "inputs.txt").read_text())
"""
RULES_TEXT = ("layer base 0 include/fixy/ include/fixy/session/\n"
              "quarantine include/crucible/\nquarantine src/\nquarantine test/\nquarantine @build/\n")
HEAD = "# a planted ledger\n"
STAMP_VALUE = "0123abcd"
PROBE_REASON = "PROBE: a planted region"
# A fake compiler: -Q --help=target prints the -march value of the file march.txt beside it, or of native.txt for
# -march=native.
FAKE_COMPILER = """\
import sys
from pathlib import Path
here = Path(__file__).parent
word = "native" if "-march=native" in sys.argv else "march"
value = (here / f"{word}.txt").read_text().strip() if (here / f"{word}.txt").exists() else "x86-64"
print("The following options are target specific:")
print(f"  -march=                     \\t\\t{value}")
print("  -mtune=                     \\t\\tgeneric")
"""
BASE_NAME = "x86_64-debug-asan"
RELEASE_NAME = "x86_64-release"
SOURCES = ("include/crucible/Vigil.h", "test/fixy/test_a.cpp", "test/fixy/test_b.cpp", "src/canopy/Lifeguard.cpp",
           "include/fixy/session/Handle.h", "test/test_arena.cpp")


class Scratch:
    """A scratch build with a fake ninja, a compile database, objects with sections, a rule table and a ledger.

    The root is a throwaway git repository with each source file of the findings, so a move is a git move.
    """

    def __init__(self, root: Path) -> None:
        """Make the tree with three objects."""
        self.root = root
        self.build = root / "build"
        self.build.mkdir(parents=True)
        throwaway_repo.init(root)
        self.environment = dict(os.environ, GIT_AUTHOR_NAME="crucible-selftest",
                                GIT_AUTHOR_EMAIL="crucible-selftest@invalid", GIT_COMMITTER_NAME="crucible-selftest",
                                GIT_COMMITTER_EMAIL="crucible-selftest@invalid")
        for rel in SOURCES:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(f"// {rel}\n", encoding="utf-8")
        (root / ".gitignore").write_text("build/\n", encoding="utf-8")
        self.git("add", "-A", "--", ".")
        self.git("commit", "-q", "-m", "Add the planted sources")
        self.ledger = root / "quarantine-ledger.txt"
        self.ledger.write_text(HEAD, encoding="utf-8")
        self.rules = root / "layer-rules.txt"
        self.rules.write_text(RULES_TEXT, encoding="utf-8")
        ninja = root / "ninja.py"
        ninja.write_text(f"#!{sys.executable}\n{FAKE_NINJA}", encoding="utf-8")
        ninja.chmod(0o755)
        self.compiler = root / "compiler" / "g++"
        self.compiler.parent.mkdir()
        self.compiler.write_text(f"#!{sys.executable}\n{FAKE_COMPILER}", encoding="utf-8")
        self.compiler.chmod(0o755)
        self.ninja = ninja
        self.configure(BASE_NAME, "Debug")
        self.objects: dict[str, tuple[list[str], str, bool]] = {}
        # The flags that the target of an object adds to its compile, as each bench target adds -march=native.
        self.flags: dict[str, str] = {}
        header = "quarantine: std_object include/crucible/Vigil.h:10:5 std::vector (std::vector<int>)"
        self.add("a", [header, "quarantine: c_library_call test/fixy/test_a.cpp:4:3 memcpy",
                       "quarantine: std_object test/fixy/test_b.cpp:6:5 std::span (std::span<int>)",
                       f"quarantine: region test/fixy/test_a.cpp:8:1 {PROBE_REASON}",
                       "quarantine: opted_out test/fixy/test_a.cpp:9:1 raw_pointer_object char*"])
        self.add("b", [header, "quarantine: raw_pointer_object src/canopy/Lifeguard.cpp:7:9 char*",
                       "quarantine: upward_include include/fixy/session/Handle.h:3:0 include/crucible/X.h"])
        self.add("c", ["quarantine: std_entity test/test_arena.cpp:2:1 std::swap"])

    def configure(self, kind: str, build_type: str, flags: str = "", extra: str = "") -> None:
        """Write the build kind and the CMakeCache.txt of one configuration of the scratch build."""
        (self.build / "build-kind.txt").write_text(f"{kind}\n", encoding="utf-8")
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_MAKE_PROGRAM:FILEPATH={self.ninja}\n"
            f"CMAKE_CXX_COMPILER:FILEPATH={self.compiler}\nCMAKE_BUILD_TYPE:STRING={build_type}\n"
            f"CMAKE_CXX_FLAGS:STRING={flags}\n{extra}", encoding="utf-8")

    def git(self, *arguments: str) -> None:
        """Run one git command in the scratch repository."""
        subprocess.run(["git", "-C", str(self.root), *arguments], check=True, capture_output=True,
                       env=self.environment)

    def add(self, name: str, lines: list[str], stamp: str = STAMP_VALUE, has_section: bool = True) -> None:
        """Plant one object of all with its finding lines, or with no section."""
        self.objects[name] = (lines, stamp, has_section)
        self.flush()

    def flush(self) -> None:
        """Write each object, the compile database and the inputs of all."""
        rows = []
        for name, (lines, stamp, has_section) in self.objects.items():
            text = "".join(f"{line}\n" for line in [f"# crucible-quarantine 1 stamp={stamp}", *lines])
            sections = [(b".text", b"\x90")] + ([(quarantine_sections.SECTION_NAME, text.encode())]
                                                if has_section else [])
            (self.build / f"{name}.o").write_bytes(quarantine_sections.planted_object(sections))
            rows.append({"directory": str(self.build), "file": str(self.root / f"{name}.cpp"), "output": f"{name}.o",
                         "command": f"g++ {self.flags.get(name, '')} -fplugin=q.so {PLUGIN_ARGUMENT}stamp="
                                    f"{STAMP_VALUE} -c {name}.cpp"})
        (self.build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")
        (self.build / "inputs.txt").write_text("".join(f"{name}.o\n" for name in self.objects), encoding="utf-8")

    def run(self, write: bool = False, allow_raise: bool = False, add_configuration: bool = False,
            listed: list[str] | None = None, admit: bool = False) -> tuple[int, list[check_report.Finding], str]:
        """Run the check on the scratch build, and return the status, the findings and the printed text."""
        request = Request(write, allow_raise, add_configuration, listed, admit)
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()) as said:
            status = run(self.build, self.root, self.ledger, self.rules, None, request)
        text = printed.getvalue() + said.getvalue()
        return status, [found for found in map(check_report.parse_line, printed.getvalue().splitlines()) if found], \
            text

    def errors(self) -> list[str]:
        """Return the first sentence of each error of a check run."""
        _, findings, _ = self.run()
        return [item.message.split(".  ")[0] for item in findings if item.level == "error"]

    def counts(self) -> Counter[Key]:
        """Return the count of each file, kind and entity of the planted objects."""
        census = read_census(plugin_objects(self.build, self.root), self.build)
        return count_findings(census.findings, census.reasons)


def directory_totals(counts: dict[Key, int], rows: list[str]) -> Counter[tuple[str, str]]:
    """Return the count of each directory and kind: the rule of the ratchet before it counted files."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(directory_of(file, rows), kind)] += count
    return totals


def file_totals(counts: dict[Key, int]) -> Counter[tuple[str, str]]:
    """Return the count of each file and kind: the rule of the ratchet before it counted entities."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(file, kind)] += count
    return totals


def self_test() -> int:
    """Do a test of the check on planted builds, with negative controls.

    Returns:
        0 when each case holds, else 2
    """
    throwaway_repo.isolate()
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}")

    rows = directory_rows(layer_rules.parse(RULES_TEXT))
    for rel, wanted in (("test/fixy/test_a.cpp", "test/fixy"), ("test/test_arena.cpp", "test"),
                        ("src/canopy/Lifeguard.cpp", "src/canopy"), ("include/crucible/Vigil.h", "include/crucible"),
                        ("include/crucible/ledger/probes/A.h", "include/crucible/ledger"),
                        ("include/fixy/session/Handle.h", "include/fixy/session"),
                        ("include/fixy/os/Time.h", "include/fixy/os"), ("include/fixy/Refined.h", "include/fixy")):
        expect(f"the directory of {rel} is {wanted}", directory_of(rel, rows) == wanted, directory_of(rel, rows))
    expect("normalized reads a BOOL, a path and a string",
           (normalized("BOOL", "TRUE"), normalized("BOOL", "OFF"), normalized("PATH", "x-NOTFOUND"),
            normalized("FILEPATH", "/usr/bin/x"), normalized("STRING", ""), normalized("STRING", "Debug"))
           == ("on", "off", "unset", "set", "unset", "Debug"))
    for text in ("format 3\nconfiguration b machine x\nx.cpp std_object -1 std::vector\n",
                 "format 3\nconfiguration b machine x\nconfiguration r machine y\nb x.cpp std_object +1 std::vector\n",
                 "format 3\nconfiguration b machine x\nconfiguration r machine y\nx.cpp k 1 e\nr x.cpp k -2 e\n",
                 "format 3\nx.cpp std_object 1 std::vector\n", "format 5\n",
                 "format 3\nconfiguration b machine x\nx.cpp std_object 1  std::vector\n",
                 "format 3\nconfiguration b machine x\nx.cpp std_object 1 std::vector\n"
                 "x.cpp std_object 2 std::vector\n",
                 "format 2\nconfiguration b machine x\nx.cpp std_object 1 std::vector\n"):
        try:
            parse_ledger(text, "planted")
            expect(f"a malformed ledger is refused: {text!r}", False)
        except LedgerError:
            expect(f"a malformed ledger is refused: {text.splitlines()[-1]!r}", True)
    keyed = parse_ledger("format 3\nconfiguration b machine x\nconfiguration r machine y\n"
                         "x.cpp raw_pointer_object 2\nx.cpp layer_header 1 <vector> in the layer X1\n"
                         "r x.cpp layer_header +1 <vector> in the layer X1\n", "planted")
    expect("a row keeps the spaces of its entity, and a kind with no entity has none",
           keyed.counts == {("x.cpp", "raw_pointer_object", ""): 2,
                            ("x.cpp", "layer_header", "<vector> in the layer X1"): 1}
           and keyed.expected("r")[("x.cpp", "layer_header", "<vector> in the layer X1")] == 2
           and ledger_text(keyed).endswith("x.cpp layer_header 1 <vector> in the layer X1\nx.cpp raw_pointer_object 2\n"
                                           "r x.cpp layer_header +1 <vector> in the layer X1\n"), ledger_text(keyed))
    old = parse_ledger("format 2\nconfiguration b machine x\nx.cpp std_object 3\n", "planted")
    expect("a format 2 ledger reads with an empty entity, for the history",
           old.format == 2 and old.counts == {("x.cpp", "std_object", ""): 3})
    expect("the entity of a row names what the plugin refuses, with no type of the code",
           [entity_key(kind, entity) for kind, entity in (
               ("std_object", "std::span (std::span<const Row>)"), ("c_library_object", "_IO_FILE (FILE*)"),
               ("raw_pointer_object", "const crucible::Expr*"), ("c_array_object", "int [2]"),
               ("raw_function_pointer", "void (*)()"), ("raw_new_delete", "placement new crucible::Node"),
               ("raw_new_delete", "new int"), ("raw_new_delete", "delete"),
               ("raw_new_delete", "declaration of operator new"), ("std_entity", "std::array::operator[]"),
               ("c_library_call", "memcpy"), ("layer_header", "<vector>  in the layer X1"))]
           == ["std::span", "_IO_FILE", "", "", "", "placement new", "new", "delete", "declaration of operator new",
               "std::array::operator[]", "memcpy", "<vector> in the layer X1"])

    with tempfile.TemporaryDirectory(prefix="quarantine-ratchet-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "first")
        status, _, text = scratch.run()
        expect("an empty ledger does not apply, and the line names the build and its tier",
               status == NOT_APPLICABLE and f"holds no rows of {BASE_NAME} on the tier x86-64-" in text, text)
        status, _, _ = scratch.run(write=True)
        expect("--write refuses a build that no configuration has", status == NOT_APPLICABLE)
        status, _, _ = scratch.run(write=True, add_configuration=True)
        written = read_ledger(scratch.ledger)
        expect("--add-configuration writes one row for each file, kind and entity of the base",
               status == 0 and written.counts == {("include/crucible/Vigil.h", "std_object", "std::vector"): 1,
                                                  ("include/fixy/session/Handle.h", "upward_include",
                                                   "include/crucible/X.h"): 1,
                                                  ("src/canopy/Lifeguard.cpp", "raw_pointer_object", ""): 1,
                                                  ("test/fixy/test_a.cpp", "c_library_call", "memcpy"): 1,
                                                  ("test/fixy/test_a.cpp", "opted_out",
                                                   f"{PROBE_REASON} | raw_pointer_object"): 1,
                                                  ("test/fixy/test_b.cpp", "std_object", "std::span"): 1,
                                                  ("test/test_arena.cpp", "std_entity", "std::swap"): 1},
               written.counts)
        base_keys = written.configurations.get(BASE_NAME, {})
        expect("--write keeps the head and writes the format and the configuration under the name of the kind",
               written.head == [HEAD.strip()] and written.format == FORMAT and written.base == BASE_NAME
               and base_keys.get("CMAKE_BUILD_TYPE") == "Debug" and base_keys.get(KIND_KEY) == BASE_NAME
               and base_keys.get(TIER_KEY, "").startswith("x86-64-"), written)
        status, findings, text = scratch.run()
        expect("an equal build passes, with one warning for each directory",
               status == 0 and all(item.level == "warning" for item in findings) and len(findings) == 5, text)
        expect("a header finding of two objects counts one time, and the region line is no finding",
               "7 findings in 7 rows of 6 files" in text, text)
        first_text = scratch.ledger.read_text()
        status, _, _ = scratch.run(write=True)
        expect("a second --write gives the same text", status == 0 and scratch.ledger.read_text() == first_text,
               scratch.ledger.read_text())

        # The opt-out regions: a region that covers more fails until --write --admit writes it.
        lines_a = scratch.objects["a"][0]
        region_rows = {key: count for key, count in read_ledger(scratch.ledger).counts.items() if key[1] == "opted_out"}
        scratch.add("a", [*lines_a, "quarantine: opted_out test/fixy/test_a.cpp:10:1 c_library_call memset"])
        memset_row = ("test/fixy/test_a.cpp", "opted_out", f"{PROBE_REASON} | c_library_call memset")
        expect("a region that covers one more finding fails, with the reason of the region in the row",
               scratch.errors() == [f"test/fixy/test_a.cpp has 1 findings of the kind opted_out and the entity "
                                    f"{memset_row[2]}, and the ledger admits 0"], scratch.errors())
        status, _, text = scratch.run(write=True)
        expect("--write refuses the growth of a region", status == 1 and "--admit" in text, text)
        status, _, text = scratch.run(write=True, admit=True)
        grown = read_ledger(scratch.ledger).counts
        expect("--write --admit writes the growth of a region, and the check passes",
               status == 0 and grown.get(memset_row) == 1 and scratch.errors() == [], text)
        scratch.add("a", [*lines_a, "quarantine: c_library_call test/fixy/test_a.cpp:4:9 memset"])
        status, _, text = scratch.run(write=True, admit=True)
        expect("--admit writes no rise of another kind", status == 1 and read_ledger(scratch.ledger).counts == grown,
               text)
        scratch.add("a", lines_a)
        expect("a region that covers less fails until the ledger is written again",
               any("opted_out" in error and "holds 1" in error for error in scratch.errors()), scratch.errors())
        status, _, _ = scratch.run(write=True)
        expect("--write writes the shrink of a region", status == 0 and scratch.errors() == [])
        scratch.add("a", [line.replace("test_a.cpp:9:1", "test_a.cpp:11:1").replace("test_a.cpp:8:1", "test_a.cpp:10:1")
                          for line in lines_a])
        expect("a region that moves with its code keeps its rows", scratch.errors() == [], scratch.errors())
        other = "ABI: a planted boundary"
        scratch.add("a", [*lines_a[:3], f"quarantine: region test/fixy/test_a.cpp:8:1 {other}", lines_a[4]])
        expect("a finding under a region of another reason fails: the row holds the reason of its region",
               sorted(scratch.errors()) == [f"test/fixy/test_a.cpp has 0 findings of the kind opted_out and the entity "
                                            f"{PROBE_REASON} | raw_pointer_object, and the ledger holds 1",
                                            f"test/fixy/test_a.cpp has 1 findings of the kind opted_out and the entity "
                                            f"{other} | raw_pointer_object, and the ledger admits 0"], scratch.errors())
        scratch.add("a", [*lines_a[:3], f"quarantine: region test/fixy/test_a.cpp:30:1 {PROBE_REASON}", lines_a[4]])
        status, findings, text = scratch.run()
        expect("an opted_out finding before each region of its file is an error, and --write refuses it",
               status == 1 and any(item.path == "test/fixy/test_a.cpp" and "no section gives a region" in item.message
                                   for item in findings) and scratch.run(write=True)[0] == 1, text)
        scratch.add("a", lines_a)
        expect("the ledger keeps the rows of the region", {key: count for key, count in
                                                           read_ledger(scratch.ledger).counts.items()
                                                           if key[1] == "opted_out"} == region_rows)

        # The hole of a count for each directory: one std object in, one std object out of the same directory.
        before = read_ledger(scratch.ledger).counts
        lines_a = scratch.objects["a"][0]
        scratch.add("a", [line for line in lines_a if "test_b.cpp" not in line]
                    + ["quarantine: std_object test/fixy/test_a.cpp:12:5 std::array (std::array<int, 2>)"])
        planted = scratch.counts()
        expect("the swap keeps each count of a directory and kind, so a count for each directory passes it",
               directory_totals(dict(planted), rows) == directory_totals(before, rows), planted)
        expect("the swap fails the count for each file: test_a rises and test_b falls",
               sorted(scratch.errors()) == ["test/fixy/test_a.cpp has 1 findings of the kind std_object and the entity "
                                            "std::array, and the ledger admits 0",
                                            "test/fixy/test_b.cpp has 0 findings of the kind std_object and the entity "
                                            "std::span, and the ledger holds 1"], scratch.errors())
        status, _, _ = scratch.run(write=True)
        expect("--write refuses the rise", status == 1 and read_ledger(scratch.ledger).counts == before)
        scratch.add("a", lines_a)

        # The hole of a count for each file: in one file, std::span goes and std::map comes.
        lines_a = scratch.objects["a"][0]
        scratch.add("a", [line.replace("std::span (std::span<int>)", "std::map (std::map<int, int>)")
                          for line in lines_a])
        planted = scratch.counts()
        expect("the entity swap keeps each count of a file and kind, so a count for each file passes it",
               file_totals(dict(planted)) == file_totals(before), planted)
        expect("the entity swap fails: std::map rises and std::span falls",
               sorted(scratch.errors()) == ["test/fixy/test_b.cpp has 0 findings of the kind std_object and the entity "
                                            "std::span, and the ledger holds 1",
                                            "test/fixy/test_b.cpp has 1 findings of the kind std_object and the entity "
                                            "std::map, and the ledger admits 0"], scratch.errors())
        first_row = read_ledger(scratch.ledger).line_of(BASE_NAME, ("test/fixy/test_b.cpp", "std_object", "std::span"))
        expect("the error of a new entity points at the first row of its file",
               any(item.line == first_row and "std::map" in item.message for item in scratch.run()[1]), first_row)
        status, _, text = scratch.run(write=True)
        expect("--write refuses the entity swap", status == 1 and read_ledger(scratch.ledger).counts == before
               and "std::map, rises from 0" in text, text)
        scratch.add("a", [line.replace("test_b.cpp:6:5 std::span (std::span<int>)",
                                       "test_b.cpp:40:9 std::span (std::span<const float>)")
                          .replace("test_a.cpp:4:3", "test_a.cpp:17:3") for line in lines_a])
        expect("a move of code and another type of the same library class keep each row", scratch.errors() == [],
               scratch.errors())
        scratch.add("a", lines_a)
        lines_b = scratch.objects["b"][0]
        scratch.add("b", [line.replace("char*", "const crucible::Expr*") for line in lines_b])
        expect("another pointee of a raw pointer keeps the row, because an alias can change that type",
               scratch.errors() == [], scratch.errors())
        scratch.add("b", lines_b)

        scratch.add("d", ["quarantine: std_object test/fixy/test_d.cpp:5:5 std::array (std::array<int, 2>)"])
        expect("a new file with one finding fails as a file that the ledger does not hold",
               scratch.errors() == ["test/fixy/test_d.cpp has 1 findings of the kind std_object and the entity "
                                    "std::array, and the ledger holds no row of the file"], scratch.errors())
        status, _, text = scratch.run(write=True)
        expect("--write refuses a new file with a finding", status == 1 and "does not hold" in text, text)
        del scratch.objects["d"]
        scratch.flush()

        scratch.add("c", [])
        status, findings, text = scratch.run()
        expect("the removal of a finding fails until the ledger is written again",
               status == 1 and any(item.level == "error" and "--write" in item.message for item in findings), text)
        status, _, _ = scratch.run(write=True)
        status, findings, text = scratch.run()
        expect("after --write the build passes again",
               status == 0
               and ("test/test_arena.cpp", "std_entity", "std::swap") not in read_ledger(scratch.ledger).counts, text)

        status, _, text = scratch.run(listed=["test/fixy", "c_library_call"])
        expect("--list prints the findings of a directory and kind",
               status == 0 and "quarantine: c_library_call test/fixy/test_a.cpp:4:3 memcpy" in text
               and "1 findings" in text, text)
        status, _, text = scratch.run(listed=["test/fixy/test_b.cpp"])
        expect("--list prints the findings of a file", status == 0 and "test_b.cpp:6:5" in text
               and "1 findings" in text, text)

        # A git move takes the rows of the old path.
        scratch.git("mv", "test/fixy/test_b.cpp", "test/fixy/test_c.cpp")
        lines_moved = [line.replace("test_b.cpp", "test_c.cpp") for line in scratch.objects["a"][0]]
        scratch.add("a", lines_moved)
        expect("a moved file fails until the ledger is written again", len(scratch.errors()) == 2, scratch.errors())
        status, _, text = scratch.run(write=True)
        expect("--write moves the rows of a file that git reports as moved",
               status == 0 and ("test/fixy/test_c.cpp", "std_object", "std::span") in read_ledger(scratch.ledger).counts
               and scratch.run()[0] == 0, text)

        # A second configuration holds differences from the base.
        scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
        status, _, text = scratch.run()
        expect("a build of another configuration does not apply, and one line gives its tier and the tiers of the "
               "ledger", status == NOT_APPLICABLE and f"holds no rows of {RELEASE_NAME} on the tier znver" not in text
               and f"holds no rows of {RELEASE_NAME} on the tier x86-64-" in text
               and f"It holds {BASE_NAME} on x86-64-" in text, text)
        (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
        door = "<sys/socket.h> has the door include/fixy/os/Socket.h"
        # Only the second configuration writes the generated file.  The tree holds no file test_gone.cpp.
        generated = (f"{layer_rules.BUILD_PREFIX}gen/table.c", "c_array_object", "")
        gone = ("test/fixy/test_gone.cpp", "std_entity", "std::swap")
        lines_e = ["quarantine: compiler_builtin include/crucible/Vigil.h:30:7 __builtin_ia32_pause",
                   f"quarantine: door_header include/fixy/session/Handle.h:5:0 {door}",
                   f"quarantine: c_array_object {generated[0]}:3:1 int [4]"]
        scratch.add("e", [*lines_e, f"quarantine: std_entity {gone[0]}:2:1 std::swap"])
        status, _, _ = scratch.run(write=True, add_configuration=True)
        release = read_ledger(scratch.ledger)
        expect("--add-configuration writes the differences of a second configuration",
               status == 0 and release.deltas.get(RELEASE_NAME)
               == {("include/crucible/Vigil.h", "compiler_builtin", "__builtin_ia32_pause"): 1,
                   ("include/fixy/session/Handle.h", "door_header", door): 1, generated: 1, gone: 1}
               and release.configurations[RELEASE_NAME][TIER_KEY].startswith("znver5-"), release.deltas)
        expect("the second configuration passes", scratch.run()[0] == 0)
        (scratch.compiler.parent / "native.txt").write_text("znver3\n", encoding="utf-8")
        status, _, text = scratch.run()
        expect("a host of another native tier does not apply, and the line names the two tiers",
               status == NOT_APPLICABLE and f"holds no rows of {RELEASE_NAME} on the tier znver3-" in text
               and f"{RELEASE_NAME} on znver5-" in text and "tier is znver3-" in text, text)
        (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
        scratch.configure(BASE_NAME, "Debug")
        del scratch.objects["e"]
        lines_a = scratch.objects["a"][0]
        scratch.add("a", [line for line in lines_a if "memcpy" not in line])
        status, _, _ = scratch.run(write=True)
        lowered = read_ledger(scratch.ledger).deltas.get(RELEASE_NAME, {})
        expect("a change that lowers a count in each configuration writes only the base rows",
               status == 0 and {key: delta for key, delta in lowered.items() if key[0] in SOURCES}
               == {key: delta for key, delta in release.deltas[RELEASE_NAME].items() if key[0] in SOURCES}, lowered)
        expect("a --write of the base removes the difference row of a source file that the tree does not hold",
               gone not in lowered, lowered)
        expect("a --write of the base keeps the difference row of a file that a build of the configuration writes",
               lowered.get(generated) == 1, lowered)
        scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
        scratch.add("e", lines_e)
        expect("the second configuration still passes after the base rows fell", scratch.run()[0] == 0,
               scratch.run()[2])
        scratch.add("g", ["quarantine: std_entity include/crucible/Vigil.h:31:7 std::swap"])
        status, _, text = scratch.run()
        expect("a failure of a later configuration prints its rows for an import",
               status == 1 and f"{ROW_MARK}{RELEASE_NAME} include/crucible/Vigil.h std_entity +1 std::swap" in text,
               text)
        del scratch.objects["g"]
        lines_e = lines_e[:2]
        scratch.add("e", lines_e)
        expect("a build of the configuration that does not write the generated file fails on its row",
               scratch.errors() == [f"{generated[0]} has 0 findings of the kind c_array_object, and the ledger holds "
                                    f"1"], scratch.errors())
        status, _, text = scratch.run(write=True)
        expect("a --write of that configuration removes the row, and the build then passes",
               status == 0 and generated not in read_ledger(scratch.ledger).deltas.get(RELEASE_NAME, {})
               and scratch.errors() == [], text)

        # A configuration that only CI builds: the check prints its rows, and they go through a log.
        scratch.configure("aarch64-debug-asan", "Debug")
        status, _, text = scratch.run()
        expect("a build that the ledger does not hold prints its configuration and its differences",
               status == NOT_APPLICABLE and f"{ROW_MARK}configuration aarch64-debug-asan kind aarch64-debug-asan" in text
               and f"{ROW_MARK}aarch64-debug-asan include/crucible/Vigil.h compiler_builtin +1 __builtin_ia32_pause"
               in text and f"{ROW_MARK}aarch64-debug-asan include/fixy/session/Handle.h door_header +1 {door}" in text,
               text)
        log = scratch.root / "ci.log"
        log.write_text("".join(f"build+test aarch64\tQuarantine ratchet\t2026-10-02T21:00:00Z {line}\n"
                               for line in text.splitlines()), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = import_rows(scratch.ledger, log)
        expect("--import puts the rows of a log into the ledger, and the build then passes",
               status == 0 and "aarch64-debug-asan" in read_ledger(scratch.ledger).configurations
               and scratch.run()[0] == 0, scratch.run()[2])
        log.write_text(log.read_text() + f"x\t{ROW_MARK}aarch64-debug-asan include/crucible/Vigil.h compiler_builtin "
                                         f"+3 __builtin_ia32_pause\n", encoding="utf-8")
        before_import = scratch.ledger.read_text()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = import_rows(scratch.ledger, log)
        expect("--import refuses two values for one row", status == 1 and scratch.ledger.read_text() == before_import)
        scratch.configure(BASE_NAME, "Debug")
        del scratch.objects["e"]
        scratch.add("g", ["quarantine: std_entity include/crucible/Vigil.h:31:7 std::swap"])
        status, _, text = scratch.run()
        expect("a failure of the base prints no rows, because --write writes the base", status == 1
               and ROW_MARK not in text, text)
        del scratch.objects["g"]
        scratch.flush()

        # A bench target adds -march=native to its own compiles, so its arms depend on the host.
        bench_name = f"{BASE_NAME}-bench"
        scratch.configure(BASE_NAME, "Debug", extra="CRUCIBLE_BENCH:BOOL=ON\n")
        scratch.flags["h"] = "-O3 -march=native"
        scratch.add("h", ["quarantine: compiler_builtin include/crucible/Vigil.h:40:7 __builtin_ia32_vpcmpb512"])
        status, _, _ = scratch.run(write=True, add_configuration=True)
        bench_tier = read_ledger(scratch.ledger).configurations.get(bench_name, {}).get(TIER_KEY, "")
        expect("a compile with its own native flag adds the native tier of the host to the tier",
               status == 0 and bench_tier.startswith("x86-64-") and "+znver5-" in bench_tier, bench_tier)
        (scratch.compiler.parent / "native.txt").write_text("znver3\n", encoding="utf-8")
        scratch.add("h", [])
        status, _, text = scratch.run()
        expect("a host of another tier, with other arms in the bench compile, skips with one line and does not fail "
               "on a count", status == NOT_APPLICABLE and f"holds no rows of {bench_name} on the tier x86-64-" in text
               and "+znver3-" in text and f"{bench_name} on {bench_tier}" in text, text)
        (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
        del scratch.objects["h"]
        del scratch.flags["h"]
        scratch.configure(BASE_NAME, "Debug")
        scratch.flush()

        scratch.add("f", ["quarantine: std_entity test/test_f.cpp:1:1 std::swap"], stamp="ffff")
        status, findings, text = scratch.run()
        expect("a section with another stamp is an error",
               status == 1 and any("compiler cache" in item.message for item in findings), text)
        scratch.add("f", [], has_section=False)
        status, findings, text = scratch.run()
        expect("an object with no section is an error",
               status == 1 and any("holds no section" in item.message for item in findings), text)
        del scratch.objects["f"]
        scratch.flush()

        rows_json = json.loads((scratch.build / "compile_commands.json").read_text())
        for row in rows_json:
            row["command"] = row["command"].replace(PLUGIN_ARGUMENT, "-fplugin-arg-crucible_contract-")
        (scratch.build / "compile_commands.json").write_text(json.dumps(rows_json), encoding="utf-8")
        status, _, text = scratch.run()
        expect("a build whose compiles do not load the quarantine plugin does not apply",
               status == NOT_APPLICABLE and "no compile" in text, text)
        scratch.flush()

        (scratch.build / "b.o").unlink()
        status, _, text = scratch.run()
        expect("a build that made only some targets does not apply", status == NOT_APPLICABLE, text)
        scratch.flush()

        good = scratch.ledger.read_text()
        scratch.ledger.write_text(good.replace("test/fixy/test_c.cpp std_object 1", "test/fixy/test_c.cpp std_object x"),
                                  encoding="utf-8")
        status, findings, _ = scratch.run()
        expect("a malformed row is an error", status == 1 and any("COUNT" in item.message for item in findings))
        scratch.ledger.write_text(HEAD + "configuration machine x86_64\ntest dir_kind 3\n", encoding="utf-8")
        status, findings, _ = scratch.run()
        expect("a format 1 ledger is an error with the way to generate it again",
               status == 1 and any("format 1" in item.message for item in findings))
        scratch.ledger.write_text(HEAD + f"format 2\nconfiguration {BASE_NAME} kind {BASE_NAME}\n"
                                         f"test/fixy/test_c.cpp std_object 1\n", encoding="utf-8")
        status, findings, _ = scratch.run()
        expect("a format 2 ledger with no entity is an error with the way to generate it again",
               status == 1 and any("format 2" in item.message and "--add-configuration" in item.message
                                   for item in findings))

    if failures:
        print(f"check-quarantine-ratchet --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 2
    print("check-quarantine-ratchet --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check, the writer, the list or the self-test."""
    parser = argparse.ArgumentParser(prog="guard check --rule quarantine-ratchet",
                                     description=__doc__.split("\n", 1)[0])
    parser.add_argument("--build-dir", type=Path, help="the build directory whose objects the check reads")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the source root")
    parser.add_argument("--write", action="store_true", help="write the rows of the configuration of the build")
    parser.add_argument("--raise", dest="allow_raise", action="store_true",
                        help="with --write, also write a count above its row")
    parser.add_argument("--admit", action="store_true",
                        help="with --write, also write a count of opted_out above its row: a region that covers more")
    parser.add_argument("--add-configuration", dest="add_configuration", action="store_true",
                        help="with --write, write the configuration of the build under its name")
    parser.add_argument("--list", dest="listed", nargs="*", metavar="WORD",
                        help="print the findings of FILE or DIRECTORY, and of KIND")
    parser.add_argument("--import", dest="import_path", type=Path, metavar="LOG",
                        help="put the rows of a configuration that the check printed into the ledger, from a log")
    parser.add_argument("--self-test", action="store_true", help="run the self-test")
    check_report.add_arguments(parser)
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    root = args.root.resolve()
    if args.import_path is not None:
        return import_rows(root / LEDGER, args.import_path)
    if (args.build_dir is None or (args.listed is not None and len(args.listed) > 2)
            or ((args.allow_raise or args.admit or args.add_configuration) and not args.write)):
        parser.print_usage(sys.stderr)
        return 2
    throwaway_repo.isolate()
    return run(args.build_dir.resolve(), root, root / LEDGER, root / RULES, args.warnings_dir,
               Request(args.write, args.allow_raise, args.add_configuration, args.listed, args.admit))
