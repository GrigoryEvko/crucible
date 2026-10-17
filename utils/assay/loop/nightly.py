"""nightly — the nightly entry: run the work of the tiers gate and nightly of the plan in a time budget (`loop nightly`).

assay.loop.entry gives the work, the budget, the report and the usage.  `guard selftest tests.loop.test_entry` does a
test of the entry.
"""

import functools

from assay.loop import entry, tiers

main = functools.partial(entry.main, tiers.Tier.NIGHTLY)
