"""fuzz — run one coverage-guided AFL++ campaign on one boundary harness (`loop fuzz`).

THE ARGUMENTS
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS

    BUILD_DIR  The instrumented build.  When BUILD_DIR is not configured, the
               command configures it with afl-g++-fast.  It also keeps a
               second build, BUILD_DIR-cmplog, with comparison logging, for
               the main instance.  It builds fuzz_HARNESS in the two builds.
    OUT_DIR    The output directory of afl-fuzz.  The seeds go to
               OUT_DIR/seeds, and the scratch files of the harness go to
               OUT_DIR/tmp.  A harness that afl-fuzz stops leaves its scratch
               directory there, so the command removes OUT_DIR/tmp when it
               exits, also when a step fails.
    HARNESS    A name from test/fuzz/boundary/harnesses, such as region.
    CPU_LIST   The cores, such as 100-131 or 100-115,200-215.  One instance
               runs on each core.  The first one is the main instance.  The
               command refuses a list that names a reserved core.
    SECONDS    The time limit of each instance.

THE ENVIRONMENT
    CRUCIBLE_FUZZ_RESERVED_CPUS  The cores that no instance can use.  The default is 88-95
    CRUCIBLE_FUZZ_JOBS           The build jobs.  The default is 6
    AFL_BIN                      The directory of the AFL++ tools.  The default is $HOME/.local/bin
    CRUCIBLE_GCC16_PREFIX        The prefix of the patched GCC that the compiler wrappers call.  The default is
                                 $HOME/.local/gcc16-patched

    ASan aborts and does not symbolize, so that afl-fuzz sees each crash.
    The core dumps of this host go to the system handler, so afl-fuzz does
    not wait for them.

Usage
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS
    guard selftest loop.fuzz

Exit 0 when each instance ended with status 0, 1 when an instance or a step failed, 2 on a usage error or a failed
self-test.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.readers import cmake as cmake_reader
from assay.setup import pins as cmake_pin

DEFAULT_RESERVED = "88-95"
DEFAULT_JOBS = "6"
HARNESSES = Path("test/fuzz/boundary/harnesses")
# The time after SECONDS that an instance gets before SIGINT stops it.
STOP_GRACE_S = 60
# The time after SECONDS and STOP_GRACE_S that an instance gets before the limit of assay.core.proc stops its process
# group.  The command `timeout` of the instance sends SIGINT first, and afl-fuzz then writes its state and ends.
INSTANCE_MARGIN_S = 120
SANITIZER_OPTIONS = {
    "ASAN_OPTIONS": "abort_on_error=1:symbolize=0:detect_leaks=0:allocator_may_return_null=1",
    "UBSAN_OPTIONS": "halt_on_error=1:abort_on_error=1:symbolize=0",
    "AFL_SKIP_CPUFREQ": "1",
    "AFL_NO_AFFINITY": "1",
    "AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES": "1",
    "AFL_NO_UI": "1",
}


class FuzzError(Exception):
    """A usage error: a bad argument, an unknown harness or a reserved core."""


@dataclass(frozen=True, slots=True)
class Campaign:
    """The arguments of one campaign."""

    build_dir: Path
    out_dir: Path
    harness: str
    cpus: tuple[int, ...]
    seconds: int


def expand_cpus(text: str) -> tuple[int, ...]:
    """Return each core of a list such as 1-3,7, in the order of the list.

    Raises:
        FuzzError: If a part is not a core or a range of cores
    """
    cpus: list[int] = []
    for part in text.split(","):
        first, dash, last = part.partition("-")
        if not first.isdigit() or (dash and not last.isdigit()):
            raise FuzzError(f"{part} is not a core or a range of cores")
        cpus.extend(range(int(first), int(last if dash else first) + 1))
    return tuple(cpus)


def campaign_of(argv: list[str], root: Path, reserved_text: str) -> Campaign:
    """Read the arguments, and refuse an empty OUT_DIR, an unknown harness, a bad time and a reserved core.

    Raises:
        FuzzError: If an argument is not correct
    """
    if len(argv) != 5:
        raise FuzzError("usage: loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS")
    build_text, out_text, harness, cpu_text, seconds_text = argv
    # The exit removes OUT_DIR/tmp, so an empty OUT_DIR would name /tmp.
    if not out_text:
        raise FuzzError("OUT_DIR must not be empty")
    if not (root / HARNESSES / f"{harness}.h").is_file():
        raise FuzzError(f"no harness named {harness} in {HARNESSES}")
    if not seconds_text.isdigit():
        raise FuzzError("SECONDS must be a whole number")
    cpus = expand_cpus(cpu_text)
    if not cpus:
        raise FuzzError("CPU_LIST names no core")
    reserved = set(expand_cpus(reserved_text))
    for cpu in cpus:
        if cpu in reserved:
            raise FuzzError(f"core {cpu} is reserved ({reserved_text})")
    return Campaign(Path(build_text), Path(out_text), harness, cpus, int(seconds_text))


def configure_and_build(root: Path, build_dir: Path, harness: str, afl_bin: Path, jobs: str,
                        environment: dict[str, str]) -> None:
    """Configure an instrumented build when it is not configured, and build fuzz_HARNESS in it.

    The configure runs the cmake of PATH, and the build runs the cmake that
    configured the directory (`setup check`, utils/assay/setup/pins.py).

    Raises:
        proc.ToolFailed: If the configure or the build cannot start, runs past its limit or fails
        cmake_pin.PinError: If the build directory names no cmake
    """
    if not cmake_reader.is_configured(build_dir):
        proc.require(proc.run(proc.Command(
            ["cmake", "-S", str(root), "-B", str(build_dir), "-G", "Ninja",
             f"-DCMAKE_TOOLCHAIN_FILE={root / 'cmake' / 'Toolchain-gcc16.cmake'}",
             f"-DCMAKE_C_COMPILER={afl_bin / 'afl-gcc-fast'}", f"-DCMAKE_CXX_COMPILER={afl_bin / 'afl-g++-fast'}",
             "-DCMAKE_BUILD_TYPE=Debug", "-DCRUCIBLE_BENCH=OFF", "-DCRUCIBLE_WERROR=ON", "-DCRUCIBLE_FUZZ=ON"],
            timeout_s=proc.BUILD_S, env=environment, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT)))
    build_cmake = cmake_pin.configured_program(build_dir, "cmake")
    proc.require(proc.run(proc.Command(
        ["nice", "-n", "10", build_cmake, "--build", str(build_dir), "-j", jobs, "--target", f"fuzz_{harness}"],
        timeout_s=proc.BUILD_S, env=environment, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT)))


def instance_command(campaign: Campaign, index: int, afl_bin: Path, binary: Path, cmplog_binary: Path) -> list[str]:
    """Return the command of one instance: on its core, with its time limit, the main role for the first one.

    Args:
        campaign: The campaign
        index: The index of the instance, also of its core in the list
        afl_bin: The directory of the AFL++ tools
        binary: The instrumented harness
        cmplog_binary: The harness with comparison logging

    Returns:
        The command
    """
    role = ["-M", "main", "-c", str(cmplog_binary)] if index == 0 else ["-S", f"secondary{index}"]
    return ["taskset", "-c", str(campaign.cpus[index]), "timeout", "--signal=INT",
            str(campaign.seconds + STOP_GRACE_S), str(afl_bin / "afl-fuzz"), "-i", str(campaign.out_dir / "seeds"),
            "-o", str(campaign.out_dir / "afl"), "-m", "none", "-t", "2000", "-V", str(campaign.seconds), *role,
            "--", str(binary)]


def instance_status(instance: proc.Running) -> int:
    """Wait for the end of one instance, and return its exit status, or 1 when it ran past its limit.

    Args:
        instance: The started instance

    Returns:
        The exit status
    """
    try:
        return instance.wait().exit
    except proc.TimedOut as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 1


def run_campaign(root: Path, campaign: Campaign) -> int:
    """Build the two instrumented builds, write the seeds, and run one instance on each core.

    Args:
        root: The source tree
        campaign: The campaign

    Returns:
        0 when each instance ended with status 0, else 1
    """
    afl_bin = Path(os.environ.get("AFL_BIN", str(Path.home() / ".local" / "bin")))
    jobs = os.environ.get("CRUCIBLE_FUZZ_JOBS", DEFAULT_JOBS)
    gcc_bin = Path(os.environ.get("CRUCIBLE_GCC16_PREFIX", str(Path.home() / ".local" / "gcc16-patched"))) / "usr" / "bin"
    environment = proc.environment({"AFL_CC": str(gcc_bin / "gcc-16p"), "AFL_CXX": str(gcc_bin / "g++-16p")})
    cmplog_dir = Path(f"{campaign.build_dir}-cmplog")
    configure_and_build(root, campaign.build_dir, campaign.harness, afl_bin, jobs, environment)
    configure_and_build(root, cmplog_dir, campaign.harness, afl_bin, jobs, dict(environment, AFL_GCC_CMPLOG="1"))
    binary = campaign.build_dir / "test" / "fuzz" / f"fuzz_{campaign.harness}"
    cmplog_binary = cmplog_dir / "test" / "fuzz" / f"fuzz_{campaign.harness}"
    scratch = campaign.out_dir / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        proc.require(proc.run(proc.Command([str(binary), f"--write-seeds={campaign.out_dir / 'seeds'}"],
                                           timeout_s=proc.TOOL_S, env=environment, stdout=proc.Output.INHERIT,
                                           stderr=proc.Output.INHERIT)))
        fuzz_environment = dict(environment, TMPDIR=str(scratch), **SANITIZER_OPTIONS)
        instances: list[proc.Running] = []
        try:
            for index in range(len(campaign.cpus)):
                instances.append(proc.start(proc.Command(
                    instance_command(campaign, index, afl_bin, binary, cmplog_binary),
                    timeout_s=campaign.seconds + STOP_GRACE_S + INSTANCE_MARGIN_S, env=fuzz_environment,
                    stdout=campaign.out_dir / f"instance-{index}.log", stderr=proc.Output.MERGE)))
            print(f"loop fuzz: {len(instances)} instances of fuzz_{campaign.harness} for {campaign.seconds} "
                  f"seconds, output in {campaign.out_dir}/afl", flush=True)
            statuses = [instance_status(instance) for instance in instances]
        finally:
            # An instance that the loop did not wait for, after an error or an interrupt, stops with its group.
            for instance in instances:
                instance.stop()
        return 0 if all(status == 0 for status in statuses) else 1
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


# ── The self-test ──────────────────────────────────────────────────────────


def self_test() -> int:
    """Check the core lists, the refusals of bad arguments and the commands of the instances.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    expect("a list of ranges and cores gives each core in its order",
           expand_cpus("100-102,7") == (100, 101, 102, 7))
    for text in ("1-", "a", "1-b", "1,,2"):
        try:
            expand_cpus(text)
            expect(f"the list {text!r} is refused", False)
        except FuzzError:
            expect(f"the list {text!r} is refused", True)
    with tempfile.TemporaryDirectory(prefix="loop-fuzz-") as work:
        root = Path(work)
        (root / HARNESSES).mkdir(parents=True)
        (root / HARNESSES / "region.h").write_text("", encoding="utf-8")
        campaign = campaign_of(["build", "out", "region", "100-101", "60"], root, DEFAULT_RESERVED)
        expect("the arguments of a campaign are read",
               campaign == Campaign(Path("build"), Path("out"), "region", (100, 101), 60))
        refusals = {"an empty OUT_DIR": ["build", "", "region", "100", "60"],
                    "an unknown harness": ["build", "out", "absent", "100", "60"],
                    "a time that is not a whole number": ["build", "out", "region", "100", "1.5"],
                    "a reserved core": ["build", "out", "region", "90-100", "60"],
                    "a wrong count of arguments": ["build", "out", "region", "100"]}
        for name, argv in refusals.items():
            try:
                campaign_of(argv, root, DEFAULT_RESERVED)
                expect(f"{name} is refused", False)
            except FuzzError:
                expect(f"{name} is refused", True)
        afl = Path("/afl")
        main_command = instance_command(campaign, 0, afl, Path("b"), Path("c"))
        secondary = instance_command(campaign, 1, afl, Path("b"), Path("c"))
        expect("the first instance is the main one with the comparison log, on the first core",
               main_command[:6] == ["taskset", "-c", "100", "timeout", "--signal=INT", "120"]
               and main_command[-6:] == ["-M", "main", "-c", "c", "--", "b"])
        expect("each other instance is a secondary one, on its core",
               secondary[2] == "101" and secondary[-4:] == ["-S", "secondary1", "--", "b"])

    if failures:
        print(f"loop fuzz --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop fuzz --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the campaign, or the self-test.

    Args:
        argv: The words after `loop fuzz`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    try:
        campaign = campaign_of(argv, REPO_ROOT, os.environ.get("CRUCIBLE_FUZZ_RESERVED_CPUS", DEFAULT_RESERVED))
    except FuzzError as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 2
    try:
        return run_campaign(REPO_ROOT, campaign)
    except (proc.ToolFailed, cmake_pin.PinError, OSError) as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 1
