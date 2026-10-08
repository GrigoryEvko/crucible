"""layout — each file of utils/ has its place, and obeys the naming rule of its place.

THE PLACES
    The path rows of the registry give the permitted paths: a glob and what
    those files are.  Each tracked file of utils/, and each tracked Python
    file or shell script outside utils/, matches exactly one path row, and
    each path row matches a minimum of one file.  A glob is a pattern of
    pathlib: '*' matches inside one directory, and '**' matches zero or more
    directories.  The rule reads the tracked files and the new files that
    .gitignore does not match.

THE NAMING RULE
    In utils/, outside the plants of utils/plants/:
      * A master of utils/bin/ has a name of one lowercase word with no
        suffix, a shebang, and mode 755.
      * A module of utils/assay/ has a name of lowercase words with
        underscores and the suffix .py, or the name __init__.py.
      * No other file has a shebang or mode 755.
      * A data file (.txt or .json) outside utils/tools/ has a name of
        lowercase words with hyphens.

THE GUIDES
    Claude Code loads each file with the name CLAUDE.md or AGENTS.md into
    each agent that works in its directory.  So only the root of the tree
    holds a file with one of these names, and a tracked file with such a name
    in another directory is an error of the kind guide-place, in each
    directory of the tree.  A plant cannot hold such a file: the copy of a
    plant (assay.guard.engines.meta.runner.copy_plant) gives each file of a
    plant whose name ends with .planted its name without that suffix, in the
    throwaway copy only.

THE KINDS
    no-row, two-rows, empty-row, master, module-name, command, data-name and
    guide-place.  During the migration of the utils system, the ledger admits
    each shell script of today, which is a command, and the change that makes
    the shell scripts modules removes its rows.

Complexity: O(f * p) for f files and p path rows.
"""

import os
from pathlib import Path, PurePosixPath

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import PLANTS, MetaContext

RULE = "layout"
MASTERS = "utils/bin/"
MODULES = "utils/assay/"
DATA_SUFFIXES = (".txt", ".json")
SHEBANG = b"#!"
GUIDE_NAMES = frozenset({"CLAUDE.md", "AGENTS.md"})


def is_word(text: str, separator: str) -> bool:
    """Whether a text is lowercase words of letters and digits with one separator between two words."""
    if not text or text[0] == separator or text[-1] == separator or separator * 2 in text:
        return False
    return all("a" <= character <= "z" or "0" <= character <= "9" or character == separator for character in text)


def in_scope(path: str) -> bool:
    """Whether the layout reads a file: each file of utils/, and each Python file or shell script outside it."""
    return path.startswith("utils/") or path.endswith((".py", ".sh"))


def naming(context: MetaContext, path: str) -> tuple[str, str] | None:
    """Return the kind and the message of the naming rule that a file of utils/ does not obey, or None."""
    if not path.startswith("utils/") or path.startswith(f"{PLANTS}/"):
        return None
    file = context.root / path
    with open(file, "rb") as handle:
        has_shebang = handle.read(2) == SHEBANG
    is_executable = bool(os.stat(file).st_mode & 0o111)
    name = PurePosixPath(path).name
    if path.startswith(MASTERS):
        if not (is_word(name, "-") and "-" not in name and has_shebang and is_executable):
            return "master", "a master has a name of one lowercase word with no suffix, a shebang and mode 755"
        return None
    if has_shebang or is_executable:
        return "command", ("the file has a shebang or mode 755, and only a master of utils/bin/ is a command.  "
                           "Make it a module of the package, which a master starts")
    if path.startswith(MODULES) and path.endswith(".py") and name != "__init__.py" and not is_word(name[:-3], "_"):
        return "module-name", "a module has a name of lowercase words with underscores"
    if (path.endswith(DATA_SUFFIXES) and not path.startswith("utils/tools/")
            and not is_word(name.rsplit(".", 1)[0], "-")):
        return "data-name", "a data file has a name of lowercase words with hyphens"
    return None


def guide_finding(path: str) -> check_report.Finding | None:
    """Return the error of a file with the name of a guide in a directory other than the root, or None (THE GUIDES)."""
    pure = PurePosixPath(path)
    if pure.name not in GUIDE_NAMES or pure.parent == PurePosixPath("."):
        return None
    return check_report.Finding(
        "error", path, 0, RULE, f"a file with the name {pure.name} is outside the root of the tree, and Claude Code "
        f"loads it into each agent that works in {pure.parent.as_posix()}/.  Give the file another name, or move its "
        f"text into the guide at the root", "guide-place", path)


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each file with no path row or two, each path row with no file, each file against its naming rule, and each
    guide outside the root."""
    rows = context.registry.paths
    matched: dict[str, int] = {row.glob: 0 for row in rows}
    found: list[check_report.Finding] = []
    for path in context.tracked():
        guide = guide_finding(path)
        if guide is not None:
            found.append(guide)
        if not in_scope(path):
            continue
        pure = PurePosixPath(path)
        holders = [row for row in rows if pure.full_match(row.glob)]
        for row in holders:
            matched[row.glob] += 1
        if not holders:
            found.append(check_report.Finding(
                "error", path, 0, RULE, f"no path row of the registry holds the file.  Give it a path row, or move "
                f"it to the place of a path row", "no-row", path))
        elif len(holders) > 1:
            found.append(check_report.Finding(
                "error", path, 0, RULE, f"two path rows hold the file: {holders[0].glob} and {holders[1].glob}.  "
                f"Make the globs disjoint", "two-rows", path))
        named = naming(context, path)
        if named is not None:
            found.append(check_report.Finding("error", path, 0, RULE, named[1], named[0], path))
    for row in rows:
        if matched[row.glob] == 0:
            found.append(check_report.Finding(
                "error", rule_registry.REGISTRY_PATH, row.line, RULE, f"the path row {row.glob} holds no file.  "
                f"Remove it", "empty-row", row.glob))
    return found
