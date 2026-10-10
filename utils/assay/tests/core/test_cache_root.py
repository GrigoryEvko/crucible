"""The test of the root of the caches as text (assay.core.cache_root), and its agreement with assay.core.cache.

`guard selftest tests.core.test_cache_root` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.
"""

import os
from pathlib import Path

from assay.core import cache, cache_root, testing


def root_cases(expect: testing.Expect) -> None:
    """Check the root of each environment, and that assay.core.cache gives the same root as a Path."""
    with testing.environment_variable(cache_root.ROOT_VARIABLE, "/planted/root"):
        expect(
            "CRUCIBLE_CACHE_DIR names the root, and assay.core.cache gives it as a Path",
            cache_root.base_root() == "/planted/root" and cache.base_root() == Path("/planted/root"),
        )
    with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
        expect(
            "the value off turns each cache off in the two modules",
            cache_root.base_root() is None and cache.base_root() is None,
        )
    with (
        testing.environment_variable(cache_root.ROOT_VARIABLE, None),
        testing.environment_variable(cache_root.HOME_VARIABLE, "/planted/xdg"),
    ):
        expect(
            "with no CRUCIBLE_CACHE_DIR, the root is crucible in XDG_CACHE_HOME",
            cache_root.base_root() == "/planted/xdg/crucible"
            and cache_root.user_cache_home() == "/planted/xdg"
            and cache.user_cache_home() == Path("/planted/xdg"),
        )
    with (
        testing.environment_variable(cache_root.ROOT_VARIABLE, None),
        testing.environment_variable(cache_root.HOME_VARIABLE, None),
    ):
        home = os.path.expanduser("~")
        expect(
            "with neither variable, the root is ~/.cache/crucible",
            cache_root.base_root() == os.path.join(home, ".cache", "crucible")
            and cache.base_root() == Path(home) / ".cache" / "crucible",
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (root_cases,))
