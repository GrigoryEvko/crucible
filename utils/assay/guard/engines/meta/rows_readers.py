"""rows-readers — the C++ and the Python readers of the rows grammar give the same rows and refuse at the same line.

THE TWO READERS
    assay.core.rows reads the rows grammar for the package.  The quarantine
    plugin of GCC reads the same grammar through utils/tools/quarantine/rows.h.
    The host program utils/tools/quarantine/rows_reader.cpp reads each file
    that its arguments name with rows.h, and it prints one line of JSON for
    each file.  The rule gives the same files to the two readers, and it
    compares their results.

THE HOST PROGRAM
    The rule compiles rows_reader.cpp with the C++ compiler of the patched GCC
    (assay.setup.gcc.toolchain_compilers) and the flags of the plugin.  The
    program links libstdc++ statically, and it runs with no library path.  The
    program goes into the cache rows-reader of assay.core.cache.  The name of
    its entry is the SHA-256 of rows.h, rows_reader.cpp, the path of the
    compiler and the flags.  A second run with the same inputs does not
    compile again.  With the caches off, the program goes into a scratch
    directory for one run.  With no compiler, the rule does not apply to the
    tree.

THE FILES
    The rule reads each rows file of the tree that is on the disk.  These are
    the registry, the pin table, the rule table, the audit table and each
    ledger of utils/ledgers/.  The two readers must accept each one with the
    same rows.  When the root
    holds the file verdicts.txt (a copy of a plant has one), each of its rows
    gives the verdict of one file:

        format 1
        file PATH accepted
        file PATH refused LINE

    PATH is relative to the root.  The two readers must give that verdict, and
    for a refused file the line of the row.

THE KINDS
    disagree  the two readers give different rows, one reader refuses a file
              that the other accepts, or the two refuse a file at different
              lines
    refused   the two readers refuse a file that has no row in verdicts.txt,
              or a file that its row gives as accepted
    verdict   the two readers accept a file that its row gives as refused, or
              refuse it at a different line.  Also a row of verdicts.txt that
              does not obey its schema
    The entity of a finding is the path of the file.

Complexity: linear in the number of bytes of the files, and one compile of the host program when the cache has no
entry.
"""

import contextlib
import hashlib
import json
import os
import tempfile
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.core import cache, proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT, listing
from assay.guard.engines.meta.runner import MetaContext, NotApplicable, copy_plant

RULE = "rows-readers"
SOURCES = REPO_ROOT / "utils" / "tools" / "quarantine"
HEADER = SOURCES / "rows.h"
PROGRAM_SOURCE = SOURCES / "rows_reader.cpp"
PROGRAM_NAME = "rows_reader"
# The flags of the quarantine plugin, and the link flags that let the program run with no library path.
COMPILE_FLAGS = ("-std=gnu++17", "-fno-rtti", "-fPIC", "-O2", "-g", "-Wall", "-Wextra", "-Werror")
LINK_FLAGS = ("-static-libstdc++", "-static-libgcc")
CACHE_NAME = "rows-reader"
# The rows files of a tree that the rule reads when they are on the disk, and the directory of the ledgers.
TREE_FILES = ("utils/registry.txt", "utils/pins.txt", "utils/rule-table.txt", "utils/admitted-audit.txt")
LEDGERS = "utils/ledgers"
VERDICTS = "verdicts.txt"
FORMAT_ROW = ("format", "1")
FILE_KIND = "file"
ACCEPTED = "accepted"
REFUSED = "refused"
# The largest line of a row, which is the largest int of rows.h.
MAX_LINE = 0x7FFFFFFF

# One row as the two readers give it: its line, its words and its cells.
RowValue = tuple[int, tuple[str, ...], tuple[str, ...]]


@dataclass(frozen=True, slots=True)
class Verdict:
    """The verdict that a file must get, and the line of its row in verdicts.txt, or 0 for a file of the tree.

    Attributes:
        refused_at: The line at which the readers must refuse the file, or 0 when they must accept it
        declared_at: The line of the row of the file in verdicts.txt, or 0
    """

    refused_at: int = 0
    declared_at: int = 0


@dataclass(frozen=True, slots=True)
class Result:
    """The result of one reader on one file: its rows, or the line and the message of its first problem.

    Attributes:
        rows: The rows of the file, when the reader accepts it
        refused_at: The line of the first problem, or 0 when the reader accepts the file
        message: The message of the first problem
    """

    rows: tuple[RowValue, ...] = ()
    refused_at: int = 0
    message: str = ""


def program_key(compiler: Path) -> str:
    """Return the name of the cache entry of the host program: the SHA-256 of its sources, the compiler and the flags.

    The length of each part goes into the hash before the part.  Then two different sets of parts do not give the
    same bytes.
    """
    digest = hashlib.sha256()
    parts = (
        HEADER.read_bytes(),
        PROGRAM_SOURCE.read_bytes(),
        str(compiler).encode(),
        "\0".join((*COMPILE_FLAGS, *LINK_FLAGS)).encode(),
    )
    for part in parts:
        digest.update(len(part).to_bytes(8, "little"))
        digest.update(part)
    return digest.hexdigest()


def compile_program(compiler: Path, output: Path) -> None:
    """Compile and link the host program into OUTPUT.

    Raises:
        proc.ToolFailed: If the compiler cannot start, runs longer than its time limit or refuses the program
    """
    proc.require(
        proc.run(
            proc.Command(
                [str(compiler), *COMPILE_FLAGS, *LINK_FLAGS, "-o", str(output), str(PROGRAM_SOURCE)],
                timeout_s=proc.TOOL_S,
            )
        )
    )


@contextlib.contextmanager
def host_program() -> Iterator[Path]:
    """Give the path of the host program, and compile it when the cache has no entry (THE HOST PROGRAM).

    A compile writes a file with a dot at the start of its name, and then renames it.  A run in a different process
    then sees all of the program or none of it.  The eviction of the cache does not read a part of a program.

    Raises:
        NotApplicable: If the C++ compiler of the patched GCC is not on the disk
        proc.ToolFailed: If the compile fails
    """
    from assay.setup import gcc

    compiler = gcc.toolchain_compilers()[1]
    if not compiler.is_file():
        raise NotApplicable(
            f"the C++ compiler {compiler} of the patched GCC is not on the disk.  Build it with "
            f"`python3 utils/bin/setup gcc PREFIX`, or name it in CRUCIBLE_CXX"
        )
    directory = cache.cache_root(CACHE_NAME)
    if directory is None:
        with tempfile.TemporaryDirectory(prefix="rows-reader-") as scratch:
            program = Path(scratch) / PROGRAM_NAME
            compile_program(compiler, program)
            yield program
        return
    # The rule cache-size gives a warning for a cache with no row of LIMITS, and the eviction reads that row.
    limit = cache.LIMITS.get(CACHE_NAME)
    if limit is not None:
        cache.hold_bound(directory, limit)
    key = program_key(compiler)
    program = directory / key[:2] / key
    try:
        cache.mark_used(program, program.stat().st_mtime)
    except FileNotFoundError:
        program.parent.mkdir(parents=True, exist_ok=True)
        staging = program.with_name(f".{key}.{os.getpid()}.tmp")
        try:
            compile_program(compiler, staging)
            os.replace(staging, program)
        finally:
            staging.unlink(missing_ok=True)
    yield program


def text_list(value: object) -> tuple[str, ...] | None:
    """Return a JSON list of texts as a tuple, or None when the value is not such a list."""
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        return None
    return tuple(str(item) for item in value)


def result_of(line: bytes, path: str) -> Result | None:
    """Return the result of one line of the output of the host program, or None when the line has a different shape.

    Args:
        line: One line of the output, with no line break
        path: The path that the line must name
    """
    try:
        record = json.loads(line)
    except ValueError:
        return None
    if not isinstance(record, dict) or record.get("path") != path:
        return None
    problem = record.get("problem")
    if isinstance(problem, dict):
        refused_at, message = problem.get("line"), problem.get("message")
        if isinstance(refused_at, int) and refused_at > 0 and isinstance(message, str):
            return Result((), refused_at, message)
        return None
    rows = record.get("rows")
    if not isinstance(rows, list):
        return None
    values: list[RowValue] = []
    for item in rows:
        if not isinstance(item, dict):
            return None
        at, words, cells = item.get("line"), text_list(item.get("words")), text_list(item.get("cells"))
        if not isinstance(at, int) or words is None or cells is None:
            return None
        values.append((at, words, cells))
    return Result(tuple(values))


def cxx_results(program: Path, paths: Sequence[Path]) -> list[Result]:
    """Run the host program one time on each path, and return the result of each file, in the sequence of the paths.

    Raises:
        proc.ToolFailed: If the program cannot start, ends with a status that is not 0, or gives output of a different
            shape

    Complexity: linear in the number of bytes of the files.
    """
    command = proc.Command([str(program), *(str(path) for path in paths)], timeout_s=proc.QUICK_S)
    completed = proc.require(proc.run(command))
    lines = completed.stdout.split(b"\n")
    results: list[Result] = []
    if len(lines) == len(paths) + 1 and not lines[-1]:
        for line, path in zip(lines, paths, strict=False):
            result = result_of(line, str(path))
            if result is None:
                break
            results.append(result)
    if len(results) != len(paths):
        raise proc.ToolFailed(
            f"the host program {program} does not give one line of JSON for each of the {len(paths)} files.  Make "
            f"rows_reader.cpp print the lines of THE HOST PROGRAM",
            command,
            completed,
        )
    return results


def python_result(path: Path, shown: str) -> Result:
    """Return the result of assay.core.rows on one file."""
    try:
        rows = rows_grammar.read(path, shown)
    except rows_grammar.RowsError as problem:
        return Result((), problem.line, str(problem))
    return Result(tuple((row.line, row.words, row.cells) for row in rows))


def declared(root: Path) -> dict[str, Verdict]:
    """Return the verdict of each file that a row of verdicts.txt names, or no verdict when the root has no such file.

    Raises:
        rows_grammar.RowsError: If verdicts.txt does not obey its schema, or a row names no file of the root

    Complexity: linear in the number of bytes of verdicts.txt.
    """
    path = root / VERDICTS
    if not path.is_file():
        return {}
    rows = rows_grammar.read(path, VERDICTS)
    if not rows or rows[0].words != FORMAT_ROW or rows[0].cells:
        raise rows_grammar.RowsError(
            VERDICTS, rows[0].line if rows else 1, "the first row is not `format 1`.  Start the file with it"
        )
    verdicts: dict[str, Verdict] = {}
    for row in rows[1:]:
        words = row.words
        if words[0] != FILE_KIND or row.cells or (words[2:3], len(words)) not in (((ACCEPTED,), 3), ((REFUSED,), 4)):
            raise row.error(f"the row is not `{FILE_KIND} PATH {ACCEPTED}` or `{FILE_KIND} PATH {REFUSED} LINE`")
        named = PurePosixPath(words[1])
        if named.is_absolute() or ".." in named.parts or not (root / named).is_file():
            raise row.error(f"the path {words[1]} names no file below the root.  Give the path of a file of the plant")
        key = named.as_posix()
        if key in verdicts:
            raise row.error(f"the file {key} has a row at line {verdicts[key].declared_at} too.  Remove one row")
        refused_at = rows_grammar.number(words[3], MAX_LINE, row, "line") if words[2] == REFUSED else 0
        if words[2] == REFUSED and refused_at == 0:
            raise row.error("the line of a refused file is 0, and the lines of a file start at 1")
        verdicts[key] = Verdict(refused_at, row.line)
    return verdicts


def described(result: Result) -> str:
    """Return what a reader does with a file, for a message."""
    if result.refused_at:
        return f"refuses it at line {result.refused_at} ({result.message})"
    return f"accepts it with {len(result.rows)} rows"


def disagreement(path: str, ours: Result, theirs: Result) -> check_report.Finding | None:
    """Return the finding of the kind disagree for one file, or None when the two readers give the same result."""
    if (ours.refused_at, ours.rows) == (theirs.refused_at, theirs.rows):
        return None
    if ours.refused_at or theirs.refused_at:
        line = ours.refused_at or theirs.refused_at
        message = f"the Python reader {described(ours)}, and the C++ reader {described(theirs)}"
    else:
        pairs = list(zip(ours.rows, theirs.rows, strict=False))
        first = next((pair for pair in pairs if pair[0] != pair[1]), None)
        if first is None:
            line = 0
            message = f"the Python reader gives {len(ours.rows)} rows, and the C++ reader gives {len(theirs.rows)}"
        else:
            line = first[0][0]
            message = f"the Python reader gives the row {first[0]!r}, and the C++ reader gives {first[1]!r}"
    return check_report.Finding(
        "error", path, line, RULE, f"{message}.  Make rows.h read the grammar of assay.core.rows", "disagree", path
    )


def verdict_findings(path: str, verdict: Verdict, result: Result) -> list[check_report.Finding]:
    """Return the finding of the kind refused or verdict for a file that the two readers read the same way."""
    if result.refused_at == verdict.refused_at:
        return []
    row = f"the row at line {verdict.declared_at} of {VERDICTS}"
    if verdict.refused_at == 0:
        given = (
            f"{row} gives it as accepted.  Correct the file, or correct its row"
            if verdict.declared_at
            else "the file must obey the grammar.  Correct the file"
        )
        message = f"the two readers refuse the file at line {result.refused_at} ({result.message}), and {given}"
        return [check_report.Finding("error", path, result.refused_at, RULE, message, REFUSED, path)]
    found = f"refuse it at line {result.refused_at}" if result.refused_at else "accept it"
    message = (
        f"{row} gives the file as refused at line {verdict.refused_at}, and the two readers {found}.  Correct the "
        f"row or the file"
    )
    return [check_report.Finding("error", path, result.refused_at, RULE, message, "verdict", path)]


def files_of(context: MetaContext) -> dict[str, Verdict]:
    """Return each file that the rule reads and its verdict (THE FILES), sorted by path.

    Raises:
        rows_grammar.RowsError: If verdicts.txt does not obey its schema

    Complexity: linear in the number of ledgers and in the number of bytes of verdicts.txt.
    """
    root = context.root
    files = {path: Verdict() for path in TREE_FILES if (root / path).is_file()}
    files.update(
        {path.relative_to(root).as_posix(): Verdict() for path in listing(root / LEDGERS, "*.txt") if path.is_file()}
    )
    files.update(declared(root))
    return dict(sorted(files.items()))


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each file that the two readers read differently, or that does not get its verdict (THE KINDS).

    Complexity: linear in the number of bytes of the files.
    """
    try:
        files = files_of(context)
    except rows_grammar.RowsError as problem:
        message = f"{VERDICTS} does not obey its schema: {problem}.  Correct the row"
        return [check_report.Finding("error", VERDICTS, problem.line, RULE, message, "verdict", VERDICTS)]
    if not files:
        return []
    paths = [context.root / path for path in files]
    with host_program() as program:
        theirs = cxx_results(program, paths)
    found: list[check_report.Finding] = []
    for (path, verdict), absolute, cxx in zip(files.items(), paths, theirs, strict=True):
        ours = python_result(absolute, path)
        disagreed = disagreement(path, ours, cxx)
        found += [disagreed] if disagreed is not None else verdict_findings(path, verdict, ours)
    return found


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository with copy_plant(), and give the context of the copy."""
    copy_plant(plant, scratch)
    return MetaContext(scratch)
