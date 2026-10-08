"""clang-format — each C++ file of the tree agrees with the .clang-format of the tree.

The .clang-format of the tree is explicit and not inherited.  An upgrade of
the formatter then cannot move the style with no sign.  This rule is the
other half: it refuses a file that the configuration changes.

THE FILES
    Each file of the git index with a suffix of SUFFIXES.  A file in a
    directory papers or in a directory that starts with build is not in the
    scope.  A file vmlinux.h is not in the scope too.  clang-format reads
    .clang-format-ignore itself.

THE FORMATTER
    CLANG_FORMAT names the formatter when it is set.  Else the rule takes
    clang-format when the first number of its version is in VERIFIED_MAJORS,
    then clang-format-23, then clang-format.  Version 22.1.8 and version
    23.1.0 give the same bytes for this tree with this configuration.  A
    formatter of a different first number can report a change that is not
    there.  The rule does not apply with such a formatter, or with no
    formatter (applies()).

THE RUNS
    The rule runs `clang-format --output-replacements-xml` on batches of
    files, and assay.readers.tool_reports reads one document for each file.
    A batch with an exit status that is not 0, or with a different number of
    documents, runs again with one process for each file.  A file
    that .clang-format-ignore names, and a file that the formatter cannot
    read, then get a verdict of their own.  An executable formatter runs
    MAX_JOBS batches at the same time, and the batches have about the same
    bytes (batches_of()).  A script, such as a wrapper that starts a
    container, runs one batch of BATCH files at a time.  Each call of the
    formatter reads no standard input.

THE CACHE
    The cache clang-format of assay.core.cache keeps one entry for each file
    that the formatter does not change.  The name of the entry is the SHA-256
    of the path and the bytes of the file, and of the identity of the run:
    this module, the reader, the formatter, its version, .clang-format and
    .clang-format-ignore.  A change of one of them gives a different name.  A
    cached result then cannot hide a change.

THE KINDS
    `drift`       an error: the formatter changes the file.  The line of the
                  finding is the line of the first change
    `incomplete`  an error: the formatter cannot format all of the file
    `failed`      an error: the formatter exits with a status that is not 0
                  on the file alone, or its report has no valid shape.  A
                  tree with no file of THE FILES gives this error at
                  .clang-format
    The entity of each finding is empty, and the path names the file.

Complexity: one read and one hash of each file of the scope, and one
formatter run for each file that the cache does not hold.
"""

import hashlib
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

from assay.core import cache, proc
from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext, NotApplicable
from assay.readers import elf, tool_reports

RULE = "clang-format"
SUFFIXES = frozenset({".h", ".hpp", ".cpp", ".cc", ".cxx"})
EXCLUDED_DIRECTORY = "papers"
EXCLUDED_PREFIX = "build"
EXCLUDED_NAME = "vmlinux.h"
VERIFIED_MAJORS = frozenset({22, 23})
FORMATTER_VARIABLE = "CLANG_FORMAT"
CANDIDATES = ("clang-format-23", "clang-format")
CONFIGURATION_FILES = (".clang-format", ".clang-format-ignore")
BATCH = 48
MAX_JOBS = 32
BATCHES_PER_JOB = 4
CACHE_NAME = "clang-format"
CACHE_FORMAT = b"clang-format-verdicts-1"
CLEAN = b"clean\n"
XML_OPTION = "--output-replacements-xml"


@dataclass(frozen=True, slots=True)
class Formatter:
    """The formatter of a run: the name of its call, its path, its version line, and True for an executable."""

    program: str
    path: Path
    version: str
    is_executable: bool


@dataclass(frozen=True, slots=True)
class Verdict:
    """The verdict of the formatter on one file: its kind, or an empty kind for a clean file, its line and a text."""

    kind: str
    line: int = 0
    detail: str = ""


def version_of(program: str) -> str:
    """Return the version line of a formatter, or an empty text when it does not run."""
    try:
        done = proc.run(proc.Command([program, "--version"], timeout_s=proc.QUICK_S))
    except proc.ToolFailed:
        return ""
    lines = done.out.strip().splitlines()
    return lines[0] if done.exit == 0 and lines else ""


def major_of(version: str) -> int | None:
    """Return the first number of the word after the word version in a version line, or None."""
    words = version.split()
    for index, word in enumerate(words[:-1]):
        if word == "version":
            head = words[index + 1].split(".")[0]
            return int(head) if head.isdigit() else None
    return None


def chosen_formatter() -> Formatter:
    """Return the formatter of THE FORMATTER.

    Raises:
        NotApplicable: If no formatter is found, or the first number of its version is not in VERIFIED_MAJORS
    """
    named = os.environ.get(FORMATTER_VARIABLE, "")
    if named:
        candidates: tuple[str, ...] = (named,)
    elif major_of(version_of("clang-format")) in VERIFIED_MAJORS:
        candidates = ("clang-format",)
    else:
        candidates = CANDIDATES
    for program in candidates:
        found = shutil.which(program)
        if found is None:
            continue
        version = version_of(program)
        if major_of(version) not in VERIFIED_MAJORS:
            raise NotApplicable(
                f"the formatter {program} gives the version {version!r}, and only the versions "
                f"{', '.join(str(major) for major in sorted(VERIFIED_MAJORS))} give the bytes of this tree"
            )
        return Formatter(program, Path(found), version, elf.is_elf(found))
    raise NotApplicable(f"no clang-format is installed.  Install clang-format 22 or 23, or set {FORMATTER_VARIABLE}")


def applies() -> None:
    """Return when a formatter of THE FORMATTER is installed.

    Raises:
        NotApplicable: If no such formatter is installed
    """
    chosen_formatter()


def is_in_scope(path: str) -> bool:
    """Return True for a file of THE FILES."""
    parts = Path(path).parts
    if Path(path).suffix not in SUFFIXES or parts[-1] == EXCLUDED_NAME:
        return False
    return not any(part == EXCLUDED_DIRECTORY or part.startswith(EXCLUDED_PREFIX) for part in parts[:-1])


def run_identity(root: Path, formatter: Formatter) -> bytes:
    """Return the SHA-256 of each input of a verdict other than the file itself (THE CACHE)."""
    digest = hashlib.sha256(CACHE_FORMAT)
    for source in (Path(__file__), Path(tool_reports.__file__), formatter.path):
        digest.update(source.read_bytes())
    digest.update(formatter.version.encode())
    for name in CONFIGURATION_FILES:
        configuration = root / name
        digest.update(f"\0{name}\0".encode())
        if configuration.is_file():
            digest.update(configuration.read_bytes())
    return digest.digest()


def entry_name(identity: bytes, path: str, data: bytes) -> str:
    """Return the name of the entry of one file in the cache (THE CACHE)."""
    digest = hashlib.sha256(identity)
    digest.update(path.encode())
    digest.update(b"\0")
    digest.update(data)
    return digest.hexdigest()


def is_cached(directory: Path, name: str) -> bool:
    """Return True when the cache holds the entry of a clean file, and mark the entry as used."""
    entry = directory / name[:2] / name
    try:
        used = entry.stat().st_mtime
        is_clean = entry.read_bytes() == CLEAN
    except OSError:
        return False
    cache.mark_used(entry, used)
    return is_clean


def document_verdict(root: Path, path: str, document: tool_reports.FormatDocument) -> Verdict:
    """Return the verdict of the document of one file."""
    if document.is_incomplete:
        return Verdict("incomplete")
    if not document.replacements:
        return Verdict("")
    first = min(replacement.offset for replacement in document.replacements)
    line = (root / path).read_bytes().count(b"\n", 0, first) + 1
    return Verdict("drift", line, str(len(document.replacements)))


def run_formatter(root: Path, formatter: Formatter, files: list[str]) -> proc.Completed | None:
    """Run the formatter on files with an empty standard input, or return None when it cannot run."""
    command = proc.Command([formatter.program, XML_OPTION, *files], timeout_s=proc.TOOL_S, cwd=root)
    try:
        return proc.run(command)
    except proc.ToolFailed:
        return None


def single_verdict(root: Path, formatter: Formatter, path: str) -> Verdict:
    """Return the verdict of the formatter on one file alone (THE RUNS)."""
    done = run_formatter(root, formatter, [path])
    if done is None:
        return Verdict("failed", 0, "the formatter cannot run, or it runs past its time limit")
    if done.exit != 0:
        lines = (done.err.strip() or done.out.strip()).splitlines()
        return Verdict("failed", 0, f"exit {done.exit}: {lines[0] if lines else 'no output'}")
    try:
        documents = tool_reports.clang_format_documents(done.out)
    except tool_reports.ReportError as problem:
        return Verdict("failed", 0, str(problem))
    if not documents:
        return Verdict("")
    if len(documents) != 1:
        return Verdict("failed", 0, f"the report holds {len(documents)} documents for one file")
    return document_verdict(root, path, documents[0])


def batch_verdicts(root: Path, formatter: Formatter, files: list[str]) -> dict[str, Verdict]:
    """Return the verdict of each file of a batch, with one run for each file when the batch run fails (THE RUNS)."""
    done = run_formatter(root, formatter, files)
    documents: tuple[tool_reports.FormatDocument, ...] = ()
    if done is not None and done.exit == 0:
        try:
            documents = tool_reports.clang_format_documents(done.out)
        except tool_reports.ReportError:
            documents = ()
    if len(documents) == len(files):
        return {path: document_verdict(root, path, document) for path, document in zip(files, documents, strict=True)}
    return {path: single_verdict(root, formatter, path) for path in files}


def batches_of(root: Path, formatter: Formatter, files: list[str]) -> list[list[str]]:
    """Put the files into batches (THE RUNS).

    An executable formatter gets BATCHES_PER_JOB times MAX_JOBS batches, or one batch for each file when there are
    fewer files.  The files go into the batches in turn, the largest file first.  The batches then take about the
    same time.  A script gets batches of BATCH files in sequence.

    Complexity: O(n log n) in the number of files, for the sort by bytes.
    """
    if not formatter.is_executable:
        return [files[start : start + BATCH] for start in range(0, len(files), BATCH)]
    count = min(len(files), MAX_JOBS * BATCHES_PER_JOB)
    batches: list[list[str]] = [[] for _ in range(count)]
    by_size = sorted(files, key=lambda path: (-(root / path).stat().st_size, path))
    for index, path in enumerate(by_size):
        batches[index % count].append(path)
    return batches


def all_verdicts(root: Path, formatter: Formatter, files: list[str]) -> dict[str, Verdict]:
    """Return the verdict of each file, with MAX_JOBS batches at the same time for an executable formatter.

    Complexity: one formatter run for each batch, and one more for each file of a batch that fails.
    """
    batches = batches_of(root, formatter, files)
    verdicts: dict[str, Verdict] = {}
    if formatter.is_executable and len(batches) > 1:
        from concurrent.futures import ThreadPoolExecutor

        with ThreadPoolExecutor(max_workers=min(MAX_JOBS, len(batches))) as pool:
            for result in pool.map(lambda batch: batch_verdicts(root, formatter, batch), batches):
                verdicts.update(result)
    else:
        for batch in batches:
            verdicts.update(batch_verdicts(root, formatter, batch))
    return verdicts


def finding_of(path: str, verdict: Verdict) -> check_report.Finding:
    """Return the error of a file that the formatter changes or cannot format (THE KINDS)."""
    if verdict.kind == "drift":
        message = (
            f"clang-format changes {verdict.detail} place(s) of the file under the .clang-format of the tree, the "
            f"first at line {verdict.line}.  Format the file with `clang-format -i {path}`, and commit the result on "
            f"its own"
        )
    elif verdict.kind == "incomplete":
        message = "clang-format cannot format the whole file.  Correct the code that it does not parse"
    else:
        message = f"clang-format gives no verdict on the file: {verdict.detail}.  Correct the file or the configuration"
    return check_report.Finding("error", path, verdict.line, RULE, message, verdict.kind)


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each file that the formatter changes or cannot format (THE KINDS).

    Raises:
        NotApplicable: If no formatter of THE FORMATTER is installed, or the root is not a git work tree
    """
    root = context.root
    formatter = chosen_formatter()
    scope = [path for path in context.tracked() if is_in_scope(path)]
    if not scope:
        message = f"the tree {root} holds no C++ file of the scope, and the rule gives no verdict.  Name the root"
        return [check_report.Finding("error", CONFIGURATION_FILES[0], 0, RULE, message, "failed")]
    directory = cache.cache_root(CACHE_NAME)
    identity = run_identity(root, formatter)
    names: dict[str, str] = {}
    missed: list[str] = []
    if directory is not None:
        cache.hold_bound(directory, cache.LIMITS[CACHE_NAME])
    for path in scope:
        names[path] = entry_name(identity, path, (root / path).read_bytes())
        if directory is None or not is_cached(directory, names[path]):
            missed.append(path)
    found: list[check_report.Finding] = []
    for path, verdict in sorted(all_verdicts(root, formatter, missed).items()):
        if verdict.kind:
            found.append(finding_of(path, verdict))
        elif directory is not None:
            cache.write_atomic(directory / names[path][:2] / names[path], CLEAN)
    return found
