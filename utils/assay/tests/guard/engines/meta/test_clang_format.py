"""The test of the rule clang-format (assay.guard.engines.meta.clang_format).

`guard selftest tests.guard.engines.meta.test_clang_format` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.  The
witness of the rule holds the verdict on each plant.  This test holds the
parts that a plant does not show.  These are the batches, the second run of
a batch with an error, the cache and a formatter that is a script.  The
cases that run the formatter use clang-format 22 or 23 of the host.
"""

import os
from pathlib import Path

from assay.core import cache, git, testing
from assay.guard.engines.meta import clang_format
from assay.guard.engines.meta.runner import MetaContext, NotApplicable
from assay.readers import tool_reports

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
        "major_of() reads the word after the word version",
        clang_format.major_of("clang-format version 22.1.8 (Fedora 22.1.8-4.fc44)") == 22
        and clang_format.major_of("Ubuntu clang-format version 18.1.3 (1ubuntu1)") == 18
        and clang_format.major_of("no version here") is None,
    )
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


def formatter_cases(expect: testing.Expect) -> None:
    """Check the batches, the second run of a batch with an error, the cache and a script formatter."""
    try:
        clang_format.applies()
    except NotApplicable as reason:
        expect(f"a verified clang-format is installed: {reason}", False)
        return
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
    script_files = {".clang-format": CONFIGURATION, "a/Drift.h": drift(0), "a/Clean.h": CLEAN}
    with cache.scratch_root(), git.scratch_repository(script_files) as root:
        script = root / "formatter.sh"
        script.write_text(f'#!/bin/sh\nexec {clang_format.chosen_formatter().path} "$@"\n', encoding="utf-8")
        script.chmod(0o755)
        previous = os.environ.get(clang_format.FORMATTER_VARIABLE)
        os.environ[clang_format.FORMATTER_VARIABLE] = str(script)
        try:
            chosen = clang_format.chosen_formatter()
            expect(
                "a script formatter runs its batches one at a time, and it finds the drift",
                not chosen.is_executable and verdicts(root) == [("a/Drift.h", "drift")],
            )
        finally:
            if previous is None:
                del os.environ[clang_format.FORMATTER_VARIABLE]
            else:
                os.environ[clang_format.FORMATTER_VARIABLE] = previous


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (unit_cases, formatter_cases))
