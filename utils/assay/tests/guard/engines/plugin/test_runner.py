"""The test of the runner of the engine plugin and of its rules quarantine and contract-specifier, on planted builds.

THE PLANTED BUILD
    Scratch makes a throwaway git repository.  It holds each source file of
    the findings, a fake ninja, a fake ctest and a CMake cache.  It also holds
    a compile database, objects with sections and a ledger.  The fake ctest
    lists the planted negative fixtures, and a case can write the record of
    the run of a fixture with its census.  A case changes the finding lines
    of an object or the census of a fixture.  Then it runs the rule, writes
    the ledger or lists the findings, as `guard check`, `guard write` and
    `guard list` do.  A complete write gives the write the planted build two
    times, as a reduce of the landing gate gives it a build of each
    configuration.

THE CASES
    The cases hold each behavior of the ledger of the rule quarantine:
      * The key of a directory and a kind
      * The census of the fixtures: a build with no fixture run and a parse
        build, a new finding in a directory with no row, the source root of
        a census, a kind of a probe and its reason, the problems of the
        records, a file that only the compiles of fixtures read, and a
        reduce with no fixture run
      * The opted-out findings and the regions, with the reason in the key
      * A replaced finding of the same kind in one directory, which passes,
        and a count above the row of its directory, which fails
      * One set of rows for the builds of two kinds, and an arm of one build
      * A new directory, a file that git moves in its directory, and a file
        that git moves to a different directory, which needs --raise
      * A base header that git moves to a different layer
      * An equal build with no warning, and the problems of the objects
      * Each mode of the write, the write of one build and the complete write.
    The cases of the rule contract-specifier hold its errors and the problems
    of the census.
"""

import contextlib
import io
import json
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import git as throwaway_repo
from assay.core import ledger as rule_ledger
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable, testing
from assay.guard.engines.plugin import counted, fixtures, quarantine, runner
from assay.readers import cmake, compdb, elf
from assay.readers import sections as quarantine_sections

RULE = quarantine.RULE
CONTRACT = "contract-specifier"
REGISTRY_ROWS = (
    "rule quarantine engine=plugin scope=quarantined ledger=count doc=XXII.R11 | a planted quarantine rule | lower it",
    "rule contract-specifier engine=plugin scope=tracked ledger=none doc=XII | a planted contract rule | use a macro",
)
# The fake ninja prints the planted dependency log for `-t deps`, and the planted inputs of all for each other tool.
FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / ("deps.txt" if "deps" in sys.argv else "inputs.txt")).read_text())
"""
# The fake ctest prints the planted test list of the directory of --test-dir.
FAKE_CTEST = """\
import sys
from pathlib import Path
directory = Path(sys.argv[sys.argv.index("--test-dir") + 1])
sys.stdout.write((directory / "tests.json").read_text())
"""
HEAD = "# a planted ledger\n"
EMPTY_LEDGER = f"{HEAD}format 1\nrule {RULE}\n"
STAMP_VALUE = "0123abcd"
PROBE_REASON = "PROBE: a planted region"
BASE_NAME = "x86_64-debug-asan"
RELEASE_NAME = "x86_64-release"
SOURCES = (
    "include/crucible/Vigil.h",
    "test/fixy/test_a.cpp",
    "test/fixy/test_b.cpp",
    "src/canopy/Lifeguard.cpp",
    "include/fixy/session/Handle.h",
    "test/test_arena.cpp",
)
IDENTITY = {
    "GIT_AUTHOR_NAME": "crucible-selftest",
    "GIT_AUTHOR_EMAIL": "crucible-selftest@invalid",
    "GIT_COMMITTER_NAME": "crucible-selftest",
    "GIT_COMMITTER_EMAIL": "crucible-selftest@invalid",
}

type Row = tuple[str, str, str]


RULES = rule_registry.planted_rules(REGISTRY_ROWS)


class Scratch:
    """A planted build with a fake ninja, a fake ctest, a compile database, objects with sections and a ledger.

    The root is a throwaway git repository with each source file of the findings.  A move is then a git move.
    """

    def __init__(self, root: Path) -> None:
        """Make the tree with three objects and no fixture."""
        self.root = root
        self.build = root / "build"
        self.build.mkdir(parents=True)
        throwaway_repo.init(root)
        for rel in SOURCES:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(f"// {rel}\n", encoding="utf-8")
        (root / ".gitignore").write_text("build/\n", encoding="utf-8")
        self.git("add", "-A", "--", ".")
        self.git("commit", "-q", "-m", "Add the planted sources")
        self.ledger = rule_ledger.ledger_path(RULE, root)
        self.ledger.parent.mkdir(parents=True)
        self.ledger.write_text(EMPTY_LEDGER, encoding="utf-8")
        self.ninja = root / "ninja.py"
        self.ninja.write_text(f"#!{sys.executable}\n{FAKE_NINJA}", encoding="utf-8")
        self.ninja.chmod(0o755)
        self.ctest = root / "ctest.py"
        self.ctest.write_text(f"#!{sys.executable}\n{FAKE_CTEST}", encoding="utf-8")
        self.ctest.chmod(0o755)
        self.configure(BASE_NAME, "Debug")
        self.objects: dict[str, tuple[list[str], str, bool]] = {}
        self.fixtures: dict[str, tuple[str, str]] = {}
        header = "quarantine std-object include/crucible/Vigil.h:10:5 std::vector"
        self.add(
            "a",
            [
                header,
                "quarantine c-library-call test/fixy/test_a.cpp:4:3 memcpy",
                "quarantine std-object test/fixy/test_b.cpp:6:5 std::span",
                f"quarantine region test/fixy/test_a.cpp:8:1 {PROBE_REASON}",
                "quarantine opted-out test/fixy/test_a.cpp:9:1 raw-pointer-object bytes",
            ],
        )
        self.add(
            "b",
            [
                header,
                "quarantine raw-pointer-object src/canopy/Lifeguard.cpp:7:9 buffer",
                "quarantine upward-include include/fixy/session/Handle.h:3:0 include/crucible/X.h",
            ],
        )
        self.add("c", ["quarantine std-entity test/test_arena.cpp:2:1 std::swap"])

    def configure(self, kind: str, build_type: str, flags: str = "", is_parse: bool = False) -> None:
        """Write the build kind and the reply of the CMake cache of one configuration of the planted build.

        With `is_parse`, the cache sets the option of a parse build (assay.core.configuration.is_parse_build).
        """
        (self.build / "build-kind.txt").write_text(f"{kind}\n", encoding="utf-8")
        cmake.plant(
            self.build,
            cache={
                "CMAKE_GENERATOR": "Ninja",
                "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(self.ninja)),
                "CMAKE_CTEST_COMMAND": ("FILEPATH", str(self.ctest)),
                "CMAKE_BUILD_TYPE": ("STRING", build_type),
                "CMAKE_CXX_FLAGS": ("STRING", flags),
                **({build_config.SYNTAX_OPTION: ("BOOL", "ON")} if is_parse else {}),
            },
        )

    def git(self, *arguments: str) -> None:
        """Run one git command in the planted repository, with the identity of the test."""
        throwaway_repo.run(self.root, *arguments, extra=IDENTITY)

    def add(self, name: str, lines: list[str], stamp: str = STAMP_VALUE, has_section: bool = True) -> None:
        """Plant one object of all with its finding lines, or with no section."""
        self.objects[name] = (lines, stamp, has_section)
        self.flush()

    def drop(self, name: str) -> None:
        """Remove one object of all."""
        del self.objects[name]
        self.flush()

    def add_fixture(self, name: str, source: str, stamp: str = STAMP_VALUE) -> None:
        """Register one negative fixture of the build and write its source, with a command that loads the plugin."""
        (self.root / source).parent.mkdir(parents=True, exist_ok=True)
        (self.root / source).write_text(f"// {source}\n", encoding="utf-8")
        self.fixtures[name] = (source, stamp)
        self.flush()

    def record(self, name: str, census: list[cost_record.CensusFinding], stamp: str = STAMP_VALUE) -> None:
        """Write the record of a run of one fixture, with its census and the stamp of the census."""
        source, _ = self.fixtures[name]
        path = Path(cost_record.fixture_record_file(self.build, name))
        path.parent.mkdir(parents=True, exist_ok=True)
        cost_record.write(
            path, cost_record.FixtureRecord(name, "compiled", 0.1, 0.0, None, (str(self.root / source),), stamp, census)
        )

    def flush(self) -> None:
        """Write each object, the compile database, the inputs of all and the test list of the fixtures."""
        tests = [
            {
                "name": name,
                "command": [
                    sys.executable,
                    str(self.root / "utils/bin/loop"),
                    "fixture",
                    str(self.build),
                    str(self.root / source),
                    name,
                ],
            }
            for name, (source, _) in self.fixtures.items()
        ]
        (self.build / "tests.json").write_text(json.dumps({"kind": "ctestInfo", "tests": tests}), encoding="utf-8")
        rows = [
            {
                "directory": str(self.build),
                "file": str(self.root / source),
                "output": f"neg/{name}.o",
                "command": f"g++ -fplugin=q.so {runner.STAMP_ARGUMENT}{stamp} {fixtures.ROOT_ARGUMENT}{self.root} "
                f"-c {source}",
            }
            for name, (source, stamp) in self.fixtures.items()
        ]
        for name, (lines, stamp, has_section) in self.objects.items():
            text = "".join(f"{line}\n" for line in [f"{quarantine_sections.HEAD_PREFIX}2 stamp={stamp}", *lines])
            sections = [elf.Planted(".text", b"\x90")] + (
                [elf.Planted(quarantine_sections.SECTION_NAME, text.encode())] if has_section else []
            )
            (self.build / f"{name}.o").write_bytes(elf.make_file(sections))
            rows.append(
                {
                    "directory": str(self.build),
                    "file": str(self.root / f"{name}.cpp"),
                    "output": f"{name}.o",
                    "command": f"g++ -fplugin=q.so {runner.STAMP_ARGUMENT}{STAMP_VALUE} -c {name}.cpp",
                }
            )
        compdb.plant(self.build, rows)
        (self.build / "inputs.txt").write_text("".join(f"{name}.o\n" for name in self.objects), encoding="utf-8")
        header = self.root / SOURCES[0]
        (self.build / "deps.txt").write_text(
            "".join(f"{name}.o: #deps 1, deps mtime 1 (VALID)\n    {header}\n\n" for name in self.objects),
            encoding="utf-8",
        )

    def run(self, rule: str = RULE) -> tuple[int, list[check_report.Finding], str]:
        """Run one rule on the planted build, and return the status, the findings and the written text."""
        with (
            check_report.github_actions(False),
            contextlib.redirect_stdout(io.StringIO()) as printed,
            contextlib.redirect_stderr(io.StringIO()) as said,
        ):
            status = runner.run([RULES[rule]], self.root, self.build, None)[0]
        found = [item for item in map(check_report.parse_line, printed.getvalue().splitlines()) if item]
        return status, found, printed.getvalue() + said.getvalue()

    def write(
        self,
        mode: rule_ledger.WriteMode = rule_ledger.WriteMode.LOWER,
        reason: str | None = None,
        is_complete: bool = False,
    ) -> tuple[int, str]:
        """Write the ledger of the rule quarantine from the planted build, and return the status and the text.

        With `is_complete`, the write gets the planted build two times, and it is a complete write that lowers the rows
        (THE REDUCE of assay.core.ledger).
        """
        request = runner.WriteRequest(self.root, self.build, mode, reason, (self.build,) if is_complete else ())
        with (
            check_report.github_actions(False),
            contextlib.redirect_stdout(io.StringIO()) as printed,
            contextlib.redirect_stderr(io.StringIO()) as said,
        ):
            status = runner.write(RULES[RULE], request)
        return status, printed.getvalue() + said.getvalue()

    def listed(self, place: str | None, kind: str | None = None) -> tuple[int, str]:
        """List the findings of the planted build, and return the status and the text."""
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = runner.print_findings(self.root, self.build, place, kind)
        return status, printed.getvalue()

    def errors(self, rule: str = RULE) -> list[str]:
        """Return the first sentence of each error of a run, sorted."""
        _, found, _ = self.run(rule)
        return sorted(item.message.split(".  ")[0] for item in found if item.level == "error")

    def counts(self) -> dict[Row, int]:
        """Return the count of each key of the planted objects: the directory, the kind and the reason of a region."""
        return as_tuples(counted.counts_of(quarantine.findings(runner.PluginContext(self.root, self.build))))

    def ledger_rows(self) -> rule_ledger.Ledger:
        """Read the planted ledger."""
        return rule_ledger.read(RULES[RULE], self.root)

    def base(self) -> dict[Row, int]:
        """Return the count of each row of the planted ledger."""
        return as_tuples({key: int(entry.value) for key, entry in self.ledger_rows().base.items()})


def as_tuples(counts: Mapping[rule_ledger.Key, int]) -> dict[Row, int]:
    """Return the counts with each key as the tuple (path, kind, entity)."""
    return {(key.path, key.kind, key.entity): count for key, count in counts.items()}


# ── The functions ──────────────────────────────────────────────────


def unit_cases(expect: testing.Expect) -> None:
    """Hold the directory of a file, the rows of a directory, the entity of each kind and the stamp."""
    for rel, wanted in (
        ("test/fixy/test_a.cpp", "test/fixy/"),
        ("include/fixy/os/Time.h", "include/fixy/os/"),
        ("@build/table.c", "@build/"),
        ("@build/test/fuzz/generated/g.h", "@build/test/fuzz/generated/"),
        ("a.cpp", quarantine.ROOT_DIRECTORY),
    ):
        expect(f"the directory of {rel} is {wanted}", quarantine.directory_of(rel) == wanted)
    keyed = rule_ledger.parse(
        f"{HEAD}format 1\nrule {RULE}\nreason test/ | a planted step of the plan\n"
        "test/fixy/ | raw-pointer-object | 2 | - | -\n"
        "test/fixy/ | opted-out | 1 | PROBE: a region | -\n"
        "test/fixy/ | region | 1 | PROBE: a region | -\n",
        "planted",
        RULE,
        RULES[RULE].policy,
    )
    expect(
        "a row of a directory reads, a kind with no entity has the empty cell, and a reason row of a prefix holds it",
        as_tuples({key: int(entry.value) for key, entry in keyed.base.items()})
        == {
            ("test/fixy/", "raw-pointer-object", ""): 2,
            ("test/fixy/", "opted-out", "PROBE: a region"): 1,
            ("test/fixy/", "region", "PROBE: a region"): 1,
        }
        and keyed.idle_reasons() == [],
    )
    doors = ruletable.parse(
        "layer os 0 src/foundation/\ndoor <sys/socket.h> src/foundation/os/A.cpp\n\ndoor <linux/types.h> "
        "src/foundation/os/B.cpp\n"
    )
    expect(
        "door_lines gives the line of each door row, in the sequence of the door rows",
        doors.door_lines == (2, 4) and len(doors.doors) == 2,
    )
    starts = [(4, "PROBE: one"), (9, "ABI: two")]
    expect(
        "reason_at() gives the reason of the region with the last first line at or before a line, and None before "
        "each region",
        (
            quarantine.reason_at(starts, 4),
            quarantine.reason_at(starts, 8),
            quarantine.reason_at(starts, 30),
            quarantine.reason_at(starts, 3),
            quarantine.reason_at([], 5),
        )
        == ("PROBE: one", "PROBE: one", "ABI: two", None, None),
    )
    expect(
        "the stamp of a compile command is the value of the plugin argument stamp=",
        (
            runner.stamp_of(f"g++ {runner.STAMP_ARGUMENT}00ab -c a.cpp"),
            runner.stamp_of(f"g++ {runner.STAMP_ARGUMENT}"),
            runner.stamp_of(f"g++ {runner.STAMP_ARGUMENT} -c a.cpp"),
            runner.stamp_of("g++ -c a.cpp"),
        )
        == ("00ab", "", "", None),
    )


def census_cases(expect: testing.Expect) -> None:
    """Hold the census of planted sections: each line one time, the regions, the problems and the rules."""
    text = (
        f"{quarantine_sections.HEAD_PREFIX}2 stamp=s\n"
        "quarantine std-entity a.cpp:3:1 std::swap\n"
        "contract-specifier contract-specifier a.cpp:5:1 pre\n"
        "quarantine region a.cpp:9:1 PROBE:  a   region\n"
        "quarantine opted-out a.cpp:10:1 std-entity std::swap\n"
    )
    section = quarantine_sections.parse_section(text.encode(), "a.o")
    census = runner.census_of(
        [
            runner.ObjectSection("a.o", "s", section),
            runner.ObjectSection("b.o", "s", section),
            runner.ObjectSection("c.o", "s", None),
            runner.ObjectSection("d.o", "t", section),
            runner.ObjectSection("e.o", "s", None, "a planted read problem"),
        ]
    )
    expect(
        "a line of two objects counts one time, and a region line is no finding",
        len(census.findings) == 3 and census.objects == 5,
    )
    expect(
        "the census keeps the first line and the reason of each region, with one space between the words",
        dict(census.regions) == {"a.cpp": ((9, "PROBE: a region"),)},
    )
    expect(
        "an object with no section, a section of a different stamp and a read problem are problems",
        [problem.path for problem in census.problems] == ["c.o", "d.o", "e.o"]
        and "holds no section" in census.problems[0].message
        and "stamp 's'" in census.problems[1].message
        and census.problems[2].message == "a planted read problem",
    )
    expect(
        "the findings of each rule are apart",
        [item.kind for item in census.of_rule(CONTRACT)] == ["contract-specifier"]
        and [item.kind for item in census.of_rule(RULE)] == ["std-entity", "opted-out"],
    )
    expect(
        "each problem is an error of each rule",
        [item.level for item in census.errors(CONTRACT)] == ["error"] * 3
        and all(item.check == CONTRACT for item in census.errors(CONTRACT)),
    )
    context = runner.PluginContext(Path("/planted"), None, census)
    counts = as_tuples(counted.counts_of(quarantine.findings(context)))
    expect(
        "a planted census gives the counts of the rule quarantine for each directory and kind, and the reason of a "
        "region in the key of a region row and of an opted-out row",
        counts
        == {
            (quarantine.ROOT_DIRECTORY, "std-entity", ""): 1,
            (quarantine.ROOT_DIRECTORY, "region", "PROBE: a region"): 1,
            (quarantine.ROOT_DIRECTORY, "opted-out", "PROBE: a region"): 1,
        },
    )
    no_build = runner.PluginContext(Path("/planted"))
    try:
        no_build.census()
        is_refused = False
    except runner.NotApplicable as reason:
        is_refused = "--build-dir" in str(reason)
    expect("a context with no build directory does not apply", is_refused)


def parsed_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the read of the report of one unit of a parse build: a match, a missing report and a different stamp."""
    build = scratch.root / "parse-build"
    object_path = build / "CMakeFiles" / "t.dir" / "a.cpp.o"
    missing = runner.read_parsed(object_path, "s", build)
    expect(
        "a unit with no report is a problem that names loop syntax",
        missing.section is None and "loop syntax" in missing.problem,
    )
    directory = quarantine_sections.report_directory(build, object_path)
    directory.mkdir(parents=True)
    (directory / f"a.cpp.0000000000000000{quarantine_sections.REPORT_SUFFIX}").write_text(
        "# unit a.cpp\nquarantine std-entity a.cpp:3:1 std::swap\n", encoding="utf-8"
    )
    (directory / quarantine_sections.STAMP_FILE).write_text("s\n", encoding="utf-8")
    matched = runner.read_parsed(object_path, "s", build)
    expect(
        "a report with the stamp of the compile command gives its findings",
        matched.problem == "" and matched.section is not None and len(matched.section.findings) == 1,
    )
    other = runner.read_parsed(object_path, "t", build)
    expect(
        "a report of another stamp is a problem that names loop syntax",
        other.section is None and "stamp 's'" in other.problem and "loop syntax" in other.problem,
    )
    census = runner.census_of([matched, other])
    expect(
        "the census of a parse build counts a report as a section",
        len(census.findings) == 1 and len(census.problems) == 1,
    )


# ── The rule quarantine ────────────────────────────────────────────


def first_write_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the first write of an empty ledger, the equal build and the second write."""
    status, _, text = scratch.run()
    expect("an empty ledger fails each finding", status == 1 and "holds no rows" not in text)
    empty_text = scratch.ledger.read_text(encoding="utf-8")
    status, _ = scratch.write()
    expect(
        "a write of one build refuses each new key with no --raise",
        status == 1 and scratch.ledger.read_text(encoding="utf-8") == empty_text,
    )
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    written = scratch.ledger_rows()
    expect(
        "--raise writes one row for each directory and kind of the build, and the regions of a reason have the "
        "reason in their key",
        status == 0
        and scratch.base()
        == {
            ("include/crucible/", "std-object", ""): 1,
            ("include/fixy/session/", "upward-include", ""): 1,
            ("src/canopy/", "raw-pointer-object", ""): 1,
            ("test/fixy/", "c-library-call", ""): 1,
            ("test/fixy/", "opted-out", PROBE_REASON): 1,
            ("test/fixy/", "region", PROBE_REASON): 1,
            ("test/fixy/", "std-object", ""): 1,
            ("test/", "std-entity", ""): 1,
        },
    )
    expect(
        "the write gives the ledger the head comment of the rule in place of the planted head",
        written.comment == quarantine.LEDGER_COMMENT != HEAD,
    )
    status, found, text = scratch.run()
    expect("an equal build gives no finding: a row gives no warning", status == 0 and found == [])
    expect(
        "a header finding of two objects counts one time, and the region counts apart",
        "7 findings and 1 regions in 8 rows of 5 directories" in text,
    )
    first_text = scratch.ledger.read_text(encoding="utf-8")
    status, _ = scratch.write()
    expect(
        "a second write gives the same text", status == 0 and scratch.ledger.read_text(encoding="utf-8") == first_text
    )


def region_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the opt-out regions: a region that covers more, a new region, a moved region and a region that goes."""
    lines_a = scratch.objects["a"][0]
    region_rows = {key: count for key, count in scratch.base().items() if key[1] in quarantine.ADMITTED_KINDS}
    scratch.add("a", [*lines_a, "quarantine opted-out test/fixy/test_a.cpp:10:1 c-library-call memset"])
    opted_row = ("test/fixy/", "opted-out", PROBE_REASON)
    expect(
        "a region that covers one more finding fails, with the reason of the region in the key",
        scratch.errors()
        == [
            (f"test/fixy/ has 2 findings in opt-out regions with the reason {PROBE_REASON!r}, and the ledger admits 1")
        ],
    )
    status, text = scratch.write()
    expect("the write refuses the growth of a region", status == 1 and "--admit" in text)
    status, text = scratch.write(rule_ledger.WriteMode.ADMIT)
    grown = scratch.base()
    expect(
        "--admit writes the growth of a region, and the rule then gives no error",
        status == 0 and grown.get(opted_row) == 2 and scratch.errors() == [],
    )
    scratch.add("a", [*lines_a, "quarantine c-library-call test/fixy/test_a.cpp:4:9 memset"])
    status, _ = scratch.write(rule_ledger.WriteMode.ADMIT)
    expect("--admit writes no rise of a different kind", status == 1 and scratch.base() == grown)
    scratch.add("a", lines_a)
    expect(
        "a region that covers less passes, because a ledger fails only on a growth",
        scratch.errors() == [],
    )
    status, _ = scratch.write()
    expect(
        "the write of one build keeps the row of a region that covers less",
        status == 0 and scratch.base() == grown and scratch.errors() == [],
    )
    status, _ = scratch.write(is_complete=True)
    expect(
        "a complete write writes the shrink of a region",
        status == 0 and scratch.base().get(opted_row) == 1 and scratch.errors() == [],
    )
    scratch.add(
        "a",
        [
            line.replace("test_a.cpp:9:1", "test_a.cpp:11:1").replace("test_a.cpp:8:1", "test_a.cpp:10:1")
            for line in lines_a
        ],
    )
    expect("a region that moves with its code keeps its rows", scratch.errors() == [])
    other = "ABI: a planted boundary"
    scratch.add("a", [*lines_a[:3], f"quarantine region test/fixy/test_a.cpp:8:1 {other}", lines_a[4]])
    expect(
        "a region of a different reason fails: each row holds the reason of its region, and the rows of the earlier "
        "reason pass",
        scratch.errors()
        == sorted(
            [
                f"test/fixy/ has 1 findings in opt-out regions with the reason {other!r}, and the ledger admits 0",
                f"test/fixy/ has 1 opt-out regions with the reason {other!r}, and the ledger admits 0",
            ]
        ),
    )
    scratch.add("a", [*lines_a[:3], f"quarantine region test/fixy/test_a.cpp:30:1 {PROBE_REASON}", lines_a[4]])
    status, found, _ = scratch.run()
    expect(
        "an opted-out finding before each region of its file is an error, and the write refuses it",
        status == 1
        and any(
            item.path == "test/fixy/test_a.cpp"
            and "no section and no census of a fixture gives a region" in item.message
            for item in found
        )
        and scratch.write()[0] == 1,
    )
    second = "quarantine region test/fixy/test_a.cpp:20:1 PROBE: a planted region"
    scratch.add("a", [*lines_a, second])
    expect(
        "a second region of a reason is a new region, which the rule refuses",
        scratch.errors()
        == [(f"test/fixy/ has 2 opt-out regions with the reason {PROBE_REASON!r}, and the ledger admits 1")],
    )
    status, text = scratch.write()
    expect("the write refuses a new region", status == 1 and "--admit" in text)
    status, _ = scratch.write(rule_ledger.WriteMode.ADMIT, reason="PROBE: a planted reason")
    expect(
        "--admit writes a new region",
        status == 0 and scratch.base().get(("test/fixy/", "region", PROBE_REASON)) == 2,
    )
    scratch.add("a", lines_a)
    expect(
        "a row that admits more regions than the file has gives no error",
        scratch.errors() == [],
    )
    status, _ = scratch.write(is_complete=True)
    expect(
        "a complete write lowers the count of the regions, and the ledger keeps the rows of each region",
        status == 0
        and {key: count for key, count in scratch.base().items() if key[1] in quarantine.ADMITTED_KINDS} == region_rows,
    )
    status, listed = scratch.listed("test/fixy/", quarantine.REGION)
    expect(
        "the list gives the region lines of the files of one directory",
        status == 0
        and f"quarantine region test/fixy/test_a.cpp:8:1 {PROBE_REASON}" in listed
        and "1 findings" in listed,
    )


def directory_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the key of a directory and kind: the loss that the owner accepts, a rise, and a finding of a new kind."""
    before = scratch.base()
    lines_a = scratch.objects["a"][0]
    scratch.add(
        "a",
        [line for line in lines_a if "test_b.cpp" not in line]
        + ["quarantine std-object test/fixy/test_a.cpp:12:5 std::array"],
    )
    expect(
        "a finding that replaces a finding of the same kind in a different file of the directory passes: the key "
        "holds no file",
        scratch.counts() == scratch.base() == before and scratch.errors() == [],
    )
    scratch.add("a", [line.replace("test_b.cpp:6:5 std::span", "test_b.cpp:6:5 std::map") for line in lines_a])
    expect(
        "a finding that replaces a finding of the same kind and a different entity passes: the key holds no entity",
        scratch.errors() == [],
    )
    scratch.add("a", [*lines_a, "quarantine std-object test/fixy/test_a.cpp:12:5 std::array"])
    expect(
        "one more finding of a kind in a directory fails, and its error names the directory",
        scratch.errors() == ["test/fixy/ has 2 findings of the kind std-object, and the ledger admits 1"],
    )
    row_line = scratch.ledger_rows().line_of(rule_ledger.Key("test/fixy/", "std-object", ""))
    expect(
        "the error of a rise points at the row of its directory and kind",
        any(item.line == row_line and "std-object" in item.message for item in scratch.run()[1]),
    )
    status, text = scratch.write()
    expect("the write refuses the rise", status == 1 and scratch.base() == before and "rises from 1" in text)
    scratch.add(
        "a",
        [line for line in lines_a if "memcpy" not in line]
        + ["quarantine std-object test/fixy/test_a.cpp:4:3 std::array"],
    )
    expect(
        "a finding of a different kind does not replace a finding that goes: the fall of c-library-call pays no "
        "std-object",
        scratch.errors() == ["test/fixy/ has 2 findings of the kind std-object, and the ledger admits 1"],
    )
    scratch.add(
        "a",
        [
            line.replace("test_b.cpp:6:5 std::span", "test_b.cpp:40:9 std::span").replace(
                "test_a.cpp:4:3", "test_a.cpp:17:3"
            )
            for line in lines_a
        ],
    )
    expect("a move of code keeps each row", scratch.errors() == [])
    scratch.add("a", lines_a)
    lines_b = scratch.objects["b"][0]
    scratch.add("b", [line.replace("Lifeguard.cpp:7:9 buffer", "Lifeguard.cpp:7:9 storage") for line in lines_b])
    expect(
        "a raw object of a different name keeps the row, because the row of a raw object holds no entity",
        scratch.errors() == [],
    )
    scratch.add("b", lines_b)


def file_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a new directory, a finding that goes, the list of the findings and the files that git moves."""
    scratch.add("d", ["quarantine std-object test/other/test_d.cpp:5:5 std::array"])
    expect(
        "a finding in a new directory fails as a directory that the ledger does not hold",
        scratch.errors()
        == ["test/other/ has 1 findings of the kind std-object, and the ledger holds no row of the directory"],
    )
    status, text = scratch.write()
    expect("the write refuses a new directory with a finding", status == 1 and "does not hold" in text)
    scratch.drop("d")
    scratch.add("c", [])
    status, found, _ = scratch.run()
    expect(
        "the removal of a finding passes, because a ledger fails only on a growth",
        status == 0 and not any(item.level == "error" for item in found),
    )
    scratch.write()
    expect(
        "the write of one build keeps the row of the finding that goes",
        ("test/", "std-entity", "") in scratch.base(),
    )
    scratch.write(is_complete=True)
    status, _, _ = scratch.run()
    expect(
        "after a complete write the build gives no error again, and the row of the finding that goes is gone",
        status == 0 and ("test/", "std-entity", "") not in scratch.base(),
    )
    status, text = scratch.listed("test/fixy", "c-library-call")
    expect(
        "the list gives the findings of a directory and kind, with the file, the line and the entity",
        status == 0 and "quarantine c-library-call test/fixy/test_a.cpp:4:3 memcpy" in text and "1 findings" in text,
    )
    status, text = scratch.listed("test/fixy/test_b.cpp")
    expect("the list gives the findings of a file", status == 0 and "test_b.cpp:6:5" in text and "1 findings" in text)
    status, outer = scratch.listed("test")
    status_one, one = scratch.listed("test/")
    expect(
        "a place with no final slash lists the directories under it too, and a place with a final slash lists only "
        "the files of that one directory",
        status == status_one == 0
        and "test/fixy/test_a.cpp" in outer
        and "test/fixy/" not in one
        and "0 findings" in one,
    )
    scratch.git("mv", "test/fixy/test_b.cpp", "test/fixy/test_c.cpp")
    scratch.add("a", [line.replace("test_b.cpp", "test_c.cpp") for line in scratch.objects["a"][0]])
    status, _ = scratch.write()
    expect(
        "a file that git moves in its directory keeps the row of the directory, and the write needs no --raise",
        scratch.errors() == [] and status == 0,
    )
    (scratch.root / "test" / "moved").mkdir()
    scratch.git("mv", "test/fixy/test_c.cpp", "test/moved/test_c.cpp")
    scratch.add(
        "a", [line.replace("test/fixy/test_c.cpp", "test/moved/test_c.cpp") for line in scratch.objects["a"][0]]
    )
    expect(
        "a file that git moves to a different directory fails there, because the write reads no git move",
        scratch.errors()
        == ["test/moved/ has 1 findings of the kind std-object, and the ledger holds no row of the directory"],
    )
    status, _ = scratch.write()
    expect("the write refuses the new directory with no --raise", status == 1)
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "--raise writes the row of the new directory, the build then gives no error, and the write of one build "
        "keeps the row of the directory that the finding left",
        status == 0
        and scratch.base().get(("test/moved/", "std-object", "")) == 1
        and scratch.base().get(("test/fixy/", "std-object", "")) == 1
        and scratch.run()[0] == 0,
    )


def layer_move_cases(expect: testing.Expect) -> None:
    """Move a base header to a different layer: its include keeps no row, and a new include increases a count."""
    with tempfile.TemporaryDirectory(prefix="plugin-layer-move-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree")
        old, new = "include/fixy/os/Spawn.h", "include/fixy/concurrent/Spawn.h"
        old_row, new_row = ("include/fixy/os/", "layer-header", ""), ("include/fixy/concurrent/", "layer-header", "")
        (scratch.root / old).parent.mkdir(parents=True)
        (scratch.root / old).write_text(f"// {old}\n", encoding="utf-8")
        scratch.git("add", "--", old)
        scratch.git("commit", "-q", "-m", "Add a planted base header")
        scratch.add("e", [f"quarantine layer-header {old}:5:1 <tuple> in the layer X3_os"])
        status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
        expect(
            "the row of an include holds its directory and kind, and no header and no layer",
            status == 0 and scratch.base().get(old_row) == 1,
        )
        (scratch.root / new).parent.mkdir(parents=True)
        scratch.git("mv", old, new)
        scratch.add("e", [f"quarantine layer-header {new}:5:1 <tuple> in the layer X4_concurrent"])
        expect(
            "a header moved to a different directory fails at its new directory until the ledger is written again",
            len(scratch.errors()) == 1,
        )
        status, _ = scratch.write(rule_ledger.WriteMode.RAISE, is_complete=True)
        expect(
            "a complete write with --raise gives the new directory the row of the include, and the build then gives "
            "no error",
            status == 0
            and scratch.base().get(new_row) == 1
            and old_row not in scratch.base()
            and scratch.run()[0] == 0,
        )
        scratch.add(
            "e",
            [
                f"quarantine layer-header {new}:5:1 <tuple> in the layer X4_concurrent",
                f"quarantine layer-header {new}:6:1 <vector> in the layer X4_concurrent",
            ],
        )
        status, text = scratch.write()
        expect(
            "a new include of the moved header increases a count: the run fails and the write refuses it",
            len(scratch.errors()) == 1 and status == 1 and "rises from 1" in text,
        )


def build_kind_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold one set of rows for the builds of two kinds: an arm of one build, a file of the build, the reduce."""
    scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
    status, _, text = scratch.run()
    expect("the one set of rows judges a build of a different kind", status == 0 and "holds no rows" not in text)
    swap = ("include/crucible/", "std-entity", "")
    scratch.add("g", ["quarantine std-entity include/crucible/Vigil.h:31:7 std::swap"])
    before_write = scratch.ledger.read_text(encoding="utf-8")
    status, written_text = scratch.write()
    swap_error = "include/crucible/ has 1 findings of the kind std-entity, and the ledger admits 0"
    expect(
        "a finding of an arm that only that build compiles fails it, and the write refuses it with no --raise",
        scratch.errors() == [swap_error]
        and status == 1
        and "rises from 0" in written_text
        and scratch.ledger.read_text(encoding="utf-8") == before_write,
    )
    # Only the release build writes the file of the build directory.
    generated = (f"{ruletable.BUILD_PREFIX}gen/", "c-array-object", "")
    scratch.add("e", [f"quarantine c-array-object {ruletable.BUILD_PREFIX}gen/table.c:3:1 int [4]"])
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "--raise adds the arm and the file of the build to the one set, and the build then gives no error",
        status == 0 and scratch.base().get(swap) == 1 and scratch.base().get(generated) == 1 and scratch.run()[0] == 0,
    )
    scratch.configure(BASE_NAME, "Debug")
    scratch.drop("g")
    scratch.drop("e")
    expect("a build that does not compile the arm passes with its row", scratch.errors() == [])
    status, _ = scratch.write()
    expect(
        "the write of that build keeps the rows of the arm and of the file of the build, because one build cannot "
        "see a different build",
        status == 0 and scratch.base().get(swap) == 1 and scratch.base().get(generated) == 1,
    )
    status, _ = scratch.write(is_complete=True)
    expect(
        "a complete write over builds that do not give the arm removes its rows, and the builds give no error",
        status == 0 and swap not in scratch.base() and generated not in scratch.base() and scratch.errors() == [],
    )


def problem_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the problems of the objects and the builds that do not apply."""
    scratch.add("f", ["quarantine std-entity test/test_f.cpp:1:1 std::swap"], stamp="ffff")
    status, found, _ = scratch.run()
    expect(
        "a section with a different stamp is an error",
        status == 1 and any("compiler cache" in item.message for item in found),
    )
    status, found, _ = scratch.run(CONTRACT)
    expect(
        "a section with a different stamp is an error of the rule contract-specifier too",
        status == 1 and any("compiler cache" in item.message for item in found),
    )
    scratch.add("f", [], has_section=False)
    status, found, _ = scratch.run()
    expect(
        "an object with no section is an error",
        status == 1 and any("holds no section" in item.message for item in found),
    )
    expect("the write refuses a build with a problem object", scratch.write()[0] == 1)
    scratch.drop("f")
    compdb.plant(
        scratch.build,
        [
            {
                "directory": entry.directory,
                "file": entry.file,
                "output": entry.output,
                "command": entry.command.replace(runner.PLUGIN_ARGUMENT, "-fplugin-arg-crucible_contract-"),
            }
            for entry in compdb.entries(scratch.build)
        ],
    )
    status, _, text = scratch.run()
    expect(
        "a build whose compiles do not load the quarantine plugin does not apply",
        status == runner.SKIPPED and "no compile" in text,
    )
    scratch.flush()
    (scratch.build / "b.o").unlink()
    status, _, _ = scratch.run()
    expect("a build that made only some targets does not apply", status == runner.SKIPPED)
    scratch.flush()
    (scratch.build / "build-kind.txt").unlink()
    status, _, text = scratch.run(CONTRACT)
    expect("a build with no build kind does not apply", status == runner.SKIPPED and "build-kind.txt" in text)
    scratch.configure(BASE_NAME, "Debug")


def ledger_problem_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a ledger that does not obey its schema, and the format of the output."""
    status, found, text = scratch.run()
    expect(
        "each line of a run has the line format, after the line of the counts",
        status == 0
        and all(check_report.parse_line(line) is not None for line in text.splitlines()[:-1])
        and len(found) == len(text.splitlines()) - 1,
    )
    good = scratch.ledger.read_text(encoding="utf-8")
    malformed = good.replace("test/fixy/ | c-library-call | 1 |", "test/fixy/ | c-library-call | x |")
    scratch.ledger.write_text(malformed, encoding="utf-8")
    bad_line = next((number for number, line in enumerate(malformed.splitlines(), start=1) if "| x |" in line), -1)
    status, found, _ = scratch.run()
    expect(
        "a malformed row is an error at its line",
        malformed != good
        and status == 1
        and any("count 'x'" in item.message and item.line == bad_line for item in found),
    )
    scratch.ledger.write_text(
        HEAD + f"format 4\nconfiguration {BASE_NAME} kind {BASE_NAME}\ntest/fixy/test_c.cpp std-object 1 std::span\n",
        encoding="utf-8",
    )
    status, found, _ = scratch.run()
    expect(
        "a ledger of the format of the earlier ratchet is an error that names the format of the rows",
        status == 1 and any(f"'format {rule_ledger.FORMAT}' and 'rule {RULE}'" in item.message for item in found),
    )
    status, _ = scratch.write()
    expect(
        "the write refuses a ledger that it cannot read",
        status == 1 and "format 4" in scratch.ledger.read_text(encoding="utf-8"),
    )
    scratch.ledger.write_text(good, encoding="utf-8")


def ledger_cases(expect: testing.Expect) -> None:
    """Run each case of the rule quarantine on one planted build, in sequence."""
    with tempfile.TemporaryDirectory(prefix="plugin-runner-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree")
        first_write_cases(expect, scratch)
        region_cases(expect, scratch)
        directory_cases(expect, scratch)
        file_cases(expect, scratch)
        build_kind_cases(expect, scratch)
        problem_cases(expect, scratch)
        ledger_problem_cases(expect, scratch)
        parsed_cases(expect, scratch)


# ── The census of the fixtures ─────────────────────────────────────


NEG_A = "test/neg/neg_a.cpp"
NEG_B = "test/neg/neg_b.cpp"
VECTOR = ("std-object", 5, 3, "std::vector")
PROBE = ("detail-use", 7, 1, "main(): fixy::detail")


def fixture_run_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a build with no fixture run, a reduce with no fixture run, a new directory and the source root."""
    scratch.write(rule_ledger.WriteMode.RAISE)
    before = scratch.base()
    scratch.add_fixture("neg_a", NEG_A)
    expect(
        "a build with a fixture with the plugin and no fixture run counts its objects only, and a census is not "
        "necessary",
        scratch.errors() == [] and scratch.counts() == before,
    )
    status, text = scratch.write(is_complete=True)
    expect(
        "a reduce in which no build has a fixture run is refused, because such a reduce removes each row of a fixture",
        status == 1 and "no build of objects of the reduce has a fixture run" in text and scratch.base() == before,
    )
    scratch.record("neg_a", [VECTOR])
    expect(
        "a new fixture finding in a directory with no row fails",
        scratch.errors()
        == ["test/neg/ has 1 findings of the kind std-object, and the ledger holds no row of the directory"],
    )
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "--raise writes the row of the directory of the fixture, and the build then gives no error",
        status == 0 and scratch.base().get(("test/neg/", "std-object", "")) == 1 and scratch.errors() == [],
    )
    other = fixtures.FixtureContext(Path("/planted-checkout"), scratch.build)
    expect(
        "the census names the source of a fixture relative to the argument root= of its command, also for a check "
        "from a different checkout",
        {item.file for item in other.part().lines} == {NEG_A} and other.part().fixtures == 1,
    )
    status, text = scratch.listed("test/neg/")
    expect(
        "the list gives the lines of the census of a fixture",
        status == 0 and f"quarantine std-object {NEG_A}:5:3 std::vector" in text and "1 findings" in text,
    )
    status, _ = scratch.write(is_complete=True)
    expect(
        "a reduce with a fixture run keeps the row of the fixture",
        status == 0 and scratch.base().get(("test/neg/", "std-object", "")) == 1,
    )
    _, _, text = scratch.run()
    expect("the summary of a run counts the fixture with a census as a unit", "from 4 objects and fixtures" in text)


def fixture_problem_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a kind of a probe and its reason, the problems of the records, a parse build and the files of fixtures."""
    scratch.record("neg_a", [VECTOR, PROBE])
    expect(
        "a kind of a probe counts in the census of a fixture",
        scratch.errors() == ["test/neg/ has 1 findings of the kind detail-use of a probe, and the ledger admits 0"],
    )
    status, _ = scratch.write()
    expect("the write refuses a new count of a kind of a probe with no --admit", status == 1)
    status, _ = scratch.write(rule_ledger.WriteMode.ADMIT, reason="the fixture probes a private check")
    expect(
        "--admit with a reason writes the row of the probe with its reason, and the build then gives no error",
        status == 0
        and scratch.ledger_rows().reason_of(rule_ledger.Key("test/neg/", "detail-use", ""))
        == "the fixture probes a private check"
        and scratch.errors() == [],
    )
    text = scratch.ledger.read_text(encoding="utf-8")
    scratch.ledger.write_text(text.replace("the fixture probes a private check", "-"), encoding="utf-8")
    expect(
        "a row of a kind of a probe with no reason is an error",
        scratch.errors() == ["the row of test/neg/ with the kind detail-use gives no reason"],
    )
    scratch.ledger.write_text(text, encoding="utf-8")
    scratch.add_fixture("neg_b", NEG_B)
    found = scratch.errors()
    expect(
        "a build with the record of one fixture and no record of a different fixture is an error",
        len(found) == 1 and "1 of the 2 fixtures with the plugin have no record" in found[0],
    )
    scratch.record("neg_b", [], stamp="ffff")
    expect(
        "a census with a stamp that is not the stamp of the compile command is an error, and the write refuses it",
        any("has the stamp ffff" in item for item in scratch.errors()) and scratch.write()[0] == 1,
    )
    scratch.record("neg_b", [])
    expect("a fixture with an empty census passes", scratch.errors() == [])
    scratch.configure(BASE_NAME, "Debug", is_parse=True)
    parsed = fixtures.FixtureContext(scratch.root, scratch.build).part()
    expect(
        "a parse build reads no record, thus it gives no fixture finding and a census is not necessary",
        parsed.fixtures == 0 and not parsed.lines and not parsed.problems,
    )
    scratch.configure(BASE_NAME, "Debug")
    root = Path("/planted")
    unjudged = fixtures.unjudged_findings(
        [
            ("neg_a", "/planted/test/neg/neg_a.cpp"),
            ("neg_a", "/planted/test/neg/helper.h"),
            ("neg_a", "/planted/include/fixy/Core.h"),
            ("neg_b", "/planted/build/quarantine/modes/test/neg/neg_b.cpp"),
            ("neg_b", "/usr/include/c++/16/vector"),
        ],
        frozenset({"/planted/include/fixy/Core.h"}),
        frozenset({"/planted/test/neg/neg_a.cpp"}),
        ("/planted/build/quarantine",),
        root,
        None,
    )
    expect(
        "a file of the tree that only the compiles of fixtures read fails, and the source of a fixture, a file of "
        "all, a mode stamp and a file outside the tree do not",
        [(item.path, "only the compiles of the fixtures neg_a" in item.message) for item in unjudged]
        == [("test/neg/helper.h", True)],
    )


def fixture_cases(expect: testing.Expect) -> None:
    """Run each case of the census of the fixtures on one planted build, in sequence."""
    with tempfile.TemporaryDirectory(prefix="plugin-fixtures-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree")
        fixture_run_cases(expect, scratch)
        fixture_problem_cases(expect, scratch)


# ── The rule contract-specifier ────────────────────────────────────


def contract_cases(expect: testing.Expect) -> None:
    """Hold the errors of the rule contract-specifier, and its specifier in an opt-out region."""
    with tempfile.TemporaryDirectory(prefix="plugin-contract-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree")
        status, found, text = scratch.run(CONTRACT)
        expect("a build with no specifier gives no error", status == 0 and not found and "0 error(s)" in text)
        lines_a = scratch.objects["a"][0]
        scratch.add(
            "a",
            [
                *lines_a,
                "contract-specifier contract-specifier include/crucible/Vigil.h:20:1 pre",
                "contract-specifier contract-specifier include/crucible/Vigil.h:21:1 post",
                "quarantine opted-out test/fixy/test_a.cpp:12:1 contract-specifier pre",
            ],
        )
        status, found, _ = scratch.run(CONTRACT)
        expect(
            "each specifier is an error that names its statement",
            status == 1
            and [(item.path, item.line) for item in found]
            == [("include/crucible/Vigil.h", 20), ("include/crucible/Vigil.h", 21)]
            and "CRUCIBLE_PRE(condition)" in found[0].message
            and "CRUCIBLE_POST(result, condition)" in found[1].message,
        )
        header_specifier = "contract-specifier contract-specifier include/crucible/Vigil.h:20:1 pre"
        scratch.add("b", [*scratch.objects["b"][0], header_specifier])
        expect("a specifier of a header of two units is one error", len(scratch.run(CONTRACT)[1]) == 2)
        counts = scratch.counts()
        expect(
            "the rule quarantine counts a specifier in an opt-out region, and no other specifier",
            counts.get(("test/fixy/", "opted-out", PROBE_REASON)) == 2
            and not any(key[1] == CONTRACT for key in counts),
        )
        scratch.write(rule_ledger.WriteMode.RAISE)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            statuses = runner.run([RULES[CONTRACT], RULES[RULE]], scratch.root, scratch.build, None)
        expect("one run of the engine gives the status of each rule", statuses == [1, 0])
        status, text = scratch.listed(None, CONTRACT)
        expect("the list gives the findings of each rule", status == 0 and "2 findings" in text)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as said:
            status = runner.write(RULES[CONTRACT], runner.WriteRequest(scratch.root, scratch.build))
        expect(
            "the rule contract-specifier has no ledger to write", status == 1 and "the policy none" in said.getvalue()
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (unit_cases, census_cases, ledger_cases, layer_move_cases, fixture_cases, contract_cases),
    )
