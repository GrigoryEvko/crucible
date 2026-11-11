"""The test of the runner of the engine plugin and of its rules quarantine and contract-specifier, on planted builds.

THE PLANTED BUILD
    Scratch makes a throwaway git repository.  It holds each source file of
    the findings, a fake ninja and a CMake cache.  It also holds a compile
    database, objects with sections, a rule table and a ledger.  A case
    changes the finding lines of an object.  Then it runs the rule, writes the
    ledger or lists the findings, as `guard check`, `guard write` and `guard
    list` do.  A complete write gives the write the planted build two times,
    as a reduce of the landing gate gives it a build of each configuration.

THE CASES
    The cases hold each behavior of the ledger of the rule quarantine:
      * The entity of each kind
      * The opted-out findings and the regions
      * One set of rows for the builds of two kinds, and an arm of one build
      * A file that git moves, which needs --raise, and a file that the tree
        does not hold
      * A base header that git moves to a different layer
      * An equal build with no warning, and the problems of the objects
      * Each mode of the write, the write of one build and the complete write.
    The cases of the rule contract-specifier hold its errors and the problems
    of the census.
"""

import contextlib
import io
import sys
import tempfile
from collections import Counter
from collections.abc import Mapping
from pathlib import Path

from assay.core import git as throwaway_repo
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable, testing
from assay.guard.engines.plugin import counted, quarantine, runner
from assay.readers import cmake, compdb, elf
from assay.readers import sections as quarantine_sections

RULE = quarantine.RULE
CONTRACT = "contract-specifier"
REGISTRY_ROWS = (
    "rule quarantine engine=plugin scope=quarantined ledger=count doc=XXII.R11 | a planted quarantine rule | lower it",
    "rule contract-specifier engine=plugin scope=tracked ledger=none doc=XII | a planted contract rule | use a macro",
)
FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / "inputs.txt").read_text())
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
    """A planted build with a fake ninja, a compile database, objects with sections, a rule table and a ledger.

    The root is a throwaway git repository with each source file of the findings.  A move is then a git move.
    """

    def __init__(self, root: Path) -> None:
        """Make the tree with three objects."""
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
        self.configure(BASE_NAME, "Debug")
        self.objects: dict[str, tuple[list[str], str, bool]] = {}
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

    def configure(self, kind: str, build_type: str, flags: str = "") -> None:
        """Write the build kind and the reply of the CMake cache of one configuration of the planted build."""
        (self.build / "build-kind.txt").write_text(f"{kind}\n", encoding="utf-8")
        cmake.plant(
            self.build,
            cache={
                "CMAKE_GENERATOR": "Ninja",
                "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(self.ninja)),
                "CMAKE_BUILD_TYPE": ("STRING", build_type),
                "CMAKE_CXX_FLAGS": ("STRING", flags),
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

    def flush(self) -> None:
        """Write each object, the compile database and the inputs of all."""
        rows = []
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
        """Return the count of each file, kind and entity of the planted objects."""
        return as_tuples(counted.counts_of(quarantine.findings(runner.PluginContext(self.root, self.build))))

    def ledger_rows(self) -> rule_ledger.Ledger:
        """Read the planted ledger."""
        return rule_ledger.read(RULES[RULE], self.root)

    def base(self) -> dict[Row, int]:
        """Return the count of each row of the planted ledger."""
        return as_tuples({key: int(entry.value) for key, entry in self.ledger_rows().base.items()})


def as_tuples(counts: Mapping[rule_ledger.Key, int]) -> dict[Row, int]:
    """Return the counts with each key as the tuple (file, kind, entity)."""
    return {(key.path, key.kind, key.entity): count for key, count in counts.items()}


def directory_totals(counts: Mapping[Row, int]) -> Counter[tuple[str, str]]:
    """Return the count of each directory and kind: the key of a coarser ratchet."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(file.rsplit("/", 1)[0], kind)] += count
    return totals


def file_totals(counts: Mapping[Row, int]) -> Counter[tuple[str, str]]:
    """Return the count of each file and kind: the rule of the ratchet before it counted entities."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(file, kind)] += count
    return totals


# ── The functions ──────────────────────────────────────────────────


def unit_cases(expect: testing.Expect) -> None:
    """Hold the entity of each kind and the stamp."""
    keyed = rule_ledger.parse(
        f"{EMPTY_LEDGER}"
        "x.cpp | raw-pointer-object | 2 | - | -\n"
        "x.cpp | layer-header | 1 | <vector> in the layer X1 | -\n"
        "x.cpp | opted-out | 1 | PROBE: a region \\| c-library-call memcpy | -\n"
        "x.cpp | region | 1 | PROBE: a region | -\n",
        "planted",
        RULE,
        RULES[RULE].policy,
    )
    expect(
        "a row keeps the spaces and the bar of its entity, and a kind with no entity has the empty cell",
        as_tuples({key: int(entry.value) for key, entry in keyed.base.items()})
        == {
            ("x.cpp", "raw-pointer-object", ""): 2,
            ("x.cpp", "layer-header", "<vector> in the layer X1"): 1,
            ("x.cpp", "opted-out", "PROBE: a region | c-library-call memcpy"): 1,
            ("x.cpp", "region", "PROBE: a region"): 1,
        },
    )
    expect(
        "the entity of a row names what the plugin refuses, and a declaration of a raw object keys no name",
        [
            quarantine.entity_key(kind, entity)
            for kind, entity in (
                ("std-object", "std::span"),
                ("c-library-object", "_IO_FILE"),
                ("raw-pointer-object", "expression"),
                ("c-array-object", "bytes"),
                ("raw-function-pointer", "callback"),
                ("raw-new-delete", "placement new"),
                ("raw-new-delete", "new"),
                ("raw-new-delete", "delete"),
                ("raw-new-delete", "declaration of operator new"),
                ("std-entity", "std::array::operator[]"),
                ("c-library-call", "memcpy"),
                ("layer-header", "<vector>  in the layer X1"),
                (
                    "layer-header",
                    "<atomic> in the layer F0_core, a file of the language with the allowance of the layer F0_primitives",
                ),
                ("layer-header", "<vector>"),
                (
                    "upward-include",
                    "include/fixy/concurrent/ParallelismRule.h (the layer X4_concurrent) from the layer X3_os",
                ),
                ("upward-include", "include/crucible/Arena.h (no layer) from the layer X1_wrappers"),
                ("upward-include", "include/crucible/X.h"),
                ("door-header", "<pthread.h> has the door src/fixy/os/Thread.cpp"),
                ("replace-pending:std::bit_cast", "std::bit_cast"),
            )
        ]
        == [
            "std::span",
            "_IO_FILE",
            "",
            "",
            "",
            "placement new",
            "new",
            "delete",
            "declaration of operator new",
            "std::array::operator[]",
            "memcpy",
            "<vector>",
            "<atomic>",
            "<vector>",
            "include/fixy/concurrent/ParallelismRule.h",
            "include/crucible/Arena.h",
            "include/crucible/X.h",
            "<pthread.h> has the door src/fixy/os/Thread.cpp",
            "std::bit_cast",
        ],
    )
    expect(
        "an include in an opt-out region keys its inner entity with no layer too",
        quarantine.opted_out_key("C-HEADER: a struct", "layer-header <vector> in the layer X3_os")
        == "C-HEADER: a struct | layer-header <vector>",
    )
    expect(
        "the entity of an opted-out row holds the reason of its region, and the inner kind and entity",
        quarantine.opted_out_key("PROBE: a region", "std-object  std::vector")
        == "PROBE: a region | std-object std::vector"
        and quarantine.opted_out_key("ABI: x", "raw-pointer-object bytes") == "ABI: x | raw-pointer-object",
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
        "a planted census gives the counts of the rule quarantine, with a region row and an opted-out row",
        counts
        == {
            ("a.cpp", "std-entity", "std::swap"): 1,
            ("a.cpp", "region", "PROBE: a region"): 1,
            ("a.cpp", "opted-out", "PROBE: a region | std-entity std::swap"): 1,
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
        "--raise writes one row for each file, kind and entity of the build, and one for each region",
        status == 0
        and scratch.base()
        == {
            ("include/crucible/Vigil.h", "std-object", "std::vector"): 1,
            ("include/fixy/session/Handle.h", "upward-include", "include/crucible/X.h"): 1,
            ("src/canopy/Lifeguard.cpp", "raw-pointer-object", ""): 1,
            ("test/fixy/test_a.cpp", "c-library-call", "memcpy"): 1,
            ("test/fixy/test_a.cpp", "opted-out", f"{PROBE_REASON} | raw-pointer-object"): 1,
            ("test/fixy/test_a.cpp", "region", PROBE_REASON): 1,
            ("test/fixy/test_b.cpp", "std-object", "std::span"): 1,
            ("test/test_arena.cpp", "std-entity", "std::swap"): 1,
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
        "7 findings and 1 regions in 8 rows of 6 files" in text,
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
    memset_row = ("test/fixy/test_a.cpp", "opted-out", f"{PROBE_REASON} | c-library-call memset")
    expect(
        "a region that covers one more finding fails, with the reason of the region in the row",
        scratch.errors()
        == [
            (
                f"test/fixy/test_a.cpp has 1 findings of the kind opted-out and the entity "
                f"{memset_row[2]}, and the ledger admits 0"
            )
        ],
    )
    status, text = scratch.write()
    expect("the write refuses the growth of a region", status == 1 and "--admit" in text)
    status, text = scratch.write(rule_ledger.WriteMode.ADMIT)
    grown = scratch.base()
    expect(
        "--admit writes the growth of a region, and the rule then gives no error",
        status == 0 and grown.get(memset_row) == 1 and scratch.errors() == [],
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
        status == 0 and memset_row not in scratch.base() and scratch.errors() == [],
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
                (
                    f"test/fixy/test_a.cpp has 1 findings of the kind opted-out and the entity {other} | "
                    f"raw-pointer-object, and the ledger admits 0"
                ),
                f"test/fixy/test_a.cpp has 1 opt-out regions with the reason {other!r}, and the ledger admits 0",
            ]
        ),
    )
    scratch.add("a", [*lines_a[:3], f"quarantine region test/fixy/test_a.cpp:30:1 {PROBE_REASON}", lines_a[4]])
    status, found, _ = scratch.run()
    expect(
        "an opted-out finding before each region of its file is an error, and the write refuses it",
        status == 1
        and any(item.path == "test/fixy/test_a.cpp" and "no section gives a region" in item.message for item in found)
        and scratch.write()[0] == 1,
    )
    second = "quarantine region test/fixy/test_a.cpp:20:1 PROBE: a planted region"
    scratch.add("a", [*lines_a, second])
    expect(
        "a second region of a reason is a new region, which the rule refuses",
        scratch.errors()
        == [(f"test/fixy/test_a.cpp has 2 opt-out regions with the reason {PROBE_REASON!r}, and the ledger admits 1")],
    )
    status, text = scratch.write()
    expect("the write refuses a new region", status == 1 and "--admit" in text)
    status, _ = scratch.write(rule_ledger.WriteMode.ADMIT, reason="PROBE: a planted reason")
    expect(
        "--admit writes a new region",
        status == 0 and scratch.base().get(("test/fixy/test_a.cpp", "region", PROBE_REASON)) == 2,
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
    status, listed = scratch.listed("test/fixy", quarantine.REGION)
    expect(
        "the list gives the region lines of a directory",
        status == 0
        and f"quarantine region test/fixy/test_a.cpp:8:1 {PROBE_REASON}" in listed
        and "1 findings" in listed,
    )


def hole_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the two holes of a count for each directory and for each file, and the rows that stay after a move."""
    before = scratch.base()
    lines_a = scratch.objects["a"][0]
    scratch.add(
        "a",
        [line for line in lines_a if "test_b.cpp" not in line]
        + ["quarantine std-object test/fixy/test_a.cpp:12:5 std::array"],
    )
    planted = scratch.counts()
    expect(
        "the swap keeps each count of a directory and kind, so a count for each directory lets it go",
        directory_totals(planted) == directory_totals(before),
    )
    expect(
        "the swap fails the count for each file: test_a rises, and the fall of test_b passes",
        scratch.errors()
        == [
            (
                "test/fixy/test_a.cpp has 1 findings of the kind std-object and the entity "
                "std::array, and the ledger admits 0"
            ),
        ],
    )
    status, _ = scratch.write()
    expect("the write refuses the rise", status == 1 and scratch.base() == before)
    scratch.add("a", [line.replace("test_b.cpp:6:5 std::span", "test_b.cpp:6:5 std::map") for line in lines_a])
    planted = scratch.counts()
    expect(
        "the entity swap keeps each count of a file and kind, so a count for each file lets it go",
        file_totals(planted) == file_totals(before),
    )
    expect(
        "the entity swap fails: std::map rises, and the fall of std::span passes",
        scratch.errors()
        == [
            (
                "test/fixy/test_b.cpp has 1 findings of the kind std-object and the entity "
                "std::map, and the ledger admits 0"
            ),
        ],
    )
    first_row = scratch.ledger_rows().line_of(rule_ledger.Key("test/fixy/test_b.cpp", "std-object", "std::span"))
    expect(
        "the error of a new entity points at the first row of its file",
        any(item.line == first_row and "std::map" in item.message for item in scratch.run()[1]),
    )
    status, text = scratch.write()
    expect(
        "the write refuses the entity swap",
        status == 1 and scratch.base() == before and "std::map, rises from 0" in text,
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
    """Hold a new file, a finding that goes, the list of the findings and a file that git moves."""
    scratch.add("d", ["quarantine std-object test/fixy/test_d.cpp:5:5 std::array"])
    expect(
        "a new file with one finding fails as a file that the ledger does not hold",
        scratch.errors()
        == [
            (
                "test/fixy/test_d.cpp has 1 findings of the kind std-object and the entity "
                "std::array, and the ledger holds no row of the file"
            )
        ],
    )
    status, text = scratch.write()
    expect("the write refuses a new file with a finding", status == 1 and "does not hold" in text)
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
        ("test/test_arena.cpp", "std-entity", "std::swap") in scratch.base(),
    )
    scratch.write(is_complete=True)
    status, _, _ = scratch.run()
    expect(
        "after a complete write the build gives no error again, and the row of the finding that goes is gone",
        status == 0 and ("test/test_arena.cpp", "std-entity", "std::swap") not in scratch.base(),
    )
    status, text = scratch.listed("test/fixy", "c-library-call")
    expect(
        "the list gives the findings of a directory and kind",
        status == 0 and "quarantine c-library-call test/fixy/test_a.cpp:4:3 memcpy" in text and "1 findings" in text,
    )
    status, text = scratch.listed("test/fixy/test_b.cpp")
    expect("the list gives the findings of a file", status == 0 and "test_b.cpp:6:5" in text and "1 findings" in text)
    scratch.git("mv", "test/fixy/test_b.cpp", "test/fixy/test_c.cpp")
    scratch.add("a", [line.replace("test_b.cpp", "test_c.cpp") for line in scratch.objects["a"][0]])
    expect("a moved file fails at its new path until the ledger is written again", len(scratch.errors()) == 1)
    status, _ = scratch.write()
    expect("the write reads no git move, and it refuses the new path with no --raise", status == 1)
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "--raise writes the row of the new path, and the build then gives no error",
        status == 0 and ("test/fixy/test_c.cpp", "std-object", "std::span") in scratch.base() and scratch.run()[0] == 0,
    )


def layer_move_cases(expect: testing.Expect) -> None:
    """Move a base header to a different layer: its include keeps its row, and a new include increases a count."""
    with tempfile.TemporaryDirectory(prefix="plugin-layer-move-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree")
        old, new = "include/fixy/os/Spawn.h", "include/fixy/concurrent/Spawn.h"
        (scratch.root / old).parent.mkdir(parents=True)
        (scratch.root / old).write_text(f"// {old}\n", encoding="utf-8")
        scratch.git("add", "--", old)
        scratch.git("commit", "-q", "-m", "Add a planted base header")
        scratch.add("e", [f"quarantine layer-header {old}:5:1 <tuple> in the layer X3_os"])
        status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
        expect(
            "the row of an include holds the header and no layer",
            status == 0 and scratch.base().get((old, "layer-header", "<tuple>")) == 1,
        )
        (scratch.root / new).parent.mkdir(parents=True)
        scratch.git("mv", old, new)
        scratch.add("e", [f"quarantine layer-header {new}:5:1 <tuple> in the layer X4_concurrent"])
        expect(
            "a header moved to a different layer fails at its new path until the ledger is written again",
            len(scratch.errors()) == 1,
        )
        status, _ = scratch.write(rule_ledger.WriteMode.RAISE, is_complete=True)
        expect(
            "a complete write with --raise gives the moved header the row of its include, and the build then gives "
            "no error",
            status == 0
            and scratch.base().get((new, "layer-header", "<tuple>")) == 1
            and (old, "layer-header", "<tuple>") not in scratch.base()
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
            len(scratch.errors()) == 1 and status == 1 and "rises from 0" in text,
        )


def build_kind_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold one set of rows for the builds of two kinds: an arm of one build, a file of the build, the reduce."""
    scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
    status, _, text = scratch.run()
    expect("the one set of rows judges a build of a different kind", status == 0 and "holds no rows" not in text)
    swap = ("include/crucible/Vigil.h", "std-entity", "std::swap")
    scratch.add("g", [f"quarantine std-entity {swap[0]}:31:7 std::swap"])
    before_write = scratch.ledger.read_text(encoding="utf-8")
    status, written_text = scratch.write()
    swap_error = (
        "include/crucible/Vigil.h has 1 findings of the kind std-entity and the entity std::swap, and the ledger "
        "admits 0"
    )
    expect(
        "a finding of an arm that only that build compiles fails it, and the write refuses it with no --raise",
        scratch.errors() == [swap_error]
        and status == 1
        and "rises from 0" in written_text
        and scratch.ledger.read_text(encoding="utf-8") == before_write,
    )
    # Only the release build writes the file of the build directory.
    generated = (f"{ruletable.BUILD_PREFIX}gen/table.c", "c-array-object", "")
    scratch.add("e", [f"quarantine c-array-object {generated[0]}:3:1 int [4]"])
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
    malformed = good.replace("test/fixy/test_c.cpp | std-object | 1 |", "test/fixy/test_c.cpp | std-object | x |")
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
        hole_cases(expect, scratch)
        file_cases(expect, scratch)
        build_kind_cases(expect, scratch)
        problem_cases(expect, scratch)
        ledger_problem_cases(expect, scratch)
        parsed_cases(expect, scratch)


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
            counts.get(("test/fixy/test_a.cpp", "opted-out", f"{PROBE_REASON} | contract-specifier pre")) == 1
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
        (unit_cases, census_cases, ledger_cases, layer_move_cases, contract_cases),
    )
