"""A planted module that runs a git command outside assay.core.git."""

from assay.core import proc

STATUS = proc.run(proc.Command(["git", "-C", "t", "status", "--porcelain"], timeout_s=60.0))
