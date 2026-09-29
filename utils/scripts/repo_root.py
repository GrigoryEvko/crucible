"""The root of the repository, found from the location of this file.

This file is utils/scripts/repo_root.py, so the root is two directories above
it.  Every guard, generator and tool gets the root from REPO_ROOT here, and no
other Python file under utils/ calculates the root from its own position.  A script in
utils/scripts imports this module directly, because Python puts the directory
of the script on the module path.  A tool in utils/tools puts utils/scripts on
the path first.

The rule holds in each copy of the tree: a checkout, a git worktree, and a
throwaway export that a guard run or a self-test makes.  The result does not
depend on git or on the working directory of the caller.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
