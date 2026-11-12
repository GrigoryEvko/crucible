"""The test of the rule clang-format (assay.guard.engines.meta.clang_format).

`guard selftest tests.guard.engines.meta.test_clang_format` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.  The
witness of the rule holds the verdict on each plant.  This test holds the
parts that a plant does not show: the batches, the second run of a batch
with an error, the cache, the parts of a run (THE PARTS), and the reader of
the ignore file (assay.readers.format_ignore).  One case holds that the
reader ignores a file exactly when the pinned formatter ignores it.  The cases
that run the formatter use the pinned clang-format, which `python3
utils/bin/setup install clang-format` installs.  With no install of the pin,
the test runs only the cases that run no formatter, and then it exits with
77.
"""

import contextlib
import io
from pathlib import Path

from assay.core import cache, git, testing
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard import check
from assay.guard.engines.meta import clang_format, runner
from assay.guard.engines.meta.runner import MetaContext, NotApplicable
from assay.readers import format_ignore, tool_reports

MODULE = __name__.removeprefix("assay.")
CONFIGURATION = b"---\nBasedOnStyle: LLVM\nIndentWidth: 4\n...\n"
CLEAN = b"#pragma once\nnamespace planted {\nstruct Clean {\n    int value = 0;\n};\n} // namespace planted\n"
DRIFT_COUNT = 200
# The part counts of the cases of the parts, and the files of the case of the part function.
PART_COUNTS = (1, 2, 3, 5, 8)
PART_FILE_COUNT = 17
# The drift that the planted ledger admits, and the ledger of the rule in a planted tree.
ADMITTED = "drift/Drift0.h"
LEDGER = (
    f"format 1\nrule clang-format\n{ADMITTED} | drift | admit | - | The planted ledger admits the drift of this file\n"
).encode()
USAGE_ERROR = 2
# A planted ignore file: a comment, a blank line, a pattern, a negated pattern, a lone `!` and a question mark.
IGNORE_TEXT = "# planted patterns\n\ngen/generated_*.cpp\n!keep/*.h\n!\none/?.h\n"
# The shapes of a pattern that the reader refuses (THE SHAPES THAT THE READER REFUSES of assay.readers.format_ignore).
REFUSED_PATTERNS = ("gen/**/a.cpp", "gen/[ab].h", "gen\\a.h", "/abs/a.h", "gen/", "gen//a.h", "./a.h", "gen/../a.h")


def drift(index: int) -> bytes:
    """Return a header that the formatter changes on its first line."""
    return f"struct    Drift{index} {{  int value=0; }};\n".encode()


def verdicts(root: Path, part: tuple[int, int] | None = None) -> list[tuple[str, str]]:
    """Return the path and the kind of each finding of a tree, or of one part of it, sorted."""
    return sorted((item.path, item.kind) for item in clang_format.findings(MetaContext(root, part=part)))


def judged_errors(rule: rule_registry.Rule, root: Path, part: tuple[int, int] | None) -> set[tuple[str, str]]:
    """Return the path and the kind of each error of a tree, or of one part, after the ledger of the tree judges it."""
    context = MetaContext(root, part=part)
    found = clang_format.findings(context)
    kept = context.is_covered if part is not None else None
    return {(item.path, item.kind) for item in runner.judged(rule, root, found, kept) if item.level == "error"}


def check_status(words: list[str]) -> int:
    """Return the status of `guard check` with the words, with a scratch root of the caches and no output."""
    with cache.scratch_root(), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return check.main(words)


def refused(words: list[str]) -> bool:
    """Return True when `guard check` refuses the words with the status of a usage error."""
    return check_status(words) == USAGE_ERROR


def part_cases(expect: testing.Expect) -> None:
    """Check the parts of a run that run no formatter: the part function, the context and the gate of guard check."""
    files = {f"src/F{index}.h": b"x" * (index * 7 % 23 + 1) for index in range(PART_FILE_COUNT)}
    with git.scratch_repository(files) as root:
        paths = sorted(files)
        holds = True
        for count in PART_COUNTS:
            parts = [runner.part_files(root, paths, (index, count)) for index in range(1, count + 1)]
            flat = [path for part in parts for path in part]
            holds = holds and sorted(flat) == paths and len(set(flat)) == len(flat)
            holds = holds and all(part == sorted(part) for part in parts)
        expect(f"the parts of each count of {PART_COUNTS} are disjoint, and together they hold each file", holds)
        context = MetaContext(root, part=(2, 3))
        chosen = context.part_of(paths)
        other = next(path for path in paths if path not in chosen)
        expect(
            "part_of() gives the files of the part, and the run judges only the ledger rows of those files",
            chosen == runner.part_files(root, paths, (2, 3))
            and context.is_covered(rule_ledger.Key(chosen[0], "drift", ""))
            and not context.is_covered(rule_ledger.Key(other, "drift", "")),
        )
        whole = MetaContext(root)
        expect(
            "a context with no part gives each file and judges each ledger row",
            whole.part_of(paths) == paths and whole.is_covered(rule_ledger.Key(other, "drift", "")),
        )
    expect(
        "guard check refuses --part for a meta rule, a tsast rule and a plugin rule that read no part, and a bad part",
        refused(["--rule", "prose-bans", "--part", "1/2"])
        and refused(["--rule", "layer-residual", "--part", "1/2"])
        and refused(["--rule", "quarantine", "--part", "1/2", "--build-dir", "build"])
        and refused(["--rule", "clang-format", "--part", "3/2"]),
    )
    expect(
        "the rules clang-format and parse-clean read a part, and the rule prose-bans does not",
        all(runner.reads_parts(rule_registry.load().rules[name]) for name in ("clang-format", "parse-clean"))
        and not runner.reads_parts(rule_registry.load().rules["prose-bans"]),
    )


def unit_cases(expect: testing.Expect) -> None:
    """Check the parts that run no formatter."""
    expect(
        "the scope holds the C++ files, and no file in a directory build* or papers",
        clang_format.is_in_scope("include/a.h")
        and clang_format.is_in_scope("src/b.cc")
        and not clang_format.is_in_scope("build-release/c.cpp")
        and not clang_format.is_in_scope("papers/d.cpp")
        and not clang_format.is_in_scope("notes/e.txt"),
    )
    ignored = format_ignore.read(REPO_ROOT / clang_format.IGNORE_FILE, clang_format.IGNORE_FILE)
    expect(
        "the ignore file of this tree ignores the BTF dump and the generated oracle tests, and not the driver of the oracle",
        ignored.is_ignored("include/crucible/perf/bpf/vmlinux.h")
        and ignored.is_ignored("test/session_oracle/generated_fixy_wire_00.cpp")
        and not ignored.is_ignored("test/session_oracle/wire_driver.h")
        and not ignored.is_ignored("test/session_oracle/sub/generated_fixy_wire_00.cpp"),
    )
    with git.scratch_repository({"a.h": b"one\ntwo\nthree\n"}) as root:
        incomplete = tool_reports.FormatDocument(True, ())
        changed = tool_reports.FormatDocument(False, (tool_reports.Replacement(9, 1, " "),))
        expect(
            "an incomplete document is the kind incomplete, and a change gives the line of its first byte",
            clang_format.document_verdict(root, "a.h", incomplete).kind == "incomplete"
            and clang_format.document_verdict(root, "a.h", changed) == clang_format.Verdict("drift", 3, "1"),
        )
        (root / "b.h").write_bytes(b"two\n")
        expect(
            "the batches hold each file one time, the largest file first",
            clang_format.batches_of(root, ["a.h", "b.h"]) == [["a.h"], ["b.h"]],
        )


def ignore_cases(expect: testing.Expect) -> None:
    """Check the reader of the ignore file, and the scope of the rule with an ignore file, with no formatter."""
    parsed = format_ignore.parse(IGNORE_TEXT, "planted")
    expect(
        "the reader skips a comment, a blank line and a lone !, and keeps each pattern in its sequence",
        [(item.text, item.is_negated, item.line) for item in parsed.patterns]
        == [("gen/generated_*.cpp", False, 3), ("keep/*.h", True, 4), ("one/?.h", False, 6)],
    )
    expect(
        "a star and a question mark match no /, and a pattern matches the whole path",
        format_ignore.matches("gen/generated_*.cpp", "gen/generated_00.cpp")
        and not format_ignore.matches("gen/generated_*.cpp", "gen/sub/generated_00.cpp")
        and not format_ignore.matches("gen/generated_*.cpp", "other/gen/generated_00.cpp")
        and not format_ignore.matches("gen/*", "gen/a/b.h")
        and format_ignore.matches("one/?.h", "one/a.h")
        and not format_ignore.matches("one/?.h", "one/ab.h"),
    )
    expect(
        "the first pattern that decides a file decides it, and a negated pattern ignores each file that it misses",
        format_ignore.parse("!keep/*.h\n", "planted").is_ignored("src/a.h")
        and not format_ignore.parse("!keep/*.h\n", "planted").is_ignored("keep/a.h")
        and format_ignore.parse("!keep/*.h\nkeep/a.h\n", "planted").is_ignored("keep/a.h")
        and not format_ignore.IgnoreFile().is_ignored("a.h"),
    )
    refused = 0
    for pattern in REFUSED_PATTERNS:
        try:
            format_ignore.parse(f"# planted\n{pattern}\n", "planted")
        except format_ignore.IgnoreError as problem:
            refused += problem.line == 2
    expect(
        f"the reader refuses each of the {len(REFUSED_PATTERNS)} shapes that it does not read",
        refused == len(REFUSED_PATTERNS),
    )
    with git.scratch_repository({"absent.txt": b"x\n"}) as root:
        expect(
            "a tree with no ignore file ignores nothing",
            format_ignore.read(root / clang_format.IGNORE_FILE, clang_format.IGNORE_FILE).patterns == (),
        )
    files = {
        clang_format.IGNORE_FILE: b"gen/generated_*.cpp\n",
        "gen/generated_00.cpp": b"x\n",
        "gen/driver.h": b"x\n",
        "src/a.h": b"x\n",
    }
    with git.scratch_repository(files) as root:
        scope, problems = clang_format.scope_of(MetaContext(root))
        expect(
            "the scope leaves out each file that the ignore file names, and keeps the other files of its directory",
            scope == ["gen/driver.h", "src/a.h"] and problems == [],
        )
    files[f"src/{clang_format.IGNORE_FILE}"] = b"a.h\n"
    with git.scratch_repository(files) as root:
        scope, problems = clang_format.scope_of(MetaContext(root))
        expect(
            "an ignore file below the root is the error failed, and the scope is empty",
            scope == []
            and [(item.path, item.kind) for item in problems] == [(f"src/{clang_format.IGNORE_FILE}", "failed")],
        )
    files.pop(f"src/{clang_format.IGNORE_FILE}")
    files[clang_format.IGNORE_FILE] = b"gen/**/generated_*.cpp\n"
    with git.scratch_repository(files) as root:
        scope, problems = clang_format.scope_of(MetaContext(root))
        expect(
            "a pattern that the reader refuses is the error failed at its line, and the scope is empty",
            scope == []
            and [(item.path, item.line, item.kind) for item in problems] == [(clang_format.IGNORE_FILE, 1, "failed")],
        )


def formatter_ignore_cases(expect: testing.Expect) -> None:
    """Check that the reader ignores a file exactly when the pinned formatter gives no document for it."""
    paths = (
        "gen/generated_00.cpp",
        "gen/generated_.cpp",
        "gen/driver.h",
        "gen/sub/generated_01.cpp",
        "keep/a.h",
        "keep/sub/b.h",
        "one/x.h",
        "one/xy.h",
        "src/c.cpp",
    )
    files = {".clang-format": CONFIGURATION, clang_format.IGNORE_FILE: IGNORE_TEXT.encode()}
    files.update({path: CLEAN for path in paths})
    ignored = format_ignore.parse(IGNORE_TEXT, "planted")
    program = clang_format.formatter()
    with git.scratch_repository(files) as root:
        disagree = []
        for path in paths:
            done = clang_format.run_formatter(root, program, [path])
            documents = tool_reports.clang_format_documents(done.out) if done is not None and done.exit == 0 else None
            if documents is None or (len(documents) == 0) != ignored.is_ignored(path):
                disagree.append(path)
        expect(f"the reader and the pinned formatter agree on each of {len(paths)} files", disagree == [])


def formatter_cases(expect: testing.Expect) -> None:
    """Check the batches, the second run of a batch with an error, and the cache, with the pinned formatter."""
    files = {".clang-format": CONFIGURATION, ".clang-format-ignore": b"ignored/Generated.h\n", "clean/Clean.h": CLEAN}
    files.update({f"drift/Drift{index}.h": drift(index) for index in range(DRIFT_COUNT)})
    files["ignored/Generated.h"] = drift(DRIFT_COUNT)
    with cache.scratch_root(), git.scratch_repository(files) as root:
        found = verdicts(root)
        expect(
            f"each of {DRIFT_COUNT} files that drift in parallel batches is a drift, and an ignored file is clean",
            len(found) == DRIFT_COUNT
            and {kind for _path, kind in found} == {"drift"}
            and all(path != "ignored/Generated.h" for path, _kind in found),
        )
        directory = cache.cache_root(clang_format.CACHE_NAME)
        entries = [] if directory is None else cache.entries(directory)
        expect(
            f"the cache keeps one entry for the clean file and none for the ignored file, and it holds {len(entries)}",
            len(entries) == 1,
        )
        (root / "clean" / "Clean.h").write_bytes(CLEAN + b"struct    Late {  int value=0; };\n")
        expect(
            "a cached file that changes runs the formatter again",
            ("clean/Clean.h", "drift") in verdicts(root),
        )


def formatter_part_cases(expect: testing.Expect) -> None:
    """Check that the parts of a run give together the findings and the verdicts of one run on each file."""
    files = {".clang-format": CONFIGURATION, ".clang-format-ignore": b"ignored/Generated.h\n", "clean/Clean.h": CLEAN}
    files.update({f"drift/Drift{index}.h": drift(index) for index in range(DRIFT_COUNT // 10)})
    files["ignored/Generated.h"] = drift(DRIFT_COUNT)
    files["utils/ledgers/clang-format.txt"] = LEDGER
    rule = rule_registry.load().rules[clang_format.RULE]
    with cache.scratch_root(), git.scratch_repository(files) as root:
        whole = verdicts(root)
        holds = True
        for count in PART_COUNTS:
            joined = sorted(item for index in range(1, count + 1) for item in verdicts(root, (index, count)))
            holds = holds and joined == whole
        expect(f"the findings of the parts of each count of {PART_COUNTS} together are the findings of one run", holds)
        whole_errors = judged_errors(rule, root, None)
        part_errors = [judged_errors(rule, root, (index, 3)) for index in range(1, 4)]
        expect(
            "the ledger admits its file in the part that holds it, and the parts together give the errors of one run",
            (ADMITTED, "drift") not in whole_errors
            and set[tuple[str, str]]().union(*part_errors) == whole_errors
            and len(whole_errors) == DRIFT_COUNT // 10 - 1,
        )
        accepted = check_status(["--rule", clang_format.RULE, "--part", "1/2", "--root", str(root)])
        expect("guard check takes --part for the rule, and a part with a drift gives the status 1", accepted == 1)
    with cache.scratch_root(), git.scratch_repository({"notes/a.txt": b"text\n"}) as root:
        expect(
            "only part 1 gives the error of a tree with no C++ file",
            verdicts(root, (1, 3)) == [(".clang-format", "failed")]
            and verdicts(root, (2, 3)) == []
            and verdicts(root, (3, 3)) == []
            and verdicts(root) == [(".clang-format", "failed")],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, and 2 for a usage error.  With no install of the pinned
        formatter, 77 when each case that runs no formatter holds
    """
    try:
        clang_format.applies()
    except NotApplicable as reason:
        status = testing.run(MODULE, argv, (unit_cases, ignore_cases, part_cases))
        if status != ExitCode.OK:
            return status
        check_report.write_line(f"{MODULE}: {reason}.  The cases of the formatter do not run")
        return ExitCode.SKIPPED
    return testing.run(
        MODULE,
        argv,
        (unit_cases, ignore_cases, part_cases, formatter_ignore_cases, formatter_cases, formatter_part_cases),
    )
