"""ccache — the reader of the two statistics outputs of ccache: the stats log of a call and `ccache --print-stats`.

THE STATS LOG
    ccache appends the counters of each call to the file that the variable
    CCACHE_STATSLOG names.  A call writes a comment line, "#" and the input
    file, and then one counter name on each line, such as direct_cache_hit or
    cache_miss.  parse_stats_log() gives the counter names of the text of one
    call or more, in sequence, with no comment and no empty line.

THE STATISTICS
    `ccache --print-stats` prints each counter of the cache directory on its
    own line: the name, a tab and the count in decimal digits.
    parse_statistics() gives the count of each such line.  A line with no tab
    or with no decimal count is no counter, and the record keeps it as text.
    statistics() runs the command in an environment and reads its output.

THE HITS
    A counter direct_cache_hit or preprocessed_cache_hit tells that ccache
    gave a stored output, and the compiler did not run.  A call is a hit when
    its stats log holds one of the two counters (StatsLog.is_hit).  The hits
    of a cache directory are the sum of the two counts (Statistics.hits).

THE COST
    The step meter `loop step` imports this module for each compile.  At its
    start, the module imports assay.core.value only.
    statistics() imports assay.core.proc when it runs.

`guard selftest tests.readers.test_ccache` does the test of the module.
"""

from assay.core.value import Value

# The names are only for the types, and a step meter does not pay their import.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Mapping

# The two counters of a call that ccache answers from its cache (THE HITS).
HIT_COUNTERS = ("direct_cache_hit", "preprocessed_cache_hit")
# The mark of a comment line of a stats log, and the separator of a line of the statistics.
COMMENT = "#"
SEPARATOR = "\t"
# The command that prints the statistics, after the program ccache.
PRINT_STATS = "--print-stats"


class CcacheError(Exception):
    """ccache cannot run, or it exits with a status that is not 0."""


class StatsLog(Value):
    """The counter names of the stats log of one call or more, in sequence (THE STATS LOG)."""

    __match_args__ = ("counters",)
    counters: tuple[str, ...]

    def __init__(self, counters: tuple[str, ...]) -> None:
        """Keep the counter names."""
        super().__init__(counters)

    @property
    def is_hit(self) -> bool:
        """Return True when the log holds a hit counter (THE HITS)."""
        return any(counter in HIT_COUNTERS for counter in self.counters)


class Statistics(Value):
    """The count of each counter of `ccache --print-stats`, and each line that is no counter (THE STATISTICS)."""

    __match_args__ = ("counts", "other")
    counts: tuple[tuple[str, int], ...]
    other: tuple[str, ...]

    def __init__(self, counts: tuple[tuple[str, int], ...], other: tuple[str, ...]) -> None:
        """Keep the counts in the sequence of the output, and the other lines."""
        super().__init__(counts, other)

    def count(self, name: str) -> int:
        """Return the count of one counter, or 0 when the output has no line of the counter."""
        return dict(self.counts).get(name, 0)

    @property
    def hits(self) -> int:
        """Return the sum of the counts of the two hit counters (THE HITS)."""
        return sum(self.count(name) for name in HIT_COUNTERS)


def parse_stats_log(text: str) -> StatsLog:
    """Return the counter names of a stats log (THE STATS LOG).  Complexity: linear in the length of the text."""
    counters: list[str] = []
    for line in text.splitlines():
        name = line.strip()
        if name and not name.startswith(COMMENT):
            counters.append(name)
    return StatsLog(tuple(counters))


def parse_statistics(text: str) -> Statistics:
    """Return the counts of the output of `ccache --print-stats` (THE STATISTICS).

    Complexity: linear in the length of the text.
    """
    counts: list[tuple[str, int]] = []
    other: list[str] = []
    for line in text.splitlines():
        name, separator, value = line.partition(SEPARATOR)
        if separator and value.isascii() and value.isdigit():
            counts.append((name, int(value)))
        elif line:
            other.append(line)
    return Statistics(tuple(counts), tuple(other))


def statistics(environment: Mapping[str, str] | None) -> Statistics:
    """Run `ccache --print-stats` in an environment, and return its counts (THE STATISTICS).

    The environment names the cache directory, for example with CCACHE_DIR.  With None, the command gets the
    environment of this process.

    Raises:
        CcacheError: If ccache cannot start, or it exits with a status that is not 0
    """
    from assay.core import proc

    command = ["ccache", PRINT_STATS]
    try:
        done = proc.run(
            proc.Command(command, timeout_s=proc.QUICK_S, env=None if environment is None else dict(environment))
        )
    except proc.ToolFailed as problem:
        raise CcacheError(f"{' '.join(command)} cannot run: {problem}") from None
    if done.exit != 0:
        raise CcacheError(f"{' '.join(command)} exits with {done.exit}: {done.err.strip()[:300]}")
    return parse_statistics(done.out)
