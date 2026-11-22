"""bench — build the benches and run each one on isolated cores, with the capabilities of BPF (`loop bench`).

Steps for each bench
    1. The command builds the bench in the build directory of the bench preset,
       with the job count of assay.core.proc (DEFAULT_JOBS).  The build is
       incremental.  A bench that is current builds nothing.  --no-build
       omits the step.
    2. A bench whose x86-64 level is above the level of the host does not
       start, and a warning names the two levels (the check of
       assay.core.x86level).  The next bench runs.
    3. It runs the bench through the launcher of assay.loop.caps, when the
       build loads BPF (the cache entry BPF_ENTRY) and the launcher is ready
       (the capabilities).
    4. It runs the bench with taskset on the cores of the reserved CPUs of the
       host that no other bench holds, one core for each thread of the bench
       (the cores of a run).  When the host reserves no CPU, it uses the core
       of CRUCIBLE_BENCH_CORE, or no pin when that variable is not set.  It
       then warns that the results are noisy.
    5. It prints the steal time of each core during the run, and it marks a
       run with steal time as not valid (the steal time).

    The form --command runs one command on a claim of cores in place of the
    benches: the same locks, the same slice and the same steal check, with no
    build and no launcher.  A program that is not a bench target, such as
    vessel/torch/bench_e2e.py, runs on its cores in this form.

Capabilities
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

Reserved CPUs
    The host reserves CPUs for dedicated work in one of two ways.
    read_reserved() reads the first one that the host gives:
      1. One cgroup cpuset partition: a cgroup below the root with the value
         "root" or "isolated" in its cpuset.cpus.partition.  A partition that
         the kernel refuses does not count.  A process of a different cgroup
         cannot run on the CPUs of the partition, also when the CPUs are in
         the isolated file.  So the partition comes first, and the command
         starts each bench in the systemd slice of the partition, with `sudo
         -n systemd-run --scope`, as the calling user.  When the isolated file
         holds other CPUs, the command prints the two lists.  When the host
         has two partitions, CRUCIBLE_BENCH_SLICE must name one of them.
      2. /sys/devices/system/cpu/isolated, which isolcpus=domain fills.  A
         process can run on these CPUs with taskset alone.
    A core of the runs is the lowest CPU of an SMT group that holds only
    reserved CPUs.  The SMT sibling of the core then stays idle.

Cores of a run
    Two threads on one core change the times of the two, so each command
    holds its cores alone, and a bench gets one core for each of its threads.
    A bench states the count of its threads with the argument CORES of
    crucible_bench() in bench/CMakeLists.txt, which sets the compile
    definition CORES_DEFINE.  The command reads it from the code model of
    the build, and a bench with no such definition gets one core.  --cores N
    gives each bench of the command N cores.  The harness pins each thread of
    the bench to one core of its affinity (bench/bench_harness.h).

    An affinity mask has no order, so the placement also gives the bench the
    cores in the order of the claim, in the variable ORDER_VARIABLE, as
    "240,242,224".  The word ENV_PROGRAM sets it after the sudo of a
    partition, so no policy of sudo can drop it, and the launcher of the
    capabilities keeps it.  The harness gives slot k the core k of that
    order, so slot 0, the measuring thread, gets the quietest core of the
    claim (the cores of a run of bench/bench_harness.h).

    claimed() takes the cores of one claim.  It reads the groups of the cores
    that share one L3 cache, from the cache of level 3 in sysfs
    (cpuN/cache/indexK/shared_cpu_list).  When sysfs gives no such cache for
    any core, the cores are one group.  The groups with fewer lines in the
    record of the steal time come first, and then the group of the lower
    first core.  In a group, the cores with fewer lines come first, then the
    lower numbers.  A claim that fits in one group takes all its cores in
    one group: the first group in which it holds enough free cores.  A claim
    larger than each group spans the groups, in their order, and the command
    prints that its threads share no L3 cache.  A claim larger than the
    reserved cores is refused with the two counts.

    The command takes a core when an exclusive lock of flock(2) on the file
    cpuN.lock of CORE_LOCK_DIR in the main work tree takes with no wait.  It
    holds the lock of each core of the claim for the run, and it frees the
    locks of a group in which it found too few free cores.  So no claim
    holds a core while it waits for another.  On a host that isolates its
    CPUs, the command then runs a busy loop on each core for PROBE_S seconds.
    A core that loses time to steal in the probe shares its host core with
    other work, such as an emulator thread of the hypervisor, so the command
    frees it and does not try it again.  When other benches hold too many
    cores, the command waits, and it tries the cores again each WAIT_S
    seconds.  A small claim can then go before a large one.  The core of
    CRUCIBLE_BENCH_CORE takes the same lock and no probe, and a claim of more
    than one core with it is refused.

Steal time
    The column steal of the line of a core in /proc/stat counts the time in
    which the hypervisor ran other work while the core had work, in ticks of
    the clock of the kernel.  The command reads it for each core of the claim
    before and after each run, and it prints the steal time in milliseconds
    after the output of the bench.  A run with steal time above zero on one
    of its cores is not valid, and the line says so.  A report with a
    variation above 5 % is valid, and it asks for a second run (the run of
    assay.loop.evidence.measurement).  A run with no pin prints no steal time.  A
    tick is 10 ms, so a pause of a few milliseconds reads as no steal.  The
    harness measures such a pause in nanoseconds for each run of a bench, and
    it marks a run with a pause over its bound (the pause check of
    bench/bench_harness.h).

    Each core with steal time in a run adds one line to the record
    STEAL_RECORD in CORE_LOCK_DIR: the date in UTC, the core and the steal
    time, as "2026-11-16T15:04:05Z cpu228 40 ms".  A probe of the claim adds
    no line, because its core is skipped at once.  A short probe misses a
    steal rate below about 1 %, and the record keeps such a core behind the
    clean cores.  Only a line of the last 24 hours counts, so a core gets its
    place back one day after its last run with steal.  To clear the record of
    a host, delete the file.

Environment
    CRUCIBLE_BENCH_BUILD   The build directory of the bench preset, relative to the tree.  The default is build-bench
    CRUCIBLE_BENCH_PRESET  The preset that configures it.  The default is bench
    CRUCIBLE_BENCH_CORE    The core when the host reserves no CPU.  Not set: the bench runs with no pin
    CRUCIBLE_BENCH_SLICE   The slice of the partition when the host has more than one

Usage
    loop bench [--no-build] [--cores N] [TARGET]... [-- ARG...]   build and run each TARGET, or the default set
    loop bench --cores N --command -- WORD...                       run one command on a claim of N cores
    loop bench --list                                               print the bench targets of the build directory
    guard selftest loop.bench

    Each ARG after -- goes to each bench.  The default set is the hot-path
    latency set: bench_trace_ring, bench_swiss_table, bench_philox and
    bench_pool_allocator.  With no --cores, each bench gets the count of its
    definition CORES_DEFINE, and a command gets one core.

Exit 0 when each bench passed, or the exit status of the first bench that failed.  Exit 1 when a step failed, and 2 on
a usage error or a failed self-test.
"""

from __future__ import annotations

import calendar
import contextlib
import dataclasses
import fcntl
import io
import os
import shutil
import sys
import tempfile
import time
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import proc, x86level
from assay.core.paths import REPO_ROOT
from assay.loop import caps, heavy
from assay.readers import cmake as cmake_reader
from assay.setup import pins as cmake_pin

DEFAULT_BUILD = "build-bench"
DEFAULT_PRESET = "bench"
# The cache entry that tells if the build loads BPF programs (the capabilities).  cmake/CrucibleBpf.cmake writes it.
BPF_ENTRY = "CRUCIBLE_HAVE_BPF"
# The hot-path latency set.  bench_dispatch and bench_meta_log are not in it, because their builds drift.
DEFAULT_TARGETS = ("bench_trace_ring", "bench_swiss_table", "bench_philox", "bench_pool_allocator")
# The CPU directory of sysfs and the root of the cgroup v2 hierarchy (the reserved CPUs).
CPU_ROOT = Path("/sys/devices/system/cpu")
CGROUP_ROOT = Path("/sys/fs/cgroup")
# The values of cpuset.cpus.partition of a valid partition.  The kernel writes "root invalid (REASON)" for a partition
# that it refuses.
PARTITION_TYPES = frozenset({"root", "isolated"})
# The program that starts a command in a slice, by its full path.  sudo can give the command a different PATH.
SYSTEMD_RUN = "/usr/bin/systemd-run"
# The program that sets the variable of the order of the cores before taskset, by its full path, and the variable that
# holds the cores in the order of the claim (the cores of a run).
ENV_PROGRAM = "/usr/bin/env"
ORDER_VARIABLE = "CRUCIBLE_BENCH_CORE_ORDER"
# The directory of the lock file of each core, relative to the main work tree (the cores of a run).
CORE_LOCK_DIR = Path(".worktrees/.grun/bench-cores")
# The compile definition of a bench target that states the count of its cores, as NAME=COUNT (the cores of a run).
CORES_DEFINE = "CRUCIBLE_BENCH_CORES"
# The level of the cache whose groups of CPUs a claim keeps together, in the file level of a cache of sysfs.
SHARED_CACHE_LEVEL = "3"
# The time of the busy loop that probes a core for steal time, in seconds.  A core that loses 2 % of its time to the
# hypervisor loses two ticks of 10 ms in it (the cores of a run).
PROBE_S = 1.0
# The interval between two tries of the cores when another bench holds each one, in seconds.
WAIT_S = 2.0
# The statistics of the kernel with the steal column of each CPU (the steal time).
STAT_FILE = Path("/proc/stat")
# The index of the column steal in the line of a CPU of STAT_FILE, after the name of the CPU.
STEAL_COLUMN = 8
# The record of the runs with steal time, in CORE_LOCK_DIR (the steal time).
STEAL_RECORD = "steal-record.txt"
# The age of the oldest line of the record that counts, in seconds: 24 hours (the steal time).
RECORD_AGE_S = 24 * 3600
# The type and the FOLDER property of a bench target in the code model of CMake.  The function crucible_bench() of
# bench/CMakeLists.txt sets the folder.
TARGET_TYPE = "EXECUTABLE"
BENCH_FOLDER = "crucible_bench"


class BenchError(Exception):
    """A step stops the command: a build directory that is not configured, a failed build or a missing binary."""


@dataclass(frozen=True, slots=True, order=True)
class CoreCount:
    """The count of the cores of one claim, at least one: one core for each thread of a bench (the cores of a run).

    Raises:
        BenchError: If the count is less than one
    """

    value: int

    def __post_init__(self) -> None:
        """Refuse a count of no core."""
        if self.value < 1:
            raise BenchError(f"a claim takes at least one core, and the count is {self.value}")


ONE_CORE = CoreCount(1)


@dataclass(frozen=True, slots=True)
class Request:
    """The command line: whether to build, whether to list, the targets and the arguments of each bench.

    cores is the count of --cores, or None for the count of each bench.  command holds the words of the form
    --command, and it is empty for the form of the targets.
    """

    should_build: bool
    should_list: bool
    targets: tuple[str, ...]
    bench_args: tuple[str, ...]
    cores: CoreCount | None = None
    command: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Settings:
    """The values of the environment: the build directory relative to the tree, the preset, the core and the slice.

    An empty core or slice is a variable that is not set.
    """

    build: str
    preset: str
    core: str = ""
    slice_name: str = ""

    @classmethod
    def from_environment(cls) -> Settings:
        """Read the settings from the environment, with the default of each variable that is not set."""
        return cls(
            os.environ.get("CRUCIBLE_BENCH_BUILD", DEFAULT_BUILD),
            os.environ.get("CRUCIBLE_BENCH_PRESET", DEFAULT_PRESET),
            os.environ.get("CRUCIBLE_BENCH_CORE", ""),
            os.environ.get("CRUCIBLE_BENCH_SLICE", ""),
        )


@dataclass(frozen=True, slots=True)
class Reserved:
    """The CPUs that the host reserves, the source of the reservation and the slice of a partition (the reserved CPUs).

    source is "isolated", "partition" or "" when the host reserves no CPU.  slice_name is empty unless the source is a
    partition.
    """

    cpus: tuple[int, ...] = ()
    source: str = ""
    slice_name: str = ""


@dataclass(frozen=True, slots=True)
class Placement:
    """Where a bench runs: the cores of the claim, the reservation that holds them, and the words before the program.

    cores is empty when the bench runs with no pin, and else sorted.  prefix holds the words that start the program on
    the cores.  record is the record of the runs with steal time of the claim, or None for a placement of no claim (the
    steal time).  order holds the cores in the order of the claim, the quietest core first, and prefix gives it to the
    program in ORDER_VARIABLE (the cores of a run).
    """

    cores: tuple[int, ...]
    reserved: Reserved
    prefix: tuple[str, ...]
    record: Path | None = None
    order: tuple[int, ...] = ()

    def command(self, words: tuple[str, ...]) -> tuple[str, ...]:
        """Return the words that run a program and its arguments on the cores of the placement."""
        return (*self.prefix, *words)

    def listed(self) -> str:
        """Return the cores as a CPU list of the kernel, or "not pinned" for a placement with no pin."""
        return format_cpu_list(self.cores) or "not pinned"

    def facts(self) -> tuple[tuple[str, str], ...]:
        """Return the facts of the placement for a sample of the evidence store: the cores and the reservation."""
        return (
            ("cores", format_cpu_list(self.cores) or "none"),
            ("reserved", self.reserved.source or "no"),
            ("slice", self.reserved.slice_name or "none"),
        )


def log(text: str) -> None:
    """Print one step in bold cyan."""
    print(f"\033[1;36m» {text}\033[0m", flush=True)


def warn(text: str) -> None:
    """Print one warning in bold yellow on standard error."""
    print(f"\033[1;33m! {text}\033[0m", file=sys.stderr, flush=True)


def parse_core_count(text: str) -> CoreCount:
    """Read the count of --cores or of the definition CORES_DEFINE.

    Raises:
        BenchError: If the text is not a decimal number of at least one
    """
    if not text.isdigit():
        raise BenchError(f"the count of cores {text!r} is not a decimal number")
    return CoreCount(int(text))


def parse(argv: list[str]) -> Request:
    """Read the command line.

    Raises:
        BenchError: If an option is unknown, if --cores has no count, or if --command has a target or no word
    """
    should_build, should_list, is_command = True, False, False
    cores: CoreCount | None = None
    targets: list[str] = []
    bench_args: tuple[str, ...] = ()
    words = iter(enumerate(argv))
    for index, word in words:
        if word == "--":
            bench_args = tuple(argv[index + 1 :])
            break
        if word == "--no-build":
            should_build = False
        elif word == "--list":
            should_list = True
        elif word == "--command":
            is_command = True
        elif word == "--cores":
            _, count = next(words, (index, ""))
            cores = parse_core_count(count)
        elif word.startswith("-"):
            raise BenchError(f"unknown option: {word}")
        else:
            targets.append(word)
    if not is_command:
        return Request(should_build, should_list, tuple(targets) or DEFAULT_TARGETS, bench_args, cores)
    if targets or not bench_args:
        raise BenchError("--command takes no target, and it runs the words after --: loop bench --command -- WORD...")
    return Request(False, should_list, (), (), cores, bench_args)


def bench_targets(model: cmake_reader.CodeModel) -> tuple[str, ...]:
    """Return each executable target of the code model of a build with the FOLDER property BENCH_FOLDER.

    `loop pgo` reads its benches with this function too.  Complexity: linear in the number of targets.

    Returns:
        The targets, sorted and each one time
    """
    names = {
        target.name for target in model.targets if target.type_name == TARGET_TYPE and target.folder == BENCH_FOLDER
    }
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
            raise BenchError(
                f"the CPU list {text.strip()!r} holds the range {item!r}, and its end is less than its start"
            )
        cpus.update(range(low, high + 1))
    return tuple(sorted(cpus))


def format_cpu_list(cpus: tuple[int, ...]) -> str:
    """Write sorted CPUs as a CPU list of the kernel: each run of consecutive CPUs as "low-high", joined by commas.

    The inverse of parse_cpu_list().  Complexity: linear in the number of CPUs.
    """
    runs: list[str] = []
    index = 0
    while index < len(cpus):
        end = index
        while end + 1 < len(cpus) and cpus[end + 1] == cpus[end] + 1:
            end += 1
        runs.append(str(cpus[index]) if end == index else f"{cpus[index]}-{cpus[end]}")
        index = end + 1
    return ",".join(runs)


def read_text(path: Path) -> str:
    """Return the text of a sysfs or cgroupfs file, or an empty text when the file cannot be read."""
    try:
        return path.read_text(encoding="ascii")
    except OSError:
        return ""


def read_reserved(slice_name: str = "", cpu_root: Path = CPU_ROOT, cgroup_root: Path = CGROUP_ROOT) -> Reserved:
    """Return the reserved CPUs of the host (the reserved CPUs).

    Args:
        slice_name: The slice of the partition when the host has more than one, or empty
        cpu_root: The CPU directory of sysfs
        cgroup_root: The root of the cgroup v2 hierarchy

    Complexity: linear in the number of cgroups of the first two levels below the root.

    Raises:
        BenchError: If the host has two partitions and slice_name names none of them
    """
    isolated = parse_cpu_list(read_text(cpu_root / "isolated"))
    partitions: dict[str, tuple[int, ...]] = {}
    for depth in ("*", "*/*"):
        for marker in sorted(cgroup_root.glob(f"{depth}/cpuset.cpus.partition")):
            if read_text(marker).strip() not in PARTITION_TYPES:
                continue
            cpus = parse_cpu_list(
                read_text(marker.parent / "cpuset.cpus.exclusive.effective")
                or read_text(marker.parent / "cpuset.cpus.effective")
            )
            if cpus:
                partitions[marker.parent.name] = cpus
    if slice_name:
        if slice_name not in partitions:
            raise BenchError(
                f"CRUCIBLE_BENCH_SLICE names {slice_name}, and the partitions of the host are "
                f"{sorted(partitions) or 'none'}"
            )
        name, cpus = slice_name, partitions[slice_name]
    elif len(partitions) > 1:
        raise BenchError(
            f"the host has the partitions {sorted(partitions)}.  Set CRUCIBLE_BENCH_SLICE to the slice of the benches"
        )
    elif partitions:
        ((name, cpus),) = partitions.items()
    elif isolated:
        return Reserved(isolated, "isolated")
    else:
        return Reserved()
    if isolated and isolated != cpus:
        warn(
            f"the isolated cpus {list(isolated)} and the cpus {list(cpus)} of the partition {name} differ: the benches "
            "run in the partition"
        )
    return Reserved(cpus, "partition", name)


def cores_of(reserved: Reserved, cpu_root: Path = CPU_ROOT) -> tuple[int, ...]:
    """Return the lowest CPU of each SMT group that holds only reserved CPUs, in the order of their numbers.

    When sysfs gives no SMT group for a CPU, the CPU is a group of one CPU.  Complexity: linear in the number of
    reserved CPUs, times one sysfs read each.
    """
    reserved_set = frozenset(reserved.cpus)
    cores: list[int] = []
    for cpu in reserved.cpus:
        group_text = read_text(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list")
        group = parse_cpu_list(group_text) if group_text.strip() else (cpu,)
        if reserved_set.issuperset(group) and cpu == min(group):
            cores.append(cpu)
    return tuple(cores)


def cores_of_target(target: cmake_reader.Target) -> CoreCount:
    """Return the count of the cores of a bench target: the count of its definition CORES_DEFINE, or one core.

    Raises:
        BenchError: If the definition gives no count of at least one
    """
    prefix = f"{CORES_DEFINE}="
    for define in target.defines:
        if define.startswith(prefix):
            try:
                return parse_core_count(define.removeprefix(prefix))
            except BenchError as problem:
                raise BenchError(f"the bench {target.name} has the definition {define!r}: {problem}") from None
    return ONE_CORE


def shared_cache_of(cpu: int, cpu_root: Path = CPU_ROOT) -> tuple[int, ...]:
    """Return the CPUs that share the cache of level SHARED_CACHE_LEVEL with a CPU, or none when sysfs gives no such cache.

    Complexity: linear in the caches of the CPU, times one sysfs read each.
    """
    for index in sorted((cpu_root / f"cpu{cpu}" / "cache").glob("index*")):
        if read_text(index / "level").strip() == SHARED_CACHE_LEVEL:
            return parse_cpu_list(read_text(index / "shared_cpu_list"))
    return ()


def cache_groups(cores: tuple[int, ...], cpu_root: Path = CPU_ROOT) -> tuple[tuple[int, ...], ...]:
    """Return the cores in groups that share one L3 cache, each group sorted, in the order of their first cores.

    When sysfs gives no L3 cache for any core, the cores are one group.  Else a core with no L3 cache is a group of
    its own (the cores of a run).  Complexity: linear in the cores, times the sysfs reads of one core.
    """
    core_set = frozenset(cores)
    shared = {core: shared_cache_of(core, cpu_root) for core in cores}
    if not any(shared.values()):
        return (tuple(sorted(cores)),) if cores else ()
    groups: dict[int, tuple[int, ...]] = {}
    for core in cores:
        group = tuple(sorted(core_set.intersection(shared[core]) | {core}))
        groups[group[0]] = group
    return tuple(groups[first] for first in sorted(groups))


def ranked_groups(groups: tuple[tuple[int, ...], ...], counts: dict[int, int]) -> tuple[tuple[int, ...], ...]:
    """Return the groups in the order of a claim, and the cores of each group in that order (the cores of a run).

    A group with fewer lines in the record of the steal time comes first, then the group of the lower first core.  In
    a group, a core with fewer lines comes first, then the lower number.  Complexity: O(n log n) in the cores.
    """
    ordered = [tuple(sorted(group, key=lambda core: (counts.get(core, 0), core))) for group in groups]
    return tuple(sorted(ordered, key=lambda group: (sum(counts.get(core, 0) for core in group), min(group))))


def placement_on(cores: tuple[int, ...], reserved: Reserved, taskset: str) -> Placement:
    """Return the placement of each bench on cores of the reserved CPUs, in the slice of a partition.

    cores holds the cores in the order of the claim.  The words of the placement set ORDER_VARIABLE to that order
    before taskset (the cores of a run).
    """
    held = tuple(sorted(cores))
    ordered = ",".join(str(core) for core in cores)
    pin = (ENV_PROGRAM, f"{ORDER_VARIABLE}={ordered}", taskset, "-c", format_cpu_list(held))
    if reserved.source != "partition":
        return Placement(held, reserved, pin, order=cores)
    scope = (
        "sudo",
        "-n",
        "--preserve-env",
        SYSTEMD_RUN,
        "--quiet",
        "--scope",
        f"--slice={reserved.slice_name}",
        f"--uid={os.getuid()}",
        f"--gid={os.getgid()}",
        "--",
    )
    return Placement(held, reserved, (*scope, *pin), order=cores)


def read_steal_ticks(core: int, stat_file: Path = STAT_FILE) -> int | None:
    """Return the steal column of the line of a CPU of the kernel statistics, or None when the file has no such line.

    Complexity: linear in the number of lines of the file (the steal time).
    """
    label = f"cpu{core}"
    for line in read_text(stat_file).splitlines():
        words = line.split()
        if words and words[0] == label and len(words) > STEAL_COLUMN and words[STEAL_COLUMN].isdigit():
            return int(words[STEAL_COLUMN])
    return None


def read_steals(cores: tuple[int, ...], stat_file: Path = STAT_FILE) -> dict[int, int | None]:
    """Return the steal ticks of each core, or None for a core with no line in the statistics (the steal time).

    Complexity: linear in the cores, times the lines of the file.
    """
    return {core: read_steal_ticks(core, stat_file) for core in cores}


def tick_ms() -> float:
    """Return the length of one tick of the kernel statistics in milliseconds."""
    return 1000.0 / os.sysconf("SC_CLK_TCK")


def stolen_ms_of(before: dict[int, int | None], after: dict[int, int | None]) -> dict[int, float]:
    """Return the steal time of each core with a reading before and after a run, in milliseconds (the steal time)."""
    stolen: dict[int, float] = {}
    for core, first in before.items():
        last = after.get(core)
        if first is not None and last is not None:
            stolen[core] = (last - first) * tick_ms()
    return stolen


def probe_steal(core: int, stat_file: Path = STAT_FILE, seconds: float = PROBE_S) -> int | None:
    """Run a busy loop on a core for the seconds, and return the steal ticks of the core in that time.

    The calling thread runs on the core for the probe, and then it runs on its CPUs of before.  Returns None when the
    thread cannot run on the core, or when the statistics have no line for it (the core of a run).
    """
    previous = os.sched_getaffinity(0)
    try:
        os.sched_setaffinity(0, {core})
    except OSError:
        return None
    try:
        before = read_steal_ticks(core, stat_file)
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            pass
        after = read_steal_ticks(core, stat_file)
    finally:
        os.sched_setaffinity(0, previous)
    if before is None or after is None:
        return None
    return after - before


def lock_dir_of(root: Path = REPO_ROOT) -> Path:
    """Return the directory of the lock files of the cores, in the main work tree of a tree (the core of a run)."""
    return heavy.main_work_tree(root) / CORE_LOCK_DIR


def read_steal_record(record: Path, moment: float | None = None) -> dict[int, int]:
    """Return the count of the lines of each core in the record of the runs with steal time (the steal time).

    Only a line of the last RECORD_AGE_S seconds before moment counts, so a core whose host core has no more steal
    gets its place back in a day.  A line that does not read, and a record that does not exist, count nothing.

    Args:
        record: The record file
        moment: The time of now in seconds since the epoch, or None for the clock

    Complexity: linear in the lines.
    """
    oldest = (time.time() if moment is None else moment) - RECORD_AGE_S
    counts: dict[int, int] = {}
    for line in read_text(record).splitlines():
        words = line.split()
        if len(words) != 4 or not words[1].startswith("cpu") or not words[1][3:].isdigit() or words[3] != "ms":
            continue
        try:
            stamp = calendar.timegm(time.strptime(words[0], "%Y-%m-%dT%H:%M:%SZ"))
        except ValueError:
            continue
        if stamp < oldest:
            continue
        core = int(words[1][3:])
        counts[core] = counts.get(core, 0) + 1
    return counts


def record_steal(record: Path, core: int, stolen_ms: float, moment: float | None = None) -> None:
    """Add the line of one run with steal time to the record, under an exclusive lock of flock(2) (the steal time).

    Args:
        record: The record file
        core: The core of the run
        stolen_ms: The steal time of the run in milliseconds
        moment: The time of the run in seconds since the epoch, or None for the time of now
    """
    stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() if moment is None else moment))
    record.parent.mkdir(parents=True, exist_ok=True)
    with open(record, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        handle.write(f"{stamp} cpu{core} {stolen_ms:.0f} ms\n")


def note_steal(where: Placement, before: dict[int, int | None], after: dict[int, int | None]) -> None:
    """Add each core of a claim with steal time in a run to the record of the claim, and nothing for a placement of
    no claim."""
    if where.record is None:
        return
    for core, stolen_ms in stolen_ms_of(before, after).items():
        if stolen_ms > 0:
            record_steal(where.record, core, stolen_ms)


def try_lock(directory: Path, core: int) -> int | None:
    """Return the open descriptor of the lock file of a core with its exclusive lock of flock(2), or None when another
    holder has it.

    The caller closes the descriptor with os.close, and the close frees the lock (the cores of a run).
    """
    descriptor = os.open(directory / f"cpu{core}.lock", os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_CLOEXEC, 0o644)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(descriptor)
        return None
    return descriptor


class Probed:
    """The probes of one claim: the steal ticks of a busy loop on a core, whether the claim runs them, and the cores
    that each answer put in the set of the cores with steal or in the set of the clean cores.

    The claim probes each core at most one time, so a wait does not run the probe of a core again.
    """

    __slots__ = ("clean", "is_run", "steal_of", "stolen")

    def __init__(self, steal_of: Callable[[int], int | None], is_run: bool) -> None:
        """Hold a probe with no answer yet."""
        self.steal_of = steal_of
        self.is_run = is_run
        self.stolen: set[int] = set()
        self.clean: set[int] = set()

    def has_steal(self, core: int) -> bool:
        """Probe a core one time, warn when it lost time to steal, and tell whether it did (the cores of a run)."""
        if not self.is_run or core in self.clean:
            return False
        if core in self.stolen:
            return True
        ticks = self.steal_of(core)
        if not ticks:
            self.clean.add(core)
            return False
        warn(
            f"cpu{core} lost {ticks * tick_ms():.0f} ms to steal in a busy loop of {PROBE_S:g} s: its host core runs "
            "other work, so the benches skip it"
        )
        self.stolen.add(core)
        return True


def take_cores(
    pool: tuple[int, ...], count: CoreCount, directory: Path, probed: Probed
) -> list[tuple[int, int]] | None:
    """Lock count free cores of a pool, in the order of the pool, or lock none and return None (the cores of a run).

    A core that another holder locks is skipped.  A core that loses time to steal in its probe is skipped, and its
    lock is freed.  When the pool has too few free cores, each lock of the try is freed, so a claim never holds a
    core while it waits.  Complexity: linear in the pool, plus one probe of each core that the claim did not probe.
    """
    held: list[tuple[int, int]] = []
    for core in pool:
        if core in probed.stolen:
            continue
        descriptor = try_lock(directory, core)
        if descriptor is None:
            continue
        if probed.has_steal(core):
            os.close(descriptor)
            continue
        held.append((core, descriptor))
        if len(held) == count.value:
            return held
    for _, descriptor in held:
        os.close(descriptor)
    return None


def pools_of(
    groups: tuple[tuple[int, ...], ...], count: CoreCount, stolen: set[int]
) -> tuple[tuple[tuple[int, ...], ...], bool]:
    """Return the pools in which a claim tries its cores, and True when the claim spans the L3 groups.

    A claim that fits in one group has one pool for each group with enough cores that lost no time to steal.  A
    larger claim has one pool: each group, one after another.  No pool fits a claim larger than the cores that lost no
    time to steal (the cores of a run).
    """
    clean = tuple(tuple(core for core in group if core not in stolen) for group in groups)
    fitting = tuple(group for group in clean if len(group) >= count.value)
    if fitting:
        return fitting, False
    spanned = tuple(core for group in clean for core in group)
    return ((spanned,) if len(spanned) >= count.value else ()), True


@dataclass(frozen=True, slots=True)
class Hooks:
    """The parts of a claim that a test replaces: the probe of a core, the wait between two tries, and the clock.

    clock gives the time of now in seconds since the epoch, for the age of the lines of the record of the steal time.
    """

    probe: Callable[[int], int | None] = probe_steal
    pause: Callable[[float], None] = time.sleep
    clock: Callable[[], float] = time.time


@dataclass(frozen=True, slots=True)
class Roots:
    """The directories that a claim reads and writes: the lock files, the CPUs of sysfs and the cgroup v2 hierarchy.

    lock_dir None is the directory of the main work tree.
    """

    lock_dir: Path | None = None
    cpu_root: Path = CPU_ROOT
    cgroup_root: Path = CGROUP_ROOT


# The roots and the hooks of a claim on the host.
HOST_ROOTS = Roots()
HOST_HOOKS = Hooks()


@contextlib.contextmanager
def claimed(
    settings: Settings, count: CoreCount = ONE_CORE, roots: Roots = HOST_ROOTS, hooks: Hooks = HOST_HOOKS
) -> Iterator[Placement]:
    """Hold count cores that no other bench holds for the block, and give their placement (the cores of a run).

    With no reserved core and no CRUCIBLE_BENCH_CORE, the block gets no pin and no lock.  `loop evidence run
    measurement` takes a claim for each run of a bench (assay.loop.evidence.measurement).

    Args:
        settings: The values of the environment
        count: The count of the cores of the claim
        roots: The directories of the locks, of sysfs and of the cgroups
        hooks: The probe, the wait and the clock

    Raises:
        BenchError: If read_reserved() refuses, if taskset is not on PATH, if CRUCIBLE_BENCH_CORE is not a CPU number,
            if the claim is larger than the reserved cores, or if too few cores lost no time to steal in their probes
    """
    reserved = read_reserved(settings.slice_name, roots.cpu_root, roots.cgroup_root)
    cores = cores_of(reserved, roots.cpu_root)
    probed = Probed(hooks.probe, reserved.source == "isolated")
    taskset = "taskset"
    if cores:
        found = shutil.which("taskset")
        if found is None:
            raise BenchError("taskset is not on PATH, and the benches run on a reserved core with it (util-linux)")
        taskset = found
    elif not settings.core:
        yield Placement((), reserved, ())
        return
    elif not settings.core.isdigit():
        raise BenchError(f"CRUCIBLE_BENCH_CORE is {settings.core!r}, and it must be the number of one CPU")
    else:
        cores, probed = (int(settings.core),), Probed(hooks.probe, False)
    if count.value > len(cores):
        where = f"the reserved cores {format_cpu_list(cores)}" if reserved.source else "CRUCIBLE_BENCH_CORE"
        raise BenchError(
            f"a claim of {count.value} cores is larger than the {len(cores)} cores of {where}, so its threads would "
            "share cores.  Give the bench fewer threads, or run it on a host with more reserved cores"
        )
    directory = roots.lock_dir if roots.lock_dir is not None else lock_dir_of()
    directory.mkdir(parents=True, exist_ok=True)
    record = directory / STEAL_RECORD
    groups = ranked_groups(cache_groups(cores, roots.cpu_root), read_steal_record(record, hooks.clock()))
    has_waited = has_spanned = False
    while True:
        pools, is_spanned = pools_of(groups, count, probed.stolen)
        if not pools:
            raise BenchError(
                f"only {len(cores) - len(probed.stolen)} of the reserved cores {format_cpu_list(cores)} lost no time "
                f"to steal in their probes, and the claim takes {count.value}"
            )
        if is_spanned and not has_spanned:
            listed = "; ".join(format_cpu_list(tuple(sorted(group))) for group in groups)
            log(
                f"a claim of {count.value} cores is larger than each group of cores that share one L3 cache "
                f"({listed}): it spans the groups, and its threads share no L3 cache"
            )
            has_spanned = True
        stolen_before = len(probed.stolen)
        for pool in pools:
            held = take_cores(pool, count, directory, probed)
            if held is None:
                continue
            with contextlib.ExitStack() as stack:
                for _, descriptor in held:
                    stack.callback(os.close, descriptor)
                placed = placement_on(tuple(core for core, _ in held), reserved, taskset)
                yield dataclasses.replace(placed, record=record)
            return
        if len(probed.stolen) > stolen_before:
            # A probe found steal, so the pools change: try them again at once, or refuse when none fits.
            continue
        if not has_waited:
            log(f"other benches hold the free cores of {format_cpu_list(cores)}: wait for {count.value} of them")
            has_waited = True
        hooks.pause(WAIT_S)


def steal_verdict(
    cores: tuple[int, ...], before: dict[int, int | None], after: dict[int, int | None]
) -> tuple[str, bool]:
    """Return the line of the steal time of one run and True when the run is valid by it (the steal time).

    A run with no pin, or cores with no line in the statistics, give a line that says so and True.
    """
    if not cores:
        return "steal time: not measured, the bench ran with no pin", True
    stolen = stolen_ms_of(before, after)
    if not stolen:
        return f"steal time: not measured, /proc/stat has no line for cpu{format_cpu_list(cores)}", True
    lost = {core: stolen_ms for core, stolen_ms in sorted(stolen.items()) if stolen_ms > 0}
    if lost:
        listed = ", ".join(f"cpu{core} {stolen_ms:.0f} ms" for core, stolen_ms in lost.items())
        return (
            f"steal time during the run: {listed}.  The hypervisor ran other work on the host cores, so each result "
            "of the run is NOT VALID"
        ), False
    return f"steal time on cpu{format_cpu_list(tuple(sorted(stolen)))}: 0 ms during the run", True


def uses_launcher(build_dir: Path, launcher: Path = caps.LAUNCHER) -> bool:
    """Tell if each bench of a build runs through the launcher, and print the reason when it does not (the capabilities).

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
        warn(
            "caps: each bench runs with no capabilities: the harness omits the eBPF lines, and the times do not change"
        )
        return False
    log(f"caps: each bench runs through {launcher}, so the harness attaches the eBPF lines")
    return True


def run(root: Path, request: Request, settings: Settings) -> int:
    """Build and run each bench of the request, through the launcher when it can, or list the bench targets.

    Args:
        root: The source tree
        request: The command line
        settings: The values of the environment

    Returns:
        The exit status

    Raises:
        BenchError: If a step cannot go on
    """
    roots = Roots(lock_dir_of(root))
    if request.command:
        with claimed(settings, request.cores or ONE_CORE, roots) as where:
            announce(where)
            log(f"run  {' '.join(request.command)}   (cpu {where.listed()})")
            return run_placed(request.command, where)
    build_dir = root / settings.build
    if not cmake_reader.is_configured(build_dir):
        raise BenchError(f"{build_dir} not configured — run: cmake --preset {settings.preset}")
    try:
        build_cmake = cmake_pin.configured_program(build_dir, "cmake")
    except cmake_pin.PinError:
        raise BenchError(
            f"{build_dir} names no cmake that exists — configure it again: cmake --preset {settings.preset}"
        ) from None
    if request.should_list:
        log(f"buildable bench targets in {build_dir}:")
        for name in bench_targets(read_model(build_dir)):
            print(name)
        return 0
    counts = core_counts(build_dir, request)
    is_launched = uses_launcher(build_dir)
    for target in request.targets:
        binary = build_dir / "bench" / target
        if request.should_build:
            log(f"build {target}")
            built = proc.run(
                proc.Command(
                    [build_cmake, "--build", str(build_dir), "--target", target, "-j", str(proc.DEFAULT_JOBS)],
                    timeout_s=proc.BUILD_S,
                    stdout=proc.Output.DISCARD,
                    stderr=proc.Output.INHERIT,
                )
            )
            if built.exit != 0:
                raise BenchError(f"build failed: {target} (known-drifted bench? try another target)")
        if not os.access(binary, os.X_OK):
            raise BenchError(f"missing binary: {binary} (build it first, drop --no-build)")
        try:
            refusal = x86level.bench_refusal(build_dir, target)
        except x86level.LevelError as problem:
            raise BenchError(f"the x86-64 level of the host cannot be read: {problem}") from None
        if refusal is not None:
            warn(refusal)
            continue
        words = (str(binary), *request.bench_args)
        with claimed(settings, counts.get(target, ONE_CORE), roots) as where:
            announce(where)
            log(f"run  {target}   (cpu {where.listed()})")
            status = run_placed(tuple(caps.wrapped(words)) if is_launched else words, where)
        if status != 0:
            return status
        print()
    return 0


def read_model(build_dir: Path) -> cmake_reader.CodeModel:
    """Return the code model of a build.

    Raises:
        BenchError: If the code model cannot be read
    """
    try:
        return cmake_reader.codemodel(build_dir)
    except cmake_reader.CmakeError as problem:
        raise BenchError(str(problem)) from None


def core_counts(build_dir: Path, request: Request) -> dict[str, CoreCount]:
    """Return the count of the cores of each target of a request: the count of --cores, or the count of the bench.

    Complexity: one read of the code model when --cores is not given (the cores of a run).

    Raises:
        BenchError: If the code model cannot be read, or if a definition of a count does not read
    """
    if request.cores is not None:
        return dict.fromkeys(request.targets, request.cores)
    wanted = frozenset(request.targets)
    return {target.name: cores_of_target(target) for target in read_model(build_dir).targets if target.name in wanted}


def announce(where: Placement) -> None:
    """Print the reservation and the cores of a claim, and warn when the results are noisy."""
    reserved = where.reserved
    listed = format_cpu_list(reserved.cpus)
    if reserved.source == "partition":
        log(
            f"reserved partition {reserved.slice_name} on cpus {listed}: the run takes the cpus {where.listed()} in "
            "the slice, and the SMT sibling of each stays idle"
        )
    elif reserved.source == "isolated" and where.cores:
        log(f"isolated cpus {listed}: the run takes the cpus {where.listed()}")
    elif reserved.cpus:
        warn(f"the reserved cpus {listed} hold no full SMT group: the SMT sibling of a core can run other work")
    if not where.cores:
        warn(
            "the host reserves no cpu, and CRUCIBLE_BENCH_CORE is not set: each bench runs with no pin, and its "
            "results are NOISY"
        )
    elif not reserved.source:
        warn(
            f"the host reserves no cpu: each bench runs on cpu{where.listed()} of CRUCIBLE_BENCH_CORE, a shared cpu "
            "with a tick, and its results are NOISY"
        )


def run_placed(words: tuple[str, ...], where: Placement) -> int:
    """Run a program on the cores of a placement, and print the steal time of each core during the run.

    Returns:
        The exit status of the program
    """
    before = read_steals(where.cores)
    status = proc.run(
        proc.Command(
            list(where.command(words)), timeout_s=proc.BUILD_S, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT
        )
    ).exit
    after = read_steals(where.cores)
    line, is_valid = steal_verdict(where.cores, before, after)
    if is_valid:
        log(line)
    else:
        warn(line)
        note_steal(where, before, after)
    return status


# ── The self-test ──────────────────────────────────────────────────────────


class Waited(Exception):
    """The wait of a claim of the self-test when other holders have too many cores."""


def stop_waiting(seconds: float) -> None:
    """Stop a claim of the self-test at its first wait."""
    raise Waited(seconds)


def l3_cases(expect: Callable[[str, bool], None], plant: Callable[[Path, str], None], work: Path, now: float) -> None:
    """Check the claims of several cores on a planted host with the shape of the bench VM (the cores of a run).

    The host has the partition bench.slice on the even CPUs 224 to 254, with each odd SMT sibling offline, and two L3
    groups of eight cores: 224 to 238 and 240 to 254.
    """
    cpu_root, cgroup_root, locks = work / "cpu", work / "cgroup", work / "locks"
    group_a, group_b = tuple(range(224, 240, 2)), tuple(range(240, 256, 2))
    for cpu in (*group_a, *group_b):
        plant(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list", f"{cpu}\n")
        plant(cpu_root / f"cpu{cpu}" / "cache" / "index0" / "level", "1\n")
        plant(cpu_root / f"cpu{cpu}" / "cache" / "index0" / "shared_cpu_list", f"{cpu}\n")
        plant(cpu_root / f"cpu{cpu}" / "cache" / "index3" / "level", "3\n")
        shared = group_a if cpu in group_a else group_b
        plant(cpu_root / f"cpu{cpu}" / "cache" / "index3" / "shared_cpu_list", f"{format_cpu_list(shared)}\n")
    plant(cpu_root / "isolated", f"{format_cpu_list((*group_a, *group_b))}\n")
    plant(cgroup_root / "bench.slice" / "cpuset.cpus.partition", "isolated\n")
    plant(cgroup_root / "bench.slice" / "cpuset.cpus.exclusive.effective", f"{format_cpu_list((*group_a, *group_b))}\n")

    def claim(count: int, lock_dir: Path = locks) -> contextlib.AbstractContextManager[Placement]:
        hooks = Hooks(lambda core: 0, stop_waiting, lambda: now)
        return claimed(Settings("b", "p"), CoreCount(count), Roots(lock_dir, cpu_root, cgroup_root), hooks)

    expect(
        "the cache of level 3 gives two groups of eight cores, and the cache of level 1 does not count",
        cache_groups((*group_a, *group_b), cpu_root) == (group_a, group_b),
    )
    with claim(2) as first, claim(2) as second:
        expect(
            f"two claims of two cores at the same time take disjoint cores of one L3 group: {first.cores} "
            f"{second.cores}",
            first.cores == (224, 226)
            and second.cores == (228, 230)
            and first.prefix[-2:] == ("-c", "224,226")
            and "--slice=bench.slice" in first.prefix,
        )
    with claim(6) as held, claim(8) as whole:
        expect(
            "a claim of eight cores skips the group in which another claim holds six, and takes the other group",
            held.cores == group_a[:6] and whole.cores == group_b,
        )
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed), claim(10) as spanned:
        pass
    expect(
        "a claim larger than an L3 group spans the groups, the first group whole, and prints it",
        spanned.cores == (*group_a, 240, 242) and "spans the groups" in printed.getvalue(),
    )
    try:
        with claim(17):
            expect("a claim larger than the partition is refused with a text", False)
    except BenchError as problem:
        expect(
            "a claim larger than the partition is refused with a text that gives the two counts",
            "17 cores" in str(problem) and "16 cores" in str(problem),
        )
    with claim(6) as left, claim(6) as right:
        try:
            with claim(4):
                expect("a claim that fits in one group waits when no group has room, and does not span", False)
        except Waited:
            expect(
                "a claim that fits in one group waits when no group has room, and does not span",
                left.cores == group_a[:6] and right.cores == group_b[:6],
            )
    ranked = work / "ranked"
    record_steal(ranked / STEAL_RECORD, 226, 20.0, now - 60)
    with claim(2, ranked) as clean:
        expect(
            "a group with a core of the record of the steal time comes after a clean group", clean.cores == (240, 242)
        )
    noisy = work / "noisy"
    for minutes in (1, 2, 3):
        record_steal(noisy / STEAL_RECORD, 224, 20.0, now - 60 * minutes)
    record_steal(noisy / STEAL_RECORD, 230, 20.0, now - 60)
    with contextlib.redirect_stdout(io.StringIO()), claim(16, noisy) as whole:
        pass
    expected = (*group_b, 226, 228, 232, 234, 236, 238, 230, 224)
    expect(
        "a claim of 16 cores orders its cores by the record: the clean group first, then the fewest lines, so cpu224 "
        f"with the most lines comes last: {whole.order}",
        whole.cores == tuple(sorted(expected))
        and whole.order == expected
        and f"{ORDER_VARIABLE}={','.join(str(core) for core in expected)}" in whole.prefix
        and whole.prefix.index(ENV_PROGRAM) < whole.prefix.index("-c"),
    )
    plant(cpu_root / "cpu256" / "topology" / "thread_siblings_list", "256\n")
    expect(
        "a core with no L3 cache in sysfs is a group of its own when other cores have one",
        cache_groups((224, 226, 256), cpu_root) == ((224, 226), (256,)),
    )


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

    expect(
        "no target gives the default set, and the words after -- go to each bench",
        parse(["--", "--iterations", "5"]) == Request(True, False, DEFAULT_TARGETS, ("--iterations", "5")),
    )
    expect(
        "--no-build and two targets",
        parse(["--no-build", "bench_arena", "bench_philox"])
        == Request(False, False, ("bench_arena", "bench_philox"), ()),
    )
    try:
        parse(["--fast"])
        expect("an unknown option is refused", False)
    except BenchError:
        expect("an unknown option is refused", True)
    expect(
        "--cores gives the count of each bench, and --command runs the words after -- with no build",
        parse(["--cores", "2", "bench_arena"]) == Request(True, False, ("bench_arena",), (), CoreCount(2))
        and parse(["--cores", "2", "--command", "--", "python3", "-V"])
        == Request(False, False, (), (), CoreCount(2), ("python3", "-V")),
    )
    for words in (["--cores"], ["--cores", "x"], ["--cores", "0"], ["--command"], ["--command", "bench_arena"]):
        try:
            parse(words)
            expect(f"the command line {words} is refused", False)
        except BenchError:
            expect(f"the command line {words} is refused", True)
    stated = cmake_reader.Target("bench_two", TARGET_TYPE, folder=BENCH_FOLDER, defines=("A", f"{CORES_DEFINE}=2"))
    plain = cmake_reader.Target("bench_one", TARGET_TYPE, folder=BENCH_FOLDER, defines=("A",))
    broken = cmake_reader.Target("bench_bad", TARGET_TYPE, folder=BENCH_FOLDER, defines=(f"{CORES_DEFINE}=two",))
    expect(
        "a bench states its cores with its definition, and a bench with no definition gets one core",
        cores_of_target(stated) == CoreCount(2) and cores_of_target(plain) == ONE_CORE,
    )
    try:
        cores_of_target(broken)
        expect("a definition of the cores that is no count is refused, with the name of the bench", False)
    except BenchError as problem:
        expect(
            "a definition of the cores that is no count is refused, with the name of the bench",
            "bench_bad" in str(problem),
        )
    model = cmake_reader.CodeModel(
        "/source",
        "/build",
        (
            cmake_reader.Target("bench_trace_ring", TARGET_TYPE, folder=BENCH_FOLDER),
            cmake_reader.Target("bench_arena", TARGET_TYPE, folder=BENCH_FOLDER),
            cmake_reader.Target("bench_arena", TARGET_TYPE, folder=BENCH_FOLDER),
            cmake_reader.Target("bench_unmarked", TARGET_TYPE),
            cmake_reader.Target("bench_harness", "STATIC_LIBRARY", folder=BENCH_FOLDER),
            cmake_reader.Target("bench_report", "UTILITY"),
            cmake_reader.Target("bench_philox", TARGET_TYPE, folder=BENCH_FOLDER),
        ),
    )
    expect(
        "the code model gives each executable of the bench folder one time, and no library and no other target",
        bench_targets(model) == ("bench_arena", "bench_philox", "bench_trace_ring"),
    )
    expect(
        "a CPU list gives its sorted CPUs, and an empty list gives none",
        parse_cpu_list("8,0-3\n") == (0, 1, 2, 3, 8) and parse_cpu_list("\n") == (),
    )
    expect(
        "the printed list gives the exact cpus: a run of consecutive cpus as a range, and the even cpus one by one",
        format_cpu_list((0, 1, 2, 3, 8)) == "0-3,8"
        and format_cpu_list(tuple(range(224, 256, 2))) == ",".join(str(cpu) for cpu in range(224, 256, 2))
        and format_cpu_list(()) == "",
    )
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

        locks = Path(work, "locks")
        # The time of now of the test: 2026-11-16T13:20:00Z, a little after the lines of the record below.
        now = 1794835200.0

        def no_steal(core: int) -> int | None:
            del core
            return 0

        def claim(
            settings: Settings,
            probe: Callable[[int], int | None] = no_steal,
            pause: Callable[[float], None] = time.sleep,
            count: CoreCount = ONE_CORE,
        ) -> contextlib.AbstractContextManager[Placement]:
            return claimed(settings, count, Roots(locks, cpu_root, cgroup_root), Hooks(probe, pause, lambda: now))

        for cpu in range(220, 256):
            plant(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list", f"{cpu & ~1}-{cpu | 1}\n")
        plant(cpu_root / "isolated", "\n")
        with claim(Settings("b", "p")) as where:
            expect("with no reservation and no CRUCIBLE_BENCH_CORE, no pin", where == Placement((), Reserved(), ()))
        with claim(Settings("b", "p", "5")) as where:
            expect(
                "with no reservation, CRUCIBLE_BENCH_CORE is the core, and the order of the cores names it",
                where.prefix == (ENV_PROGRAM, f"{ORDER_VARIABLE}=5", "taskset", "-c", "5") and where.order == (5,),
            )
        try:
            with claim(Settings("b", "p", "cpu5")):
                expect("a CRUCIBLE_BENCH_CORE that is no CPU number is refused", False)
        except BenchError as problem:
            expect("a CRUCIBLE_BENCH_CORE that is no CPU number is refused", "CRUCIBLE_BENCH_CORE" in str(problem))
        try:
            with claim(Settings("b", "p", "5"), count=CoreCount(2)):
                expect("a claim of two cores on the one core of CRUCIBLE_BENCH_CORE is refused", False)
        except BenchError as problem:
            expect(
                "a claim of two cores on the one core of CRUCIBLE_BENCH_CORE is refused",
                "CRUCIBLE_BENCH_CORE" in str(problem) and "2 cores" in str(problem),
            )
        plant(
            cgroup_root / "bad.slice" / "cpuset.cpus.partition",
            "root invalid (Cpu list in cpuset.cpus not exclusive)\n",
        )
        plant(cgroup_root / "bad.slice" / "cpuset.cpus.effective", "0-3\n")
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.partition", "root\n")
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.exclusive.effective", "224-255\n")
        with claim(Settings("b", "p", "5")) as where:
            expect(
                "a valid partition is the reservation, and the kernel's invalid one is not",
                where.reserved == Reserved(tuple(range(224, 256)), "partition", "reserved.slice"),
            )
            expect(
                "a partition starts the bench in its slice, on the lowest CPU of a full SMT group",
                where.cores == (224,)
                and "--slice=reserved.slice" in where.prefix
                and where.prefix[-2:] == ("-c", "224")
                and where.command(("bench",))[-1] == "bench",
            )
            launched = where.command(tuple(caps.wrapped(("bench",))))
            expect(
                "the launcher comes after the sudo of a partition, which clears the ambient set",
                launched.index(str(caps.LAUNCHER)) > launched.index("sudo") and launched[-1] == "bench",
            )
        plant(cgroup_root / "reserved.slice" / "cpuset.cpus.exclusive.effective", "225-255\n")
        expect(
            "a half SMT group is skipped, so each core has an idle sibling",
            cores_of(read_reserved("", cpu_root, cgroup_root), cpu_root)[:2] == (226, 228),
        )
        plant(cgroup_root / "other.slice" / "cpuset.cpus.partition", "isolated\n")
        plant(cgroup_root / "other.slice" / "cpuset.cpus.exclusive.effective", "220-223\n")
        try:
            read_reserved("", cpu_root, cgroup_root)
            expect("two partitions with no CRUCIBLE_BENCH_SLICE are refused", False)
        except BenchError as problem:
            expect("two partitions with no CRUCIBLE_BENCH_SLICE are refused", "CRUCIBLE_BENCH_SLICE" in str(problem))
        expect(
            "CRUCIBLE_BENCH_SLICE chooses one of two partitions",
            read_reserved("other.slice", cpu_root, cgroup_root).cpus == (220, 221, 222, 223),
        )
        both = Path(work, "cgroup-both")
        plant(both / "bench.slice" / "cpuset.cpus.partition", "isolated\n")
        plant(both / "bench.slice" / "cpuset.cpus.exclusive.effective", "224-255\n")
        plant(cpu_root / "isolated", "224-255\n")
        printed = io.StringIO()
        with contextlib.redirect_stderr(printed):
            agreed = read_reserved("", cpu_root, both)
        expect(
            "a valid partition wins over the isolated file, and the same cpus print nothing",
            agreed == Reserved(tuple(range(224, 256)), "partition", "bench.slice") and not printed.getvalue(),
        )
        plant(cpu_root / "isolated", "220-227\n")
        printed = io.StringIO()
        with contextlib.redirect_stderr(printed):
            differed = read_reserved("", cpu_root, both)
        expect(
            "a partition and an isolated file that differ give the partition and print the two lists",
            differed.source == "partition" and "[220, 221" in printed.getvalue() and "[224, 225" in printed.getvalue(),
        )
        refused = Path(work, "cgroup-refused")
        plant(refused / "bench.slice" / "cpuset.cpus.partition", "isolated invalid (Cpu list is not exclusive)\n")
        plant(refused / "bench.slice" / "cpuset.cpus.exclusive.effective", "224-255\n")
        expect(
            "a partition that the kernel refuses gives the isolated file",
            read_reserved("", cpu_root, refused) == Reserved(tuple(range(220, 228)), "isolated"),
        )
        # The cases of the isolated file alone, with no partition.
        cgroup_root = Path(work, "cgroup-none")
        with claim(Settings("b", "p")) as where:
            expect(
                "isolcpus with no partition: taskset alone puts the bench on its core",
                where.reserved.source == "isolated" and where.cores == (220,) and "sudo" not in where.prefix,
            )
            with claim(Settings("b", "p")) as second:
                expect("two claims at the same time take two cores", second.cores == (222,))
        expect(
            "a sysfs with no L3 cache gives one group of each core",
            cache_groups((220, 222, 224, 226), cpu_root) == ((220, 222, 224, 226),),
        )
        with claim(Settings("b", "p"), lambda core: 2 if core == 220 else 0) as where:
            expect("a core that loses time to steal in its probe is skipped", where.cores == (222,))
        probed: list[int] = []

        def steal_on_222(core: int) -> int | None:
            probed.append(core)
            return 2 if core == 222 else 0

        with claim(Settings("b", "p"), steal_on_222, count=CoreCount(2)) as where:
            expect(
                "a claim of two cores probes each core, and a core with steal does not join it",
                where.cores == (220, 224) and probed == [220, 222, 224] and where.prefix[-2:] == ("-c", "220,224"),
            )
        try:
            with claim(Settings("b", "p"), lambda core: 1):
                expect("each core with steal time is refused", False)
        except BenchError as problem:
            expect("each core with steal time is refused", "steal" in str(problem))

        with claim(Settings("b", "p")), claim(Settings("b", "p")), claim(Settings("b", "p")), claim(Settings("b", "p")):
            try:
                with claim(Settings("b", "p"), pause=stop_waiting):
                    expect("a claim waits while another holder has each core", False)
            except Waited:
                expect("a claim waits while another holder has each core", True)
        stat = Path(work, "stat")
        plant(stat, "cpu  9 0 9 9 0 0 0 7 0 0\ncpu220 1 2 3 4 5 6 7 8 0 0\ncpu222 1 2 3 4 5 6 7 x 0 0\n")
        expect(
            "the steal column of a CPU reads, and a CPU with no line or no number reads as None",
            read_steal_ticks(220, stat) == 8
            and read_steal_ticks(222, stat) is None
            and read_steal_ticks(224, stat) is None,
        )
        expect(
            "the steal ticks of the cores of a claim read one by one",
            read_steals((220, 222, 224), stat) == {220: 8, 222: None, 224: None},
        )
        line, is_valid = steal_verdict((220,), {220: 8}, {220: 8})
        expect("a run with no steal time is valid, and its line gives 0 ms", is_valid and "0 ms" in line)
        line, is_valid = steal_verdict((224,), {224: 8}, {224: 11})
        expect("a run with steal time is not valid, and its line says so", not is_valid and "NOT VALID" in line)
        line, is_valid = steal_verdict((224, 226), {224: 8, 226: 3}, {224: 8, 226: 5})
        expect(
            "a run with steal time on one core of two is not valid, and its line names that core",
            not is_valid and "NOT VALID" in line and "cpu226" in line and "cpu224" not in line,
        )
        expect("a run with no pin is valid, with no steal time", steal_verdict((), {}, {})[1])
        record = locks / STEAL_RECORD
        record_steal(record, 220, 40.0, 1794835123.0)
        record_steal(record, 220, 10.0, 1794835124.0)
        record_steal(record, 224, 30.0, 1794835125.0)
        with open(record, "a", encoding="ascii") as handle:
            handle.write("a line that does not read\n")
        expect(
            "the record holds the date in UTC, the core and the steal time of each run, and a bad line counts nothing",
            read_text(record).splitlines()[0] == "2026-11-16T13:18:43Z cpu220 40 ms"
            and read_steal_record(record, now) == {220: 2, 224: 1},
        )
        aged = Path(work, "aged.txt")
        record_steal(aged, 230, 20.0, now - 25 * 3600)
        record_steal(aged, 232, 20.0, now - 23 * 3600)
        expect(
            "a line older than 24 hours counts nothing, and a line of the last 24 hours counts",
            read_steal_record(aged, now) == {232: 1},
        )
        with claim(Settings("b", "p")) as where:
            expect(
                "a claim tries the cores with a clean record first", where.cores == (222,) and where.record == record
            )
            with claim(Settings("b", "p")) as second, claim(Settings("b", "p")) as third:
                expect(
                    "the cores of the record come last, from the fewest lines",
                    second.cores == (226,) and third.cores == (224,),
                )
        note_steal(Placement((226, 228), Reserved(), (), record), {226: 5, 228: 1}, {226: 7, 228: 4})
        note_steal(Placement((226,), Reserved(), (), record), {226: 7}, {226: 7})
        note_steal(Placement((226,), Reserved(), ()), {226: 5}, {226: 9})
        expect(
            "a run with steal time on a claim adds one line for each core with steal, and a clean run or no claim "
            "adds none",
            read_steal_record(record, now).get(226) == 1 and read_steal_record(record, now).get(228) == 1,
        )
        l3_cases(expect, plant, Path(work, "l3"), now)
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
