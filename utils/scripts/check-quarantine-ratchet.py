#!/usr/bin/env python3
"""check-quarantine-ratchet — the count of quarantine findings in each directory and kind can only fall.

THE FINDINGS
    The quarantine plugin of utils/tools/quarantine/ writes the findings of
    each unit into the section .crucible.quarantine of its object
    (utils/scripts/quarantine_sections.py).  The check reads the section of
    each object of the target all of one build (utils/scripts/build_census.py,
    objects_of_all) whose compile loads the quarantine plugin.  A finding is
    one distinct finding line.  The same line from two objects, as a finding
    of a header that two units include, counts one time.  A finding of the
    kind opted_out is not counted, and a line of the kind region is no
    finding: utils/scripts/check-quarantine-regions.py and its ledger hold
    each opt-out region.

THE DIRECTORY OF A FINDING
    The directory rows of the rule table utils/scripts/layer-rules.txt are
    the paths of its layer rows and quarantine rows that end with '/'.  The
    directory of a file is the longest directory row that holds the file, and
    the first directory under that row.  So test/fixy/test_x.cpp counts in
    test/fixy, src/canopy/Lifeguard.cpp in src/canopy, include/crucible/Vigil.h
    in include/crucible and include/fixy/session/Handle.h in
    include/fixy/session.

THE LEDGER
    utils/scripts/quarantine-ledger.txt holds one row for each directory and
    kind with a finding: "DIRECTORY KIND COUNT".  It also holds the
    configuration of the build that counts: "configuration NAME VALUE" for
    the target machine and for each cache variable of CONFIGURATION.  A
    different configuration compiles other objects or other preprocessor arms,
    so its counts are different.

THE VERDICT
    * A count above its ledger row is an error.  A change can only lower a
      count, as rule R11 of misc/01_10_2026_quarantine.md says.
    * A count below its ledger row is an error too.  The commit that removes
      a finding writes the ledger again (--write), so the ledger keeps no
      slack.
    * Each directory of the ledger gives one warning, so the findings that
      stay in the quarantine are in the output of each run.
    * An object whose compile loads the plugin and that holds no section, and
      a section whose stamp is not the stamp of the compile command of its
      object, are errors.  The stamp holds the plugin and the rule table, so a
      compiler cache that does not hash it gives the findings of another
      plugin or another table.

WRITE THE LEDGER
    --write writes the counts of the build into the ledger.  It keeps the
    head comment and the configuration rows, and it refuses a build of a
    different configuration.  It refuses a count above its row: remove the
    new finding.  --raise writes such a count too.  Use it only when the
    plugin or the rule table changed what the plugin reports, and give the
    reason in the commit.

NOT APPLICABLE
    The check exits 3, with the reason, when the build made only some targets
    or its generator is not Ninja, when no compile of the target all loads the
    quarantine plugin, or when the configuration of the build is not the
    configuration of the ledger.

Usage
    check-quarantine-ratchet.py --build-dir DIR [--warnings-dir DIR]
    check-quarantine-ratchet.py --build-dir DIR --write [--raise]
    check-quarantine-ratchet.py --build-dir DIR --list [DIRECTORY [KIND]]
    check-quarantine-ratchet.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage error
or a failed self-test, 3 when the check does not apply to the build.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import platform
import re
import sys
import tempfile
from collections import Counter
from collections.abc import Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_census  # noqa: E402
import check_report  # noqa: E402
import layer_rules  # noqa: E402
import quarantine_sections  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECK = "quarantine-ratchet"
LEDGER = "utils/scripts/quarantine-ledger.txt"
RULES = "utils/scripts/layer-rules.txt"
NOT_APPLICABLE = 3
CONFIGURATION_ROW = "configuration"
MACHINE = "machine"
# The cache variables that change the objects of all or their preprocessor arms.
CONFIGURATION = ("CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "CRUCIBLE_SANITIZE", "CRUCIBLE_TSAN",
                 "CRUCIBLE_UBSAN_STRICT", "CRUCIBLE_ANALYZER", "CRUCIBLE_VERIFY", "CRUCIBLE_PGO", "CRUCIBLE_BENCH",
                 "CRUCIBLE_FUZZ", "CRUCIBLE_EXAMPLES", "CRUCIBLE_HAVE_BPF", "CRUCIBLE_SENSE_HUB_EXTENDED",
                 "CRUCIBLE_VALGRIND_INCLUDE", "TORCH_DIR")
PLUGIN_ARGUMENT = "-fplugin-arg-crucible_quarantine-"
STAMP = re.compile(re.escape(PLUGIN_ARGUMENT) + r"stamp=(\S*)")
CMAKE_FALSE = {"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"}
READERS = 16


class LedgerError(ValueError):
    """A row of the ledger does not have the format."""


@dataclass(slots=True)
class Ledger:
    """The rows of the ledger: the configuration, the count of each directory and kind, and the line of each row."""

    head: list[str] = field(default_factory=list)
    configuration: dict[str, str] = field(default_factory=dict)
    counts: dict[tuple[str, str], int] = field(default_factory=dict)
    lines: dict[tuple[str, str], int] = field(default_factory=dict)


@dataclass(slots=True)
class Census:
    """The distinct findings of the objects of all, and the problems of the objects."""

    findings: set[quarantine_sections.Finding] = field(default_factory=set)
    objects: int = 0
    problems: list[check_report.Finding] = field(default_factory=list)


# ── The ledger ─────────────────────────────────────────────────────


def read_ledger(path: Path) -> Ledger:
    """Read the ledger.  The leading comment lines are its head.

    Raises:
        LedgerError: If a row does not have the format, or a row has a second copy
        OSError: If the file cannot be read
    """
    ledger = Ledger()
    is_head = True
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            if is_head:
                ledger.head.append(raw)
            continue
        is_head = False
        words = stripped.split()
        if words[0] == CONFIGURATION_ROW:
            if len(words) != 3 or words[1] in ledger.configuration:
                raise LedgerError(f"{path}:{number}: a configuration row is 'configuration NAME VALUE', one for each "
                                  f"name")
            ledger.configuration[words[1]] = words[2]
            continue
        if len(words) != 3 or not words[2].isdigit() or int(words[2]) == 0:
            raise LedgerError(f"{path}:{number}: a row is 'DIRECTORY KIND COUNT', with a COUNT above 0")
        key = (words[0], words[1])
        if key in ledger.counts:
            raise LedgerError(f"{path}:{number}: the directory {key[0]} and the kind {key[1]} have a second row")
        ledger.counts[key] = int(words[2])
        ledger.lines[key] = number
    return ledger


def ledger_text(head: list[str], configuration: dict[str, str], counts: dict[tuple[str, str], int]) -> str:
    """Return the text of a ledger with the head, the configuration rows and one row for each count above 0."""
    lines = [*head]
    lines += [f"{CONFIGURATION_ROW} {name} {value}" for name, value in configuration.items()]
    lines += [f"{directory} {kind} {count}" for (directory, kind), count in sorted(counts.items()) if count > 0]
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


def build_configuration(build_dir: Path) -> dict[str, str]:
    """Return the configuration of a build: the machine and each name of CONFIGURATION, normalized."""
    entries = cache_entries(build_dir)
    configuration = {MACHINE: platform.machine()}
    for name in CONFIGURATION:
        kind, value = entries.get(name, ("STRING", ""))
        configuration[name] = normalized(kind, value).replace(" ", "_")
    return configuration


def configuration_difference(build: dict[str, str], ledger: dict[str, str]) -> list[str]:
    """Return a text for each name whose value in the build is not its value in the ledger."""
    return [f"{name} is {build.get(name, 'absent')} in the build and {ledger.get(name, 'absent')} in the ledger"
            for name in sorted(set(build) | set(ledger)) if build.get(name) != ledger.get(name)]


# ── The findings of the build ──────────────────────────────────────


def plugin_objects(build_dir: Path, root: Path) -> list[tuple[Path, str]]:
    """Return each object of all whose compile loads the quarantine plugin, with the stamp of its command.

    Raises:
        build_census.NotApplicable: If the build made only some targets, or its generator is not Ninja
        build_census.CensusError: If the compile database or the inputs of all cannot be read
    """
    ninja, _ = build_census.tools_of(build_dir)
    objects = {str(path) for path, _ in build_census.objects_of_all(build_dir, ninja, root)}
    rows = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    found: list[tuple[Path, str]] = []
    for row in rows:
        output = os.path.normpath(os.path.join(row["directory"], row.get("output", "")))
        command = row.get("command") or " ".join(row.get("arguments", []))
        stamp = STAMP.search(command)
        if output in objects and stamp is not None:
            found.append((Path(output), stamp[1]))
    return sorted(found)


def read_census(objects: list[tuple[Path, str]], build_dir: Path) -> Census:
    """Read the section of each object, and check its stamp against the stamp of its command.

    Complexity: linear in the number of sections and finding lines of the objects.
    """
    census = Census(objects=len(objects))

    def read_one(item: tuple[Path, str]) -> tuple[Path, str, quarantine_sections.Section | None, str]:
        path, stamp = item
        try:
            data = quarantine_sections.read_section(path)
            return path, stamp, None if data is None else quarantine_sections.parse_section(data, str(path)), ""
        except (OSError, quarantine_sections.SectionError) as problem:
            return path, stamp, None, str(problem)

    with ThreadPoolExecutor(max_workers=READERS) as pool:
        results = list(pool.map(read_one, objects))
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
            census.findings.update(item for item in section.findings
                                   if item.kind not in quarantine_sections.NOT_FINDINGS)
    return census


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
    parent = rel.rsplit("/", 1)[0] if "/" in rel else "."
    return parent


def count_findings(findings: Iterable[quarantine_sections.Finding], rows: list[str]) -> Counter[tuple[str, str]]:
    """Return the number of findings of each directory and kind."""
    counts: Counter[tuple[str, str]] = Counter()
    directories: dict[str, str] = {}
    for item in findings:
        directory = directories.get(item.file)
        if directory is None:
            directory = directories[item.file] = directory_of(item.file, rows)
        counts[(directory, item.kind)] += 1
    return counts


# ── The verdict ────────────────────────────────────────────────────


def evaluate(counts: Counter[tuple[str, str]], ledger: Ledger, ledger_shown: str,
             build_shown: str) -> list[check_report.Finding]:
    """Compare each count with its ledger row, and give one warning for each directory that keeps its rows."""
    findings: list[check_report.Finding] = []
    failed: set[str] = set()
    write = f"python3 utils/scripts/check-quarantine-ratchet.py --build-dir {build_shown} --write"
    for key in sorted(set(counts) | set(ledger.counts)):
        directory, kind = key
        measured, admitted = counts.get(key, 0), ledger.counts.get(key, 0)
        line = ledger.lines.get(key, 0)
        if measured != admitted:
            failed.add(directory)
        if measured > admitted:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{directory} has {measured} findings of the kind {kind}, and the ledger admits {admitted}.  The "
                f"change adds {measured - admitted}, and a change can only lower a count.  List them with "
                f"`python3 utils/scripts/check-quarantine-ratchet.py --build-dir {build_shown} --list {directory} "
                f"{kind}`, and use a type of fixy or foundation"))
        elif measured < admitted:
            findings.append(check_report.Finding(
                "error", ledger_shown, line, CHECK,
                f"{directory} has {measured} findings of the kind {kind}, and the ledger row holds {admitted}.  "
                f"Write the ledger again in the same commit: `{write}`"))
    kept: dict[str, list[tuple[str, int]]] = {}
    for (directory, kind), admitted in sorted(ledger.counts.items()):
        if directory not in failed:
            kept.setdefault(directory, []).append((kind, admitted))
    for directory, parts in kept.items():
        total = sum(admitted for _, admitted in parts)
        shown = ", ".join(f"{kind} {admitted}" for kind, admitted in parts)
        findings.append(check_report.Finding("warning", ledger_shown, ledger.lines[(directory, parts[0][0])], CHECK,
                                             f"{directory} keeps {total} quarantine findings: {shown}"))
    return findings


# ── The command ────────────────────────────────────────────────────


def run(build_dir: Path, root: Path, ledger_path: Path, rules_path: Path, warnings_dir: Path | None,
        write: bool = False, allow_raise: bool = False, listed: list[str] | None = None) -> int:
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
    configuration = build_configuration(build_dir)
    difference = configuration_difference(configuration, ledger.configuration) if ledger.configuration else []
    if difference:
        print(f"{CHECK}: the ledger counts a build of another configuration: {'; '.join(difference)}.  The check "
              f"does not apply")
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
    census = read_census(objects, build_dir)
    rows = directory_rows(table)
    counts = count_findings(census.findings, rows)
    if listed is not None:
        selected = sorted(item for item in census.findings
                          if (not listed or directory_of(item.file, rows) == listed[0])
                          and (len(listed) < 2 or item.kind == listed[1]))
        for item in selected:
            print(item.text())
        print(f"{CHECK}: {len(selected)} findings")
        return 0
    if write:
        return write_ledger(ledger_path, ledger, configuration, counts, census, allow_raise, warnings_dir)
    findings = census.problems + evaluate(counts, ledger, ledger_shown, build_shown)
    status = check_report.emit(findings, CHECK, warnings_dir)
    directories = {directory for directory, _ in counts}
    print(f"{CHECK}: {sum(counts.values())} findings in {len(counts)} rows of {len(directories)} directories, from "
          f"{census.objects} objects")
    return status


def write_ledger(ledger_path: Path, ledger: Ledger, configuration: dict[str, str], counts: Counter[tuple[str, str]],
                 census: Census, allow_raise: bool, warnings_dir: Path | None) -> int:
    """Write the counts into the ledger, unless an object has a problem or a count rose and --raise is absent."""
    if census.problems:
        print(f"{CHECK}: the ledger is not written, because an object has a problem", file=sys.stderr)
        return check_report.emit(census.problems, CHECK, warnings_dir)
    rises = sorted((key, count, ledger.counts.get(key, 0)) for key, count in counts.items()
                   if count > ledger.counts.get(key, 0))
    for (directory, kind), count, admitted in rises:
        print(f"{CHECK}: {directory} {kind} rises from {admitted} to {count}", file=sys.stderr)
    if rises and not allow_raise:
        print(f"{CHECK}: the ledger is not written: {len(rises)} counts rose.  Remove the new findings.  Give "
              f"--raise only when the plugin or the rule table changed what it reports", file=sys.stderr)
        return 1
    ledger_path.write_text(ledger_text(ledger.head, ledger.configuration or configuration, dict(counts)),
                           encoding="utf-8")
    print(f"{CHECK}: wrote {sum(1 for count in counts.values() if count > 0)} rows, {sum(counts.values())} findings, "
          f"to {ledger_path}")
    return 0


# ── The self-test ──────────────────────────────────────────────────


FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / "inputs.txt").read_text())
"""
RULES_TEXT = ("layer base 0 include/fixy/ include/fixy/session/\n"
              "quarantine include/crucible/\nquarantine src/\nquarantine test/\n")
HEAD = "# a planted ledger\n"
STAMP_VALUE = "0123abcd"


class Scratch:
    """A scratch build with a fake ninja, a compile database, objects with sections, a rule table and a ledger."""

    def __init__(self, root: Path) -> None:
        """Make the tree with three objects."""
        self.root = root
        self.build = root / "build"
        self.build.mkdir(parents=True)
        self.ledger = root / "quarantine-ledger.txt"
        self.ledger.write_text(HEAD, encoding="utf-8")
        self.rules = root / "layer-rules.txt"
        self.rules.write_text(RULES_TEXT, encoding="utf-8")
        ninja = root / "ninja.py"
        ninja.write_text(f"#!{sys.executable}\n{FAKE_NINJA}", encoding="utf-8")
        ninja.chmod(0o755)
        self.cache = f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_MAKE_PROGRAM:FILEPATH={ninja}\nCMAKE_BUILD_TYPE:STRING=Debug\n"
        (self.build / "CMakeCache.txt").write_text(self.cache, encoding="utf-8")
        self.objects: dict[str, tuple[list[str], str, bool]] = {}
        header = "quarantine: std_object include/crucible/Vigil.h:10:5 std::vector (std::vector<int>)"
        self.add("a", [header, "quarantine: c_library_call test/fixy/test_a.cpp:4:3 memcpy",
                       "quarantine: opted_out test/fixy/test_a.cpp:9:1 raw_pointer_object char*"])
        self.add("b", [header, "quarantine: raw_pointer_object src/canopy/Lifeguard.cpp:7:9 char*",
                       "quarantine: upward_include include/fixy/session/Handle.h:3:0 include/crucible/X.h"])
        self.add("c", ["quarantine: std_entity test/test_arena.cpp:2:1 std::swap"])

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
            (self.build / f"{name}.o").write_bytes(quarantine_sections.make_object(sections))
            rows.append({"directory": str(self.build), "file": str(self.root / f"{name}.cpp"), "output": f"{name}.o",
                         "command": f"g++ -fplugin=q.so {PLUGIN_ARGUMENT}stamp={STAMP_VALUE} -c {name}.cpp"})
        (self.build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")
        (self.build / "inputs.txt").write_text("".join(f"{name}.o\n" for name in self.objects), encoding="utf-8")

    def run(self, write: bool = False, allow_raise: bool = False,
            listed: list[str] | None = None) -> tuple[int, list[check_report.Finding], str]:
        """Run the check on the scratch build, and return the status, the findings and the printed text."""
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()):
            status = run(self.build, self.root, self.ledger, self.rules, None, write, allow_raise, listed)
        text = printed.getvalue()
        return status, [found for found in map(check_report.parse_line, text.splitlines()) if found], text


def self_test() -> int:
    """Do a test of the check on planted builds, with negative controls.

    Returns:
        0 when each case holds, else 2
    """
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

    with tempfile.TemporaryDirectory(prefix="quarantine-ratchet-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "first")
        status, _, _ = scratch.run(write=True)
        expect("--write refuses the first counts without --raise", status == 1 and scratch.ledger.read_text() == HEAD)
        status, _, _ = scratch.run(write=True, allow_raise=True)
        written = read_ledger(scratch.ledger)
        expect("--write --raise writes one row for each directory and kind",
               status == 0 and written.counts == {("include/crucible", "std_object"): 1,
                                                  ("include/fixy/session", "upward_include"): 1,
                                                  ("src/canopy", "raw_pointer_object"): 1,
                                                  ("test", "std_entity"): 1, ("test/fixy", "c_library_call"): 1},
               written.counts)
        expect("--write keeps the head and writes the configuration",
               written.head == [HEAD.strip()] and written.configuration.get("CMAKE_BUILD_TYPE") == "Debug"
               and written.configuration.get(MACHINE) == platform.machine(), written)
        status, findings, text = scratch.run()
        expect("an equal build passes, with one warning for each directory",
               status == 0 and all(item.level == "warning" for item in findings) and len(findings) == 5, text)
        expect("a header finding of two objects counts one time, and opted_out is not counted",
               "5 findings in 5 rows" in text, text)

        scratch.add("d", ["quarantine: std_object test/fixy/test_d.cpp:5:5 std::array (std::array<int, 2>)"])
        status, findings, text = scratch.run()
        expect("a planted std object raises one count, and the check fails",
               status == 1 and [item.message.split(".")[0] for item in findings if item.level == "error"]
               == ["test/fixy has 1 findings of the kind std_object, and the ledger admits 0"], text)
        status, _, _ = scratch.run(write=True)
        expect("--write refuses the rise",
               status == 1 and ("test/fixy", "std_object") not in read_ledger(scratch.ledger).counts)
        del scratch.objects["d"]
        scratch.flush()

        scratch.add("c", [])
        status, findings, text = scratch.run()
        expect("the removal of a finding fails until the ledger is written again",
               status == 1 and any(item.level == "error" and "--write" in item.message for item in findings), text)
        status, _, _ = scratch.run(write=True)
        status, findings, text = scratch.run()
        expect("after --write the build passes again", status == 0 and ("test", "std_entity")
               not in read_ledger(scratch.ledger).counts, text)

        status, _, text = scratch.run(listed=["test/fixy", "c_library_call"])
        expect("--list prints the findings of a directory and kind",
               status == 0 and "quarantine: c_library_call test/fixy/test_a.cpp:4:3 memcpy" in text
               and "1 findings" in text, text)

        scratch.add("e", ["quarantine: std_entity test/test_e.cpp:1:1 std::swap"], stamp="ffff")
        status, findings, text = scratch.run()
        expect("a section with another stamp is an error",
               status == 1 and any("compiler cache" in item.message for item in findings), text)
        scratch.add("e", [], has_section=False)
        status, findings, text = scratch.run()
        expect("an object with no section is an error",
               status == 1 and any("holds no section" in item.message for item in findings), text)
        del scratch.objects["e"]
        scratch.flush()

        (scratch.build / "CMakeCache.txt").write_text(scratch.cache.replace("Debug", "Release"), encoding="utf-8")
        status, _, text = scratch.run()
        expect("a build of another configuration does not apply",
               status == NOT_APPLICABLE and "CMAKE_BUILD_TYPE is Release in the build and Debug" in text, text)
        (scratch.build / "CMakeCache.txt").write_text(scratch.cache, encoding="utf-8")

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

        scratch.ledger.write_text(HEAD + "test/fixy c_library_call x\n", encoding="utf-8")
        status, findings, _ = scratch.run()
        expect("a malformed row is an error", status == 1 and any("COUNT" in item.message for item in findings))

    if failures:
        print(f"check-quarantine-ratchet --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 2
    print("check-quarantine-ratchet --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check, the writer, the list or the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--build-dir", type=Path, help="the build directory whose objects the check reads")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the source root")
    parser.add_argument("--write", action="store_true", help="write the counts of the build into the ledger")
    parser.add_argument("--raise", dest="allow_raise", action="store_true",
                        help="with --write, also write a count above its row")
    parser.add_argument("--list", dest="listed", nargs="*", metavar="WORD",
                        help="print the findings of DIRECTORY and of KIND")
    parser.add_argument("--self-test", action="store_true", help="run the self-test")
    check_report.add_arguments(parser)
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.build_dir is None or (args.listed is not None and len(args.listed) > 2) or (args.allow_raise
                                                                                          and not args.write):
        parser.print_usage(sys.stderr)
        return 2
    root = args.root.resolve()
    return run(args.build_dir.resolve(), root, root / LEDGER, root / RULES, args.warnings_dir, args.write,
               args.allow_raise, args.listed)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
