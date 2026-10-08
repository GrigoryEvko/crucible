"""The test of the rule clang-format (assay.guard.engines.meta.clang_format).

`guard selftest tests.guard.engines.meta.test_clang_format` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.  The
witness of the rule holds the verdict on each plant.  This test holds the
parts that a plant does not show: the batches, the second run of a batch
with an error, and the cache.  The cases that run the formatter use the
pinned clang-format, which `python3 utils/bin/setup install clang-format`
installs.  With no install of the pin, the test runs only the cases that
run no formatter, and then it exits with 77.
"""

from pathlib import Path

from assay.core import cache, git, testing
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.meta import clang_format
from assay.guard.engines.meta.runner import MetaContext, NotApplicable
from assay.readers import tool_reports

MODULE = __name__.removeprefix("assay.")
CONFIGURATION = b"---\nBasedOnStyle: LLVM\nIndentWidth: 4\n...\n"
CLEAN = b"#pragma once\nnamespace planted {\nstruct Clean {\n    int value = 0;\n};\n} // namespace planted\n"
DRIFT_COUNT = 200


def drift(index: int) -> bytes:
    """Return a header that the formatter changes on its first line."""
    return f"struct    Drift{index} {{  int value=0; }};\n".encode()


def verdicts(root: Path) -> list[tuple[str, str]]:
    """Return the path and the kind of each finding of a tree, sorted."""
    return sorted((item.path, item.kind) for item in clang_format.findings(MetaContext(root)))


def unit_cases(expect: testing.Expect) -> None:
    """Check the parts that run no formatter."""
    expect(
        "the scope holds the C++ files, and no file vmlinux.h or file in a directory build* or papers",
        clang_format.is_in_scope("include/a.h")
        and clang_format.is_in_scope("src/b.cc")
        and not clang_format.is_in_scope("include/crucible/perf/bpf/vmlinux.h")
        and not clang_format.is_in_scope("build-release/c.cpp")
        and not clang_format.is_in_scope("papers/d.cpp")
        and not clang_format.is_in_scope("notes/e.txt"),
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
            f"the cache keeps one entry for each of the 2 clean files, and it holds {len(entries)}", len(entries) == 2
        )
        (root / "clean" / "Clean.h").write_bytes(CLEAN + b"struct    Late {  int value=0; };\n")
        expect(
            "a cached file that changes runs the formatter again",
            ("clean/Clean.h", "drift") in verdicts(root),
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
        status = testing.run(MODULE, argv, (unit_cases,))
        if status != ExitCode.OK:
            return status
        check_report.write_line(f"{MODULE}: {reason}.  The cases of the formatter do not run")
        return ExitCode.SKIPPED
    return testing.run(MODULE, argv, (unit_cases, formatter_cases))
