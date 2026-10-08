"""bench — build the benches, give them their capabilities and run each one on an isolated core (`loop bench`).

THE STEPS FOR EACH BENCH
    1. The command builds the bench in the build directory of the bench preset.
       The build is incremental, so a bench that is current builds nothing.
       --no-build omits the step.
    2. It gives the binary the capabilities of the CMake target bench-caps of
       bench/CMakeLists.txt again, after each build: a link writes a new file
       with no file capabilities.  With them, the eBPF lines of the bench
       harness (SenseHub and SchedSwitch) attach.  This host has
       perf_event_paranoid 2 and unprivileged_bpf_disabled 2, so the
       capabilities are necessary.  Without them, the harness omits those
       lines, and the times and the percentiles do not change.
         cap_bpf              bpf_prog_load of a tracepoint
         cap_perfmon          perf_event_open on a tracepoint ID
         cap_dac_read_search  a read of /sys/kernel/tracing/events/*/id and of the BTF, which only root can read
    3. It runs the bench on one isolated core with taskset: the first core of
       /sys/devices/system/cpu/isolated.  When the kernel isolates no core,
       it uses CRUCIBLE_BENCH_CORE and warns that the results are noisy.

    The command does not change the clocks of a GPU or the global boost of
    cpufreq.  crucible-benchtune.service sets the governor of each core, the
    offline SMT siblings and isolcpus at boot, and the command only uses them.

THE ENVIRONMENT
    CRUCIBLE_BENCH_BUILD   The build directory of the bench preset, relative to the tree.  The default is build-bench
    CRUCIBLE_BENCH_PRESET  The preset that configures it.  The default is bench
    CRUCIBLE_BENCH_CORE    The core when the kernel isolates no core.  The default is 88
    CRUCIBLE_BENCH_JOBS    The jobs of each incremental build.  The default is 8

Usage
    loop bench [--no-build] [TARGET]... [-- ARG...]   build and run each TARGET, or the default set
    loop bench --list                                  print the bench targets of the build directory
    guard selftest loop.bench

    Each ARG after -- goes to each bench.  The default set is the hot-path
    latency set: bench_trace_ring, bench_arena, bench_swiss_table,
    bench_philox and bench_pool_allocator.

Exit 0 when each bench passed, the exit status of the first bench that failed, 1 when a step failed, 2 on a usage
error or a failed self-test.
"""

from __future__ import annotations

import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.readers import cmake as cmake_reader
from assay.setup import pins as cmake_pin

DEFAULT_BUILD = "build-bench"
DEFAULT_PRESET = "bench"
DEFAULT_CORE = "88"
DEFAULT_JOBS = "8"
# The capabilities of the target bench-caps of bench/CMakeLists.txt.
CAPABILITIES = "cap_bpf,cap_perfmon,cap_dac_read_search=eip"
# The hot-path latency set.  bench_dispatch and bench_meta_log are not in it, because their builds drift.
DEFAULT_TARGETS = ("bench_trace_ring", "bench_arena", "bench_swiss_table", "bench_philox", "bench_pool_allocator")
ISOLATED = Path("/sys/devices/system/cpu/isolated")
# The distance from a core to its SMT sibling on this host.
SIBLING_DISTANCE = 192
TARGET_PREFIX = "bench_"
NAME_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyz_")


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
    """The values of THE ENVIRONMENT: the build directory relative to the tree, the preset, the core and the jobs."""

    build: str
    preset: str
    core: str
    jobs: str

    @classmethod
    def from_environment(cls) -> Settings:
        """Read the settings from the environment, with the default of each variable that is not set."""
        return cls(os.environ.get("CRUCIBLE_BENCH_BUILD", DEFAULT_BUILD),
                   os.environ.get("CRUCIBLE_BENCH_PRESET", DEFAULT_PRESET),
                   os.environ.get("CRUCIBLE_BENCH_CORE", DEFAULT_CORE),
                   os.environ.get("CRUCIBLE_BENCH_JOBS", DEFAULT_JOBS))


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


def bench_targets(help_text: str) -> list[str]:
    """Return each bench target that the help output of the build names: a word bench_ of lowercase letters and '_'.

    A word starts after a character that is not a letter, a digit or '_'.  Complexity: linear in the length of the text.

    Args:
        help_text: The output of `cmake --build DIR --target help`

    Returns:
        The targets, sorted and each one time
    """
    found: set[str] = set()
    start = help_text.find(TARGET_PREFIX)
    while start != -1:
        before = help_text[start - 1] if start > 0 else " "
        if not (before.isalnum() or before == "_"):
            end = start + len(TARGET_PREFIX)
            while end < len(help_text) and help_text[end] in NAME_CHARACTERS:
                end += 1
            if end > start + len(TARGET_PREFIX):
                found.add(help_text[start:end])
        start = help_text.find(TARGET_PREFIX, start + 1)
    return sorted(found)


def pin_core(isolated_text: str | None, fallback: str) -> tuple[str, bool]:
    """Return the core of the runs and whether the kernel isolates it.

    Args:
        isolated_text: The text of /sys/devices/system/cpu/isolated, or None when it cannot be read
        fallback: The core when the kernel isolates no core

    Returns:
        The first isolated core, and True; or the fallback, and False
    """
    text = (isolated_text or "").strip()
    if not text:
        return fallback, False
    first = text.split(",", 1)[0].split("-", 1)[0]
    return first, True


def apply_capabilities(binary: Path) -> None:
    """Give a bench binary the capabilities of bench-caps, unless it has them, with `sudo -n setcap`."""
    current = proc.run(proc.Command(["getcap", str(binary)], timeout_s=proc.QUICK_S)).out
    if CAPABILITIES in current:
        return
    if proc.run(proc.Command(["sudo", "-n", "setcap", CAPABILITIES, str(binary)], timeout_s=proc.QUICK_S)).exit == 0:
        log(f"  caps: {CAPABILITIES} applied (eBPF sensory line enabled)")
    else:
        warn("  caps: could not setcap (no passwordless sudo?) — eBPF sensory line")
        warn("        will be omitted; ns/percentiles are unaffected.")


def run(root: Path, request: Request, settings: Settings) -> int:
    """Build, give the capabilities and run each bench of the request, or list the bench targets.

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
        listed = proc.run(proc.Command([build_cmake, "--build", str(build_dir), "--target", "help"],
                                       timeout_s=proc.TOOL_S)).out
        for name in bench_targets(listed):
            print(name)
        return 0
    try:
        isolated_text: str | None = ISOLATED.read_text(encoding="ascii")
    except OSError:
        isolated_text = None
    core, is_isolated = pin_core(isolated_text, settings.core)
    if is_isolated:
        log(f"isolated partition active: [{(isolated_text or '').strip()}] → pinning to cpu{core}")
        sibling = Path(f"/sys/devices/system/cpu/cpu{int(core) + SIBLING_DISTANCE}/online")
        try:
            if sibling.read_text(encoding="ascii").strip() == "0":
                log(f"  (SMT sibling offline — cpu{core} runs single-threaded)")
        except OSError:
            pass
    else:
        warn("no isolated CPUs advertised (bench partition not active — reboot pending?).")
        warn(f"falling back to cpu{core}; results will be NOISY (shared, tick-active core).")
    for target in request.targets:
        binary = build_dir / "bench" / target
        if request.should_build:
            log(f"build {target}")
            built = proc.run(proc.Command([build_cmake, "--build", str(build_dir), "--target", target, "-j",
                                           settings.jobs], timeout_s=proc.BUILD_S, stdout=proc.Output.DISCARD,
                                          stderr=proc.Output.INHERIT))
            if built.exit != 0:
                raise BenchError(f"build failed: {target} (known-drifted bench? try another target)")
        if not os.access(binary, os.X_OK):
            raise BenchError(f"missing binary: {binary} (build it first, drop --no-build)")
        apply_capabilities(binary)
        log(f"run  {target}   (taskset -c {core})")
        status = proc.run(proc.Command(["taskset", "-c", core, str(binary), *request.bench_args],
                                       timeout_s=proc.BUILD_S, stdout=proc.Output.INHERIT,
                                       stderr=proc.Output.INHERIT)).exit
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
    help_text = ("All primary targets available:\nbench_arena: phony\nbench/bench_trace_ring: CXX_EXECUTABLE_LINKER\n"
                 "test_bench_arena: phony\nbench_philox.o: CXX\nbench_: phony\nall: phony\nbench_arena: phony\n")
    expect("the help output gives each bench target one time, also after a slash, and not inside another name",
           bench_targets(help_text) == ["bench_arena", "bench_philox", "bench_trace_ring"])
    expect("the first isolated core is the core of the runs",
           pin_core("90-95\n", "88") == ("90", True) and pin_core("92,94\n", "88") == ("92", True))
    expect("with no isolated core, the fallback core and a warning",
           pin_core("\n", "88") == ("88", False) and pin_core(None, "88") == ("88", False))
    with tempfile.TemporaryDirectory(prefix="loop-bench-") as work:
        try:
            run(Path(work), parse([]), Settings("absent", DEFAULT_PRESET, DEFAULT_CORE, DEFAULT_JOBS))
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
