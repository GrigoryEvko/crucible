#!/usr/bin/env python3
"""Test utils/scripts/build-launcher.py, the launcher in front of each compile and each link of the tree.

Each case runs the launcher as CMake runs it, in a temporary directory that
holds a CMakeCache.txt and a build-kind.txt, as a build directory does.  The
command is a planted compiler (a Python script that writes an object), a
planted ccache (a Python script that writes a stats log as ccache 4 does) or
a planted linker.  A budget table and a directory of ledgers of the test set
the thresholds and the admitted outputs.  A case examines the status, the
output and the record of the launcher.  Each case runs with no
GITHUB_ACTIONS, except two cases that set it to "true": on a CI runner, a
compile past the CPU limit runs to its end with a warning, and a compile over
the memory error threshold still fails.  Two cases read the instruction
count of a compile record: the record holds the count when this host gives
an exact counter (utils/scripts/cost_meter.py, THE INSTRUCTION COUNT), and
no count with CRUCIBLE_COUNT_INSTRUCTIONS=0.

With `--cxx`, two more cases use the real compiler.  One compiles and links.
The other compiles a constant evaluation that runs away, and the compile must
end at the CPU limit with status 1, as the GCC driver reports a compiler that
SIGKILL stopped.  When ccache is on PATH, a case compiles through the real
ccache with a cache
directory of its own: a cold cache must give a built record, and a warm cache
a hit.  Without ccache that part is skipped, because a build without ccache
never gives a hit.

The cases form groups, and each group runs in a temporary directory of its
own, at the same time as the other groups.  The cases at a CPU limit take
about one second each, so the groups let them run at the same time.  The
verdicts print in the order of the groups.

The exit code is 0 when each case passes, and 1 when a case fails.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True
SCRIPTS = Path(__file__).resolve().parents[1] / "utils" / "scripts"
sys.path.insert(0, str(SCRIPTS))
import check_report  # noqa: E402
import cost_meter  # noqa: E402

LAUNCHER = SCRIPTS / "build-launcher.py"
KIND = "test-kind"

PLANTED_TOOL = """\
import os, signal, subprocess, sys, time
signal.signal(signal.SIGINT, signal.SIG_DFL)
mode = os.environ.get("PLANTED_MODE", "ok")
out = sys.argv[sys.argv.index("-o") + 1]
if mode == "sleep":
    open(out + ".started", "w").close()
    time.sleep(30)
if mode == "spin":
    spin = ("import os, time\\nstop = time.process_time() + float(os.environ.get('PLANTED_SPIN_S', '20'))\\n"
            "while time.process_time() < stop:\\n    pass\\n")
    if subprocess.call([sys.executable, "-c", spin]) < 0:
        sys.stderr.write("tool: fatal error: Killed signal terminated program\\n")
        sys.exit(1)
sys.stdout.write("tool stdout\\n")
sys.stderr.write("tool stderr\\n")
sys.stdout.flush()
sys.stderr.flush()
if mode == "term":
    os.kill(os.getpid(), signal.SIGTERM)
if mode == "fail":
    sys.exit(1)
with open(out, "wb") as handle:
    handle.write(b"object" * 10)
"""

PLANTED_CCACHE = """\
import os, subprocess, sys
log = os.environ["CCACHE_STATSLOG"]
out = sys.argv[sys.argv.index("-o") + 1]
if os.environ.get("PLANTED_HIT") == "1":
    with open(log, "a") as handle:
        handle.write("# source.cpp\\ndirect_cache_hit\\nlocal_storage_hit\\n")
    with open(out, "wb") as handle:
        handle.write(b"cached object")
    sys.exit(0)
with open(log, "a") as handle:
    handle.write("# source.cpp\\ncache_miss\\ndirect_cache_miss\\n")
sys.exit(subprocess.call(sys.argv[1:]))
"""

# A constant evaluation that takes minutes, so a compile of it reaches each CPU limit of the test.
SPIN_SOURCE = """\
constexpr unsigned long spin(unsigned long count) {
    unsigned long total = 0;
    for (unsigned long index = 0; index < count; ++index) {
        total += index * index;
    }
    return total;
}
constexpr unsigned long spun = spin(2000000000UL);
int main() { return static_cast<int>(spun & 1); }
"""
SPIN_FLAGS = ("-fconstexpr-loop-limit=2147483647", "-fconstexpr-ops-limit=1000000000000")

QUIET = ("compile-cpu | 1000 | 2000 | s | t\ncompile-memory | 1000 | 2000 | GB | m\n"
         "link-time | 1000 | 2000 | s | t\nlink-memory | 1000 | 2000 | GB | m\n")


class Bench:
    """A temporary build directory with a source, an object path, the planted tools and a quiet budget table."""

    def __init__(self, root: Path) -> None:
        """Make the tree."""
        self.root = root
        (root / "CMakeCache.txt").write_text("", encoding="utf-8")
        (root / "build-kind.txt").write_text(f"{KIND}\n", encoding="utf-8")
        self.tool = [sys.executable, str(root / "tool.py")]
        self.ccache = [sys.executable, str(root / "ccache.py")]
        (root / "tool.py").write_text(PLANTED_TOOL, encoding="utf-8")
        (root / "ccache.py").write_text(PLANTED_CCACHE, encoding="utf-8")
        self.source = root / "source.cpp"
        self.source.write_text("int main() { return 0; }\n", encoding="utf-8")
        self.object = root / "out" / "source.o"
        self.object.parent.mkdir()
        self.record_path = Path(f"{self.object}.cost")
        self.ledgers = root / "ledgers"
        self.ledgers.mkdir()
        for row in ("compile-memory", "link-memory"):
            (self.ledgers / f"{row}-ledger.txt").write_text("# a planted ledger\n", encoding="utf-8")
        self.budgets = root / "budgets.txt"
        self.budgets.write_text(QUIET, encoding="utf-8")
        self.env = dict(os.environ, CRUCIBLE_BUILD_BUDGETS=str(self.budgets), CRUCIBLE_BUILD_LEDGERS=str(self.ledgers))
        # A case that needs a CI runner sets GITHUB_ACTIONS itself, so each
        # verdict is the same on a CI runner and on the build host.
        for name in ("CCACHE_STATSLOG", "CCACHE_DISABLE", "CCACHE_RECACHE", "CCACHE_READONLY", "GITHUB_ACTIONS",
                     cost_meter.INSTRUCTIONS_ENV):
            self.env.pop(name, None)

    def argv(self, tool: list[str], link: bool = False, output: Path | None = None) -> list[str]:
        """Return the command line of the launcher in front of one tool."""
        target = str(output or self.object)
        if link:
            return [sys.executable, "-S", str(LAUNCHER), "--link", *tool, str(self.object), "-o", target]
        return [sys.executable, "-S", str(LAUNCHER), *tool, "-o", target, "-c", str(self.source)]

    def launch(self, tool: list[str], link: bool = False, output: Path | None = None,
               **env: str) -> subprocess.CompletedProcess[bytes]:
        """Run the launcher in front of one tool and wait for it."""
        return subprocess.run(self.argv(tool, link, output), capture_output=True, env={**self.env, **env},
                              timeout=60)

    def record(self, output: Path | None = None) -> dict[str, object]:
        """Return the record of an output, or an empty dictionary."""
        try:
            return json.loads(Path(f"{output or self.object}.cost").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def table(self, text: str) -> None:
        """Write the budget table of the test."""
        self.budgets.write_text(text, encoding="utf-8")


class Verdicts:
    """The verdict lines and the failed cases of one group of cases."""

    def __init__(self) -> None:
        """Start with no verdict."""
        self.lines: list[str] = []
        self.failures: list[str] = []

    def expect(self, name: str, holds: bool) -> None:
        """Record the verdict of one case."""
        self.lines.append(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            self.failures.append(name)


def last_finding(stderr: bytes) -> check_report.Finding | None:
    """Return the finding of the last line of standard error, or None."""
    lines = stderr.decode().splitlines()
    return check_report.parse_line(lines[-1]) if lines else None


def run_records(bench: Bench, verdicts: Verdicts) -> None:
    """The status, the output and the record of a compile, a failed compile, a signal, a ccache hit and a warning."""
    expect = verdicts.expect
    result = bench.launch(bench.tool)
    expect("a compile passes its status and its output unchanged",
           result.returncode == 0 and result.stdout == b"tool stdout\n" and result.stderr == b"tool stderr\n")
    first = bench.record()
    expect("a compile gives a built record with its time, its source and the output size",
           first.get("step") == "compile" and first.get("result") == "built"
           and first.get("output_bytes") == bench.object.stat().st_size
           and float(first["cost"]["cpu_s"]) > 0 and first.get("source") == str(bench.source))
    expect("the stats log of the call is removed", not Path(f"{bench.object}.ccache-stats").exists())
    probe = cost_meter.open_instruction_counter()
    host_counts = probe is not None
    if probe is not None:
        os.close(probe)
    counted = first["cost"].get("instructions")
    expect(f"a compile record holds the user instructions of the compile exactly when the host gives a counter "
           f"(this host: {'a counter' if host_counts else 'no counter'})",
           (isinstance(counted, int) and counted > 0) if host_counts else counted is None)
    bench.launch(bench.tool, **{cost_meter.INSTRUCTIONS_ENV: "0"})
    expect(f"with {cost_meter.INSTRUCTIONS_ENV}=0, a compile record holds no instruction count",
           bench.record().get("result") == "built" and "instructions" not in bench.record()["cost"])

    result = bench.launch(bench.tool, PLANTED_MODE="fail")
    expect("a failed compile passes status 1 and gives a failed record",
           result.returncode == 1 and bench.record().get("result") == "failed")

    result = bench.launch(bench.tool, PLANTED_MODE="term")
    expect("a compile that ends on SIGTERM makes the launcher end on SIGTERM",
           result.returncode == -signal.SIGTERM and b"Traceback" not in result.stderr)

    bench.record_path.unlink()
    result = subprocess.run([sys.executable, "-S", str(LAUNCHER), sys.executable, "-c", "print(7)"],
                            capture_output=True, env=bench.env, timeout=60)
    expect("a command with no output runs and writes no record",
           result.returncode == 0 and result.stdout == b"7\n" and not bench.record_path.exists())

    bench.launch(bench.tool)
    compiled = bench.record()["cost"]
    result = bench.launch([*bench.ccache, *bench.tool], PLANTED_HIT="1")
    hit = bench.record()
    expect("a ccache hit gives a hit record with no time that keeps the last cost",
           result.returncode == 0 and hit.get("result") == "hit" and "cost" not in hit
           and hit.get("last_cost") == compiled and hit.get("output_bytes") == len(b"cached object"))
    bench.launch([*bench.ccache, *bench.tool], PLANTED_HIT="1")
    expect("a second hit keeps the same last cost", bench.record().get("last_cost") == compiled)
    outer = bench.root / "outer.log"
    result = bench.launch([*bench.ccache, *bench.tool], PLANTED_HIT="0", CCACHE_STATSLOG=str(outer))
    expect("a ccache miss gives a built record, and the counters reach the stats log of the build",
           result.returncode == 0 and bench.record().get("result") == "built"
           and "cache_miss" in outer.read_text(encoding="utf-8"))

    bench.table("compile-cpu | 0 | 1000 | s | t\ncompile-memory | 0 | 1000 | GB | m\n")
    result = bench.launch(bench.tool)
    lines = result.stderr.decode().splitlines()
    found = [check_report.parse_line(line) for line in lines[1:]]
    expect("a compile over two warning thresholds prints one warning line after the tool output",
           result.returncode == 0 and lines[0] == "tool stderr" and len(found) == 1 and found[0] is not None
           and found[0].level == "warning" and found[0].check == "compile-cpu"
           and "peak memory" in found[0].message)
    bench.table("compile-cpu | zero | 1 | s | t\n")
    result = bench.launch(bench.tool)
    expect("a broken budget table changes neither the status nor the output",
           result.returncode == 0 and result.stderr == b"tool stderr\n")


def run_memory_ledger(bench: Bench, verdicts: Verdicts) -> None:
    """A compile over the memory error threshold, and the rows of the ledger that admit it or do not."""
    expect = verdicts.expect
    bench.table("compile-cpu | 1000 | 2000 | s | t\ncompile-memory | 0.0001 | 0.0002 | GB | m\n")
    result = bench.launch(bench.tool)
    found_line = last_finding(result.stderr)
    expect("a compile over the memory error threshold fails, removes the object and gives a rejected record",
           result.returncode == 1 and not bench.object.exists() and bench.record().get("result") == "rejected"
           and found_line is not None and found_line.level == "error" and found_line.check == "compile-memory"
           and "no row of" in found_line.message)
    (bench.ledgers / "compile-memory-ledger.txt").write_text(
        f"# a planted ledger\n{KIND} | out/source.o | 0.01 | the test admits it\n", encoding="utf-8")
    result = bench.launch(bench.tool)
    found_line = last_finding(result.stderr)
    expect("a ledger row with a reason admits the compile, with a warning",
           result.returncode == 0 and bench.object.exists() and bench.record().get("result") == "built"
           and found_line is not None and found_line.level == "warning" and "the test admits it" in
           found_line.message)
    (bench.ledgers / "compile-memory-ledger.txt").write_text(
        "# a planted ledger\nother-kind | out/source.o | 0.01 | another kind\n", encoding="utf-8")
    result = bench.launch(bench.tool)
    expect("a row of another kind does not admit the compile", result.returncode == 1)
    (bench.ledgers / "compile-memory-ledger.txt").write_text(
        f"# a planted ledger\n{KIND} | out/source.o | 0.01 |\n", encoding="utf-8")
    result = bench.launch(bench.tool)
    expect("a row with no reason does not admit the compile", result.returncode == 1)
    (bench.root / "build-kind.txt").unlink()
    result = bench.launch(bench.tool)
    found_line = last_finding(result.stderr)
    expect("with no build kind, no row admits the compile",
           result.returncode == 1 and found_line is not None and "build-kind.txt" in found_line.message)


def run_cpu_limit(bench: Bench, verdicts: Verdicts) -> None:
    """A planted compile that runs away ends at the CPU limit, and on a CI runner it runs to its end."""
    expect = verdicts.expect
    bench.table("compile-cpu | 0.1 | 0.3 | s | t\ncompile-memory | 1000 | 2000 | GB | m\n")
    started = time.monotonic()
    result = bench.launch(bench.tool, PLANTED_MODE="spin")
    found_line = last_finding(result.stderr)
    expect("a compile that runs away ends at the CPU limit, with an error line",
           result.returncode == 1 and time.monotonic() - started < 15 and found_line is not None
           and found_line.level == "error" and found_line.check == "compile-cpu"
           and "hard limit of 1 s" in found_line.message and bench.record().get("result") == "failed")
    result = bench.launch(bench.tool, PLANTED_MODE="spin", PLANTED_SPIN_S="1.5", GITHUB_ACTIONS="true")
    found_line = last_finding(result.stderr)
    expect("on a CI runner, a compile past the CPU limit runs to its end, and its time error is a warning",
           result.returncode == 0 and bench.object.exists() and bench.record().get("result") == "built"
           and found_line is not None and found_line.level == "warning" and found_line.check == "compile-cpu"
           and "hard limit of 0.9 s" in found_line.message and "demoted" in found_line.message)


def run_ci_memory(bench: Bench, verdicts: Verdicts) -> None:
    """On a CI runner, a compile over the memory error threshold still fails."""
    bench.table("compile-cpu | 1000 | 2000 | s | t\ncompile-memory | 0.0001 | 0.0002 | GB | m\n")
    result = bench.launch(bench.tool, GITHUB_ACTIONS="true")
    found_line = last_finding(result.stderr)
    verdicts.expect("on a CI runner, a compile over the memory error threshold still fails and removes the object",
                    result.returncode == 1 and not bench.object.exists()
                    and bench.record().get("result") == "rejected" and found_line is not None
                    and found_line.level == "error" and found_line.check == "compile-memory")


def run_link(bench: Bench, verdicts: Verdicts) -> None:
    """The record of a link, and a link over the memory error threshold."""
    expect = verdicts.expect
    linked = bench.root / "out" / "program"
    bench.launch(bench.tool)
    result = bench.launch(bench.tool, link=True, output=linked)
    link_record = bench.record(linked)
    expect("a link gives a built record of step link, with no source and no instruction count",
           result.returncode == 0 and link_record.get("step") == "link" and link_record.get("result") == "built"
           and "source" not in link_record and link_record.get("output") == str(linked)
           and "instructions" not in link_record["cost"])
    bench.table("link-time | 1000 | 2000 | s | t\nlink-memory | 0.0001 | 0.0002 | GB | m\n")
    result = bench.launch(bench.tool, link=True, output=linked)
    found_line = last_finding(result.stderr)
    expect("a link over the memory error threshold fails with a link-memory error",
           result.returncode == 1 and not linked.exists() and found_line is not None
           and found_line.check == "link-memory")


def run_interrupt(bench: Bench, verdicts: Verdicts) -> None:
    """An interrupt of the process group makes the launcher end on SIGINT."""
    process = subprocess.Popen(bench.argv(bench.tool), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               start_new_session=True, env={**bench.env, "PLANTED_MODE": "sleep"})
    marker = Path(f"{bench.object}.started")
    deadline = time.monotonic() + 20
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.02)
    os.killpg(process.pid, signal.SIGINT)
    _, err = process.communicate(timeout=30)
    verdicts.expect("an interrupt of the process group makes the launcher end on SIGINT, with no traceback",
                    process.returncode == -signal.SIGINT and b"Traceback" not in err)


def run_real_tools(bench: Bench, verdicts: Verdicts, cxx: str | None) -> None:
    """The real compiler at a compile, a link and the CPU limit, and the real ccache when it is on PATH."""
    expect = verdicts.expect
    real_ccache = shutil.which("ccache")
    if cxx:
        program = bench.root / "out" / "real-program"
        bench.object.unlink(missing_ok=True)
        compiled_run = bench.launch([cxx])
        linked_run = bench.launch([cxx], link=True, output=program)
        expect("the real compiler: a compile and a link give built records",
               compiled_run.returncode == 0 and linked_run.returncode == 0
               and bench.record().get("result") == "built" and bench.record(program).get("result") == "built"
               and subprocess.run([str(program)], timeout=30).returncode == 0)

        bench.source.write_text(SPIN_SOURCE, encoding="utf-8")
        bench.table("compile-cpu | 0.1 | 0.3 | s | t\ncompile-memory | 1000 | 2000 | GB | m\n")
        started = time.monotonic()
        stopped_run = bench.launch([cxx, *SPIN_FLAGS])
        found_line = last_finding(stopped_run.stderr)
        expect("the real compiler: a compile at the CPU limit ends with status 1 and an error line, and the "
               "driver reports no internal compiler error",
               stopped_run.returncode == 1 and time.monotonic() - started < 15
               and b"internal compiler error" not in stopped_run.stderr and found_line is not None
               and found_line.check == "compile-cpu" and "hard limit of 1 s" in found_line.message)
        bench.table(QUIET)
        bench.source.write_text("int main() { return 0; }\n", encoding="utf-8")
    if cxx and real_ccache:
        config = bench.root / "ccache.conf"
        config.write_text("", encoding="utf-8")
        env = {"CCACHE_DIR": str(bench.root / "ccache"), "CCACHE_CONFIGPATH": str(config),
               "CCACHE_NOHASHDIR": "1"}
        bench.object.unlink(missing_ok=True)
        cold_run = bench.launch([real_ccache, cxx], **env)
        cold = bench.record()
        bench.object.unlink(missing_ok=True)
        warm_run = bench.launch([real_ccache, cxx], **env)
        warm = bench.record()
        expect("the real ccache: a cold cache gives a built record, and a warm cache a hit",
               cold_run.returncode == 0 and warm_run.returncode == 0 and cold.get("result") == "built"
               and warm.get("result") == "hit" and warm.get("last_cost") == cold.get("cost"))
    else:
        verdicts.lines.append(f"  skip the real ccache case: compiler {cxx or 'not given'}, ccache "
                              f"{real_ccache or 'not on PATH'}")


def main(argv: list[str]) -> int:
    """Run each group of cases at the same time, each group in a bench of its own.

    Args:
        argv: The arguments: an optional --cxx COMPILER

    Returns:
        0 when each case passes, else 1
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--cxx", help="the real C++ compiler, for the cases with the real tools")
    arguments = parser.parse_args(argv)
    groups: list[Callable[[Bench, Verdicts], None]] = [
        run_records, run_memory_ledger, run_cpu_limit, run_ci_memory, run_link, run_interrupt,
        lambda bench, verdicts: run_real_tools(bench, verdicts, arguments.cxx),
    ]
    verdicts = [Verdicts() for _ in groups]
    with tempfile.TemporaryDirectory(prefix="build-launcher-") as scratch:
        benches: list[Bench] = []
        for index in range(len(groups)):
            (Path(scratch) / str(index)).mkdir()
            benches.append(Bench(Path(scratch) / str(index)))
        with ThreadPoolExecutor(max_workers=len(groups)) as pool:
            futures = [pool.submit(group, bench, verdict)
                       for group, bench, verdict in zip(groups, benches, verdicts, strict=True)]
            for future in futures:
                future.result()
    for verdict in verdicts:
        print("\n".join(verdict.lines))
    failures = [failure for verdict in verdicts for failure in verdict.failures]
    if failures:
        print(f"build_launcher_test: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("build_launcher_test: every case passes.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
