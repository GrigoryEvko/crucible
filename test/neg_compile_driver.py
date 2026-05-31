#!/usr/bin/env python3
"""Run one negative-compile fixture without invoking Ninja.

CTest can launch many negative fixtures concurrently.  Calling
`cmake --build` from each fixture makes those CTest jobs nested writers
to the same Ninja log and generated-output graph.  This driver instead
replays the target's compile command from compile_commands.json and
redirects the object output into a per-fixture scratch directory.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
import sys
from pathlib import Path


def _replace_output(argv: list[str], output: Path) -> list[str]:
    result = list(argv)
    for index, arg in enumerate(result):
        if arg == "-o" and index + 1 < len(result):
            result[index + 1] = str(output)
            return result
        if arg.startswith("-o") and len(arg) > 2:
            result[index] = f"-o{output}"
            return result
    return result + ["-o", str(output)]


def main() -> int:
    # argv[4..] are one OR MORE required-diagnostic regexes.  The
    # single-regex form (exactly 5 argv) is the original one-file-one-
    # assertion contract.  Supplying additional regexes (argv >= 6) is
    # the multi-cell contract: the fixture is a single positive-syntax
    # TU containing N independent `static_assert`-failure cells, and
    # EVERY listed regex must appear in the compile output — proving
    # each cell failed independently for its own named reason.  AND
    # semantics are mandatory: a merged TU whose first cell's regex
    # matched but whose later cells silently stopped failing would
    # otherwise pass spuriously.  See test/CMakeLists.txt
    # `crucible_neg_compile_fixy_multi_test` and the binary-per-TU
    # soundness note above that function.
    if len(sys.argv) < 5:
        print(
            "usage: neg_compile_driver.py <build-dir> <source> "
            "<fixture-name> <expected-regex> [<expected-regex> ...]",
            file=sys.stderr,
        )
        return 2

    build_dir = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    fixture_name = sys.argv[3]
    expected_regexes = sys.argv[4:]
    compile_db = build_dir / "compile_commands.json"

    rows = json.loads(compile_db.read_text())
    command = None
    directory = build_dir
    for row in rows:
        if Path(row["file"]).resolve() == source:
            command = row.get("arguments") or shlex.split(row["command"])
            directory = Path(row.get("directory", build_dir))
            break

    if command is None:
        print(f"no compile command for {source}", file=sys.stderr)
        return 2

    scratch = build_dir / "neg-compile" / fixture_name
    scratch.mkdir(parents=True, exist_ok=True)
    output = scratch / f"{fixture_name}.o"
    argv = _replace_output(list(command), output)

    # A negative-compile fixture asserts a COMPILE-TIME rejection — a
    # property that only manifests under the `enforce` contract
    # evaluation semantic.  Presets that relax contracts (release =>
    # observe; the `ignore` hot-path TUs) would otherwise let the
    # fixture compile clean, inverting its WILL_FAIL / expected-
    # diagnostic gate.  GCC honors the LAST -fcontract-evaluation-
    # semantic flag, so append `enforce` to override whatever the
    # replayed preset command carried.  No-op under default/tsan
    # (already enforce); fixes the release preset.  Identical on stock
    # and patched GCC 16 — the consteval rejection here is NOT the
    # patched compiler's c++/124241 fix (that only affects the two
    # graded-regime fixtures CI excludes by name).  Only appended when
    # contracts are already enabled on the replayed command.
    if any(arg.startswith("-fcontract") for arg in argv):
        argv.append("-fcontract-evaluation-semantic=enforce")

    proc = subprocess.run(
        argv,
        cwd=directory,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    combined = proc.stdout + proc.stderr
    sys.stdout.write(combined)

    if proc.returncode == 0:
        print(
            f"negative fixture {fixture_name} compiled successfully",
            file=sys.stderr,
        )
        return 1
    # EVERY required regex must appear.  For a single-regex fixture this
    # is identical to the original `re.search` gate.  For a multi-cell
    # fixture, a missing regex means one cell stopped failing (discipline
    # slipped on that cell) OR its diagnostic text drifted — either way
    # the merged TU no longer proves what it claims, and we fail loudly
    # naming the specific cell that went silent.
    missing = [
        pattern
        for pattern in expected_regexes
        if not re.search(pattern, combined, flags=re.MULTILINE)
    ]
    if missing:
        for pattern in missing:
            print(
                f"expected diagnostic not found for {fixture_name}: "
                f"{pattern}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
