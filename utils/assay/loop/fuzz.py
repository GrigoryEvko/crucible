"""fuzz — run one coverage-guided AFL++ campaign on one boundary harness (`loop fuzz`).

Arguments
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS

    BUILD_DIR  The instrumented build.  When BUILD_DIR is not configured, the
               command configures it with afl-g++-fast.  It also keeps a
               second build, BUILD_DIR-cmplog, with comparison logging, for
               the main instance.  It builds fuzz_HARNESS in the two builds,
               with the job count of assay.core.proc (DEFAULT_JOBS).
    OUT_DIR    The output directory of the campaign.  afl-fuzz writes into
               OUT_DIR/afl, the seeds go to OUT_DIR/seeds, and the scratch
               files of the harness go to OUT_DIR/tmp.  A harness that
               afl-fuzz stops leaves its scratch directory there, so the
               command removes OUT_DIR/tmp when it exits, also when a step
               fails.  OUT_DIR/afl of an earlier campaign goes into the kept
               state before the instances start (the kept corpus).
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
               `loop bench` too (the reserved CPUs of that module).
    SECONDS    The time limit of each instance.

Environment
    CRUCIBLE_BENCH_SLICE         The slice of the reserved partition when the host has more than one
    AFL_BIN                      The directory of the AFL++ tools.  The default is the bin directory of the install
                                 of the pin aflplusplus for the patched GCC (`setup install aflplusplus`)
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

Findings
    afl-fuzz ends with the status 0 also when it saved a crash or a hang, so
    the status of the instances is not the verdict of a campaign.  After the
    instances end, the command counts the files of OUT_DIR/afl/*/crashes and
    OUT_DIR/afl/*/hangs (campaign_findings()).  The README.txt that afl-fuzz
    writes in a crashes directory is no input.  A count above zero fails the
    campaign, and the report names the first input and the command that
    replays it: the harness binary with the input as its argument.

Kept corpus
    afl-fuzz refuses an output directory that holds a session of more than
    25 minutes (OUTPUT_GRACE of AFL++).  After a shorter session, it deletes
    the crashes and the hangs of that session.  So each campaign starts with
    no output of an earlier campaign (harvested()):
      * Each saved crash and hang of OUT_DIR/afl moves into the directory
        FINDINGS of the kept state of the harness.  Its name holds its kind,
        the first digits of its SHA-256, its instance and its own name.  The
        report of the earlier campaign named it, and no step deletes it.
      * Each input of the queue of each instance goes into the corpus of
        the kept state, with the digits of its SHA-256 as its name, so the
        name of an input never changes.  afl-cmin then keeps the inputs that
        give the coverage of the corpus (minimized()).  The corpus holds each
        input before afl-cmin starts, so an afl-cmin that cannot start, runs
        past its limit or fails loses no input, and the campaign fails.
      * OUT_DIR/afl goes.
    The seeds of a campaign are the seeds that the harness writes and each
    input of the corpus.  The kept state of a harness is the directory
    KEPT_ROOT/HARNESS of the work root of assay.core.cache, which each work
    tree shares.  A cache of that module has a size limit and an eviction,
    but the work root has neither, so no step removes a kept input.

Usage
    loop fuzz BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS
    guard selftest loop.fuzz

Exit 0 when each instance ended with status 0 and the campaign saved no crash and no hang, 1 when an instance or a
step failed or the campaign saved a crash or a hang, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import signal
import sys
import tempfile
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cache, proc
from assay.core.paths import REPO_ROOT
from assay.loop import bench
from assay.readers import cmake as cmake_reader
from assay.setup import gcc as patched_gcc
from assay.setup import install as setup_install
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
# The pin and the tool of `setup install` that give the AFL++ tools (the environment).
AFL_TOOL = "aflplusplus"
# The entry of the CMake cache that names the compiler of an instrumented build (configure_and_build()).
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
# The directory of the kept state of the campaigns in the work root, the two directories of the state of a harness, the
# directory of a new corpus that afl-cmin writes and the directory of the corpus during the swap (minimized()), the
# prefix of a seed from the corpus, and the digits of the SHA-256 in the name of a kept input (the kept corpus).
KEPT_ROOT = "fuzz-campaigns"
CORPUS = "corpus"
FINDINGS = "findings"
NEXT_CORPUS = ".corpus-next"
EARLIER_CORPUS = ".corpus-earlier"
KEPT_SEED_PREFIX = "kept-"
CONTENT_DIGITS = 32
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
    # A seed of the kept corpus that crashes the harness of this time is a saved crash of the campaign (the kept
    # corpus), not a seed that afl-fuzz skips.
    "AFL_CRASHING_SEEDS_AS_NEW_CRASH": "1",
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
    """Return the inputs that the instances of a campaign saved (the findings).

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
    """Return the exit status of a campaign and the lines of its report (the findings).

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
    """Return the directory of the AFL++ tools: AFL_BIN, else the install of the pin aflplusplus (the environment).

    Raises:
        setup_install.InstallError: If AFL_BIN is not set, and the pinned AFL++ is not installed for the patched GCC
    """
    named = os.environ.get("AFL_BIN", "")
    return Path(named) if named else setup_install.program(AFL_TOOL)


def kept_directory(harness: str) -> Path:
    """Return the kept state of a harness: KEPT_ROOT/HARNESS in the work root (the kept corpus).

    The directory can be missing.
    """
    return cache.work_root() / KEPT_ROOT / harness


def content_name(path: Path) -> str:
    """Return the first CONTENT_DIGITS digits of the SHA-256 of the bytes of a file (the kept corpus).

    Two inputs with the same bytes get the same name, so the name of an input of the corpus never changes.
    Complexity: linear in the bytes of the file.
    """
    return hashlib.sha256(path.read_bytes()).hexdigest()[:CONTENT_DIGITS]


def finding_name(path: Path) -> str:
    """Return the name of a saved input in the kept findings: KIND-DIGEST-INSTANCE-NAME (the kept corpus).

    KIND is crashes or hangs and INSTANCE the instance of afl-fuzz, from the path OUT_DIR/afl/INSTANCE/KIND/NAME.  Two
    inputs with the same bytes get the same name.  Complexity: linear in the bytes of the input.
    """
    return f"{path.parent.name}-{content_name(path)}-{path.parent.parent.name}-{path.name}"


def harvested(out_dir: Path, kept: Path, staging: Path) -> tuple[Path, ...]:
    """Move the output of an earlier campaign out of OUT_DIR/afl, remove OUT_DIR/afl, and return the moved inputs.

    Each saved crash and hang goes into kept/FINDINGS, and each input of the queue of each instance is copied into
    staging with the digits of its SHA-256 as its name (the kept corpus).  An OUT_DIR with no afl directory gives
    nothing.  Complexity: linear in the bytes of the files of OUT_DIR/afl.

    Args:
        out_dir: The output directory of the campaign
        kept: The kept state of the harness (kept_directory())
        staging: An empty directory for the inputs of the queues
    """
    afl = out_dir / "afl"
    if not afl.is_dir():
        return ()
    findings = campaign_findings(out_dir)
    saved = (*findings.crashes, *findings.hangs)
    moved: list[Path] = []
    if saved:
        (kept / FINDINGS).mkdir(parents=True, exist_ok=True)
    for path in saved:
        target = kept / FINDINGS / finding_name(path)
        shutil.move(path, target)
        moved.append(target)
    for path in sorted(afl.glob("*/queue/id:*")):
        if path.is_file():
            shutil.copyfile(path, staging / content_name(path))
    shutil.rmtree(afl)
    return tuple(moved)


def minimized(kept: Path, staging: Path, afl_bin: Path, binary: Path, cpus: int) -> int:
    """Give the corpus of the kept state the inputs of staging, minimize it with afl-cmin, and return its inputs.

    Each input of staging goes into the corpus first, so the corpus holds each input before afl-cmin starts, also
    when afl-cmin cannot start, runs past its limit or fails.  Each input has the digits of its SHA-256 as its name,
    so an input of the corpus and the same input of a queue give one file, and afl-cmin keeps the name.  The output
    of afl-cmin replaces the corpus only after a run with the status 0.  A staging with no input changes nothing
    (the kept corpus).  Complexity: one run of afl-cmin on the inputs.

    Args:
        kept: The kept state of the harness
        staging: The inputs of the queues of the earlier campaign (harvested())
        afl_bin: The directory of the AFL++ tools
        binary: The instrumented harness
        cpus: The parallel runs of afl-cmin

    Raises:
        proc.ToolFailed: If afl-cmin does not give a corpus.  The corpus then holds each input of staging
    """
    corpus, following, earlier = kept / CORPUS, kept / NEXT_CORPUS, kept / EARLIER_CORPUS
    # A step that a kill stopped between the two renames of the swap below left the corpus as EARLIER_CORPUS.
    if earlier.is_dir() and not corpus.exists():
        earlier.rename(corpus)
    shutil.rmtree(earlier, ignore_errors=True)
    corpus.mkdir(parents=True, exist_ok=True)
    if not any(staging.iterdir()):
        return sum(1 for _ in corpus.iterdir())
    for path in staging.iterdir():
        shutil.copyfile(path, corpus / path.name)
    shutil.rmtree(following, ignore_errors=True)
    environment = dict(_afl_environment(), AFL_PATH=str(afl_bin), **SANITIZER_OPTIONS)
    limits = ("-m", "none", "-t", "2000", "-T", str(max(1, cpus)))
    command = [str(afl_bin / "afl-cmin"), "-i", str(corpus), "-o", str(following), *limits, "--", str(binary)]
    completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S, env=environment, stderr=proc.Output.MERGE))
    if completed.exit != 0 or not following.is_dir():
        shutil.rmtree(following, ignore_errors=True)
        tail = " ".join(line.strip() for line in completed.out.strip().splitlines()[-3:])
        message = f"afl-cmin of {binary.name} exits with {completed.exit}, and {corpus} keeps each input: {tail}"
        raise proc.ToolFailed(message, completed.command, completed)
    corpus.rename(earlier)
    following.rename(corpus)
    shutil.rmtree(earlier)
    return sum(1 for _ in corpus.iterdir())


def kept_seeds(kept: Path, seeds: Path) -> int:
    """Copy each input of the corpus of the kept state into the seeds, with the prefix KEPT_SEED_PREFIX, and count them.

    Complexity: linear in the bytes of the corpus.
    """
    corpus = kept / CORPUS
    if not corpus.is_dir():
        return 0
    count = 0
    for path in sorted(corpus.iterdir()):
        if path.is_file():
            shutil.copyfile(path, seeds / f"{KEPT_SEED_PREFIX}{path.name}")
            count += 1
    return count


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


def configured_compiler(build_dir: Path) -> str | None:
    """Return the value of CMAKE_CXX_COMPILER in the CMake cache of a build, or None when the cache gives none.

    Complexity: linear in the lines of the cache.
    """
    try:
        lines = cmake_reader.cache_file(build_dir).read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    return next((line.partition("=")[2] for line in lines if line.startswith(f"{COMPILER_ENTRY}:")), None)


def configure_and_build(root: Path, build_dir: Path, harness: str, afl_bin: Path,
                        environment: dict[str, str]) -> None:
    """Configure an instrumented build when it is not configured for afl-g++-fast of afl_bin, and build fuzz_HARNESS.

    A build whose cache names a different compiler, such as the afl-g++-fast of an install for an earlier patched
    GCC, is configured again with --fresh.  The configure runs the cmake of PATH, and the build runs the cmake that
    configured the directory (`setup check`, utils/assay/setup/pins.py).  The build runs the job count of
    assay.core.proc.

    Raises:
        proc.ToolFailed: If the configure or the build cannot start, runs past its limit or fails
        cmake_pin.PinError: If the build directory names no cmake
    """
    compiler = afl_bin / "afl-g++-fast"
    is_current = cmake_reader.is_configured(build_dir) and configured_compiler(build_dir) == str(compiler)
    if not is_current:
        fresh = ["--fresh"] if cmake_reader.is_configured(build_dir) else []
        proc.require(proc.run(proc.Command(
            ["cmake", *fresh, "-S", str(root), "-B", str(build_dir), "-G", "Ninja",
             f"-DCMAKE_TOOLCHAIN_FILE={root / 'cmake' / 'Toolchain-gcc16.cmake'}",
             f"-DCMAKE_C_COMPILER={afl_bin / 'afl-gcc-fast'}", f"-D{COMPILER_ENTRY}={compiler}",
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


def run_instances(campaign: Campaign, afl_bin: Path, binary: Path, cmplog_binary: Path,
                  kept: Path) -> tuple[list[int], Findings]:
    """Keep the output of an earlier campaign, write the seeds, run one instance on each core, and read the findings.

    The build of the harness comes first (configure_and_build()).  The kept corpus gives the steps before the
    instances.  The step removes OUT_DIR/tmp at its end, also after an error.

    Args:
        campaign: The campaign
        afl_bin: The directory of the AFL++ tools
        binary: The instrumented harness
        cmplog_binary: The harness with comparison logging
        kept: The kept state of the harness (kept_directory())

    Returns:
        The exit status of each instance, and the inputs that the instances saved (campaign_findings())

    Raises:
        proc.ToolFailed: If afl-cmin fails, the harness does not write its seeds, or an instance does not start
    """
    environment = _afl_environment()
    scratch = campaign.out_dir / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        # A campaign that a kill stopped leaves its copy of the queues here, and the corpus takes it too.
        staging = scratch / "harvest"
        staging.mkdir(exist_ok=True)
        for path in harvested(campaign.out_dir, kept, staging):
            print(f"loop fuzz: an input that an earlier campaign saved is kept at {path}", flush=True)
        inputs = minimized(kept, staging, afl_bin, binary, len(campaign.cpus))
        seeds = campaign.out_dir / "seeds"
        shutil.rmtree(seeds, ignore_errors=True)
        proc.require(proc.run(proc.Command([str(binary), f"--write-seeds={seeds}"],
                                           timeout_s=proc.TOOL_S, env=environment, stdout=proc.Output.INHERIT,
                                           stderr=proc.Output.INHERIT)))
        print(f"loop fuzz: the corpus {kept / CORPUS} gives {kept_seeds(kept, seeds)} of its {inputs} inputs as seeds",
              flush=True)
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
    kept = kept_directory(campaign.harness)
    statuses, findings = run_instances(campaign, afl_bin, binary, harness_binary(cmplog_dir, campaign.harness), kept)
    status, lines = campaign_verdict(statuses, findings, binary)
    for line in lines:
        print(f"loop fuzz: {line}", flush=True)
    return status


# ── The self-test ──────────────────────────────────────────────────────────

# The input on which the planted harness of the self-test ends with SIGABRT.
PLANTED_CRASH = b"CRASH"
# A fake afl-fuzz for the self-test.  It refuses an output of its instance from an earlier campaign, as afl-fuzz does
# after a session of more than 25 minutes.  It runs the harness of its command on each seed, saves each seed whose run
# ends by a signal in crashes/ with the README.txt of afl-fuzz, and puts each other seed into queue/.  It ends with the
# status 0, as afl-fuzz ends after it saves a crash.
_FAKE_AFL_FUZZ = '''
import os, shutil, subprocess, sys
words = sys.argv[1:]
seeds, out = words[words.index("-i") + 1], words[words.index("-o") + 1]
name = words[words.index("-M" if "-M" in words else "-S") + 1]
binary = words[words.index("--") + 1]
if os.path.exists(os.path.join(out, name)):
    print(f"At-risk data found in {out}")
    sys.exit(1)
crashes, queue = os.path.join(out, name, "crashes"), os.path.join(out, name, "queue")
os.makedirs(crashes)
os.makedirs(queue)
with open(os.path.join(crashes, "README.txt"), "w", encoding="utf-8") as readme:
    readme.write("the command of the campaign\\n")
for count, seed in enumerate(sorted(os.listdir(seeds))):
    path = os.path.join(seeds, seed)
    ended = subprocess.run([binary, path], check=False).returncode
    if ended < 0:
        shutil.copyfile(path, os.path.join(crashes, f"id:{count:06d},sig:{-ended:02d},src:{seed}"))
    else:
        shutil.copyfile(path, os.path.join(queue, f"id:{count:06d},orig:{seed}"))
sys.exit(0)
'''
# A fake afl-cmin for the self-test.  It copies each input of -i with bytes that no earlier input has into -o, which
# must not exist, as afl-cmin keeps one input for each coverage.
_FAKE_AFL_CMIN = '''
import os, shutil, sys
words = sys.argv[1:]
source, target = words[words.index("-i") + 1], words[words.index("-o") + 1]
os.makedirs(target)
seen = set()
for name in sorted(os.listdir(source)):
    with open(os.path.join(source, name), "rb") as given:
        data = given.read()
    if data not in seen:
        seen.add(data)
        shutil.copyfile(os.path.join(source, name), os.path.join(target, name))
sys.exit(0)
'''
# A fake afl-cmin for the self-test that fails.
_FAILED_AFL_CMIN = '''
import sys
print("[-] Error: no instrumentation output detected")
sys.exit(1)
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


def planted_tools(work: Path) -> tuple[Path, Path]:
    """Write the fake afl-fuzz, the fake afl-cmin and the planted harness into a directory, and return the tools and
    the harness."""
    afl_bin = work / "afl"
    afl_bin.mkdir(exist_ok=True)
    _write_program(afl_bin / "afl-fuzz", _FAKE_AFL_FUZZ)
    _write_program(afl_bin / "afl-cmin", _FAKE_AFL_CMIN)
    return afl_bin, _write_program(work / "fuzz_planted", _PLANTED_HARNESS)


def run_planted_campaign(work: Path) -> tuple[list[int], Findings, Path]:
    """Run the step of the instances on a planted harness that crashes on PLANTED_CRASH, with a fake afl-fuzz.

    The campaign has one instance on the first CPU of this process, its output in work/out and its kept state in
    work/kept.  A second call on the same directory is the next campaign.  Complexity: one run of the harness for each
    seed.

    Args:
        work: A directory for the fake tools, the output of the campaign and the kept state

    Returns:
        The status of each instance, the inputs that the campaign saved, and the planted harness
    """
    afl_bin, binary = planted_tools(work)
    campaign = Campaign(work / "build", work / "out", "planted", (min(os.sched_getaffinity(0)),), 1)
    statuses, findings = run_instances(campaign, afl_bin, binary, binary, work / "kept")
    return statuses, findings, binary


def leftover_cases(work: Path) -> bool:
    """Plant the copy of the queues that a killed campaign leaves in OUT_DIR/tmp, and run the next planted campaign.

    Returns:
        True when the campaign starts and the corpus takes the planted input
    """
    left = b"left by a killed campaign"
    staging = work / "out" / "tmp" / "harvest"
    staging.mkdir(parents=True, exist_ok=True)
    (staging / hashlib.sha256(left).hexdigest()[:CONTENT_DIGITS]).write_bytes(left)
    try:
        statuses, _, _ = run_planted_campaign(work)
    except proc.ToolFailed as problem:
        print(f"  the planted campaign after a kill did not run: {problem}")
        return False
    return statuses == [0] and any(path.read_bytes() == left for path in (work / "kept" / CORPUS).iterdir())


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
        configured = root / "configured"
        configured.mkdir()
        cmake_reader.cache_file(configured).write_text(
            "# a planted cache\nCMAKE_C_COMPILER:FILEPATH=/old/afl-gcc-fast\nCMAKE_CXX_COMPILER:STRING=/old/afl-g++-fast\n",
            encoding="utf-8")
        expect("the compiler of a configured build is its CMAKE_CXX_COMPILER, and a build with no cache gives none",
               configured_compiler(configured) == "/old/afl-g++-fast" and configured_compiler(root / "none") is None)
        expect("the kept state of a harness is in the work root, which has no eviction",
               kept_directory("region") == cache.work_root() / KEPT_ROOT / "region")
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
        # The next campaign on the same output directory.  afl-fuzz refuses the output of a long earlier session, and
        # deletes the saved inputs of a short one, so the step keeps them and starts with no earlier output.
        crash_bytes = None if saved_input is None else saved_input.read_bytes()
        try:
            again, second, _ = run_planted_campaign(Path(work))
        except proc.ToolFailed as problem:
            print(f"  the second planted campaign did not run: {problem}")
            again, second = [], Findings((), ())
        kept = Path(work) / "kept"
        kept_crashes = sorted((kept / FINDINGS).glob("crashes-*-main-id:*")) if (kept / FINDINGS).is_dir() else []
        expect(f"the next campaign on the same output directory starts, and saves the known input again: {again}",
               again == [0] and len(second.crashes) == 1)
        expect("the crash of the earlier campaign moved into the kept findings with its bytes, so no step deletes it",
               len(kept_crashes) == 1 and kept_crashes[0].read_bytes() == crash_bytes)
        corpus = sorted((kept / CORPUS).iterdir()) if (kept / CORPUS).is_dir() else []
        seeds = sorted(path.name for path in (Path(work) / "out" / "seeds").iterdir())
        expect(f"the queue of the earlier campaign went into the corpus, and the corpus gives the next seeds: {seeds}",
               [path.read_bytes() for path in corpus] == [b"seed"]
               and any(name.startswith(KEPT_SEED_PREFIX) for name in seeds))
        try:
            run_planted_campaign(Path(work))
        except proc.ToolFailed as problem:
            print(f"  the third planted campaign did not run: {problem}")
        names = sorted(path.name for path in (kept / CORPUS).iterdir()) if (kept / CORPUS).is_dir() else []
        expect(f"an input of the corpus keeps its name, the digits of its SHA-256, over the next campaigns: {names}",
               bool(corpus) and names == [path.name for path in corpus] and names == [content_name(corpus[0])])
        expect("a campaign that a kill stopped left its copy of the queues, and the next campaign starts",
               leftover_cases(Path(work)))
    with tempfile.TemporaryDirectory(prefix="loop-fuzz-cmin-") as work:
        failed_bin, staging, kept = Path(work) / "afl", Path(work) / "staging", Path(work) / "kept"
        failed_bin.mkdir()
        staging.mkdir()
        _write_program(failed_bin / "afl-cmin", _FAILED_AFL_CMIN)
        (staging / "main-id:000000").write_bytes(b"queued")
        (kept / CORPUS).mkdir(parents=True)
        (kept / CORPUS / "older").write_bytes(b"older")
        try:
            minimized(kept, staging, failed_bin, Path(work) / "fuzz_planted", 1)
            is_refused = False
        except proc.ToolFailed:
            is_refused = True
        names = sorted(path.name for path in (kept / CORPUS).iterdir())
        expect(f"a failure of afl-cmin fails the campaign, and the corpus keeps each input: {names}",
               is_refused and names == ["main-id:000000", "older"] and not (kept / NEXT_CORPUS).exists())
        (failed_bin / "afl-cmin").unlink()
        (staging / "main-id:000001").write_bytes(b"queued again")
        try:
            minimized(kept, staging, failed_bin, Path(work) / "fuzz_planted", 1)
            is_refused = False
        except proc.ToolFailed:
            is_refused = True
        names = sorted(path.name for path in (kept / CORPUS).iterdir())
        expect(f"an afl-cmin that cannot start fails the campaign, and the corpus keeps each input: {names}",
               is_refused and names == ["main-id:000000", "main-id:000001", "older"])

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
    except (proc.ToolFailed, cmake_pin.PinError, setup_install.InstallError, OSError) as problem:
        print(f"loop fuzz: {problem}", file=sys.stderr)
        return 1
