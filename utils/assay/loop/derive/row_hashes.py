"""loop derive row-hashes — write utils/tools/row_hash_golden.txt, the output of dump_row_hashes, or compare it.

usage: loop derive row-hashes --write [--build-dir DIR]
       loop derive row-hashes --stdout [--build-dir DIR]
       loop derive row-hashes --check [--build-dir DIR]

THE GOLDEN
    utils/tools/dump_row_hashes.cpp prints a head with the toolchain tag.
    Then it prints one line for each entry of its matrix: the row hash of
    foundation/diag/RowHash.h of the entry.  The golden is that output, byte
    for byte.  It is the cross-build witness of the fold.  A static_assert in
    one translation unit cannot see a change of a reflected name in the fold.
    A moved line is a moved row hash, and the commit that writes the golden
    again names the cause.

THE BUILD
    The code model of the build directory (assay.readers.cmake) gives the file
    of the target dump_row_hashes.  The command runs that program with no
    argument through assay.core.proc.  DIR is the build directory, with
    REPO_ROOT/build as the default.  A build directory with no such target
    does not apply.  When the build has not made the program, or when its
    run does not give the status 0, the command gives an error.

THE MACHINE
    The payload lines fold display_string_of, and a reflected name can change
    with the machine.  The golden holds the values of one machine, x86_64
    (GOLDEN_MACHINE).  A program of a different
    machine gives different values, and its build does not apply.  The rows
    R** of the golden fold no reflected name, and each machine gives the same
    rows R**.  But the compare reads each line, and the gate applies to all
    lines of the file.

THE CHECK
    --check compares the golden with the output of the program of the build.
    The rule derived-files (assay.guard.engines.meta.derived_files) reads the
    same finding (drift()).  It gives a warning for a build that does not
    apply.

Exit 0 when the mode did its work, 1 on drift and 2 on a usage error.  Exit 3
when the build does not apply or its program cannot give the output.
"""

from __future__ import annotations

from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta.runner import NotApplicable
from assay.readers import cmake, elf

PROGRAM = "loop derive row-hashes"
# The rule of the registry that reads the findings of drift().
CHECK = "derived-files"
GOLDEN = "utils/tools/row_hash_golden.txt"
TARGET = "dump_row_hashes"
DEFAULT_BUILD = "build"
# The machine of the values of the golden (THE MACHINE).
GOLDEN_MACHINE = elf.EM_X86_64
GOLDEN_MACHINE_NAME = "x86_64"
WRITER = "python3 utils/bin/loop derive row-hashes --write"
# The most characters of the standard error of a run that the message of an error holds.
ERROR_TAIL = 2000


class GoldenError(Exception):
    """The output cannot be read: the code model cannot be read, the program is missing, or its run gives no output."""


def program_of(build_dir: Path) -> Path:
    """Return the program of the target dump_row_hashes of a build (THE BUILD, THE MACHINE).

    Complexity: one read of the code model, linear in its targets.

    Raises:
        NotApplicable: If the build has no such target, or its program is of a machine other than GOLDEN_MACHINE
        GoldenError: If the code model cannot be read, or the build has not made a program that the ELF reader reads
    """
    try:
        model = cmake.codemodel(build_dir)
    except cmake.CmakeError as problem:
        raise GoldenError(f"the code model of {build_dir} cannot be read: {problem}") from None
    artifacts = [Path(artifact) for target in model.targets if target.name == TARGET for artifact in target.artifacts]
    if not artifacts:
        raise NotApplicable(f"the build {build_dir} has no target {TARGET}")
    program = artifacts[0]
    if not program.is_file():
        raise GoldenError(f"the build has not made {program}.  Build the target {TARGET}")
    try:
        machine = elf.members(program)[0].machine()
    except (OSError, elf.ElfError, IndexError) as problem:
        raise GoldenError(f"the ELF reader cannot read {program}: {problem}") from None
    if machine != GOLDEN_MACHINE:
        raise NotApplicable(
            f"the golden holds the values of {GOLDEN_MACHINE_NAME}, and {program} is a program of the ELF machine "
            f"{machine}"
        )
    return program


def output_of(build_dir: Path) -> bytes:
    """Return the standard output of the program of the target dump_row_hashes of a build (THE BUILD).

    Raises:
        NotApplicable: If the build does not apply (program_of())
        GoldenError: If the program cannot be read, cannot start, or exits with a status other than 0
    """
    program = program_of(build_dir)
    try:
        result = proc.run(proc.Command([str(program)], timeout_s=proc.QUICK_S, cwd=build_dir))
    except proc.ToolFailed as problem:
        raise GoldenError(f"{program} cannot run: {problem}") from None
    if result.exit != 0:
        raise GoldenError(f"{program} exits with {result.exit}: {result.err[-ERROR_TAIL:]}")
    return result.stdout


def compared(root: Path, output: bytes) -> list[check_report.Finding]:
    """Return an error when the golden of the tree is not the output, byte for byte (THE CHECK)."""
    path = root / GOLDEN
    if path.is_file() and path.read_bytes() == output:
        return []
    message = (
        f"the golden is not the output of {TARGET} of the build, and a row hash moved.  Name the cause in the "
        f"commit message, and run: {WRITER} --build-dir BUILD_DIR"
    )
    return [check_report.Finding("error", GOLDEN, 0, CHECK, message)]


def drift(root: Path, build_dir: Path) -> list[check_report.Finding]:
    """Return an error when the golden of the tree is not the output of the program of the build (THE CHECK).

    Raises:
        NotApplicable: If the build does not apply (program_of())
        GoldenError: If the output cannot be read (output_of())
    """
    return compared(root, output_of(build_dir))


def main(argv: list[str]) -> int:
    """Write, print or compare the golden.

    Args:
        argv: The words after `loop derive row-hashes`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(PROGRAM, "write the golden of dump_row_hashes from the program of a build, or compare it")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true", help="write the golden")
    modes.add_argument("--stdout", action="store_true", help="print the output of the program")
    modes.add_argument("--check", action="store_true", help="compare the golden with the output of the program")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / DEFAULT_BUILD)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    try:
        output = output_of(options.build_dir.resolve())
    except (NotApplicable, GoldenError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    if options.check:
        differences = compared(REPO_ROOT, output)
        for finding in differences:
            check_report.write_line(finding.text(), is_error=True)
        return ExitCode.FINDINGS if differences else ExitCode.OK
    if options.stdout:
        check_report.write_line(output.decode("utf-8", "replace").removesuffix("\n"))
        return ExitCode.OK
    (REPO_ROOT / GOLDEN).write_bytes(output)
    check_report.write_line(f"{PROGRAM}: wrote {GOLDEN} ({len(output.splitlines())} lines).", is_error=True)
    return ExitCode.OK
