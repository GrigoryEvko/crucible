"""A planted script that commits through a wrapper of git, and does not isolate the repository."""

import subprocess


def run_git(*arguments):
    """Run git in the directory t."""
    subprocess.run(["git", "-C", "t", *arguments], check=False)


run_git("commit", "-m", "x")
