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

# The name is only for the types, and the module imports only os at run time.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Mapping

# The environment variable that moves the root, or turns each cache off with OFF.
ROOT_VARIABLE = "CRUCIBLE_CACHE_DIR"
OFF = "off"
# The environment variable of the directory of the caches of the user.
HOME_VARIABLE = "XDG_CACHE_HOME"
# The cache of the compiled modules of Python, which each master names in sys.pycache_prefix.  The test launcher gives
# it to each test as PYTHONPYCACHEPREFIX (assay.loop.teststep, THE ENVIRONMENT).
COMPILED_CACHE = "pycache"
# A run touches an entry that it uses when the last touch is older than this, in seconds (assay.core.cache, THE BOUND).
TOUCH_INTERVAL = 3600.0


def user_cache_home(environment: Mapping[str, str] | None = None) -> str:
    """Return the directory of the caches of the user: $XDG_CACHE_HOME, else ~/.cache.  It makes no directory.

    `environment` is the environment to read, or None for the environment of this process.
    """
    variables = os.environ if environment is None else environment
    home = variables.get(HOME_VARIABLE, "")
    if home:
        return home
    return os.path.join(variables.get("HOME", "") or os.path.expanduser("~"), ".cache")


def base_root(environment: Mapping[str, str] | None = None) -> str | None:
    """Return the root of the caches, or None when $CRUCIBLE_CACHE_DIR is "off".  The function makes no directory.

    `environment` is the environment to read, or None for the environment of this process.
    """
    variables = os.environ if environment is None else environment
    chosen = variables.get(ROOT_VARIABLE, "")
    if chosen == OFF:
        return None
    if chosen:
        return chosen
    return os.path.join(user_cache_home(variables), "crucible")
