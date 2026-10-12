"""decide-cite-ratio — each procedure of the catalog has two production cites 180 days after it entered the catalog.

The catalog is the namespace `decide` of Decide.h (CLAUDE.md section XII).  A
procedure of the catalog earns its row.  A site that cites it lands first,
and the named procedure lands second.  Each procedure has a minimum of
MIN_CITES production cites when GRACE_DAYS days went by after its first
commit in Decide.h.

THE COUNTS
    The module of the rule contract-cites reads the catalog and the cite count
    of each procedure from the parse trees of include/ and src/
    (contract_cites.scan()).  The rule reads the catalog of the work tree.

THE DATES
    The date of a procedure is the author date of the first commit with a
    Decide.h that holds its definition.  The rule reads each path of
    DECIDE_HEADERS.  It parses each committed version of the header, and a
    definition is a function definition in a namespace with the name `decide`
    (decide_catalog.procedures()).  A comment or a string that names a
    procedure is not a definition, and it gives no date.  A procedure that
    only the work tree holds has the date of HEAD.

    The age of a procedure is the number of days from its date to the author
    date of HEAD.  The wall clock does not change the verdict, and a tree
    gives the same verdict on each day.  A shallow history does not hold the
    first commit of a procedure, and the rule does not apply to it.

THE KINDS
    violation   an error: a procedure with fewer than MIN_CITES cites and an
                age of GRACE_DAYS days or more
    grace       a warning: a procedure with fewer than MIN_CITES cites and a
                smaller age
    unreadable  an error: a file of the scope or a committed version of
                Decide.h does not parse, and the rule cannot count or date
    The entity of a finding is the name of the procedure, or the catalog
    header when a file does not parse.

THE PLANTS
    A plant gives its history as directories history/DATE, with DATE in the
    shape YYYY-MM-DD.  plant_context() commits the files of each directory in
    the sequence of the dates, with that date, on the files of the earlier
    directories.  Then it puts each other file of the plant in the index,
    with no commit.

Complexity: one `git log` and one `git cat-file --batch` read the history,
and one kit run parses each version of the header.  The count is linear in
the nodes of include/ and src/.
"""

import datetime
from pathlib import Path

from assay.core import git
from assay.core import report as check_report
from assay.guard.engines.meta.runner import PLANTED_SUFFIX, MetaContext, NotApplicable
from assay.guard.engines.tsast import contract_cites, decide_catalog
from assay.readers import tsast

RULE = "decide-cite-ratio"
MIN_CITES = 2
GRACE_DAYS = 180
CATALOG = decide_catalog.DECIDE_HEADERS[0]
# The directory of the dated commits of a plant (THE PLANTS).
HISTORY = "history"


class Unreadable(Exception):
    """A file of the scope or a committed version of Decide.h does not parse.  The message names the file."""


def error(kind: str, entity: str, message: str) -> check_report.Finding:
    """Return one error of the rule at the catalog header."""
    return check_report.Finding("error", CATALOG, 0, RULE, message, kind, entity)


def warning(kind: str, entity: str, message: str) -> check_report.Finding:
    """Return one warning of the rule at the catalog header."""
    return check_report.Finding("warning", CATALOG, 0, RULE, message, kind, entity)


def head_date(root: Path) -> datetime.date:
    """Return the author date of HEAD.

    Raises:
        NotApplicable: If the tree has no commit
    """
    try:
        return git.author_date(root, "HEAD")
    except git.GitError as problem:
        raise NotApplicable(f"the tree {root} has no commit, and no procedure has an age: {problem}") from None


def definition_dates(root: Path) -> dict[str, datetime.date]:
    """Return the date of the first commit with a Decide.h that holds the definition of each procedure (THE DATES).

    Complexity: one `git log` and one `git cat-file --batch` read each version, and one kit run parses them.

    Raises:
        NotApplicable: If the history is shallow
        Unreadable: If a committed version of the header does not parse
    """
    if git.is_shallow(root):
        raise NotApplicable("the history is shallow, and it does not hold the first commit of each procedure")
    entries = git.dated_commits(root, decide_catalog.DECIDE_HEADERS)
    names = [f"{entry.sha}:{header}" for entry in entries for header in decide_catalog.DECIDE_HEADERS]
    contents = iter(git.blobs(root, names))
    versions: list[tuple[str, str]] = []
    version_dates: list[datetime.date] = []
    for entry in entries:
        for header in decide_catalog.DECIDE_HEADERS:
            content = next(contents)
            if content is None:
                continue
            try:
                versions.append((f"{entry.sha}:{header}", content.decode("utf-8")))
            except UnicodeDecodeError:
                raise Unreadable(f"the version {entry.sha}:{header} is not UTF-8") from None
            version_dates.append(entry.date)
    dates: dict[str, datetime.date] = {}
    try:
        for tree, date in zip(tsast.parse_texts(versions, strict=True), version_dates, strict=True):
            for procedure in decide_catalog.procedures(tree):
                dates.setdefault(procedure, date)
    except tsast.ParseError as problem:
        raise Unreadable(str(problem)) from None
    return dates


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each procedure with fewer than MIN_CITES cites, by its age (THE KINDS).

    Raises:
        NotApplicable: If the history is shallow or the tree has no commit
        tsast.KitMissing: If the pinned kit is not installed
    """
    root = context.root
    reference = head_date(root)
    try:
        counts = contract_cites.scan(root).procedures
        dates = definition_dates(root)
    except (contract_cites.AuditError, Unreadable, tsast.ParseError) as problem:
        shown_problem = " ".join(str(problem).split())
        message = f"the rule cannot count or date the procedures: {shown_problem}.  Correct the file"
        return [error("unreadable", CATALOG, message)]
    found: list[check_report.Finding] = []
    for name, cites in sorted(counts.items()):
        if cites >= MIN_CITES:
            continue
        date = dates.get(name, reference)
        age = (reference - date).days
        shown = (
            f"decide::{name} has {cites} production cite(s), and it entered {CATALOG} on {date.isoformat()}, "
            f"{age} days before HEAD"
        )
        if age >= GRACE_DAYS:
            message = (
                f"{shown}.  A procedure has {MIN_CITES} cites {GRACE_DAYS} days after its definition.  Cite it at a "
                f"second site, or remove it from the catalog"
            )
            found.append(error("violation", name, message))
        else:
            message = f"{shown}.  It must have {MIN_CITES} cites {GRACE_DAYS} days after its definition"
            found.append(warning("grace", name, message))
    return found


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Commit the dated directories of a plant, and put its other files in the index (THE PLANTS).

    Complexity: linear in the files of the plant, with one commit for each dated directory.
    """
    scratch.mkdir(parents=True)
    git.init(scratch)
    history = plant / HISTORY
    dated = sorted(path for path in history.iterdir() if path.is_dir()) if history.is_dir() else []
    for directory in dated:
        place_files(directory, scratch, ())
        stamp = f"{datetime.date.fromisoformat(directory.name).isoformat()}T12:00:00"
        git.commit_all(scratch, f"the files of {directory.name}", date=stamp)
    place_files(plant, scratch, (HISTORY,))
    git.run(scratch, "add", "-A", "--", ".")
    return MetaContext(scratch)


def place_files(source: Path, target: Path, skipped: tuple[str, ...]) -> None:
    """Copy each file in the directory source to target, with no top directory of skipped, and no suffix .planted.

    Complexity: linear in the bytes of the files.
    """
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if not path.is_file() or relative.parts[0] in skipped:
            continue
        destination = target / relative.as_posix().removesuffix(PLANTED_SUFFIX)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(path.read_bytes())
