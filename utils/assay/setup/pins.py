"""setup check — make sure that each installed tool and the cmake and the ctest of a build agree with their pins.

THE ROWS OF THE TABLE (the rule tool-pins of utils/registry.txt)
    check_table() holds each row of utils/pins.txt to its holder, and
    holder() gives the holder of each pin name:
      build             cmake: THE CHECK OF A BUILD, and the installed tool
                        cmake as a tool row
      tool              a pin of a tool of assay.setup.install, or a pin of a
                        part of it (the lineage of the kit, the packages of
                        mypy).  When the tools directory holds the directory of
                        the tool for this host, that directory must meet its
                        pin.  A tool that is not installed gives no finding
      compiler          gcc: the C++ compiler of the CMake cache of the build
                        gives the pinned version with -dumpfullversion
      host              python: the interpreter of the check has the pinned
                        version or a version after it
      oracle-toolchain  temurin-jdk, sbt and mcrl2 (assay.setup.oracle): the
                        directory of the toolchain holds no install of the pin
                        that stopped (NAME-VERSION.partial)
      oracle-source     the pins of assay.oracle.sources.SOURCES: the row is a
                        git source on GitHub or an archive
      cross-package     the pins of assay.setup.gcc.CROSS_PINS: the row is an
                        archive with a source and a sha256 field
    A row with no holder is an error, and a new pin then gets a check before
    it lands.  Only the row of this host gets the check of its holder.  The
    rows of cmake give one version for each platform.  THE CHECK OF A BUILD
    reads them all.  It runs one time when the call gives a build directory,
    also when no row of cmake is for this host.  `setup check` with no TOOL
    and the rule call check_table().

THE PIN OF CMAKE
    The rows of the pin cmake of utils/pins.txt give one version of CMake and
    ctest, with the SHA-256 of the manylinux wheel of CMake on PyPI for each
    platform.  The rows must give one version.  read_pin() reads them.  The
    configure step rejects a CMake of another version (cmake/CMakePin.cmake),
    and this module is the check that rejects a ctest of another version.

THE INSTALLED TOOLS
    `setup check TOOL...` makes sure that each named tool of the tools
    directory (assay.setup.install) is installed and meets its pin: the
    stamp, then the checks of the tool.  No network.

THE PROGRAMS OF A BUILD DIRECTORY
    The CMake cache of a configured build directory holds CMAKE_COMMAND and
    CMAKE_CTEST_COMMAND: the cmake that configured the directory, and the
    ctest of the same install.  The reply cache-v2 of the file API gives them
    (assay.readers.cmake).  A module that operates on a build directory
    runs these two programs (configured_program), and it does not look in
    PATH.  PATH can hold a ctest of another version, and ctest does not
    reject a build directory that another version configured.  A module that
    configures a new build directory runs the cmake of PATH, and the
    configure step rejects that cmake when its version is not the pin.  A
    module that runs cmake or ctest with no build directory, for example a
    self-test outside ctest, takes the program of PATH only when that program
    gives the pinned version (pinned_program).

THE CHECK OF A BUILD (`setup check --build-dir BUILD_DIR`, the rule tool-pins)
    Each of these programs must give the pinned version:
    * The ctest that runs the check: the nearest ancestor process whose name
      in /proc/PID/comm is ctest.  The check runs /proc/PID/exe --version, so
      it reads the program of that process, also when a wrapper started it.
    * CMAKE_COMMAND and CMAKE_CTEST_COMMAND of the build directory.
    A program of another version is an error.  A run with no ctest ancestor,
    a program whose version the check cannot read and a table with no row of
    cmake or with two versions of it are errors too.  Each finding has the
    place of the first row of the pin cmake.

Usage
    setup check [--build-dir BUILD_DIR] [--warnings-dir DIR]
    setup check TOOL...
    setup check --print-program BUILD_DIR cmake|ctest

Exit 0 with no error, 1 on an error of the table, 2 on a usage error.  For
the named tools, the exit is 2 when a tool does not meet its pin.  It is 3
when a tool is not installed, and 4 when the host has no pin of a tool.
When more than one tool fails, the exit is the largest of these.
`guard selftest tests.setup.test_pins` does a test of the module, and the
plants of the rule tool-pins are the tests of check_table().
"""

from __future__ import annotations

import enum
import functools
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

from assay.core import pins as pin_table
from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.readers import cmake as cmake_reader

CHECK = "tool-pins"
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
    """Return the cmake or the ctest of a configured build directory, from its CMake cache (assay.readers.cmake).

    Args:
        build_dir: The build directory
        name: "cmake" or "ctest"

    Returns:
        The path of the program

    Raises:
        PinError: If the CMake cache of the directory cannot be read, the cache has no entry of the program,
            or the program does not exist
    """
    entry = CACHE_ENTRIES[name]
    try:
        found = cmake_reader.cache(build_dir).get(entry)
    except cmake_reader.CmakeError as problem:
        raise PinError(f"the CMake cache of {build_dir} cannot be read: {problem}") from None
    if found is None:
        raise PinError(f"the CMake cache of {build_dir} has no entry {entry}.  {cmake_reader.AGAIN}")
    program = found.value.strip()
    if not Path(program).is_file():
        raise PinError(f"the CMake cache of {build_dir} names the {name} {program}, which does not exist.  Configure "
                       f"the build directory again with the pinned CMake.  {install_hint()}")
    return program


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


def judge_program(pin: CmakePin, name: str, program: str | Path, what: str, remedy: str,
                  kind: str = "build-program", entity: str = "") -> check_report.Finding | None:
    """Return an error when a program does not give the pinned version, else None.

    Args:
        pin: The pin
        name: "cmake" or "ctest", the program that the place must hold
        program: The program to run.  A link of /proc runs the program of its process
        what: The role and the path of the program, for the message
        remedy: The sentence that tells how to correct the error
        kind: The kind of the finding: build-program for a program of the build, run-program for the ctest of the run
        entity: The entity of the finding, by default the name of the program
    """
    found = program_version(program)
    if found == (name, pin.version):
        return None
    shown = f"{found[0]} {found[1]}" if found else "no version on the first line of --version"
    return check_report.Finding("error", pin_table.PINS_PATH, pin.line, CHECK,
                                f"{what} gives {shown}, but this file pins {name} {pin.version}.  {remedy}",
                                kind, entity or name)


def link_target(link: Path) -> str:
    """Return the path that a link names, or the link itself when it cannot be read."""
    try:
        return os.readlink(link)
    except OSError:
        return str(link)


def build_findings(pin: CmakePin, build_dir: Path, proc_root: Path, pid: int | None) -> list[check_report.Finding]:
    """Check the ctest that runs this process and the two programs of the build directory against the pin.

    Args:
        pin: The pin of CMake
        build_dir: The build directory of the run
        proc_root: The process file system, or a copy of its shape for a plant
        pid: The process of the check, with a ctest ancestor that the check reads, by default this process

    Returns:
        One error for each program that does not give the pinned version, at the first row of the pin
    """
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
            f"guard_tool_pins with {configured_ctest}.", "run-program", "ctest"))
    else:
        found = judge_program(pin, "ctest", runner, f"the ctest that runs the tests ({link_target(runner)})",
                              f"Run the tests with the ctest of the build directory, {configured_ctest}.",
                              "run-program")
        findings.extend([found] if found else [])
    for name, entry in CACHE_ENTRIES.items():
        try:
            program = configured_program(build_dir, name)
        except PinError as error:
            findings.append(check_report.Finding("error", pin_table.PINS_PATH, pin.line, CHECK, str(error),
                                                 "build-program", entry))
            continue
        found = judge_program(pin, name, program, f"{entry} of {build_dir} ({program})",
                              f"Configure the build directory again with the pinned CMake.  {install_hint()}",
                              "build-program", entry)
        findings.extend([found] if found else [])
    return findings


class Holder(enum.StrEnum):
    """The check that holds the rows of one pin name (THE ROWS OF THE TABLE)."""

    BUILD = "build"
    TOOL = "tool"
    COMPILER = "compiler"
    HOST = "host"
    ORACLE_TOOLCHAIN = "oracle-toolchain"
    ORACLE_SOURCE = "oracle-source"
    CROSS_PACKAGE = "cross-package"


# The pin of the minimum version of Python, which each master holds (assay.core.pins).
PYTHON_PIN = "python"


def tool_of(name: str) -> str | None:
    """Return the tool of assay.setup.install that a pin is a part of: the pin of the tool, a package or the kit lineage."""
    from assay.setup import install

    for tool in install.TOOLS.values():
        if name == tool.pin or name in tool.requires:
            return tool.name
    return "tree-sitter" if name in install.LINEAGE_PINS else None


def holder(name: str) -> Holder | None:
    """Return the holder of the rows of a pin name, or None for a pin that no check holds (THE ROWS OF THE TABLE)."""
    from assay.oracle import sources
    from assay.setup import gcc, oracle

    if name == PIN_NAME:
        return Holder.BUILD
    if tool_of(name) is not None:
        return Holder.TOOL
    if name == gcc.PIN_NAME:
        return Holder.COMPILER
    if name == PYTHON_PIN:
        return Holder.HOST
    if name in oracle.TOOLCHAIN_PINS:
        return Holder.ORACLE_TOOLCHAIN
    if name in sources.SOURCES:
        return Holder.ORACLE_SOURCE
    if name in gcc.CROSS_PINS:
        return Holder.CROSS_PACKAGE
    return None


@dataclass(frozen=True, slots=True)
class TableCheck:
    """The inputs of check_table(): the tree, the build, the tools directory, the toolchain of the oracle and the processes.

    `root` is the tree with the file utils/pins.txt that the check reads.  `build_dir`
    is None when the check reads no build.  `tools_root` and `oracle_root`
    are None for the directories of assay.core.cache and assay.setup.oracle.
    `proc_root` and `pid` give the process chain of THE CHECK OF A BUILD.
    """

    root: Path
    build_dir: Path | None = None
    tools_root: Path | None = None
    oracle_root: Path | None = None
    proc_root: Path = PROC
    pid: int | None = None


def row_error(pin: pin_table.Pin, kind: str, message: str) -> check_report.Finding:
    """Return an error of the rule at the row of a pin."""
    return check_report.Finding("error", pin_table.PINS_PATH, pin.line, CHECK, message, kind, pin.name)


def tool_findings(name: str, table: tuple[pin_table.Pin, ...], tools_root: Path) -> list[check_report.Finding]:
    """Check the installed directory of one tool for this host, when the tools directory holds it."""
    from assay.setup import install

    tool = install.TOOLS[name]
    try:
        pin = install.pin_of(tool, table)
        directory = install.directory_of(tool, pin, tools_root, table)
    except install.InstallError:
        return []
    if not directory.is_dir():
        return []
    try:
        install.verify_installed(tool, directory, pin, table)
    except install.InstallError as problem:
        return [row_error(pin, "tool", f"the installed {name} of this host does not meet this row: {problem}  Run "
                                       f"`python3 utils/bin/setup install {name}`")]
    return []


def compiler_findings(pin: pin_table.Pin, build_dir: Path) -> list[check_report.Finding]:
    """Check that the C++ compiler of the CMake cache of a build gives the pinned version with -dumpfullversion."""
    try:
        entry = cmake_reader.cache(build_dir).get("CMAKE_CXX_COMPILER")
    except cmake_reader.CmakeError as problem:
        return [row_error(pin, "compiler", f"the CMake cache of {build_dir} cannot be read: {problem}")]
    if entry is None or not entry.value.strip():
        return []
    compiler = entry.value.strip()
    try:
        done: proc.Completed | None = proc.run(proc.Command([compiler, "-dumpfullversion"],
                                                            timeout_s=VERSION_TIMEOUT_SECONDS))
    except proc.ToolFailed:
        done = None
    version = done.out.strip() if done is not None and done.exit == 0 else ""
    if version == pin.version:
        return []
    return [row_error(pin, "compiler", f"the C++ compiler of {build_dir} ({compiler}) gives "
                                       f"{version or 'no version'} with -dumpfullversion, and this row pins gcc "
                                       f"{pin.version}.  Build the patched compiler with `python3 utils/bin/setup gcc "
                                       f"PREFIX`, and configure the build again")]


def host_findings(pin: pin_table.Pin) -> list[check_report.Finding]:
    """Check that the interpreter of the check has the pinned version of Python or a version after it."""
    parts = pin.version.split(".")
    if not all(part.isdigit() for part in parts):
        return [row_error(pin, "host", f"the version {pin.version} of the pin {pin.name} is not numbers with dots")]
    wanted = tuple(int(part) for part in parts)
    if tuple(sys.version_info[:len(wanted)]) >= wanted:
        return []
    shown = ".".join(str(part) for part in sys.version_info[:3])
    return [row_error(pin, "host", f"the interpreter {sys.executable} is Python {shown}, and this row pins Python "
                                   f"{pin.version} or a version after it")]


def oracle_toolchain_findings(pin: pin_table.Pin, oracle_root: Path) -> list[check_report.Finding]:
    """Check that the directory of the toolchain of the oracle holds no install of the pin that stopped."""
    partial = oracle_root / f"{pin.name}-{pin.version}.partial"
    if not partial.exists():
        return []
    return [row_error(pin, "oracle-toolchain", f"{partial} is left from an install that stopped, and "
                                               f"`setup install oracle` refuses it.  Remove it and run the install "
                                               f"again")]


def oracle_source_findings(pin: pin_table.Pin) -> list[check_report.Finding]:
    """Check that a pin of an oracle source is a git source on GitHub or an archive (assay.oracle.sources)."""
    from assay.oracle import sources
    from assay.oracle.rocq import OracleError

    try:
        sources.of_pin(pin)
    except OracleError as problem:
        return [row_error(pin, "oracle-source", str(problem))]
    return []


def cross_package_findings(pin: pin_table.Pin) -> list[check_report.Finding]:
    """Check that a pin of a package of a cross target is an archive with a source and a SHA-256 (assay.setup.gcc)."""
    if pin.via == pin_table.Via.ARCHIVE and pin.source and pin.digest:
        return []
    return [row_error(pin, "cross-package", f"the pin {pin.name} is a package of a cross target, and `setup gcc "
                                            f"--target` unpacks only an archive with a source and a sha256 field")]


def check_table(request: TableCheck) -> list[check_report.Finding]:
    """Hold each row of the pin table to its holder (THE ROWS OF THE TABLE), and give each error.

    Complexity: linear in the rows, and one check of each holder for this host.

    Args:
        request: The tree, the build and the directories of the check
    """
    from assay.core import cache
    from assay.setup import oracle

    try:
        table = pin_table.load(request.root / pin_table.PINS_PATH)
    except (rows_grammar.RowsError, OSError) as problem:
        return [check_report.Finding("error", pin_table.PINS_PATH, getattr(problem, "line", 0), CHECK,
                                     f"the pin table cannot be read: {problem}", "table", "")]
    host = pin_table.host_platform()
    tools_root = request.tools_root if request.tools_root is not None else cache.tools_root()
    oracle_root = request.oracle_root if request.oracle_root is not None else oracle.toolchain_root()
    findings: list[check_report.Finding] = []
    checked_tools: set[str] = set()
    for pin in sorted(table, key=lambda item: item.line):
        kind = holder(pin.name)
        if kind is None:
            findings.append(row_error(pin, "unheld", f"no check of the rule {CHECK} holds the pin {pin.name}.  Give "
                                                     f"its name a holder in holder() of utils/assay/setup/pins.py, or "
                                                     f"remove the row"))
            continue
        if pin_table.find(table, pin.name, host) != pin:
            continue
        tool = tool_of(pin.name)
        if tool is not None and tool not in checked_tools:
            checked_tools.add(tool)
            findings += tool_findings(tool, table, tools_root)
        if kind == Holder.COMPILER and request.build_dir is not None:
            findings += compiler_findings(pin, request.build_dir)
        elif kind == Holder.HOST:
            findings += host_findings(pin)
        elif kind == Holder.ORACLE_TOOLCHAIN:
            findings += oracle_toolchain_findings(pin, oracle_root)
        elif kind == Holder.ORACLE_SOURCE:
            findings += oracle_source_findings(pin)
        elif kind == Holder.CROSS_PACKAGE:
            findings += cross_package_findings(pin)
    if request.build_dir is not None:
        try:
            findings += build_findings(read_pin(table), request.build_dir, request.proc_root, request.pid)
        except PinError as problem:
            findings.append(check_report.Finding("error", pin_table.PINS_PATH, problem.line, CHECK, str(problem),
                                                 "build-program", PIN_NAME))
    return findings


def check(build_dir: Path | None, warnings_dir: Path | None, proc_root: Path = PROC, pid: int | None = None,
          root: Path = ROOT) -> int:
    """Hold each row of the pin table of a tree to its holder, with the build directory when one is given.

    Args:
        build_dir: The build directory of the run, or None
        warnings_dir: The warnings directory, or None
        proc_root: The process file system, or a copy of its shape in a self-test
        pid: The process of the check, with a ctest ancestor that the check reads, by default this process
        root: The tree with the pin table that the check reads

    Returns:
        1 when one finding or more is an error, else 0
    """
    findings = check_table(TableCheck(root, build_dir, proc_root=proc_root, pid=pid))
    if not findings:
        check_report.write_line(f"{CHECK}: each row of {pin_table.PINS_PATH} agrees with its holder"
                                + (f" and with the build {build_dir}." if build_dir is not None else "."))
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


# ── A copy of the shape of /proc ───────────────────────────────────────────


def write_process(proc_root: Path, pid: int, parent: int, name: str, exe: Path | None = None) -> None:
    """Write the stat, comm and exe entries of one process into a copy of the shape of /proc, for a plant or a test."""
    directory = proc_root / str(pid)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "stat").write_text(f"{pid} ({name} with ) and spaces) S {parent} {pid} 0 0\n", encoding="utf-8")
    (directory / "comm").write_text(f"{name}\n", encoding="utf-8")
    if exe is not None:
        (directory / "exe").symlink_to(exe)


# ── Main ───────────────────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check of the table, the check of the installed tools or a print mode.

    Args:
        argv: The words after `setup check`

    Returns:
        The exit status of the mode
    """
    from assay.core import cli
    from assay.setup import install

    parser = cli.parser("setup check", __doc__.split("\n", 1)[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--print-program", nargs=2, metavar=("BUILD_DIR", "PROGRAM"),
                      help="print the cmake or the ctest of a configured build directory")
    mode.add_argument("--build-dir", type=Path, help="also check the programs and the compiler of a build directory")
    parser.add_argument("tools", nargs="*", metavar="TOOL", help=f"one of {', '.join(install.TOOLS)}")
    check_report.add_arguments(parser)
    options = parser.parse_args(argv)
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
    if options.tools:
        if options.build_dir is not None:
            parser.error("give TOOL or --build-dir, not the two")
        return check_tools(options.tools)
    return check(options.build_dir.resolve() if options.build_dir is not None else None, options.warnings_dir)
