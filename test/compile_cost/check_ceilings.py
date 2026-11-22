"""Compile each probe of a heavy compile-time gate, and fail when it costs more than its ceiling.

usage: check_ceilings.py BUILD_DIR SOURCE_ROOT [--measure]

Probes
    Each file probe_*.cpp of this directory runs one heavy gate of the base on
    a fixed input, such as the checkpoint verdict of fixy/session/Checkpoint.h.
    Its line "// measured: N G instructions" gives the user instructions of
    the compiler for the probe when the probe was written.  The ceiling of the
    probe is CEILING_FACTOR times that count.  A gate that loses its cache, or
    a walk that grows with the paths of a protocol in place of its nodes, then
    costs many times its count, and the check fails.  The ceiling is in the
    probe, so the check has no ledger.

Compile
    The compile of a probe is the compile of REFERENCE_UNIT in the compile
    database of the build, with the probe as its source and -fsyntax-only.  The
    probe then gets the flags of a test of the session layer, with the
    quarantine plugin.  The compile writes no file: the check removes -c, -o
    and the words of a dependency file.  With -fsyntax-only and no report
    directory, the plugin writes its findings as notes and no file
    (utils/tools/quarantine/plugin.cpp).  The count then holds the gate, the
    headers and the census of the plugin, as the compile of a test does.

Count
    The count is the user instructions of the compiler and of each process
    that it starts, from a hardware counter (the instruction count of
    utils/assay/core/meter.py).  The count changes by less than 0.1 % between
    two compiles at each load of the host, and the wall time does not stay
    so.  A host with no exact counter, and a build with no REFERENCE_UNIT,
    give the exit SKIPPED.

Operations
    A probe with the line "// operations: N" also states that its heaviest
    constant evaluation counts the same operations under the flags of each
    build.  The verdict of a gate at the operation limit of a constant
    evaluation (-fconstexpr-ops-limit) then does not depend on the build.  A
    gate that runs a body of the standard library that a macro of the build
    changes breaks that: the sanitizer guards of each std::vector operation in
    the Debug build with AddressSanitizer cost the checkpoint walk of
    fixy/session/Checkpoint.h about 3 % more operations, so one pair met the
    limit in Debug and not in Release.  The check finds the count of the probe
    under the flags of this build, the smallest limit at which the probe
    compiles, and then compiles the probe under the plain flags at that count
    and one below it.  The plain flags are the flags of this build with no
    sanitizer, no macro of the library checks, no NDEBUG and no contract
    semantic (PLAIN_DROPPED).  Each build runs the check, so each build must
    count what the plain flags count.  N is the count when the probe was
    written.  A probe whose count moved still passes, after a search of its
    count, and the check prints the new count for its line.  This check needs
    no instruction counter.

--measure prints the count of each probe and judges no ceiling.  Exit 0 when
each probe is at or below its ceiling and counts the same operations under
the plain flags, 1 when one is above it, counts a different number of
operations or does not compile, 2 on a usage error, 77 for a skip.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

# The unit of the compile database whose flags each probe gets (the compile).
REFERENCE_UNIT = "test/fixy/test_session_checkpoint_work_bound.cpp"
# The ceiling of a probe as a multiple of its measured count (the probes).
CEILING_FACTOR = 2.0
# The line of a probe that gives its measured count, in G (10^9) instructions.
MEASURED_LINE = re.compile(r"^// measured: ([0-9]+(?:\.[0-9]+)?) G instructions$", re.MULTILINE)
GIGA = 1e9
# The time limit of one compile, in seconds.  A walk of the paths of a protocol can run for hours.
COMPILE_S = 900
# The exits of the check.
OK, FAILED, USAGE, SKIPPED = 0, 1, 2, 77
# The words that the compile of a probe drops, and the words whose next word it drops too (the compile).
DROPPED_WORDS = frozenset({"-c", "-MD", "-MMD"})
DROPPED_PAIRS = frozenset({"-o", "-MF", "-MT", "-MQ"})
# The line of a probe that gives the operations of its heaviest constant evaluation (the operations).
OPERATIONS_LINE = re.compile(r"^// operations: ([0-9]+)$", re.MULTILINE)
# The prefix of the operation limit of a compile, and a word of the refusal of a compile that meets it.
LIMIT_PREFIX = "-fconstexpr-ops-limit="
LIMIT_WORD = "constexpr-ops-limit"
# The prefixes of the words that the plain flags drop, and the word that they add (the operations).
PLAIN_DROPPED = ("-fsanitize=", "-fno-sanitize-recover", "-D_GLIBCXX_ASSERTIONS", "-D_GLIBCXX_SANITIZE_VECTOR",
                 "-DNDEBUG", "-UNDEBUG", "-fcontract-evaluation-semantic=", "-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE",
                 LIMIT_PREFIX)
PLAIN_ADDED = "-D_GLIBCXX_NO_ASSERTIONS"


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


class ProbeError(Exception):
    """A compile of a probe that fails for a reason other than the operation limit."""


def fits(command: list[str], directory: str, limit: int) -> bool:
    """Compile under one operation limit: True when the probe compiles, False when it meets the limit."""
    try:
        done = subprocess.run([*command, f"{LIMIT_PREFIX}{limit}"], cwd=directory, capture_output=True, text=True,
                              timeout=COMPILE_S, check=False)
    except subprocess.TimeoutExpired as error:
        raise ProbeError(f"the compile ran for more than {COMPILE_S} s") from error
    if done.returncode == 0:
        return True
    if LIMIT_WORD in done.stderr:
        return False
    raise ProbeError(done.stderr[-4000:])


def operation_count(command: list[str], directory: str, hint: int) -> int:
    """Return the smallest operation limit at which the probe compiles: two compiles when it is the hint.

    Complexity: two compiles, or a doubling search and a binary search of about thirty compiles when the count
    moved from the hint.
    """
    if fits(command, directory, hint) and not fits(command, directory, hint - 1):
        return hint
    # The search keeps a limit that the probe meets at low and a limit at which it fits at high.  No limit is
    # zero, and the search treats zero as a limit that each probe meets.
    low, high = 0, max(hint, 1)
    while not fits(command, directory, high):
        low, high = high, high * 2
    while low == 0 and high > 1:
        half = high // 2
        if not fits(command, directory, half):
            low = half
            break
        high = half
    while high - low > 1:
        middle = (low + high) // 2
        if fits(command, directory, middle):
            high = middle
        else:
            low = middle
    return high


def plain_command(command: list[str]) -> list[str]:
    """Return the compile under the plain flags (the operations)."""
    return [word for word in command if not word.startswith(PLAIN_DROPPED)] + [PLAIN_ADDED]


def judge_operations(command: list[str], directory: str, probe: Path, hint: int) -> bool:
    """Compare the operations of the probe under the flags of this build and under the plain flags."""
    own = [word for word in command if not word.startswith(LIMIT_PREFIX)]
    plain = plain_command(own)
    try:
        count = operation_count(own, directory, hint)
        is_same = fits(plain, directory, count) and not fits(plain, directory, count - 1)
        plain_count = count if is_same else operation_count(plain, directory, count)
    except ProbeError as error:
        print(f"check_ceilings: {probe.name} does not compile: {error}")
        return False
    moved = "" if count == hint else f"; the line says {hint}, so write '// operations: {count}'"
    if count != plain_count:
        print(f"check_ceilings: {probe.name}: {count} operations under the flags of this build and {plain_count} "
              f"under the plain flags, so a verdict at the operation limit depends on the build{moved}")
        return False
    print(f"check_ceilings: {probe.name}: {count} operations, the same under the plain flags{moved}")
    return True


def main(argv: list[str]) -> int:
    """Compile each probe, and judge its count against its ceiling (the probes) and its operations."""
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
    probes = sorted(Path(__file__).resolve().parent.glob("probe_*.cpp"))
    for probe in probes:
        operations = OPERATIONS_LINE.search(probe.read_text(encoding="utf-8"))
        if operations is not None and not is_measure:
            failures += int(not judge_operations(probe_command(words, source, probe), directory, probe,
                                                 int(operations.group(1))))
    for probe in probes:
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
