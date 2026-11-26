"""Compile the probes of fixy/fp at x86-64-v3 and x86-64-v4, and refuse each fma that no fp::fma gives.

usage: check_fp_fma.py BUILD_DIR SOURCE_ROOT

Probe
    test/fixy/fp_elementary_unit_v3.cpp instantiates the probes of
    test/fixy/fp_elementary_forms.h: one function out of line for exp, log and
    erf of double and of float.  Each probe holds the body of its function or a
    call of it, and the cold paths keep their own symbols in the namespace
    fixy::fp.

Compile
    The check compiles the probe with the command of PROBE_UNIT's neighbor
    REFERENCE_UNIT in the compile database of the build, so the probe gets the
    flags of the test, and the floor crucible_fp_strict with them.  The compile
    drops the words of the quarantine plugin and of a sanitizer, and it puts
    -O3 and the word of one level at the end: -march=x86-64-v3, then
    -march=x86-64-v4.  GCC forms an fma from a multiply and an add only at -O2
    and above, so -O3 shows what the contraction flags of the build permit,
    also in a build at -O1.
    For each level it compiles three objects:
      built  the flags of the build
      off    the flags of the build and -ffp-contract=off at the end
      fast   the flags of the build and -ffp-contract=fast at the end, the
             control
    The three compiles differ only in the contraction of a multiply and an add.
    So the fma instructions of `off` are the fma of fp::fma, at the places
    where the optimizer lays out each fp::fma site.

Verdict
    For each level and each function of the namespace fixy::fp and each
    probe, the count of the fma instructions (vfmadd, vfmsub, vfnmadd and
    vfnmsub) of `built` must equal the count of `off`.  A contraction of the
    build flags adds an fma, and the count of `built` is then larger.  The
    total of `fast` must be larger than the total of `off`: the control shows
    that the check sees a contraction.  The total of `off` must be above zero.
    Exit 0 when each count holds, 1 when a count does not hold or a probe does
    not compile, 2 on a usage error, 77 for a skip on a host that is not
    x86_64 or a build whose compiler does not target x86_64, such as the cross
    build of aarch64.  Complexity: six compiles and six disassemblies.
"""

from __future__ import annotations

import platform
import subprocess
import sys
import tempfile
from collections import Counter
from collections.abc import Sequence
from pathlib import Path

REFERENCE_UNIT = "test/fixy/test_fp_elementary.cpp"
PROBE_UNIT = "test/fixy/fp_elementary_unit_v3.cpp"
LEVELS = ("-march=x86-64-v3", "-march=x86-64-v4")
OPTIMIZATION = "-O3"
VARIANTS = {"built": (), "off": ("-ffp-contract=off",), "fast": ("-ffp-contract=fast",)}
DROPPED_PREFIXES = ("-fplugin", "-fsanitize", "-fno-sanitize", "-DCRUCIBLE_QUARANTINE_ACTIVE")
FMA_PREFIXES = ("vfmadd", "vfmsub", "vfnmadd", "vfnmsub")
# The names of the functions that the check counts: the namespace of the functions and the probes.
COUNTED_NAMES = ("fixy::fp::", "test_fp_elementary::probe_")
SKIPPED = 77


def probe_command(build: Path, root: Path, level: str, extra: Sequence[str], obj: Path) -> tuple[list[str], str]:
    """Return the words and the directory of the compile of the probe at a level, with extra words at the end.

    Raises:
        RuntimeError: If the compile database holds no REFERENCE_UNIT
    """
    from assay.readers import compdb

    entry = next((item for item in compdb.entries(build) if item.file.endswith("/" + REFERENCE_UNIT)), None)
    if entry is None:
        raise RuntimeError(f"the compile database of {build} holds no {REFERENCE_UNIT}")
    arguments = list(entry.argv())
    words: list[str] = []
    index = 0
    while index < len(arguments):
        word = arguments[index]
        index += 1
        if word in ("-o", "-MF", "-MT") and index < len(arguments):
            if word == "-o":
                words += [word, str(obj)]
            index += 1
        elif word == "-MD":
            continue
        elif word.endswith(REFERENCE_UNIT):
            words.append(str(root / PROBE_UNIT))
        elif not word.startswith(DROPPED_PREFIXES):
            words.append(word)
    return [*words, OPTIMIZATION, level, *extra], entry.directory


def target_of_build(build: Path) -> str:
    """Return the target triple of the compiler of REFERENCE_UNIT, or an empty text when the build has no such unit."""
    from assay.readers import compdb

    entry = next((item for item in compdb.entries(build) if item.file.endswith("/" + REFERENCE_UNIT)), None)
    if entry is None:
        return ""
    done = subprocess.run([entry.argv()[0], "-dumpmachine"], capture_output=True, text=True, check=False)
    return done.stdout.strip() if done.returncode == 0 else ""


def fma_counts(obj: Path) -> Counter[str]:
    """Return the count of the fma instructions of each counted function of an object."""
    from assay.readers import objdump

    counts: Counter[str] = Counter()
    for function in objdump.functions(obj):
        if not any(part in function.name for part in COUNTED_NAMES):
            continue
        counts[function.name] += sum(1 for item in function.instructions if item.mnemonic.startswith(FMA_PREFIXES))
    return counts


def compile_counts(build: Path, root: Path, scratch: Path) -> dict[tuple[str, str], Counter[str]]:
    """Compile the probe for each level and variant, and return the counts of each object.

    Raises:
        RuntimeError: If a compile fails
    """
    counts: dict[tuple[str, str], Counter[str]] = {}
    for level in LEVELS:
        for variant, extra in VARIANTS.items():
            obj = scratch / f"probe{level.removeprefix('-march=')}-{variant}.o"
            words, directory = probe_command(build, root, level, extra, obj)
            done = subprocess.run(words, cwd=directory, capture_output=True, text=True, check=False)
            if done.returncode != 0:
                raise RuntimeError(f"the probe does not compile at {level} ({variant}):\n{done.stderr[-6000:]}")
            counts[(level, variant)] = fma_counts(obj)
    return counts


def verdict(counts: dict[tuple[str, str], Counter[str]]) -> list[str]:
    """Return each problem of the counts."""
    problems: list[str] = []
    for level in LEVELS:
        built, off, fast = counts[(level, "built")], counts[(level, "off")], counts[(level, "fast")]
        for name in sorted(set(built) | set(off)):
            if built[name] != off[name]:
                problems.append(
                    f"{level}: {name} holds {built[name]} fma with the build flags and {off[name]} with "
                    f"-ffp-contract=off: the build contracts a multiply and an add"
                )
        if sum(off.values()) == 0:
            problems.append(f"{level}: the probe holds no fma, so the check reads no fp::fma")
        if sum(fast.values()) <= sum(off.values()):
            problems.append(
                f"{level}: the control with -ffp-contract=fast holds {sum(fast.values())} fma and the "
                f"probe {sum(off.values())}, so the check does not see a contraction"
            )
    return problems


def main(arguments: Sequence[str]) -> int:
    """Compile the probes, count, and print the verdict."""
    if len(arguments) != 2:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 2
    if platform.machine() not in ("x86_64", "AMD64"):
        print(f"check_fp_fma: SKIPPED: the host is {platform.machine()}, and the check reads x86-64 code")
        return SKIPPED
    build, root = Path(arguments[0]).resolve(), Path(arguments[1]).resolve()
    sys.path.insert(0, str(root / "utils"))
    target = target_of_build(build)
    if not target.startswith("x86_64"):
        print(
            f"check_fp_fma: SKIPPED: the build compiles for {target or 'no known target'}, and the check reads "
            f"x86-64 code"
        )
        return SKIPPED
    with tempfile.TemporaryDirectory(prefix="fp-fma-") as scratch:
        try:
            counts = compile_counts(build, root, Path(scratch))
        except RuntimeError as problem:
            print(f"check_fp_fma: {problem}")
            return 1
    problems = verdict(counts)
    for problem in problems:
        print(f"check_fp_fma: {problem}")
    if problems:
        return 1
    for level in LEVELS:
        off, fast = counts[(level, "off")], counts[(level, "fast")]
        print(
            f"check_fp_fma: {level}: {sum(off.values())} fma in {len(off)} functions, each from an fp::fma.  "
            f"The control with -ffp-contract=fast holds {sum(fast.values())}."
        )
        for name in sorted(off):
            print(f"  {off[name]:3d}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
