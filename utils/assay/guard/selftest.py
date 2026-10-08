"""guard selftest — run the test modules of one group, or the test of one module of the package.

usage: guard selftest GROUP [--part K/N]
       guard selftest MODULE [ARG]...

THE GROUPS
    GROUP is a name with no dot, and utils/assay/tests/GROUP/ holds its test
    modules.  The command runs each module with a file name that starts with
    test_, in the sequence of their dotted names, in one Python process.  It
    calls the function main of each module.  A module with PARTED = True
    takes `--part K/N` and does part K of N, with 0 <= K < N.  Each other
    module runs in the last part only.  Part 0 of a module with parts also
    does the work of that module that has no parts.  A module that cannot run
    on this host returns SKIPPED (77).  The exit status is the largest status
    of a module that ran, or SKIPPED when no module ran.

THE MODULES
    MODULE is the name of a module under assay, for example core.cache or
    readers.tsast.  A test module under assay.tests is its own test: the
    command calls its function main with each ARG.  For each other module,
    the command calls its function main with --self-test and then each ARG.
    The exit status is the value that main returns.

Complexity: one import and one call of main for each module of the group.
"""

import importlib

from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT

# The directory of the test groups (THE GROUPS).
TESTS = REPO_ROOT / "utils" / "assay" / "tests"
USAGE = (
    "usage: guard selftest GROUP [--part K/N]  (GROUP is a directory of utils/assay/tests)\n"
    "       guard selftest MODULE [ARG]...  (MODULE is a module under assay, for example core.cache)"
)


def group_modules(group: str) -> list[str]:
    """Return the dotted name of each test module of a group in the package assay, in sequence (THE GROUPS)."""
    directory = TESTS / group
    return [
        ".".join(("tests", *path.relative_to(TESTS).with_suffix("").parts))
        for path in sorted(directory.rglob("test_*.py"))
    ]


def parsed_part(words: list[str]) -> tuple[int, int] | None:
    """Return (K, N) of `--part K/N`, (0, 1) with no words, or None for other words."""
    if not words:
        return (0, 1)
    if len(words) != 2 or words[0] != "--part":
        return None
    index, slash, count = words[1].partition("/")
    if not (slash and index.isdigit() and count.isdigit() and int(index) < int(count)):
        return None
    return int(index), int(count)


def group_status(statuses: list[int]) -> int:
    """Return the status of a group: the largest status of a module that ran, else SKIPPED, else OK (THE GROUPS)."""
    ran = [status for status in statuses if status != ExitCode.SKIPPED]
    if statuses and not ran:
        return ExitCode.SKIPPED
    return max(ran, default=ExitCode.OK)


def run_group(group: str, part: tuple[int, int]) -> int:
    """Run each test module of a group for one part, and return the status of the group (THE GROUPS)."""
    names = group_modules(group)
    if not names:
        check_report.write_line(
            f"guard selftest: utils/assay/tests/{group}/ holds no test module.\n{USAGE}", is_error=True
        )
        return ExitCode.USAGE
    statuses: list[int] = []
    for name in names:
        module = importlib.import_module(f"assay.{name}")
        is_parted = bool(getattr(module, "PARTED", False))
        if not is_parted and part[0] != part[1] - 1:
            continue
        words = ["--part", f"{part[0]}/{part[1]}"] if is_parted and part != (0, 1) else []
        statuses.append(module.main(words))
    worst = group_status(statuses)
    check_report.write_line(
        f"guard selftest {group}: part {part[0]}/{part[1]}, {len(statuses)} modules, "
        f"{'each module passes' if worst == ExitCode.OK else f'the status is {worst}'}."
    )
    return worst


def run_module(argv: list[str]) -> int:
    """Run the test of one module (THE MODULES), and return the value of its main."""
    name = f"assay.{argv[0]}"
    try:
        module = importlib.import_module(name)
    except ModuleNotFoundError as error:
        if error.name is None or not (name == error.name or name.startswith(f"{error.name}.")):
            raise
        check_report.write_line(
            f"guard selftest: {name} is not a module of the package.  Give the name of a file "
            f"under utils/assay, with dots in place of the slashes and no suffix.",
            is_error=True,
        )
        return ExitCode.USAGE
    words = argv[1:] if argv[0].startswith("tests.") else ["--self-test", *argv[1:]]
    status: int = module.main(words)
    return status


def main(argv: list[str]) -> int:
    """Run the group or the module that the command line names.

    Args:
        argv: The words after the subcommand: GROUP and an optional --part K/N, or MODULE and its words

    Returns:
        The status of a group, the value that main of a module returns, or 2 on a usage error
    """
    if not argv or not all(part.isidentifier() for part in argv[0].split(".")):
        check_report.write_line(USAGE, is_error=True)
        return ExitCode.USAGE
    if "." not in argv[0] and (TESTS / argv[0]).is_dir():
        part = parsed_part(argv[1:])
        if part is None:
            check_report.write_line(
                f"guard selftest: give --part K/N with 0 <= K < N after the group.\n{USAGE}", is_error=True
            )
            return ExitCode.USAGE
        return run_group(argv[0], part)
    return run_module(argv)
