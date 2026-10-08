"""A planted script that makes a repository and does not isolate it."""

import subprocess

subprocess.run(["git", "-C", "tmp", "init", "-q"], check=False)
