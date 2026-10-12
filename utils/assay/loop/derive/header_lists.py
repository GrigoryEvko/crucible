"""loop derive header-lists — write the two header lists of CMake from the parse trees of include/.

usage: loop derive header-lists --write
       loop derive header-lists --check

The rule header-lists (assay.guard.engines.tsast.header_lists) derives the
headers of two reflection walks from the parse trees.  The first list holds
the crucible headers of the walk units.  The second list holds the headers of
the roster test of the permission tags.  Each list is a fragment of CMake
with one set() command, and CMake reads no header itself.

THE MODES
    --write     write each fragment again from the parse trees, and only when
                its bytes change.  It writes nothing while a header does not
                parse.
    --check     run the rule header-lists, as `guard check --rule
                header-lists` does.

Exit 0 when the mode did its work, and 1 when a header does not parse.  Exit 2
on a usage error, and 3 when the parse kit is not installed.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard.engines.tsast import header_lists as rule
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

PROGRAM = "loop derive header-lists"


def written(root: Path) -> ExitCode:
    """Write each fragment of the rule again from the parse trees of a tree, and only when its bytes change.

    Returns:
        ExitCode.OK when each fragment is written, ExitCode.FINDINGS when a header does not parse
    """
    context = TsastContext(root)
    headers = [path for path in context.files([f"{rule.INCLUDE}/"]) if path.endswith(".h")]
    broken = [tree for tree in context.trees(headers, with_broken=True) if tsast.first_error(tree) is not None]
    if broken:
        for tree in broken:
            check_report.write_line(f"{tree.path}: the parse kit cannot read the header", is_error=True)
        check_report.write_line(
            f"{PROGRAM}: --write does not write the lists while a header does not parse.", is_error=True
        )
        return ExitCode.FINDINGS
    for fragment in rule.fragments(rule.header_lists(context)):
        file = root / fragment.path
        if not file.is_file() or file.read_text(encoding="utf-8") != fragment.text:
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(fragment.text, encoding="utf-8")
        check_report.write_line(f"{PROGRAM}: {fragment.path} written.", is_error=True)
    return ExitCode.OK


def checked() -> int:
    """Run the rule header-lists through the command guard check, and give its status."""
    from assay.guard import check

    return check.main(["--rule", rule.RULE])


def main(argv: list[str]) -> int:
    """Run one mode (THE MODES).

    Args:
        argv: The words after `loop derive header-lists`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(PROGRAM, "write the two header lists of CMake from the parse trees of include/")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true", help="write each fragment again from the parse trees")
    modes.add_argument("--check", action="store_true", help="run the rule header-lists")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    try:
        return checked() if options.check else written(REPO_ROOT)
    except tsast.KitMissing as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
