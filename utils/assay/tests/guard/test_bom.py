"""The test of the command guard bom (assay.guard.bom).

`guard selftest tests.guard.test_bom` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

THE CASES
    The cases read the build that ASSAY_TEST_BUILD_DIR names.  They hold the
    targets that the command finds, and the five layers of a static library
    and of a linked file.  They also hold an artifact that the command cannot
    read, and the exits of the command.  The registration of the test in
    test/CMakeLists.txt gives the build directory.  A build that only parses
    its units has no artifact, and the build cases then do not run.  A build
    of only some targets gives the layers of the first linked file that it
    made.
"""

import os
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import testing
from assay.core.paths import REPO_ROOT
from assay.guard import bom
from assay.guard.engines.elf import runner
from assay.tests.guard.engines.elf.test_runner import quiet

BUILD_VARIABLE = "ASSAY_TEST_BUILD_DIR"
HEADINGS = ("1. imports", "2. library code", "3. compiler calls", "4. link facts", "5. source facts")


def headings_of(lines: list[str]) -> list[str]:
    """Return the heading of each layer line of a bill, in its sequence."""
    return [heading for line in lines for heading in HEADINGS if line.startswith(f"{heading}:")]


def build_directory() -> Path | None:
    """Return the build directory of the test, or None for no build or for a build that only parses its units."""
    text = os.environ.get(BUILD_VARIABLE, "")
    if not text or not Path(text).is_dir() or build_config.is_parse_build(Path(text)):
        return None
    return Path(text).resolve()


def text_cases(expect: testing.Expect) -> None:
    """Hold the limit of a long list."""
    expect(
        "a list over the limit shows the limit and one line with the count of the other lines",
        bom.limited(["a", "b", "c"], 2) == [f"{bom.INDENT}a", f"{bom.INDENT}b", f"{bom.INDENT}... 1 more"],
    )
    expect("a list in the limit shows each line", bom.limited(["a"], 2) == [f"{bom.INDENT}a"])


def build_cases(expect: testing.Expect) -> None:
    """Hold the targets, the layers and the exits on the build of the test."""
    build = build_directory()
    if build is None:
        expect(f"{BUILD_VARIABLE} names a build, or the build only parses its units", BUILD_VARIABLE in os.environ)
        return
    context = runner.ElfContext(REPO_ROOT, build)
    library = bom.resolve(context, "foundation")
    expect(
        "the name of a static library of the root gives its target, with no directory",
        library is not None and library.is_library and library.path.name == "libfoundation.a" and not library.directory,
    )
    by_path = bom.resolve(context, str(library.path)) if library is not None else None
    expect("the path of the file of a target gives the same target", by_path == library)
    expect("a name of no target gives no target", bom.resolve(context, "no_such_target_of_the_build") is None)
    if library is not None:
        lines = bom.bill(context, library, 3)
        expect(
            "a static library gives the five layers, and each layer can be read",
            headings_of(lines) == list(HEADINGS) and not any("cannot be read" in line for line in lines),
        )
    # A build of only some targets holds only some linked files.
    made = next((item.path for item in context.linked_artifacts() if item.path.is_file()), None)
    program = bom.resolve(context, str(made)) if made is not None else None
    if program is not None:
        lines = bom.bill(context, program, 3)
        expect(
            "a linked file gives the five layers, and each layer can be read",
            not program.is_library
            and headings_of(lines) == list(HEADINGS)
            and not any("cannot be read" in line for line in lines),
        )
    ghost = bom.Artifact("ghost", runner.STATIC_LIBRARY, build / "libplanted_ghost.a", "")
    lines = bom.bill(context, ghost, 3)
    expect(
        "an artifact with no file gives one line for each layer that cannot be read, and no exception",
        sum("cannot be read" in line for line in lines) >= 4 and headings_of(lines) == list(HEADINGS),
    )
    status, text = quiet(lambda: bom.main(["--build-dir", str(build), "--limit", "2", "foundation"]))
    expect("the command prints the layers of a target and exits 0", status == 0 and "5. source facts" in text)
    status, _ = quiet(lambda: bom.main(["--build-dir", str(build), "no_such_target_of_the_build"]))
    expect("a name of no target exits 1", status == 1)
    status, _ = quiet(lambda: bom.main(["--build-dir", str(build), "--limit", "0", "foundation"]))
    expect("a limit of 0 is a usage error", status == 2)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (text_cases, build_cases))
