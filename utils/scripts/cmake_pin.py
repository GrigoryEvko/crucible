#!/usr/bin/env python3
"""cmake_pin — the pinned version of CMake and ctest, the programs of a build directory, and the check of both.

utils/toolchain/cmake/requirements.txt pins one version of CMake and ctest.
The configure step rejects a CMake of another version (cmake/CMakePin.cmake).
This module reads the same pin for the scripts, and it is the check that
rejects a ctest of another version.

THE PIN
    Each requirement of the pin file gives the version and the SHA256 of the
    manylinux wheel of CMake on PyPI for one machine:

        cmake==X.Y.Z ; platform_machine == "MACHINE" \\
            --hash=sha256:DIGEST

    A line that starts with '#' is a comment.  Each other line is an error,
    and the requirements must give one version.  read_pin() reads the file.
    utils/scripts/install-cmake.sh reads it with --print-pin.

THE PROGRAMS OF A BUILD DIRECTORY
    CMakeCache.txt of a configured build directory holds CMAKE_COMMAND and
    CMAKE_CTEST_COMMAND: the cmake that configured the directory, and the
    ctest of the same install.  A script that operates on a build directory
    runs these two programs (configured_program), and it does not look in
    PATH.  PATH can hold a ctest of another version, and ctest does not
    reject a build directory that another version configured.  A script that
    configures a new build directory runs the cmake of PATH, and the
    configure step rejects that cmake when its version is not the pin.  A
    script that runs cmake or ctest with no build directory, for example a
    self-test outside ctest, takes the program of PATH only when that program
    gives the pinned version (pinned_program).

THE CHECK (--check, the test cmake_pin)
    Each of these programs must give the pinned version:
    * The ctest that runs the check: the nearest ancestor process whose name
      in /proc/PID/comm is ctest.  The check runs /proc/PID/exe --version, so
      it reads the program of that process, also when a wrapper started it.
    * CMAKE_COMMAND and CMAKE_CTEST_COMMAND of the build directory.
    A program of another version is an error.  A run with no ctest ancestor,
    a program whose version the check cannot read and a pin file that the
    check cannot read are errors too.  Each finding has the place of the
    first requirement of the pin file.

Usage
    cmake_pin.py --check --build-dir BUILD_DIR [--warnings-dir DIR]
    cmake_pin.py --print-program BUILD_DIR cmake|ctest
    cmake_pin.py --print-pin [MACHINE]
    cmake_pin.py --self-test [--cmake CMAKE --ctest CTEST --make-program NINJA]

Exit 0 with no error, 1 on an error, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import functools
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402

CHECK = "cmake-pin"
SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parents[1]
PIN_RELATIVE = "utils/toolchain/cmake/requirements.txt"
PIN_FILE = ROOT / PIN_RELATIVE
PROC = Path("/proc")
# The cache entry of each program of a build directory.
CACHE_ENTRIES = {"cmake": "CMAKE_COMMAND", "ctest": "CMAKE_CTEST_COMMAND"}
REQUIREMENT = re.compile(
    r'cmake==(?P<version>[0-9]+\.[0-9]+\.[0-9]+) *; *platform_machine *== *"(?P<machine>[A-Za-z0-9_]+)"'
    r" +--hash=sha256:(?P<digest>[0-9a-f]{64}) *")
VERSION_LINE = re.compile(r"(?P<name>cmake|ctest) version (?P<version>\S+)")
# A program that gives no version in this time is broken.
VERSION_TIMEOUT_SECONDS = 60
# The longest chain of ancestors that the check walks.  A chain from a test to
# its ctest has two or three links.
ANCESTOR_LIMIT = 64


class PinError(Exception):
    """The pin file, a build directory or a program does not give what the caller needs."""

    def __init__(self, message: str, line: int = 0) -> None:
        """Keep the message and the line of the pin file that it is about, or 0."""
        super().__init__(message)
        self.line = line


@dataclass(frozen=True)
class Pin:
    """The pinned version, the SHA256 of the wheel of each machine, and the line of the first requirement."""

    version: str
    digests: dict[str, str]
    line: int


# ── The pin ────────────────────────────────────────────────────────────────


def logical_lines(text: str) -> list[tuple[int, str]]:
    """Return each line that is not a comment or blank, with a line that ends in a backslash joined to the next.

    Args:
        text: The text of a pin file

    Returns:
        The number of the first physical line and the joined text of each line
    """
    joined: list[tuple[int, str]] = []
    pending: list[str] = []
    first = 0
    for number, raw in enumerate(text.splitlines(), start=1):
        stripped = raw.strip()
        if not pending and (not stripped or stripped.startswith("#")):
            continue
        if not pending:
            first = number
        if stripped.endswith("\\"):
            pending.append(stripped[:-1].strip())
            continue
        pending.append(stripped)
        joined.append((first, " ".join(pending)))
        pending = []
    if pending:
        joined.append((first, " ".join(pending)))
    return joined


def read_pin(path: Path = PIN_FILE) -> Pin:
    """Read the pin file.

    Complexity: O(n) in the size of the file.

    Args:
        path: The pin file

    Returns:
        The pin

    Raises:
        PinError: If the file is absent, holds a line that is not a requirement, holds no
            requirement, gives two versions or gives two digests for one machine
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise PinError(f"the pin file {path} cannot be read: {error.strerror}") from None
    version = ""
    first_line = 0
    digests: dict[str, str] = {}
    for number, line in logical_lines(text):
        found = REQUIREMENT.fullmatch(line)
        if found is None:
            raise PinError(f"the line {number} of {path} is not a requirement "
                           f"'cmake==X.Y.Z ; platform_machine == \"MACHINE\" --hash=sha256:DIGEST': {line}", number)
        if version and found["version"] != version:
            raise PinError(f"{path} gives the two versions {version} and {found['version']}.  Give one version on "
                           f"each line.", number)
        if found["machine"] in digests:
            raise PinError(f"{path} gives two requirements for the machine {found['machine']}.", number)
        version = version or found["version"]
        first_line = first_line or number
        digests[found["machine"]] = found["digest"]
    if not version:
        raise PinError(f"{path} holds no requirement 'cmake==X.Y.Z'.")
    return Pin(version=version, digests=digests, line=first_line)


def install_hint() -> str:
    """Return the sentence that tells how to install the pinned CMake and ctest."""
    return (f"Install the pinned CMake and ctest with `bash {ROOT}/utils/scripts/install-cmake.sh`, and put the bin "
            f"directory that it prints first in PATH.  A Python environment can take them with "
            f"`python3 -m pip install --require-hashes -r {PIN_FILE}`.")


# ── Programs ───────────────────────────────────────────────────────────────


def program_version(program: str | Path) -> tuple[str, str] | None:
    """Return the name and the version that `PROGRAM --version` gives on its first line, or None.

    Args:
        program: The path of a cmake or a ctest

    Returns:
        ("cmake" or "ctest", version), or None when the program does not run or gives no such line
    """
    try:
        result = subprocess.run([str(program), "--version"], capture_output=True, text=True, errors="replace",
                                timeout=VERSION_TIMEOUT_SECONDS, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    lines = result.stdout.splitlines()
    found = VERSION_LINE.fullmatch(lines[0].strip()) if result.returncode == 0 and lines else None
    return (found["name"], found["version"]) if found else None


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
        raise PinError(f"the {name} of PATH, {program}, gives {shown}, but {PIN_RELATIVE} pins {pin.version}.  "
                       f"{install_hint()}")
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


# ── The check ──────────────────────────────────────────────────────────────


def judge_program(pin: Pin, name: str, program: str | Path, what: str, remedy: str) -> check_report.Finding | None:
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
    return check_report.Finding("error", PIN_RELATIVE, pin.line, CHECK,
                                f"{what} gives {shown}, but this file pins {name} {pin.version}.  {remedy}")


def link_target(link: Path) -> str:
    """Return the path that a link names, or the link itself when it cannot be read."""
    try:
        return os.readlink(link)
    except OSError:
        return str(link)


def check(build_dir: Path, warnings_dir: Path | None, proc_root: Path = PROC, pid: int | None = None) -> int:
    """Check the ctest that runs this process and the two programs of the build directory against the pin.

    Args:
        build_dir: The build directory of the run
        warnings_dir: The warnings directory, or None
        proc_root: The process file system, or a copy of its shape in a self-test
        pid: The process whose ctest ancestor the check reads, by default this process

    Returns:
        1 when one finding or more is an error, else 0
    """
    try:
        pin = read_pin()
    except PinError as error:
        return check_report.emit([check_report.Finding("error", PIN_RELATIVE, error.line, CHECK, str(error))],
                                 CHECK, warnings_dir)
    findings: list[check_report.Finding] = []
    try:
        configured_ctest = configured_program(build_dir, "ctest")
    except PinError:
        configured_ctest = "the ctest of the build directory"
    runner = ancestor_ctest(proc_root, pid)
    if runner is None:
        findings.append(check_report.Finding(
            "error", PIN_RELATIVE, pin.line, CHECK,
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
            findings.append(check_report.Finding("error", PIN_RELATIVE, pin.line, CHECK, str(error)))
            continue
        found = judge_program(pin, name, program, f"{entry} of {build_dir} ({program})",
                              f"Configure the build directory again with the pinned CMake.  {install_hint()}")
        findings.extend([found] if found else [])
    if not findings:
        print(f"cmake-pin: the ctest of the run and the cmake and the ctest of {build_dir} give {pin.version}, the "
              f"pinned version.")
    return check_report.emit(findings, CHECK, warnings_dir)


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

    def refuses(name: str, action: Callable[[], object], pattern: str) -> None:
        """Expect ACTION to raise a PinError whose message matches PATTERN."""
        try:
            action()
        except PinError as error:
            expect(name, re.search(pattern, str(error)) is not None, str(error))
            return
        expect(name, False, "no PinError")

    print("cmake_pin --self-test")
    tree = read_pin()
    expect("the pin of the tree gives one version X.Y.Z", re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", tree.version)
           is not None)
    expect("the pin of the tree gives a digest for x86_64 and for aarch64",
           set(tree.digests) == {"x86_64", "aarch64"} and all(len(digest) == 64 for digest in tree.digests.values()))
    expect("the first requirement of the tree is the place of a finding",
           PIN_FILE.read_text(encoding="utf-8").splitlines()[tree.line - 1].startswith("cmake=="))

    digest = "0" * 64
    with tempfile.TemporaryDirectory(prefix="cmake-pin-") as directory:
        work = Path(directory)

        def pin_file(name: str, *lines: str) -> Path:
            path = work / "pins" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("# a pin of the self-test\n" + "".join(f"{line}\n" for line in lines), encoding="utf-8")
            return path

        def requirement(version: str, machine: str) -> str:
            return f'cmake=={version} ; platform_machine == "{machine}" \\\n    --hash=sha256:{digest}'

        good = read_pin(pin_file("good", requirement("4.4.2", "x86_64"), requirement("4.4.2", "aarch64")))
        expect("a pin of two machines reads", good.version == "4.4.2" and good.line == 2
               and good.digests == {"x86_64": digest, "aarch64": digest})
        refuses("an absent pin file is an error", lambda: read_pin(work / "absent"), "cannot be read")
        refuses("a pin file with no requirement is an error", lambda: read_pin(pin_file("empty")), "no requirement")
        refuses("a pin of two versions is an error",
                lambda: read_pin(pin_file("two", requirement("4.4.2", "x86_64"), requirement("4.4.3", "aarch64"))),
                "two versions 4.4.2 and 4.4.3")
        refuses("a pin of one machine two times is an error",
                lambda: read_pin(pin_file("twice", requirement("4.4.2", "x86_64"), requirement("4.4.2", "x86_64"))),
                "two requirements for the machine x86_64")
        refuses("a requirement with no hash is an error",
                lambda: read_pin(pin_file("unhashed", 'cmake==4.4.2 ; platform_machine == "x86_64"')),
                "line 2 .* is not a requirement")
        refuses("a requirement with no machine is an error",
                lambda: read_pin(pin_file("unmarked", f"cmake==4.4.2 --hash=sha256:{digest}")), "is not a requirement")
        refuses("a requirement of another project is an error",
                lambda: read_pin(pin_file("other", requirement("4.4.2", "x86_64"),
                                          f'ninja==1.13.0 ; platform_machine == "x86_64" --hash=sha256:{digest}')),
                "line 4 .* is not a requirement")
        refuses("a version that is not X.Y.Z is an error",
                lambda: read_pin(pin_file("short", requirement("4.4", "x86_64"))), "is not a requirement")

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
                    "gives ctest 4.3.0, but .* pins")
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
               and f"{PIN_RELATIVE}:{tree.line}: error: [cmake-pin]" in said, said)
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
        print(f"cmake_pin --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 2
    print("cmake_pin --self-test: every case holds.")
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
        f'add_test(NAME probe COMMAND "{sys.executable}" "{Path(__file__).resolve()}" --check '
        f'--build-dir "{build_dir}")\n', encoding="utf-8")
    configured = subprocess.run([cmake, "-S", str(source), "-B", str(build_dir), "-G", "Ninja",
                                 f"-DCMAKE_MAKE_PROGRAM={make_program}"], capture_output=True, text=True, check=False)
    expect("the scratch project configures", configured.returncode == 0, configured.stdout + configured.stderr)
    if configured.returncode != 0:
        return
    run = subprocess.run([ctest, "--test-dir", str(build_dir), "--output-on-failure"], capture_output=True, text=True,
                         check=False)
    expect("the check passes under the ctest of the build", run.returncode == 0, run.stdout + run.stderr)


# ── Main ───────────────────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check, a print mode or the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="check the ctest of the run and the build directory")
    mode.add_argument("--print-program", nargs=2, metavar=("BUILD_DIR", "PROGRAM"),
                      help="print the cmake or the ctest of a configured build directory")
    mode.add_argument("--print-pin", nargs="?", const=os.uname().machine, metavar="MACHINE",
                      help="print the pinned version and the digest of the wheel of MACHINE (default: this host)")
    mode.add_argument("--self-test", action="store_true", help="plant each case and check each verdict")
    parser.add_argument("--build-dir", type=Path, help="the build directory of --check")
    parser.add_argument("--cmake", help="the cmake of the real run of --self-test")
    parser.add_argument("--ctest", help="the ctest of the real run of --self-test")
    parser.add_argument("--make-program", help="the ninja of the real run of --self-test")
    check_report.add_arguments(parser)
    options = parser.parse_args(argv)
    if options.self_test:
        return self_test(options.cmake, options.ctest, options.make_program)
    try:
        if options.print_program:
            build_dir, name = options.print_program
            if name not in CACHE_ENTRIES:
                parser.error(f"the program is cmake or ctest, not {name}")
            print(configured_program(Path(build_dir), name))
            return 0
        if options.print_pin:
            pin = read_pin()
            if options.print_pin not in pin.digests:
                print(f"cmake_pin: {PIN_RELATIVE} pins no wheel for the machine {options.print_pin}.  It pins "
                      f"{', '.join(sorted(pin.digests))}.", file=sys.stderr)
                return 1
            print(f"{pin.version} {pin.digests[options.print_pin]}")
            return 0
    except PinError as error:
        print(f"cmake_pin: {error}", file=sys.stderr)
        return 1
    if options.build_dir is None:
        parser.error("--check needs --build-dir")
    return check(options.build_dir.resolve(), options.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
