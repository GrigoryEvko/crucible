"""gauge --cold — the cost of one clean build of the tree and of one run of each test group (`loop gauge --cold`).

THE MEASUREMENT
    The command configures a new build directory with no compiler cache, and
    it builds `all`.  Then it runs the tests in one ctest run, the negative
    fixtures with the other tests.  The census checks depend on the
    fixtures, so ctest runs them after the last fixture.  For each of the
    three steps (the configure, the build and the test run) it records the
    wall time, the user and the system CPU time, and the largest resident
    set of one process.  The CPU times come from wait4 of the step, so they
    include each process that the step waited for: a build includes each
    compiler run, and a test run includes each test.  At the end it prints
    the 20 compile steps that took the most time, from the ninja log of the
    build, and the 10 tests that took the most time, from the output of the
    test run.

    The configure and the build run with CCACHE_DISABLE=1 and
    -DCRUCIBLE_USE_CCACHE=OFF, so a compiler that PATH finds through a link
    of ccache also compiles with no cache.  The test run has the environment
    of the caller, because the self-tests of ccache need a ccache that
    operates.  The configure runs the cmake of PATH, and the configure step
    rejects a cmake that is not the pin.  The build and the tests run the
    cmake and the ctest that the build directory names (`setup check`,
    utils/assay/setup/pins.py).

THE BUILD DIRECTORY
    A clean build needs a new directory.  When DIR is a configured build
    directory, the command removes it first.  The command refuses a DIR that
    exists and is not a configured build directory, and a DIR that contains
    the tree.

THE REPORT
    The report goes to standard output and to DIR/gauge-report.txt.  The
    ninja log stays in DIR, and the output of each step is in
    DIR/gauge-STEP.log.  The host can be shared.  A number from a host with
    other load cannot be compared with a number from a quiet host, so the
    report names the host, the commit and the job count.

Usage
    loop gauge --cold [--jobs N] [--preset NAME] [--build-dir DIR] [--target NAME]... [--test-regex RE]
                      [--timeout S] [--no-tests]
    guard selftest loop.gauge_cold

    --jobs N         The parallelism of the build and the tests.  The default is 96
    --preset NAME    The configure preset.  The default is default
    --build-dir DIR  The build directory.  The default is ROOT/build-gauge
    --target NAME    Build NAME and not `all`.  Give it again for more targets
    --test-regex RE  A regex of ctest that limits the test run
    --timeout S      The ctest limit of one test, in seconds.  The default is 900
    --no-tests       Build, and run no test

Exit 0 when the build passed and each test that ran passed, 1 when the configure, the build or a test failed (the report
still holds each number that the command measured), 2 on a usage error or a failed self-test, 3 when cmake or ninja
is not on PATH.
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from assay.core import git, proc
from assay.core.paths import REPO_ROOT
from assay.loop import history as loop_history
from assay.readers import cmake as cmake_files
from assay.readers import ctest as ctest_output
from assay.readers import ninja as ninja_files
from assay.setup import pins as cmake_pin

DEFAULT_JOBS = 96
DEFAULT_TIMEOUT_S = 900
DEFAULT_BUILD_NAME = "build-gauge"
REPORT_NAME = "gauge-report.txt"
SLOWEST_COMPILES = 20
SLOWEST_TESTS = 10
OBJECT_SUFFIX = ".o"
FIXTURE_PREFIX = "neg_"
KB_PER_MB = 1024
MISSING_TOOL = 3


@dataclass(frozen=True, slots=True)
class StepCost:
    """The cost of one step: its wall time, its user and system CPU time, its largest resident set and its status."""

    name: str
    wall_s: float
    user_s: float
    system_s: float
    peak_kb: int
    status: int

    def line(self) -> str:
        """Return the line of the step in the report."""
        return (f"{self.name:<10} wall {self.wall_s:8.1f} s   cpu {self.user_s + self.system_s:9.1f} s (user "
                f"{self.user_s:9.1f}, system {self.system_s:7.1f})   peak {self.peak_kb // KB_PER_MB:6d} MB   exit "
                f"{self.status}")


class Report:
    """The lines of the report: each one goes to standard output and to the report file."""

    def __init__(self, path: Path) -> None:
        """Start a new report file."""
        self.path = path
        path.write_text("", encoding="utf-8")

    def say(self, text: str) -> None:
        """Print one or more lines, and append them to the report file."""
        print(text, flush=True)
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(text + "\n")


def timed_step(name: str, command: list[str], cwd: Path, environment: dict[str, str], log: Path) -> StepCost:
    """Run one step with its output in a log, and measure it with wait4 through assay.core.proc.

    A step that runs past the limit BUILD_S of assay.core.proc gets the status of its end on the signal of the limit.

    Args:
        name: The name of the step
        command: The command
        cwd: The working directory
        environment: The environment
        log: The log of the output

    Returns:
        The cost of the step

    Raises:
        proc.CannotStart: If the program of the step cannot start
    """
    step = proc.Command(command, timeout_s=proc.BUILD_S, cwd=cwd, env=environment, stdout=log,
                        stderr=proc.Output.MERGE)
    try:
        completed = proc.run(step)
    except proc.TimedOut as problem:
        if problem.completed is None:
            raise
        completed = problem.completed
    status = completed.exit if completed.exit >= 0 else 128 - completed.exit
    return StepCost(name, completed.wall_s, completed.user_s, completed.system_s, completed.peak_kb, status)


def source_of(output: str) -> str:
    """Return the source path of one object path of the build: the path without CMakeFiles/TARGET.dir/ and .o."""
    parts = output.removesuffix(OBJECT_SUFFIX).split("/")
    if "CMakeFiles" in parts:
        start = parts.index("CMakeFiles")
        end = next((index for index in range(start, len(parts)) if parts[index].endswith(".dir")), start)
        parts = parts[:start] + parts[end + 1:]
    return "/".join(parts)


def compile_lines(build_dir: Path) -> list[str]:
    """Return the lines of the compile steps of the report: their count and sum, and the slowest ones.

    Complexity: O(n log n) for n records of the ninja log.

    Args:
        build_dir: The build directory

    Returns:
        The lines, or one line that says that the build wrote no ninja log
    """
    records = ninja_files.read_log(build_dir)
    if not records:
        return ["loop gauge --cold: the build wrote no ninja log."]
    steps = {os.path.relpath(output, build_dir): (record.end_ms - record.start_ms) / 1000.0
             for output, record in records.items() if output.endswith(OBJECT_SUFFIX)}
    lines = [f"compile steps: {len(steps)}, sum of their wall times {sum(steps.values()):.0f} s",
             f"the {SLOWEST_COMPILES} slowest compile steps (wall seconds, from the ninja log):"]
    for output, seconds in sorted(steps.items(), key=lambda item: item[1], reverse=True)[:SLOWEST_COMPILES]:
        lines.append(f"  {seconds:8.1f}  {source_of(output)}")
    return lines


def test_lines(test_text: str) -> list[str]:
    """Return the lines of the tests of the report: the counts and sums, and the slowest tests.

    Args:
        test_text: The output of the test run

    Returns:
        The lines
    """
    times = ctest_output.test_times(test_text)
    fixtures = [seconds for name, seconds in times.items() if name.startswith(FIXTURE_PREFIX)]
    lines = [f"tests: {len(times)}, of them {len(fixtures)} negative fixtures; sum of the wall times "
             f"{sum(times.values()):.0f} s, of the fixtures {sum(fixtures):.0f} s",
             f"the {SLOWEST_TESTS} slowest tests (wall seconds, from the ctest log):"]
    for name, seconds in sorted(times.items(), key=lambda item: item[1], reverse=True)[:SLOWEST_TESTS]:
        lines.append(f"  {seconds:8.1f}  {name}")
    return lines


def summary_line(test_text: str, log: Path) -> str:
    """Return the line of the test summary of the report."""
    found = ctest_output.run_summary(test_text)
    if found == ctest_output.NO_TESTS_LINE:
        found = "no test matched the regex"
    return f"           {found or f'ctest printed no summary; read {log}'}"


def prepare(build_dir: Path, root: Path) -> str | None:
    """Make an empty build directory: remove a configured one, and refuse one that is not a build directory.

    Args:
        build_dir: The absolute build directory
        root: The source tree

    Returns:
        None, or the reason of the refusal
    """
    if root.resolve().is_relative_to(build_dir.resolve()):
        return f"the build directory {build_dir} contains the tree.  Give another --build-dir."
    if build_dir.exists():
        if not cmake_files.is_configured(build_dir):
            return (f"{build_dir} exists and is not a configured build directory.  Give another --build-dir, or remove "
                    f"it yourself.")
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    return None


def short_commit(root: Path) -> str:
    """Return the short commit of the tree, or "unknown"."""
    try:
        return git.run(root, "rev-parse", "--short", "HEAD").decode().strip()
    except git.GitError:
        return "unknown"


def measure(root: Path, options: argparse.Namespace, step: Callable[..., StepCost] = timed_step) -> int:
    """Configure, build and test in a new build directory, and report, as THE MEASUREMENT tells.

    Args:
        root: The source tree
        options: The options of the command line
        step: The function that runs and measures one step

    Returns:
        The exit status
    """
    for tool in ("cmake", "ninja"):
        if shutil.which(tool) is None:
            print(f"loop gauge --cold: {tool} is not on PATH.", file=sys.stderr)
            return MISSING_TOOL
    build_dir = (options.build_dir or root / DEFAULT_BUILD_NAME).absolute()
    refusal = prepare(build_dir, root)
    if refusal is not None:
        print(f"loop gauge --cold: {refusal}", file=sys.stderr)
        return 2
    report = Report(build_dir / REPORT_NAME)
    state = loop_history.git_state(root)
    report.say(f"loop gauge --cold: host {socket.gethostname()}, commit {short_commit(root)} "
               f"({state['tree'] or 'unknown'}), "
               f"preset {options.preset}, {options.jobs} jobs")
    report.say(f"loop gauge --cold: build directory {build_dir}, no compiler cache in the configure and the build")
    no_cache = proc.environment({"CCACHE_DISABLE": "1"})
    jobs = str(options.jobs)
    configured = step("configure", ["cmake", "--preset", options.preset, "-B", str(build_dir),
                                    "-DCRUCIBLE_USE_CCACHE=OFF"], root, no_cache, build_dir / "gauge-configure.log")
    report.say(configured.line())
    if configured.status != 0:
        print(f"loop gauge --cold: the configure failed.  Read {build_dir / 'gauge-configure.log'}.", file=sys.stderr)
        return 1
    build_cmake = cmake_pin.configured_program(build_dir, "cmake")
    build_ctest = cmake_pin.configured_program(build_dir, "ctest")
    build_command = [build_cmake, "--build", str(build_dir), "-j", jobs]
    if options.target:
        build_command += ["--target", *options.target]
    built = step("build", build_command, root, no_cache, build_dir / "gauge-build.log")
    report.say(built.line())
    status = 0
    if built.status != 0:
        print(f"loop gauge --cold: the build failed.  Read {build_dir / 'gauge-build.log'}.", file=sys.stderr)
        status = 1
    test_log = build_dir / "gauge-tests.log"
    if not options.no_tests and status == 0:
        test_command = [build_ctest, "--test-dir", str(build_dir), "-j", jobs, "--timeout", str(options.timeout),
                        "--no-tests=error"]
        if options.test_regex:
            test_command += ["-R", options.test_regex]
        tested = step("tests", test_command, root, proc.environment(), test_log)
        report.say(tested.line())
        if tested.status != 0:
            status = 1
        report.say(summary_line(test_log.read_text(encoding="utf-8", errors="replace"), test_log))
    report.say("\n".join(compile_lines(build_dir)))
    if test_log.is_file():
        report.say("\n".join(test_lines(test_log.read_text(encoding="utf-8", errors="replace"))))
    print(f"loop gauge --cold: the report is in {report.path}")
    return status


# ── The self-test ──────────────────────────────────────────────────────────

PLANTED_TEST_OUTPUT = """Test project /b
      Start  1: neg_fixture_a
 1/3 Test  #1: neg_fixture_a ....................   Passed    2.50 sec
 2/3 Test  #2: test_arena .......................   Passed    0.25 sec
 3/3 Test  #3: test_slow ........................***Failed   10.00 sec

67% tests passed, 1 tests failed out of 3
"""


def self_test() -> int:
    """Check the step measure, the directory rules, the report lines and a measurement with planted steps.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="loop-gauge-cold-") as work:
        root = Path(work)
        cost = timed_step("probe", [sys.executable, "-c", "import sys; sum(range(10**6)); sys.exit(3)"], root,
                          proc.environment(), root / "probe.log")
        expect("a step gives its status, a CPU time from wait4 and a peak resident set",
               cost.status == 3 and cost.user_s + cost.system_s > 0 and cost.peak_kb > 1000)
        expect("the line of a step names its times, its peak in MB and its status",
               StepCost("build", 12.34, 100.0, 20.0, 2048 * KB_PER_MB, 1).line()
               == "build      wall     12.3 s   cpu     120.0 s (user     100.0, system    20.0)   peak   2048 MB   "
                  "exit 1")
        tree = root / "tree"
        tree.mkdir()
        expect("a build directory that contains the tree is refused", prepare(root, tree) is not None)
        stranger = root / "stranger"
        stranger.mkdir()
        expect("a directory that is not a configured build directory is refused",
               prepare(stranger, tree) is not None and stranger.is_dir())
        old = root / "old-build"
        old.mkdir()
        cmake_files.cache_file(old).write_text("", encoding="utf-8")
        (old / "stale.o").write_text("", encoding="utf-8")
        expect("a configured build directory is removed and made again, empty",
               prepare(old, tree) is None and old.is_dir() and not any(old.iterdir()))
        expect("the source of an object path has no CMakeFiles part",
               source_of("test/CMakeFiles/test_arena.dir/test_arena.cpp.o") == "test/test_arena.cpp"
               and source_of("libplain.o") == "libplain")
        lines = test_lines(PLANTED_TEST_OUTPUT)
        expect("the test lines count the tests and the fixtures, and give the slowest first",
               lines[0] == "tests: 3, of them 1 negative fixtures; sum of the wall times 13 s, of the fixtures 2 s"
               and lines[2] == "      10.0  test_slow")
        expect("the summary line comes from the output of the run",
               summary_line(PLANTED_TEST_OUTPUT, root).strip() == "67% tests passed, 1 tests failed out of 3"
               and summary_line("No tests were found!!!\n", root).strip() == "no test matched the regex")

        calls: list[str] = []

        def planted_step(name: str, command: list[str], _cwd: Path, _environment: dict[str, str],
                         log: Path) -> StepCost:
            calls.append(name)
            if name == "configure":
                cmake_files.cache_file(log.parent).write_text(
                    f"CMAKE_COMMAND:INTERNAL={shutil.which('cmake')}\nCMAKE_CTEST_COMMAND:INTERNAL="
                    f"{shutil.which('ctest') or shutil.which('cmake')}\n", encoding="utf-8")
            if name == "tests":
                log.write_text(PLANTED_TEST_OUTPUT, encoding="utf-8")
            return StepCost(name, 1.0, 1.0, 0.0, 4096, 1 if name == "tests" else 0)

        options = argparse.Namespace(build_dir=root / "measured", preset="default", jobs=4, target=[],
                                     test_regex="", timeout=60, no_tests=False)
        if shutil.which("cmake") is not None and shutil.which("ninja") is not None:
            status = measure(tree, options, planted_step)
            report = (root / "measured" / REPORT_NAME).read_text(encoding="utf-8")
            expect("a failed test run gives the status 1, and the report holds each step and the slowest tests",
                   status == 1 and calls == ["configure", "build", "tests"] and "tests      wall" in report
                   and "test_slow" in report and "the build wrote no ninja log" in report)
        else:
            print("  skip a measurement with planted steps: cmake or ninja is not on PATH")

    if failures:
        print(f"loop gauge --cold --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop gauge --cold --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the options and measure, or run the self-test.

    Args:
        argv: The words after `loop gauge --cold`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(prog="loop gauge --cold",
                                     description="Measure one clean build of the tree and one run of each test group.")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--preset", default="default")
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--test-regex", default="")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--no-tests", action="store_true")
    options = parser.parse_args(argv)
    if options.jobs < 1 or options.timeout < 1:
        print("loop gauge --cold: --jobs and --timeout take a positive number.", file=sys.stderr)
        return 2
    try:
        return measure(REPO_ROOT, options)
    except (cmake_pin.PinError, proc.CannotStart) as problem:
        print(f"loop gauge --cold: {problem}", file=sys.stderr)
        return 1
