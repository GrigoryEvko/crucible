"""fuzz — run one coverage-guided AFL++ campaign on one boundary harness (`loop fuzz`).

THE ARGUMENTS
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS

    BUILD_DIR  The instrumented build.  When BUILD_DIR is not configured, the
               command configures it with afl-g++-fast.  It also keeps a
               second build, BUILD_DIR-cmplog, with comparison logging, for
               the main instance.  It builds fuzz_HARNESS in the two builds,
               with the job count of assay.core.proc (DEFAULT_JOBS).
    OUT_DIR    The output directory of afl-fuzz.  The seeds go to
               OUT_DIR/seeds, and the scratch files of the harness go to
               OUT_DIR/tmp.  A harness that afl-fuzz stops leaves its scratch
               directory there, so the command removes OUT_DIR/tmp when it
               exits, also when a step fails.
    HARNESS    A harness of test/fuzz/CMakeLists.txt, such as region: the
               first argument of a line crucible_boundary_fuzzer(NAME ...)
               (harness_names()).  A header of the harness directory that no
               such line names, such as tensor_meta_claims.h, is no harness.
               A header NAME.h that defines run_NAME is a harness, so a line
               must name it.  Such a header with no line is an error of the
               list, and the self-test fails on it (unlisted_harnesses()).
    CPU_LIST   The cores, such as 100-131 or 100-115,200-215.  One instance
               runs on each core.  The first one is the main instance.  The
               command refuses a list that names a reserved CPU of the host.
               read_reserved() of assay.loop.bench reads these CPUs, for
               `loop bench` too (THE RESERVED CPUS of that module).
    SECONDS    The time limit of each instance.

THE ENVIRONMENT
    CRUCIBLE_BENCH_SLICE         The slice of the reserved partition when the host has more than one
    AFL_BIN                      The directory of the AFL++ tools.  The default is $HOME/.local/bin
    CRUCIBLE_CXX                 The patched g++ that the compiler wrappers call, and the gcc in the same directory
    CRUCIBLE_GCC16_PREFIX        The prefix of the patched GCC, when CRUCIBLE_CXX is not set.  The default is
                                 $HOME/.local/gcc16-patched-p8.  cmake/Toolchain-gcc16.cmake reads the two variables
                                 with the same rules.  The wrappers then call the compiler of the toolchain file

    Each instance stops with SIGINT: at the end of its time, at the limit
    of assay.core.proc and at an error of the command.  afl-fuzz then
    writes its state and ends.  SIGKILL comes only STOP_GRACE_S after it.

    ASan aborts and does not symbolize, so that afl-fuzz sees each crash.
    The core dumps of this host go to the system handler, so afl-fuzz does
    not wait for them.

THE FINDINGS
    afl-fuzz ends with the status 0 also when it saved a crash or a hang, so
    the status of the instances is not the verdict of a campaign.  After the
    instances end, the command counts the files of OUT_DIR/afl/*/crashes and
    OUT_DIR/afl/*/hangs (campaign_findings()).  The README.txt that afl-fuzz
    writes in a crashes directory is no input.  A count above zero fails the
    campaign, and the report names the first input and the command that
    replays it: the harness binary with the input as its argument.

Usage
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS
    guard selftest loop.fuzz

Exit 0 when each instance ended with status 0 and the campaign saved no crash and no hang, 1 when an instance or a
step failed or the campaign saved a crash or a hang, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import sys
import tempfile
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.loop import bench
from assay.readers import cmake as cmake_reader
from assay.setup import gcc as patched_gcc
from assay.setup import pins as cmake_pin

HARNESSES = Path("test/fuzz/boundary/harnesses")
# The list of the boundary harnesses: each line crucible_boundary_fuzzer(NAME MUTATIONS) of this file names one, and
# its target fuzz_NAME.
HARNESS_LIST = Path("test/fuzz/CMakeLists.txt")
_HARNESS_LINE = re.compile(r"^crucible_boundary_fuzzer\(\s*([A-Za-z0-9_]+)\b")
# The start of the definition of the run function of a harness in its header: run_NAME of the header NAME.h.
_RUN_DEFINITION = re.compile(r"^inline void run_([A-Za-z0-9_]+)\(", re.MULTILINE)
# The file that afl-fuzz writes in each crashes directory, which is no saved input.
AFL_README = "README.txt"
# The time after SECONDS that an instance gets before SIGINT stops it, and the time after a SIGINT of
# assay.core.proc before SIGKILL.
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


@dataclass(frozen=True, slots=True)
class Findings:
    """The inputs that the instances of a campaign saved: each crash and each hang, in the order of their paths."""

    crashes: tuple[Path, ...]
    hangs: tuple[Path, ...]

    def first(self) -> Path | None:
        """Return the first saved input: the first crash, else the first hang, else None."""
        saved = (*self.crashes, *self.hangs)
        return saved[0] if saved else None


def harness_names(root: Path) -> tuple[str, ...]:
    """Return the name of each boundary harness of a tree, in the order of HARNESS_LIST.

    Each line crucible_boundary_fuzzer(NAME ...) of HARNESS_LIST names one harness, whatever arguments follow NAME.
    A header of HARNESSES that no line names, such as the helper tensor_meta_claims.h, is no harness.  A tree with no
    list has no harness.  Complexity: linear in the lines of the list.

    Args:
        root: The source tree
    """
    path = root / HARNESS_LIST
    if not path.is_file():
        return ()
    names = (_HARNESS_LINE.match(line) for line in path.read_text(encoding="utf-8").splitlines())
    return tuple(match.group(1) for match in names if match is not None)


def unlisted_harnesses(root: Path) -> tuple[str, ...]:
    """Return each harness of HARNESSES that no line of HARNESS_LIST names, in the order of the names.

    A header NAME.h that defines run_NAME is a harness.  No target builds a harness with no line, so no campaign and
    no smoke test runs it.  Such a harness is an error of the list, and not a header to skip.  A helper header such
    as tensor_meta_claims.h defines no run function of its own name.  Complexity: linear in the bytes of the headers.

    Args:
        root: The source tree
    """
    directory = root / HARNESSES
    if not directory.is_dir():
        return ()
    listed = frozenset(harness_names(root))
    found: list[str] = []
    for header in sorted(directory.glob("*.h")):
        defined = {match.group(1) for match in _RUN_DEFINITION.finditer(header.read_text(encoding="utf-8"))}
        if header.stem in defined and header.stem not in listed:
            found.append(header.stem)
    return tuple(found)


def harness_binary(build_dir: Path, harness: str) -> Path:
    """Return the fuzz binary of a harness in a build: BUILD_DIR/test/fuzz/fuzz_HARNESS."""
    return build_dir / "test" / "fuzz" / f"fuzz_{harness}"


def campaign_findings(out_dir: Path) -> Findings:
    """Return the inputs that the instances of a campaign saved (THE FINDINGS).

    Each file of OUT_DIR/afl/*/crashes and OUT_DIR/afl/*/hangs is a saved input, except the README.txt of afl-fuzz.
    A campaign with no output directory saved no input.  Complexity: linear in the files of those directories.

    Args:
        out_dir: The output directory of the campaign
    """

    def saved(kind: str) -> tuple[Path, ...]:
        return tuple(
            sorted(path for path in (out_dir / "afl").glob(f"*/{kind}/*") if path.is_file() and path.name != AFL_README)
        )

    return Findings(saved("crashes"), saved("hangs"))


def replay_command(binary: Path, saved_input: Path) -> str:
    """Return the command that replays one saved input: the harness binary with the input as its argument."""
    return f"{binary} {saved_input}"


def campaign_verdict(statuses: Sequence[int], findings: Findings, binary: Path) -> tuple[int, tuple[str, ...]]:
    """Return the exit status of a campaign and the lines of its report (THE FINDINGS).

    The status is 0 only when each instance ended with status 0 and the campaign saved no crash and no hang.  A
    saved input fails the campaign, and the report names the counts, the first input and its replay command.

    Args:
        statuses: The exit status of each instance
        findings: The inputs that the instances saved (campaign_findings())
        binary: The harness binary of the campaign
    """
    first = findings.first()
    if first is not None:
        counts = f"{len(findings.crashes)} crash input(s) and {len(findings.hangs)} hang input(s)"
        return 1, (
            f"the campaign of {binary.name} saved {counts}",
            f"replay the first input with: {replay_command(binary, first)}",
        )
    if not all(status == 0 for status in statuses):
        return 1, (f"an instance of {binary.name} ended with a status other than 0",)
    return 0, ()


def afl_directory() -> Path:
    """Return the directory of the AFL++ tools: AFL_BIN, else $HOME/.local/bin (THE ENVIRONMENT)."""
    return Path(os.environ.get("AFL_BIN", str(Path.home() / ".local" / "bin")))


def reserved_cpus(cpu_root: Path = bench.CPU_ROOT, cgroup_root: Path = bench.CGROUP_ROOT) -> tuple[int, ...]:
    """Return the cores that no instance can use: the reserved CPUs of the host, which `loop bench` reads too.

    CRUCIBLE_BENCH_SLICE selects the partition when the host has more than one.  Complexity: the complexity of
    read_reserved() of assay.loop.bench.

    Args:
        cpu_root: The CPU directory of sysfs
        cgroup_root: The root of the cgroup v2 hierarchy

    Raises:
        bench.BenchError: If the host has two partitions and CRUCIBLE_BENCH_SLICE names none of them
    """
    return bench.read_reserved(bench.Settings.from_environment().slice_name, cpu_root, cgroup_root).cpus


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


def campaign_of(argv: list[str], root: Path, reserved: tuple[int, ...]) -> Campaign:
    """Read the arguments, and refuse an empty OUT_DIR, an unknown harness, a bad time and a reserved core.

    Args:
        argv: The words after `loop fuzz`
        root: The source tree
        reserved: The reserved CPUs of the host (reserved_cpus())

    Raises:
        FuzzError: If an argument is not correct
    """
    if len(argv) != 5:
        raise FuzzError("usage: loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS")
    build_text, out_text, harness, cpu_text, seconds_text = argv
    # The exit removes OUT_DIR/tmp, so an empty OUT_DIR would name /tmp.
    if not out_text:
        raise FuzzError("OUT_DIR must not be empty")
    if harness not in harness_names(root):
        raise FuzzError(f"no harness named {harness} in {HARNESS_LIST}")
    if not seconds_text.isdigit():
        raise FuzzError("SECONDS must be a whole number")
    cpus = expand_cpus(cpu_text)
    if not cpus:
        raise FuzzError("CPU_LIST names no core")
    reserved_set = frozenset(reserved)
    for cpu in cpus:
        if cpu in reserved_set:
            raise FuzzError(f"core {cpu} is a reserved CPU of the host.  Give a CPU_LIST with no reserved CPU")
    return Campaign(Path(build_text), Path(out_text), harness, cpus, int(seconds_text))


def configure_and_build(root: Path, build_dir: Path, harness: str, afl_bin: Path,
                        environment: dict[str, str]) -> None:
    """Configure an instrumented build when it is not configured, and build fuzz_HARNESS in it.

    The configure runs the cmake of PATH, and the build runs the cmake that
    configured the directory (`setup check`, utils/assay/setup/pins.py).  The
    build runs the job count of assay.core.proc.

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
        ["nice", "-n", "10", build_cmake, "--build", str(build_dir), "-j", str(proc.DEFAULT_JOBS), "--target",
         f"fuzz_{harness}"],
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


def _afl_environment() -> dict[str, str]:
    """Return the environment of this process, with the compilers of the patched GCC for the AFL++ tools."""
    c_compiler, cxx_compiler = patched_gcc.toolchain_compilers()
    return proc.environment({"AFL_CC": str(c_compiler), "AFL_CXX": str(cxx_compiler)})


def instance_run(campaign: Campaign, index: int, afl_bin: Path, binary: Path, cmplog_binary: Path,
                 scratch: Path) -> proc.Command:
    """Return the Command of one instance: its words, its limit, its log and its stop signal SIGINT.

    Args:
        campaign: The campaign
        index: The index of the instance, also of its core in the list
        afl_bin: The directory of the AFL++ tools
        binary: The instrumented harness
        cmplog_binary: The harness of the build BUILD_DIR-cmplog
        scratch: The directory of the temporary files of the instance (TMPDIR)

    Returns:
        The command
    """
    environment = dict(_afl_environment(), TMPDIR=str(scratch), **SANITIZER_OPTIONS)
    return proc.Command(instance_command(campaign, index, afl_bin, binary, cmplog_binary),
                        timeout_s=campaign.seconds + STOP_GRACE_S + INSTANCE_MARGIN_S, env=environment,
                        stdout=campaign.out_dir / f"instance-{index}.log", stderr=proc.Output.MERGE,
                        grace_s=STOP_GRACE_S, stop=proc.Stop.INTERRUPT)


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


def run_instances(campaign: Campaign, afl_bin: Path, binary: Path,
                  cmplog_binary: Path) -> tuple[list[int], Findings]:
    """Write the seeds of a built harness, run one instance on each core, and read the inputs that they saved.

    The build of the harness comes first (configure_and_build()).  The step removes OUT_DIR/tmp at its end, also
    after an error.

    Args:
        campaign: The campaign
        afl_bin: The directory of the AFL++ tools
        binary: The instrumented harness
        cmplog_binary: The harness with comparison logging

    Returns:
        The exit status of each instance, and the inputs that the instances saved (campaign_findings())

    Raises:
        proc.ToolFailed: If the harness does not write its seeds, or an instance does not start
    """
    environment = _afl_environment()
    scratch = campaign.out_dir / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        proc.require(proc.run(proc.Command([str(binary), f"--write-seeds={campaign.out_dir / 'seeds'}"],
                                           timeout_s=proc.TOOL_S, env=environment, stdout=proc.Output.INHERIT,
                                           stderr=proc.Output.INHERIT)))
        instances: list[proc.Running] = []
        try:
            for index in range(len(campaign.cpus)):
                instances.append(proc.start(instance_run(campaign, index, afl_bin, binary, cmplog_binary, scratch)))
            print(f"loop fuzz: {len(instances)} instances of {binary.name} for {campaign.seconds} "
                  f"seconds, output in {campaign.out_dir}/afl", flush=True)
            statuses = [instance_status(instance) for instance in instances]
        finally:
            # An instance that the loop did not wait for, after an error or an interrupt, stops with its group.
            for instance in instances:
                instance.stop()
        return statuses, campaign_findings(campaign.out_dir)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run_campaign(root: Path, campaign: Campaign) -> int:
    """Build the two instrumented builds, run the instances, and print the verdict of the campaign.

    Args:
        root: The source tree
        campaign: The campaign

    Returns:
        The status of campaign_verdict(): 0 when each instance ended with status 0 and the campaign saved no crash
        and no hang, else 1
    """
    afl_bin = afl_directory()
    environment = _afl_environment()
    cmplog_dir = Path(f"{campaign.build_dir}-cmplog")
    configure_and_build(root, campaign.build_dir, campaign.harness, afl_bin, environment)
    configure_and_build(root, cmplog_dir, campaign.harness, afl_bin, dict(environment, AFL_GCC_CMPLOG="1"))
    binary = harness_binary(campaign.build_dir, campaign.harness)
    statuses, findings = run_instances(campaign, afl_bin, binary, harness_binary(cmplog_dir, campaign.harness))
    status, lines = campaign_verdict(statuses, findings, binary)
    for line in lines:
        print(f"loop fuzz: {line}", flush=True)
    return status


# ── The self-test ──────────────────────────────────────────────────────────

# The input on which the planted harness of the self-test ends with SIGABRT.
PLANTED_CRASH = b"CRASH"
# A fake afl-fuzz for the self-test.  It runs the harness of its command on each seed, saves each seed whose run ends
# by a signal in crashes/ with the README.txt of afl-fuzz, and ends with the status 0, as afl-fuzz ends after it saves
# a crash.
_FAKE_AFL_FUZZ = '''
import os, shutil, subprocess, sys
words = sys.argv[1:]
seeds, out = words[words.index("-i") + 1], words[words.index("-o") + 1]
name = words[words.index("-M" if "-M" in words else "-S") + 1]
binary = words[words.index("--") + 1]
crashes = os.path.join(out, name, "crashes")
os.makedirs(crashes, exist_ok=True)
with open(os.path.join(crashes, "README.txt"), "w", encoding="utf-8") as readme:
    readme.write("the command of the campaign\\n")
for count, seed in enumerate(sorted(os.listdir(seeds))):
    path = os.path.join(seeds, seed)
    ended = subprocess.run([binary, path], check=False).returncode
    if ended < 0:
        shutil.copyfile(path, os.path.join(crashes, f"id:{count:06d},sig:{-ended:02d},src:{seed}"))
sys.exit(0)
'''
# A planted harness for the self-test.  --write-seeds=DIR writes a benign seed and the seed PLANTED_CRASH.  A run on a
# file ends with SIGABRT when the file holds PLANTED_CRASH, and with no core file.
_PLANTED_HARNESS = f'''
import os, resource, sys
argument = sys.argv[1]
if argument.startswith("--write-seeds="):
    seeds = argument.split("=", 1)[1]
    os.makedirs(seeds, exist_ok=True)
    for name, data in (("benign", b"seed"), ("known", {PLANTED_CRASH!r})):
        with open(os.path.join(seeds, name), "wb") as seed:
            seed.write(data)
    sys.exit(0)
with open(argument, "rb") as given:
    if given.read() == {PLANTED_CRASH!r}:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        os.abort()
sys.exit(0)
'''


def _write_program(path: Path, body: str) -> Path:
    """Write a Python program that runs with the interpreter of this process, and make it executable."""
    path.write_text(f"#!{sys.executable} -I\n{body}", encoding="utf-8")
    path.chmod(0o755)
    return path


def run_planted_campaign(work: Path) -> tuple[list[int], Findings, Path]:
    """Run the step of the instances on a planted harness that crashes on PLANTED_CRASH, with a fake afl-fuzz.

    The campaign has one instance on the first CPU of this process.  Complexity: one run of the harness for each of
    its two seeds.

    Args:
        work: An empty directory for the fake tools and the output of the campaign

    Returns:
        The status of each instance, the inputs that the campaign saved, and the planted harness
    """
    afl_bin = work / "afl"
    afl_bin.mkdir()
    _write_program(afl_bin / "afl-fuzz", _FAKE_AFL_FUZZ)
    binary = _write_program(work / "fuzz_planted", _PLANTED_HARNESS)
    campaign = Campaign(work / "build", work / "out", "planted", (min(os.sched_getaffinity(0)),), 1)
    statuses, findings = run_instances(campaign, afl_bin, binary, binary)
    return statuses, findings, binary


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
    real_names = harness_names(REPO_ROOT)
    expect("the harness list of the tree names the helper tensor_meta_claims as no harness",
           "tensor_meta_claims" not in real_names)
    expect("each harness of the list of the tree has its header, and the list holds protocol_image",
           bool(real_names) and "protocol_image" in real_names
           and all((REPO_ROOT / HARNESSES / f"{name}.h").is_file() for name in real_names))
    expect("each harness header of the tree that defines its run function has its line in the list",
           unlisted_harnesses(REPO_ROOT) == ())
    with tempfile.TemporaryDirectory(prefix="loop-fuzz-") as work:
        root = Path(work)
        (root / HARNESSES).mkdir(parents=True)
        (root / HARNESSES / "region.h").write_text("inline void run_region(int bytes) noexcept {}\n", encoding="utf-8")
        (root / HARNESSES / "protocol_image.h").write_text("", encoding="utf-8")
        (root / HARNESSES / "tensor_meta_claims.h").write_text("inline void run_region_claims(int) {}\n",
                                                                 encoding="utf-8")
        (root / HARNESSES / "unbuilt.h").write_text("inline void run_unbuilt(int bytes) noexcept {}\n",
                                                    encoding="utf-8")
        (root / HARNESS_LIST).write_text(
            "function(crucible_boundary_fuzzer name mutations)\nendfunction()\n"
            "crucible_boundary_fuzzer(region 2000)\n# crucible_boundary_fuzzer(commented 1)\n"
            "crucible_boundary_fuzzer(protocol_image 20000 VIEW)\n", encoding="utf-8")
        expect("the harnesses are the names of the crucible_boundary_fuzzer lines, in the order of the list",
               harness_names(root) == ("region", "protocol_image"))
        expect("a header that defines its run function and has no line is an error of the list, and a helper is not",
               unlisted_harnesses(root) == ("unbuilt",))
        expect("a tree with no list has no harness", harness_names(root / "absent") == ())
        expect("a tree with no harness directory has no unlisted harness", unlisted_harnesses(root / "absent") == ())
        cpu_root, cgroup_root = root / "cpu", root / "cgroup"
        cpu_root.mkdir()
        expect("a host with no reservation reserves no core", reserved_cpus(cpu_root, cgroup_root) == ())
        (cgroup_root / "reserved.slice").mkdir(parents=True)
        (cgroup_root / "reserved.slice" / "cpuset.cpus.partition").write_text("root\n", encoding="ascii")
        (cgroup_root / "reserved.slice" / "cpuset.cpus.exclusive.effective").write_text("224-255\n", encoding="ascii")
        reserved = reserved_cpus(cpu_root, cgroup_root)
        expect("the reserved CPUs are the CPUs of the partition that loop bench reads",
               reserved == tuple(range(224, 256)))
        campaign = campaign_of(["build", "out", "region", "100-101", "60"], root, reserved)
        expect("the arguments of a campaign are read",
               campaign == Campaign(Path("build"), Path("out"), "region", (100, 101), 60))
        refusals = {"an empty OUT_DIR": ["build", "", "region", "100", "60"],
                    "an unknown harness": ["build", "out", "absent", "100", "60"],
                    "a helper header that the list does not name": ["build", "out", "tensor_meta_claims", "100", "60"],
                    "a time that is not a whole number": ["build", "out", "region", "100", "1.5"],
                    "a core of the reserved partition": ["build", "out", "region", "220-224", "60"],
                    "a wrong count of arguments": ["build", "out", "region", "100"]}
        for name, argv in refusals.items():
            try:
                campaign_of(argv, root, reserved)
                expect(f"{name} is refused", False)
            except FuzzError:
                expect(f"{name} is refused", True)
        # A fake campaign whose main instance saved one crash and whose secondary instance saved one hang, each
        # instance with the status 0, as afl-fuzz ends after it saves a crash.
        out = root / "out"
        (out / "afl" / "main" / "crashes").mkdir(parents=True)
        (out / "afl" / "main" / "crashes" / AFL_README).write_text("readme\n", encoding="utf-8")
        crash = out / "afl" / "main" / "crashes" / "id:000000,sig:06"
        crash.write_bytes(b"\x00crash")
        (out / "afl" / "secondary1" / "hangs").mkdir(parents=True)
        hang = out / "afl" / "secondary1" / "hangs" / "id:000000"
        hang.write_bytes(b"hang")
        findings = campaign_findings(out)
        expect("the findings are each file of crashes and hangs, and the README.txt of afl-fuzz is no input",
               findings == Findings((crash,), (hang,)))
        binary = harness_binary(Path("build"), "region")
        status, lines = campaign_verdict([0, 0], findings, binary)
        expect("a campaign whose instances end with 0 after a saved crash fails",
               status == 1 and len(lines) == 2 and "1 crash input(s) and 1 hang input(s)" in lines[0])
        expect("the report names the first input and the command that replays it",
               lines[1] == f"replay the first input with: build/test/fuzz/fuzz_region {crash}")
        expect("a campaign with no output saved no input", campaign_findings(root / "none") == Findings((), ()))
        expect("a campaign with no saved input and each instance at 0 passes",
               campaign_verdict([0, 0], Findings((), ()), binary) == (0, ()))
        expect("a campaign with an instance that failed fails",
               campaign_verdict([0, 1], Findings((), ()), binary)[0] == 1)
        afl = Path("/afl")
        main_command = instance_command(campaign, 0, afl, Path("b"), Path("c"))
        secondary = instance_command(campaign, 1, afl, Path("b"), Path("c"))
        expect("the first instance is the main one with the comparison log, on the first core",
               main_command[:6] == ["taskset", "-c", "100", "timeout", "--signal=INT", "120"]
               and main_command[-6:] == ["-M", "main", "-c", "c", "--", "b"])
        expect("each other instance is a secondary one, on its core",
               secondary[2] == "101" and secondary[-4:] == ["-S", "secondary1", "--", "b"])
        run = instance_run(campaign, 1, afl, Path("b"), Path("c"), Path("scratch"))
        expect("an instance stops with SIGINT, and gets SIGKILL only STOP_GRACE_S after it",
               run.stop == proc.Stop.INTERRUPT and run.grace_s == STOP_GRACE_S
               and run.timeout_s == campaign.seconds + STOP_GRACE_S + INSTANCE_MARGIN_S)
        expect("an instance has its TMPDIR and the compilers of the patched GCC",
               run.env is not None and run.env.get("TMPDIR") == "scratch"
               and run.env.get("AFL_CXX") == str(patched_gcc.toolchain_compilers()[1]))
        saved = {name: os.environ.get(name) for name in ("CRUCIBLE_CXX", "CRUCIBLE_GCC16_PREFIX")}
        try:
            os.environ["CRUCIBLE_CXX"] = "/opt/gcc/bin/g++-16p"
            os.environ["CRUCIBLE_GCC16_PREFIX"] = "/elsewhere"
            named = patched_gcc.toolchain_compilers()
            del os.environ["CRUCIBLE_CXX"]
            prefixed = patched_gcc.toolchain_compilers()
        finally:
            for name, value in saved.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value
        expect("the compiler wrappers call the compiler of the toolchain file: CRUCIBLE_CXX first, then the prefix",
               named == (Path("/opt/gcc/bin/gcc-16p"), Path("/opt/gcc/bin/g++-16p"))
               and prefixed == (Path("/elsewhere/usr/bin/gcc-16p"), Path("/elsewhere/usr/bin/g++-16p")))
    # The step of the instances, on a planted harness that crashes on a known seed and a fake afl-fuzz that ends with
    # the status 0 after it saves the crash.
    with tempfile.TemporaryDirectory(prefix="loop-fuzz-planted-") as work:
        try:
            statuses, findings, binary = run_planted_campaign(Path(work))
        except proc.ToolFailed as problem:
            print(f"  the planted campaign did not run: {problem}")
            statuses, findings, binary = [], Findings((), ()), Path(work) / "fuzz_planted"
        expect("each instance of the planted campaign ends with 0, so a verdict of the statuses alone is green",
               statuses == [0])
        saved_input = findings.first()
        expect("the planted campaign saves the known input as its one crash",
               saved_input is not None and findings.crashes == (saved_input,) and findings.hangs == ()
               and saved_input.read_bytes() == PLANTED_CRASH)
        status, lines = campaign_verdict(statuses, findings, binary)
        expect("the planted campaign fails, and its report names the command that replays the saved input",
               status == 1 and saved_input is not None
               and lines[-1] == f"replay the first input with: {replay_command(binary, saved_input)}")
        replayed = None if saved_input is None else proc.run(
            proc.Command([str(binary), str(saved_input)], timeout_s=proc.TOOL_S))
        expect("the replay command ends with SIGABRT again", replayed is not None and replayed.exit == -signal.SIGABRT)
        expect("the step of the instances removes its scratch directory", not (Path(work) / "out" / "tmp").exists())

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
        campaign = campaign_of(argv, REPO_ROOT, reserved_cpus())
    except (FuzzError, bench.BenchError) as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 2
    try:
        return run_campaign(REPO_ROOT, campaign)
    except (proc.ToolFailed, cmake_pin.PinError, OSError) as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 1
