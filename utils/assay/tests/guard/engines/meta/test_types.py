"""The test of the run of mypy of the rule types (assay.guard.engines.meta.types): the recovery from a broken cache.

`guard selftest tests.guard.engines.meta.test_types` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.  The witness of
the rule holds the verdicts on a tree with the pinned mypy.  This test holds
THE BROKEN CACHE with a planted mypy, which gives an internal error while a
mark file is in its cache.
"""

import tempfile
from pathlib import Path

from assay.core import cache_root, proc, testing
from assay.guard.engines.meta import types
from assay.guard.engines.meta.runner import MetaContext

# A planted mypy.  It gives an internal error while its cache holds the mark BROKEN, or while the cache directory
# holds the mark ALWAYS.  Else it writes the mark RAN in the cache of the version, and it gives no error.
PLANTED_MAIN = """\
import os
import sys


def console_entry():
    cache = sys.argv[sys.argv.index("--cache-dir") + 1]
    version = os.path.join(cache, "{version}")
    if os.path.exists(os.path.join(version, "broken")) or os.path.exists(os.path.join(cache, "always")):
        sys.stderr.write("error: {crash} -- the planted crash\\n")
        sys.exit(2)
    os.makedirs(version, exist_ok=True)
    with open(os.path.join(version, "ran"), "w") as handle:
        handle.write("ran\\n")
"""


def planted_site(scratch: Path) -> Path:
    """Write the planted mypy in a site directory, and return the directory."""
    package = scratch / "site" / "mypy"
    package.mkdir(parents=True)
    (package / "__init__.py").write_text("")
    (package / "__main__.py").write_text(PLANTED_MAIN.format(version=types.PYTHON_VERSION, crash=types.CRASH_WORDS))
    return package.parent


def run_cases(expect: testing.Expect) -> None:
    """Check that a crash on a broken cache gives one run with a cold cache, and that a second crash fails."""
    with tempfile.TemporaryDirectory(prefix="test-types-") as scratch_text:
        scratch = Path(scratch_text)
        site = planted_site(scratch)
        (scratch / "tree" / types.UTILS).mkdir(parents=True)
        context = MetaContext(scratch / "tree")
        with testing.environment_variable(cache_root.ROOT_VARIABLE, str(scratch / "caches")):
            version = scratch / "caches" / types.CACHE_NAME / types.PYTHON_VERSION
            version.mkdir(parents=True)
            (version / "kept").write_text("kept\n")
            types.run_mypy(context, ["planted.py"], site)
            expect("a clean run keeps the cache", (version / "kept").is_file() and (version / "ran").is_file())
            (version / "broken").write_text("")
            types.run_mypy(context, ["planted.py"], site)
            expect(
                "a crash removes the cache of the version, and the second run with a cold cache passes",
                not (version / "broken").exists() and not (version / "kept").exists() and (version / "ran").is_file(),
            )
            (version.parent / "always").write_text("")
            try:
                types.run_mypy(context, ["planted.py"], site)
                is_refused = False
            except proc.ToolFailed as failure:
                is_refused = types.CRASH_WORDS in str(failure)
            expect("a crash with a cold cache too fails, with the words of mypy", is_refused)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (run_cases,))
