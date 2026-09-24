#!/usr/bin/env python3
"""Check that every C++ file in the tree parses, and that the roster is exact.

Every AST gate reads a tree-sitter parse.  A file that does not parse is a hole
in every one of them at once, and a hole that nothing reports is the worst kind.
So this gate owns the one question they all depend on.

It fails in three directions, so the roster cannot rot:

  * A file parses with an error and `tsast.UNPARSEABLE` does not list it.  Either
    the grammar needs the construct, or the file is not C++.  Name it and say
    which.
  * A file the roster lists now parses clean.  The entry is stale, and a stale
    entry hides the next real error in that file.  Delete the entry.
  * The roster names a path that no longer exists.  The file moved or went away,
    and the entry now excuses nothing.

The roster is keyed by path, never by line, so an edit above a site cannot drift
the key.

EXIT CODES
    0  every file parses, or the roster admits it for a stated reason
    2  a finding in any of the three directions above
    3  the pinned tree-sitter kit is not installed
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

# The roots an AST gate scans.  A directory absent from a checkout is skipped.
ROOTS = ("include", "src", "test", "bench", "vessel", "tools", "examples", "fuzz")


def verdict(
    errored: set[str],
    roster: dict[str, str],
    existing: set[str],
) -> list[str]:
    """Compare the parse result against the roster and return every finding.

    The comparison is a pure function of three sets, so the self-test can plant
    inputs and prove that each direction fires.

    Args:
        errored: Repo-relative paths whose parse reported an error
        roster: The path-keyed roster of admitted files, each with its reason
        existing: Repo-relative paths that exist in the checkout

    Returns:
        One message for each finding, in sorted order for a stable report
    """
    findings: list[str] = []
    for path in sorted(errored - set(roster)):
        findings.append(
            f"PARSE unadmitted: {path} — the parse reports an error and the roster "
            f"does not list it. Add the construct to the grammar, or add a roster "
            f"entry in scripts/tsast.py that names the reason."
        )
    for path in sorted(set(roster) - errored):
        if path not in existing:
            findings.append(
                f"PARSE roster dangling: {path} — the roster lists a path that does "
                f"not exist. Remove the entry from tsast.UNPARSEABLE."
            )
        else:
            findings.append(
                f"PARSE roster stale: {path} — the file parses clean now, so the "
                f"entry excuses nothing and hides the next real error in it. "
                f"Remove the entry from tsast.UNPARSEABLE."
            )
    return findings


def scan() -> int:
    """Parse every C++ file under ROOTS and report the findings.

    Returns:
        0 when the roster is exact, 2 on any finding, 3 when the kit is absent
    """
    try:
        files = tsast.cpp_files(*ROOTS)
    except tsast.KitMissing as exc:
        print(f"check-parse-clean: {exc}", file=sys.stderr)
        return 3
    try:
        errored = {
            str(tree.path)
            for tree in tsast.parse(files, strict=False)
            if tree.diagnostic is not None
        }
    except tsast.KitMissing as exc:
        print(f"check-parse-clean: {exc}", file=sys.stderr)
        return 3

    existing = {str(p) for p in files}
    findings = verdict(errored, tsast.UNPARSEABLE, existing)
    for message in findings:
        print(message, file=sys.stderr)
    if findings:
        print(
            f"\ncheck-parse-clean: {len(findings)} finding(s) across "
            f"{len(files)} files. The AST gates read this parse, so a hole here "
            f"is a hole in all of them.",
            file=sys.stderr,
        )
        return 2
    print(
        f"check-parse-clean: clean — {len(files)} files parse, "
        f"{len(tsast.UNPARSEABLE)} admitted by the roster, none stale.",
        file=sys.stderr,
    )
    return 0


def self_test() -> int:
    """Exercise the verdict in every direction, positive and negative.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        """Record one case result and print it.

        Args:
            name: What the case asserts
            ok: Whether it held
        """
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    print("check-parse-clean --self-test")

    # Positive control: an exact roster yields nothing.
    check(
        "an exact roster reports no finding",
        verdict({"a.cpp"}, {"a.cpp": "reason"}, {"a.cpp"}) == [],
    )
    # Positive control: a clean tree with an empty roster yields nothing.
    check("a clean tree with an empty roster reports nothing", verdict(set(), {}, set()) == [])

    # Negative control: an error the roster does not admit must be reported.
    out = verdict({"b.cpp"}, {}, {"b.cpp"})
    check(
        "an unadmitted parse error is reported",
        len(out) == 1 and out[0].startswith("PARSE unadmitted: b.cpp"),
    )
    # Negative control: a roster entry whose file parses clean must be reported.
    out = verdict(set(), {"c.cpp": "reason"}, {"c.cpp"})
    check(
        "a stale roster entry is reported",
        len(out) == 1 and out[0].startswith("PARSE roster stale: c.cpp"),
    )
    # Negative control: a roster entry naming an absent file must be reported.
    out = verdict(set(), {"gone.cpp": "reason"}, set())
    check(
        "a dangling roster entry is reported",
        len(out) == 1 and out[0].startswith("PARSE roster dangling: gone.cpp"),
    )
    # Negative control: the three directions are reported together, not one at a time.
    out = verdict({"b.cpp"}, {"c.cpp": "r", "gone.cpp": "r"}, {"b.cpp", "c.cpp"})
    check("all three directions are reported at once", len(out) == 3)

    # Positive control: the roster in the tree names a reason for every entry.
    check(
        "every roster entry carries a non-empty reason",
        all(reason.strip() for reason in tsast.UNPARSEABLE.values()),
    )

    if failures:
        print(f"check-parse-clean --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print("check-parse-clean --self-test: 7 cases pass, 4 of them negative controls.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        sys.exit(self_test())
    sys.exit(scan())
