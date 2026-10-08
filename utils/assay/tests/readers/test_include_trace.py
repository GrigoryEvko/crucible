"""The test of the reader of the include trace of GCC (assay.readers.include_trace).

`guard selftest tests.readers.test_include_trace` runs the test.  Each case
writes one line.  Exit 0 when each case holds, else 1.

The planted cases read a trace with each shape of a line, in a text with
diagnostic lines.  The compile cases read the trace of GCC with a precompiled header
that the compile uses and with one that it cannot use.  ASSAY_TEST_CXX names
the compiler, and the registration of the test in test/CMakeLists.txt gives
the compiler of the build.
"""

import os
import shutil
import tempfile
from pathlib import Path

from assay.core import proc, testing
from assay.readers import include_trace
from assay.readers.include_trace import IncludeStep, Mark

PLANTED = """\
! used.h.gch
 probe.cpp
. inc/outer.h
.. inc/inner.h
x stale.h.gch
probe.cpp:3:5: error: an error with ../inc/outer.h in its text
  • a nested note
      ../inc/outer.h:2:1:
In file included from probe.cpp:2:
   12 | int value = . 3;
Multiple include guards may be useful for:
inc/inner.h
"""


def planted_cases(expect: testing.Expect) -> None:
    """Check the steps of a planted trace in a text with diagnostic lines."""
    steps = include_trace.read(PLANTED)
    expect(
        "read() gives each line of the trace with its depth, its mark and its path, and no other line",
        steps
        == (
            IncludeStep(1, Mark.PRECOMPILED, "used.h.gch"),
            IncludeStep(0, Mark.HEADER, "probe.cpp"),
            IncludeStep(1, Mark.HEADER, "inc/outer.h"),
            IncludeStep(2, Mark.HEADER, "inc/inner.h"),
            IncludeStep(1, Mark.INVALID_PRECOMPILED, "stale.h.gch"),
        ),
    )
    expect("precompiled() gives each precompiled header in use", include_trace.precompiled(steps) == ("used.h.gch",))
    expect("an empty text gives no step", include_trace.read("") == ())


def compiler() -> str:
    """Return the compiler of the compile cases: ASSAY_TEST_CXX, or g++ on PATH."""
    return os.environ.get("ASSAY_TEST_CXX") or shutil.which("g++") or ""


def traced(cxx: str, work: Path, standard: str) -> tuple[IncludeStep, ...]:
    """Build the precompiled header of used.h at one standard, compile probe.cpp with -H, and read the trace.

    CCACHE_DISABLE stops the cache of a ccache wrapper on PATH.  A cache hit can give the precompiled header of
    a different standard, and the trace then shows the mark ! and not the mark x.
    """
    environment = proc.environment({"LC_ALL": "C", "CCACHE_DISABLE": "1"})
    build = [cxx, f"-std={standard}", "-x", "c++-header", "used.h", "-o", "used.h.gch"]
    proc.require(proc.run(proc.Command(build, timeout_s=proc.QUICK_S, cwd=work, env=environment)))
    use = [cxx, "-std=c++26", "-fsyntax-only", include_trace.TRACE_FLAG, "probe.cpp"]
    done = proc.run(proc.Command(use, timeout_s=proc.QUICK_S, cwd=work, env=environment))
    return include_trace.read(done.err)


def compile_cases(expect: testing.Expect) -> None:
    """Read the trace of GCC with a precompiled header in use and with one that the compile cannot use."""
    cxx = compiler()
    expect("ASSAY_TEST_CXX or PATH names a compiler", bool(cxx))
    if not cxx:
        return
    with tempfile.TemporaryDirectory(prefix="include-trace-test-") as scratch:
        work = Path(scratch)
        (work / "inc").mkdir()
        (work / "inc" / "inner.h").write_text("#pragma once\nint inner_value();\n", encoding="utf-8")
        (work / "inc" / "outer.h").write_text(
            '#pragma once\n#include "inner.h"\nint outer_value();\n', encoding="utf-8"
        )
        (work / "used.h").write_text("#pragma once\nint used_value();\n", encoding="utf-8")
        (work / "probe.cpp").write_text('#include "used.h"\n#include "inc/outer.h"\n', encoding="utf-8")
        steps = traced(cxx, work, "c++26")
        expect(
            "a compile that uses a precompiled header gives its line, the main file and each header",
            steps
            == (
                IncludeStep(1, Mark.PRECOMPILED, "used.h.gch"),
                IncludeStep(0, Mark.HEADER, "probe.cpp"),
                IncludeStep(1, Mark.HEADER, "inc/outer.h"),
                IncludeStep(2, Mark.HEADER, "inc/inner.h"),
            ),
        )
        stale = traced(cxx, work, "c++17")
        expect(
            "a precompiled header of another standard has the mark x, and the compile reads the header",
            stale[:2] == (IncludeStep(1, Mark.INVALID_PRECOMPILED, "used.h.gch"), IncludeStep(1, Mark.HEADER, "used.h"))
            and include_trace.precompiled(stale) == (),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (planted_cases, compile_cases))
