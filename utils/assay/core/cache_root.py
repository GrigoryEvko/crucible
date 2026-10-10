"""cache_root — the root of the caches as a path text, for the modules that a step meter imports.

THE ROOT
    assay.core.cache gives the rule of the root of the caches (THE ROOT of
    that module): $CRUCIBLE_CACHE_DIR when it is set, else
    $XDG_CACHE_HOME/crucible, else ~/.cache/crucible.  The value "off" turns
    each cache off.  This module holds that rule, and assay.core.cache gives
    its result as a Path.

    `loop step` reads the root of a cache for each compile
    (assay.core.cost_store).  The import of assay.core.cache costs 12.7 ms
    with -S, of which pathlib takes 7.5 ms and contextlib 3.9 ms.  A step
    meter itself takes approximately 10 ms.  This module then gives the root
    as text, and it imports only os.

`guard selftest tests.core.test_cache_root` does a test of the module.
"""

import os

# The environment variable that moves the root, or turns each cache off with OFF.
ROOT_VARIABLE = "CRUCIBLE_CACHE_DIR"
OFF = "off"
# The environment variable of the directory of the caches of the user.
HOME_VARIABLE = "XDG_CACHE_HOME"
# A run touches an entry that it uses when the last touch is older than this, in seconds (assay.core.cache, THE BOUND).
TOUCH_INTERVAL = 3600.0


def user_cache_home() -> str:
    """Return the directory of the caches of the user: $XDG_CACHE_HOME, else ~/.cache.  It makes no directory."""
    home = os.environ.get(HOME_VARIABLE, "")
    return home if home else os.path.join(os.path.expanduser("~"), ".cache")


def base_root() -> str | None:
    """Return the root of the caches, or None when $CRUCIBLE_CACHE_DIR is "off".  The function makes no directory."""
    chosen = os.environ.get(ROOT_VARIABLE, "")
    if chosen == OFF:
        return None
    if chosen:
        return chosen
    return os.path.join(user_cache_home(), "crucible")
