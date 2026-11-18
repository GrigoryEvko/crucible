"""Compile each probe of a heavy compile-time gate, and fail when it costs more than its ceiling.

usage: check_ceilings.py BUILD_DIR SOURCE_ROOT [--measure]

THE PROBES
    Each file probe_*.cpp of this directory runs one heavy gate of the base on
    a fixed input, such as the checkpoint verdict of fixy/session/Checkpoint.h.
    Its line "// measured: N G instructions" gives the user instructions of
    the compiler for the probe when the probe was written.  The ceiling of the
    probe is CEILING_FACTOR times that count.  A gate that loses its cache, or
    a walk that grows with the paths of a protocol in place of its nodes, then
    costs many times its count, and the check fails.  The ceiling is in the
    probe, so the check has no ledger.

THE COMPILE
    The compile of a probe is the compile of REFERENCE_UNIT in the compile
    database of the build, with the probe as its source and -fsyntax-only.  The
    probe then gets the flags of a test of the session layer, with the
    quarantine plugin.  The compile writes no file: the check removes -c, -o
    and the words of a dependency file.  With -fsyntax-only and no report
    directory, the plugin writes its findings as notes and no file
    (utils/tools/quarantine/plugin.cpp).  The count then holds the gate, the
    headers and the census of the plugin, as the compile of a test does.

THE COUNT
    The count is the user instructions of the compiler and of each process
    that it starts, from a hardware counter (THE INSTRUCTION COUNT of
    utils/assay/core/meter.py).  The count changes by less than 0.1 % between
    two compiles at each load of the host, and the wall time does not stay
    so.  A host with no exact counter, and a build with no REFERENCE_UNIT,
    give the exit SKIPPED.

--measure prints the count of each probe and judges no ceiling.  Exit 0 when
each probe is at or below its ceiling, 1 when one is above it or does not
compile, 2 on a usage error, 77 for a skip.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

# The unit of the compile database whose flags each probe gets (THE COMPILE).
REFERENCE_UNIT = "test/fixy/test_session_checkpoint_work_bound.cpp"
# The ceiling of a probe as a multiple of its measured count (THE PROBES).
CEILING_FACTOR = 2.0
# The line of a probe that gives its measured count, in G (10^9) instructions.
MEASURED_LINE = re.compile(r"^// measured: ([0-9]+(?:\.[0-9]+)?) G instructions$", re.MULTILINE)
GIGA = 1e9
# The time limit of one compile, in seconds.  A walk of the paths of a protocol can run for hours.
COMPILE_S = 900
# The exits of the check.
OK, FAILED, USAGE, SKIPPED = 0, 1, 2, 77
# The words that the compile of a probe drops, and the words whose next word it drops too (THE COMPILE).
DROPPED_WORDS = frozenset({"-c", "-MD", "-MMD"})
DROPPED_PAIRS = frozenset({"-o", "-MF", "-MT", "-MQ"})


def reference_command(build: Path) -> tuple[list[str], str, str] | None:
    """Return the words, the working directory and the source of the compile of REFERENCE_UNIT, or None."""
    database = build / "compile_commands.json"
    if not database.is_file():
        return None
    for entry in json.loads(database.read_text(encoding="utf-8")):
        if entry["file"].endswith(REFERENCE_UNIT):
            words = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
            return list(words), entry["directory"], entry["file"]
    return None


def probe_command(words: list[str], source: str, probe: Path) -> list[str]:
    """Return the compile of a probe: the reference words with the probe as the source and -fsyntax-only."""
    kept: list[str] = []
    skip_next = False
    for word in words:
        if skip_next:
            skip_next = False
            continue
        if word in DROPPED_PAIRS:
            skip_next = True
            continue
        if word in DROPPED_WORDS:
            continue
        kept.append(str(probe) if word == source else word)
    return [*kept, "-fsyntax-only"]


def counted_compile(command: list[str], directory: str) -> tuple[int | None, str]:
    """Run one compile under the instruction counter, and give its count, or None, and its error output."""
    from assay.core import meter

    descriptor = meter.open_instruction_counter()
    if descriptor is None:
        return None, ""
    try:
        done = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=COMPILE_S, check=False)
    except subprocess.TimeoutExpired:
        meter.read_instruction_counter(descriptor)
        return -1, f"the compile ran for more than {COMPILE_S} s"
    count = meter.read_instruction_counter(descriptor)
    if done.returncode != 0:
        return -1, done.stderr[-4000:]
    return count, ""


def main(argv: list[str]) -> int:
    """Compile each probe, and judge its count against its ceiling (THE PROBES)."""
    if len(argv) not in (2, 3) or (len(argv) == 3 and argv[2] != "--measure"):
        print(__doc__.splitlines()[2], file=sys.stderr)
        return USAGE
    build, root = Path(argv[0]).resolve(), Path(argv[1]).resolve()
    is_measure = len(argv) == 3
    sys.path.insert(0, str(root / "utils"))
    found = reference_command(build)
    if found is None:
        print(f"check_ceilings: the compile database of {build} has no {REFERENCE_UNIT}, so the check skips")
        return SKIPPED
    words, directory, source = found
    failures = 0
    for probe in sorted(Path(__file__).resolve().parent.glob("probe_*.cpp")):
        measured = MEASURED_LINE.search(probe.read_text(encoding="utf-8"))
        if measured is None and not is_measure:
            print(f"check_ceilings: {probe.name} has no line '// measured: N G instructions'")
            failures += 1
            continue
        count, error = counted_compile(probe_command(words, source, probe), directory)
        if count is None:
            print("check_ceilings: the host gives no exact instruction counter, so the check skips")
            return SKIPPED
        if count < 0:
            print(f"check_ceilings: {probe.name} does not compile: {error}")
            failures += 1
            continue
        if is_measure or measured is None:
            print(f"check_ceilings: {probe.name}: {count / GIGA:.3f} G instructions")
            continue
        ceiling = CEILING_FACTOR * float(measured.group(1))
        is_over = count / GIGA > ceiling
        print(
            f"check_ceilings: {probe.name}: {count / GIGA:.3f} G instructions, ceiling {ceiling:.3f} G "
            f"({CEILING_FACTOR:g} x {measured.group(1)} G){'  ABOVE THE CEILING' if is_over else ''}"
        )
        failures += int(is_over)
    return FAILED if failures else OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
