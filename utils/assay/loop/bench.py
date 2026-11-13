"""bench — build the benches and run each one on an isolated core, with the capabilities of BPF (`loop bench`).

THE STEPS FOR EACH BENCH
    1. The command builds the bench in the build directory of the bench preset,
       with the job count of assay.core.proc (DEFAULT_JOBS).  The build is
       incremental.  A bench that is current builds nothing.  --no-build
       omits the step.
    2. It runs the bench through the launcher of assay.loop.caps, when the
       build loads BPF (the cache entry BPF_ENTRY) and the launcher is ready
       (THE CAPABILITIES).
    3. It runs the bench with taskset on one core of the reserved CPUs of the
       host (THE RESERVED CPUS).  When the host reserves no CPU, it uses the
       core of CRUCIBLE_BENCH_CORE, or no pin when that variable is not set.
       It then warns that the results are noisy.

THE CAPABILITIES
    The launcher gives each bench three capabilities.  With them, the eBPF
    lines of the bench harness (SenseHub and SchedSwitch) attach.  A host
    with perf_event_paranoid 2 and unprivileged_bpf_disabled 2 attaches
    them only with the capabilities.
      cap_bpf              bpf_prog_load of a tracepoint
      cap_perfmon          perf_event_open on a tracepoint ID
      cap_dac_read_search  a read of /sys/kernel/tracing/events/*/id and of the BTF, which only root can read
    The command looks at the build and at the launcher one time for each
    run.  When the launcher is not ready, it prints the reason and the fix,
    and it runs each bench with no capabilities.  The harness then omits
    the eBPF lines, and the times and the percentiles do not change.  The
    launcher comes after the words of the placement, because the sudo of a
    partition clears the ambient set of the capabilities.

    The command does not change the clocks of a GPU or of a CPU.  The host
    sets the clocks, the reservation and the interrupt routing at boot, and the
    command only uses them.

THE RESERVED CPUS
    The host reserves CPUs for dedicated work in one of two ways.
    read_reserved() reads the first one that the host gives:
      1. /sys/devices/system/cpu/isolated, which isolcpus=domain fills.  A
         process can run on these CPUs with taskset alone.
      2. One cgroup cpuset partition: a cgroup below the root with the value
         "root" or "isolated" in its cpuset.cpus.partition.  A partition that
         the kernel refuses does not count.  A process of a different cgroup
         cannot run on the CPUs of the partition.  The command then starts
         each bench in the systemd slice of the partition, with `sudo -n
         systemd-run --scope`, as the calling user.  When the host has two
         partitions, CRUCIBLE_BENCH_SLICE must name one of them.
    The core of the runs is the lowest CPU of the first SMT group that holds
    only reserved CPUs.  The SMT sibling of the core then stays idle.

THE ENVIRONMENT
    CRUCIBLE_BENCH_BUILD   The build directory of the bench preset, relative to the tree.  The default is build-bench
    CRUCIBLE_BENCH_PRESET  The preset that configures it.  The default is bench
    CRUCIBLE_BENCH_CORE    The core when the host reserves no CPU.  Not set: the bench runs with no pin
    CRUCIBLE_BENCH_SLICE   The slice of the partition when the host has more than one

Usage
    loop bench [--no-build] [TARGET]... [-- ARG...]   build and run each TARGET, or the default set
    loop bench --list                                  print the bench targets of the build directory
    guard selftest loop.bench

    Each ARG after -- goes to each bench.  The default set is the hot-path
    latency set: bench_trace_ring, bench_arena, bench_swiss_table,
    bench_philox and bench_pool_allocator.

Exit 0 when each bench passed, or the exit status of the first bench that failed.  Exit 1 when a step failed, and 2 on
a usage error or a failed self-test.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.loop import caps
from assay.readers import cmake as cmake_reader
from assay.setup import pins as cmake_pin

DEFAULT_BUILD = "build-bench"
DEFAULT_PRESET = "bench"
# The cache entry that tells if the build loads BPF programs (THE CAPABILITIES).  cmake/CrucibleBpf.cmake writes it.
BPF_ENTRY = "CRUCIBLE_HAVE_BPF"
# The hot-path latency set.  bench_dispatch and bench_meta_log are not in it, because their builds drift.
DEFAULT_TARGETS = ("bench_trace_ring", "bench_arena", "bench_swiss_table", "bench_philox", "bench_pool_allocator")
# The CPU directory of sysfs and the root of the cgroup v2 hierarchy (THE RESERVED CPUS).
CPU_ROOT = Path("/sys/devices/system/cpu")
CGROUP_ROOT = Path("/sys/fs/cgroup")
# The values of cpuset.cpus.partition of a valid partition.  The kernel writes "root invalid (REASON)" for a partition
# that it refuses.
PARTITION_TYPES = frozenset({"root", "isolated"})
# The program that starts a command in a slice, by its full path.  sudo can give the command a different PATH.
SYSTEMD_RUN = "/usr/bin/systemd-run"
# The type and the FOLDER property of a bench target in the code model of CMake.  The function crucible_bench() of
# bench/CMakeLists.txt sets the folder.
TARGET_TYPE = "EXECUTABLE"
BENCH_FOLDER = "crucible_bench"


class BenchError(Exception):
    """A step stops the command: a build directory that is not configured, a failed build or a missing binary."""


@dataclass(frozen=True, slots=True)
class Request:
    """The command line: whether to build, whether to list, the targets and the arguments of each bench."""

    should_build: bool
    should_list: bool
    targets: tuple[str, ...]
    bench_args: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Settings:
    """The values of THE ENVIRONMENT: the build directory relative to the tree, the preset, the core and the slice.

    An empty core or slice is a variable that is not set.
    """

    build: str
    preset: str
    core: str = ""
    slice_name: str = ""

    @classmethod
    def from_environment(cls) -> Settings:
        """Read the settings from the environment, with the default of each variable that is not set."""
        return cls(os.environ.get("CRUCIBLE_BENCH_BUILD", DEFAULT_BUILD),
                   os.environ.get("CRUCIBLE_BENCH_PRESET", DEFAULT_PRESET),
                   os.environ.get("CRUCIBLE_BENCH_CORE", ""),
                   os.environ.get("CRUCIBLE_BENCH_SLICE", ""))


@dataclass(frozen=True, slots=True)
class Reserved:
    """The CPUs that the host reserves, the source of the reservation and the slice of a partition (THE RESERVED CPUS).

    source is "isolated", "partition" or "" when the host reserves no CPU.  slice_name is empty unless the source is a
    partition.
    """

    cpus: tuple[int, ...] = ()
    source: str = ""
    slice_name: str = ""


@dataclass(frozen=True, slots=True)
class Placement:
    """Where each bench runs: the core of the runs, the reservation that holds it, and the words before the program.

    core is empty when the bench runs with no pin.  prefix holds the words that start the program on the core.
    """

    core: str
    reserved: Reserved
    prefix: tuple[str, ...]

    def command(self, words: tuple[str, ...]) -> tuple[str, ...]:
        """Return the words that run a program and its arguments on the core of the placement."""
        return (*self.prefix, *words)

    def facts(self) -> tuple[tuple[str, str], ...]:
        """Return the facts of the placement for a sample of the evidence store: the core and the reservation."""
        return (("core", self.core or "none"), ("reserved", self.reserved.source or "no"),
                ("slice", self.reserved.slice_name or "none"))


def log(text: str) -> None:
    """Print one step in bold cyan."""
    print(f"\033[1;36m» {text}\033[0m", flush=True)


def warn(text: str) -> None:
    """Print one warning in bold yellow on standard error."""
    print(f"\033[1;33m! {text}\033[0m", file=sys.stderr, flush=True)


def parse(argv: list[str]) -> Request:
    """Read the command line.

    Raises:
        BenchError: If an option is unknown
    """
    should_build, should_list = True, False
    targets: list[str] = []
    bench_args: tuple[str, ...] = ()
    for index, word in enumerate(argv):
        if word == "--":
            bench_args = tuple(argv[index + 1:])
            break
        if word == "--no-build":
            should_build = False
        elif word == "--list":
            should_list = True
        elif word.startswith("-"):
            raise BenchError(f"unknown option: {word}")
        else:
            targets.append(word)
    return Request(should_build, should_list, tuple(targets) or DEFAULT_TARGETS, bench_args)


def bench_targets(model: cmake_reader.CodeModel) -> tuple[str, ...]:
    """Return each executable target of the code model of a build with the FOLDER property BENCH_FOLDER.

    `loop pgo` reads its benches with this function too.  Complexity: linear in the number of targets.

    Returns:
        The targets, sorted and each one time
    """
    names = {target.name for target in model.targets if target.type_name == TARGET_TYPE and target.folder == BENCH_FOLDER}
    return tuple(sorted(names))


def parse_cpu_list(text: str) -> tuple[int, ...]:
    """Read a CPU list of the kernel, such as "0-3,8,10-11", as the sorted CPUs.  An empty text gives no CPU.

    Complexity: linear in the number of CPUs of the list.

    Raises:
        BenchError: If an item is not a CPU number or a range of two CPU numbers, or if a range ends before it starts
    """
    cpus: set[int] = set()
    for item in text.strip().split(","):
        if not item:
            continue
        low_text, _, high_text = item.partition("-")
        if not low_text.isdigit() or (high_text and not high_text.isdigit()):
            raise BenchError(f"the CPU list {text.strip()!r} holds the item {item!r}, which is not a CPU or a range")
        low = int(low_text)
        high = int(high_text) if high_text else low
        if high < low:
            raise BenchError(f"the CPU list {text.strip()!r} holds the range {item!r}, and its end is less than its start")
        cpus.update(range(low, high + 1))
    return tuple(sorted(cpus))


def read_text(path: Path) -> str:
    """Return the text of a sysfs or cgroupfs file, or an empty text when the file cannot be read."""
    try:
        return path.read_text(encoding="ascii")
    except OSError:
        return ""


def read_reserved(slice_name: str = "", cpu_root: Path = CPU_ROOT, cgroup_root: Path = CGROUP_ROOT) -> Reserved:
    """Return the reserved CPUs of the host (THE RESERVED CPUS).

    Args:
        slice_name: The slice of the partition when the host has more than one, or empty
        cpu_root: The CPU directory of sysfs
        cgroup_root: The root of the cgroup v2 hierarchy

    Complexity: linear in the number of cgroups of the first two levels below the root.

    Raises:
        BenchError: If the host has two partitions and slice_name names none of them
    """
    isolated = parse_cpu_list(read_text(cpu_root / "isolated"))
    if isolated:
        return Reserved(isolated, "isolated")
    partitions: dict[str, tuple[int, ...]] = {}
    for depth in ("*", "*/*"):
        for marker in sorted(cgroup_root.glob(f"{depth}/cpuset.cpus.partition")):
            if read_text(marker).strip() not in PARTITION_TYPES:
                continue
            cpus = parse_cpu_list(read_text(marker.parent / "cpuset.cpus.exclusive.effective")
                                  or read_text(marker.parent / "cpuset.cpus.effective"))
            if cpus:
                partitions[marker.parent.name] = cpus
    if slice_name:
        if slice_name not in partitions:
            raise BenchError(f"CRUCIBLE_BENCH_SLICE names {slice_name}, and the partitions of the host are "
                             f"{sorted(partitions) or 'none'}")
        return Reserved(partitions[slice_name], "partition", slice_name)
    if len(partitions) > 1:
        raise BenchError(f"the host has the partitions {sorted(partitions)}.  Set CRUCIBLE_BENCH_SLICE to the slice "
                         "of the benches")
    if not partitions:
        return Reserved()
    (name, cpus), = partitions.items()
    return Reserved(cpus, "partition", name)


def core_of(reserved: Reserved, cpu_root: Path = CPU_ROOT) -> int | None:
    """Return the lowest CPU of the first SMT group that holds only reserved CPUs, or None when no group does.

    When sysfs gives no SMT group for a CPU, the CPU is a group of one CPU.  Complexity: linear in the number of
    reserved CPUs, times one sysfs read each.
    """
    reserved_set = frozenset(reserved.cpus)
    for cpu in reserved.cpus:
        group_text = read_text(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list")
        group = parse_cpu_list(group_text) if group_text.strip() else (cpu,)
        if reserved_set.issuperset(group) and cpu == min(group):
            return cpu
    return None


def placement(settings: Settings, cpu_root: Path = CPU_ROOT, cgroup_root: Path = CGROUP_ROOT) -> Placement:
    """Return where each bench runs: a core of the reserved CPUs, the fallback core, or no pin (THE RESERVED CPUS).

    `loop evidence run measurement` runs each bench with the same placement (assay.loop.evidence.measurement).

    Raises:
        BenchError: If read_reserved() refuses, or if taskset is not on PATH
    """
    reserved = read_reserved(settings.slice_name, cpu_root, cgroup_root)
    core = core_of(reserved, cpu_root)
    if core is None:
        if not settings.core:
            return Placement("", reserved, ())
        return Placement(settings.core, reserved, ("taskset", "-c", settings.core))
    taskset = shutil.which("taskset")
    if taskset is None:
        raise BenchError("taskset is not on PATH, and the benches run on a reserved core with it (util-linux)")
    pin = (taskset, "-c", str(core))
    if reserved.source != "partition":
        return Placement(str(core), reserved, pin)
    scope = ("sudo", "-n", "--preserve-env", SYSTEMD_RUN, "--quiet", "--scope", f"--slice={reserved.slice_name}",
             f"--uid={os.getuid()}", f"--gid={os.getgid()}", "--")
    return Placement(str(core), reserved, (*scope, *pin))


def uses_launcher(build_dir: Path, launcher: Path = caps.LAUNCHER) -> bool:
    """Tell if each bench of a build runs through the launcher, and print the reason when it does not (THE CAPABILITIES).

    The answer is yes when the build loads BPF and the launcher is ready.
    """
    try:
        value = cmake_reader.cache_value(build_dir, BPF_ENTRY) or ""
    except cmake_reader.CmakeError as error:
        warn(f"caps: the cache of {build_dir} does not read ({error}): each bench runs with no capabilities")
        return False
    if build_config.normalized("BOOL", value) != "on":
        log(f"caps: {build_dir} loads no BPF program ({BPF_ENTRY} is off): each bench runs with no capabilities")
        return False
    problem = caps.launcher_problem(launcher)
    if problem:
        warn(f"caps: {caps.fix_line(problem, launcher)}")
        warn("caps: each bench runs with no capabilities: the harness omits the eBPF lines, and the times do not change")
        return False
    log(f"caps: each bench runs through {launcher}, so the harness attaches the eBPF lines")
    return True


def run(root: Path, request: Request, settings: Settings) -> int:
    """Build and run each bench of the request, through the launcher when it can, or list the bench targets.

    Args:
        root: The source tree
        request: The command line
        settings: The values of THE ENVIRONMENT

    Returns:
        The exit status

    Raises:
        BenchError: If a step cannot go on
    """
    build_dir = root / settings.build
    if not cmake_reader.is_configured(build_dir):
        raise BenchError(f"{build_dir} not configured — run: cmake --preset {settings.preset}")
    try:
        build_cmake = cmake_pin.configured_program(build_dir, "cmake")
    except cmake_pin.PinError:
        raise BenchError(f"{build_dir} names no cmake that exists — configure it again: cmake --preset "
                         f"{settings.preset}") from None
    if request.should_list:
        log(f"buildable bench targets in {build_dir}:")
        try:
            model = cmake_reader.codemodel(build_dir)
        except cmake_reader.CmakeError as problem:
            raise BenchError(str(problem)) from None
        for name in bench_targets(model):
            print(name)
        return 0
    where = placement(settings)
    reserved = where.reserved
    if reserved.source == "partition":
        log(f"reserved partition {reserved.slice_name} on cpus {reserved.cpus[0]}-{reserved.cpus[-1]}: each bench runs "
            f"in the slice on cpu{where.core}, and its SMT sibling stays idle")
    elif reserved.source == "isolated" and where.core:
        log(f"isolated cpus {reserved.cpus[0]}-{reserved.cpus[-1]}: each bench runs on cpu{where.core}")
    elif reserved.cpus:
        warn(f"the reserved cpus {reserved.cpus[0]}-{reserved.cpus[-1]} hold no full SMT group: the SMT sibling of "
             "the core can run other work")
    if not where.core:
        warn("the host reserves no cpu, and CRUCIBLE_BENCH_CORE is not set: each bench runs with no pin, and its "
             "results are NOISY")
    elif not reserved.source:
        warn(f"the host reserves no cpu: each bench runs on cpu{where.core} of CRUCIBLE_BENCH_CORE, a shared cpu with "
             "a tick, and its results are NOISY")
    is_launched = uses_launcher(build_dir)
    for target in request.targets:
        binary = build_dir / "bench" / target
        if request.should_build:
            log(f"build {target}")
            built = proc.run(proc.Command([build_cmake, "--build", str(build_dir), "--target", target, "-j",
                                           str(proc.DEFAULT_JOBS)], timeout_s=proc.BUILD_S,
                                          stdout=proc.Output.DISCARD, stderr=proc.Output.INHERIT))
            if built.exit != 0:
                raise BenchError(f"build failed: {target} (known-drifted bench? try another target)")
        if not os.access(binary, os.X_OK):
            raise BenchError(f"missing binary: {binary} (build it first, drop --no-build)")
        log(f"run  {target}   (cpu {where.core or 'not pinned'})")
        words = (str(binary), *request.bench_args)
        launched = tuple(caps.wrapped(words)) if is_launched else words
        status = proc.run(proc.Command(list(where.command(launched)), timeout_s=proc.BUILD_S,
                                       stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT)).exit
        if status != 0:
            return status
        print()
    return 0


# ── The self-test ──────────────────────────────────────────────────────────


def self_test() -> int:
    """Check the command line, the list of targets, the choice of the core and the refusal of a build that is absent.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    expect("no target gives the default set, and the words after -- go to each bench",
           parse(["--", "--iterations", "5"]) == Request(True, False, DEFAULT_TARGETS, ("--iterations", "5")))
    expect("--no-build and two targets",
           parse(["--no-build", "bench_arena", "bench_philox"])
           == Request(False, False, ("bench_arena", "bench_philox"), ()))
    try:
        parse(["--fast"])
        expect("an unknown option is refused", False)
    except BenchError:
        expect("an unknown option is refused", True)
    model = cmake_reader.CodeModel("/source", "/build", (
        cmake_reader.Target("bench_trace_ring", TARGET_TYPE, folder=BENCH_FOLDER),
        cmake_reader.Target("bench_arena", TARGET_TYPE, folder=BENCH_FOLDER),
        cmake_reader.Target("bench_arena", TARGET_TYPE, folder=BENCH_FOLDER),
        cmake_reader.Target("bench_unmarked", TARGET_TYPE),
        cmake_reader.Target("bench_harness", "STATIC_LIBRARY", folder=BENCH_FOLDER),
        cmake_reader.Target("bench_report", "UTILITY"),
        cmake_reader.Target("bench_philox", TARGET_TYPE, folder=BENCH_FOLDER)))
    expect("the code model gives each executable of the bench folder one time, and no library and no other target",
           bench_targets(model) == ("bench_arena", "bench_philox", "bench_trace_ring"))
    expect("a CPU list gives its sorted CPUs, and an empty list gives none",
           parse_cpu_list("8,0-3\n") == (0, 1, 2, 3, 8) and parse_cpu_list("\n") == ())
    for bad in ("3-1", "a", "1-b"):
        try:
            parse_cpu_list(bad)
            expect(f"the CPU list {bad!r} is refused", False)
        except BenchError:
            expect(f"the CPU list {bad!r} is refused", True)
    with tempfile.TemporaryDirectory(prefix="loop-bench-") as work:
        cpu_root, cgroup_root = Path(work, "cpu"), Path(work, "cgroup")

        def plant(path: Path, text: str) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="ascii")

        for cpu in range(220, 256):
            plant(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list", f"{cpu & ~1}-{cpu | 1}\n")
        plant(cpu_root / "isolated", "\n")
        expect("with no reservation and no CRUCIBLE_BENCH_CORE, no pin",
               placement(Settings("b", "p"), cpu_root, cgroup_root) == Placement("", Reserved(), ()))
        expect("with no reservation, CRUCIBLE_BENCH_CORE is the core",
               placement(Settings("b", "p", "5"), cpu_root, cgroup_root).prefix == ("taskset", "-c", "5"))
        plant(cgroup_root / "bad.slice" / "cpuset.cpus.partition", "root invalid (Cpu list in cpuset.cpus not exclusive)\n")
        plant(cgroup_root / "bad.slice" / "cpuset.cpus.effective", "0-3\n")
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.partition", "root\n")
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.exclusive.effective", "224-255\n")
        where = placement(Settings("b", "p", "5"), cpu_root, cgroup_root)
        expect("a valid partition is the reservation, and the kernel's invalid one is not",
               where.reserved == Reserved(tuple(range(224, 256)), "partition", "reserved.slice"))
        expect("a partition starts the bench in its slice, on the lowest CPU of a full SMT group",
               where.core == "224" and "--slice=reserved.slice" in where.prefix and where.prefix[-2:] == ("-c", "224")
               and where.command(("bench",))[-1] == "bench")
        launched = where.command(tuple(caps.wrapped(("bench",))))
        expect("the launcher comes after the sudo of a partition, which clears the ambient set",
               launched.index(str(caps.LAUNCHER)) > launched.index("sudo") and launched[-1] == "bench")
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.exclusive.effective", "225-255\n")
        expect("a half SMT group is skipped, so the core has an idle sibling",
               core_of(read_reserved("", cpu_root, cgroup_root), cpu_root) == 226)
        plant(cgroup_root / "other.slice" / "cpuset.cpus.partition", "isolated\n")
        plant(cgroup_root / "other.slice" / "cpuset.cpus.exclusive.effective", "220-223\n")
        try:
            read_reserved("", cpu_root, cgroup_root)
            expect("two partitions with no CRUCIBLE_BENCH_SLICE are refused", False)
        except BenchError as problem:
            expect("two partitions with no CRUCIBLE_BENCH_SLICE are refused", "CRUCIBLE_BENCH_SLICE" in str(problem))
        expect("CRUCIBLE_BENCH_SLICE chooses one of two partitions",
               read_reserved("other.slice", cpu_root, cgroup_root).cpus == (220, 221, 222, 223))
        plant(cpu_root / "isolated", "220-223\n")
        where = placement(Settings("b", "p"), cpu_root, cgroup_root)
        expect("isolcpus comes first, and taskset alone puts the bench on its core",
               where.reserved.source == "isolated" and where.core == "220" and "sudo" not in where.prefix)
        expect("a build with no reply of the cache runs each bench with no capabilities", not uses_launcher(Path(work)))
        try:
            run(Path(work), parse([]), Settings("absent", DEFAULT_PRESET))
            expect("a build directory that is not configured is refused", False)
        except BenchError as problem:
            expect("a build directory that is not configured is refused", "not configured" in str(problem))

    if failures:
        print(f"loop bench --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop bench --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the benches, list the targets, print the usage or run the self-test.

    Args:
        argv: The words after `loop bench`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] in (["-h"], ["--help"]):
        print(__doc__)
        return 0
    try:
        request = parse(argv)
    except BenchError as problem:
        print(f"\033[1;31m✗ {problem}\033[0m", file=sys.stderr)
        return 2
    try:
        return run(REPO_ROOT, request, Settings.from_environment())
    except (BenchError, proc.ToolFailed) as problem:
        print(f"\033[1;31m✗ {problem}\033[0m", file=sys.stderr)
        return 1
