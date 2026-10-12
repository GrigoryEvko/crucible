"""setup gcc — build the patched GCC that Crucible needs, and install it into a prefix.

usage: setup gcc PREFIX [JOBS] [--target TRIPLET] [--work DIR] [--time-limit SECONDS] [--dry-run]

    PREFIX     The install prefix.  The compiler is PREFIX/usr/bin/g++-16p.  Give
               PREFIX to CMake as CRUCIBLE_GCC16_PREFIX, or give the compiler
               path as CRUCIBLE_CXX.
    JOBS       The number of make jobs.  The default is the CPU count of the
               process.
    --target   Build a cross compiler for TRIPLET (THE CROSS COMPILER).  The
               compiler is PREFIX/usr/bin/TRIPLET-g++-16p.
    --work     Keep the source tree and the object tree in DIR.  A second run
               with the same DIR does not download again, and make continues
               from the objects of the first run.
    --time-limit
               Stop the build after SECONDS.  A run with the same --work then
               continues it.
    --dry-run  Do the checks of the host, print each command of the build, and
               run none of them.

Local development and CI use this command.  It downloads the commit of the pin
gcc of utils/pins.txt, and it applies each file of utils/toolchain/gcc/patches/
in name sequence.  Then it builds GCC with the configuration of CONFIGURE.  CI
keys its cache of the install on the pin, the patches and this file.  A change
of one of them then builds the compiler again.

With no --work, the source tree and the object tree go in a temporary directory
below TMPDIR, or below /tmp when TMPDIR is not set.  The command deletes that
directory when it stops.  The two trees use approximately 6 GB.  With 8 jobs
the build takes approximately 8 minutes.

The build uses git, tar, make, flex, a C and C++ compiler, and the development
files of GMP, MPFR, MPC, ISL, zlib and zstd.  On Fedora:
    dnf install gcc-c++ make flex git tar gmp-devel mpfr-devel libmpc-devel isl-devel zlib-devel libzstd-devel

THE CROSS COMPILER
    aarch64-linux-gnu is the one target of CROSS_TARGETS.  Before the build,
    the command unpacks three packages of Fedora 44, each with the SHA-256 of
    its pin (CrossPackage):
      * the binutils of the target (the pin binutils-aarch64), below PREFIX
      * the glibc headers and libraries of the target (the pin
        sysroot-aarch64), below PREFIX
      * the libbpf headers of the target (the pin libbpf-devel-aarch64),
        below the sysroot.  Some units of the tree include <bpf/bpf.h> also
        in a build with no BPF program.
    The sysroot is the directory of CrossTarget.sysroot below PREFIX, and GCC
    finds the binutils in PREFIX/usr/aarch64-linux-gnu/bin.  An unpack uses
    rpm2cpio and cpio.  The configuration is CROSS_CONFIGURE.
    A parse of the tree reads no library of the sanitizers, of OpenMP, of the
    transactional memory or of quadmath.  The cross build makes none of these
    libraries.

    The install holds the plugin headers of the cross compiler.  A plugin of
    that compiler is code of the host.  The patched g++ of the host compiles
    it with those headers (utils/tools/quarantine/Quarantine.cmake, THE
    COMPILER OF THE PLUGIN).  cmake/Toolchain-gcc16-aarch64.cmake finds the
    cross compiler.

A repository variable in the environment of the caller wins over `git -C`.
With GIT_DIR exported, the init, the fetch and the apply write to the
repository that GIT_DIR names.  assay.core.proc removes each such variable
from the environment of each command, and assay.core.git.init() stops
unless git resolves the new repository itself.

Exit status: 0 when the compiler is installed, 1 when a step fails, 2 on a
usage error.
"""

import os
import shutil
import tempfile
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

from assay.core import git
from assay.core import pins as pin_table
from assay.core import proc
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT

PIN_NAME = "gcc"
PATCHES = REPO_ROOT / "utils" / "toolchain" / "gcc" / "patches"
TOOLS = ("git", "tar", "make", "flex", "gcc", "g++")
# The tools that unpack the packages of a cross build.
PACKAGE_TOOLS = ("rpm2cpio", "cpio")
HEADERS = ("gmp.h", "mpfr.h", "mpc.h", "isl/version.h", "zlib.h", "zstd.h")
# The configuration of the build, after --prefix=PREFIX/usr.
CONFIGURE = ("--program-suffix=-16p", "--enable-languages=c,c++,lto", "--disable-multilib", "--disable-bootstrap",
             "--enable-checking=release", "--with-system-zlib")
# The configuration of a cross build, after --prefix, --target, --program-prefix and --with-sysroot (THE CROSS
# COMPILER).  An explicit suffix stops the prefix TRIPLET- that configure gives the programs of a cross build by
# default.  configure_words() then gives the prefix and the suffix.
CROSS_CONFIGURE = ("--program-suffix=-16p", "--enable-languages=c,c++,lto", "--disable-multilib",
                   "--enable-checking=release", "--with-system-zlib", "--disable-libsanitizer", "--disable-libgomp",
                   "--disable-libitm", "--disable-libquadmath", "--disable-libssp", "--disable-libvtv")
COMPILER = Path("usr") / "bin" / "g++-16p"
# The install prefix of the patched GCC below the home directory, when the environment names none.
HOME_PREFIX = Path(".local") / "gcc16-patched"
# The time limit of one step of the build (run), in seconds.  With 8 jobs the build takes approximately 8 minutes,
# but with one job on a slow host it can take some hours.
STEP_TIMEOUT_S = 6 * 3600.0
# The file of the source tree that names the commit and the patches of the tree.  A second run with --work then
# downloads only when they change.
SOURCE_MARKER = ".crucible-source"
# The file of the object tree that holds the words of its configure step.
CONFIGURE_MARKER = ".crucible-configure"
# The directory below the prefix with one file for each package that the prefix holds.
PACKAGE_MARKERS = Path(".crucible-packages")


class BuildError(Exception):
    """A check of the host or a step of the build fails."""


@dataclass(frozen=True, slots=True)
class CrossPackage:
    """A package of a cross target: its pin, and is_in_sysroot when its files go below the sysroot, not the prefix."""

    pin: str
    is_in_sysroot: bool


@dataclass(frozen=True, slots=True)
class CrossTarget:
    """A target of a cross compiler: its triplet, its packages in the sequence of their unpack, and its sysroot.

    `sysroot` is the path of the sysroot below the prefix, as the package of the glibc of the target holds it.
    """

    triplet: str
    packages: tuple[CrossPackage, ...]
    sysroot: Path


CROSS_TARGETS = {
    "aarch64-linux-gnu": CrossTarget(
        "aarch64-linux-gnu",
        (CrossPackage("binutils-aarch64", False), CrossPackage("sysroot-aarch64", False),
         CrossPackage("libbpf-devel-aarch64", True)),
        Path("usr") / "aarch64-redhat-linux" / "sys-root" / "fc44"),
}
# The pins of the packages of each cross target.  The rule tool-pins holds them (assay.setup.pins).
CROSS_PINS = frozenset(package.pin for target in CROSS_TARGETS.values() for package in target.packages)


@dataclass(frozen=True, slots=True)
class Deadline:
    """The end of a build with --time-limit, as a time of time.monotonic(), or None for a build with no limit."""

    end: float | None = None

    def step_limit(self, step: str) -> float:
        """Return the time limit of the next step: the time to the end, and at most STEP_TIMEOUT_S.

        Raises:
            BuildError: If the end is 1 s away or less
        """
        if self.end is None:
            return STEP_TIMEOUT_S
        remaining = self.end - time.monotonic()
        if remaining <= 1.0:
            raise BuildError(f"the time limit stops the build before the step `{step}`.  Run the command again with "
                             f"the same --work, and the build continues")
        return min(STEP_TIMEOUT_S, remaining)


def toolchain_compilers() -> tuple[Path, Path]:
    """Return the C compiler and the C++ compiler of the patched GCC, with the rules of cmake/Toolchain-gcc16.cmake.

    CRUCIBLE_CXX names the g++ first, and its C compiler is the gcc with the same suffix in the same directory.  If
    CRUCIBLE_CXX is not set, CRUCIBLE_GCC16_PREFIX names the prefix.  If the two variables are not set, the prefix is
    HOME_PREFIX below the home directory.  The files can be missing, and each caller makes sure that its compiler is
    there.
    """
    named = os.environ.get("CRUCIBLE_CXX", "")
    if named:
        cxx = Path(named)
        return cxx.with_name("gcc" + cxx.name.removeprefix("g++")), cxx
    prefix_text = os.environ.get("CRUCIBLE_GCC16_PREFIX", "")
    prefix = Path(prefix_text) if prefix_text else Path.home() / HOME_PREFIX
    return prefix / COMPILER.with_name("gcc-16p"), prefix / COMPILER


def compiler_of(prefix: Path, target: CrossTarget | None) -> Path:
    """Return the C++ compiler that a build installs into the prefix: g++-16p, or TRIPLET-g++-16p for a target."""
    return prefix / COMPILER if target is None else prefix / COMPILER.with_name(f"{target.triplet}-g++-16p")


def configure_words(prefix: Path, target: CrossTarget | None) -> list[str]:
    """Return the words of the configure step after the path of the script, for a native build or a cross build."""
    if target is None:
        return [f"--prefix={prefix / 'usr'}", *CONFIGURE]
    return [f"--prefix={prefix / 'usr'}", f"--target={target.triplet}", f"--program-prefix={target.triplet}-",
            f"--with-sysroot={prefix / target.sysroot}", *CROSS_CONFIGURE]


def default_jobs() -> int:
    """Return the CPU count of this process, as nproc gives it."""
    return len(os.sched_getaffinity(0))


def check_host(environment: dict[str, str], tools: tuple[str, ...] = TOOLS) -> None:
    """Make sure that each tool of TOOLS is in PATH, and that g++ finds each development header of HEADERS.

    Raises:
        BuildError: If a tool or a header is missing
    """
    for tool in tools:
        if shutil.which(tool) is None:
            raise BuildError(f"'{tool}' is not on PATH.  Install it, then run the command again.")
    probe = "".join(f"#include <{header}>\n" for header in HEADERS)
    try:
        completed = proc.run(proc.Command(["g++", "-E", "-x", "c++", "-"], timeout_s=proc.TOOL_S, env=environment,
                                          stdin=probe.encode(), stdout=proc.Output.DISCARD,
                                          stderr=proc.Output.DISCARD))
    except proc.ToolFailed as problem:
        raise BuildError(f"the check of the development headers cannot run: {problem}") from None
    if completed.exit != 0:
        raise BuildError("a development header is missing.  Install the GMP, MPFR, MPC, ISL, zlib and zstd "
                         "development packages.")


def patches() -> list[Path]:
    """Return the patches of PATCHES in name order.

    Raises:
        BuildError: If the directory holds no .patch file
    """
    found = sorted(PATCHES.glob("*.patch"))
    if not found:
        raise BuildError(f"{PATCHES} holds no .patch file")
    return found


def run(argv: list[str], environment: dict[str, str], cwd: Path | None = None,
        timeout_s: float = STEP_TIMEOUT_S) -> None:
    """Run one step of the build, with its output on the terminal, and a time limit of timeout_s.

    Raises:
        BuildError: If the step cannot start, runs past its limit, or exits with a status that is not 0
    """
    try:
        completed = proc.run(proc.Command(argv, timeout_s=timeout_s, cwd=cwd, env=environment,
                                          stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT))
    except proc.TimedOut:
        raise BuildError(f"the time limit stops the step `{' '.join(argv)}`.  Run the command again with the same "
                         f"--work, and the build continues") from None
    except proc.ToolFailed as problem:
        raise BuildError(f"the step `{' '.join(argv)}` cannot run: {problem}") from None
    if completed.exit != 0:
        raise BuildError(f"the step `{' '.join(argv)}` exits with {completed.exit}")


def extract(source: Path, environment: dict[str, str]) -> None:
    """Extract the tree of FETCH_HEAD into the source directory, through a tar file beside it.

    git archive writes the tree to SOURCE.tar, the source directory with the
    suffix .tar, and tar unpacks that file into the source directory.  The
    function removes the tar file at its end.

    Raises:
        BuildError: If git or tar fails
    """
    archive = source.with_name(f"{source.name}.tar")
    try:
        try:
            git.run(source, "archive", "--format=tar", f"--output={archive}", "FETCH_HEAD", extra={"LC_ALL": "C"},
                    timeout_s=proc.TOOL_S)
            unpacked = proc.run(proc.Command(["tar", "-x", "-f", str(archive), "-C", str(source)],
                                             timeout_s=proc.TOOL_S, env=environment, stdout=proc.Output.INHERIT,
                                             stderr=proc.Output.INHERIT))
        except (git.GitError, proc.ToolFailed) as problem:
            raise BuildError(f"the extract of FETCH_HEAD into {source} fails: {problem}") from None
        if unpacked.exit != 0:
            raise BuildError(f"the extract of FETCH_HEAD into {source} fails")
    finally:
        archive.unlink(missing_ok=True)


def source_key(pin: pin_table.Pin, applied: list[Path]) -> str:
    """Return the text of the source marker: the commit of the pin, and the name and the SHA-256 of each patch."""
    from assay.setup import install

    return "".join([f"{pin.commit}\n", *(f"{patch.name} {install.sha256_of(patch)}\n" for patch in applied)])


def has_marker(path: Path, text: str) -> bool:
    """Return True when the file holds the text and nothing more.  A file that cannot be read holds no text."""
    try:
        return path.read_text(encoding="utf-8") == text
    except OSError:
        return False


def prepare_source(source: Path, pin: pin_table.Pin, applied: list[Path], environment: Mapping[str, str]) -> None:
    """Make the source tree hold the commit of the pin with each patch, unless its marker says that it holds them.

    Raises:
        BuildError: If the download, the extract or a patch fails
    """
    key = source_key(pin, applied)
    if has_marker(source / SOURCE_MARKER, key):
        check_report.write_line(f"setup gcc: {source} holds {pin.commit} and the patches")
        return
    shutil.rmtree(source, ignore_errors=True)
    source.mkdir(parents=True)
    check_report.write_line(f"setup gcc: download {pin.source} at {pin.commit}")
    try:
        git.init(source)
        git.run(source, "fetch", "-q", "--depth", "1", pin.source, pin.commit, extra={"LC_ALL": "C"})
        fetched = git.resolve(source, "FETCH_HEAD")
    except (git.GitError, git.NotIsolated) as error:
        raise BuildError(str(error)) from None
    if fetched != pin.commit:
        raise BuildError(f"the download is not commit {pin.commit}")
    extract(source, dict(environment))
    for patch in applied:
        check_report.write_line(f"setup gcc: apply {patch.name}")
        try:
            git.run(source, "apply", str(patch), extra={"LC_ALL": "C"})
        except git.GitError as error:
            raise BuildError(str(error)) from None
    (source / SOURCE_MARKER).write_text(key, encoding="utf-8")


def configure_objects(source: Path, objects: Path, words: list[str], environment: Mapping[str, str],
                      deadline: Deadline) -> None:
    """Run the configure step in the object tree, unless its marker holds the same words.

    A configure with other words starts from an empty object tree.

    Raises:
        BuildError: If the configure step fails or the time limit stops it
    """
    key = " ".join(words) + "\n"
    if has_marker(objects / CONFIGURE_MARKER, key):
        check_report.write_line(f"setup gcc: {objects} holds the configure step of these words")
        return
    shutil.rmtree(objects, ignore_errors=True)
    objects.mkdir(parents=True)
    run([str(source / "configure"), *words], dict(environment), objects, deadline.step_limit("configure"))
    (objects / CONFIGURE_MARKER).write_text(key, encoding="utf-8")


def unpack_package(pin: pin_table.Pin, prefix: Path, destination: Path, scratch: Path) -> None:
    """Download the package of a pin, make sure of its SHA-256, and unpack its files below the destination.

    A file of PACKAGE_MARKERS below the prefix names each package that the prefix holds.  A second run then does not
    download the package again.  Complexity: linear in the bytes of the package.

    Raises:
        BuildError: If the pin has no source or no digest, the download fails, the digest is not the pinned digest, or
            the unpack fails
    """
    from assay.setup import install

    marker = prefix / PACKAGE_MARKERS / f"{pin.name}-{pin.version}"
    if has_marker(marker, f"{pin.digest}\n"):
        return
    if pin.via != pin_table.Via.ARCHIVE or not pin.source or not pin.digest:
        raise BuildError(f"the pin {pin.name} of {pin_table.PINS_PATH} is not an archive with a source and a SHA-256")
    archive = scratch / f"{pin.name}.rpm"
    check_report.write_line(f"setup gcc: download {pin.source}")
    try:
        install.download(pin.source, archive)
    except install.InstallError as problem:
        raise BuildError(str(problem)) from None
    found = install.sha256_of(archive)
    if found != pin.digest:
        raise BuildError(f"the download of the pin {pin.name} has the SHA-256 {found}, and the pin gives {pin.digest}")
    try:
        archived = proc.run(proc.Command(["rpm2cpio", str(archive)], timeout_s=proc.TOOL_S))
        destination.mkdir(parents=True, exist_ok=True)
        unpacked = proc.run(proc.Command(["cpio", "-idm", "--quiet"], timeout_s=proc.TOOL_S, cwd=destination,
                                         stdin=archived.stdout))
    except proc.ToolFailed as problem:
        raise BuildError(f"the unpack of the pin {pin.name} cannot run: {problem}") from None
    if archived.exit != 0 or unpacked.exit != 0:
        raise BuildError(f"the unpack of the pin {pin.name} fails: {(archived.err + unpacked.err)[-2000:]}")
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(f"{pin.digest}\n", encoding="utf-8")
    check_report.write_line(f"setup gcc: unpacked {pin.name} {pin.version} into {destination}")


def unpack_cross_packages(target: CrossTarget, prefix: Path, table: tuple[pin_table.Pin, ...]) -> None:
    """Unpack each package of a cross target below the prefix or below its sysroot (THE CROSS COMPILER).

    The package of the glibc holds no lib and no lib64 at the top of the sysroot, and its libraries are in usr/.
    The function then links lib to usr/lib and lib64 to usr/lib64, as a host with a merged /usr has them.

    Raises:
        BuildError: If the pin table has no pin of the target for this host, or a package does not unpack
    """
    host = pin_table.host_platform()
    sysroot = prefix / target.sysroot
    prefix.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="crucible-package.", dir=os.environ.get("TMPDIR") or "/tmp") as scratch:
        for package in target.packages:
            pin = pin_table.find(table, package.pin, host)
            if pin is None:
                raise BuildError(f"{pin_table.PINS_PATH} has no pin {package.pin} for the host {host}")
            unpack_package(pin, prefix, sysroot if package.is_in_sysroot else prefix, Path(scratch))
    for name in ("lib", "lib64"):
        link = sysroot / name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(Path("usr") / name)


def cross_environment(prefix: Path, target: CrossTarget | None) -> Mapping[str, str]:
    """Return the overrides of the environment of the build: LC_ALL, and for a target the bin directory of the
    binutils first in PATH."""
    if target is None:
        return {"LC_ALL": "C"}
    return {"LC_ALL": "C", "PATH": f"{prefix / 'usr' / 'bin'}{os.pathsep}{os.environ.get('PATH', '')}"}


def print_plan(pin: pin_table.Pin, applied: list[Path], prefix: Path, jobs: int, target: CrossTarget | None) -> None:
    """Print each command of the build, for --dry-run."""
    lines = [f"setup gcc: download {pin.source} at {pin.commit} (GCC {pin.version})"]
    if target is not None:
        lines += [f"  unpack the pin {package.pin} into "
                  f"{prefix / target.sysroot if package.is_in_sysroot else prefix} (rpm2cpio PACKAGE | cpio -idm)"
                  for package in target.packages]
        lines += [f"  link lib and lib64 of {prefix / target.sysroot} to usr/"]
    lines += [f"  git -C SOURCE init -q, then git -C SOURCE fetch -q --depth 1 {pin.source} {pin.commit}",
              "  git -C SOURCE archive --format=tar --output=SOURCE.tar FETCH_HEAD, then tar -x -f SOURCE.tar -C SOURCE",
              *(f"  git -C SOURCE apply {patch}" for patch in applied),
              f"  cd OBJECT && {' '.join([str(Path('SOURCE') / 'configure'), *configure_words(prefix, target)])}",
              f"  make -j{jobs}", "  make install-strip", f"  {compiler_of(prefix, target)} -v"]
    for line in lines:
        check_report.write_line(line)


def build(prefix: Path, jobs: int, dry_run: bool, target: CrossTarget | None = None, work: Path | None = None,
          time_limit_s: float | None = None) -> None:
    """Build the patched GCC and install it into PREFIX, or print the commands with dry_run.

    With work, the trees stay in that directory, and a second run continues the build there.  With time_limit_s,
    each step gets the time to the end of the limit.

    Raises:
        BuildError: If a check or a step fails, or the time limit stops the build
    """
    deadline = Deadline(None if time_limit_s is None else time.monotonic() + time_limit_s)
    table = pin_table.load()
    pin = pin_table.find(table, PIN_NAME, "")
    if pin is None or pin.via != pin_table.Via.GIT:
        raise BuildError(f"{pin_table.PINS_PATH} has no pin {PIN_NAME} with via=git")
    applied = patches()
    environment = proc.environment(dict(cross_environment(prefix, target)))
    check_host(environment, TOOLS if target is None else (*TOOLS, *PACKAGE_TOOLS))
    if dry_run:
        print_plan(pin, applied, prefix, jobs, target)
        return
    root = work if work is not None else Path(tempfile.mkdtemp(prefix="crucible-gcc.",
                                                               dir=os.environ.get("TMPDIR") or "/tmp"))
    try:
        if target is not None:
            unpack_cross_packages(target, prefix, table)
        prepare_source(root / "src", pin, applied, environment)
        configure_objects(root / "src", root / "obj", configure_words(prefix, target), environment, deadline)
        run(["make", f"-j{jobs}"], environment, root / "obj", deadline.step_limit("make"))
        # install-strip removes the debug information from the compiler binaries.  cc1plus then goes from
        # approximately 410 MB to 46 MB, and the install from 1.9 GB to 254 MB, so the CI cache stays small.
        run(["make", "install-strip"], environment, root / "obj", deadline.step_limit("make install-strip"))
        run([str(compiler_of(prefix, target)), "-v"], environment)
        check_report.write_line(f"setup gcc: installed {compiler_of(prefix, target)}")
    finally:
        if work is None:
            shutil.rmtree(root, ignore_errors=True)


def main(argv: list[str]) -> int:
    """Parse the arguments, and build the patched GCC.

    Args:
        argv: The words after `setup gcc`

    Returns:
        The exit status (the docstring of the module)
    """
    from assay.core import cli

    parser = cli.parser("setup gcc", "build the patched GCC from the pinned commit and the patches")
    parser.add_argument("prefix", metavar="PREFIX", help="the install prefix; the compiler is PREFIX/usr/bin/g++-16p")
    parser.add_argument("jobs", metavar="JOBS", nargs="?", default=None, help="the number of make jobs")
    parser.add_argument("--target", metavar="TRIPLET", default=None,
                        help=f"build a cross compiler for TRIPLET: {', '.join(sorted(CROSS_TARGETS))}")
    parser.add_argument("--work", metavar="DIR", default=None,
                        help="keep the trees in DIR, so a second run continues the build there")
    parser.add_argument("--time-limit", metavar="SECONDS", type=float, default=None,
                        help="stop the build after SECONDS; a second run with the same --work continues it")
    parser.add_argument("--dry-run", action="store_true", help="do the checks and print each command, run none")
    options = parser.parse_args(argv)
    if not options.prefix:
        parser.error("give the install prefix")
    jobs = default_jobs() if options.jobs is None else options.jobs
    if not str(jobs).isdigit() or int(jobs) < 1:
        parser.error(f"JOBS must be a positive integer, not '{jobs}'")
    if options.target is not None and options.target not in CROSS_TARGETS:
        parser.error(f"the target '{options.target}' is not one of {', '.join(sorted(CROSS_TARGETS))}")
    if options.time_limit is not None and options.time_limit <= 0:
        parser.error(f"SECONDS of --time-limit must be more than 0, not '{options.time_limit}'")
    target = None if options.target is None else CROSS_TARGETS[options.target]
    work = None if options.work is None else Path(os.path.realpath(options.work))
    try:
        build(Path(os.path.realpath(options.prefix)), int(jobs), options.dry_run, target, work, options.time_limit)
    except BuildError as error:
        check_report.write_line(f"setup gcc: {error}", is_error=True)
        return 1
    return 0
