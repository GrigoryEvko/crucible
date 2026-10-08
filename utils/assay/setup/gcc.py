"""setup gcc — build the patched GCC that Crucible needs, and install it into a prefix.

usage: setup gcc PREFIX [JOBS] [--dry-run]

    PREFIX     The install prefix.  The compiler is PREFIX/usr/bin/g++-16p.  Give
               PREFIX to CMake as CRUCIBLE_GCC16_PREFIX, or give the compiler
               path as CRUCIBLE_CXX.
    JOBS       The number of make jobs.  The default is the CPU count of the
               process.
    --dry-run  Do the checks of the host, print each command of the build, and
               run none of them.

Local development and CI use this command.  It downloads the commit of the pin
gcc of utils/pins.txt, applies each file of utils/toolchain/gcc/patches/ in name
order, and builds GCC with the configuration of CONFIGURE.  CI keys its cache of
the install on the pin, the patches and this file, so a change of one of them
builds the compiler again.

The source tree and the object tree go in a temporary directory below TMPDIR,
or below /tmp when TMPDIR is not set.  The command deletes that directory when
it stops.  The two trees need approximately 6 GB.  With 8 jobs the build takes
approximately 8 minutes.

The build needs git, tar, make, flex, a C and C++ compiler, and the
development files of GMP, MPFR, MPC, ISL, zlib and zstd.  On Fedora:
    dnf install gcc-c++ make flex git tar gmp-devel mpfr-devel libmpc-devel isl-devel zlib-devel libzstd-devel

A repository variable in the environment of the caller wins over `git -C`.
With GIT_DIR exported, the init, the fetch and the apply would write to the
repository that GIT_DIR names.  assay.core.git.init() clears each such
variable, and it stops unless git resolves the new repository itself.

Exit status: 0 when the compiler is installed, 1 when a step fails, 2 on a
usage error.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from assay.core import git
from assay.core import pins as pin_table
from assay.core.paths import REPO_ROOT

PIN_NAME = "gcc"
PATCHES = REPO_ROOT / "utils" / "toolchain" / "gcc" / "patches"
TOOLS = ("git", "tar", "make", "flex", "gcc", "g++")
HEADERS = ("gmp.h", "mpfr.h", "mpc.h", "isl/version.h", "zlib.h", "zstd.h")
# The configuration of the build, after --prefix=PREFIX/usr.
CONFIGURE = ("--program-suffix=-16p", "--enable-languages=c,c++,lto", "--disable-multilib", "--disable-bootstrap",
             "--enable-checking=release", "--with-system-zlib")
COMPILER = Path("usr") / "bin" / "g++-16p"


class BuildError(Exception):
    """A check of the host or a step of the build fails."""


def default_jobs() -> int:
    """Return the CPU count of this process, as nproc gives it."""
    return len(os.sched_getaffinity(0))


def check_host(environment: dict[str, str]) -> None:
    """Make sure that each tool of TOOLS is in PATH, and that g++ finds each development header of HEADERS.

    Raises:
        BuildError: If a tool or a header is missing
    """
    for tool in TOOLS:
        if shutil.which(tool) is None:
            raise BuildError(f"'{tool}' is not on PATH.  Install it, then run the command again.")
    probe = "".join(f"#include <{header}>\n" for header in HEADERS)
    completed = subprocess.run(["g++", "-E", "-x", "c++", "-"], input=probe, text=True, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, env=environment, check=False)
    if completed.returncode != 0:
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


def run(argv: list[str], environment: dict[str, str], cwd: Path | None = None) -> None:
    """Run one step of the build, with its output on the terminal.

    Raises:
        BuildError: If the step exits with a status that is not 0
    """
    completed = subprocess.run(argv, cwd=cwd, env=environment, check=False)
    if completed.returncode != 0:
        raise BuildError(f"the step `{' '.join(argv)}` exits with {completed.returncode}")


def extract(source: Path, environment: dict[str, str]) -> None:
    """Extract the tree of FETCH_HEAD into the source directory: `git archive FETCH_HEAD | tar -x -C SOURCE`.

    Raises:
        BuildError: If git or tar fails
    """
    archive = subprocess.Popen(["git", "-C", str(source), "archive", "FETCH_HEAD"], stdout=subprocess.PIPE,
                               env=git.clean_environment(environment))
    assert archive.stdout is not None
    unpack = subprocess.run(["tar", "-x", "-C", str(source)], stdin=archive.stdout, env=environment, check=False)
    archive.stdout.close()
    if archive.wait() != 0 or unpack.returncode != 0:
        raise BuildError(f"the extract of FETCH_HEAD into {source} fails")


def build(prefix: Path, jobs: int, dry_run: bool) -> None:
    """Build the patched GCC and install it into PREFIX, or print the commands with dry_run.

    Raises:
        BuildError: If a check or a step fails
    """
    pin = pin_table.find(pin_table.load(), PIN_NAME, "")
    if pin is None or pin.via != pin_table.Via.GIT:
        raise BuildError(f"{pin_table.PINS_PATH} has no pin {PIN_NAME} with via=git")
    applied = patches()
    environment = {**os.environ, "LC_ALL": "C"}
    check_host(environment)
    configure = [str(Path("SOURCE") / "configure"), f"--prefix={prefix / 'usr'}", *CONFIGURE]
    if dry_run:
        print(f"setup gcc: download {pin.source} at {pin.commit} (GCC {pin.version})")
        print(f"  git -C SOURCE init -q, then git -C SOURCE fetch -q --depth 1 {pin.source} {pin.commit}")
        print("  git -C SOURCE archive FETCH_HEAD | tar -x -C SOURCE")
        for patch in applied:
            print(f"  git -C SOURCE apply {patch}")
        print(f"  cd OBJECT && {' '.join(configure)}")
        print(f"  make -j{jobs}")
        print("  make install-strip")
        print(f"  {prefix / COMPILER} -v")
        return
    work = Path(tempfile.mkdtemp(prefix="crucible-gcc.", dir=os.environ.get("TMPDIR") or "/tmp"))
    try:
        source = work / "src"
        objects = work / "obj"
        source.mkdir()
        print(f"setup gcc: download {pin.source} at {pin.commit}")
        try:
            git.init(source)
            git.run(source, "fetch", "-q", "--depth", "1", pin.source, pin.commit, extra={"LC_ALL": "C"})
            fetched = git.run(source, "rev-parse", "FETCH_HEAD").decode().strip()
        except (git.GitError, git.NotIsolated, subprocess.CalledProcessError) as error:
            raise BuildError(str(error)) from None
        if fetched != pin.commit:
            raise BuildError(f"the download is not commit {pin.commit}")
        extract(source, environment)
        for patch in applied:
            print(f"setup gcc: apply {patch.name}")
            try:
                git.run(source, "apply", str(patch), extra={"LC_ALL": "C"})
            except git.GitError as error:
                raise BuildError(str(error)) from None
        objects.mkdir()
        run([str(source / "configure"), *configure[1:]], environment, objects)
        run(["make", f"-j{jobs}"], environment, objects)
        # install-strip removes the debug information from the compiler binaries.  cc1plus then goes from
        # approximately 410 MB to 46 MB, and the install from 1.9 GB to 254 MB, so the CI cache stays small.
        run(["make", "install-strip"], environment, objects)
        run([str(prefix / COMPILER), "-v"], environment)
        print(f"setup gcc: installed {prefix / COMPILER}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


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
    parser.add_argument("--dry-run", action="store_true", help="do the checks and print each command, run none")
    options = parser.parse_args(argv)
    if not options.prefix:
        parser.error("give the install prefix")
    jobs = default_jobs() if options.jobs is None else options.jobs
    if not str(jobs).isdigit() or int(jobs) < 1:
        parser.error(f"JOBS must be a positive integer, not '{jobs}'")
    try:
        build(Path(os.path.realpath(options.prefix)), int(jobs), options.dry_run)
    except BuildError as error:
        print(f"setup gcc: {error}", file=sys.stderr)
        return 1
    return 0
