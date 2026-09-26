#!/usr/bin/env python3
"""Enforce the cite ratio of the decide:: catalog.

A decide:: procedure must earn its place in the catalog (CLAUDE.md XII).  A
citing site lands first, and the named predicate lands second.  So each
procedure needs at least MIN_CITES production cites within GRACE_DAYS of the
day its definition entered Decide.h.

The catalog and the cite count of each procedure come from the sibling audit,
scripts/audit-pre-callsite-count.sh --json.  The day of each definition comes
from the history of Decide.h, under each path the file has had.  The audit
parses every committed version with the pinned tree-sitter kit and takes the
first commit whose parse holds a definition of that name inside a namespace
named decide (scripts/decide_catalog.py).  A comment or a string that names a
procedure is not a definition node, so it does not date the procedure.

Buckets:
  GOOD       cites >= min_cites
  GRACE      cites <  min_cites, age <  grace_days  (informational)
  VIOLATION  cites <  min_cites, age >= grace_days  (a trim candidate)

A procedure with fewer than min_cites cites and no definition in the history
of Decide.h stops the audit, because the audit cannot judge it.  Dates are
calendar days in the local time zone of the author, as git prints them.

Exit status:
  0  no violation, or --soft
  1  at least one violation
  2  a bad invocation, a procedure the audit cannot judge, or a failed self-test
  3  the tree-sitter kit is missing
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import decide_catalog  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

REPO_ROOT = SCRIPTS.parent
SIBLING = "scripts/audit-pre-callsite-count.sh"
MIN_CITES = 2
GRACE_DAYS = 180


class Unjudgeable(Exception):
    """A procedure has too few cites and no definition in the history of Decide.h."""


def git(root: Path, *args: str) -> str:
    """Run one git command in the repository at root and return its output."""
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, text=True).stdout


def cite_counts(root: Path) -> dict[str, int]:
    """The catalog of the sibling audit, with the production cite count of each procedure."""
    env = dict(os.environ, CRUCIBLE_PRE_CALLSITE_TEST_ROOT=str(root))
    result = subprocess.run(["bash", str(root / SIBLING), "--json"], capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise SystemExit(f"audit-decide-cite-ratio: {SIBLING} --json failed:\n{result.stderr}")
    return json.loads(result.stdout)["decide_per_procedure"]


def definition_days(root: Path) -> dict[str, datetime.date]:
    """The day of the first commit whose Decide.h defines each procedure.

    Complexity: one `git show` for each commit and path, and one kit run over
    every version.  The parse is strict, so a version the kit cannot read
    stops the audit instead of dating a procedure late.  A shallow clone
    stops the audit too, because its cut history would date every procedure
    to the cut and so put every one of them in grace."""
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
        for tree, day in zip(tsast.parse(versions, strict=True), version_days):
            for name in decide_catalog.procedures(tree):
                days.setdefault(name, day)
    return days


def audit(root: Path, min_cites: int, grace_days: int) -> dict:
    """The bucket of every catalog procedure, as the JSON object that --json prints."""
    counts = cite_counts(root)
    days = definition_days(root)
    today = datetime.date.today()
    procedures = []
    summary = {"good": 0, "grace": 0, "violation": 0}
    for name, cites in counts.items():
        intro = days.get(name)
        age = (today - intro).days if intro is not None else 0
        if cites >= min_cites:
            bucket = "GOOD"
        elif intro is None:
            raise Unjudgeable(f"decide::{name} has {cites} cites and no definition in the history of "
                              f"{', '.join(decide_catalog.DECIDE_HEADERS)}, so its age is unknown.  Define it in "
                              f"{decide_catalog.DECIDE_HEADERS[0]}, or remove it from the catalog of {SIBLING}.")
        else:
            bucket = "VIOLATION" if age >= grace_days else "GRACE"
        summary[bucket.lower()] += 1
        procedures.append({"name": name, "cites": cites, "age_days": age,
                           "intro": intro.isoformat() if intro is not None else "unknown", "bucket": bucket})
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
    if args.self_test:
        return self_test()
    try:
        report = audit(args.root.resolve(), args.min_cites, args.grace_days)
    except tsast.KitMissing as missing:
        print(f"audit-decide-cite-ratio: SKIP, {missing}", file=sys.stderr)
        return 3
    except (Unjudgeable, tsast.ParseError) as error:
        print(f"audit-decide-cite-ratio: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, separators=(",", ":")))
    else:
        print_human(report)
    return 1 if report["summary"]["violation"] and not args.soft else 0


# ── the self-test ──────────────────────────────────────────────────────

# Every form that the sibling audit counts appears once, so the planted tree
# is a realistic one.  The namespace name holds no test word, because the
# sibling does not count the body of a test namespace.
CITE_FIXTURE_HEAD = """#pragma once
namespace crucible::planted {
inline void planted_macro_forms(int n) {
    CRUCIBLE_PRE(n > 0);
    CRUCIBLE_PRE_FAST(n > 0);
    CRUCIBLE_PRE_MSG(n > 0, "synthetic");
    CRUCIBLE_POST(r, r > 0);
    CRUCIBLE_POST_FAST(r, r > 0);
    CRUCIBLE_POST_MSG(r, r > 0, "synthetic");
    contract_assert(n > 0);
}
inline int planted_p2900_forms(int const n)
    pre (n > 0)
    post (r: r > 0)
{ return n; }
inline void planted_decide_cites(int n) {
"""
CITE_FIXTURE_TAIL = "}\n}  // namespace crucible::planted\n"


def _decide_header(defined: list[str], mentioned: str) -> str:
    """A Decide.h that defines the named procedures and names one more in a comment only."""
    body = "".join(f"constexpr bool {name}(int) noexcept {{ return true; }}\n" for name in defined)
    return f"#pragma once\nnamespace foundation::decide {{\n// decide::{mentioned} is planned.\n{body}}}\n"


def self_test() -> int:
    """Plant one procedure in each bucket in a throwaway repository, and check every verdict.

    The planted VIOLATION is defined in the live Decide.h path 400 days ago,
    which the old pickaxe over the old paths never saw.  The planted GRACE
    procedure is named in a comment 400 days ago and defined today, so a
    text search that dates the comment would move it into VIOLATION."""
    failures: list[str] = []
    try:
        tsast.kit_dir()
    except tsast.KitMissing as missing:
        print(f"audit-decide-cite-ratio: SKIP, {missing}", file=sys.stderr)
        return 3
    try:
        procedures = list(cite_counts(REPO_ROOT))
    except SystemExit as error:
        print(error, file=sys.stderr)
        return 2
    if len(procedures) < 3:
        print(f"audit-decide-cite-ratio: SELF-TEST FAILED, the catalog has only {len(procedures)} procedures",
              file=sys.stderr)
        return 2
    violation, grace = procedures[0], procedures[1]
    old_day = datetime.date.today() - datetime.timedelta(days=400)
    new_day = datetime.date.today()
    with tempfile.TemporaryDirectory(prefix="decide-ratio-self-test-") as tmp_name:
        root = Path(tmp_name)
        (root / "scripts").mkdir()
        shutil.copy2(REPO_ROOT / SIBLING, root / SIBLING)
        (root / "src").mkdir()
        (root / "src" / "selftest_anchor.cpp").write_text("int crucible_selftest_anchor = 0;\n")
        cites = "".join(f"    decide::{name}(n);\n" * (1 if name in (violation, grace) else 2) for name in procedures)
        (root / "include").mkdir()
        (root / "include" / "selftest_cites.h").write_text(CITE_FIXTURE_HEAD + cites + CITE_FIXTURE_TAIL)
        decide = root / decide_catalog.DECIDE_HEADERS[0]
        decide.parent.mkdir(parents=True)

        throwaway_repo.init(root)
        env = dict(os.environ, GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null",
                   GIT_AUTHOR_NAME="crucible-selftest", GIT_AUTHOR_EMAIL="crucible-selftest@invalid",
                   GIT_COMMITTER_NAME="crucible-selftest", GIT_COMMITTER_EMAIL="crucible-selftest@invalid")
        for day, defined in ((old_day, [violation]), (new_day, [violation, grace])):
            decide.write_text(_decide_header(defined, grace))
            stamp = dict(env, GIT_AUTHOR_DATE=f"{day.isoformat()}T12:00:00",
                         GIT_COMMITTER_DATE=f"{day.isoformat()}T12:00:00")
            subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, env=stamp, capture_output=True)
            subprocess.run(["git", "-C", str(root), "commit", "-q", "-m", f"define through {defined[-1]}"],
                           check=True, env=stamp, capture_output=True)

        def invoke(*extra: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(root), *extra],
                                  capture_output=True, text=True)

        plain = invoke()
        if plain.returncode != 1:
            failures.append(f"the planted violation must fail the audit with 1, not {plain.returncode}")
        if f"decide::{violation}" not in plain.stdout:
            failures.append(f"the report never names decide::{violation}")
        soft = invoke("--json", "--soft")
        if soft.returncode != 0:
            failures.append(f"--json --soft must exit 0, not {soft.returncode}: {soft.stderr.strip()}")
        else:
            report = json.loads(soft.stdout)
            rows = {row["name"]: row for row in report["procedures"]}
            want_summary = {"good": len(procedures) - 2, "grace": 1, "violation": 1}
            if report["summary"] != want_summary:
                failures.append(f"summary {report['summary']}, expected {want_summary}")
            for name, day, bucket in ((violation, old_day, "VIOLATION"), (grace, new_day, "GRACE")):
                got = (rows.get(name, {}).get("intro"), rows.get(name, {}).get("bucket"))
                if got != (day.isoformat(), bucket):
                    failures.append(f"decide::{name} reads {got}, expected ({day.isoformat()!r}, {bucket!r})")
        for knob in (("--soft",), ("--min-cites", "1"), ("--grace-days", "100000")):
            if invoke(*knob).returncode != 0:
                failures.append(f"{' '.join(knob)} must clear the planted violation")
        # With three cites needed, every GOOD procedure of the planted tree
        # becomes under-cited, and none has a definition to date it.
        unjudged = invoke("--min-cites", "3")
        if unjudged.returncode != 2 or "no definition in the history" not in unjudged.stderr:
            failures.append(f"an under-cited procedure with no definition must stop the audit with 2, not "
                            f"{unjudged.returncode}: {unjudged.stderr.strip()}")
        # A clone of depth 1 dates every procedure to its one commit, today.
        shallow = root / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", f"file://{root}", str(shallow)], check=True,
                       env=env, capture_output=True)
        cut = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--root", str(shallow)],
                             capture_output=True, text=True)
        if cut.returncode != 2 or "shallow clone" not in cut.stderr:
            failures.append(f"a shallow clone must stop the audit with 2, not {cut.returncode}: {cut.stderr.strip()}")
    for line in failures:
        print(f"audit-decide-cite-ratio: SELF-TEST FAILED, {line}", file=sys.stderr)
    if failures:
        return 2
    print(f"audit-decide-cite-ratio: self-test passed, decide::{violation} is a violation, decide::{grace} is in "
          f"grace although a comment named it {old_day.isoformat()}, and an undated procedure and a shallow clone "
          "each stop the audit",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(run(sys.argv[1:]))
    except tsast.KitMissing as missing:
        print(f"audit-decide-cite-ratio: SKIP, {missing}", file=sys.stderr)
        raise SystemExit(3)
