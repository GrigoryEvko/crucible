"""setup check — make sure that each installed tool and the cmake and the ctest of a build agree with their pins.

THE PIN OF CMAKE
    The rows of the pin cmake of utils/pins.txt give one version of CMake and
    ctest, with the SHA-256 of the manylinux wheel of CMake on PyPI for each
    platform.  The rows must give one version.  read_pin() reads them.  The
    configure step rejects a CMake of another version (cmake/CMakePin.cmake),
    and this module is the check that rejects a ctest of another version.

THE INSTALLED TOOLS
    `setup check [TOOL]...` makes sure that each installed tool of the tools
    directory (assay.setup.install) meets its pin, by default tree-sitter,
    ast-grep and cmake: the stamp, then the checks of the tool.  No network.

THE PROGRAMS OF A BUILD DIRECTORY
    CMakeCache.txt of a configured build directory holds CMAKE_COMMAND and
    CMAKE_CTEST_COMMAND: the cmake that configured the directory, and the
    ctest of the same install.  A module that operates on a build directory
    runs these two programs (configured_program), and it does not look in
    PATH.  PATH can hold a ctest of another version, and ctest does not
    reject a build directory that another version configured.  A module that
    configures a new build directory runs the cmake of PATH, and the
    configure step rejects that cmake when its version is not the pin.  A
    module that runs cmake or ctest with no build directory, for example a
    self-test outside ctest, takes the program of PATH only when that program
    gives the pinned version (pinned_program).

THE CHECK OF A BUILD (`setup check --build-dir BUILD_DIR`, the test cmake_pin)
    Each of these programs must give the pinned version:
    * The ctest that runs the check: the nearest ancestor process whose name
      in /proc/PID/comm is ctest.  The check runs /proc/PID/exe --version, so
      it reads the program of that process, also when a wrapper started it.
    * CMAKE_COMMAND and CMAKE_CTEST_COMMAND of the build directory.
    A program of another version is an error.  A run with no ctest ancestor,
    a program whose version the check cannot read and a pin table that the
    check cannot read are errors too.  Each finding has the place of the
    first row of the pin cmake.

Usage
    setup check [TOOL]...
    setup check --build-dir BUILD_DIR [--warnings-dir DIR]
    setup check --print-program BUILD_DIR cmake|ctest
    guard selftest setup.pins [--cmake CMAKE --ctest CTEST --make-program NINJA]

Exit 0 with no error, 1 on an error of a build, 2 on a usage error or a failed
self-test.  For the installed tools: 2 when a tool does not meet its pin, 3
when a tool is absent, 4 when the host has no pin of a tool, and the largest
of these when more than one tool fails.
"""

from __future__ import annotations

import functools
import os
import shutil
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from assay.core import pins as pin_table
from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT, SETUP

CHECK = "cmake-pin"
ROOT = REPO_ROOT
PIN_NAME = "cmake"
PROC = Path("/proc")
# The cache entry of each program of a build directory.
CACHE_ENTRIES = {"cmake": "CMAKE_COMMAND", "ctest": "CMAKE_CTEST_COMMAND"}
# A program that gives no version in this time is broken.
VERSION_TIMEOUT_SECONDS = 60
# The longest chain of ancestors that the check walks.  A chain from a test to
# its ctest has two or three links.
ANCESTOR_LIMIT = 64


class PinError(Exception):
    """The pin table, a build directory or a program does not give what the caller needs."""

    def __init__(self, message: str, line: int = 0) -> None:
        """Keep the message and the line of the pin table that it is about, or 0."""
        super().__init__(message)
        self.line = line


@dataclass(frozen=True, slots=True)
class CmakePin:
    """The pinned version of CMake, the SHA-256 of the wheel of each platform, and the line of the first row."""

    version: str
    digests: dict[str, str]
    line: int


# ── The pin ────────────────────────────────────────────────────────────────


def read_pin(table: tuple[pin_table.Pin, ...] | None = None) -> CmakePin:
    """Read the rows of the pin cmake.

    Complexity: O(n) in the rows of the table.

    Args:
        table: The pins, by default the pin table of the tree

    Returns:
        The pin of CMake

    Raises:
        PinError: If the table cannot be read, has no row of cmake, or its rows give two versions
    """
    try:
        pins = pin_table.load() if table is None else table
    except (rows_grammar.RowsError, OSError) as error:
        raise PinError(f"the pin table cannot be read: {error}", getattr(error, "line", 0)) from None
    rows = sorted(pin_table.named(pins, PIN_NAME), key=lambda pin: pin.line)
    if not rows:
        raise PinError(f"{pin_table.PINS_PATH} has no row of the pin {PIN_NAME}.")
    versions = sorted({pin.version for pin in rows})
    if len(versions) > 1:
        raise PinError(f"{pin_table.PINS_PATH} gives the two versions {versions[0]} and {versions[1]} of "
                       f"{PIN_NAME}.  Give one version on each row.", rows[1].line)
    return CmakePin(version=versions[0], digests={pin.platform: pin.digest for pin in rows}, line=rows[0].line)


def install_hint() -> str:
    """Return the sentence that tells how to install the pinned CMake and ctest."""
    return (f"Install the pinned CMake and ctest with `python3 {ROOT}/utils/bin/setup install cmake`, and put the bin "
            f"directory that `python3 {ROOT}/utils/bin/setup install --print-bin cmake` prints first in PATH.")


# ── Programs ───────────────────────────────────────────────────────────────


def program_version(program: str | Path) -> tuple[str, str] | None:
    """Return the name and the version that `PROGRAM --version` gives on its first line, or None.

    The first line is `cmake version X` or `ctest version X`.

    Args:
        program: The path of a cmake or a ctest

    Returns:
        ("cmake" or "ctest", version), or None when the program does not run or gives no such line
    """
    try:
        result = proc.run(proc.Command([str(program), "--version"], timeout_s=VERSION_TIMEOUT_SECONDS))
    except proc.ToolFailed:
        return None
    lines = result.out.splitlines()
    words = lines[0].split(" ") if result.exit == 0 and lines else []
    if len(words) == 3 and words[0] in CACHE_ENTRIES and words[1] == "version" and words[2]:
        return words[0], words[2]
    return None


def configured_program(build_dir: Path, name: str) -> str:
    """Return the cmake or the ctest of a configured build directory, from its CMakeCache.txt.

    Args:
        build_dir: The build directory
        name: "cmake" or "ctest"

    Returns:
        The path of the program

    Raises:
        PinError: If the directory holds no CMakeCache.txt, the cache has no entry of the program,
            or the program does not exist
    """
    entry = CACHE_ENTRIES[name]
    cache = build_dir / "CMakeCache.txt"
    try:
        text = cache.read_text(encoding="utf-8", errors="replace")
    except OSError:
        raise PinError(f"{build_dir} holds no CMakeCache.txt.  Configure it first, for example with "
                       f"`cmake --preset default`.") from None
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if separator and key.split(":", 1)[0] == entry:
            program = value.strip()
            if not Path(program).is_file():
                raise PinError(f"{cache} names the {name} {program}, which does not exist.  Configure the build "
                               f"directory again with the pinned CMake.  {install_hint()}")
            return program
    raise PinError(f"{cache} has no entry {entry}.  Configure the build directory again.")


@functools.cache
def pinned_program(name: str) -> str:
    """Return the cmake or the ctest of PATH, when it gives the pinned version.

    Use it only where no build directory exists.  The result of each name is
    kept for the life of the process.

    Args:
        name: "cmake" or "ctest"

    Returns:
        The path of the program

    Raises:
        PinError: If PATH holds no such program, or the program gives another version
    """
    pin = read_pin()
    program = shutil.which(name)
    if program is None:
        raise PinError(f"{name} is not in PATH.  {install_hint()}")
    found = program_version(program)
    if found != (name, pin.version):
        shown = f"{found[0]} {found[1]}" if found else "no version"
        raise PinError(f"the {name} of PATH, {program}, gives {shown}, but {pin_table.PINS_PATH} pins "
                       f"{pin.version}.  {install_hint()}")
    return program


def parent_pid(proc_root: Path, pid: int) -> int | None:
    """Return the parent of a process from PROC_ROOT/PID/stat, or None when the file cannot be read.

    The name of a process in that file is in parentheses and can hold any
    character, so the fields start after the last ')'.
    """
    try:
        text = (proc_root / str(pid) / "stat").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    fields = text.rpartition(")")[2].split()
    return int(fields[1]) if len(fields) > 1 and fields[1].isdigit() else None


def ancestor_ctest(proc_root: Path = PROC, pid: int | None = None) -> Path | None:
    """Return PROC_ROOT/PID/exe of the nearest ancestor of a process whose name is ctest, or None.

    Complexity: O(d) reads for d ancestors, at most ANCESTOR_LIMIT.

    Args:
        proc_root: The process file system, or a copy of its shape in a self-test
        pid: The process whose ancestors the walk reads, by default this process

    Returns:
        The exe link of the ctest process, or None when no ancestor is a ctest
    """
    current = os.getpid() if pid is None else pid
    for _ in range(ANCESTOR_LIMIT):
        parent = parent_pid(proc_root, current)
        if parent is None or parent < 1 or parent == current:
            return None
        try:
            name = (proc_root / str(parent) / "comm").read_text(encoding="utf-8", errors="replace").strip()
        except OSError:
            return None
        if name == "ctest":
            return proc_root / str(parent) / "exe"
        current = parent
    return None


# ── The check of a build ───────────────────────────────────────────────────


def judge_program(pin: CmakePin, name: str, program: str | Path, what: str, remedy: str) -> check_report.Finding | None:
    """Return an error when a program does not give the pinned version, else None.

    Args:
        pin: The pin
        name: "cmake" or "ctest", the program that the place must hold
        program: The program to run.  A link of /proc runs the program of its process
        what: The role and the path of the program, for the message
        remedy: The sentence that tells how to correct the error
    """
    found = program_version(program)
    if found == (name, pin.version):
        return None
    shown = f"{found[0]} {found[1]}" if found else "no version on the first line of --version"
    return check_report.Finding("error", pin_table.PINS_PATH, pin.line, CHECK,
                                f"{what} gives {shown}, but this file pins {name} {pin.version}.  {remedy}")


def link_target(link: Path) -> str:
    """Return the path that a link names, or the link itself when it cannot be read."""
    try:
        return os.readlink(link)
    except OSError:
        return str(link)


def check(build_dir: Path, warnings_dir: Path | None, proc_root: Path = PROC, pid: int | None = None,
          table: tuple[pin_table.Pin, ...] | None = None) -> int:
    """Check the ctest that runs this process and the two programs of the build directory against the pin.

    Args:
        build_dir: The build directory of the run
        warnings_dir: The warnings directory, or None
        proc_root: The process file system, or a copy of its shape in a self-test
        pid: The process whose ctest ancestor the check reads, by default this process
        table: The pins, by default the pin table of the tree

    Returns:
        1 when one finding or more is an error, else 0
    """
    try:
        pin = read_pin(table)
    except PinError as error:
        return check_report.emit([check_report.Finding("error", pin_table.PINS_PATH, error.line, CHECK, str(error))],
                                 CHECK, warnings_dir)
    findings: list[check_report.Finding] = []
    try:
        configured_ctest = configured_program(build_dir, "ctest")
    except PinError:
        configured_ctest = "the ctest of the build directory"
    runner = ancestor_ctest(proc_root, pid)
    if runner is None:
        findings.append(check_report.Finding(
            "error", pin_table.PINS_PATH, pin.line, CHECK,
            f"no ctest runs this check, so it cannot read the version of the ctest of the run.  Run the test "
            f"cmake_pin with {configured_ctest}."))
    else:
        found = judge_program(pin, "ctest", runner, f"the ctest that runs the tests ({link_target(runner)})",
                              f"Run the tests with the ctest of the build directory, {configured_ctest}.")
        findings.extend([found] if found else [])
    for name, entry in CACHE_ENTRIES.items():
        try:
            program = configured_program(build_dir, name)
        except PinError as error:
            findings.append(check_report.Finding("error", pin_table.PINS_PATH, pin.line, CHECK, str(error)))
            continue
        found = judge_program(pin, name, program, f"{entry} of {build_dir} ({program})",
                              f"Configure the build directory again with the pinned CMake.  {install_hint()}")
        findings.extend([found] if found else [])
    if not findings:
        print(f"cmake-pin: the ctest of the run and the cmake and the ctest of {build_dir} give {pin.version}, the "
              f"pinned version.")
    return check_report.emit(findings, CHECK, warnings_dir)


# ── The check of the installed tools ───────────────────────────────────────


def check_tools(names: list[str], root: Path | None = None) -> int:
    """Make sure that each named tool of the tools directory meets its pin (THE INSTALLED TOOLS).

    Args:
        names: The tools, each a tool of assay.setup.install
        root: The tools directory, by default tools_root() of assay.core.cache

    Returns:
        0 when each tool meets its pin, else the largest exit status of a tool that fails
    """
    from assay.core import cache
    from assay.setup import install

    table = pin_table.load()
    base = cache.tools_root() if root is None else root
    worst = 0
    for name in names:
        tool = install.TOOLS[name]
        try:
            pin = install.pin_of(tool, table)
            directory = install.directory_of(tool, pin, base, table)
            install.verify_installed(tool, directory, pin, table)
        except install.InstallError as error:
            print(f"setup check {name}: {error}", file=sys.stderr)
            worst = max(worst, error.status)
            continue
        print(f"setup check {name}: {directory} is at the pin ({pin.version}).", file=sys.stderr)
    return worst


# ── Self-test ──────────────────────────────────────────────────────────────


def write_program(path: Path, text: str, status: int = 0) -> Path:
    """Write a shell program that prints TEXT and exits with STATUS, and return its path."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"#!/bin/sh\nprintf '%s\\n' '{text}'\nexit {status}\n", encoding="utf-8")
    path.chmod(0o755)
    return path


def write_process(proc_root: Path, pid: int, parent: int, name: str, exe: Path | None = None) -> None:
    """Write the stat, comm and exe entries of one process into a copy of the shape of /proc."""
    directory = proc_root / str(pid)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "stat").write_text(f"{pid} ({name} with ) and spaces) S {parent} {pid} 0 0\n", encoding="utf-8")
    (directory / "comm").write_text(f"{name}\n", encoding="utf-8")
    if exe is not None:
        (directory / "exe").symlink_to(exe)


def planted_table(*versions: str) -> tuple[pin_table.Pin, ...]:
    """Return a pin table with one row of cmake for each version, each for a platform of its own."""
    platforms = ("linux-x86_64", "linux-aarch64", "darwin-x86_64")
    rows = [f"pin cmake via=archive version={version} platform={platforms[index]} "
            f"source=https://example.invalid/cmake.whl sha256={'0' * 64} | a planted pin"
            for index, version in enumerate(versions)]
    return pin_table.parse("# a planted pin table\nformat 1\n" + "".join(f"{row}\n" for row in rows), "planted")


def self_test(cmake: str | None, ctest: str | None, make_program: str | None) -> int:
    """Plant each kind of pin, program, cache and process chain, and check each verdict.

    With CMAKE, CTEST and MAKE_PROGRAM, the self-test also runs the check
    under a real ctest in a scratch project.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: str = "") -> None:
        """Record one case and print it."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}\n{detail}" if detail else name)

    def refuses(name: str, action: Callable[[], object], fragment: str) -> None:
        """Expect ACTION to raise a PinError whose message holds FRAGMENT."""
        try:
            action()
        except PinError as error:
            expect(name, fragment in str(error), str(error))
            return
        expect(name, False, "no PinError")

    print("setup.pins --self-test")
    tree = read_pin()
    numbers = tree.version.split(".")
    expect("the pin of the tree gives one version X.Y.Z", len(numbers) == 3 and all(part.isdigit() for part in numbers))
    expect("the pin of the tree gives a digest for linux-x86_64 and for linux-aarch64",
           set(tree.digests) == {"linux-x86_64", "linux-aarch64"} and all(len(d) == 64 for d in tree.digests.values()))
    expect("the first row of cmake of the tree is the place of a finding",
           pin_table.PINS.read_text(encoding="utf-8").splitlines()[tree.line - 1].startswith("pin cmake "))
    expect("a pin of two platforms reads", read_pin(planted_table("4.4.2", "4.4.2")).version == "4.4.2")
    refuses("a pin of two versions is an error", lambda: read_pin(planted_table("4.4.2", "4.4.3")),
            "two versions 4.4.2 and 4.4.3")
    refuses("a table with no row of cmake is an error", lambda: read_pin(planted_table()), "has no row of the pin")

    with tempfile.TemporaryDirectory(prefix="cmake-pin-") as directory:
        work = Path(directory)
        programs = work / "programs"
        pinned_ctest = write_program(programs / "pinned" / "ctest", f"ctest version {tree.version}")
        pinned_cmake = write_program(programs / "pinned" / "cmake", f"cmake version {tree.version}")
        other_ctest = write_program(programs / "other" / "ctest", "ctest version 4.3.0")
        other_cmake = write_program(programs / "other" / "cmake", "cmake version 4.3.0")
        expect("program_version reads the first line", program_version(other_ctest) == ("ctest", "4.3.0"))
        expect("program_version gives None for a program that fails",
               program_version(write_program(programs / "failing", f"ctest version {tree.version}", 1)) is None)
        expect("program_version gives None for a line of another shape",
               program_version(write_program(programs / "shapeless", "GNU bash, version 5.2")) is None)
        expect("program_version gives None for an absent program", program_version(programs / "absent") is None)

        def build(name: str, cmake_program: Path | None, ctest_program: Path | None) -> Path:
            build_dir = work / "builds" / name
            build_dir.mkdir(parents=True)
            entries = [f"CMAKE_COMMAND:INTERNAL={cmake_program}"] if cmake_program else []
            entries += [f"CMAKE_CTEST_COMMAND:INTERNAL={ctest_program}"] if ctest_program else []
            (build_dir / "CMakeCache.txt").write_text("# a cache\n" + "".join(f"{e}\n" for e in entries),
                                                      encoding="utf-8")
            return build_dir

        pinned_build = build("pinned", pinned_cmake, pinned_ctest)
        expect("configured_program reads CMAKE_CTEST_COMMAND",
               configured_program(pinned_build, "ctest") == str(pinned_ctest))
        refuses("a build directory with no cache is an error", lambda: configured_program(work / "absent", "cmake"),
                "holds no CMakeCache.txt")
        refuses("a cache with no CMAKE_CTEST_COMMAND is an error",
                lambda: configured_program(build("no_ctest", pinned_cmake, None), "ctest"),
                "has no entry CMAKE_CTEST_COMMAND")
        refuses("a cache that names an absent program is an error",
                lambda: configured_program(build("absent_program", programs / "absent", pinned_ctest), "cmake"),
                "which does not exist")

        saved_path = os.environ.get("PATH", "")
        try:
            os.environ["PATH"] = f"{programs / 'other'}:{saved_path}"
            pinned_program.cache_clear()
            refuses("a ctest of another version in PATH is an error", lambda: pinned_program("ctest"),
                    "gives ctest 4.3.0, but ")
            os.environ["PATH"] = f"{programs / 'pinned'}:{saved_path}"
            pinned_program.cache_clear()
            expect("the pinned ctest in PATH is the program", pinned_program("ctest") == str(pinned_ctest))
        finally:
            os.environ["PATH"] = saved_path
            pinned_program.cache_clear()

        # A copy of the shape of /proc: the check (500) runs under cmake -E env
        # (400), which runs under a ctest (300).  The ctest of a chain is the
        # program that its exe link names.
        def chain(name: str, runner: Path | None, runner_name: str = "ctest") -> Path:
            proc_root = work / "proc" / name
            write_process(proc_root, 500, 400, "python3")
            write_process(proc_root, 400, 300, "cmake")
            write_process(proc_root, 300, 1, runner_name, runner)
            return proc_root

        expect("the walk finds the ctest above cmake -E env",
               ancestor_ctest(chain("found", pinned_ctest), 500) == work / "proc" / "found" / "300" / "exe")
        expect("the walk finds no ctest in a chain with none",
               ancestor_ctest(chain("none", None, "bash"), 500) is None)
        looping = work / "proc" / "loop"
        write_process(looping, 500, 400, "python3")
        write_process(looping, 400, 500, "cmake")
        expect("the walk stops in a chain with a loop", ancestor_ctest(looping, 500) is None)

        def verdict(name: str, proc_root: Path, build_dir: Path) -> tuple[int, str]:
            captured = work / f"{name}.out"
            saved = sys.stdout
            with open(captured, "w", encoding="utf-8") as handle:
                sys.stdout = handle
                try:
                    with check_report.github_actions(False):
                        status = check(build_dir, None, proc_root, 500)
                finally:
                    sys.stdout = saved
            return status, captured.read_text(encoding="utf-8")

        status, said = verdict("all_pinned", chain("all_pinned", pinned_ctest), pinned_build)
        expect("the check passes when each program gives the pin", status == 0 and "error" not in said, said)
        status, said = verdict("other_runner", chain("other_runner", other_ctest), pinned_build)
        expect("the check reports a ctest of another version that runs the tests",
               status == 1 and "the ctest that runs the tests" in said and "gives ctest 4.3.0" in said
               and f"{pin_table.PINS_PATH}:{tree.line}: error: [cmake-pin]" in said, said)
        status, said = verdict("other_cmake", chain("other_cmake", pinned_ctest),
                               build("other_cmake", other_cmake, pinned_ctest))
        expect("the check reports a CMAKE_COMMAND of another version",
               status == 1 and "CMAKE_COMMAND of" in said and "gives cmake 4.3.0" in said, said)
        status, said = verdict("other_cache_ctest", chain("other_cache_ctest", pinned_ctest),
                               build("other_cache_ctest", pinned_cmake, other_ctest))
        expect("the check reports a CMAKE_CTEST_COMMAND of another version",
               status == 1 and "CMAKE_CTEST_COMMAND of" in said, said)
        status, said = verdict("no_runner", chain("no_runner", None, "bash"), pinned_build)
        expect("the check reports a run with no ctest", status == 1 and "no ctest runs this check" in said, said)
        status, said = verdict("unread_runner", chain("unread_runner", programs / "shapeless"), pinned_build)
        expect("the check reports a ctest whose version it cannot read",
               status == 1 and "gives no version" in said, said)

        if cmake and ctest and make_program:
            real_run(work, cmake, ctest, make_program, expect)

    if failures:
        print(f"setup.pins --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 2
    print("setup.pins --self-test: every case holds.")
    return 0


def real_run(work: Path, cmake: str, ctest: str, make_program: str, expect: Callable[..., None]) -> None:
    """Run the check as a test of a scratch project under a real ctest, and check that it passes.

    The run shows that the walk finds a real ctest process, and that its exe
    link gives the version of the program.
    """
    source, build_dir = work / "real" / "src", work / "real" / "build"
    source.mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.25)\nproject(cmake_pin_probe NONE)\nenable_testing()\n"
        f'add_test(NAME probe COMMAND "{sys.executable}" "{SETUP}" check --build-dir "{build_dir}")\n',
        encoding="utf-8")
    try:
        configured = proc.run(proc.Command([cmake, "-S", str(source), "-B", str(build_dir), "-G", "Ninja",
                                            f"-DCMAKE_MAKE_PROGRAM={make_program}"], timeout_s=proc.TOOL_S))
    except proc.ToolFailed as problem:
        expect("the scratch project configures", False, str(problem))
        return
    expect("the scratch project configures", configured.exit == 0, configured.out + configured.err)
    if configured.exit != 0:
        return
    try:
        run = proc.run(proc.Command([ctest, "--test-dir", str(build_dir), "--output-on-failure"],
                                    timeout_s=proc.TOOL_S))
    except proc.ToolFailed as problem:
        expect("the check passes under the ctest of the build", False, str(problem))
        return
    expect("the check passes under the ctest of the build", run.exit == 0, run.out + run.err)


# ── Main ───────────────────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check of a build, the check of the installed tools, a print mode or the self-test.

    Args:
        argv: The words after `setup check`, or --self-test and its words

    Returns:
        The exit status of the mode
    """
    from assay.core import cli
    from assay.setup import install

    parser = cli.parser("setup check", __doc__.split("\n", 1)[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--print-program", nargs=2, metavar=("BUILD_DIR", "PROGRAM"),
                      help="print the cmake or the ctest of a configured build directory")
    mode.add_argument("--build-dir", type=Path, help="check the cmake and the ctest of a build directory")
    mode.add_argument("--self-test", action="store_true", help="plant each case and check each verdict")
    parser.add_argument("--cmake", help="the cmake of the real run of --self-test")
    parser.add_argument("--ctest", help="the ctest of the real run of --self-test")
    parser.add_argument("--make-program", help="the ninja of the real run of --self-test")
    parser.add_argument("tools", nargs="*", metavar="TOOL", help=f"one of {', '.join(install.TOOLS)}")
    check_report.add_arguments(parser)
    options = parser.parse_args(argv)
    if options.self_test:
        return self_test(options.cmake, options.ctest, options.make_program)
    unknown = [name for name in options.tools if name not in install.TOOLS]
    if unknown:
        parser.error(f"{', '.join(unknown)} is not a tool.  The tools are {', '.join(install.TOOLS)}")
    if options.print_program:
        build_dir, name = options.print_program
        if name not in CACHE_ENTRIES:
            parser.error(f"the program is cmake or ctest, not {name}")
        try:
            print(configured_program(Path(build_dir), name))
        except PinError as error:
            print(f"setup check: {error}", file=sys.stderr)
            return 1
        return 0
    if options.build_dir is not None:
        return check(options.build_dir.resolve(), options.warnings_dir)
    return check_tools(options.tools or list(install.DEFAULT_TOOLS))
