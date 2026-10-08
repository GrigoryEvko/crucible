"""guard check — run the module that holds one rule.

usage: guard check --rule ID [ARG]...

RULES of assay.guard.legacy_rules gives the module of each rule ID and the
words that select its mode.  The command calls the function main of that
module with those words and then each ARG, unchanged, and it exits with the
value that main returns.

Each run also holds the cache of the compiled modules of the package under its
limit, when its turn is due (assay.core.cache, THE COMPILED MODULES).  A
failure of that work changes no verdict of the rule.
"""

import sys

from assay.core import cache
from assay.guard.legacy_rules import RULES


def main(argv: list[str]) -> int:
    """Run the module of the rule that the command line names.

    Args:
        argv: The words after the subcommand: --rule ID, and then the words for the module

    Returns:
        The value that main of the module returns, or 2 on a usage error
    """
    if len(argv) < 2 or argv[0] != "--rule":
        print("usage: guard check --rule ID [ARG]...", file=sys.stderr)
        return 2
    row = RULES.get(argv[1])
    if row is None:
        print(f"guard check: the rule {argv[1]!r} has no row in RULES of utils/assay/guard/legacy_rules.py.  "
              f"Give one of: {', '.join(sorted(RULES))}.", file=sys.stderr)
        return 2
    module_name, words = row
    try:
        cache.hold_compiled_bound()
    except OSError:
        pass
    __import__(module_name)
    return sys.modules[module_name].main([*words, *argv[2:]])
