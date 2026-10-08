"""A planted script whose git calls only read."""

import subprocess

subprocess.run(["git", "log", "-1"], check=False)
VERB = "commit"
