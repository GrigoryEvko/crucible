"""The test of the writer of the row-hash golden (assay.loop.derive.row_hashes), on planted builds and goldens.

`guard selftest tests.loop.derive.test_row_hashes` runs the test.  A case
writes a planted golden into a scratch tree, and a planted reply of the CMake
file API (assay.readers.cmake.plant()) into a scratch build directory.  The
program of a planted build is a planted ELF file, or the command `true` of
the host.  The cases hold:
  * The finding of compared(): no finding for an equal golden, and one error
    for a golden with one changed byte and for a missing golden
  * The verdict of program_of() on a build with no reply and with no target
    dump_row_hashes
  * The verdict of program_of() on a program that the build has not made
  * The verdict of program_of() on a file that is no ELF file, and on a
    program of the machine aarch64
  * The output and the drift of a build with the program `true`
  * The command line of main.
Exit 0 when each case holds, else 1.
"""

import functools
import shutil
import tempfile
from pathlib import Path

from assay.core import testing
from assay.core.exit import ExitCode
from assay.guard.engines.meta.runner import NotApplicable
from assay.loop.derive import row_hashes
from assay.readers import cmake, elf

# The output of a planted program.
OUTPUT = b"# row_hash cross-build witness\nG01_Linear               0x1d459776daf5970d\n"
# The position of e_machine in the ELF header.
MACHINE_FIELD = slice(18, 20)


def planted_golden(root: Path, data: bytes) -> None:
    """Write a planted golden into a scratch tree."""
    (root / row_hashes.GOLDEN).parent.mkdir(parents=True, exist_ok=True)
    (root / row_hashes.GOLDEN).write_bytes(data)


def planted_build(build: Path, program: str | None) -> None:
    """Write a planted reply into a scratch build directory, with a target dump_row_hashes when a program is given."""
    target = {"name": row_hashes.TARGET, "type": "EXECUTABLE", "artifacts": [{"path": program}]}
    cmake.plant(build, targets=[] if program is None else [target])


def verdict_of(build: Path) -> str:
    """Return the class of the result of program_of(): the word program, the name of the exception."""
    try:
        row_hashes.program_of(build)
    except (NotApplicable, row_hashes.GoldenError) as problem:
        return type(problem).__name__
    return "program"


def compare_cases(expect: testing.Expect) -> None:
    """Hold the finding of compared() for an equal golden, a changed golden and a missing golden."""
    with tempfile.TemporaryDirectory(prefix="row-hashes-") as scratch_text:
        root = Path(scratch_text)
        expect(
            "a tree with no golden gives one error at the golden",
            [(item.level, item.path) for item in row_hashes.compared(root, OUTPUT)] == [("error", row_hashes.GOLDEN)],
        )
        planted_golden(root, OUTPUT)
        expect("a golden equal to the output byte for byte gives no finding", row_hashes.compared(root, OUTPUT) == [])
        planted_golden(root, OUTPUT.replace(b"0x1d45", b"0x1d46"))
        found = row_hashes.compared(root, OUTPUT)
        expect(
            "a golden with one changed digit gives one error of the rule derived-files",
            [(item.level, item.check) for item in found] == [("error", row_hashes.CHECK)],
        )
        planted_golden(root, OUTPUT.removesuffix(b"\n"))
        expect(
            "a golden with no last newline is not the output",
            len(row_hashes.compared(root, OUTPUT)) == 1,
        )


def program_cases(expect: testing.Expect) -> None:
    """Hold the verdict of program_of() on each kind of planted build."""
    with tempfile.TemporaryDirectory(prefix="row-hashes-") as scratch_text:
        scratch = Path(scratch_text)
        empty = scratch / "empty"
        empty.mkdir()
        expect("a build directory with no reply of the file API is an error", verdict_of(empty) == "GoldenError")
        other = scratch / "other"
        planted_build(other, None)
        expect("a build with no target dump_row_hashes does not apply", verdict_of(other) == "NotApplicable")
        unmade = scratch / "unmade"
        planted_build(unmade, str(unmade / "dump_row_hashes"))
        expect("a program that the build has not made is an error", verdict_of(unmade) == "GoldenError")
        text = scratch / "text"
        planted_build(text, str(text / "dump_row_hashes"))
        (text / "dump_row_hashes").write_text("no ELF file\n", encoding="utf-8")
        expect("a program that is no ELF file is an error", verdict_of(text) == "GoldenError")
        x86 = scratch / "x86"
        planted_build(x86, str(x86 / "dump_row_hashes"))
        (x86 / "dump_row_hashes").write_bytes(elf.make_file(()))
        expect("a program of the machine x86_64 is the program of the build", verdict_of(x86) == "program")
        arm = scratch / "arm"
        planted_build(arm, str(arm / "dump_row_hashes"))
        planted = bytearray(elf.make_file(()))
        planted[MACHINE_FIELD] = elf.EM_AARCH64.to_bytes(2, "little")
        (arm / "dump_row_hashes").write_bytes(bytes(planted))
        expect("a program of the machine aarch64 does not apply", verdict_of(arm) == "NotApplicable")
        expect(
            "a program of the machine x86_64 that cannot run gives an error, not an output",
            raises_golden_error(functools.partial(row_hashes.output_of, x86)),
        )


def raises_golden_error(action: functools.partial[bytes]) -> bool:
    """Return True when the action raises GoldenError."""
    try:
        action()
    except row_hashes.GoldenError:
        return True
    return False


def run_cases(expect: testing.Expect) -> None:
    """Hold the output and the drift of a build with the command `true` of the host as its program."""
    program = shutil.which("true")
    if program is None:
        expect("a host with no command `true` has no run case", True)
        return
    is_golden_machine = elf.members(program)[0].machine() == row_hashes.GOLDEN_MACHINE
    with tempfile.TemporaryDirectory(prefix="row-hashes-") as scratch_text:
        root, build = Path(scratch_text) / "tree", Path(scratch_text) / "build"
        root.mkdir()
        planted_build(build, program)
        if not is_golden_machine:
            expect("on a host of a different machine, the build does not apply", verdict_of(build) == "NotApplicable")
            return
        expect("the output of the command `true` is empty", row_hashes.output_of(build) == b"")
        planted_golden(root, b"")
        expect("an empty golden agrees with the empty output", row_hashes.drift(root, build) == [])
        planted_golden(root, OUTPUT)
        expect("a golden with lines is not the empty output", len(row_hashes.drift(root, build)) == 1)


# The command lines that main refuses as usage errors.
USAGE_WORDS = ((), ("--write", "--check"), ("--golden",))


def command_cases(expect: testing.Expect) -> None:
    """Hold the usage errors of main (USAGE_WORDS), and the modes with a directory that holds no build."""
    refused = [testing.captured(functools.partial(row_hashes.main, list(words)))[0] for words in USAGE_WORDS]
    expect(
        "main refuses a call with no mode, a call with two modes and an unknown word",
        refused == [ExitCode.USAGE] * len(USAGE_WORDS),
    )
    with tempfile.TemporaryDirectory(prefix="row-hashes-") as scratch_text:
        missing = [
            testing.captured(functools.partial(row_hashes.main, [mode, "--build-dir", scratch_text]))[0]
            for mode in ("--check", "--stdout", "--write")
        ]
        expect(
            "each mode with a directory that holds no build is a missing input",
            set(missing) == {ExitCode.MISSING_INPUT},
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (compare_cases, program_cases, run_cases, command_cases))
