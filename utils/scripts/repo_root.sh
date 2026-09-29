# repo_root.sh — set REPO_ROOT to the absolute root of the repository.
#
# A shell script in utils/scripts sources this file:
#   . "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"
#
# This file is utils/scripts/repo_root.sh, so the root is two directories
# above it.  No other shell script under utils/ calculates the root from its
# own position.
# The rule holds in each copy of the tree: a checkout, a git worktree, and a
# throwaway export.  The result does not depend on git or on the working
# directory of the caller.
REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
