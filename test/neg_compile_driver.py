#!/usr/bin/env python3
"""Run one negative-compile fixture without invoking Ninja.

CTest can launch many negative fixtures concurrently.  Calling
`cmake --build` from each fixture makes those CTest jobs nested writers
to the same Ninja log and generated-output graph.  This driver instead
replays the target's compile command from compile_commands.json and
redirects the object output into a per-fixture scratch directory.

Matching discipline
-------------------
GCC quotes the offending source line back in its caret display.  A
required-diagnostic regex matched against the raw compiler output can
therefore be satisfied by the FIXTURE'S OWN TEXT rather than by the
compiler rejecting anything: give the fixture a valid argument (so the
rejection the fixture exists to prove no longer fires) plus any
unrelated error on the same line, and a regex naming the probed symbol
still matches -- from the echo.  The fixture then proves nothing while
staying green.

`strip_source_echo` removes the caret display before matching, so a
regex can only be satisfied by text the compiler ITSELF produced:
message lines, `note:` lines, the `In file included from` chain, the
GCC 16 bullet sub-diagnostics, and the instantiation-context headers.
Those legitimately quote type names, concept names and template
arguments -- that is exactly the evidence a fixture should match on.
The caret display is still printed verbatim for the human reading a
failure; only the MATCHED text is stripped.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

# ── Diagnostic-text normalisation ──────────────────────────────────
#
# The replayed command carries `-fdiagnostics-color=always` (see the
# top-level CMakeLists), so every highlighted token is wrapped in SGR
# escapes and each line carries an EL (`ESC [ K`) sequence.  A regex
# such as `concept GradedWrapper` would not match `concept
# <SGR>GradedWrapper<SGR>`, so escapes are removed before matching.
# Removing them can only make matching see MORE of what the compiler
# actually said; it never invents text.
_ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")

# A GCC caret-display line.  With `-fdiagnostics-show-line-numbers`
# (default since GCC 9, and appended below so the shape is guaranteed
# rather than assumed) every such line carries a left gutter that ends
# in `|`:
#
#     `   19 |     static_assert(GradedWrapper<int>);`   source echo
#     `      |                   ~~~~~^~~~~~~~~~~~~`     caret / range
#     `  +++ |+    #include <foo>`                       fix-it insert
#     `      |                   replacement`            fix-it replace
#
# The gutter is never colourised, and no GCC MESSAGE line begins with
# an optional line number followed by `|` -- messages begin with a
# path, with `In file included from`, with `In function`, with the
# GCC 16 bullet, or with `from`/`required from` continuations.
#
# One known over-reach: `-fanalyzer`'s inline-events path art also draws
# a `|` gutter, so an analyzer path would be removed along with the
# source it quotes.  Neg-compile fixtures assert compile-time rejections
# and never gate on analyzer path text, and `-fanalyzer` is confined to
# its own preset, so nothing in the corpus depends on it.
_GUTTER = re.compile(r"^[ \t]*(?:\d+|\+\+\+)?[ \t]*\|")

# GCC's line-elision marker between two non-adjacent quoted lines.
# Carries no diagnostic text of its own.
_ELISION = re.compile(r"^[ \t]*\.{3,}[ \t]*$")

# A primary diagnostic header.  GCC 16 renders sub-reasons as indented
# `•` bullets belonging to the preceding header, so counting headers
# counts distinct diagnostics rather than lines.  The `:line:col` part is
# optional inside the location, which is how `cc1plus: error: ...` and
# `<built-in>: error: ...` are counted; both carry no fixture line and so
# contribute to the total without being attributed to the fixture.
_ERROR_HEADER = re.compile(
    r"^(?:(?P<file>[^\s:][^:]*):(?:(?P<line>\d+):(?:\d+:)?)?[ \t]*)?"
    r"(?:error|fatal error):[ \t]",
)


def strip_source_echo(text: str) -> str:
    """Return `text` with ANSI escapes and GCC's caret display removed.

    Only the caret display is discarded -- the quoted source line, its
    caret/range underline, and fix-it hint lines.  Every line the
    compiler wrote as prose survives, including the ones that quote a
    type name, a concept name or a template argument list.
    """
    kept: list[str] = []
    for line in text.splitlines():
        plain = _ANSI.sub("", line)
        if _GUTTER.match(plain) or _ELISION.match(plain):
            continue
        kept.append(plain)
    return "\n".join(kept)


def summarise_errors(stripped: str, source: Path) -> tuple[int, list[int]]:
    """Count primary error diagnostics and locate the fixture-local ones.

    Returns the total number of error headers and the sorted distinct
    `line` numbers at which the FIXTURE'S OWN source produced one.  A
    fixture that rejects for exactly one reason reports a single
    fixture-local line; more than one means the file triggers more than
    one independent rejection, and the registered regex may be matching
    a rejection other than the one the fixture documents.
    """
    total = 0
    own: set[int] = set()
    resolved = str(source)
    for line in stripped.splitlines():
        match = _ERROR_HEADER.match(line)
        if match is None:
            continue
        total += 1
        where = match.group("file")
        lineno = match.group("line")
        if where is None or lineno is None:
            continue
        try:
            if Path(where).resolve() == source:
                own.add(int(lineno))
        except OSError:  # pragma: no cover - defensive
            if where == resolved:
                own.add(int(lineno))
    return total, sorted(own)


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

    # `strip_source_echo` recognises the caret display by its left
    # gutter, which exists only while line numbers are shown.  That is
    # GCC's default, but a preset (or a future default change) could
    # turn it off and silently reopen the echo hole, so pin it here.
    # GCC honours the LAST occurrence.
    argv.append("-fdiagnostics-show-line-numbers")

    proc = subprocess.run(
        argv,
        cwd=directory,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    combined = proc.stdout + proc.stderr
    # The HUMAN sees the compiler's output verbatim, carets and all.
    sys.stdout.write(combined)
    # Matching sees only what the compiler said, never what the fixture
    # wrote.  See the module docstring.
    matchable = strip_source_echo(combined)

    if proc.returncode == 0:
        print(
            f"negative fixture {fixture_name} compiled successfully",
            file=sys.stderr,
        )
        return 1

    # Independence report.  A fixture documents ONE rejection; if its
    # own source produces error headers at several distinct lines it is
    # rejecting for several reasons at once, and the registered regex
    # may be witnessing a different one than the file claims.  Reported
    # unconditionally so the count shows up in CI logs; promoted to a
    # failure under CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 so the discipline
    # can be swept without editing 2,000+ registrations.
    total_errors, own_lines = summarise_errors(matchable, source)
    strict = os.environ.get("CRUCIBLE_NEG_STRICT_SINGLE_ERROR") == "1"
    not_independent = len(own_lines) > 1
    if not_independent:
        detail = ", ".join(str(n) for n in own_lines)
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local errors at {len(own_lines)} distinct lines "
            f"({detail}) — the fixture rejects for more than one reason",
            file=sys.stderr,
        )
    else:
        print(
            f"neg-compile {fixture_name}: {total_errors} error diagnostic(s), "
            f"fixture-local error lines {own_lines}",
            file=sys.stderr,
        )

    # EVERY required regex must appear.  For a single-regex fixture this
    # is identical to the original `re.search` gate.  For a multi-cell
    # fixture, a missing regex means one cell stopped failing (discipline
    # slipped on that cell) OR its diagnostic text drifted — either way
    # the merged TU no longer proves what it claims, and we fail loudly
    # naming the specific cell that went silent.
    missing = [
        pattern
        for pattern in expected_regexes
        if not re.search(pattern, matchable, flags=re.MULTILINE)
    ]
    if missing:
        for pattern in missing:
            print(
                f"expected diagnostic not found for {fixture_name}: "
                f"{pattern}",
                file=sys.stderr,
            )
            # The most common cause, by a wide margin, is a regex that
            # was only ever satisfied by GCC quoting the fixture's own
            # source back at it.  Say so instead of leaving the author
            # to rediscover it.
            if re.search(pattern, _ANSI.sub("", combined), flags=re.MULTILINE):
                print(
                    f"  note: {fixture_name} matched this pattern ONLY in "
                    f"the quoted source line, not in any diagnostic the "
                    f"compiler produced — the fixture is asserting its own "
                    f"text, not a rejection",
                    file=sys.stderr,
                )
        return 1
    # The regex verdict is the more informative one, so it is reported
    # first; the strict-mode verdict is applied only after it.
    return 1 if (strict and not_independent) else 0


if __name__ == "__main__":
    raise SystemExit(main())
