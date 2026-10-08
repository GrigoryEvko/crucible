"""clang-format — the tree is clang-format clean, and stays clean (`guard check --rule clang-format`).

The .clang-format of the tree is explicit and not inherited, so an upgrade of
the formatter cannot move the style with no sign.  This check is the other
half: it fails when a file drifts from what the configuration gives.

THE FILES
    Each file with the suffix .h, .hpp, .cpp, .cc or .cxx that fd lists from
    the scan root, with no file under a directory build* or papers and no
    vmlinux.h.  fd reads .gitignore.  FD names a different fd, and the check
    also finds the name fdfind of Debian and Ubuntu.

THE SPEED
    The formatter takes about 270 s of CPU time for the tree, and the
    generated files of test/session_oracle take half of that time.  Two things
    keep a run short:
      * An executable formatter runs in parallel, one process for each batch.
        The files go into the batches in turn, the largest file first, so the
        batches take about the same time.  CRUCIBLE_CLANG_FORMAT_JOBS sets the
        number of processes.  The default is the CPU count, with a maximum of 32.
      * A cache keeps each file that the formatter found clean, keyed on the
        SHA-256 of its content.  The name of the cache file is the SHA-256 of
        the formatter, its version, .clang-format and .clang-format-ignore.  A
        file with a cached key needs no formatter run.  A change of the file,
        the formatter or the configuration gives a different key, so a cached
        result cannot hide a drift.  The check does not cache a file that
        drifts.  CRUCIBLE_CLANG_FORMAT_CACHE names the directory.  The default
        is ~/.cache/crucible/clang-format, and an empty value turns the cache
        off.  A cache file that no run used for 30 days goes.

A SCRIPT FORMATTER RUNS IN SEQUENCE
    clang-format 23 needs glibc 2.44, and this host has 2.43.  So
    ~/.local/bin/clang-format-23 is a script that runs the formatter in a
    container, which mounts the scan root with an SELinux relabel (`:Z`).
    Containers in parallel race on the relabel.  A container that loses the
    race cannot read .clang-format, uses the LLVM default style, and reports
    almost each file as a drift.  The check cannot see what a script starts,
    so it runs a script formatter in sequence, in batches of
    CRUCIBLE_CLANG_FORMAT_BATCH files (250 by default).

EACH FORMATTER CALL READS STDIN FROM /dev/null
    The container wrapper runs `podman run -i`, which sends stdin into the
    container.  When the stdin of a formatter call was the file list, the
    first batch got the remaining paths, and the check read only that batch.
    The self-test runs a formatter that reads its stdin, and it requires the
    full count.

THE FORMATTER
    CLANG_FORMAT wins when it is set.  Else clang-format when its major
    version is 22 or 23, then clang-format-23, then clang-format.  Version
    22.1.8 and 23.1.0 give the same bytes for this tree with this
    configuration, so CI can use either.  Another major version gives a note,
    because it can report a drift that is not there.

A FAILED BATCH
    `--dry-run -Werror` exits 1 on a drift.  A batch that exits 1 with no
    violation line did not check its files: a configuration that the
    formatter cannot read, or a crash.  A batch that exits 0 with a violation
    line, or with another status, is also a failure.  So a formatter that
    checked nothing never reads as a clean tree.  With --fix, a batch whose
    `-i` run exits with a status that is not 0 is a failure too, and the
    report gives the output of the formatter.

Usage
    guard check --rule clang-format            check, and exit 1 on a drift
    guard check --rule clang-format --fix      format each file that drifts, in place
    guard check --rule clang-format --self-test

Exit 0 when each file agrees with the configuration, 1 on a drift, 2 on a usage error, with no formatter or fd, or
when the formatter failed on a batch, so that a file was not checked or, with --fix, not reformatted.
"""

from __future__ import annotations

import concurrent.futures
import hashlib
import os
import shlex
import shutil
import stat
import sys
import tempfile
import time
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import proc
from assay.core.paths import REPO_ROOT

RULE = "clang-format"
VERIFIED_MAJORS = (" 22.", " 23.")
SUFFIXES = ("h", "hpp", "cpp", "cc", "cxx")
EXCLUDED = ("build*", "papers", "vmlinux.h")
DEFAULT_BATCH = 250
MAX_DEFAULT_JOBS = 32
CACHE_FORMAT = "check-clang-format cache 1"
CACHE_MAX_AGE_S = 30 * 86400
VIOLATION = ": error: code should be clang-formatted"
ELF_MAGIC = b"\x7fELF"
# The lines of the output of a failed batch that the report shows.
FAILED_OUTPUT_LINES = 20
FIX_COMMAND = "python3 utils/bin/guard check --rule clang-format --fix"


class SetupError(Exception):
    """The check cannot run: no formatter, no fd, or a bad number of jobs."""


@dataclass(frozen=True, slots=True)
class Settings:
    """The formatter, its path and its version, fd, the parallelism, the size of a batch and the cache directory."""

    formatter: str
    formatter_path: str | None
    version: str
    finder: str
    jobs: int
    batch: int
    cache_dir: Path | None
    is_parallel: bool


@dataclass(frozen=True, slots=True)
class Verdict:
    """The status of a run, and the lines that it wrote to standard error."""

    status: int
    report: tuple[str, ...]


# ── The settings ───────────────────────────────────────────────────────────


def version_of(formatter: str) -> str:
    """Return the version line of a formatter, as `FORMATTER --version || echo unknown` gives it, with no newline."""
    try:
        done = proc.run(proc.Command([formatter, "--version"], timeout_s=proc.QUICK_S))
    except proc.CannotStart:
        return "unknown"
    return (done.out + ("unknown\n" if done.exit != 0 else "")).rstrip("\n")


def is_verified(version: str) -> bool:
    """Whether a version line names a major version whose output was verified against this tree."""
    return any(major in version for major in VERIFIED_MAJORS)


def choose_formatter(environment: dict[str, str]) -> str:
    """Return the formatter of THE FORMATTER.

    Raises:
        SetupError: If no formatter is found
    """
    chosen = environment.get("CLANG_FORMAT", "")
    if chosen:
        return chosen
    if shutil.which("clang-format") is not None and is_verified(version_of("clang-format")):
        return "clang-format"
    for name in ("clang-format-23", "clang-format"):
        if shutil.which(name) is not None:
            return name
    raise SetupError("no clang-format found.  Set $CLANG_FORMAT.")


def choose_finder(environment: dict[str, str]) -> str:
    """Return fd, or the fdfind of Debian and Ubuntu, or the fd that FD names.

    Raises:
        SetupError: If no fd is found
    """
    chosen = environment.get("FD", "")
    if chosen:
        return chosen
    for name in ("fd", "fdfind"):
        if shutil.which(name) is not None:
            return name
    raise SetupError("fd (or fdfind) is required.  Set $FD.")


def is_elf(path: str | None) -> bool:
    """Whether a file is an executable of ELF, which can run in parallel, and not a script."""
    if path is None:
        return False
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == ELF_MAGIC
    except OSError:
        return False


def default_cache_dir(environment: dict[str, str]) -> Path | None:
    """Return the cache directory of THE SPEED: CRUCIBLE_CLANG_FORMAT_CACHE, or None when it is empty, or the default."""
    if "CRUCIBLE_CLANG_FORMAT_CACHE" in environment:
        chosen = environment["CRUCIBLE_CLANG_FORMAT_CACHE"]
        return Path(chosen) if chosen else None
    home = environment.get("XDG_CACHE_HOME", "")
    return (Path(home) if home else Path.home() / ".cache") / "crucible" / "clang-format"


def settings_of(environment: dict[str, str]) -> Settings:
    """Read the settings from the environment.

    Raises:
        SetupError: If no formatter or fd is found, or a number is not a positive whole number
    """
    formatter = choose_formatter(environment)
    finder = choose_finder(environment)
    formatter_path = shutil.which(formatter)
    jobs_text = environment.get("CRUCIBLE_CLANG_FORMAT_JOBS", str(min(os.cpu_count() or 1, MAX_DEFAULT_JOBS)))
    batch_text = environment.get("CRUCIBLE_CLANG_FORMAT_BATCH", str(DEFAULT_BATCH))
    for name, text in (("CRUCIBLE_CLANG_FORMAT_JOBS", jobs_text), ("CRUCIBLE_CLANG_FORMAT_BATCH", batch_text)):
        if not text.isdigit() or int(text) == 0:
            raise SetupError(f"{name} must be a positive integer, not \"{text}\".")
    return Settings(formatter, formatter_path, version_of(formatter), finder, int(jobs_text), int(batch_text),
                    default_cache_dir(environment), is_elf(formatter_path))


# ── The files and the cache ────────────────────────────────────────────────


def list_files(root: Path, settings: Settings) -> list[str]:
    """Return each file of THE FILES, relative to the root, as fd prints it."""
    command = [settings.finder]
    for suffix in SUFFIXES:
        command += ["-e", suffix]
    for pattern in EXCLUDED:
        command += ["-E", pattern]
    done = proc.run(proc.Command([*command, "."], timeout_s=proc.QUICK_S, cwd=root))
    return [line for line in done.out.splitlines() if line]


def hash_line(root: Path, path: str) -> str:
    """Return the line of one file as sha256sum gives it: the SHA-256 of its content, two spaces and its path."""
    return f"{hashlib.sha256((root / path).read_bytes()).hexdigest()}  {path}"


def cache_identity(root: Path, settings: Settings) -> str:
    """Return the SHA-256 of each input that changes the verdict on a file that does not change.

    The inputs are the formatter, its version and the two configuration files
    of the scan root, in the bytes that the shell check of earlier versions
    hashed, so a cache of that check stays valid.
    """
    text = f"{CACHE_FORMAT}\n{settings.version}\n".encode()
    if settings.formatter_path is not None:
        text += f"{hashlib.sha256(Path(settings.formatter_path).read_bytes()).hexdigest()}  -\n".encode()
    for name in (".clang-format", ".clang-format-ignore"):
        text += f"-- {name}\n".encode()
        if (root / name).is_file():
            text += (root / name).read_bytes()
    return hashlib.sha256(text).hexdigest()


def write_cache(cache_file: Path, hashes: list[str], drifting: set[str]) -> None:
    """Write the cache: each line of this run whose file does not drift, through a temporary file and a rename.

    Then remove each cache file of the directory that no run used for CACHE_MAX_AGE_S.
    """
    lines = [line for line in hashes if line[66:] not in drifting]
    handle, temporary = tempfile.mkstemp(prefix=f"{cache_file.name}.", dir=cache_file.parent)
    with os.fdopen(handle, "w", encoding="utf-8") as output:
        output.write("".join(f"{line}\n" for line in lines))
    os.replace(temporary, cache_file)
    limit = time.time() - CACHE_MAX_AGE_S
    for entry in cache_file.parent.iterdir():
        try:
            info = entry.lstat()
            if stat.S_ISREG(info.st_mode) and info.st_mtime < limit:
                entry.unlink()
        except OSError:
            continue


# ── The formatter runs ─────────────────────────────────────────────────────


def violation_path(line: str) -> str | None:
    """Return the file of one violation line of the formatter, PATH:LINE:COLUMN: error: code should be ..., or None.

    The path does not start with a space, holds no ':' and has a suffix of SUFFIXES.
    """
    head, separator, _ = line.partition(VIOLATION)
    if not separator or not head or head[0] == " ":
        return None
    parts = head.split(":")
    if len(parts) != 3 or not parts[1].isdigit() or not parts[2].isdigit():
        return None
    path = parts[0]
    return path if path.rpartition(".")[2] in SUFFIXES and "." in path else None


def run_batch(root: Path, formatter: str, files: list[str]) -> tuple[set[str], str | None]:
    """Run the formatter on one batch with stdin from /dev/null, and read its drift (A FAILED BATCH).

    Returns:
        The files that drift, and None; or an empty set and the report of a failed batch
    """
    done = proc.run(proc.Command([formatter, "--dry-run", "-Werror", *files], timeout_s=proc.TOOL_S, cwd=root,
                                 stderr=proc.Output.MERGE))
    output = done.out
    drift = {path for line in output.splitlines() if (path := violation_path(line)) is not None}
    if (done.exit == 0 and not drift) or (done.exit == 1 and drift):
        return drift, None
    shown = "\n".join(output.splitlines()[:FAILED_OUTPUT_LINES])
    return set(), (f"check-clang-format: the formatter failed on a batch of {len(files)} files (exit "
                   f"{done.exit}, {len(drift)} violation lines):\n{shown}")


def batches(root: Path, files: list[str], settings: Settings) -> list[list[str]]:
    """Put the files into batches: for a parallel formatter, min(jobs, files) batches by size in turn, else in order.

    Complexity: O(n log n) in the file count, for the sort by size.
    """
    if not files:
        return []
    if settings.is_parallel:
        count = min(settings.jobs, len(files))
        lists: list[list[str]] = [[] for _ in range(count)]
        by_size = sorted(files, key=lambda path: (-(root / path).stat().st_size, path))
        for index, path in enumerate(by_size):
            lists[index % count].append(path)
        return lists
    return [files[start:start + settings.batch] for start in range(0, len(files), settings.batch)]


def drifting_files(root: Path, files: list[str], settings: Settings) -> tuple[set[str], list[str]]:
    """Run the formatter on each file, and return the files that drift and the report of each failed batch."""
    groups = batches(root, files, settings)
    if settings.is_parallel and len(groups) > 1:
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(groups)) as pool:
            results = list(pool.map(lambda group: run_batch(root, settings.formatter, group), groups))
    else:
        results = [run_batch(root, settings.formatter, group) for group in groups]
    drift: set[str] = set()
    failures: list[str] = []
    for found, failure in results:
        drift |= found
        if failure is not None:
            failures.append(failure)
    return drift, failures


# ── The check ──────────────────────────────────────────────────────────────


def check_tree(root: Path, settings: Settings, should_fix: bool = False) -> Verdict:
    """Check or fix each file of the scan root, as the sections of this module tell.

    Args:
        root: The scan root
        settings: The settings
        should_fix: Whether to format each file that drifts in place

    Returns:
        The status and the lines of standard error
    """
    report: list[str] = []
    if not is_verified(settings.version):
        report.append(f"check-clang-format: NOTE — {settings.version}.  The repo .clang-format was verified against "
                      f"22.1.8 and 23.1.0; another major may report drift that is not real.")
    files = list_files(root, settings)
    if not files:
        return Verdict(2, (*report, f"check-clang-format: no source files found under {root}"))
    hashes = [hash_line(root, path) for path in files]
    cache_file = None
    known: set[str] = set()
    if settings.cache_dir is not None:
        settings.cache_dir.mkdir(parents=True, exist_ok=True)
        cache_file = settings.cache_dir / cache_identity(root, settings)
        if cache_file.is_file():
            known = set(cache_file.read_text(encoding="utf-8", errors="replace").splitlines())
    to_check = [line[66:] for line in hashes if line not in known]
    drift, failures = drifting_files(root, to_check, settings)
    if failures:
        report += failures
        report.append("check-clang-format: the formatter did not check every file, so the tree is not known to be "
                      "clean.")
        return Verdict(2, tuple(report))
    if cache_file is not None:
        write_cache(cache_file, hashes, drift)
    ordered = sorted(drift)
    if should_fix:
        if not ordered:
            return Verdict(0, (*report, "check-clang-format: already clean — nothing to reformat."))
        notes = len(report)
        for start in range(0, len(ordered), settings.batch):
            group = ordered[start:start + settings.batch]
            done = proc.run(proc.Command([settings.formatter, "-i", *group], timeout_s=proc.TOOL_S, cwd=root,
                                         stderr=proc.Output.MERGE))
            if done.exit != 0:
                shown = "\n".join(done.out.splitlines()[:FAILED_OUTPUT_LINES])
                report.append(f"check-clang-format: the formatter failed to reformat a batch of {len(group)} files "
                              f"(exit {done.exit}):\n{shown}")
        if len(report) > notes:
            report.append("check-clang-format: the fix did not reformat each file that drifts.")
            return Verdict(2, tuple(report))
        return Verdict(0, (*report, f"check-clang-format: reformatted {len(ordered)} file(s)."))
    if not ordered:
        return Verdict(0, (*report, f"check-clang-format: clean — all {len(files)} files match .clang-format "
                                    f"({settings.formatter}; {len(to_check)} checked, {len(files) - len(to_check)} "
                                    f"from the cache)."))
    report.append(f"check-clang-format: {len(ordered)} file(s) drift from .clang-format:")
    report += [f"  {path}" for path in ordered]
    report.append(f"\nRun {FIX_COMMAND} to reformat, then commit the\nresult on its own so the diff stays reviewable.")
    return Verdict(1, tuple(report))


# ── The self-test ──────────────────────────────────────────────────────────

CLEAN_HEADER = """#pragma once
namespace crucible::planted {
struct Clean {
    int value = 0;
};
}  // namespace crucible::planted
"""
DRIFT_HEADER = """#pragma once
namespace crucible::planted {
struct    Drift {
      int   value=0;
};
}
"""


def self_test(settings: Settings) -> int:
    """Plant a clean tree and drifts, and check each verdict, as the shell check of earlier versions did.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="check-clang-format-") as work:
        root = Path(work)
        planted = replace(settings, cache_dir=root / "cache")
        tree = root / "tree"
        (tree / "include" / "crucible").mkdir(parents=True)
        shutil.copyfile(REPO_ROOT / ".clang-format", tree / ".clang-format")
        clean = tree / "include" / "crucible" / "planted_clean.h"
        clean.write_text(CLEAN_HEADER, encoding="utf-8")
        # The clean file is what this configuration gives, and not what looks tidy to a person.
        proc.run(proc.Command([settings.formatter, "-i", "include/crucible/planted_clean.h"], timeout_s=proc.TOOL_S,
                              cwd=tree, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT))
        formatted = clean.read_bytes()
        expect("a formatted tree passes", check_tree(tree, planted).status == 0)
        second = check_tree(tree, planted)
        expect("a second clean run takes each file from the cache",
               second.status == 0 and "0 checked, 1 from the cache" in second.report[-1])
        drift = tree / "include" / "crucible" / "planted_drift.h"
        drift.write_text(DRIFT_HEADER, encoding="utf-8")
        expect("a planted drift exits 1", check_tree(tree, planted).status == 1)
        drift.unlink()
        clean.write_bytes(formatted + b"struct    Late {  int x=0; };\n")
        expect("a cached file that drifts is checked again and exits 1", check_tree(tree, planted).status == 1)
        clean.write_bytes(formatted)

        config_root = root / "config"
        (config_root / "include" / "crucible").mkdir(parents=True)
        (config_root / ".clang-format").write_text("---\nBasedOnStyle: LLVM\nIndentWidth: 4\n...\n", encoding="utf-8")
        (config_root / "include" / "crucible" / "planted_clean.h").write_bytes(formatted)
        proc.run(proc.Command([settings.formatter, "-i", "include/crucible/planted_clean.h"], timeout_s=proc.TOOL_S,
                              cwd=config_root, stdout=proc.Output.INHERIT, stderr=proc.Output.INHERIT))
        expect("a tree formatted with its own configuration passes", check_tree(config_root, planted).status == 0)
        (config_root / ".clang-format").write_text("---\nBasedOnStyle: LLVM\nIndentWidth: 2\n...\n", encoding="utf-8")
        expect("a cached file is checked again under a changed configuration",
               check_tree(config_root, planted).status == 1)

        failing = replace(planted, formatter="false", formatter_path=shutil.which("false"),
                          is_parallel=is_elf(shutil.which("false")))
        expect("a formatter that fails with no violation line exits 2", check_tree(tree, failing).status == 2)
        broken_root = root / "broken"
        (broken_root / "include" / "crucible").mkdir(parents=True)
        (broken_root / ".clang-format").write_text("NotARealClangFormatKey: 7\n", encoding="utf-8")
        (broken_root / "include" / "crucible" / "planted_clean.h").write_bytes(formatted)
        expect("a configuration that the formatter cannot read exits 2", check_tree(broken_root, planted).status == 2)

        many_root = root / "many"
        (many_root / "include" / "crucible").mkdir(parents=True)
        shutil.copyfile(REPO_ROOT / ".clang-format", many_root / ".clang-format")
        for index in range(40):
            (many_root / "include" / "crucible" / f"clean_{index}.h").write_bytes(formatted)
            (many_root / "include" / "crucible" / f"drift_{index}.h").write_text(
                f"struct    Drift{index} {{  int x=0; }};\n", encoding="utf-8")
        many = check_tree(many_root, replace(planted, jobs=8))
        expect("40 drifting files in parallel batches are each reported",
               many.status == 1 and "40 file(s) drift" in "\n".join(many.report))

        stdin_root = root / "stdin"
        (stdin_root / "include" / "crucible").mkdir(parents=True)
        shutil.copyfile(REPO_ROOT / ".clang-format", stdin_root / ".clang-format")
        for index in range(settings.batch + 1):
            (stdin_root / "include" / "crucible" / f"drift_{index}.h").write_text(
                f"struct    Drift{index} {{  int x=0; }};\n", encoding="utf-8")
        stub = root / "reads_stdin_formatter"
        real = settings.formatter_path or settings.formatter
        stub.write_text(f"#!/bin/sh\ncat >/dev/null\nexec {shlex.quote(real)} \"$@\"\n", encoding="utf-8")
        stub.chmod(0o755)
        reading = check_tree(stdin_root, replace(planted, formatter=str(stub), formatter_path=str(stub),
                                                 is_parallel=False))
        expect(f"a formatter that reads its stdin sees all {settings.batch + 1} drifting files",
               reading.status == 1 and f"{settings.batch + 1} file(s) drift" in "\n".join(reading.report))
        expect("a violation line gives its file, and another line gives none",
               violation_path("include/a.h:3:7: error: code should be clang-formatted [-Wclang-format-violations]")
               == "include/a.h" and violation_path("  include/a.h:3:7: error: code should be clang-formatted") is None
               and violation_path("include/a.txt:3:7: error: code should be clang-formatted") is None)

        fix_root = root / "fix"
        (fix_root / "include" / "crucible").mkdir(parents=True)
        shutil.copyfile(REPO_ROOT / ".clang-format", fix_root / ".clang-format")
        (fix_root / "include" / "crucible" / "drift.h").write_text("struct    Drift {  int x=0; };\n", encoding="utf-8")
        refusing = root / "refusing_formatter"
        refusing.write_text("#!/bin/sh\nif [ \"$1\" = -i ]; then echo 'planted formatter: no write' >&2; exit 3; fi\n"
                            "shift 2\nfor file in \"$@\"; do echo \"$file:1:1: error: code should be clang-formatted\"; "
                            "done\nexit 1\n", encoding="utf-8")
        refusing.chmod(0o755)
        fixed = check_tree(fix_root, replace(planted, formatter=str(refusing), formatter_path=str(refusing),
                                             is_parallel=False), should_fix=True)
        expect("a fix whose formatter fails on a file exits 2 and gives the error of the formatter",
               fixed.status == 2 and "planted formatter: no write" in "\n".join(fixed.report))

    if failures:
        print(f"check-clang-format: SELF-TEST FAILED — {len(failures)} case(s) did not hold", file=sys.stderr)
        return 2
    print("check-clang-format: self-test passed — a formatted tree passes and fills the cache, planted drift exits 1, "
          "a cached file that drifts or a configuration change is checked again, parallel batches report each file, "
          "a failing formatter, a failed fix or an unreadable configuration exits 2, and a formatter that reads stdin "
          "sees every file.", file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, fix it, or run the self-test.

    Args:
        argv: The words after `guard check --rule clang-format`

    Returns:
        The exit status
    """
    if argv in (["-h"], ["--help"]):
        print(__doc__)
        return 0
    if argv not in ([], ["--fix"], ["--self-test"]):
        print(f"check-clang-format: unknown option: {' '.join(argv)}\nusage: guard check --rule clang-format "
              f"[--fix | --self-test]", file=sys.stderr)
        return 2
    try:
        settings = settings_of(dict(os.environ))
    except SetupError as problem:
        print(f"check-clang-format: {problem}", file=sys.stderr)
        return 2
    if argv == ["--self-test"]:
        return self_test(settings)
    verdict = check_tree(REPO_ROOT, settings, should_fix=argv == ["--fix"])
    for line in verdict.report:
        print(line, file=sys.stderr)
    return verdict.status
