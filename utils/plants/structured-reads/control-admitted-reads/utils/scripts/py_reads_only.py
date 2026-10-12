"""A planted script whose git calls only read, through a function of assay.core.git, and which holds the word commit."""

from pathlib import Path

from assay.core import git

LAST = git.head(Path("."))
VERB = "commit"
