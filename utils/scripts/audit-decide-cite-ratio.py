#!/usr/bin/env python3
"""Enforce the cite ratio of the decide:: catalog.

A decide:: procedure must earn its place in the catalog (CLAUDE.md XII).  A
citing site lands first, and the named predicate lands second.  So each
procedure needs at least MIN_CITES production cites within GRACE_DAYS of the
day its definition entered Decide.h.

The catalog and the cite count of each procedure come from the sibling audit,
utils/scripts/audit-pre-callsite-count.py, which reads both from the parse tree.
The day of each definition comes from the history of Decide.h, under each
path the file has had.  The audit parses every committed version with the
pinned tree-sitter kit and takes the first commit whose parse holds a
definition of that name inside a namespace named decide
(utils/scripts/decide_catalog.py).  A comment or a string that names a procedure is
not a definition node, so it does not date the procedure.  A procedure that
the working tree defines and no commit holds yet is dated today.

Buckets:
  GOOD       cites >= min_cites
  GRACE      cites <  min_cites, age <  grace_days  (informational)
  VIOLATION  cites <  min_cites, age >= grace_days  (a trim candidate)

A shallow clone stops the audit, because its cut history would date every
procedure to the cut.  Dates are calendar days in the local time zone of the
author, as git prints them.

Exit status:
  0  no violation, or --soft
  1  at least one violation
  2  a bad invocation, a tree the audit cannot read, or a failed self-test
  3  the tree-sitter kit is missing
"""

from __future__ import annotations

import argparse
import datetime
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import decide_catalog  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

SIBLING = SCRIPTS / "audit-pre-callsite-count.py"
MIN_CITES = 2
GRACE_DAYS = 180


class Unjudgeable(Exception):
    """The audit cannot give a true verdict: the history is cut or the tree does not parse."""


def git(root: Path, *args: str) -> str:
    """Run one git command in the repository at root and return its output."""
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, text=True).stdout


def _load_sibling():
    """The module of the sibling audit.  Its file name holds hyphens, so it loads by path."""
    spec = importlib.util.spec_from_file_location("audit_pre_callsite_count", SIBLING)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def cite_counts(root: Path) -> dict[str, int]:
    """The catalog of the tree at root, with the production cite count of each procedure."""
    sibling = _load_sibling()
    try:
        return sibling.scan(root).procedures
    except (sibling.AuditError, tsast.ParseError) as error:
        raise Unjudgeable(f"the cite count of {root} failed: {error}") from error


def definition_days(root: Path) -> dict[str, datetime.date]:
    """The day of the first commit whose Decide.h defines each procedure.

    Complexity: one `git show` for each commit and path, and one kit run over
    every version.  The parse is strict, so a version the kit cannot read
    stops the audit instead of dating a procedure late."""
    if git(root, "rev-parse", "--is-shallow-repository").strip() == "true":
        raise Unjudgeable(f"{root} is a shallow clone, so the history of Decide.h is cut and no procedure has a "
                          f"true date.  Fetch the whole history (git fetch --unshallow, or fetch-depth: 0 in CI).")
    log = git(root, "log", "--reverse", "--format=%H %ad", "--date=short", "--", *decide_catalog.DECIDE_HEADERS)
    days: dict[str, datetime.date] = {}
    with tempfile.TemporaryDirectory(prefix="decide-history-") as tmp_name:
        versions: list[Path] = []
        version_days: list[datetime.date] = []
        for entry in log.splitlines():
            commit, day = entry.split()
            for header in decide_catalog.DECIDE_HEADERS:
                shown = subprocess.run(["git", "-C", str(root), "show", f"{commit}:{header}"], capture_output=True)
                if shown.returncode != 0:
                    continue
                version = Path(tmp_name) / f"{len(versions):05d}.h"
                version.write_bytes(shown.stdout)
                versions.append(version)
                version_days.append(datetime.date.fromisoformat(day))
        try:
            for tree, day in zip(tsast.parse(versions, strict=True), version_days):
                for name in decide_catalog.procedures(tree):
                    days.setdefault(name, day)
        except tsast.ParseError as error:
            raise Unjudgeable(f"a committed version of Decide.h does not parse: {error}") from error
    return days


def audit(root: Path, min_cites: int, grace_days: int) -> dict:
    """The bucket of every catalog procedure, as the JSON object that --json prints."""
    counts = cite_counts(root)
    days = definition_days(root)
    today = datetime.date.today()
    procedures = []
    summary = {"good": 0, "grace": 0, "violation": 0}
    for name, cites in counts.items():
        # The sibling reads the catalog from the working tree.  A procedure
        # that no commit holds yet is new, so its age is zero days.
        intro = days.get(name, today)
        age = (today - intro).days
        if cites >= min_cites:
            bucket = "GOOD"
        else:
            bucket = "VIOLATION" if age >= grace_days else "GRACE"
        summary[bucket.lower()] += 1
        procedures.append({"name": name, "cites": cites, "age_days": age, "intro": intro.isoformat(),
                           "bucket": bucket})
    return {"min_cites": min_cites, "grace_days": grace_days, "summary": summary, "procedures": procedures}


def print_human(report: dict) -> None:
    """Print the buckets for a reader, violations first."""
    min_cites, grace_days = report["min_cites"], report["grace_days"]
    print("=== Crucible Decide.h cite-ratio audit ===")
    print(f"Threshold: at least {min_cites} production cites within {grace_days} days of the definition.\n")
    headings = {"VIOLATION": "VIOLATIONS: past the grace period, candidates for a trim",
                "GRACE": f"GRACE: under-cited, but inside the {grace_days}-day window (informational)",
                "GOOD": f"GOOD: at least {min_cites} cites"}
    for bucket in ("VIOLATION", "GRACE", "GOOD"):
        rows = [row for row in report["procedures"] if row["bucket"] == bucket]
        if not rows:
            continue
        print(f"-- {headings[bucket]} --")
        for row in rows:
            age = f", {row['age_days']} days old (defined {row['intro']})" if bucket != "GOOD" else ""
            print(f"  decide::{row['name']:<30} {row['cites']} cites{age}")
        print()
    summary = report["summary"]
    print(f"Summary: {summary['good']} good, {summary['grace']} grace, {summary['violation']} violation(s).")
    if summary["violation"]:
        print("\nFor each violation: cite it at a second real site, or record in Decide.h the work that will cite\n"
              "it, or delete the procedure.")


def run(argv: list[str]) -> int:
    """Parse the arguments, run the audit or the self-test, and return the exit status."""
    parser = argparse.ArgumentParser(description="Enforce the cite ratio of the decide:: catalog.")
    parser.add_argument("--json", action="store_true", help="print one JSON object")
    parser.add_argument("--soft", action="store_true", help="exit 0 even with a violation")
    parser.add_argument("--min-cites", type=int, default=MIN_CITES)
    parser.add_argument("--grace-days", type=int, default=GRACE_DAYS)
    parser.add_argument("--self-test", action="store_true", help="plant a violation and prove the audit catches it")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            return self_test()
        report = audit(args.root.resolve(), args.min_cites, args.grace_days)
    except tsast.KitMissing as missing:
        print(f"audit-decide-cite-ratio: SKIP, {missing}", file=sys.stderr)
        return 3
    except Unjudgeable as error:
        print(f"audit-decide-cite-ratio: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, separators=(",", ":")))
    else:
        print_human(report)
    return 1 if report["summary"]["violation"] and not args.soft else 0


# ── the self-test ──────────────────────────────────────────────────────

VIOLATION, GRACE, NEW, GOOD = "planted_violation", "planted_grace", "planted_new", ("planted_good", "planted_fine")


def _decide_header(defined: list[str]) -> str:
    """A Decide.h that defines the named procedures, and names the grace procedure in a comment only."""
    body = "".join(f"constexpr bool {name}(int) noexcept {{ return true; }}\n" for name in defined)
    return f"#pragma once\nnamespace foundation::decide {{\n// decide::{GRACE} is planned.\n{body}}}\n"


def self_test() -> int:
    """Plant one procedure in each bucket in a throwaway repository, and check every verdict.

    The planted violation is defined 400 days ago under the live Decide.h
    path.  The old pickaxe over the old paths never dated it, so it sat in
    grace for ever.  The planted grace procedure is named in a comment 400
    days ago and defined today, so a text search that dated the comment would
    move it into VIOLATION.  A third procedure is defined only in the working
    tree, and a depth-1 clone stops the audit."""
    tsast.kit_dir()
    failures: list[str] = []
    old_day = datetime.date.today() - datetime.timedelta(days=400)
    new_day = datetime.date.today()
    with tempfile.TemporaryDirectory(prefix="decide-ratio-self-test-") as tmp_name:
        root = Path(tmp_name)
        (root / "src").mkdir()
        (root / "src" / "anchor.cpp").write_text("int crucible_selftest_anchor = 0;\n")
        cites = [(VIOLATION, 1), (GRACE, 1), (NEW, 0)] + [(name, 2) for name in GOOD]
        body = "".join(f"    decide::{name}(n);\n" * count for name, count in cites)
        (root / "include").mkdir()
        (root / "include" / "cites.h").write_text(
            f"#pragma once\nnamespace crucible::planted {{\ninline void cite(int n) {{\n{body}}}\n}}\n")
        decide = root / decide_catalog.DECIDE_HEADERS[0]
        decide.parent.mkdir(parents=True)

        throwaway_repo.init(root)
        env = dict(os.environ, GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null",
                   GIT_AUTHOR_NAME="crucible-selftest", GIT_AUTHOR_EMAIL="crucible-selftest@invalid",
                   GIT_COMMITTER_NAME="crucible-selftest", GIT_COMMITTER_EMAIL="crucible-selftest@invalid")
        for day, defined in ((old_day, [VIOLATION, *GOOD]), (new_day, [VIOLATION, *GOOD, GRACE])):
            decide.write_text(_decide_header(defined))
            stamp = dict(env, GIT_AUTHOR_DATE=f"{day.isoformat()}T12:00:00",
                         GIT_COMMITTER_DATE=f"{day.isoformat()}T12:00:00")
            subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, env=stamp, capture_output=True)
            subprocess.run(["git", "-C", str(root), "commit", "-q", "-m", f"define through {defined[-1]}"],
                           check=True, env=stamp, capture_output=True)
        # A definition that no commit holds yet.
        decide.write_text(_decide_header([VIOLATION, *GOOD, GRACE, NEW]))

        def invoke(*extra: str, at: Path = root) -> subprocess.CompletedProcess[str]:
            return subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(at), *extra],
                                  capture_output=True, text=True)

        plain = invoke()
        if plain.returncode != 1 or f"decide::{VIOLATION}" not in plain.stdout:
            failures.append(f"the planted violation must fail the audit with 1 and be named, not "
                            f"{plain.returncode}: {plain.stderr.strip()}")
        soft = invoke("--json", "--soft")
        if soft.returncode != 0:
            failures.append(f"--json --soft must exit 0, not {soft.returncode}: {soft.stderr.strip()}")
        else:
            report = json.loads(soft.stdout)
            rows = {row["name"]: row for row in report["procedures"]}
            want_summary = {"good": len(GOOD), "grace": 2, "violation": 1}
            if report["summary"] != want_summary:
                failures.append(f"summary {report['summary']}, expected {want_summary}")
            for name, day, bucket in ((VIOLATION, old_day, "VIOLATION"), (GRACE, new_day, "GRACE"),
                                      (NEW, new_day, "GRACE")):
                got = (rows.get(name, {}).get("intro"), rows.get(name, {}).get("bucket"))
                if got != (day.isoformat(), bucket):
                    failures.append(f"decide::{name} reads {got}, expected ({day.isoformat()!r}, {bucket!r})")
        for knob in (("--soft",), ("--min-cites", "1"), ("--grace-days", "100000")):
            if invoke(*knob).returncode != 0:
                failures.append(f"{' '.join(knob)} must clear the planted violation")
        shallow = root / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", f"file://{root}", str(shallow)], check=True,
                       env=env, capture_output=True)
        cut = invoke(at=shallow)
        if cut.returncode != 2 or "shallow clone" not in cut.stderr:
            failures.append(f"a shallow clone must stop the audit with 2, not {cut.returncode}: {cut.stderr.strip()}")
    for line in failures:
        print(f"audit-decide-cite-ratio: SELF-TEST FAILED, {line}", file=sys.stderr)
    if failures:
        return 2
    print(f"audit-decide-cite-ratio: self-test passed, the violation is caught, a comment of {old_day.isoformat()} "
          "dates nothing, an uncommitted definition is new, and a shallow clone stops the audit", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(run(sys.argv[1:]))
