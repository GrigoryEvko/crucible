"""cache-size — each cache in the root of the caches holds no more than its limit.

THE RULE
    Each subdirectory of the root of assay.core.cache is a cache, and LIMITS
    of that module gives the limit of each one.  assay.core.cache.sizes()
    gives the bytes of each cache from a sample of its files.  A run then
    does not stat each file of a large cache.  The rule divides the bytes by
    the limit, and it compares the quotient with the measured rule cache-size
    of utils/registry.txt.  The eviction of each cache keeps it at or less
    than its limit.  A cache over its limit shows an eviction that does not
    run, or a limit that is too small.  The rule gives:
      over-limit  a cache over the warning threshold gives a warning, and over
                  the error threshold an error
      no-limit    a subdirectory with no row of LIMITS gives a warning,
                  because no eviction and no check knows its limit
    The entity of a finding is the name of the cache.  With $CRUCIBLE_CACHE_DIR
    set to "off", or with no root directory, the rule reads no cache, and it
    gives no finding.

THE COST
    Measured on 2026-10-02 on the build host: a sample of the root with nine
    caches and about 330,000 files takes about 0.2 s.

Complexity: one sample of each cache (assay.core.cache.sizes()).
"""

from assay.core import cache
from assay.core import report as check_report
from assay.guard.engines.buildsys.runner import BuildContext

RULE = "cache-size"
GIB = float(1 << 30)


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each cache over its limit, and each subdirectory of the root with no limit."""
    budget = context.budget(RULE)
    root = context.cache_root
    found: list[check_report.Finding] = []
    if root is None:
        return found
    for name, size in cache.sizes(root).items():
        place = str(root / name)
        limit = context.cache_limits.get(name)
        if limit is None:
            found.append(
                check_report.Finding(
                    "warning",
                    place,
                    0,
                    RULE,
                    f"the cache {name} has no row of LIMITS in utils/assay/core/cache.py.  No eviction and no "
                    f"check knows its limit.  Add a row for it, or remove the directory",
                    "no-limit",
                    name,
                )
            )
            continue
        level = check_report.classify(size / limit, budget)
        if level is None:
            continue
        threshold = check_report.threshold_text(budget.warn if level == "warning" else budget.error)
        found.append(
            check_report.Finding(
                level,
                place,
                0,
                RULE,
                f"the cache {name} holds {size / GIB:.2f} GiB, {size / limit:.2f} times its limit "
                f"of {limit / GIB:.2f} GiB in LIMITS of utils/assay/core/cache.py.  The {level} threshold of the measured "
                f"rule {RULE} is {threshold} times the limit.  The eviction of the cache keeps it at or less than its "
                f"limit.  The eviction does not run, or the limit is too small",
                "over-limit",
                name,
            )
        )
    return found
