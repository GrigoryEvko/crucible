"""The test of the list of the walk units (assay.loop.derive.walk_units): its writer, its drift and its measure.

`guard selftest tests.loop.derive.test_walk_units` runs the test.  Each case
plants its input in a throwaway repository or directory, and writes one line.
The plants of the rule derived-files start with a tree that holds a list, so
the case of a tree with no list is here.  Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

from assay.core import git, testing
from assay.core import meter as cost_meter
from assay.core.exit import ExitCode
from assay.guard.engines.tsast.runner import TsastContext
from assay.loop.derive import walk_units as writer
from assay.readers import compdb

WALK_HEADER = (
    b"#pragma once\n#include <meta>\nnamespace foundation::tags { struct First {}; }\nnamespace foundation {\n"
    b"template <std::meta::info Ns>\nconsteval std::size_t count_of() { return std::meta::members_of(Ns, "
    b"std::meta::access_context::unchecked()).size(); }\n}  // namespace foundation\n"
)
USES_CHECK = (
    b"// The checks of fixy/Uses.h.\n#include <fixy/Uses.h>\n\n"
    b"static_assert(::foundation::count_of<^^::foundation::tags>() == 1);\n"
)
NO_WALK_CHECK = (
    b"// The checks of crucible/NoWalk.h.\n#include <crucible/NoWalk.h>\n\nstatic_assert(sizeof(int) == 4);\n"
)
TREE = {
    "include/foundation/Walk.h": WALK_HEADER,
    f"{writer.CHECKS}/fixy/Uses.cpp": USES_CHECK,
    f"{writer.CHECKS}/crucible/NoWalk.cpp": NO_WALK_CHECK,
}
USES_ROW = "fixy/Uses.cpp | {weight} | ::foundation::tags\n"
UNIT_DIRECTORY = "/build/test/layer/walk_units/0"
# A compiler of a test: it makes the object that -o names, and it reads nothing.
FAKE_COMPILER = "import sys; open(sys.argv[sys.argv.index('-o') + 1], 'wb').close()"


def unit_command(build: Path, has_include: bool = True) -> Mapping[str, object]:
    """Return a compile of the database of a test: a walk unit that the compiler of the test compiles.

    Args:
        build: The build directory of the compile
        has_include: True when the command holds the -I of the directory of the walk unit
    """
    output = f"test/layer/CMakeFiles{writer.UNIT_OUTPUT}0.dir/walks_across_headers.cpp.o"
    include = [f"-I{build}{UNIT_DIRECTORY}"] if has_include else []
    return {
        "directory": str(build),
        "file": "/source/test/layer/walks_across_headers.cpp",
        "output": output,
        "arguments": [sys.executable, "-c", FAKE_COMPILER, *include, "-o", output, "-c", "walks_across_headers.cpp"],
    }


def write_cases(expect: testing.Expect) -> None:
    """Check --write: the rows of the joined check files, the head comment, the weights that stay and a parse error."""
    with git.scratch_repository(TREE) as root:
        status, _text = testing.captured(lambda: writer.written(root))
        listed = (root / writer.LIST).read_text(encoding="utf-8")
        expect(
            "--write writes a row for the check file that joins, with the weight unmeasured",
            status == ExitCode.OK and USES_ROW.format(weight=writer.UNMEASURED) in listed and "NoWalk" not in listed,
        )
        expect("--write gives the list its head comment", listed.startswith(writer.LIST_HEADER))
        expect(
            "the drift of the rule derived-files accepts the list that --write writes",
            not [found for found in writer.drift(TsastContext(root)) if found.level == "error"],
        )
        (root / writer.LIST).write_text(listed.replace(writer.UNMEASURED, "2.5"), encoding="utf-8")
        testing.captured(lambda: writer.written(root))
        kept = (root / writer.LIST).read_text(encoding="utf-8")
        expect("--write keeps the weight of a row that stays", USES_ROW.format(weight="2.5") in kept)
        (root / writer.CHECKS / "crucible/Broken.cpp").write_text("static_assert(1 == 1;\n", encoding="utf-8")
        status, text = testing.captured(lambda: writer.written(root))
        expect(
            "--write writes nothing while a file does not parse",
            status == ExitCode.FINDINGS
            and "does not parse" in text
            and (root / writer.LIST).read_text(encoding="utf-8") == kept,
        )


def drift_cases(expect: testing.Expect) -> None:
    """Check drift() and --check: a tree with no list, and the list that --write writes."""
    with git.scratch_repository(TREE) as root:
        found = writer.drift(TsastContext(root))
        expect(
            "the drift of a tree with a check file and no list gives one error no-list for the rule derived-files",
            [(item.level, item.kind, item.check) for item in found if item.kind == "no-list"]
            == [("error", "no-list", "derived-files")],
        )
        status, _text = testing.captured(lambda: writer.checked(root))
        expect(
            "--check of a tree with no list exits 1 and writes no list",
            status == ExitCode.FINDINGS and not (root / writer.LIST).exists(),
        )
        testing.captured(lambda: writer.written(root))
        status, _text = testing.captured(lambda: writer.checked(root))
        expect("--check of the list that --write writes exits 0", status == ExitCode.OK)


def command_cases(expect: testing.Expect) -> None:
    """Check the include directory of a walk unit and the search of its command in the compile database."""
    expect(
        "an -I of a walk unit directory gives the directory",
        writer.unit_include(f"-I{UNIT_DIRECTORY}") == UNIT_DIRECTORY
        and writer.unit_include("-Ibuild/test/layer/walk_units/12") == "build/test/layer/walk_units/12",
    )
    expect(
        "an argument that is not an -I of a walk unit directory gives None",
        all(
            writer.unit_include(argument) is None
            for argument in (
                "-I/build/test/layer/walk_units/x",
                "-I/build/walk_units/3",
                "-I/build/test/layer/walk_units/",
                f"-D{UNIT_DIRECTORY}",
                UNIT_DIRECTORY,
            )
        ),
    )
    with tempfile.TemporaryDirectory(prefix="test-walk-units-") as scratch:
        build = Path(scratch)

        def refusal() -> str:
            """Give the message of the refusal of the search, or an empty text when the search finds a command."""
            try:
                writer.walk_unit_command(build)
            except writer.MeasureError as problem:
                return str(problem)
            return ""

        expect("a build with no compile database has no walk unit command", "does not exist" in refusal())
        compdb.plant(build, [unit_command(build)])
        found = writer.walk_unit_command(build)
        expect(
            "the search finds the walk unit and its include directory",
            found.unit_dir == f"{build}{UNIT_DIRECTORY}" and found.directory == str(build),
        )
        compdb.plant(build, [{"directory": str(build), "file": "/source/a.cpp", "command": "g++ -c a.cpp -o a.o"}])
        expect("a database with no walk unit is refused", "holds no walk unit" in refusal())
        compdb.plant(build, [unit_command(build, has_include=False)])
        expect("a walk unit with no -I of its directory is refused", "has no -I" in refusal())


def measure_cases(expect: testing.Expect) -> None:
    """Check --measure: a malformed list, a build with no database, and a measure with the compiler of the test."""
    with git.scratch_repository(TREE) as root, tempfile.TemporaryDirectory(prefix="test-walk-units-") as scratch:
        build = Path(scratch)
        list_path = root / writer.LIST
        malformed = writer.LIST_HEADER + "a row with no separator\n"
        list_path.write_text(malformed, encoding="utf-8")
        status, text = testing.captured(lambda: writer.measured(build, list_path, 1))
        expect(
            "--measure refuses a list with a malformed row, and writes nothing",
            status == ExitCode.FINDINGS
            and "no malformed row" in text
            and list_path.read_text(encoding="utf-8") == malformed,
        )
        listed = writer.LIST_HEADER + USES_ROW.format(weight=writer.UNMEASURED)
        list_path.write_text(listed, encoding="utf-8")
        status, text = testing.captured(lambda: writer.measured(build, list_path, 1))
        expect(
            "--measure refuses a build with no compile database, and writes nothing",
            status == ExitCode.FINDINGS
            and "does not exist" in text
            and list_path.read_text(encoding="utf-8") == listed,
        )
        compdb.plant(build, [unit_command(build)])
        probe = cost_meter.open_instruction_counter()
        has_counter = probe is not None and cost_meter.read_instruction_counter(probe) is not None
        status, text = testing.captured(lambda: writer.measured(build, list_path, 2, 3))
        rows = writer.read_list(list_path).rows
        if has_counter:
            expect(
                "--measure with three runs writes a measured weight to each row",
                status == ExitCode.OK
                and "the median of 3 measure(s)" in text
                and "fixy/Uses.cpp" in rows
                and rows["fixy/Uses.cpp"].weight != writer.UNMEASURED
                and writer.is_weight(rows["fixy/Uses.cpp"].weight),
            )
        else:
            expect(
                "--measure on a host with no instruction counter writes nothing",
                status == ExitCode.FINDINGS and list_path.read_text(encoding="utf-8") == listed,
            )


def median_cases(expect: testing.Expect) -> None:
    """Check the median of the weights of the runs: an outlier run, two runs and a weight below 0."""
    runs = [{"": 100, "a.cpp": 110}, {"": 90, "a.cpp": 102}, {"": 100, "a.cpp": 150}]
    expect(
        "the weight of a row is the median of its weights in the runs, and an outlier run does not move it",
        writer.median_weights(runs, ["a.cpp"]) == {"a.cpp": 12},
    )
    expect(
        "for two runs, the weight is the lower of the two middle weights",
        writer.median_weights(runs[:2], ["a.cpp"]) == {"a.cpp": 10},
    )
    expect(
        "a weight below 0 in a run counts as 0",
        writer.median_weights([{"": 100, "a.cpp": 90}], ["a.cpp"]) == {"a.cpp": 0},
    )


def explain_cases(expect: testing.Expect) -> None:
    """Check --explain: the path of a walk to the call of members_of, and a target that is not a check file."""
    with git.scratch_repository(TREE) as root:
        status, text = testing.captured(lambda: writer.explained(root, f"{writer.CHECKS}/fixy/Uses.cpp"))
        expect(
            "--explain prints the path of the walk to the call of members_of",
            status == ExitCode.OK
            and "fixy/Uses.cpp: joins the walk units" in text
            and "::foundation::tags" in text
            and "members_of at include/foundation/Walk.h" in text,
        )
        status, text = testing.captured(lambda: writer.explained(root, "crucible/NoWalk.cpp"))
        expect(
            "--explain takes a check file with no prefix, and tells that it does not join",
            status == ExitCode.OK and "does not join" in text,
        )
        status, _text = testing.captured(lambda: writer.explained(root, "crucible/Missing.cpp"))
        expect("--explain refuses a target that is not a check file", status == ExitCode.USAGE)


def usage_cases(expect: testing.Expect) -> None:
    """Check the usage errors of main: no mode, two modes and a number of jobs that is not 1 or more."""
    expect("main refuses a call with no mode", testing.captured(lambda: writer.main([]))[0] == ExitCode.USAGE)
    expect("main refuses two modes", testing.captured(lambda: writer.main(["--write", "--check"]))[0] == ExitCode.USAGE)
    expect(
        "main refuses --jobs 0",
        testing.captured(lambda: writer.main(["--measure", "build", "--jobs", "0"]))[0] == ExitCode.USAGE,
    )
    expect(
        "main refuses --runs 0",
        testing.captured(lambda: writer.main(["--measure", "build", "--runs", "0"]))[0] == ExitCode.USAGE,
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (write_cases, drift_cases, command_cases, measure_cases, median_cases, explain_cases, usage_cases),
    )
