"""The test of the install of the pinned tools (assay.setup.install): the directories, the checks and the install.

Each check of a property also runs against a stand-in for the behavior that
the property forbids, and the check must reject that stand-in.  An install of
a planted tool runs with no network: a planted pin table names the SHA-256 of
a planted zip, and the fetch copies the zip.

`guard selftest tests.setup.test_install` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import os
import shutil
import stat
import sys
import tempfile
import time
import zipfile
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path, PurePosixPath

from assay.core import cache
from assay.core import pins as pin_table
from assay.setup import install

KNOWN_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
DAY = 86400.0
type Expect = Callable[[str, bool], None]


def refused(action: Callable[[], object], status: int | None = None) -> bool:
    """Whether an action raises an InstallError, with the status when one is given."""
    try:
        action()
    except install.InstallError as error:
        return status is None or error.status == status
    return False


@contextmanager
def environment(**values: str | None) -> Iterator[None]:
    """Set (a string) or remove (None) environment variables for the block."""
    saved = {name: os.environ.get(name) for name in values}
    try:
        for name, value in values.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        yield
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def set_age(path: Path, days: float) -> None:
    """Make a file when it is absent, and set its modification time to a number of days ago."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
    moment = time.time() - days * DAY
    os.utime(path, (moment, moment))


# ── The directories ────────────────────────────────────────────────


def replacing_publish(staging: Path, final: Path) -> bool:
    """A stand-in for a publish that replaces the directory at the final path."""
    shutil.rmtree(final, ignore_errors=True)
    staging.rename(final)
    return True


def publish_keeps_tool(publish: Callable[[Path, Path], bool], root: Path) -> bool:
    """Publish onto a path that holds a tool.  True when the tool stays, and the publish reports the refusal."""
    (root / "staged").mkdir(parents=True)
    (root / "taken").mkdir()
    (root / "staged" / "file").write_text("new\n", encoding="utf-8")
    (root / "taken" / "file").write_text("old\n", encoding="utf-8")
    return not publish(root / "staged", root / "taken") and (root / "taken" / "file").read_text() == "old\n"


def prune_every_other(root: Path, prefix: str, keep: Path, now: float | None = None) -> list[Path]:
    """A stand-in for a prune that removes each other directory of the tool."""
    removed = [child for child in root.iterdir() if child.name.startswith(prefix) and child != keep]
    for child in removed:
        shutil.rmtree(child)
    return removed


def prune_keeps_recent(prune: Callable[..., list[Path]], root: Path) -> bool:
    """True when a prune removes the tool of 15 days and the staging directory of 2 days, and keeps the tool of 13
    days, the pinned tool, a tool of another prefix and the staging directory of today."""
    for name in ("kit-old", "kit-recent", "kit-pinned", "other-old", ".staging.old", ".staging.new"):
        (root / name).mkdir(parents=True)
    set_age(root / "kit-old" / install.LAST_USE, 15)
    set_age(root / "kit-recent" / install.LAST_USE, 13)
    set_age(root / "kit-pinned" / install.LAST_USE, 30)
    set_age(root / "other-old" / install.LAST_USE, 30)
    set_age(root / ".staging.old", 2)
    prune(root, "kit-", root / "kit-pinned")
    return (not (root / "kit-old").exists() and (root / "kit-recent").is_dir() and (root / "kit-pinned").is_dir()
            and (root / "other-old").is_dir() and not (root / ".staging.old").exists()
            and (root / ".staging.new").is_dir())


def directory_cases(expect: Expect, work: Path) -> None:
    """Check the tools directory, the publish, the prune and the record of a use."""
    with environment(XDG_CACHE_HOME=str(work / "xdg"), HOME=str(work / "home"), CRUCIBLE_CACHE_DIR=None):
        expect("the tools directory follows XDG_CACHE_HOME", cache.tools_root() == work / "xdg" / "crucible" / "tools")
    with environment(XDG_CACHE_HOME=None, HOME=str(work / "home"), CRUCIBLE_CACHE_DIR=None):
        expect("the tools directory falls back to the .cache directory of HOME",
               cache.tools_root() == work / "home" / ".cache" / "crucible" / "tools")
        expect("the check of the directory rejects another directory",
               cache.tools_root() != work / "elsewhere" / ".cache" / "crucible" / "tools")
    with environment(XDG_CACHE_HOME=None, HOME=str(work / "home"), CRUCIBLE_CACHE_DIR=str(work / "scratch")):
        expect("a scratch CRUCIBLE_CACHE_DIR does not move the tools directory",
               cache.tools_root() == work / "home" / ".cache" / "crucible" / "tools")
    with environment(XDG_CACHE_HOME=None, HOME=str(work / "home"), CRUCIBLE_CACHE_DIR="off"):
        expect("CRUCIBLE_CACHE_DIR=off does not hide the tools directory",
               cache.tools_root() == work / "home" / ".cache" / "crucible" / "tools")
    expect("the digest tag is the first 16 digits", install.digest_tag("0123456789abcdef0123") == "0123456789abcdef")
    free = work / "free"
    (free / "staged").mkdir(parents=True)
    (free / "staged" / "file").write_text("new\n", encoding="utf-8")
    expect("a publish moves a staged tool to a free path",
           install.publish(free / "staged", free / "final") and (free / "final" / "file").read_text() == "new\n"
           and not (free / "staged").exists())
    expect("a publish keeps a tool that another install placed", publish_keeps_tool(install.publish, work / "taken"))
    expect("the check of the publish rejects a publish that replaces the tool",
           not publish_keeps_tool(replacing_publish, work / "replaced"))
    expect("a prune removes only what no run used", prune_keeps_recent(install.prune, work / "prune"))
    expect("the check of the prune rejects a prune of each other tool",
           not prune_keeps_recent(prune_every_other, work / "prune-all"))
    set_age(work / "mark" / "stale" / install.LAST_USE, 2 / 24)
    set_age(work / "mark" / "fresh" / install.LAST_USE, 0.5 / 24)
    fresh_before = (work / "mark" / "fresh" / install.LAST_USE).stat().st_mtime
    install.mark_used(work / "mark" / "stale")
    install.mark_used(work / "mark" / "fresh")
    expect("a use touches a stale marker and keeps a fresh one",
           time.time() - (work / "mark" / "stale" / install.LAST_USE).stat().st_mtime < 300
           and (work / "mark" / "fresh" / install.LAST_USE).stat().st_mtime == fresh_before)


# ── The checks of the tools ────────────────────────────────────────


def write_manifest(directory: Path, lineage: dict[str, str], grammar: str) -> Path:
    """Write a MANIFEST.txt with the lineage, and the grammar commit in place of the pinned one."""
    directory.mkdir(parents=True, exist_ok=True)
    fields = {"kit": "synthetic", **lineage, "grammar_commit": grammar}
    (directory / "MANIFEST.txt").write_text("".join(f"{key:<19}{value}\n" for key, value in fields.items()),
                                            encoding="utf-8")
    return directory


def write_stand_in(directory: Path, version: str, stamp: str) -> Path:
    """Write a stand-in cmake install whose cmake and ctest give VERSION, with a stamp, or none when it is empty."""
    (directory / "bin").mkdir(parents=True)
    for name in ("cmake", "ctest"):
        program = directory / "bin" / name
        program.write_text(f'#!/bin/sh\necho "{name} version {version}"\necho\n'
                           f'echo "CMake suite maintained and supported by Kitware."\n', encoding="utf-8")
        program.chmod(0o755)
    if stamp:
        (directory / install.STAMP).write_text(f"{stamp}\n", encoding="utf-8")
    return directory


def check_cases(expect: Expect, work: Path) -> None:
    """Check the SHA-256, the lineage, the fork parse, the version checks, the platforms and the stamps."""
    table = pin_table.load()
    (work / "abc").write_bytes(b"abc")
    expect("sha256_of computes the known digest of 'abc'", install.sha256_of(work / "abc") == KNOWN_ABC)
    expect("the comparison of a digest rejects a wrong digest", install.sha256_of(work / "abc") != "deadbeef")
    lineage = install.kit_lineage(table)
    expect("verify_lineage accepts the pinned lineage",
           not refused(lambda: install.verify_lineage(write_manifest(work / "good", lineage,
                                                                     lineage["grammar_commit"]), lineage)))
    expect("verify_lineage rejects a wrong grammar commit",
           refused(lambda: install.verify_lineage(write_manifest(work / "wrong", lineage, "0" * 40), lineage), 2))
    (work / "empty").mkdir()
    expect("verify_lineage rejects a kit with no MANIFEST", refused(lambda: install.verify_lineage(work / "empty",
                                                                                                 lineage), 2))
    expect("the parse check accepts the conditional in the argument list",
           not refused(lambda: install.check_fork_parse("(argument_list (preproc_ifdef (preproc_else)))")))
    expect("the parse check rejects an ERROR node",
           refused(lambda: install.check_fork_parse("(argument_list (preproc_ifdef) (ERROR))"), 2))
    expect("the parse check rejects an output with no conditional",
           refused(lambda: install.check_fork_parse("(argument_list (number_literal))"), 2))
    ast_grep = install.TOOLS["ast-grep"]
    ast_grep_pin = install.pin_of(ast_grep, table, "linux-x86_64")
    expect("the version check of ast-grep accepts the pinned version",
           not refused(lambda: install.check_named_version("ast-grep", f"ast-grep {ast_grep_pin.version}",
                                                           ast_grep_pin.version)))
    expect("the version check of ast-grep rejects another version",
           refused(lambda: install.check_named_version("ast-grep", "ast-grep 0.1.0", ast_grep_pin.version), 2))
    expect("the version check rejects the line of a different program",
           refused(lambda: install.check_named_version("ruff", f"ast-grep {ast_grep_pin.version}",
                                                       ast_grep_pin.version), 2))
    expect("the fixture check accepts the one real command", not refused(lambda: install.check_fixture_lines("0")))
    for lines, what in (("0,2", "a match in the heredoc body"), ("0,4", "a match in the comment"), ("", "no match")):
        expect(f"the fixture check rejects {what}", refused(lambda: install.check_fixture_lines(lines), 2))
    cmake = install.TOOLS["cmake"]
    cmake_pin = install.pin_of(cmake, table, "linux-x86_64")
    version = cmake_pin.version
    expect("the version check of cmake accepts the pinned version",
           not refused(lambda: install.check_cmake_version("cmake", f"cmake version {version}", version)))
    expect("the version check of cmake rejects another version",
           refused(lambda: install.check_cmake_version("ctest", "ctest version 4.3.0", version), 2))
    expect("the version check of cmake rejects a version with a suffix",
           refused(lambda: install.check_cmake_version("cmake", f"cmake version {version}-rc1", version), 2))
    expect("the version check of cmake rejects the line of the other program",
           refused(lambda: install.check_cmake_version("ctest", f"cmake version {version}", version), 2))
    expect("each tool resolves linux-x86_64, and ast-grep and the kit resolve darwin-aarch64",
           all(install.pin_of(install.TOOLS[name], table, "linux-x86_64").platform == "linux-x86_64"
               for name in install.TOOLS)
           and install.pin_of(install.TOOLS["tree-sitter"], table, "darwin-aarch64").platform == "darwin-aarch64")
    expect("a platform with no pin is rejected with the status 4",
           refused(lambda: install.pin_of(install.TOOLS["tree-sitter"], table, "windows-i686"), 4)
           and refused(lambda: install.pin_of(cmake, table, "darwin-aarch64"), 4))
    expect("the wheel of x86_64 is the manylinux2014 wheel", PurePosixPath(cmake_pin.source).name
           == f"cmake-{version}-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl")
    root = work / "tools"
    for name, slug in (("tree-sitter", "linux-x64"), ("ast-grep", "x86_64-unknown-linux-gnu"), ("cmake", "x86_64")):
        tool = install.TOOLS[name]
        pin = install.pin_of(tool, table, "linux-x86_64")
        directory = install.directory_of(tool, pin, root, table)
        expect(f"the directory of {name} is in the tools directory, and its name holds the version, the platform "
               f"and the digest tag",
               directory.parent == root and directory.name == f"{tool.prefix}{pin.version}-{slug}-{pin.digest[:16]}")
    stand = work / "stand"
    good = write_stand_in(stand / "pinned", version, f"{version} {cmake_pin.digest}")
    expect("verify_installed accepts an install at the pin",
           not refused(lambda: install.verify_installed(cmake, good, cmake_pin, table)))
    cases = {
        "programs of another version": write_stand_in(stand / "other", "4.3.0", f"{version} {cmake_pin.digest}"),
        "an install with no stamp": write_stand_in(stand / "unstamped", version, ""),
        "the stamp of another pin": write_stand_in(stand / "stale", version, f"4.3.0 {cmake_pin.digest}"),
    }
    for what, directory in cases.items():
        expect(f"verify_installed rejects {what}",
               refused(lambda: install.verify_installed(cmake, directory, cmake_pin, table), 2))
    expect("verify_installed rejects an absent install with the status 3",
           refused(lambda: install.verify_installed(cmake, stand / "absent", cmake_pin, table), 3))
    (good / "bin" / "ctest").unlink()
    expect("verify_installed rejects an install with no ctest",
           refused(lambda: install.verify_installed(cmake, good, cmake_pin, table), 2))


# ── An install with no network ─────────────────────────────────────


def planted_zip(path: Path, version: str, lines: str) -> Path:
    """Write a zip of a stand-in ast-grep that gives VERSION and matches the fixture on LINES."""
    matches = ",".join(f'{{"range":{{"start":{{"line":{line}}}}}}}' for line in lines.split(",") if line)
    script = (f'#!/bin/sh\nif [ "$1" = --version ]; then echo "ast-grep {version}"; exit 0; fi\n'
              f"echo '[{matches}]'\n")
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("ast-grep", script)
    return path


def planted_table(source: Path, digest: str) -> install.Pins:
    """Return a pin table with one ast-grep of this host, with the source and the digest."""
    return pin_table.parse(f"# a planted pin table\nformat 1\npin ast-grep via=archive version=9.9.9 "
                           f"platform={pin_table.host_platform()} source=file://{source} sha256={digest} | a stand-in\n",
                           "planted")


def copy_fetch(url: str, target: Path) -> None:
    """A fetch with no network: copy the file of a file URL."""
    shutil.copyfile(url.removeprefix("file://"), target)


def leftovers(root: Path) -> list[str]:
    """Return the staging and retired directories that an install left in the tools directory."""
    return sorted(child.name for child in root.iterdir() if child.name.startswith((install.STAGING_PREFIX,
                                                                                  install.RETIRED_PREFIX)))


def install_cases(expect: Expect, work: Path) -> None:
    """Install a planted tool, and refuse a download with another digest, and a tool that fails its check."""
    if pin_table.host_platform() not in install.TOOLS["ast-grep"].slugs:
        expect("the install cases need a host platform of ast-grep", True)
        return
    good = planted_zip(work / "good.zip", "9.9.9", "0")
    table = planted_table(good, install.sha256_of(good))
    root = work / "install"
    directory = install.install("ast-grep", root, copy_fetch, table)
    expect("an install of a planted tool unpacks, checks and stamps it",
           (directory / "ast-grep").is_file() and (directory / install.STAMP).read_text().startswith("9.9.9 ")
           and leftovers(root) == [])
    expect("the installed tool is found, and a second install keeps it",
           install.installed("ast-grep", root, table) == directory
           and install.install("ast-grep", root, copy_fetch, table) == directory)
    other_root = work / "wrong-digest"
    wrong = planted_table(good, "0" * 64)
    expect("an install refuses a download whose SHA-256 is not the digest of its pin, with the status 2",
           refused(lambda: install.install("ast-grep", other_root, copy_fetch, wrong), 2))
    expect("the refused download installs nothing and leaves no staging directory",
           [child.name for child in other_root.iterdir()] == [])
    bad = planted_zip(work / "bad.zip", "9.9.9", "0,2")
    bad_root = work / "bad-fixture"
    expect("an install refuses a tool that reads the heredoc body as a command, with the status 2",
           refused(lambda: install.install("ast-grep", bad_root, copy_fetch, planted_table(bad, install.sha256_of(bad))),
                   2) and [child.name for child in bad_root.iterdir()] == [])
    expect("a tool that is absent is not installed, with the status 3",
           refused(lambda: install.installed("ast-grep", work / "nothing", table), 3))


# ── A tool of via=pip with no network ──────────────────────────────


def planted_wheel(directory: Path, version: str) -> Path:
    """Write the wheel of a stand-in ruff with a program that gives VERSION, and return its path."""
    name = f"ruff-{version}"
    files = {
        f"{name}.dist-info/METADATA": f"Metadata-Version: 2.1\nName: ruff\nVersion: {version}\n",
        f"{name}.dist-info/WHEEL": "Wheel-Version: 1.0\nGenerator: planted\nRoot-Is-Purelib: true\nTag: py3-none-any\n",
        f"{name}.data/scripts/ruff": f'#!/bin/sh\necho "ruff {version}"\n',
    }
    record = "".join(f"{path},,\n" for path in (*files, f"{name}.dist-info/RECORD"))
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{name}-py3-none-any.whl"
    with zipfile.ZipFile(path, "w") as archive:
        for member, text in files.items():
            entry = zipfile.ZipInfo(member)
            # The type and the mode of the entry, as the wheel of ruff gives them.  pip gives a script its mode bit
            # only when the entry is a regular file with a mode bit.
            entry.external_attr = (stat.S_IFREG | (0o755 if "/scripts/" in member else 0o644)) << 16
            archive.writestr(entry, text)
        archive.writestr(f"{name}.dist-info/RECORD", record)
    return path


def planted_pip_table(source: Path, digest: str, version: str = "9.9.9") -> install.Pins:
    """Return a pin table with one ruff of via=pip for this host, with the source and the digest."""
    return pin_table.parse(f"# a planted pin table\nformat 1\npin ruff via=pip version={version} "
                           f"platform={pin_table.host_platform()} source=file://{source} sha256={digest} "
                           f"| a stand-in\n", "planted")


def pip_cases(expect: Expect, work: Path) -> None:
    """Install a planted wheel with pip, and refuse an incorrect digest and a tool of a different version."""
    table = pin_table.load()
    mypy_pin = install.pin_of(install.TOOLS["mypy"], table, "linux-x86_64")
    pins = install.required_pins("mypy", install.MYPY_REQUIRES, mypy_pin, table)
    text = install.requirements(pins)
    expect("the requirements file of mypy names each pin with its URL and its SHA-256",
           [pin.name for pin in pins] == ["mypy", *install.MYPY_REQUIRES]
           and all(f"{pin.name} @ {pin.source} --hash=sha256:{pin.digest}\n" in text for pin in pins))
    directory = install.directory_of(install.TOOLS["mypy"], mypy_pin, work, table)
    expect("the directory of mypy has the digest tag of all its pins, and not of the pin of mypy only",
           directory.name.startswith(f"mypy-{mypy_pin.version}-x86_64-")
           and not directory.name.endswith(mypy_pin.digest[:16]))
    expect("a tool that requires a pin that the table does not hold is refused with the status 4",
           refused(lambda: install.required_pins("mypy", ("absent-package",), mypy_pin, table), 4))
    expect("the version check of mypy accepts the compiled pinned version, and rejects a mypy of pure Python",
           not refused(lambda: install.check_mypy_version("mypy 2.4.0 (compiled: yes)", "2.4.0"))
           and refused(lambda: install.check_mypy_version("mypy 2.4.0 (compiled: no)", "2.4.0"), 2))
    if pin_table.host_platform() not in install.TOOLS["ruff"].slugs:
        expect("the install cases of pip need a host platform of ruff", True)
        return
    good = planted_wheel(work / "wheels" / "good", "9.9.9")
    root = work / "install"
    installed = install.install("ruff", root, copy_fetch, planted_pip_table(good, install.sha256_of(good)))
    expect("pip installs a planted wheel with its SHA-256 into the site directory, and the install stamps it",
           (installed / install.SITE / "bin" / "ruff").is_file()
           and (installed / install.STAMP).read_text() == f"ruff 9.9.9 {install.sha256_of(good)}\n"
           and leftovers(root) == [])
    wrong_root = work / "wrong-digest"
    expect("pip refuses a wheel whose SHA-256 is not the digest of its pin, with the status 2, and installs nothing",
           refused(lambda: install.install("ruff", wrong_root, copy_fetch, planted_pip_table(good, "0" * 64)), 2)
           and sorted(child.name for child in wrong_root.iterdir()) == [])
    other = planted_wheel(work / "wheels" / "other", "9.9.8")
    other_root = work / "other-version"
    expect("an install refuses a ruff that gives a version that is not the pin, with the status 2, and adds nothing",
           refused(lambda: install.install("ruff", other_root, copy_fetch,
                                           planted_pip_table(other, install.sha256_of(other), "9.9.9")), 2)
           and sorted(child.name for child in other_root.iterdir()) == [])


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.setup.test_install", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="test-install-") as scratch:
        work = Path(scratch)
        for group, cases in (("directories", directory_cases), ("checks", check_cases), ("installs", install_cases),
                             ("pip", pip_cases)):
            (work / group).mkdir()
            cases(expect, work / group)
    if failures:
        print(f"tests.setup.test_install: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.setup.test_install: every case holds.")
    return 0
