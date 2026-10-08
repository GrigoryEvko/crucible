"""The test of the runner of the engine plugin and of its rules quarantine and contract-specifier, on planted builds.

THE PLANTED BUILD
    Scratch makes a throwaway git repository.  It holds each source file of
    the findings, a fake ninja, a fake compiler and a CMake cache.  It also
    holds a compile database, objects with sections, a rule table and a
    ledger.  A case changes the finding lines of an object.  Then it runs the
    rule, writes the ledger or lists the findings, as `guard check`, `guard
    write` and `guard list` do.

THE CASES
    The cases hold each behavior of the ledger of the rule quarantine:
      * The entity of each kind
      * The opted-out findings and the regions
      * The configurations and their difference rows
      * A build that the ledger does not hold, and the rows for an import
      * A file that git moves, and a file that the tree does not hold
      * The directory warnings and the problems of the objects
      * Each mode of the write.
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
from assay.core import rows as rows_grammar
from assay.core import ruletable, testing
from assay.guard.engines.plugin import quarantine, runner
from assay.readers import cmake, compdb
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
RULES_TEXT = (
    "layer base 0 include/fixy/ include/fixy/session/\n"
    "quarantine include/crucible/\nquarantine src/\nquarantine test/\nquarantine @build/\n"
)
HEAD = "# a planted ledger\n"
EMPTY_LEDGER = f"{HEAD}format 1\nrule {RULE}\n"
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


def planted_rules() -> dict[str, rule_registry.Rule]:
    """Return the two rules of the engine plugin of a planted registry."""
    registry, problems = rule_registry.examine(rule_registry.planted(REGISTRY_ROWS))
    if problems:
        raise ValueError(f"the planted registry does not read: {problems[0]}")
    return dict(registry.rules)


RULES = planted_rules()


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
        table = root / quarantine.TABLE
        table.parent.mkdir(parents=True, exist_ok=True)
        table.write_text(RULES_TEXT, encoding="utf-8")
        self.ninja = root / "ninja.py"
        self.ninja.write_text(f"#!{sys.executable}\n{FAKE_NINJA}", encoding="utf-8")
        self.ninja.chmod(0o755)
        self.compiler = root / "compiler" / "g++"
        self.compiler.parent.mkdir()
        self.compiler.write_text(f"#!{sys.executable}\n{FAKE_COMPILER}", encoding="utf-8")
        self.compiler.chmod(0o755)
        self.configure(BASE_NAME, "Debug")
        self.objects: dict[str, tuple[list[str], str, bool]] = {}
        # The flags that the target of an object adds to its compile, as each bench target adds -march=native.
        self.flags: dict[str, str] = {}
        header = "quarantine std-object include/crucible/Vigil.h:10:5 std::vector (std::vector<int>)"
        self.add(
            "a",
            [
                header,
                "quarantine c-library-call test/fixy/test_a.cpp:4:3 memcpy",
                "quarantine std-object test/fixy/test_b.cpp:6:5 std::span (std::span<int>)",
                f"quarantine region test/fixy/test_a.cpp:8:1 {PROBE_REASON}",
                "quarantine opted-out test/fixy/test_a.cpp:9:1 raw-pointer-object char*",
            ],
        )
        self.add(
            "b",
            [
                header,
                "quarantine raw-pointer-object src/canopy/Lifeguard.cpp:7:9 char*",
                "quarantine upward-include include/fixy/session/Handle.h:3:0 include/crucible/X.h",
            ],
        )
        self.add("c", ["quarantine std-entity test/test_arena.cpp:2:1 std::swap"])

    def configure(
        self, kind: str, build_type: str, flags: str = "", extra: Mapping[str, tuple[str, str]] | None = None
    ) -> None:
        """Write the build kind and the reply of the CMake cache of one configuration of the planted build."""
        (self.build / "build-kind.txt").write_text(f"{kind}\n", encoding="utf-8")
        cmake.plant(
            self.build,
            cache={
                "CMAKE_GENERATOR": "Ninja",
                "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(self.ninja)),
                "CMAKE_CXX_COMPILER": ("FILEPATH", str(self.compiler)),
                "CMAKE_BUILD_TYPE": ("STRING", build_type),
                "CMAKE_CXX_FLAGS": ("STRING", flags),
                **(extra or {}),
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
        self.flags.pop(name, None)
        self.flush()

    def flush(self) -> None:
        """Write each object, the compile database and the inputs of all."""
        rows = []
        for name, (lines, stamp, has_section) in self.objects.items():
            text = "".join(f"{line}\n" for line in [f"{quarantine_sections.HEAD_PREFIX}2 stamp={stamp}", *lines])
            sections = [(b".text", b"\x90")] + (
                [(quarantine_sections.SECTION_NAME, text.encode())] if has_section else []
            )
            (self.build / f"{name}.o").write_bytes(quarantine_sections.planted_object(sections))
            rows.append(
                {
                    "directory": str(self.build),
                    "file": str(self.root / f"{name}.cpp"),
                    "output": f"{name}.o",
                    "command": f"g++ {self.flags.get(name, '')} -fplugin=q.so {runner.STAMP_ARGUMENT}"
                    f"{STAMP_VALUE} -c {name}.cpp",
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
        add_configuration: bool = False,
        import_log: Path | None = None,
        reason: str | None = None,
    ) -> tuple[int, str]:
        """Write the ledger of the rule quarantine from the planted build, and return the status and the text."""
        request = runner.WriteRequest(
            self.root, None if import_log is not None else self.build, mode, reason, add_configuration, import_log
        )
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
        return as_tuples(quarantine.counts_of(quarantine.findings(runner.PluginContext(self.root, self.build))))

    def ledger_rows(self) -> rule_ledger.Ledger:
        """Read the planted ledger."""
        return rule_ledger.read(RULES[RULE], self.root)

    def base(self) -> dict[Row, int]:
        """Return the count of each base row of the planted ledger."""
        return as_tuples({key: int(entry.value) for key, entry in self.ledger_rows().base.items()})

    def deltas(self, name: str) -> dict[Row, int]:
        """Return the difference of each row of one configuration of the planted ledger."""
        return as_tuples({key: int(entry.value) for key, entry in self.ledger_rows().deltas.get(name, {}).items()})

    def tier(self, name: str) -> str:
        """Return the tier of one configuration of the planted ledger, or an empty text."""
        item = self.ledger_rows().configuration(name)
        return dict(item.facts).get(runner.TIER_KEY, "") if item is not None else ""


def as_tuples(counts: Mapping[rule_ledger.Key, int]) -> dict[Row, int]:
    """Return the counts with each key as the tuple (file, kind, entity)."""
    return {(key.path, key.kind, key.entity): count for key, count in counts.items()}


def delta_row(name: str, path: str, kind: str, delta: str, entity: str) -> str:
    """Return one difference row that the rule writes for an import, with its mark."""
    return rule_ledger.ROW_MARK + rows_grammar.render(
        [f"{rule_ledger.DELTA_MARK}{name}"], [path, kind, delta, entity, ""]
    )


def directory_totals(counts: Mapping[Row, int], rows: list[str]) -> Counter[tuple[str, str]]:
    """Return the count of each directory and kind: the rule of the ratchet before it counted files."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(quarantine.directory_of(file, rows), kind)] += count
    return totals


def file_totals(counts: Mapping[Row, int]) -> Counter[tuple[str, str]]:
    """Return the count of each file and kind: the rule of the ratchet before it counted entities."""
    totals: Counter[tuple[str, str]] = Counter()
    for (file, kind, _entity), count in counts.items():
        totals[(file, kind)] += count
    return totals


# ── The functions ──────────────────────────────────────────────────


def unit_cases(expect: testing.Expect) -> None:
    """Hold the directory of a file, the one word of a cache value, the entity of each kind and the stamp."""
    rows = quarantine.directory_rows(ruletable.parse(RULES_TEXT))
    for rel, wanted in (
        ("test/fixy/test_a.cpp", "test/fixy"),
        ("test/test_arena.cpp", "test"),
        ("src/canopy/Lifeguard.cpp", "src/canopy"),
        ("include/crucible/Vigil.h", "include/crucible"),
        ("include/crucible/ledger/probes/A.h", "include/crucible/ledger"),
        ("include/fixy/session/Handle.h", "include/fixy/session"),
        ("include/fixy/os/Time.h", "include/fixy/os"),
        ("include/fixy/Refined.h", "include/fixy"),
    ):
        expect(f"the directory of {rel} is {wanted}", quarantine.directory_of(rel, rows) == wanted)
    expect(
        "normalized reads a BOOL, a path and a string",
        (
            runner.normalized("BOOL", "TRUE"),
            runner.normalized("BOOL", "OFF"),
            runner.normalized("PATH", "x-NOTFOUND"),
            runner.normalized("FILEPATH", "/usr/bin/x"),
            runner.normalized("STRING", ""),
            runner.normalized("STRING", "Debug"),
        )
        == ("on", "off", "unset", "set", "unset", "Debug"),
    )
    keyed = rule_ledger.parse(
        f"{EMPTY_LEDGER}configuration b machine x\nconfiguration r machine y\n"
        "x.cpp | raw-pointer-object | 2 | - | -\n"
        "x.cpp | layer-header | 1 | <vector> in the layer X1 | -\n"
        "x.cpp | opted-out | 1 | PROBE: a region \\| c-library-call memcpy | -\n"
        "x.cpp | region | 1 | PROBE: a region | -\n"
        "@r | x.cpp | layer-header | +1 | <vector> in the layer X1 | -\n",
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
        }
        and keyed.expected("r")[rule_ledger.Key("x.cpp", "layer-header", "<vector> in the layer X1")] == 2,
    )
    expect(
        "the entity of a row names what the plugin refuses, with no type of the code",
        [
            quarantine.entity_key(kind, entity)
            for kind, entity in (
                ("std-object", "std::span (std::span<const Row>)"),
                ("c-library-object", "_IO_FILE (FILE*)"),
                ("raw-pointer-object", "const crucible::Expr*"),
                ("c-array-object", "int [2]"),
                ("raw-function-pointer", "void (*)()"),
                ("raw-new-delete", "placement new crucible::Node"),
                ("raw-new-delete", "new int"),
                ("raw-new-delete", "delete"),
                ("raw-new-delete", "declaration of operator new"),
                ("std-entity", "std::array::operator[]"),
                ("c-library-call", "memcpy"),
                ("layer-header", "<vector>  in the layer X1"),
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
            "<vector> in the layer X1",
            "std::bit_cast",
        ],
    )
    expect(
        "the entity of an opted-out row holds the reason of its region, and the inner kind and entity",
        quarantine.opted_out_key("PROBE:  a  region", "std-object std::vector (std::vector<int>)")
        == "PROBE: a region | std-object std::vector"
        and quarantine.opted_out_key("ABI: x", "raw-pointer-object char*") == "ABI: x | raw-pointer-object",
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
    configuration = runner.BuildConfiguration("planted", (("kind", "planted"), ("tier", "x86-64-0")))
    context = runner.PluginContext(Path("/planted"), None, census, configuration)
    counts = as_tuples(quarantine.counts_of(quarantine.findings(context)))
    expect(
        "a planted census gives the counts of the rule quarantine, with a region row and an opted-out row",
        counts
        == {
            ("a.cpp", "std-entity", "std::swap"): 1,
            ("a.cpp", "region", "PROBE: a region"): 1,
            ("a.cpp", "opted-out", "PROBE: a region | std-entity std::swap"): 1,
        },
    )
    expect("a planted context gives its configuration and reads no build", context.configuration() is configuration)
    no_build = runner.PluginContext(Path("/planted"))
    try:
        no_build.census()
        is_refused = False
    except runner.NotApplicable as reason:
        is_refused = "--build-dir" in str(reason)
    expect("a context with no build directory does not apply", is_refused)


# ── The rule quarantine ────────────────────────────────────────────


def first_write_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the first write of an empty ledger, the equal build and the second write."""
    status, _, text = scratch.run()
    expect(
        "an empty ledger does not apply, and the line names the build and its tier",
        status == runner.SKIPPED and f"holds no rows of {BASE_NAME} on the tier x86-64-" in text,
    )
    status, _ = scratch.write()
    expect("a write refuses a build that no configuration has", status == runner.SKIPPED)
    status, _ = scratch.write(add_configuration=True)
    written = scratch.ledger_rows()
    expect(
        "--add-configuration writes one row for each file, kind and entity of the base, and one for each region",
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
    base_keys = dict(written.configurations[0].facts) if written.configurations else {}
    expect(
        "the write keeps the head and writes the configuration under the name of the kind",
        written.comment == HEAD
        and written.base_name == BASE_NAME
        and base_keys.get("CMAKE_BUILD_TYPE") == "Debug"
        and base_keys.get(runner.KIND_KEY) == BASE_NAME
        and base_keys.get(runner.TIER_KEY, "").startswith("x86-64-"),
    )
    status, found, text = scratch.run()
    expect(
        "an equal build gives no error, and one warning for each directory",
        status == 0 and all(item.level == "warning" for item in found) and len(found) == 5,
    )
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
        "a region that covers less fails until the ledger is written again",
        any("opted-out" in error and "holds 1" in error for error in scratch.errors()),
    )
    status, _ = scratch.write()
    expect("the write writes the shrink of a region", status == 0 and scratch.errors() == [])
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
        "a region of a different reason fails: each row holds the reason of its region",
        scratch.errors()
        == sorted(
            [
                (
                    f"test/fixy/test_a.cpp has 0 findings of the kind opted-out and the entity {PROBE_REASON} | "
                    f"raw-pointer-object, and the ledger holds 1"
                ),
                (
                    f"test/fixy/test_a.cpp has 0 findings of the kind region and the entity {PROBE_REASON}, and the "
                    f"ledger holds 1"
                ),
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
        "a row that admits more regions than the file has is an error",
        scratch.errors()
        == [
            (
                f"test/fixy/test_a.cpp has 1 findings of the kind region and the entity "
                f"{PROBE_REASON}, and the ledger holds 2"
            )
        ],
    )
    status, _ = scratch.write()
    expect(
        "the write lowers the count of the regions, and the ledger keeps the rows of each region",
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
    rows = quarantine.directory_rows(ruletable.parse(RULES_TEXT))
    before = scratch.base()
    lines_a = scratch.objects["a"][0]
    scratch.add(
        "a",
        [line for line in lines_a if "test_b.cpp" not in line]
        + ["quarantine std-object test/fixy/test_a.cpp:12:5 std::array (std::array<int, 2>)"],
    )
    planted = scratch.counts()
    expect(
        "the swap keeps each count of a directory and kind, so a count for each directory lets it go",
        directory_totals(planted, rows) == directory_totals(before, rows),
    )
    expect(
        "the swap fails the count for each file: test_a rises and test_b falls",
        scratch.errors()
        == [
            (
                "test/fixy/test_a.cpp has 1 findings of the kind std-object and the entity "
                "std::array, and the ledger admits 0"
            ),
            (
                "test/fixy/test_b.cpp has 0 findings of the kind std-object and the entity "
                "std::span, and the ledger holds 1"
            ),
        ],
    )
    status, _ = scratch.write()
    expect("the write refuses the rise", status == 1 and scratch.base() == before)
    scratch.add("a", [line.replace("std::span (std::span<int>)", "std::map (std::map<int, int>)") for line in lines_a])
    planted = scratch.counts()
    expect(
        "the entity swap keeps each count of a file and kind, so a count for each file lets it go",
        file_totals(planted) == file_totals(before),
    )
    expect(
        "the entity swap fails: std::map rises and std::span falls",
        scratch.errors()
        == [
            (
                "test/fixy/test_b.cpp has 0 findings of the kind std-object and the entity "
                "std::span, and the ledger holds 1"
            ),
            (
                "test/fixy/test_b.cpp has 1 findings of the kind std-object and the entity "
                "std::map, and the ledger admits 0"
            ),
        ],
    )
    first_row = scratch.ledger_rows().line_of(
        rule_ledger.Key("test/fixy/test_b.cpp", "std-object", "std::span"), BASE_NAME
    )
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
            line.replace(
                "test_b.cpp:6:5 std::span (std::span<int>)", "test_b.cpp:40:9 std::span (std::span<const float>)"
            ).replace("test_a.cpp:4:3", "test_a.cpp:17:3")
            for line in lines_a
        ],
    )
    expect("a move of code and a different type of the same library class keep each row", scratch.errors() == [])
    scratch.add("a", lines_a)
    lines_b = scratch.objects["b"][0]
    scratch.add("b", [line.replace("char*", "const crucible::Expr*") for line in lines_b])
    expect(
        "a different pointee of a raw pointer keeps the row, because an alias can change that type",
        scratch.errors() == [],
    )
    scratch.add("b", lines_b)


def file_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a new file, a finding that goes, the list of the findings and a file that git moves."""
    scratch.add("d", ["quarantine std-object test/fixy/test_d.cpp:5:5 std::array (std::array<int, 2>)"])
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
        "the removal of a finding fails until the ledger is written again",
        status == 1 and any(item.level == "error" and "guard write" in item.message for item in found),
    )
    scratch.write()
    status, _, _ = scratch.run()
    expect(
        "after the write the build gives no error again",
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
    expect("a moved file fails until the ledger is written again", len(scratch.errors()) == 2)
    status, _ = scratch.write()
    expect(
        "the write moves the rows of a file that git reports as moved",
        status == 0 and ("test/fixy/test_c.cpp", "std-object", "std::span") in scratch.base() and scratch.run()[0] == 0,
    )


def configuration_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a second configuration, its difference rows, a different tier, a gone file and a file of the build."""
    scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
    status, _, text = scratch.run()
    expect(
        "a build of a different configuration does not apply, and one line gives its tier and the tiers of the ledger",
        status == runner.SKIPPED
        and f"holds no rows of {RELEASE_NAME} on the tier znver" not in text
        and f"holds no rows of {RELEASE_NAME} on the tier x86-64-" in text
        and f"It holds {BASE_NAME} on x86-64-" in text,
    )
    (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
    door = "<sys/socket.h> has the door include/fixy/os/Socket.h"
    # Only the second configuration writes the file of the build directory.  The tree holds no file test_gone.cpp.
    generated = (f"{ruletable.BUILD_PREFIX}gen/table.c", "c-array-object", "")
    gone = ("test/fixy/test_gone.cpp", "std-entity", "std::swap")
    lines_e = [
        "quarantine compiler-builtin include/crucible/Vigil.h:30:7 __builtin_ia32_pause",
        f"quarantine door-header include/fixy/session/Handle.h:5:0 {door}",
        f"quarantine c-array-object {generated[0]}:3:1 int [4]",
    ]
    scratch.add("e", [*lines_e, f"quarantine std-entity {gone[0]}:2:1 std::swap"])
    status, _ = scratch.write(add_configuration=True)
    release = scratch.deltas(RELEASE_NAME)
    expect(
        "--add-configuration writes the differences of a second configuration",
        status == 0
        and release
        == {
            ("include/crucible/Vigil.h", "compiler-builtin", "__builtin_ia32_pause"): 1,
            ("include/fixy/session/Handle.h", "door-header", door): 1,
            generated: 1,
            gone: 1,
        }
        and scratch.tier(RELEASE_NAME).startswith("znver5-"),
    )
    expect("the second configuration gives no error", scratch.run()[0] == 0)
    (scratch.compiler.parent / "native.txt").write_text("znver3\n", encoding="utf-8")
    status, _, text = scratch.run()
    expect(
        "a host of a different native tier does not apply, and the line names the two tiers",
        status == runner.SKIPPED
        and f"holds no rows of {RELEASE_NAME} on the tier znver3-" in text
        and f"{RELEASE_NAME} on znver5-" in text
        and "tier is znver3-" in text,
    )
    (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
    scratch.configure(BASE_NAME, "Debug")
    scratch.drop("e")
    scratch.add("a", [line for line in scratch.objects["a"][0] if "memcpy" not in line])
    status, _ = scratch.write()
    lowered = scratch.deltas(RELEASE_NAME)
    expect(
        "a change that lowers a count in each configuration writes only the base rows",
        status == 0
        and {key: delta for key, delta in lowered.items() if key[0] in SOURCES}
        == {key: delta for key, delta in release.items() if key[0] in SOURCES},
    )
    expect(
        "a write of the base removes the difference row of a source file that the tree does not hold",
        gone not in lowered,
    )
    expect(
        "a write of the base keeps the difference row of a file that a build of the configuration writes",
        lowered.get(generated) == 1,
    )
    scratch.configure(RELEASE_NAME, "Release", "-O1 -march=native -DNDEBUG")
    scratch.add("e", lines_e)
    expect("the second configuration still gives no error after the base rows fell", scratch.run()[0] == 0)
    scratch.add("g", ["quarantine std-entity include/crucible/Vigil.h:31:7 std::swap"])
    status, _, text = scratch.run()
    expect(
        "a failure of a later configuration writes its rows for an import",
        status == 1 and delta_row(RELEASE_NAME, "include/crucible/Vigil.h", "std-entity", "+1", "std::swap") in text,
    )
    scratch.drop("g")
    scratch.add("e", lines_e[:2])
    expect(
        "a build of the configuration that does not write the generated file fails on its row",
        scratch.errors() == [f"{generated[0]} has 0 findings of the kind c-array-object, and the ledger holds 1"],
    )
    status, _ = scratch.write()
    expect(
        "a write of that configuration removes the row, and the build then gives no error",
        status == 0 and generated not in scratch.deltas(RELEASE_NAME) and scratch.errors() == [],
    )


def import_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a configuration that only CI builds: its rows go through a log."""
    door = "<sys/socket.h> has the door include/fixy/os/Socket.h"
    scratch.configure("aarch64-debug-asan", "Debug")
    status, _, text = scratch.run()
    expect(
        "a build that the ledger does not hold writes its configuration and its differences",
        status == runner.SKIPPED
        and f"{rule_ledger.ROW_MARK}configuration aarch64-debug-asan kind aarch64-debug-asan" in text
        and delta_row(
            "aarch64-debug-asan", "include/crucible/Vigil.h", "compiler-builtin", "+1", "__builtin_ia32_pause"
        )
        in text
        and delta_row("aarch64-debug-asan", "include/fixy/session/Handle.h", "door-header", "+1", door) in text,
    )
    log = scratch.root / "ci.log"
    log.write_text(
        "".join(f"build+test aarch64\tQuarantine ratchet\t2026-10-02T21:00:00Z {line}\n" for line in text.splitlines()),
        encoding="utf-8",
    )
    status, _ = scratch.write(import_log=log)
    expect(
        "--import puts the rows of a log into the ledger, and the build then gives no error",
        status == 0 and scratch.ledger_rows().configuration("aarch64-debug-asan") is not None and scratch.run()[0] == 0,
    )
    third = delta_row(
        "aarch64-debug-asan", "include/crucible/Vigil.h", "compiler-builtin", "+3", "__builtin_ia32_pause"
    )
    log.write_text(log.read_text(encoding="utf-8") + f"x\t{third}\n", encoding="utf-8")
    before_import = scratch.ledger.read_text(encoding="utf-8")
    status, _ = scratch.write(import_log=log)
    expect(
        "--import refuses two values for one row",
        status == 1 and scratch.ledger.read_text(encoding="utf-8") == before_import,
    )
    scratch.configure(BASE_NAME, "Debug")
    scratch.drop("e")
    scratch.add("g", ["quarantine std-entity include/crucible/Vigil.h:31:7 std::swap"])
    status, _, text = scratch.run()
    expect(
        "a failure of the base writes no rows, because a write writes the base",
        status == 1 and rule_ledger.ROW_MARK not in text,
    )
    scratch.drop("g")


def bench_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a bench target that adds -march=native to its own compiles, so that its arms depend on the host."""
    bench_name = f"{BASE_NAME}-bench"
    scratch.configure(BASE_NAME, "Debug", extra={"CRUCIBLE_BENCH": ("BOOL", "ON")})
    scratch.flags["h"] = "-O3 -march=native"
    scratch.add("h", ["quarantine compiler-builtin include/crucible/Vigil.h:40:7 __builtin_ia32_vpcmpb512"])
    status, _ = scratch.write(add_configuration=True)
    bench_tier = scratch.tier(bench_name)
    expect(
        "a compile with its own native flag adds the native tier of the host to the tier",
        status == 0 and bench_tier.startswith("x86-64-") and "+znver5-" in bench_tier,
    )
    (scratch.compiler.parent / "native.txt").write_text("znver3\n", encoding="utf-8")
    scratch.add("h", [])
    status, _, text = scratch.run()
    expect(
        "a host of a different tier, with other arms in the bench compile, does not apply and writes one line",
        status == runner.SKIPPED
        and f"holds no rows of {bench_name} on the tier x86-64-" in text
        and "+znver3-" in text
        and f"{bench_name} on {bench_tier}" in text,
    )
    (scratch.compiler.parent / "native.txt").write_text("znver5\n", encoding="utf-8")
    scratch.drop("h")
    scratch.configure(BASE_NAME, "Debug")


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
        configuration_cases(expect, scratch)
        import_cases(expect, scratch)
        bench_cases(expect, scratch)
        problem_cases(expect, scratch)
        ledger_problem_cases(expect, scratch)


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
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            statuses = runner.run([RULES[CONTRACT], RULES[RULE]], scratch.root, scratch.build, None)
        expect("one run of the engine gives the status of each rule", statuses == [1, runner.SKIPPED])
        status, text = scratch.listed(None, CONTRACT)
        expect("the list gives the findings of each rule", status == 0 and "2 findings" in text)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = runner.write(
                RULES[CONTRACT], runner.WriteRequest(scratch.root, scratch.build, add_configuration=True)
            )
        expect("the rule contract-specifier has no configuration to write", status == 2)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (unit_cases, census_cases, ledger_cases, contract_cases))
