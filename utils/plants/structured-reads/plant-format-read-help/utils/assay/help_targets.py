"""A planted module that runs the help output of the build tool to find the targets of a build."""

from assay.core import proc


def target_lines(cmake, build_dir):
    """Return the lines of the help output of a build."""
    command = proc.Command([cmake, "--build", build_dir, "--target", "help"], timeout_s=60.0)
    return proc.run(command).out.splitlines()
