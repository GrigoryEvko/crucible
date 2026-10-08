"""setup install — install each pinned tool, and find the program of an installed tool.

usage:
    setup install [TOOL]...           install each TOOL, by default tree-sitter, ast-grep and cmake
    setup install --print-bin TOOL    print the program of an installed TOOL.  No network
    setup install --print-dir TOOL    print the install directory of TOOL, also when it is absent.  No network

THE TOOLS
    tree-sitter  The tree-sitter kit (the pins tree-sitter-kit,
                 tree-sitter-runtime and tree-sitter-grammar of utils/pins.txt):
                 the CLI of the runtime fork and the C++ grammar fork.  The
                 runtime fork declares the grammar ABI 1019, and no upstream
                 runtime loads such a grammar.  So the CLI of the kit is the
                 only program that can read the grammar, and each AST gate
                 calls it.  --print-bin prints bin/tree-sitter of the kit.
    ast-grep     ast-grep (the pin ast-grep), which reads each shell script
                 with its bash grammar.  --print-bin prints the binary.
    cmake        The cmake and the ctest of the pin cmake: the CMake prefix
                 of the manylinux wheel of CMake on PyPI.  A wheel is a zip
                 file, so the install needs no pip.  --print-bin prints the
                 bin directory.
    oracle       The toolchain of the crash-stop oracle of the session oracle
                 (the pins temurin-jdk, sbt and mcrl2), which
                 assay.setup.oracle installs into the cache of the oracle.
    mypy         The type checker of the rule types (the pin mypy and the
                 pins of its packages: ast-serialize, librt, mypy-extensions,
                 pathspec and typing-extensions).
                 --print-bin prints the site directory of the packages.  The
                 rule starts mypy with `python3 -I -S` and that directory
                 first in sys.path.
    ruff         The linter and the formatter of the rule python-lint (the pin
                 ruff).  --print-bin prints the binary.
    No tool comes from PATH, because a program of PATH is an input that no pin
    holds.

THE PIP TOOLS
    mypy and ruff are rows of via=pip.  The install writes the file
    `requirements.txt` of pip with the URL and the SHA-256 of each pin of the
    tool.  The file is in a temporary directory that the install removes
    after it.  Then this Python runs pip with `--require-hashes`, `--no-index`
    and `--only-binary :all:`.  pip installs each wheel into the site
    directory of the staging directory.  pip refuses a download with a
    different SHA-256, and a package that the file does not hold.  The tool
    goes into the tools directory, and not into the Python of the user.

FAIL-CLOSED PINNING
    The download must have the SHA-256 of its pin.  The install does not read
    a SHA256SUMS file of a release: a release that uploads an asset again
    uploads its SHA256SUMS again, so the file shows only that the two agree.
    After the unpack, the kit must give the pinned lineage in MANIFEST.txt
    and parse a fixture that only the fork parses, ast-grep must give its
    pinned version and read a fixture as the guards need, and cmake and
    ctest must give their pinned version.  mypy must give its pinned version
    and the word that its compiled wheel gives, and ruff must give its pinned
    version.  A failure at each step installs nothing.

THE DIRECTORIES
    Each tool lives in the tools directory of assay.core.cache (tools_root()),
    outside each checkout, so each checkout, each work tree and each export of
    a guard run uses one install.  The name of the directory of a tool holds
    its pinned version, its platform and the first 16 hexadecimal digits of
    its pinned SHA-256, so two pins never share a directory.  A tool with
    more pins, such as mypy, has the digits of the SHA-256 of the name and the
    digest of each of its pins.  An install
    unpacks into a staging directory under the tools directory, and it moves
    the verified directory into place with one rename.  A rename never
    replaces a directory that holds a tool, so a process that uses a tool
    never sees it change.  The file .crucible-pin of a directory is its
    stamp: the install writes it last, so a directory with no stamp is an
    install that did not complete.

THE BOUND
    A use of a tool touches .last-use in its directory when the last touch is
    more than one hour old.  An install removes each other directory of the
    same tool that no run used for UNUSED_DAYS days, so a checkout on an older
    pin keeps its tool.  It also removes a staging directory that a stopped
    install left for more than one day.

ON A CI RUNNER
    With GITHUB_PATH set, an install of the kit or of cmake appends its bin
    directory to that file, so each later step of the job finds the program.

Exit status: 0 on success; 2 when a download or an install does not meet its
pin, and then nothing is installed; 3 when a tool is absent and the mode does
not install it; 4 when the host has no pinned download, or a necessary program
is absent.

`guard selftest tests.setup.test_install` does a test of the module.
"""

import functools
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.parse
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.core import cache
from assay.core import pins as pin_table
from assay.core import proc

OK = 0
NOT_PINNED = 2
ABSENT = 3
UNSUPPORTED = 4
# The days of no use after which an install removes the directory of a tool.
UNUSED_DAYS = 14
# A use touches .last-use when the last touch is older than this.
USE_INTERVAL_S = 3600.0
# A staging or a retired directory with no change for this period is the remains of an install that stopped.
STAGING_MAX_AGE_S = 86400.0
# The time limit of one download, and the attempts of it.
DOWNLOAD_TIMEOUT_S = 300.0
DOWNLOAD_ATTEMPTS = 4
# The time limit of one run of an installed program.
PROGRAM_TIMEOUT_S = 120.0
STAMP = ".crucible-pin"
LAST_USE = ".last-use"
STAGING_PREFIX = ".staging."
RETIRED_PREFIX = ".retired."
DEFAULT_TOOLS = ("tree-sitter", "ast-grep", "cmake")
INSTALL_COMMAND = "python3 utils/bin/setup install"

# The fixture that only the fork of the kit parses: a conditional inside an argument list.  The upstream
# grammar reports an error node there, and the fork reports the conditional.
KIT_FIXTURE = "void g(int);\nvoid f() {\n    g(\n#ifdef A\n        1\n#else\n        2\n#endif\n    );\n}\n"
# The fixture of ast-grep: a git command, the same text in a heredoc body, and the same text in a comment.  The
# guards need exactly the command on line 0.
AST_GREP_FIXTURE = ('git -C "$tmp" commit -q -m x\ncat > note <<EOF\ngit commit -m fixture\nEOF\n'
                    '# git commit -m example\n')
AST_GREP_PATTERN = "git $$$ARGS"
# The options of pip for a tool of via=pip (THE PIP TOOLS): no configuration file and no environment of pip, no index,
# a hash for each package, wheels only, no cache, and no warning about the packages of the Python that runs pip.
PIP_OPTIONS = ("--isolated", "--no-index", "--require-hashes", "--only-binary", ":all:", "--no-cache-dir",
               "--disable-pip-version-check", "--no-input", "--no-warn-conflicts")
# The time limit of one run of pip.
PIP_TIMEOUT_S = proc.TOOL_S
# The error of a Python with no pip, in the output of `python3 -m pip`.
NO_PIP = "No module named pip"
# The directory of a tool of via=pip that holds its packages.
SITE = "site"
# The start of mypy from the site directory that is the first argument: `python3 -I -S -c MYPY_START SITE ARG...`.
MYPY_START = ("import sys; sys.path.insert(0, sys.argv.pop(1)); sys.argv[0] = 'mypy'; "
              "from mypy.__main__ import console_entry; console_entry()")

# The rows of a pin table.
type Pins = tuple[pin_table.Pin, ...]


class InstallError(Exception):
    """A tool does not meet its pin, is absent, or has no pin for the host.  `status` is the exit status."""

    def __init__(self, message: str, status: int) -> None:
        """Keep the message and the exit status."""
        super().__init__(message)
        self.status = status


# ── The directories (THE DIRECTORIES, THE BOUND) ───────────────────


def digest_tag(digest: str) -> str:
    """Return the part of a SHA-256 that the name of a tool directory holds."""
    return digest[:16]


def publish(staging: Path, final: Path) -> bool:
    """Move a verified staging directory to its final path with one rename, and return whether it moved.

    The rename fails when another install put a tool there first, and then the
    function changes nothing.
    """
    try:
        os.rename(staging, final)
    except OSError:
        return False
    return True


def retire(directory: Path, root: Path) -> bool:
    """Move a directory that fails its check out of its path, so an install can put a verified one there.

    Returns:
        False when the directory is gone already, because another install moved it
    """
    aside = Path(tempfile.mkdtemp(prefix=RETIRED_PREFIX, dir=root))
    try:
        os.rename(directory, aside)
    except OSError:
        aside.rmdir()
        return False
    shutil.rmtree(aside, ignore_errors=True)
    return True


def mark_used(directory: Path, now: float | None = None) -> None:
    """Touch .last-use of a tool directory when the last touch is more than one hour old.

    A directory that the user cannot write keeps no record, and the call still succeeds.
    """
    marker = directory / LAST_USE
    moment = time.time() if now is None else now
    try:
        if moment - marker.stat().st_mtime <= USE_INTERVAL_S:
            return
    except OSError:
        pass
    try:
        marker.touch()
        os.utime(marker, (moment, moment))
    except OSError:
        pass


def prune(root: Path, prefix: str, keep: Path, now: float | None = None) -> list[Path]:
    """Remove each directory of one tool, other than keep, that no run used for UNUSED_DAYS days.

    Then remove each staging and retired directory with no change for one day.
    Each removal stays inside the tools directory.

    Complexity: one stat of each directory of the tools directory.

    Returns:
        The removed directories
    """
    moment = time.time() if now is None else now
    removed: list[Path] = []
    try:
        children = sorted(root.iterdir())
    except OSError:
        return removed
    for child in children:
        if not child.is_dir() or child == keep:
            continue
        name = child.name
        if name.startswith(prefix):
            marker = child / LAST_USE
            stamp = marker if marker.exists() else child
            if moment - stamp.stat().st_mtime >= UNUSED_DAYS * 86400.0:
                shutil.rmtree(child, ignore_errors=True)
                removed.append(child)
                print(f"setup install: removed {child}, which no run used for {UNUSED_DAYS} days", file=sys.stderr)
        elif name.startswith((STAGING_PREFIX, RETIRED_PREFIX)):
            if moment - child.stat().st_mtime >= STAGING_MAX_AGE_S:
                shutil.rmtree(child, ignore_errors=True)
                removed.append(child)
    return removed


def sha256_of(path: Path) -> str:
    """Return the SHA-256 of a file in lowercase hexadecimal.  Complexity: linear in the size of the file."""
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, target: Path) -> None:
    """Download one file, with DOWNLOAD_ATTEMPTS attempts.

    Raises:
        InstallError: If each attempt fails
    """
    import urllib.request

    problem = ""
    for attempt in range(DOWNLOAD_ATTEMPTS):
        if attempt:
            time.sleep(min(2.0 ** attempt, 8.0))
        try:
            with urllib.request.urlopen(url, timeout=DOWNLOAD_TIMEOUT_S) as response, open(target, "wb") as handle:
                shutil.copyfileobj(response, handle, 1 << 20)
            return
        except OSError as error:
            problem = str(error)
    raise InstallError(f"the download of {url} fails: {problem}", UNSUPPORTED)


def run_program(argv: list[str], environment: dict[str, str] | None = None, with_errors: bool = False,
                timeout_s: float = PROGRAM_TIMEOUT_S) -> tuple[int, str]:
    """Run an installed program, and return its exit status and its output.

    With with_errors, the output holds the errors of the program too.  A
    program that cannot start or that runs past its time limit gives the
    status 127.
    """
    try:
        completed = proc.run(proc.Command(argv, timeout_s=timeout_s, env=environment,
                                          stderr=proc.Output.MERGE if with_errors else proc.Output.DISCARD))
    except proc.ToolFailed as error:
        return 127, str(error)
    return completed.exit, completed.out


def is_executable(path: Path) -> bool:
    """Whether a path is a file that the user can run."""
    return path.is_file() and os.access(path, os.X_OK)


def stamp_fields(path: Path) -> dict[str, str]:
    """Return the first word and the rest of each line of a stamp or a manifest, or nothing when it cannot be read."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return {}
    fields: dict[str, str] = {}
    for line in text.splitlines():
        parts = line.split(maxsplit=1)
        if parts and parts[0] not in fields:
            fields[parts[0]] = parts[1].strip() if len(parts) > 1 else ""
    return fields


def export_github_path(directory: Path, tool: str) -> None:
    """Append a bin directory to the file of GITHUB_PATH, when the variable is set."""
    target = os.environ.get("GITHUB_PATH", "")
    if target:
        with open(target, "a", encoding="utf-8") as handle:
            handle.write(f"{directory}\n")
        print(f"setup install {tool}: appended {directory} to GITHUB_PATH.", file=sys.stderr)


# ── The tools ──────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Tool:
    """One tool of the tools directory: its pin, the parts of its directory name and its checks.

    The name of a directory of the tool is PREFIX, the pinned version, the
    word of the platform in `slugs` and the digest tag, divided by hyphens.
    `verify` checks a directory that holds the unpacked tool, and `unpack`
    moves the download into a directory to publish.  `program` gives what
    --print-bin prints.  A tool with `exports_bin` gives its bin directory to
    GITHUB_PATH.  `requires` names the pins of the packages of a tool of
    via=pip, other than its own pin (THE PIP TOOLS).
    """

    name: str
    pin: str
    prefix: str
    slugs: dict[str, str]
    verify: Callable[[Path, pin_table.Pin, Pins], None]
    unpack: Callable[[Path, Path, pin_table.Pin], Path]
    stamp: Callable[[pin_table.Pin, str, Pins], str]
    stamp_matches: Callable[[Path, pin_table.Pin, str, Pins], str | None]
    program: Callable[[Path], Path]
    exports_bin: bool
    requires: tuple[str, ...] = ()

    def slug(self, pin: pin_table.Pin) -> str:
        """Return the word of the platform of a pin in the name of a directory."""
        return self.slugs[pin.platform]

    def directory_name(self, pin: pin_table.Pin, digest: str) -> str:
        """Return the name of the directory of the tool of a pin, with the SHA-256 of tool_digest()."""
        return f"{self.prefix}{pin.version}-{self.slug(pin)}-{digest_tag(digest)}"


def pin_of(tool: Tool, table: tuple[pin_table.Pin, ...], platform: str | None = None) -> pin_table.Pin:
    """Return the pin of a tool for a platform, by default for this host.

    Raises:
        InstallError: If the table has no pin of the tool for the platform
    """
    host = pin_table.host_platform() if platform is None else platform
    pin = pin_table.find(table, tool.pin, host)
    if pin is None or pin.platform not in tool.slugs:
        served = ", ".join(sorted(item.platform for item in pin_table.named(table, tool.pin)))
        raise InstallError(f"no pin {tool.pin} serves the platform {host}.  {pin_table.PINS_PATH} pins it for "
                           f"{served or 'no platform'}", UNSUPPORTED)
    return pin


def required_pins(name: str, requires: tuple[str, ...], pin: pin_table.Pin, table: Pins) -> tuple[pin_table.Pin, ...]:
    """Return the pin of a tool and the pin of each of its packages, for the platform of the pin.

    Raises:
        InstallError: If the table has no pin of a package of the tool for the platform
    """
    found = [pin]
    for package in requires:
        required = pin_table.find(table, package, pin.platform)
        if required is None:
            raise InstallError(f"no pin {package}, which {name} requires, serves the platform "
                               f"{pin.platform or 'of each host'} in {pin_table.PINS_PATH}", UNSUPPORTED)
        found.append(required)
    return tuple(found)


def tool_digest(tool: Tool, pin: pin_table.Pin, table: Pins) -> str:
    """Return the SHA-256 that names the directory of a tool (THE DIRECTORIES).

    It is the digest of the pin.  For a tool with more pins, it is the
    SHA-256 of the name and the digest of each of its pins.

    Raises:
        InstallError: If the table has no pin of a package of the tool
    """
    if not tool.requires:
        return pin.digest
    lines = "".join(f"{item.name} {item.digest}\n" for item in required_pins(tool.name, tool.requires, pin, table))
    return hashlib.sha256(lines.encode("utf-8")).hexdigest()


def directory_of(tool: Tool, pin: pin_table.Pin, root: Path, table: Pins) -> Path:
    """Return the directory of the tool of a pin in the tools directory.

    Raises:
        InstallError: If the table has no pin of a package of the tool
    """
    return root / tool.directory_name(pin, tool_digest(tool, pin, table))


# The tree-sitter kit.


def kit_lineage(table: tuple[pin_table.Pin, ...]) -> dict[str, str]:
    """Return the lineage that MANIFEST.txt of the kit must give, from the pins of the runtime and the grammar.

    Raises:
        InstallError: If the table has no pin of the runtime or of the grammar
    """
    runtime = pin_table.find(table, "tree-sitter-runtime", "")
    grammar = pin_table.find(table, "tree-sitter-grammar", "")
    if runtime is None or grammar is None:
        raise InstallError(f"{pin_table.PINS_PATH} has no pin tree-sitter-runtime or tree-sitter-grammar, which give "
                           f"the lineage of the kit", UNSUPPORTED)
    return {"grammar_commit": grammar.commit, "grammar_abi": grammar.version, "runtime_commit": runtime.commit,
            "runtime_tag": runtime.version}


def verify_lineage(directory: Path, lineage: dict[str, str]) -> None:
    """Make sure that MANIFEST.txt of a kit gives the pinned lineage.

    Raises:
        InstallError: If the manifest is absent, or a field differs from the pin
    """
    manifest = directory / "MANIFEST.txt"
    if not manifest.is_file():
        raise InstallError(f"{directory} has no readable MANIFEST.txt.", NOT_PINNED)
    fields = stamp_fields(manifest)
    wrong = [f"{key} is {fields.get(key) or '<absent>'}, the pin says {value}" for key, value in lineage.items()
             if fields.get(key) != value]
    if wrong:
        raise InstallError(f"the lineage of {directory} does not agree with the pin: {'; '.join(wrong)}.",
                           NOT_PINNED)


def check_fork_parse(output: str) -> None:
    """Make sure that the output of the parse of KIT_FIXTURE is the result of the fork.

    A version string cannot tell the fork from upstream, and this result can.

    Raises:
        InstallError: If the output holds an error node, or no conditional inside the argument list
    """
    if "ERROR" in output or "MISSING" in output:
        raise InstallError("the fixture parse gives an error node.  This CLI is not the fork.", NOT_PINNED)
    if "preproc_ifdef" not in output or "argument_list" not in output:
        raise InstallError("the fixture parse has no conditional inside the argument list.  This CLI is not the "
                           "fork.", NOT_PINNED)


def verify_kit(directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that a directory holds a kit of the pinned lineage whose CLI parses the fixture as the fork does.

    Raises:
        InstallError: If a check fails
    """
    if not is_executable(directory / "bin" / "tree-sitter"):
        raise InstallError(f"{directory}/bin/tree-sitter is missing or not executable.", NOT_PINNED)
    verify_lineage(directory, kit_lineage(table))
    with tempfile.TemporaryDirectory(prefix="setup-kit-") as scratch:
        fixture = Path(scratch) / "fixture.cpp"
        fixture.write_text(KIT_FIXTURE, encoding="utf-8")
        status, output = run_program([str(directory / "bin" / "tree-sitter"), "parse", "--lib-path",
                                      str(directory / "lib" / "cpp.so"), "--lang-name", "cpp", str(fixture)],
                                     {"HOME": "/nonexistent"}, with_errors=True)
    if status != 0:
        raise InstallError(f"the CLI of {directory} fails to run the fixture parse:\n{output}", NOT_PINNED)
    check_fork_parse(output)


def unpack_kit(download_path: Path, staging: Path, pin: pin_table.Pin) -> Path:
    """Unpack the tarball of the kit into the staging directory, and return its top directory.

    Raises:
        InstallError: If the tarball does not unpack to the directory that its name gives
    """
    import tarfile

    with tarfile.open(download_path, "r:gz") as archive:
        archive.extractall(staging, filter="tar")
    top = staging / download_path.name.removesuffix(".tar.gz")
    if not top.is_dir():
        raise InstallError(f"the tarball did not unpack to {top.name}.", NOT_PINNED)
    return top


def kit_stamp(pin: pin_table.Pin, slug: str, table: Pins) -> str:
    """Return the stamp of a kit: the pin, the platform and the lineage."""
    lineage = kit_lineage(table)
    fields = [("tag", pin.version), ("platform", slug), ("sha256", pin.digest), *lineage.items(),
              ("installed_by", INSTALL_COMMAND)]
    return "".join(f"{key:<19}{value}\n" for key, value in fields)


def kit_stamp_matches(directory: Path, pin: pin_table.Pin, slug: str, table: Pins) -> str | None:
    """Return why the stamp of a kit does not name the pin, or None."""
    fields = stamp_fields(directory / STAMP)
    if fields.get("tag") != pin.version or fields.get("sha256") != pin.digest:
        return (f"the stamp reads tag {fields.get('tag') or '<absent>'} sha256 {fields.get('sha256') or '<absent>'}, "
                f"the pin says tag {pin.version} sha256 {pin.digest}.")
    return None


# ast-grep.


def verify_ast_grep(directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that the binary of a directory gives the pinned version and reads the fixture as the guards need.

    Raises:
        InstallError: If a check fails
    """
    binary = directory / "ast-grep"
    if not is_executable(binary):
        raise InstallError(f"{binary} is missing or not executable.", NOT_PINNED)
    status, output = run_program([str(binary), "--version"], with_errors=True)
    if status != 0:
        raise InstallError(f"{binary} --version fails.", NOT_PINNED)
    check_named_version("ast-grep", output.rstrip("\n"), pin.version)
    with tempfile.TemporaryDirectory(prefix="setup-ast-grep-") as scratch:
        fixture = Path(scratch) / "fixture.sh"
        fixture.write_text(AST_GREP_FIXTURE, encoding="utf-8")
        try:
            completed = proc.run(proc.Command([str(binary), "run", "--lang", "bash", "-p", AST_GREP_PATTERN,
                                               "--json=compact", str(fixture)], timeout_s=PROGRAM_TIMEOUT_S))
        except proc.ToolFailed as problem:
            raise InstallError(f"the fixture run fails.\n{problem}", NOT_PINNED) from None
    if completed.exit != 0:
        raise InstallError(f"the fixture run fails.\n{completed.out}{completed.err}", NOT_PINNED)
    try:
        matches = json.loads(completed.out)
    except json.JSONDecodeError:
        raise InstallError("the output of the fixture run is not JSON.", NOT_PINNED) from None
    check_fixture_lines(",".join(str(match["range"]["start"]["line"]) for match in matches))


def check_named_version(name: str, line: str, version: str) -> None:
    """Make sure that the output of `NAME --version` of ast-grep or ruff is the name and the pinned version.

    Raises:
        InstallError: If it names a different program or a different version
    """
    if line != f"{name} {version}":
        raise InstallError(f"the binary reports {line!r}, the pin says {name} {version}.", NOT_PINNED)


def check_fixture_lines(lines: str) -> None:
    """Make sure that the fixture rule matches exactly the command on line 0.

    A match on line 2 is the heredoc body, and a match on line 4 is the comment.

    Raises:
        InstallError: If the lines differ
    """
    if lines != "0":
        raise InstallError(f"the fixture matches on the lines [{lines}], and the guards need exactly [0].",
                           NOT_PINNED)


def unpack_zip(download_path: Path, staging: Path, pin: pin_table.Pin) -> Path:
    """Unpack the zip of ast-grep into staging/out, give the binary its mode bit, and return staging/out."""
    import zipfile

    out = staging / "out"
    with zipfile.ZipFile(download_path) as archive:
        archive.extractall(out)
    binary = out / "ast-grep"
    if binary.is_file():
        binary.chmod(binary.stat().st_mode | 0o111)
    return out


def version_stamp(pin: pin_table.Pin, slug: str, table: Pins) -> str:
    """Return the stamp of ast-grep or cmake: the version and the SHA-256."""
    return f"{pin.version} {pin.digest}\n"


def version_stamp_matches(directory: Path, pin: pin_table.Pin, slug: str, table: Pins) -> str | None:
    """Return why the stamp of ast-grep or cmake does not name the pin, or None."""
    try:
        found = (directory / STAMP).read_text(encoding="utf-8").strip()
    except OSError:
        found = ""
    if found != f"{pin.version} {pin.digest}":
        return f"the stamp reads {found!r}, the pin says '{pin.version} {pin.digest}'."
    return None


# cmake.


def check_cmake_version(name: str, line: str, version: str) -> None:
    """Make sure that the first line of `PROGRAM --version` gives the pinned version.

    Raises:
        InstallError: If it gives another version or another program
    """
    if line != f"{name} version {version}":
        raise InstallError(f"{name} gives {line!r}, the pin says {name} version {version}.", NOT_PINNED)


def verify_cmake(directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that the cmake and the ctest of a directory give the pinned version.

    Raises:
        InstallError: If a program is missing or gives another version
    """
    bin_dir = directory / "bin"
    for name in ("cmake", "ctest"):
        program = bin_dir / name
        if not is_executable(program):
            raise InstallError(f"{program} is missing or not executable.", NOT_PINNED)
        status, output = run_program([str(program), "--version"])
        if status != 0:
            raise InstallError(f"{program} --version fails.", NOT_PINNED)
        lines = output.splitlines()
        check_cmake_version(name, lines[0].strip() if lines else "", pin.version)


def unpack_wheel(download_path: Path, staging: Path, pin: pin_table.Pin) -> Path:
    """Unpack the CMake prefix of the wheel (cmake/data) into staging/out, and return staging/out.

    A zip file keeps no mode bits that the unpack restores, so each program of bin gets its mode bit here.
    """
    import zipfile

    unpacked = staging / "unpacked"
    with zipfile.ZipFile(download_path) as archive:
        archive.extractall(unpacked)
    out = staging / "out"
    (unpacked / "cmake" / "data").rename(out)
    for program in (out / "bin").iterdir():
        program.chmod(program.stat().st_mode | 0o111)
    return out


# The tools of via=pip (THE PIP TOOLS).

# The pins of the packages of mypy, other than the pin mypy.
MYPY_REQUIRES = ("ast-serialize", "librt", "mypy-extensions", "pathspec", "typing-extensions")


def requirements(pins: tuple[pin_table.Pin, ...]) -> str:
    """Return the text of the file `requirements.txt` of pip: the name, the URL and the SHA-256 of each pin."""
    return "".join(f"{pin.name} @ {pin.source} --hash=sha256:{pin.digest}\n" for pin in pins)


def pip_command(arguments: list[str]) -> list[str]:
    """Return the words that run pip in this Python, in isolated mode, with arguments."""
    return [sys.executable, "-I", "-m", "pip", *arguments]


def pip_install(tool: Tool, pin: pin_table.Pin, table: Pins, staging: Path) -> Path:
    """Install the pins of a tool of via=pip into staging/out/site with pip, and return staging/out (THE PIP TOOLS).

    Raises:
        InstallError: With the status UNSUPPORTED when this Python has no pip, and NOT_PINNED when pip refuses a pin
    """
    out = staging / "out"
    with tempfile.TemporaryDirectory(prefix="setup-pip-") as scratch:
        listing = Path(scratch) / "requirements.txt"
        listing.write_text(requirements(required_pins(tool.name, tool.requires, pin, table)), encoding="utf-8")
        status, output = run_program(pip_command(["install", *PIP_OPTIONS, "--target", str(out / SITE), "-r",
                                                  str(listing)]), with_errors=True, timeout_s=PIP_TIMEOUT_S)
    if status != 0 and NO_PIP in output:
        raise InstallError(f"the Python {sys.executable} has no pip, which installs {tool.name}.", UNSUPPORTED)
    if status != 0:
        raise InstallError(f"pip refuses the pins of {tool.name}, and nothing was installed:\n{output}", NOT_PINNED)
    return out


def mypy_command(site: Path, arguments: list[str]) -> list[str]:
    """Return the words that start the mypy of a site directory with arguments, with no site of this Python."""
    return [sys.executable, "-I", "-S", "-c", MYPY_START, str(site), *arguments]


def check_mypy_version(line: str, version: str) -> None:
    """Make sure that the output of `mypy --version` names the pinned version and a compiled mypy.

    Raises:
        InstallError: If it names a different version, or a mypy of pure Python
    """
    if line != f"mypy {version} (compiled: yes)":
        raise InstallError(f"mypy reports {line!r}, the pin says mypy {version} (compiled: yes).", NOT_PINNED)


def verify_mypy(directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that the site directory of a directory holds the pinned mypy, which starts.

    Raises:
        InstallError: If mypy is missing, does not start or gives a different version
    """
    site = directory / SITE
    if not (site / "mypy").is_dir():
        raise InstallError(f"{site} holds no package mypy.", NOT_PINNED)
    status, output = run_program(mypy_command(site, ["--version"]), with_errors=True)
    if status != 0:
        raise InstallError(f"the mypy of {site} does not start:\n{output}", NOT_PINNED)
    check_mypy_version(output.strip(), pin.version)


def verify_ruff(directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that the binary of the site directory of a directory gives the pinned version of ruff.

    Raises:
        InstallError: If the binary is missing, or gives a different version
    """
    binary = directory / SITE / "bin" / "ruff"
    if not is_executable(binary):
        raise InstallError(f"{binary} is missing or not executable.", NOT_PINNED)
    status, output = run_program([str(binary), "--version"], with_errors=True)
    if status != 0:
        raise InstallError(f"{binary} --version fails:\n{output}", NOT_PINNED)
    check_named_version("ruff", output.strip(), pin.version)


def pip_unpack(download_path: Path, staging: Path, pin: pin_table.Pin) -> Path:
    """Refuse the call: pip_install() installs a tool of via=pip, and no download comes before it.

    Raises:
        InstallError: Always
    """
    raise InstallError(f"the pin {pin.name} has via=pip, and pip installs it with no unpack.", NOT_PINNED)


def pip_stamp(requires: tuple[str, ...], pin: pin_table.Pin, slug: str, table: Pins) -> str:
    """Return the stamp of a tool of via=pip: the name, the version and the SHA-256 of each of its pins."""
    return "".join(f"{item.name} {item.version} {item.digest}\n"
                   for item in required_pins(pin.name, requires, pin, table))


def pip_stamp_matches(requires: tuple[str, ...], directory: Path, pin: pin_table.Pin, slug: str,
                      table: Pins) -> str | None:
    """Return why the stamp of a tool of via=pip does not name each of its pins, or None."""
    try:
        found = (directory / STAMP).read_text(encoding="utf-8")
    except OSError:
        found = ""
    wanted = pip_stamp(requires, pin, slug, table)
    if found != wanted:
        return f"the stamp reads {found!r}, the pins say {wanted!r}."
    return None


TOOLS: dict[str, Tool] = {
    "tree-sitter": Tool(
        "tree-sitter", "tree-sitter-kit", "tree-sitter-cpp-fork-",
        {"linux-x86_64": "linux-x64", "linux-aarch64": "linux-arm64", "darwin-aarch64": "macos-arm64"},
        verify_kit, unpack_kit, kit_stamp, kit_stamp_matches, lambda directory: directory / "bin" / "tree-sitter",
        True),
    "ast-grep": Tool(
        "ast-grep", "ast-grep", "ast-grep-",
        {"linux-x86_64": "x86_64-unknown-linux-gnu", "linux-aarch64": "aarch64-unknown-linux-gnu",
         "darwin-aarch64": "aarch64-apple-darwin"},
        verify_ast_grep, unpack_zip, version_stamp, version_stamp_matches, lambda directory: directory / "ast-grep",
        False),
    "cmake": Tool(
        "cmake", "cmake", "cmake-", {"linux-x86_64": "x86_64", "linux-aarch64": "aarch64"},
        verify_cmake, unpack_wheel, version_stamp, version_stamp_matches, lambda directory: directory / "bin", True),
    "mypy": Tool(
        "mypy", "mypy", "mypy-", {"linux-x86_64": "x86_64", "linux-aarch64": "aarch64"},
        verify_mypy, pip_unpack, functools.partial(pip_stamp, MYPY_REQUIRES),
        functools.partial(pip_stamp_matches, MYPY_REQUIRES), lambda directory: directory / SITE, False, MYPY_REQUIRES),
    "ruff": Tool(
        "ruff", "ruff", "ruff-", {"linux-x86_64": "x86_64", "linux-aarch64": "aarch64"},
        verify_ruff, pip_unpack, functools.partial(pip_stamp, ()), functools.partial(pip_stamp_matches, ()),
        lambda directory: directory / SITE / "bin" / "ruff", False),
}


# ── Verify, install and find ───────────────────────────────────────


def verify_installed(tool: Tool, directory: Path, pin: pin_table.Pin, table: Pins) -> None:
    """Make sure that an installed directory meets its pin: the stamp, then the checks of the tool.

    Raises:
        InstallError: With the status ABSENT when the directory is absent, else NOT_PINNED
    """
    if not directory.is_dir():
        raise InstallError(f"the {tool.name} of the pin is absent from {directory}.", ABSENT)
    if not (directory / STAMP).is_file():
        raise InstallError(f"{directory} has no {STAMP} stamp.  The install did not complete.", NOT_PINNED)
    problem = tool.stamp_matches(directory, pin, tool.slug(pin), table)
    if problem is not None:
        raise InstallError(problem, NOT_PINNED)
    tool.verify(directory, pin, table)


def installed(name: str, root: Path | None = None, table: Pins | None = None) -> Path:
    """Return the directory of an installed tool that meets its pin, and record its use.

    Args:
        name: A tool of TOOLS
        root: The tools directory, by default tools_root() of assay.core.cache
        table: The pins, by default the pin table of the tree

    Raises:
        InstallError: If the tool is absent or does not meet its pin, with the status ABSENT, or UNSUPPORTED when the
            host has no pin
    """
    tool = TOOLS[name]
    pins = pin_table.load() if table is None else table
    pin = pin_of(tool, pins)
    directory = directory_of(tool, pin, cache.tools_root() if root is None else root, pins)
    try:
        verify_installed(tool, directory, pin, pins)
    except InstallError as error:
        raise InstallError(f"the pinned {name} is not installed: {error}  Run: {INSTALL_COMMAND} {name}",
                           ABSENT) from None
    mark_used(directory)
    return directory


def program(name: str, root: Path | None = None, table: Pins | None = None) -> Path:
    """Return what --print-bin prints for an installed tool (THE TOOLS), and record its use.

    Raises:
        InstallError: As installed() does
    """
    return TOOLS[name].program(installed(name, root, table))


def finish(tool: Tool, directory: Path, root: Path) -> None:
    """Record the use of a tool, remove each directory of the tool that no run used for long, and export its bin."""
    mark_used(directory)
    prune(root, tool.prefix, directory)
    if tool.exports_bin:
        export_github_path(directory / "bin", tool.name)


def fetched(tool: Tool, pin: pin_table.Pin, staging: Path, fetch: Callable[[str, Path], None]) -> Path:
    """Download a pin into the staging directory, check its SHA-256, and return what `tool.unpack` gives.

    Raises:
        InstallError: If the download fails, or its SHA-256 is not the digest of the pin
    """
    download_path = staging / PurePosixPath(urllib.parse.urlsplit(pin.source).path).name
    print(f"setup install {tool.name}: download {pin.source}", file=sys.stderr)
    fetch(pin.source, download_path)
    got = sha256_of(download_path)
    if got != pin.digest:
        raise InstallError(f"SHA256 mismatch for {download_path.name}.\n  got  {got}\n  want {pin.digest}\n"
                           f"Nothing was installed.", NOT_PINNED)
    print(f"setup install {tool.name}: SHA256 matches the pin.", file=sys.stderr)
    return tool.unpack(download_path, staging, pin)


def install(name: str, root: Path | None = None, fetch: Callable[[str, Path], None] = download,
            table: Pins | None = None) -> Path:
    """Install one tool when it is absent or off its pin, and return its directory.

    Args:
        name: A tool of TOOLS
        root: The tools directory, by default tools_root() of assay.core.cache
        fetch: The function that downloads a URL into a file.  A test gives a function that copies a planted file
        table: The pins, by default the pin table of the tree

    Raises:
        InstallError: If the download, its SHA-256 or a check of the unpacked tool fails
    """
    tool = TOOLS[name]
    pins = pin_table.load() if table is None else table
    pin = pin_of(tool, pins)
    base = cache.tools_root() if root is None else root
    directory = directory_of(tool, pin, base, pins)
    try:
        verify_installed(tool, directory, pin, pins)
    except InstallError:
        pass
    else:
        print(f"setup install {name}: already at the pin ({pin.version}, {pin.platform}) at {directory}.",
              file=sys.stderr)
        finish(tool, directory, base)
        return directory
    base.mkdir(parents=True, exist_ok=True)
    # The staging directory is under the tools directory, so the final rename stays on one file system.
    staging = Path(tempfile.mkdtemp(prefix=STAGING_PREFIX, dir=base))
    try:
        if pin.via == pin_table.Via.PIP:
            print(f"setup install {name}: pip installs the pins of {name} with their SHA-256", file=sys.stderr)
            unpacked = pip_install(tool, pin, pins, staging)
        else:
            unpacked = fetched(tool, pin, staging, fetch)
        tool.verify(unpacked, pin, pins)
        (unpacked / STAMP).write_text(tool.stamp(pin, tool.slug(pin), pins), encoding="utf-8")
        # A directory at the final path failed the check above, so it is damaged or incomplete.
        if directory.exists():
            retire(directory, base)
        if publish(unpacked, directory):
            print(f"setup install {name}: installed {pin.version} at {directory}", file=sys.stderr)
        else:
            try:
                verify_installed(tool, directory, pin, pins)
            except InstallError:
                raise InstallError(f"the verified {name} did not move to {directory}.  Nothing was installed.",
                                   NOT_PINNED) from None
            print(f"setup install {name}: another install put the tool at {directory} first.", file=sys.stderr)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    finish(tool, directory, base)
    return directory


# ── The command ────────────────────────────────────────────────────

# Each name that `setup install` accepts: the tools of TOOLS, and the toolchain of the oracle.
NAMES = (*TOOLS, "oracle")


def install_oracle() -> None:
    """Install the toolchain of the crash-stop oracle into the cache of the oracle (assay.setup.oracle)."""
    from assay.oracle.rocq import OracleError
    from assay.setup import oracle

    try:
        root = oracle.install()
    except OracleError as error:
        raise InstallError(f"the toolchain of the oracle does not install: {error}", NOT_PINNED) from None
    print(f"setup install oracle: the toolchain is in {root}", file=sys.stderr)


def main(argv: list[str]) -> int:
    """Install the named tools, or print the program or the directory of one tool.

    Args:
        argv: The words after `setup install`

    Returns:
        The exit status (the docstring of the module)
    """
    from assay.core import cli

    parser = cli.parser("setup install", "install each pinned tool, or print the program of an installed tool")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--print-bin", metavar="TOOL", choices=tuple(TOOLS), help="print the program of a tool")
    mode.add_argument("--print-dir", metavar="TOOL", choices=tuple(TOOLS),
                      help="print the install directory of a tool, also when it is absent")
    parser.add_argument("tools", nargs="*", metavar="TOOL", help=f"one of {', '.join(NAMES)}")
    options = parser.parse_args(argv)
    unknown = [name for name in options.tools if name not in NAMES]
    if unknown:
        parser.error(f"{', '.join(unknown)} is not a tool.  The tools are {', '.join(NAMES)}")
    try:
        if options.print_dir:
            tool = TOOLS[options.print_dir]
            pins = pin_table.load()
            print(directory_of(tool, pin_of(tool, pins), cache.tools_root(), pins))
            return OK
        if options.print_bin:
            print(program(options.print_bin))
            return OK
        for name in options.tools or DEFAULT_TOOLS:
            if name == "oracle":
                install_oracle()
            else:
                install(name)
    except InstallError as error:
        print(f"setup install: {error}", file=sys.stderr)
        return error.status
    return OK
