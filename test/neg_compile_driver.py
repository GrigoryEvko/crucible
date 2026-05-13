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
    if len(sys.argv) != 5:
        print(
            "usage: neg_compile_driver.py <build-dir> <source> "
            "<fixture-name> <expected-regex>",
            file=sys.stderr,
        )
        return 2

    build_dir = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    fixture_name = sys.argv[3]
    expected_regex = sys.argv[4]
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
    if not re.search(expected_regex, combined, flags=re.MULTILINE):
        print(
            f"expected diagnostic not found for {fixture_name}: "
            f"{expected_regex}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
