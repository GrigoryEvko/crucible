"""target — the build kind and the target tier of a build, and the rows of a build that a ledger does not hold.

The rules quarantine and artifact-symbols hold ledgers with rows that depend
on the build.  This module gives these rules the same facts, and the same
line for a build that the ledger does not hold.

THE BUILD KIND
    The one line of BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake
    writes from the processor, the build type and the sanitizer and analysis
    options of the build, for example x86_64-debug-asan or aarch64-release
    (read_kind of utils/assay/core/meter.py).

THE TARGET TIER
    The target flags of a build are the last -march= and the last -mcpu= of
    CMAKE_CXX_FLAGS and of the flags of its build type.  The tier of target
    flags with no native flag is NAME-DIGEST (resolved_tier()).  The compiler
    prints its target for those flags with -Q --help=target:
      * NAME is the value of the -march= line.  An aarch64 compiler gives its
        target through -mcpu, and it can print no -march value.  NAME is then
        the value of the -mcpu= line, and "default" when no line has a value.
      * DIGEST is the first 8 hexadecimal digits of the SHA-256 of the
        values of the -march=, -mcpu= and -mtune= lines and of each target
        option that the compiler prints as [enabled].
    An explicit target, such as -march=x86-64-v3, then has the same tier on
    each host.

    A native flag, such as -march=native, is a request.  The compiler
    resolves it to the processor of the host.  The tier of a native request
    is native-MACHINE (request_tier()), such as native-x86_64.  MACHINE is
    the first field of the machine that the compiler gives with
    -dumpmachine.  When a target flag of the build is a native flag, the
    tier of the build flags is that request.  That tier names no other
    target flag, and no build of this tree gives a native flag with an
    explicit target.

    A target can add a native flag to its own compiles, as each bench target
    does with -march=native.  So a check can give the compile commands of its
    objects.  The tier then adds "+" and the tier of each native flag of
    those commands that the compiler accepts and that is not the tier of the
    build flags.  A cross compiler refuses a native flag, and that flag then
    adds no tier.  The tier names no processor of the host.  Two hosts of
    one architecture then give one tier, also when the processors of the
    hosts make the native compiles select other preprocessor arms.

    native_tier() gives the resolved tier of a native flag on this host.  It
    is the key of a measurement, which belongs to its host
    (assay.loop.evidence), and no configuration of a ledger reads it.

A BUILD THAT THE LEDGER DOES NOT HOLD
    A rule prints one line with not_held().  The line gives the kind or the
    configuration, the tier of the build and what the ledger holds.  It also
    tells what the rule does with the build.  Then the rule prints the rows
    of the build for the ledger, each with a prefix.  Only CI builds an aarch64 kind.  The log of its leg then holds
    the rows, and --import LOG of `guard write` puts them into the ledger.
    rows_in_log() reads the rows back from log lines with any other prefix in
    front.

`guard selftest core.target` does a test of the module.
"""

from __future__ import annotations

import hashlib
import re
import shlex
import tempfile
from collections.abc import Callable, Iterable
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc, testing
from assay.readers import cmake

TARGET_OPTIONS = ("-march=", "-mcpu=")
NATIVE_FLAGS = frozenset({"-march=native", "-mcpu=native"})
TIER_LINE = re.compile(r"\s*-m(arch|cpu|tune)=\s*(\S*)\s*")
DEFAULT_NAME = "default"
# The first word of the tier of a native request (THE TARGET TIER).
NATIVE_NAME = "native"


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


def resolved_tier(compiler: str, flags: list[str]) -> str:
    """Return the tier NAME-DIGEST that the compiler gives for the target flags (THE TARGET TIER).

    For a native flag, the result names the processor of the host.

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


def machine_of(compiler: str) -> str:
    """Return the first field of the machine that the compiler gives with -dumpmachine, such as x86_64.

    Raises:
        TierError: If the compiler cannot run, fails, or prints no machine
    """
    command = [compiler, "-dumpmachine"]
    try:
        done = proc.run(proc.Command(command, timeout_s=proc.QUICK_S))
    except proc.ToolFailed as problem:
        raise TierError(f"{shlex.join(command)} cannot run: {problem}") from None
    machine = done.out.strip().split("-", 1)[0]
    if done.exit != 0 or not machine or any(character.isspace() for character in machine):
        raise TierError(f"{shlex.join(command)} exits {done.exit} and prints no machine, so the architecture of the "
                        f"build is unknown: {done.err.strip()[:300]}")
    return machine


def request_tier(compiler: str, flag: str) -> str:
    """Return the tier native-MACHINE of one native flag, or an empty text when the compiler refuses the flag.

    The tier names the request and the architecture of the compiler, and no processor of the host (THE TARGET TIER).
    A cross compiler has no native target, and `loop syntax` drops each native flag of its compiles.  A compiler that
    refuses a native flag then adds no tier.

    Raises:
        TierError: If the compiler accepts the flag and prints no machine
    """
    try:
        resolved_tier(compiler, [flag])
    except TierError:
        return ""
    return f"{NATIVE_NAME}-{machine_of(compiler)}"


def target_tier(compiler: str, flags: list[str]) -> str:
    """Return the tier of the target flags of a build: the native request of a native flag, else NAME-DIGEST.

    Raises:
        TierError: If the compiler cannot run, fails, refuses a native flag of the build, or prints no target option
    """
    natives = [flag for flag in flags if flag in NATIVE_FLAGS]
    if not natives:
        return resolved_tier(compiler, flags)
    tier = request_tier(compiler, natives[-1])
    if not tier:
        raise TierError(f"{compiler} refuses the flag {natives[-1]} of the build, so the target of the build is "
                        f"unknown")
    return tier


def build_tier(build_dir: Path, compiler: str, commands: Iterable[str] = ()) -> str:
    """Return the tier of a build: the tier of its target flags, and the request of each native flag of the commands.

    Complexity: linear in the size of the commands, and at most two compiler runs for each distinct native flag.

    Raises:
        TierError: If the CMake cache of the build cannot be read, or the compiler does not print its target
    """
    base = target_tier(compiler, target_flags(build_dir))
    natives = sorted({word for command in commands for word in command.split() if word in NATIVE_FLAGS})
    extra = sorted({tier for tier in (request_tier(compiler, flag) for flag in natives) if tier not in ("", base)})
    return "+".join([base, *extra])


def native_tier(compiler: str, flag: str) -> str:
    """Return the resolved tier of one native flag on this host, or an empty text when the compiler refuses the flag.

    The tier names the processor of the host.  Only the key of a measurement reads it (THE TARGET TIER).
    """
    try:
        return resolved_tier(compiler, [flag])
    except TierError:
        return ""


def not_held(check: str, what: str, tier: str, held: Iterable[tuple[str, str]], consequence: str,
             detail: str = "") -> str:
    """Return the one line of a build that the ledger does not hold.

    The line gives what the build is, its tier and what the ledger holds.  Then `consequence` tells what the check
    does with the build.
    """
    holds = ", ".join(f"{name} on {held_tier}" for name, held_tier in held) or "nothing"
    more = f"  {detail}." if detail else ""
    return f"{check}: the ledger holds no rows of {what} on the tier {tier}.  It holds {holds}.{more}  {consequence}"


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
# A planted x86_64 compiler.  -march=native resolves to the processor that the file native.txt beside it names, the
# processor of a planted host.
PLANTED_X86 = f"""import sys
from pathlib import Path
if "-dumpmachine" in sys.argv:
    sys.exit(print("x86_64-pc-linux-gnu"))
native = (Path(__file__).parent / "native.txt").read_text().strip()
print({X86_HELP!r}.replace("x86-64", native if "-march=native" in sys.argv else "x86-64"))
"""
# A planted cross compiler for aarch64, which refuses -march=native as a cross compiler does.
PLANTED_CROSS = f"""import sys
if "-dumpmachine" in sys.argv:
    sys.exit(print("aarch64-linux-gnu"))
sys.exit(1) if "-march=native" in sys.argv else print({AARCH64_HELP!r})
"""
# A planted compiler that accepts each flag and prints no machine.
PLANTED_MUTE = f"""import sys
sys.exit(0) if "-dumpmachine" in sys.argv else print({X86_HELP!r})
"""


type Expect = Callable[[str, bool, object], None]


def tier_cases(expect: Expect) -> None:
    """Hold the tier NAME-DIGEST of planted compiler output, and a compiler that cannot run."""
    plain, native, x86 = tier_of(AARCH64_HELP), tier_of(AARCH64_NATIVE_HELP), tier_of(X86_HELP)
    expect("an aarch64 compiler with no -march value has the tier default", plain.startswith("default-"), plain)
    expect("the resolved tier of an aarch64 -march=native names its resolved -march",
           native.startswith("armv9-a+sve2-sha3+sve2-sm4-") and native != plain, native)
    expect("an -mcpu value names the tier when -march has none",
           tier_of(AARCH64_HELP.replace("-mcpu=                      \t\t\n",
                                        "-mcpu=                      \t\tneoverse-n2\n")).startswith("neoverse-n2-"),
           "")
    expect("an x86 compiler names its -march, and the list of valid arguments is no value",
           x86.startswith("x86-64-") and x86 != tier_of(X86_HELP.replace("-mavx2                      \t\t[disabled]",
                                                                         "-mavx2                      \t\t[enabled]")),
           x86)
    expect("the same output gives the same tier", tier_of(X86_HELP) == x86, "")
    try:
        target_tier("/nonexistent/g++", [])
        expect("a compiler that cannot run is a TierError", False, "")
    except TierError:
        expect("a compiler that cannot run is a TierError", True, "")


def build_cases(expect: Expect, scratch: Path) -> None:
    """Hold the tier of a planted build on two planted hosts of one architecture, and the native requests."""
    compiler = testing.planted_program(scratch, "g++", PLANTED_X86)
    host = scratch / "native.txt"
    host.write_text("znver5\n", encoding="utf-8")
    try:
        build_tier(scratch, compiler)
        expect("a build whose CMake cache cannot be read is a TierError", False, "")
    except TierError as problem:
        expect("a build whose CMake cache cannot be read is a TierError", "cannot be read" in str(problem), problem)
    cmake.plant(scratch, cache={"CMAKE_BUILD_TYPE": ("STRING", "Debug"), "CMAKE_CXX_FLAGS": ("STRING", "")})
    commands = ["g++ -O1 -c a.cpp", "g++ -O3 -march=native -c bench.cpp"]
    plain_tier = build_tier(scratch, compiler, commands[:1])
    bench_tier = build_tier(scratch, compiler, commands)
    expect("a compile with its own native flag adds the native request and the architecture to the tier",
           plain_tier.startswith("x86-64-") and "+" not in plain_tier and bench_tier == f"{plain_tier}+native-x86_64",
           (plain_tier, bench_tier))
    first_host = native_tier(compiler, "-march=native")
    host.write_text("znver4\n", encoding="utf-8")
    other_host = build_tier(scratch, compiler, commands)
    expect("a host with a different processor gives the build the same tier, so the tier names no fact of the host",
           other_host == bench_tier, other_host)
    expect("native_tier names the processor of the host, for the key of a measurement",
           first_host.startswith("znver5-") and native_tier(compiler, "-march=native").startswith("znver4-"),
           first_host)
    cmake.plant(scratch, cache={"CMAKE_BUILD_TYPE": ("STRING", "Release"),
                                "CMAKE_CXX_FLAGS": ("STRING", "-march=native")})
    release_tier = build_tier(scratch, compiler, commands[1:])
    expect("a native flag of the build flags gives the tier of the request, and no second tier",
           release_tier == "native-x86_64", release_tier)
    cross = testing.planted_program(scratch, "cross-g++", PLANTED_CROSS)
    try:
        build_tier(scratch, cross)
        expect("a build flag that the compiler refuses is a TierError", False, "")
    except TierError as problem:
        expect("a build flag that the compiler refuses is a TierError", "refuses the flag" in str(problem), problem)
    cmake.plant(scratch, cache={"CMAKE_BUILD_TYPE": ("STRING", "Debug"), "CMAKE_CXX_FLAGS": ("STRING", "")})
    cross_tier = build_tier(scratch, cross, commands[1:])
    expect("a compiler that refuses a native flag adds no tier for it", cross_tier == tier_of(AARCH64_HELP),
           cross_tier)
    mute = testing.planted_program(scratch, "mute-g++", PLANTED_MUTE)
    try:
        build_tier(scratch, mute, commands[1:])
        expect("a compiler that accepts a native flag and prints no machine is a TierError", False, "")
    except TierError as problem:
        expect("a compiler that accepts a native flag and prints no machine is a TierError",
               "prints no machine" in str(problem), problem)


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

    tier_cases(expect)
    with tempfile.TemporaryDirectory(prefix="build-target-") as scratch_text:
        build_cases(expect, Path(scratch_text))
    log = ("build+test aarch64 / default\tQuarantine ratchet\t2026-10-02T21:00:00Z check-row: kind | a | b\n"
           "noise\ncheck-row: k | lib.a | allow | abi | __cxa_guard_acquire | x\n")
    expect("rows_in_log reads each row after its prefix",
           rows_in_log(log, "check-row: ") == ["kind | a | b", "k | lib.a | allow | abi | __cxa_guard_acquire | x"])
    line = not_held("check", "aarch64-release", "armv9-a-0000", [("x86_64-release", "znver5-1111")],
                    "The base rows judge the build")
    expect("not_held names the build, its tier, what the ledger holds and the consequence",
           "aarch64-release on the tier armv9-a-0000" in line and "x86_64-release on znver5-1111" in line
           and line.endswith("The base rows judge the build"), line)
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
