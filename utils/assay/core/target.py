"""target — the build kind and the target tier of a build, and the rows of a build that a ledger does not hold.

The rules quarantine and artifact-symbols hold ledgers with rows that
depend on the build.  This module gives the two rules the same facts, and the
same line for a build that the ledger does not hold.

THE BUILD KIND
    The one line of BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake
    writes from the processor, the build type and the sanitizer and analysis
    options of the build, for example x86_64-debug-asan or aarch64-release
    (read_kind of utils/assay/core/meter.py).

THE TARGET TIER
    The target flags of a build are the last -march= and the last -mcpu= of
    CMAKE_CXX_FLAGS and of the flags of its build type.  The compiler prints
    its target for those flags with -Q --help=target.  The tier is NAME-DIGEST:
      * NAME is the value of the -march= line.  An aarch64 compiler gives its
        target through -mcpu, and it can print no -march value.  NAME is then
        the value of the -mcpu= line, and "default" when no line has a value.
      * DIGEST is the first 8 hexadecimal digits of the SHA-256 of the
        values of the -march=, -mcpu= and -mtune= lines and of each target
        option that the compiler prints as [enabled].
    A target can add a native flag to its own compiles, as each bench target
    does with -march=native.  So a check can give the compile commands of its
    objects, and the tier then adds "+" and the tier of each native flag of
    those commands that is not the tier of the build flags.  A build that
    names no native target has the same tier on each host.  A build with a
    native flag on another processor has another tier, and its preprocessor
    arms can be different.

A BUILD THAT THE LEDGER DOES NOT HOLD
    A rule prints one line with not_held(): the kind or the configuration,
    the tier of the build, and what the ledger holds.  Then it prints the
    rows of the build for the ledger, each with a prefix.  Only CI builds an
    aarch64 kind.  The log of its leg then holds the rows, and --import LOG
    of `guard write` puts them into the ledger.  rows_in_log() reads the rows
    back from log lines with any other prefix in front.

`guard selftest core.target` does a test of the module.
"""

from __future__ import annotations

import hashlib
import re
import shlex
import sys
import tempfile
from collections.abc import Iterable
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc
from assay.readers import cmake

TARGET_OPTIONS = ("-march=", "-mcpu=")
NATIVE_FLAGS = frozenset({"-march=native", "-mcpu=native"})
TIER_LINE = re.compile(r"\s*-m(arch|cpu|tune)=\s*(\S*)\s*")
DEFAULT_NAME = "default"


class TierError(ValueError):
    """The compiler does not print its target."""


def kind_of(build_dir: Path) -> str | None:
    """Return the build kind of a build directory, or None when it has no build-kind.txt."""
    return cost_meter.read_kind(str(build_dir))


def target_flags(build_dir: Path) -> list[str]:
    """Return the last -march= and the last -mcpu= of CMAKE_CXX_FLAGS and the flags of the build type.

    Raises:
        TierError: If the CMake cache of the build cannot be read, because the target of the build is then unknown
    """
    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as problem:
        raise TierError(f"the CMake cache of {build_dir} cannot be read, so the target of the build is unknown: "
                        f"{problem}") from None

    def value(name: str) -> str:
        """Return the value of one cache entry, or an empty text when the cache has no such entry."""
        entry = entries.get(name)
        return "" if entry is None else entry.value

    build_type = value("CMAKE_BUILD_TYPE").upper()
    words = shlex.split(" ".join(value(name) for name in ("CMAKE_CXX_FLAGS", f"CMAKE_CXX_FLAGS_{build_type}")))
    flags: list[str] = []
    for option in TARGET_OPTIONS:
        flags += [word for word in words if word.startswith(option)][-1:]
    return flags


def tier_of(help_text: str) -> str:
    """Return the tier from the output of -Q --help=target.  Complexity: linear in the size of the output."""
    values: dict[str, str] = {}
    for line in help_text.splitlines():
        match = TIER_LINE.fullmatch(line)
        if match is not None:
            values.setdefault(match[1], match[2])
    name = next((values[key] for key in ("arch", "cpu") if values.get(key) and values[key] != "native"),
                DEFAULT_NAME)
    enabled = sorted(" ".join(line.split()) for line in help_text.splitlines() if line.rstrip().endswith("[enabled]"))
    facts = [f"{key}={values.get(key, '')}" for key in ("arch", "cpu", "tune")] + enabled
    return f"{name}-{hashlib.sha256(chr(10).join(facts).encode()).hexdigest()[:8]}"


def target_tier(compiler: str, flags: list[str]) -> str:
    """Return the tier that the compiler gives for the target flags.

    Raises:
        TierError: If the compiler cannot run, fails, or prints no target option
    """
    command = [compiler, *flags, "-Q", "--help=target"]
    try:
        done = proc.run(proc.Command(command, timeout_s=proc.QUICK_S))
    except proc.ToolFailed as problem:
        raise TierError(f"{shlex.join(command)} cannot run: {problem}") from None
    if done.exit != 0 or not any(TIER_LINE.fullmatch(line) for line in done.out.splitlines()):
        raise TierError(f"{shlex.join(command)} exits {done.exit} and prints no -march, -mcpu or -mtune line, "
                        f"so the target of the build is unknown: {done.err.strip()[:300]}")
    return tier_of(done.out)


def build_tier(build_dir: Path, compiler: str, commands: Iterable[str] = ()) -> str:
    """Return the tier of a build: the tier of its target flags, and the tier of each native flag of the commands.

    Complexity: linear in the size of the commands, and one compiler run for each distinct tier.

    Raises:
        TierError: If the CMake cache of the build cannot be read, or the compiler does not print its target
    """
    base = target_tier(compiler, target_flags(build_dir))
    natives = sorted({word for command in commands for word in command.split() if word in NATIVE_FLAGS})
    extra = sorted({tier for tier in (target_tier(compiler, [flag]) for flag in natives) if tier != base})
    return "+".join([base, *extra])


def not_held(check: str, what: str, tier: str, held: Iterable[tuple[str, str]], detail: str = "") -> str:
    """Return the one line of a build that the ledger does not hold: what it is, its tier and what the ledger holds."""
    holds = ", ".join(f"{name} on {held_tier}" for name, held_tier in held) or "nothing"
    more = f"  {detail}." if detail else ""
    return f"{check}: the ledger holds no rows of {what} on the tier {tier}.  It holds {holds}.{more}  The check does " \
           f"not apply"


def print_rows(prefix: str, rows: Iterable[str]) -> None:
    """Print each row with the prefix of its check, so that a log line with a longer prefix still gives the row."""
    for row in rows:
        print(f"{prefix}{row}")


def rows_in_log(text: str, prefix: str) -> list[str]:
    """Return the row after the prefix of each line of a log that holds the prefix."""
    return [line.split(prefix, 1)[1].rstrip() for line in text.splitlines() if prefix in line]


# ── The self-test ──────────────────────────────────────────────────


# The output of -Q --help=target of an aarch64 GCC with no target flag, and with -march=native.
AARCH64_HELP = """The following options are target specific:
  -mabi=                      \t\tlp64
  -march=                     \t\t
  -mbig-endian                \t\t[disabled]
  -mbranch-protection=        \t\t
  -mcmodel=                   \t\tsmall
  -mcpu=                      \t\t
  -mfix-cortex-a53-835769     \t\t[enabled]
  -mlittle-endian             \t\t[enabled]
  -mtune=                     \t\t
"""
AARCH64_NATIVE_HELP = AARCH64_HELP.replace("-march=                     \t\t\n",
                                           "-march=                     \t\tarmv9-a+sve2-sha3+sve2-sm4\n")
X86_HELP = """The following options are target specific:
  -m64                        \t\t[enabled]
  -march=                     \t\tx86-64
  -mavx2                      \t\t[disabled]
  -mtune=                     \t\tgeneric
  -mtune-ctrl=                \t\t

  Known valid arguments for -march= option:
    i386 i486 i586 pentium znver5
"""


def self_test() -> int:
    """Do a test of the tier and of the rows of a log, with planted compiler output.

    Returns:
        0 when each case holds, else 1
    """
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}")

    plain, native, x86 = tier_of(AARCH64_HELP), tier_of(AARCH64_NATIVE_HELP), tier_of(X86_HELP)
    expect("an aarch64 compiler with no -march value has the tier default", plain.startswith("default-"), plain)
    expect("an aarch64 -march=native build names its resolved -march", native.startswith("armv9-a+sve2-sha3+sve2-sm4-")
           and native != plain, native)
    expect("an -mcpu value names the tier when -march has none",
           tier_of(AARCH64_HELP.replace("-mcpu=                      \t\t\n",
                                        "-mcpu=                      \t\tneoverse-n2\n")).startswith("neoverse-n2-"))
    expect("an x86 compiler names its -march, and the list of valid arguments is no value",
           x86.startswith("x86-64-") and x86 != tier_of(X86_HELP.replace("-mavx2                      \t\t[disabled]",
                                                                         "-mavx2                      \t\t[enabled]")),
           x86)
    expect("the same output gives the same tier", tier_of(X86_HELP) == x86)
    try:
        target_tier("/nonexistent/g++", [])
        expect("a compiler that cannot run is a TierError", False)
    except TierError:
        expect("a compiler that cannot run is a TierError", True)
    with tempfile.TemporaryDirectory(prefix="build-target-") as scratch_text:
        scratch = Path(scratch_text)
        compiler = scratch / "g++"
        compiler.write_text(f"#!{sys.executable}\nimport sys\n"
                            f"print({X86_HELP!r}.replace('x86-64', 'znver5' if '-march=native' in sys.argv "
                            f"else 'x86-64'))\n", encoding="utf-8")
        compiler.chmod(0o755)
        try:
            build_tier(scratch, str(compiler))
            expect("a build whose CMake cache cannot be read is a TierError", False)
        except TierError as problem:
            expect("a build whose CMake cache cannot be read is a TierError", "cannot be read" in str(problem))
        cmake.plant(scratch, cache={"CMAKE_BUILD_TYPE": ("STRING", "Debug"), "CMAKE_CXX_FLAGS": ("STRING", "")})
        plain_tier = build_tier(scratch, str(compiler), ["g++ -O1 -c a.cpp"])
        bench_tier = build_tier(scratch, str(compiler), ["g++ -O1 -c a.cpp", "g++ -O3 -march=native -c bench.cpp"])
        expect("a compile with its own native flag adds the native tier to the tier",
               plain_tier.startswith("x86-64-") and "+" not in plain_tier
               and bench_tier.startswith(f"{plain_tier}+znver5-"), (plain_tier, bench_tier))
        cmake.plant(scratch, cache={"CMAKE_BUILD_TYPE": ("STRING", "Release"),
                                    "CMAKE_CXX_FLAGS": ("STRING", "-march=native")})
        release_tier = build_tier(scratch, str(compiler), ["g++ -O3 -march=native -c bench.cpp"])
        expect("a native flag of the build flags adds no second tier", release_tier.startswith("znver5-")
               and "+" not in release_tier, release_tier)
    log = ("build+test aarch64 / default\tQuarantine ratchet\t2026-10-02T21:00:00Z check-row: kind | a | b\n"
           "noise\ncheck-row: k | lib.a | allow | abi | __cxa_guard_acquire | x\n")
    expect("rows_in_log reads each row after its prefix",
           rows_in_log(log, "check-row: ") == ["kind | a | b", "k | lib.a | allow | abi | __cxa_guard_acquire | x"])
    line = not_held("check", "aarch64-release", "armv9-a-0000", [("x86_64-release", "znver5-1111")])
    expect("not_held names the build, its tier and what the ledger holds",
           "aarch64-release on the tier armv9-a-0000" in line and "x86_64-release on znver5-1111" in line, line)
    if failures:
        print(f"build_target --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("build_target --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test of the module (`guard selftest core.target`).

    Args:
        argv: --self-test

    Returns:
        The exit status of the self-test, or 2 on a usage error
    """
    from assay.core import report

    return report.self_test_main(argv, self_test, "core.target")
