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
       host that no other bench holds (THE CORE OF A RUN).  When the host
       reserves no CPU, it uses the core of CRUCIBLE_BENCH_CORE, or no pin when
       that variable is not set.  It then warns that the results are noisy.
    4. It prints the steal time of the core during the run, and it marks a
       run with steal time as not valid (THE STEAL TIME).

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

THE CORE OF A RUN
    Two benches on one core change the times of the two, so each command
    holds its core alone.  claimed() tries first the cores with no line in
    the record of THE STEAL TIME, in the order of their numbers, and then
    each core of the record, from the fewest lines.  It takes a core when an
    exclusive lock of
    flock(2) on the file cpuN.lock of CORE_LOCK_DIR in the main work tree
    takes with no wait, and it holds the lock for each run of the command.
    On a host that isolates its CPUs, the command then runs a busy loop on
    the core for PROBE_S seconds.  A core that loses time to steal in the
    probe shares its host core with other work, such as an emulator thread of
    the hypervisor, so the command frees it and tries the next core.  When
    another bench holds each other core, the command waits for one, and it
    tries the cores again each WAIT_S seconds.  The core of
    CRUCIBLE_BENCH_CORE takes the same lock and no probe.

THE STEAL TIME
    The column steal of the line of the core in /proc/stat counts the time
    in which the hypervisor ran other work while the core had work, in ticks
    of the clock of the kernel.  The command reads it before and after each
    run, and it prints the steal time in milliseconds after the output of
    the bench.  A run with steal time above zero is not valid, as a report
    with a variation above 5 % is not, and the line says so.  A run with no
    pin prints no steal time.

    Each run with steal time adds one line to the record STEAL_RECORD in
    CORE_LOCK_DIR: the date in UTC, the core and the steal time, as
    "2026-11-16T15:04:05Z cpu228 40 ms".  A probe of the claim adds no line,
    because its core is skipped at once.  A short probe misses a steal rate
    below about 1 %, and the record keeps such a core behind the clean
    cores.  Only a line of the last 24 hours counts, so a core gets its place
    back one day after its last run with steal.  To clear the record of a
    host, delete the file.

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
    latency set: bench_trace_ring, bench_swiss_table, bench_philox and
    bench_pool_allocator.

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
from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.loop import caps, heavy
from assay.readers import cmake as cmake_reader
from assay.setup import pins as cmake_pin

DEFAULT_BUILD = "build-bench"
DEFAULT_PRESET = "bench"
# The cache entry that tells if the build loads BPF programs (THE CAPABILITIES).  cmake/CrucibleBpf.cmake writes it.
BPF_ENTRY = "CRUCIBLE_HAVE_BPF"
# The hot-path latency set.  bench_dispatch and bench_meta_log are not in it, because their builds drift.
DEFAULT_TARGETS = ("bench_trace_ring", "bench_swiss_table", "bench_philox", "bench_pool_allocator")
# The CPU directory of sysfs and the root of the cgroup v2 hierarchy (THE RESERVED CPUS).
CPU_ROOT = Path("/sys/devices/system/cpu")
CGROUP_ROOT = Path("/sys/fs/cgroup")
# The values of cpuset.cpus.partition of a valid partition.  The kernel writes "root invalid (REASON)" for a partition
# that it refuses.
PARTITION_TYPES = frozenset({"root", "isolated"})
# The program that starts a command in a slice, by its full path.  sudo can give the command a different PATH.
SYSTEMD_RUN = "/usr/bin/systemd-run"
# The directory of the lock file of each core, relative to the main work tree (THE CORE OF A RUN).
CORE_LOCK_DIR = Path(".worktrees/.grun/bench-cores")
# The time of the busy loop that probes a core for steal time, in seconds.  A core that loses 2 % of its time to the
# hypervisor loses two ticks of 10 ms in it (THE CORE OF A RUN).
PROBE_S = 1.0
# The interval between two tries of the cores when another bench holds each one, in seconds.
WAIT_S = 2.0
# The statistics of the kernel with the steal column of each CPU (THE STEAL TIME).
STAT_FILE = Path("/proc/stat")
# The index of the column steal in the line of a CPU of STAT_FILE, after the name of the CPU.
STEAL_COLUMN = 8
# The record of the runs with steal time, in CORE_LOCK_DIR (THE STEAL TIME).
STEAL_RECORD = "steal-record.txt"
# The age of the oldest line of the record that counts, in seconds: 24 hours (THE STEAL TIME).
RECORD_AGE_S = 24 * 3600
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
        return cls(
            os.environ.get("CRUCIBLE_BENCH_BUILD", DEFAULT_BUILD),
            os.environ.get("CRUCIBLE_BENCH_PRESET", DEFAULT_PRESET),
            os.environ.get("CRUCIBLE_BENCH_CORE", ""),
            os.environ.get("CRUCIBLE_BENCH_SLICE", ""),
        )


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

    core is empty when the bench runs with no pin.  prefix holds the words that start the program on the core.  record
    is the record of the runs with steal time of the claim, or None for a placement of no claim (THE STEAL TIME).
    """

    core: str
    reserved: Reserved
    prefix: tuple[str, ...]
    record: Path | None = None

    def command(self, words: tuple[str, ...]) -> tuple[str, ...]:
        """Return the words that run a program and its arguments on the core of the placement."""
        return (*self.prefix, *words)

    def facts(self) -> tuple[tuple[str, str], ...]:
        """Return the facts of the placement for a sample of the evidence store: the core and the reservation."""
        return (
            ("core", self.core or "none"),
            ("reserved", self.reserved.source or "no"),
            ("slice", self.reserved.slice_name or "none"),
        )


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
            bench_args = tuple(argv[index + 1 :])
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


def placement_on(core: int, reserved: Reserved, taskset: str) -> Placement:
    """Return the placement of each bench on one core of the reserved CPUs, in the slice of a partition."""
    pin = (taskset, "-c", str(core))
    if reserved.source != "partition":
        return Placement(str(core), reserved, pin)
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
    return Placement(str(core), reserved, (*scope, *pin))


def read_steal_ticks(core: int, stat_file: Path = STAT_FILE) -> int | None:
    """Return the steal column of the line of a CPU of the kernel statistics, or None when the file has no such line.

    Complexity: linear in the number of lines of the file (THE STEAL TIME).
    """
    label = f"cpu{core}"
    for line in read_text(stat_file).splitlines():
        words = line.split()
        if words and words[0] == label and len(words) > STEAL_COLUMN and words[STEAL_COLUMN].isdigit():
            return int(words[STEAL_COLUMN])
    return None


def tick_ms() -> float:
    """Return the length of one tick of the kernel statistics in milliseconds."""
    return 1000.0 / os.sysconf("SC_CLK_TCK")


def probe_steal(core: int, stat_file: Path = STAT_FILE, seconds: float = PROBE_S) -> int | None:
    """Run a busy loop on a core for the seconds, and return the steal ticks of the core in that time.

    The calling thread runs on the core for the probe, and then it runs on its CPUs of before.  Returns None when the
    thread cannot run on the core, or when the statistics have no line for it (THE CORE OF A RUN).
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
    """Return the directory of the lock files of the cores, in the main work tree of a tree (THE CORE OF A RUN)."""
    return heavy.main_work_tree(root) / CORE_LOCK_DIR


def read_steal_record(record: Path, moment: float | None = None) -> dict[int, int]:
    """Return the count of the lines of each core in the record of the runs with steal time (THE STEAL TIME).

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
    """Add the line of one run with steal time to the record, under an exclusive lock of flock(2) (THE STEAL TIME).

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


def note_steal(where: Placement, before: int | None, after: int | None) -> None:
    """Add a run with steal time on the core of a claim to the record of the claim, and nothing for each other run."""
    if where.record is None or not where.core or before is None or after is None or after <= before:
        return
    record_steal(where.record, int(where.core), (after - before) * tick_ms())


@contextlib.contextmanager
def claimed(
    settings: Settings,
    lock_dir: Path | None = None,
    cpu_root: Path = CPU_ROOT,
    cgroup_root: Path = CGROUP_ROOT,
    probe: Callable[[int], int | None] = probe_steal,
    pause: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.time,
) -> Iterator[Placement]:
    """Hold a core that no other bench holds for the block, and give its placement (THE CORE OF A RUN).

    With no reserved core and no CRUCIBLE_BENCH_CORE, the block gets no pin and no lock.  `loop evidence run
    measurement` runs its benches in the same claim (assay.loop.evidence.measurement).

    Args:
        settings: The values of THE ENVIRONMENT
        lock_dir: The directory of the lock files, or None for the directory of the main work tree
        cpu_root: The CPU directory of sysfs
        cgroup_root: The root of the cgroup v2 hierarchy
        probe: The steal ticks of a core in a busy loop, or None when the probe cannot run there
        pause: The wait between two tries of the cores
        clock: The time of now in seconds since the epoch, for the age of the lines of the record

    Raises:
        BenchError: If read_reserved() refuses, if taskset is not on PATH, if CRUCIBLE_BENCH_CORE is not a CPU number,
            or if each reserved core loses time to steal in its probe
    """
    reserved = read_reserved(settings.slice_name, cpu_root, cgroup_root)
    cores = cores_of(reserved, cpu_root)
    is_probed = reserved.source == "isolated"
    taskset = "taskset"
    if cores:
        found = shutil.which("taskset")
        if found is None:
            raise BenchError("taskset is not on PATH, and the benches run on a reserved core with it (util-linux)")
        taskset = found
    elif not settings.core:
        yield Placement("", reserved, ())
        return
    elif not settings.core.isdigit():
        raise BenchError(f"CRUCIBLE_BENCH_CORE is {settings.core!r}, and it must be the number of one CPU")
    else:
        cores, is_probed = (int(settings.core),), False
    directory = lock_dir if lock_dir is not None else lock_dir_of()
    directory.mkdir(parents=True, exist_ok=True)
    record = directory / STEAL_RECORD
    counts = read_steal_record(record, clock())
    cores = tuple(sorted(cores, key=lambda core: (counts.get(core, 0), core)))
    stolen_cores: set[int] = set()
    has_waited = False
    while True:
        for core in cores:
            if core in stolen_cores:
                continue
            with open(directory / f"cpu{core}.lock", "a", encoding="ascii") as handle:
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    continue
                stolen = probe(core) if is_probed else None
                if stolen:
                    warn(
                        f"cpu{core} lost {stolen * tick_ms():.0f} ms to steal in a busy loop of {PROBE_S:g} s: its "
                        "host core runs other work, so the benches skip it"
                    )
                    stolen_cores.add(core)
                    continue
                yield dataclasses.replace(placement_on(core, reserved, taskset), record=record)
                return
        if len(stolen_cores) == len(cores):
            raise BenchError(f"each reserved core of {list(cores)} lost time to steal in its probe")
        if not has_waited:
            log(f"another bench holds each free core of {list(cores)}: wait for one")
            has_waited = True
        pause(WAIT_S)


def steal_verdict(core: str, before: int | None, after: int | None) -> tuple[str, bool]:
    """Return the line of the steal time of one run and True when the run is valid by it (THE STEAL TIME).

    A run with no pin, or a core with no line in the statistics, gives a line that says so and True.
    """
    if not core:
        return "steal time: not measured, the bench ran with no pin", True
    if before is None or after is None:
        return f"steal time: not measured, /proc/stat has no line for cpu{core}", True
    stolen_ms = (after - before) * tick_ms()
    if stolen_ms > 0:
        return (
            f"steal time on cpu{core}: {stolen_ms:.0f} ms during the run.  The hypervisor ran other work on the "
            "host core, so each result of the run is NOT VALID"
        ), False
    return f"steal time on cpu{core}: 0 ms during the run", True


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
        raise BenchError(
            f"{build_dir} names no cmake that exists — configure it again: cmake --preset {settings.preset}"
        ) from None
    if request.should_list:
        log(f"buildable bench targets in {build_dir}:")
        try:
            model = cmake_reader.codemodel(build_dir)
        except cmake_reader.CmakeError as problem:
            raise BenchError(str(problem)) from None
        for name in bench_targets(model):
            print(name)
        return 0
    with claimed(settings, lock_dir_of(root)) as where:
        return run_claimed(build_dir, build_cmake, request, where)


def run_claimed(build_dir: Path, build_cmake: str, request: Request, where: Placement) -> int:
    """Build and run each bench of the request on the claimed core, and print the steal time of each run.

    Returns:
        The exit status

    Raises:
        BenchError: If a step cannot go on
    """
    reserved = where.reserved
    listed = format_cpu_list(reserved.cpus)
    if reserved.source == "partition":
        log(
            f"reserved partition {reserved.slice_name} on cpus {listed}: each bench runs in the slice on "
            f"cpu{where.core}, and its SMT sibling stays idle"
        )
    elif reserved.source == "isolated" and where.core:
        log(f"isolated cpus {listed}: each bench runs on cpu{where.core}")
    elif reserved.cpus:
        warn(f"the reserved cpus {listed} hold no full SMT group: the SMT sibling of the core can run other work")
    if not where.core:
        warn(
            "the host reserves no cpu, and CRUCIBLE_BENCH_CORE is not set: each bench runs with no pin, and its "
            "results are NOISY"
        )
    elif not reserved.source:
        warn(
            f"the host reserves no cpu: each bench runs on cpu{where.core} of CRUCIBLE_BENCH_CORE, a shared cpu with "
            "a tick, and its results are NOISY"
        )
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
        log(f"run  {target}   (cpu {where.core or 'not pinned'})")
        words = (str(binary), *request.bench_args)
        launched = tuple(caps.wrapped(words)) if is_launched else words
        before = read_steal_ticks(int(where.core)) if where.core else None
        status = proc.run(
            proc.Command(
                list(where.command(launched)),
                timeout_s=proc.BUILD_S,
                stdout=proc.Output.INHERIT,
                stderr=proc.Output.INHERIT,
            )
        ).exit
        after = read_steal_ticks(int(where.core)) if where.core else None
        line, is_valid = steal_verdict(where.core, before, after)
        if is_valid:
            log(line)
        else:
            warn(line)
            note_steal(where, before, after)
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
        ) -> contextlib.AbstractContextManager[Placement]:
            return claimed(settings, locks, cpu_root, cgroup_root, probe, pause, lambda: now)

        for cpu in range(220, 256):
            plant(cpu_root / f"cpu{cpu}" / "topology" / "thread_siblings_list", f"{cpu & ~1}-{cpu | 1}\n")
        plant(cpu_root / "isolated", "\n")
        with claim(Settings("b", "p")) as where:
            expect("with no reservation and no CRUCIBLE_BENCH_CORE, no pin", where == Placement("", Reserved(), ()))
        with claim(Settings("b", "p", "5")) as where:
            expect("with no reservation, CRUCIBLE_BENCH_CORE is the core", where.prefix == ("taskset", "-c", "5"))
        try:
            with claim(Settings("b", "p", "cpu5")):
                expect("a CRUCIBLE_BENCH_CORE that is no CPU number is refused", False)
        except BenchError as problem:
            expect("a CRUCIBLE_BENCH_CORE that is no CPU number is refused", "CRUCIBLE_BENCH_CORE" in str(problem))
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
                where.core == "224"
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
                where.reserved.source == "isolated" and where.core == "220" and "sudo" not in where.prefix,
            )
            with claim(Settings("b", "p")) as second:
                expect("two claims at the same time take two cores", second.core == "222")
        with claim(Settings("b", "p"), lambda core: 2 if core == 220 else 0) as where:
            expect("a core that loses time to steal in its probe is skipped", where.core == "222")
        try:
            with claim(Settings("b", "p"), lambda core: 1):
                expect("each core with steal time is refused", False)
        except BenchError as problem:
            expect("each core with steal time is refused", "steal" in str(problem))

        class Waited(Exception):
            """The wait of a claim when another holder has each core."""

        def stop(seconds: float) -> None:
            raise Waited(seconds)

        with claim(Settings("b", "p")), claim(Settings("b", "p")), claim(Settings("b", "p")), claim(Settings("b", "p")):
            try:
                with claim(Settings("b", "p"), pause=stop):
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
        line, is_valid = steal_verdict("220", 8, 8)
        expect("a run with no steal time is valid, and its line gives 0 ms", is_valid and "0 ms" in line)
        line, is_valid = steal_verdict("224", 8, 11)
        expect("a run with steal time is not valid, and its line says so", not is_valid and "NOT VALID" in line)
        expect("a run with no pin is valid, with no steal time", steal_verdict("", None, None)[1])
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
            expect("a claim tries the cores with a clean record first", where.core == "222" and where.record == record)
            with claim(Settings("b", "p")) as second, claim(Settings("b", "p")) as third:
                expect(
                    "the cores of the record come last, from the fewest lines",
                    second.core == "226" and third.core == "224",
                )
        note_steal(Placement("226", Reserved(), (), record), 5, 7)
        note_steal(Placement("226", Reserved(), (), record), 7, 7)
        note_steal(Placement("226", Reserved(), ()), 5, 9)
        expect(
            "a run with steal time on a claim adds one line, and a clean run or no claim adds none",
            read_steal_record(record, now).get(226) == 1,
        )
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
