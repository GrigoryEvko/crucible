"""prose-bans — the prose of the tree names no reference of the work, no retired axis number and no path that is gone.

THE RULE
    A comment, a docstring, a diagnostic, a script or a ledger row tells what
    the code does.  It names no task, no stage and no entry of a tracker that
    made the code.  A reader with no access to the tracker gets no meaning
    from the name, and the name becomes incorrect when the plan changes.  A
    doc-block of include/fixy/ names an axis by its enumerator and not by its
    retired number of FX.  A guide at the root of the tree names a file by
    its path in the tree.

THE KINDS
    coordination-ref  a reference of the work in the prose of a file.
                      COORDINATION gives its families.  They are a task, a tag
                      of a tracker, and the label of a stage, a phase or an
                      agent.  A preprocessor directive, a code of a collision
                      rule, a marker of a guard and a color look the same.  The
                      pull request of an upstream project looks the same too.
                      No branch matches them, and they are no reference.
    retired-axis      the axis numbering of FX in the prose of a C++ file of
                      include/fixy/.  AXIS_NUMBER gives the header shape and
                      the shape in parentheses.  The spelling of the enumerator
                      does not match.
    dead-path         a path in a guide that names no file.  A guide is a
                      regular file *.md at the root of the tree or in docs/.
                      A path starts with a name of PATH_ROOTS and a slash.  It
                      holds the characters of PATH_CHARACTERS, and it ends with
                      an extension of PROSE_EXTENSIONS.  The next character is
                      not a letter, a digit, an underscore or a hyphen.  The
                      message names each file of the tree with the same name.
                      The file most likely moved to one of them.
    unreadable        a C++ file that does not parse, or a Python file that
                      does not tokenize.  The rule cannot read its prose.
    The entity of a finding is the reference, the axis number or the path
    that is gone.  For a file that the rule cannot read, it is the path of the
    file.

THE SCOPE
    The rule reads the prose of each file that git holds, and of each new
    file that .gitignore does not match (assay.readers.prose).  It does not
    read the dated notes of misc/, this module and its plants, because they
    name references on purpose.  The rule ledger-owners holds the path of
    each ledger row, and this rule gives no finding for it.

THE CACHE
    The references and the axis numbers of one file come only from its path
    and its bytes.  The cache prose-bans of assay.core.cache keeps them, one
    entry for each file.  The name of an entry is the SHA-256 of this module,
    of the two readers, of the identity of the kit and of the file.  A change
    of one of them gives a different name, and an entry does not become
    stale.  A path that is gone is a fact of the other files of the tree.
    The rule reads each guide on each run for that reason.  With the caches
    off, the rule reads each file.

Complexity: one read and one hash of each file of the scope.  One read of the prose of each file that the cache does
not hold.  Linear in the number of the files of the tree for each path that is gone.
"""

import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.core import cache
from assay.core import report as check_report
from assay.guard.engines.meta.runner import PLANTS, MetaContext
from assay.readers import prose, tsast

RULE = "prose-bans"
CACHE_NAME = RULE
CACHE_FORMAT = b"prose-bans-results-1"
SELF = "utils/assay/guard/engines/meta/prose_bans.py"
SKIPPED_DIRECTORIES = ("misc/", f"{PLANTS}/{RULE}/")
FIXY = "include/fixy/"
CPP_SUFFIXES = (".h", ".hpp", ".cpp", ".cc")
DOCS = "docs/"
GUIDE_SUFFIX = ".md"
# One branch for each family of references.  Each branch starts with a character or a character class.  Its word
# boundary comes after that character, as `F(?<!\wF)` stands for a boundary and F.  The search then skips each
# position with a character that starts no branch.  The first look-behind of the branch of a task refuses an HTML
# entity, the argument count of a shell and a flag of printf.  It also refuses a bracket of a regular expression, an
# escape and the paste operator.  The second look-behind refuses the pull request of an upstream project.
COORDINATION = prose.Pattern(
    r"#(?<![&\w$%{\[\\#]#)(?<!\bPR #)[0-9]{2,4}\b",
    r"F(?<!\wF)IXY-(?:U|V|FOUND)-[0-9]",
    r"F(?<!\wF)IXY-[0-9]",
    r"F(?<!\wF)OUND[-_](?:[A-Z][0-9]*|[0-9]+)\b",
    r"G(?<!\wG)APS[-_][0-9]",
    r"S(?<!\wS)EPLOG-[A-Z]?[0-9]",
    r"C(?<!\wC)ONTRACT-[0-9]",
    r"M(?<!\wM)ETX-[A-Z0-9]",
    r"W(?<!\wW)RAP-(?:\*|[0-9]+\b|[A-Z][A-Za-z]*(?:-[A-Za-z]+)*-[0-9]+)",
    r"A(?<!\wA)UDIT-[A-Z]\b",
    r"a(?<!\wa)udit-[A-Z]\b",
    r"[A-Z](?<!\w[A-Z])[0-9]{2}-AUDIT\b",
    r"B(?<!\wB)C-[0-9]+\b",
    r"P(?<!\wP)ERF-[0-9]",
    r"C(?<!\wC)R-[0-9]{2}\b",
    r"f(?<!\wf)ixy-[A-Z]+[0-9]*-(?:[0-9]+\b|\*|X{3}\b)",
    r"f(?<!\wf)ix-[0-9]+\b",
    r"[UV](?<![\w-][UV])-[0-9]{3}[a-z]?\b",
    r"A(?<![\w.\-/]A)[0-9]{1,2}(?:\.(?:[0-9]+|x))+(?![\w\-])",
    r"S(?<!\wS)tage\s+[A-D](?:[0-9]{1,2}(?:\.[0-9]+)*)?\b",
    r"a(?<!\wa)t\s+A[0-9]{1,2}\b",
    r"t(?<!\wt)asks?\s+A[0-9]{1,2}\b",
    r"s(?<!\ws)ince\s+A[0-9]{1,2}\b",
    r"u(?<!\wu)ntil\s+A[0-9]{1,2}\b",
    r"P(?<!\wP)hase\s+[A-Z][0-9]+\b",
    r"A(?<!\wA)gent\s+[0-9]+\b",
)
AXIS_NUMBER = prose.Pattern(r"\bDim [0-9]{1,2}\b", r"\(dim [0-9]{1,2}[,)]")
PATH_ROOTS = (
    "include",
    "src",
    "test",
    "bench",
    "utils",
    "vessel",
    "examples",
    "cmake",
    "fuzz",
    "lean",
    "scripts",
    "tools",
    "toolchain",
)
ROOT_INITIALS = frozenset(name[0] for name in PATH_ROOTS)
PROSE_EXTENSIONS = frozenset({"h", "hpp", "cpp", "cc", "sh", "txt", "py", "json"})
LETTERS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
PATH_CHARACTERS = LETTERS | frozenset("_/.-")
WORD_CHARACTERS = LETTERS | frozenset("_-")
COORDINATION_KIND = "coordination-ref"
AXIS_KIND = "retired-axis"
UNREADABLE_KIND = "unreadable"
# The characters of a line that a message shows.
SHOWN_TEXT = 160
COORDINATION_ACTION = (
    "Tell what the code does, with no reference to the work that made it, and remove the sentence when it says "
    "nothing without the reference"
)
AXIS_ACTION = "Name the axis by its enumerator, DimensionAxis::NAME = ORDINAL"
GONE_ACTION = (
    "No file of the tree has that name.  Tell that the item is gone, or write a placeholder that does not read as a "
    "path"
)


def is_skipped(path: str) -> bool:
    """Return True for a file that the rule does not read (THE SCOPE)."""
    return path == SELF or path.startswith(SKIPPED_DIRECTORIES)


def is_guide(context: MetaContext, path: str) -> bool:
    """Return True for a guide of the kind dead-path: a regular file *.md at the root or in docs/."""
    is_placed = "/" not in path or path.startswith(DOCS)
    file = context.root / path
    return is_placed and path.endswith(GUIDE_SUFFIX) and file.is_file() and not file.is_symlink()


def path_at(line: str, start: int) -> tuple[str, int] | None:
    """Return the longest path of the kind dead-path that starts at one position of a line, and the end of the match.

    The end of the match is after the character that ends the path.
    Complexity: linear in the length of the run of path characters.
    """
    root = next((name for name in PATH_ROOTS if line.startswith(f"{name}/", start)), None)
    if root is None:
        return None
    body = start + len(root) + 1
    stop = body
    while stop < len(line) and line[stop] in PATH_CHARACTERS:
        stop += 1
    for end in range(stop, body, -1):
        candidate = line[start:end]
        dot = candidate.rfind(".")
        if dot <= len(root) + 1 or candidate[dot + 1 :] not in PROSE_EXTENSIONS:
            continue
        if end < len(line) and line[end] in WORD_CHARACTERS:
            continue
        return candidate, min(end + 1, len(line))
    return None


def paths_in(line: str) -> list[str]:
    """Return each path of the kind dead-path in one line, in the sequence of the line.

    Complexity: O(n * r) for n characters and the length r of the longest run of path characters.
    """
    found: list[str] = []
    position = 0
    while position < len(line):
        match = path_at(line, position) if line[position] in ROOT_INITIALS else None
        if match is None:
            position += 1
            continue
        found.append(match[0])
        position = match[1]
    return found


@dataclass(frozen=True, slots=True)
class Hit:
    """One prose line that a pattern of the rule matches: its kind, its line, the match and the start of the line."""

    kind: str
    line: int
    entity: str
    shown: str

    def finding(self, path: str) -> check_report.Finding:
        """Return the error of the hit in a file."""
        action = AXIS_ACTION if self.kind == AXIS_KIND else COORDINATION_ACTION
        message = f"the prose names {self.entity!r}: {self.shown!r}.  {action}"
        return check_report.Finding("error", path, self.line, RULE, message, self.kind, self.entity)


def hits_of(item: prose.FileProse) -> list[Hit]:
    """Return each prose line of a file that the pattern of a kind of the file matches (THE KINDS).

    Complexity: linear in the length of the prose of the file.
    """
    patterns = [(COORDINATION_KIND, COORDINATION)]
    if item.path.startswith(FIXY) and item.path.endswith(CPP_SUFFIXES):
        patterns.append((AXIS_KIND, AXIS_NUMBER))
    hits: list[Hit] = []
    for line in item.lines:
        for kind, pattern in patterns:
            match = pattern.first(line.content)
            if match is not None:
                hits.append(Hit(kind, line.line, match, line.content.strip()[:SHOWN_TEXT]))
    return hits


def results_identity() -> bytes:
    """Return the SHA-256 of the code that gives the hits of a file: this module, the two readers and the kit."""
    digest = hashlib.sha256(CACHE_FORMAT)
    for source in (Path(__file__), Path(prose.__file__), Path(tsast.__file__)):
        digest.update(source.read_bytes())
    digest.update(tsast.parser_identity().encode())
    return digest.digest()


def entry_name(identity: bytes, root: Path, path: str) -> str:
    """Return the name of the entry of a file in the cache (THE CACHE): a link counts with the path of its target."""
    file = root / path
    is_link = file.is_symlink()
    data = os.fsencode(os.readlink(file)) if is_link else file.read_bytes()
    digest = hashlib.sha256(identity)
    digest.update(b"link\0" if is_link else b"file\0")
    digest.update(path.encode())
    digest.update(b"\0")
    digest.update(data)
    return digest.hexdigest()


def stored_hits(directory: Path, name: str) -> list[Hit] | None:
    """Return the hits of an entry of the cache, or None when the cache holds no entry or a damaged one."""
    entry = directory / name[:2] / name
    try:
        used = entry.stat().st_mtime
        records = json.loads(entry.read_bytes())
        hits = [Hit(str(kind), int(line), str(entity), str(shown)) for kind, line, entity, shown in records]
    except OSError, ValueError, TypeError:
        return None
    cache.mark_used(entry, used)
    return hits


def store_hits(directory: Path, name: str, hits: list[Hit]) -> None:
    """Write the hits of a file to its entry of the cache."""
    records = [[hit.kind, hit.line, hit.entity, hit.shown] for hit in hits]
    cache.write_atomic(directory / name[:2] / name, json.dumps(records).encode())


def dead_path_findings(
    context: MetaContext, item: prose.FileProse, tracked: tuple[str, ...]
) -> list[check_report.Finding]:
    """Return one error for each path of a guide that names no file, at the first line that names it.

    Complexity: linear in the length of the guide, and in the number of the files of the tree for each path that is
    gone.
    """
    found: list[check_report.Finding] = []
    seen: set[str] = set()
    for line in item.lines:
        for path in paths_in(line.content):
            if path in seen or (context.root / path).exists():
                continue
            seen.add(path)
            name = PurePosixPath(path).name
            moved = [other for other in tracked if PurePosixPath(other).name == name and other != path]
            action = (
                f"The file most likely moved to {', '.join(moved)}.  Name the path of the file"
                if moved
                else GONE_ACTION
            )
            message = f"the guide names {path}, which is not in the tree.  {action}"
            found.append(check_report.Finding("error", item.path, line.line, RULE, message, "dead-path", path))
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each reference of the work, each retired axis number and each path that is gone (THE KINDS).

    Raises:
        NotApplicable: If the root is not a git work tree
    """
    root = context.root
    tracked = context.tracked()
    scope = [
        path for path in tracked if not is_skipped(path) and ((root / path).is_symlink() or (root / path).is_file())
    ]
    directory = cache.cache_root(CACHE_NAME)
    names: dict[str, str] = {}
    found: list[check_report.Finding] = []
    missed: list[str] = []
    if directory is None:
        missed = scope
    else:
        cache.hold_bound(directory, cache.LIMITS[CACHE_NAME])
        identity = results_identity()
        for path in scope:
            names[path] = entry_name(identity, root, path)
            hits = stored_hits(directory, names[path])
            if hits is None:
                missed.append(path)
            else:
                found += [hit.finding(path) for hit in hits]
    for item in prose.prose_of(root, missed):
        if item.problem:
            message = f"{item.problem}.  The rule cannot read the prose of the file.  Correct the file"
            found.append(check_report.Finding("error", item.path, 0, RULE, message, UNREADABLE_KIND, item.path))
            continue
        hits = hits_of(item)
        if directory is not None:
            store_hits(directory, names[item.path], hits)
        found += [hit.finding(item.path) for hit in hits]
    guides = [path for path in scope if is_guide(context, path)]
    for item in prose.prose_of(root, guides):
        found += dead_path_findings(context, item, tracked)
    return found
