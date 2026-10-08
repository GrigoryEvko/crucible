"""The test of the paths of the package (assay.core.paths): the root, the masters and the sorted listing.

`guard selftest tests.core.test_paths` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import paths, testing


def root_cases(expect: testing.Expect) -> None:
    """Check the root of the repository and the paths of the masters."""
    expect(
        "REPO_ROOT holds this module at utils/assay/core/paths.py",
        (paths.REPO_ROOT / "utils" / "assay" / "core" / "paths.py").resolve() == Path(paths.__file__).resolve(),
    )
    expect(
        "each master is a file of utils/bin/",
        all(master.is_file() and master.parent.name == "bin" for master in (paths.GUARD, paths.LOOP, paths.SETUP)),
    )


def listing_cases(expect: testing.Expect) -> None:
    """Check that listing() gives the entries sorted, with a pattern, and nothing for a missing directory."""
    with tempfile.TemporaryDirectory(prefix="test-paths-") as scratch:
        root = Path(scratch)
        for name in ("zeta.py", "alpha.py", "mid.txt", "sub/inner.py"):
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text("", encoding="utf-8")
        expect(
            "listing() gives each entry, sorted by path",
            [path.name for path in paths.listing(root)] == ["alpha.py", "mid.txt", "sub", "zeta.py"],
        )
        expect(
            "a pattern selects the entries",
            [path.name for path in paths.listing(root, "*.py")] == ["alpha.py", "zeta.py"],
        )
        expect(
            "a pattern with ** reads the subdirectories too",
            [path.relative_to(root).as_posix() for path in paths.listing(root, "**/*.py")]
            == ["alpha.py", "sub/inner.py", "zeta.py"],
        )
        expect("a missing directory gives no entry", paths.listing(root / "missing") == ())


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (root_cases, listing_cases))
