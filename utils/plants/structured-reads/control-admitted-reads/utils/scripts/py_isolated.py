"""A planted script that isolates its throwaway repository before it commits."""

import subprocess
from pathlib import Path

from assay.core import git as throwaway_repo

ROOT = Path("t")
throwaway_repo.init(ROOT)
subprocess.run(["git", "-C", "t", "commit", "-q"], check=False)
