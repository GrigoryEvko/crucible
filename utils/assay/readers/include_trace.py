"""include_trace — read the include trace that GCC writes with the option -H.

THE TRACE
    With -H, GCC writes one line to the standard error for each header that a
    compile reads, in the sequence of the reads.  The depth D of a header is
    its level in the include stack.  Its line holds D - 1 dots, a mark, a space
    and the path of the header.  The mark is "." for a header, "!" for a
    precompiled header that the compile uses, and "x" for a precompiled header
    that the compile cannot use.  After a precompiled header that the compile
    uses, GCC writes the main file at the depth 0: one space and the path.

    The standard error also holds the diagnostics of the compile and the list
    after "Multiple include guards may be useful for:".  A line of the trace
    starts with a dot, or with a mark and a space.  The line of the main file
    starts with one space and a character that is not a space.  A diagnostic
    line starts with a path, with a word, or with two spaces or more.

WHY THE TRACE
    A dependency file of the compile does not name a precompiled header that
    the compile uses (measured with GCC 16.2.1).  A test of a precompiled
    header reads the trace for that reason.

`guard selftest tests.readers.test_include_trace` does the test of the module.
"""

import dataclasses
import re
from enum import StrEnum

# The option that makes GCC write the trace.
TRACE_FLAG = "-H"
# One line of the trace: the dots, the mark and the path.  The main file after a precompiled header has no dot
# and no mark.
_STEP = re.compile(r"(?P<dots>\.*)(?P<mark>[.!x]) (?P<path>\S.*)|(?P<main> )(?P<main_path>\S.*)")


class Mark(StrEnum):
    """The mark of a line of the trace: a header, a precompiled header in use, or one that the compile cannot use."""

    HEADER = "."
    PRECOMPILED = "!"
    INVALID_PRECOMPILED = "x"


@dataclasses.dataclass(frozen=True, slots=True)
class IncludeStep:
    """One line of the trace.  The main file after a precompiled header has the depth 0 and the mark HEADER."""

    depth: int
    mark: Mark
    path: str


def read(stderr: str) -> tuple[IncludeStep, ...]:
    """Return each line of the include trace in the standard error of a compile, in sequence.

    The function ignores each line that is not a line of the trace (THE
    TRACE).  Complexity: linear in the length of the text.
    """
    steps: list[IncludeStep] = []
    for line in stderr.splitlines():
        match = _STEP.fullmatch(line)
        if match is None:
            continue
        if match.group("main") is not None:
            steps.append(IncludeStep(0, Mark.HEADER, match.group("main_path")))
            continue
        steps.append(IncludeStep(len(match.group("dots")) + 1, Mark(match.group("mark")), match.group("path")))
    return tuple(steps)


def precompiled(steps: tuple[IncludeStep, ...]) -> tuple[str, ...]:
    """Return the path of each precompiled header that the compile uses, in sequence."""
    return tuple(step.path for step in steps if step.mark is Mark.PRECOMPILED)
