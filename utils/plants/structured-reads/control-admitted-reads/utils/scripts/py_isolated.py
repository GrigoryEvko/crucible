"""A planted script that commits in its throwaway repository through assay.core.git, which isolates it."""

from pathlib import Path

from assay.core import git as throwaway_repo

ROOT = Path("t")
throwaway_repo.init(ROOT)
throwaway_repo.commit_all(ROOT, "A planted commit")
