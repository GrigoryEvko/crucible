"""The test of the writer of the heavy and light rows of the rule table (assay.loop.derive.header_costs).

`guard selftest tests.loop.derive.test_header_costs` runs the test.  Each
case plants its input in a throwaway directory, and writes one line.  A
compiler of the test is a Python script that burns CPU time for one header.
The measure then gives one heavy header, and the other headers are light.
Exit 0 when each case holds, else 1.
"""

import functools
import sys
import tempfile
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import testing
from assay.core.exit import ExitCode
from assay.loop import gauge_sample
from assay.loop.derive import header_costs as writer
from assay.readers import compdb

# The threshold of the planted registry.  A light header of the fake compiler costs approximately the time of
# <meta>, and the heavy header costs more than ten times that time.
THRESHOLD = 5.0
TABLE_HEAD = "# A planted rule table.\n\nquarantine src/\n\n" + writer.MARKER + "\n"
GOOD_ROWS = (
    "# Measured on a planted host.\n"
    "heavy <arm_neon.h> | unmeasured: it does not compile alone\n"
    "heavy <chrono> | 1.12 s, 70628 lines\n"
    "light <cstdint> | 0.02 s, 178 lines\n"
    "light <meta> | 0.37 s, 34799 lines\n"
)
# The fake compiler: -fsyntax-only burns CPU for <heavy.h> and fails for <broken.h>, and -E prints three lines.
FAKE_COMPILER = """#!{python}
import sys
source = open(sys.argv[-1], encoding="utf-8").read()
if "-E" in sys.argv:
    print("one\\ntwo\\n\\nthree")
    sys.exit(0)
if "broken.h" in source:
    print("unit.cpp:1:10: fatal error: broken.h: No such file or directory", file=sys.stderr)
    sys.exit(1)
if "heavy.h" in source:
    total = 0
    for index in range(30_000_000):
        total += index
sys.exit(0)
"""


def plant(root: Path, rows: str = GOOD_ROWS, threshold: float = THRESHOLD) -> None:
    """Write a rule table with the rows after the marker line, and a registry with the threshold of the rule."""
    (root / writer.TABLE).parent.mkdir(parents=True, exist_ok=True)
    (root / writer.TABLE).write_text(TABLE_HEAD + rows, encoding="utf-8")
    (root / rule_registry.REGISTRY_PATH).write_text(
        rule_registry.planted(
            [rule_registry.measured_rule(writer.RULE, threshold, threshold, "x", "the planted threshold")]
        ),
        encoding="utf-8",
    )


def read_cases(expect: testing.Expect) -> None:
    """Check the reader of the cells and of the rows: each measure, each problem and the limit."""
    measured = writer.parse_cell("<chrono>", "1.12 s, 70628 lines")
    expect("a measure gives its seconds and its lines", measured.seconds == 1.12 and measured.lines == 70628)
    unmeasured = writer.parse_cell("<x.h>", "unmeasured: it does not compile")
    expect(
        "an unmeasured cell gives its reason and no number",
        unmeasured.seconds is None and unmeasured.reason == "it does not compile",
    )
    expect("an unmeasured header is heavy", writer.is_heavy(unmeasured, 10.0))
    expect("the cell of a measure reads back", writer.parse_cell("<chrono>", measured.cell()) == measured)
    for cell, label in (
        ("1.12 s", "a measure with no lines"),
        ("fast s, 3 lines", "seconds that are no number"),
        ("-1 s, 3 lines", "negative seconds"),
        ("unmeasured:", "an unmeasured cell with no reason"),
        ("1.0 s, 3 apples", "a measure with a different unit"),
    ):
        try:
            writer.parse_cell("<x.h>", cell)
            refused = False
        except writer.CostError:
            refused = True
        expect(f"the reader refuses {label}", refused)
    costs = {"<meta>": writer.Cost("<meta>", 0.4, 10, "")}
    expect("the limit is the seconds of <meta> times the threshold", abs(writer.limit_of(costs, 1.25) - 0.5) < 1e-9)
    try:
        writer.limit_of({"<meta>": writer.Cost("<meta>", None, None, "no")}, 1.25)
        refused = False
    except writer.CostError:
        refused = True
    expect("the limit of rows with no measured row of <meta> is an error", refused)
    with tempfile.TemporaryDirectory(prefix="header-costs-") as work:
        root = Path(work)
        plant(root)
        found = writer.read_costs(root)
        expect(
            "the reader gives each row and no problem of a good table",
            set(found.costs) == {"<arm_neon.h>", "<chrono>", "<cstdint>", "<meta>"} and not found.problems,
        )
        expect("the reader keeps the comment line after the marker", found.lines[0].startswith("# Measured"))
        plant(root, GOOD_ROWS + "quarantine test/\n")
        expect(
            "a row of a different kind after the marker is a problem",
            any("quarantine row is after the marker" in problem for problem in writer.read_costs(root).problems),
        )
        (root / writer.TABLE).write_text("light <x.h> | 0.01 s, 1 lines\n" + TABLE_HEAD + GOOD_ROWS, encoding="utf-8")
        expect(
            "a light row before the marker is a problem",
            any("before the marker line" in problem for problem in writer.read_costs(root).problems),
        )
        for text, label in (
            (TABLE_HEAD.replace(writer.MARKER, "# no marker") + GOOD_ROWS, "no marker line"),
            (TABLE_HEAD + writer.MARKER + "\n" + GOOD_ROWS, "two marker lines"),
        ):
            (root / writer.TABLE).write_text(text, encoding="utf-8")
            try:
                writer.read_costs(root)
                refused = False
            except writer.CostError:
                refused = True
            expect(f"the reader refuses a table with {label}", refused)


def check_cases(expect: testing.Expect) -> None:
    """Check --check and --write: a kind that does not agree with its measure, and the write of each kind."""
    with tempfile.TemporaryDirectory(prefix="header-costs-") as work:
        root = Path(work)
        plant(root, threshold=1.25)
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("--check accepts rows that agree with their measures", status == ExitCode.OK)
        swapped = GOOD_ROWS.replace("heavy <chrono>", "light <chrono>")
        plant(root, swapped, threshold=1.25)
        status, text = testing.captured(lambda: writer.checked(root))
        expect(
            "--check refuses a light row with a measure that makes it heavy",
            status == ExitCode.FINDINGS and "the row of <chrono> is light" in text,
        )
        status, _text = testing.captured(lambda: writer.written(root))
        expect(
            "--write gives the row the kind of its measure, and keeps the comment line",
            status == ExitCode.OK and (root / writer.TABLE).read_text(encoding="utf-8") == TABLE_HEAD + GOOD_ROWS,
        )
        plant(root, threshold=5.0)
        status, _text = testing.captured(lambda: writer.written(root))
        expect(
            "--write after a change of the threshold makes a heavy row light",
            status == ExitCode.OK and "light <chrono>" in (root / writer.TABLE).read_text(encoding="utf-8"),
        )
        plant(root, GOOD_ROWS + "light <x.h> | fast\n", threshold=1.25)
        status, _text = testing.captured(lambda: writer.written(root))
        expect(
            "--write writes nothing while a row has a problem",
            status == ExitCode.FINDINGS and "fast" in (root / writer.TABLE).read_text(encoding="utf-8"),
        )
        plant(root, threshold=1.25)
        (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted([]), encoding="utf-8")
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("--check without the measured rule of the registry is a missing input", status == ExitCode.MISSING_INPUT)


def measure_cases(expect: testing.Expect) -> None:
    """Check --measure with a fake compiler: a heavy header, a light header, a broken header and the comment line."""
    with tempfile.TemporaryDirectory(prefix="header-costs-") as work:
        root = Path(work) / "root"
        root.mkdir()
        plant(root, "light <meta> | 0.37 s, 34799 lines\n")
        (root / "include" / "crucible").mkdir(parents=True)
        (root / "include" / "crucible" / "Uses.h").write_text(
            "#pragma once\n#include <heavy.h>\n#include <crucible/Other.h>\n", encoding="utf-8"
        )
        compiler = Path(work) / "fake-compiler"
        compiler.write_text(FAKE_COMPILER.format(python=sys.executable), encoding="utf-8")
        compiler.chmod(0o755)
        build = Path(work) / "build"
        build.mkdir()
        source = gauge_sample.HEADER_REFERENCE
        compdb.plant(
            build,
            [
                {
                    "directory": str(root),
                    "file": str(root / source),
                    "arguments": [str(compiler), "-std=c++26", "-c", source, "-o", "x.o"],
                }
            ],
        )
        status, text = testing.captured(lambda: writer.measured(root, build, ["broken.h"], 4))
        written = (root / writer.TABLE).read_text(encoding="utf-8")
        expect(
            "--measure writes a row for each header of the rows, of include/ and of the command line",
            status == ExitCode.OK and all(header in written for header in ("<meta>", "<heavy.h>", "<broken.h>")),
        )
        expect(
            "--measure gives the costly header a heavy row and <meta> a light row",
            "heavy <heavy.h> | " in written and "light <meta> | " in written,
        )
        expect(
            "--measure gives a header that does not compile an unmeasured heavy row with the first error",
            "heavy <broken.h> | unmeasured: it does not compile alone" in written
            and "broken.h: No such file or directory" in written,
        )
        expect(
            "--measure keeps the head of the table and writes one comment line after the marker",
            written.startswith(TABLE_HEAD + "# Measured "),
        )
        expect("--measure names no project header", "<crucible/Other.h>" not in written)
        expect("--measure writes a summary line", "wrote 3 rows" in text)
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("the rows that --measure writes agree with --check", status == ExitCode.OK)
        status, _text = testing.captured(lambda: writer.measured(root, Path(work) / "missing", [], 4))
        expect("--measure without a compile database is a missing input", status == ExitCode.MISSING_INPUT)


# The command lines that main refuses, each with the words of its case.
USAGE_ERRORS = (
    ("a call with no mode", ()),
    ("a header with --check", ("--check", "x.h")),
    ("a header with --write", ("--write", "x.h")),
    ("--jobs 0", ("--measure", "build", "--jobs", "0")),
    ("two modes", ("--write", "--check")),
)


def usage_cases(expect: testing.Expect) -> None:
    """Check the usage errors of main (USAGE_ERRORS)."""
    for label, words in USAGE_ERRORS:
        status, _text = testing.captured(functools.partial(writer.main, list(words)))
        expect(f"main refuses {label}", status == ExitCode.USAGE)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (read_cases, check_cases, measure_cases, usage_cases))
