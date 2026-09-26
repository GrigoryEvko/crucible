#!/usr/bin/env python3
"""Refuse the retired FX axis numbering in the prose of the fixy headers.

WHY
    The fixy doc-blocks once named an axis by its historical FX number ("Dim 22
    Staleness", "(dim 22)").  Two FX axes were dropped, so the FX numbers no
    longer match the ordinals of the axis enum.  A doc-block names an axis by
    the enum spelling instead, for example "DimensionAxis::Staleness = 19".
    This guard refuses a regression to the FX spelling.

THE ENGINE
    The scan reads the parse tree of each C++ file under include/fixy and
    include/crucible/fixy, and applies the two patterns below to the text of the
    prose nodes only: comments, string literals and raw string literals.  A
    macro body or any other code is not prose, so a code token that happens to
    read "Dim 22" is not a finding.
        \\bDim [0-9]{1,2}\\b    the header form "Dim 1 Type"
        \\(dim [0-9]{1,2}[,)]   the parenthetical form "(dim 22)" or "(dim 22,"
    The enum spelling does not match, because "Dim" there follows "Dimension"
    with no word boundary.

EXIT STATUS
    0  no FX-ordinal prose
    1  at least one finding
    2  bad invocation, no scan directory, or a file that does not parse
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

SCAN_DIRS = ("include/fixy", "include/crucible/fixy")
FX_ORDINAL = re.compile(r"\bDim [0-9]{1,2}\b|\(dim [0-9]{1,2}[,)]")


def scan(root: Path) -> tuple[int, list[str]]:
    """Report each FX-ordinal spelling in the prose of the scanned headers.

    Complexity: O(total nodes + total prose bytes) over the scanned files.

    Args:
        root: The repository root to scan

    Returns:
        (exit status, report lines), with the status as the module docstring states
    """
    present = [root / d for d in SCAN_DIRS if (root / d).is_dir()]
    if not present:
        return 2, [f"check-fixy-dim-prose: none of {', '.join(SCAN_DIRS)} exists under {root}"]
    files = sorted(
        path for base in present for suffix in tsast.CPP_SUFFIXES for path in base.rglob(f"*{suffix}")
        if tsast.is_in_cpp_scope(path.relative_to(root))
    )
    report: list[str] = []
    try:
        for tree in tsast.parse(files):
            relative = Path(tree.path).relative_to(root).as_posix()
            for prose in tsast.prose_nodes(tree):
                for offset, text in enumerate(tsast.prose_text(prose).split("\n")):
                    if FX_ORDINAL.search(text):
                        report.append(f"{relative}:{prose.line + offset}: {text.strip()}")
    except tsast.ParseError as exc:
        return 2, [f"check-fixy-dim-prose: {exc}"]
    if report:
        report.append("Re-spell each one as 'DimensionAxis::<Name> = <ordinal>', the ordinal of the axis enum.")
        return 1, report
    return 0, report


def self_test() -> int:
    """Plant FX prose, the enum spelling and look-alike code, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, root: Path, want_status: int, named: list[str], unnamed: list[str]) -> None:
        """Run the scan and compare its status and the sites it names.

        Args:
            name: What the case proves
            root: The scratch repository root
            want_status: The exit status the scan must give
            named: Site strings the report must contain
            unnamed: Site strings the report must not contain
        """
        status, report = scan(root)
        text = "\n".join(report)
        held = status == want_status and all(n in text for n in named) and not any(n in text for n in unnamed)
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            print(f"       status {status}, want {want_status}\n       " + text.replace("\n", "\n       "))

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        expect("no scan directory is a refusal, not a pass", root, 2, [], [])

        old = root / "include" / "crucible" / "fixy"
        old.mkdir(parents=True)
        (old / "planted.h").write_text(
            "// Dim 22 Staleness is the retired header form.\n"
            "// Staleness (dim 22) is the retired parenthetical form.\n"
            "// DimensionAxis::Staleness = 19 is the enum spelling.\n"
            "#pragma once\n",
            encoding="utf-8",
        )
        expect("both FX forms are caught and the enum spelling is not", root, 1,
               ["planted.h:1", "planted.h:2"], ["planted.h:3"])
        (old / "planted.h").write_text("// DimensionAxis::Staleness = 19\n#pragma once\n", encoding="utf-8")
        expect("a header with the enum spelling alone passes", root, 0, [], [])

        # Positive controls: the grep of the old shell guard got each of these wrong.
        new = root / "include" / "fixy"
        new.mkdir(parents=True)
        (new / "Moved.h").write_text("#pragma once\n/* Staleness (dim 22, stale reads) */\n", encoding="utf-8")
        expect("a header under include/fixy is scanned", root, 1, ["include/fixy/Moved.h:2"], [])
        (new / "Moved.h").write_text(
            "#pragma once\n#define Dim 22\n"
            'inline constexpr char note[] = "Dim 7 in a string is prose";\n',
            encoding="utf-8",
        )
        expect("code that reads Dim 22 is not prose, a string that says it is", root, 1,
               ["include/fixy/Moved.h:3"], ["include/fixy/Moved.h:2"])

    if failures:
        print(f"check-fixy-dim-prose --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-fixy-dim-prose --self-test: every case holds")
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test or the scan, as the arguments ask.

    Args:
        argv: The command-line arguments after the program name

    Returns:
        The process exit status
    """
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv:
            print(__doc__, file=sys.stderr)
            return 2
        status, report = scan(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-fixy-dim-prose: {exc}", file=sys.stderr)
        return 3
    for line in report:
        print(line, file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
