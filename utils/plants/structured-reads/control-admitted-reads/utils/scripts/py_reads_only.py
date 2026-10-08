"""A planted script whose git calls only read, through assay.core.git, and which holds the word commit outside a command."""

from pathlib import Path

from assay.core import git

LAST = git.run(Path("."), "log", "-1")
VERB = "commit"
