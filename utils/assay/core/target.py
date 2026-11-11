"""target — the build kind of a build, and the native tier of a host for the key of a measurement.

THE BUILD KIND
    The one line of BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake
    writes from the processor, the build type and the sanitizer and analysis
    options of the build, for example x86_64-debug-asan or aarch64-release
    (read_kind of utils/assay/core/meter.py).

THE NATIVE TIER
    A native flag, such as -march=native, is a request.  The compiler
    resolves it to the processor of the host.  native_tier() gives the tier
    NAME-DIGEST of that request on this host (resolved_tier()).  The
    compiler prints its target for the flag with -Q --help=target:
      * NAME is the value of the -march= line.  An aarch64 compiler gives its
        target through -mcpu, and it can print no -march value.  NAME is then
        the value of the -mcpu= line, and "default" when no line has a value.
      * DIGEST is the first 8 hexadecimal digits of the SHA-256 of the
        values of the -march=, -mcpu= and -mtune= lines and of each target
        option that the compiler prints as [enabled].
    The tier names the processor of the host.  It is the key of a
    measurement, which belongs to its host (assay.loop.evidence).  No ledger
    reads it.

`guard selftest core.target` does a test of the module.
"""

from __future__ import annotations

import hashlib
import re
import shlex
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc, testing

TIER_LINE = re.compile(r"\s*-m(arch|cpu|tune)=\s*(\S*)\s*")
DEFAULT_NAME = "default"


class TierError(ValueError):
    """The compiler does not print its target."""


def kind_of(build_dir: Path) -> str | None:
    """Return the build kind of a build directory, or None when it has no build-kind.txt."""
    return cost_meter.read_kind(str(build_dir))


def tier_of(help_text: str) -> str:
    """Return the tier from the output of -Q --help=target.  Complexity: linear in the size of the output."""
    values: dict[str, str] = {}
    for line in help_text.splitlines():
        match = TIER_LINE.fullmatch(line)
        if match is not None:
            values.setdefault(match[1], match[2])
    name = next((values[key] for key in ("arch", "cpu") if values.get(key) and values[key] != "native"), DEFAULT_NAME)
    enabled = sorted(" ".join(line.split()) for line in help_text.splitlines() if line.rstrip().endswith("[enabled]"))
    facts = [f"{key}={values.get(key, '')}" for key in ("arch", "cpu", "tune")] + enabled
    return f"{name}-{hashlib.sha256(chr(10).join(facts).encode()).hexdigest()[:8]}"


def resolved_tier(compiler: str, flags: list[str]) -> str:
    """Return the tier NAME-DIGEST that the compiler gives for the target flags (THE NATIVE TIER).

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
        raise TierError(
            f"{shlex.join(command)} exits {done.exit} and prints no -march, -mcpu or -mtune line, "
            f"so the target of the build is unknown: {done.err.strip()[:300]}"
        )
    return tier_of(done.out)


def native_tier(compiler: str, flag: str) -> str:
    """Return the resolved tier of one native flag on this host, or an empty text when the compiler refuses the flag.

    The tier names the processor of the host.  Only the key of a measurement reads it (THE NATIVE TIER).
    """
    try:
        return resolved_tier(compiler, [flag])
    except TierError:
        return ""


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
AARCH64_NATIVE_HELP = AARCH64_HELP.replace(
    "-march=                     \t\t\n", "-march=                     \t\tarmv9-a+sve2-sha3+sve2-sm4\n"
)
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
native = (Path(__file__).parent / "native.txt").read_text().strip()
print({X86_HELP!r}.replace("x86-64", native if "-march=native" in sys.argv else "x86-64"))
"""
# A planted cross compiler for aarch64, which refuses -march=native as a cross compiler does.
PLANTED_CROSS = f"""import sys
sys.exit(1) if "-march=native" in sys.argv else print({AARCH64_HELP!r})
"""


type Expect = Callable[[str, bool, object], None]


def tier_cases(expect: Expect) -> None:
    """Hold the tier NAME-DIGEST of planted compiler output, and a compiler that cannot run."""
    plain, native, x86 = (tier_of(AARCH64_HELP), tier_of(AARCH64_NATIVE_HELP), tier_of(X86_HELP))
    expect("an aarch64 compiler with no -march value has the tier default", plain.startswith("default-"), plain)
    expect(
        "the resolved tier of an aarch64 -march=native names its resolved -march",
        native.startswith("armv9-a+sve2-sha3+sve2-sm4-") and native != plain,
        native,
    )
    expect(
        "an -mcpu value names the tier when -march has none",
        tier_of(
            AARCH64_HELP.replace("-mcpu=                      \t\t\n", "-mcpu=                      \t\tneoverse-n2\n")
        ).startswith("neoverse-n2-"),
        "",
    )
    expect(
        "an x86 compiler names its -march, and the list of valid arguments is no value",
        x86.startswith("x86-64-")
        and x86
        != tier_of(
            X86_HELP.replace("-mavx2                      \t\t[disabled]", "-mavx2                      \t\t[enabled]")
        ),
        x86,
    )
    expect("the same output gives the same tier", tier_of(X86_HELP) == x86, "")
    try:
        resolved_tier("/nonexistent/g++", [])
        expect("a compiler that cannot run is a TierError", False, "")
    except TierError:
        expect("a compiler that cannot run is a TierError", True, "")


def native_cases(expect: Expect, scratch: Path) -> None:
    """Hold the native tier of a planted compiler on two planted hosts, and of a compiler that refuses the flag."""
    compiler = testing.planted_program(scratch, "g++", PLANTED_X86)
    host = scratch / "native.txt"
    host.write_text("znver5\n", encoding="utf-8")
    first_host = native_tier(compiler, "-march=native")
    host.write_text("znver4\n", encoding="utf-8")
    expect(
        "native_tier names the processor of the host, for the key of a measurement",
        first_host.startswith("znver5-") and native_tier(compiler, "-march=native").startswith("znver4-"),
        first_host,
    )
    cross = testing.planted_program(scratch, "cross-g++", PLANTED_CROSS)
    expect("a compiler that refuses a native flag gives no native tier", native_tier(cross, "-march=native") == "", "")


def self_test() -> int:
    """Do a test of the tier with planted compiler output.

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
        native_cases(expect, Path(scratch_text))
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
