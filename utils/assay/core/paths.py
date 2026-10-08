"""The root of the repository and the masters, found from the location of this file.

This file is utils/assay/core/paths.py, and the root is three directories
above it.  Each module of the package gets the root from REPO_ROOT here.  The
rule job-owners holds that no different Python file of utils/ calculates the
root from its own position.

The rule holds in each copy of the tree: a checkout, a git worktree, and the
export of a guard run or of a self-test.  Git and the working directory of the
caller do not change the result.

listing() gives the entries of a directory, sorted.  The file system gives
the entries of a directory in a sequence that changes from one host to the
next host.  The rule determinism holds that a module sorts each listing.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
# The three masters, the only commands of utils/.  A module that starts a
# command of the package starts one of them.
GUARD = REPO_ROOT / "utils" / "bin" / "guard"
LOOP = REPO_ROOT / "utils" / "bin" / "loop"
SETUP = REPO_ROOT / "utils" / "bin" / "setup"


def listing(directory: Path, pattern: str = "*") -> tuple[Path, ...]:
    """Return the entries of a directory that a pattern of pathlib matches, sorted by path.

    A pattern with "**" matches in the subdirectories too.  A missing
    directory gives no entry.  Complexity: O(n log n) for n entries.
    """
    return tuple(sorted(directory.glob(pattern)))
