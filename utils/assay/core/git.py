"""A throwaway git repository for the self-test of a guard.

A repository variable in the environment of the caller wins over
`git -C DIR`: with GIT_DIR set, `git -C tmp init` initializes the
repository that GIT_DIR names again, and a later add and commit land there.
On 2026-09-24 an exported GIT_DIR sent the fixture commits of one
self-test into the real repository and emptied its main branch.

isolate removes each such variable from the environment of this process,
so every git call after it resolves its repository from its own path.
init makes the repository and stops the self-test unless git resolves the
fixture directory itself.
"""

import os
import subprocess
from pathlib import Path

# Each variable that tells git which repository, index or object store to
# use, whatever directory it runs in.
REPOSITORY_VARIABLES = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_NAMESPACE",
    "GIT_CEILING_DIRECTORIES",
)


class NotIsolated(Exception):
    """git resolves a repository other than the fixture directory."""


def isolate() -> None:
    """Remove every repository variable from the environment of this process."""
    for name in REPOSITORY_VARIABLES:
        os.environ.pop(name, None)


def init(root: Path) -> None:
    """Make a repository in root, and refuse to go on unless git resolves root itself.

    Raises:
        NotIsolated: If git resolves a repository outside root
    """
    isolate()
    subprocess.run(["git", "-C", str(root), "init", "-q"], check=True, capture_output=True)
    resolved = subprocess.run(["git", "-C", str(root), "rev-parse", "--absolute-git-dir"], check=True,
                              capture_output=True, text=True).stdout.strip()
    if Path(resolved).resolve() != (root / ".git").resolve():
        raise NotIsolated(f"the fixture repository {root} resolves to {resolved}; the self-test stops before it "
                          f"writes to a repository that is not its own")
