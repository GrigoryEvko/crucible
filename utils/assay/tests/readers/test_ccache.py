"""The test of the reader of the statistics of ccache (assay.readers.ccache).

`guard selftest tests.readers.test_ccache` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The planted cases read a stats log and an output of `ccache --print-stats`
with each shape of a line.  The planted program cases run statistics() with
a planted ccache first in PATH, which prints planted statistics or exits
with an error.  The run cases compile one source two times through the
ccache of PATH, in a cache of their own.  Then they read the stats log of
each call and the statistics of the cache.  ASSAY_TEST_CXX or
CRUCIBLE_GCC16_PREFIX names the compiler.  With no ccache on PATH or no
compiler, the run cases write a skip line.
"""

import shutil
import sys
import tempfile
from pathlib import Path

from assay.core import proc, testing
from assay.core import report as check_report
from assay.readers import ccache
from assay.readers.ccache import Statistics, StatsLog

# The stats log of two calls: a miss with its comment line, and a direct_cache_hit with spaces and an empty line.
PLANTED_LOG = "# /w/a.cpp\ncache_miss\n\n# /w/b.cpp\n  direct_cache_hit  \n"
PLANTED_STATISTICS = (
    "autoconf_test\t7\n"
    "direct_cache_hit\t821\n"
    "local_storage_hit\t3\n"
    "preprocessed_cache_hit\t20\n"
    "a line with no tab\n"
    "stats_zeroed_timestamp\tnot a count\n"
    "\n"
)


def planted_cases(expect: testing.Expect) -> None:
    """Read a planted stats log and planted statistics."""
    log = ccache.parse_stats_log(PLANTED_LOG)
    expect(
        "parse_stats_log() gives each counter name in sequence, with no comment and no empty line",
        log == StatsLog(("cache_miss", "direct_cache_hit")),
    )
    expect("a log with a direct hit is a hit", log.is_hit)
    expect("a log with a preprocessed hit is a hit", ccache.parse_stats_log("preprocessed_cache_hit\n").is_hit)
    expect(
        "a log with a miss, a comment that names a hit counter and no hit counter is no hit",
        not ccache.parse_stats_log("# direct_cache_hit\ncache_miss\nlocal_storage_hit\n").is_hit,
    )
    expect("an empty log is no hit", ccache.parse_stats_log("") == StatsLog(()))
    statistics = ccache.parse_statistics(PLANTED_STATISTICS)
    expect(
        "parse_statistics() gives each counter with a decimal count, and keeps each other line that is not empty",
        statistics
        == Statistics(
            (("autoconf_test", 7), ("direct_cache_hit", 821), ("local_storage_hit", 3), ("preprocessed_cache_hit", 20)),
            ("a line with no tab", "stats_zeroed_timestamp\tnot a count"),
        ),
    )
    expect(
        "the hits are the sum of the direct and the preprocessed hits, and a missing counter counts 0",
        statistics.hits == 841 and statistics.count("cache_miss") == 0,
    )
    expect("an empty output has no hit", ccache.parse_statistics("").hits == 0)


def run_cases(expect: testing.Expect) -> None:
    """Compile one source two times through ccache, and read each stats log and the statistics of the cache."""
    program = shutil.which("ccache")
    cxx = testing.patched_compiler()
    if program is None or not cxx or not Path(cxx).is_file():
        check_report.write_line(
            f"  skip the run cases: ccache {program or 'not on PATH'}, compiler {cxx or 'not given'}"
        )
        return
    with tempfile.TemporaryDirectory(prefix="ccache-test-") as scratch:
        work = Path(scratch)
        configuration = work / "ccache.conf"
        configuration.write_text("", encoding="utf-8")
        (work / "unit.cpp").write_text("int unit_value() { return 7; }\n", encoding="utf-8")
        environment = proc.environment({"CCACHE_DIR": str(work / "cache"), "CCACHE_CONFIGPATH": str(configuration)})
        logs: list[StatsLog] = []
        for index in range(2):
            stats_log = work / f"call-{index}.log"
            command = [program, cxx, "-c", "unit.cpp", "-o", f"unit-{index}.o"]
            done = proc.run(
                proc.Command(
                    command, timeout_s=proc.QUICK_S, cwd=work, env={**environment, "CCACHE_STATSLOG": str(stats_log)}
                )
            )
            expect(f"the compile {index} through ccache exits with 0", done.exit == 0)
            logs.append(ccache.parse_stats_log(stats_log.read_text(encoding="utf-8")))
        expect("the stats log of the first call is no hit", not logs[0].is_hit and bool(logs[0].counters))
        expect("the stats log of the second call is a hit", logs[1].is_hit)
        statistics = ccache.statistics(environment)
        expect(
            "statistics() reads one hit in the cache of the two calls, and one miss",
            statistics.hits == 1 and statistics.count("cache_miss") == 1,
        )


def planted_program_cases(expect: testing.Expect) -> None:
    """Run statistics() with a planted ccache program first in PATH: one that prints, and one that fails."""
    with tempfile.TemporaryDirectory(prefix="ccache-planted-") as scratch:
        bin_dir = Path(scratch)
        program = bin_dir / "ccache"
        environment = proc.environment({"PATH": f"{bin_dir}:{proc.environment().get('PATH', '')}"})
        program.write_text(
            f"#!{sys.executable}\nimport sys\nsys.stdout.write({PLANTED_STATISTICS!r})\n", encoding="utf-8"
        )
        program.chmod(0o755)
        expect(
            "statistics() gives the counts of the output of the program",
            ccache.statistics(environment) == ccache.parse_statistics(PLANTED_STATISTICS),
        )
        program.write_text(f"#!{sys.executable}\nimport sys\nsys.exit(3)\n", encoding="utf-8")
        try:
            ccache.statistics(environment)
            is_refused = False
        except ccache.CcacheError as problem:
            is_refused = "exits with 3" in str(problem)
        expect("statistics() raises CcacheError when ccache exits with a status that is not 0", is_refused)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (planted_cases, planted_program_cases, run_cases))
