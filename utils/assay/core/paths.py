"""The root of the repository, the masters and the directory of the data files, found from the location of this file.

This file is utils/assay/core/paths.py, so the root is three directories above
it.  Every module of the package gets the root from REPO_ROOT here, and no
other Python file under utils/ calculates the root from its own position.

The rule holds in each copy of the tree: a checkout, a git worktree, and a
throwaway export that a guard run or a self-test makes.  The result does not
depend on git or on the working directory of the caller.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
# The three masters, the only commands of utils/.  A module that starts a
# command of the package starts one of them.
GUARD = REPO_ROOT / "utils" / "bin" / "guard"
LOOP = REPO_ROOT / "utils" / "bin" / "loop"
SETUP = REPO_ROOT / "utils" / "bin" / "setup"
# The directory of the data files of the package: the budget table, the rule
# table, the ledgers, the allowlists and the rosters.
DATA = REPO_ROOT / "utils" / "scripts"
