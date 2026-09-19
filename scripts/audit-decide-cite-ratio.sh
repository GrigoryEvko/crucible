#!/usr/bin/env bash
# audit-decide-cite-ratio.sh — CONTRACT-125 cite-ratio policy enforcer.
#
# Catalog discipline (CLAUDE.md §XII; feedback_decide_catalog.md):
#   * decide::* procedures grow bottom-up — a citing site lands FIRST,
#     the named predicate lands second.  A procedure with zero cites
#     six months after introduction is either undefended speculation
#     or a name that didn't fit reality; CONTRACT-126 trims it.
#   * Threshold: every procedure has ≥ 2 production cites within 6
#     months of introduction.  Single-cite procedures may be a
#     genuinely-bespoke pattern but more often signal a name that
#     never proliferated; the second cite proves the procedure earned
#     its place in the catalog.
#
# This script enforces that policy.  For each procedure in the
# audit-pre-callsite-count.sh catalog it asks two questions:
#   1. How many production cites? (via audit-pre-callsite-count --json)
#   2. How old is the procedure?  (git pickaxe `-S` on Decide.h finds
#      the first commit that introduced the procedure name.)
#
# A procedure is then bucketed:
#   GOOD       — cites ≥ MIN_CITES.  Earned its place.
#   GRACE      — cites < MIN_CITES, age < GRACE_DAYS.  Still warming up;
#                informational only, doesn't fail.
#   VIOLATION  — cites < MIN_CITES, age ≥ GRACE_DAYS.  Trim candidate
#                for CONTRACT-126; fails the audit unless --soft.
#
# Modes:
#   default          — human-readable bucketed report; exit 1 on any
#                      VIOLATION (CI gate).
#   --json           — single JSON object for machine ingestion.
#   --soft           — exit 0 even on VIOLATION; informational only.
#                      Useful during catalog growth phase before any
#                      procedure has aged past the grace window.
#   --min-cites N    — override threshold (default: 2).
#   --grace-days N   — override grace window (default: 180 ≈ 6 months).
#
# Mirrors scripts/audit-pre-callsite-count.sh idioms (set -euo pipefail,
# ripgrep + git only, no awk/sed) per the user's tool preferences.
#
# Exit status:
#   0  — no VIOLATIONs (or --soft mode)
#   1  — at least one VIOLATION
#   2  — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
audit-decide-cite-ratio.sh — enforce CONTRACT-125 cite-ratio policy.

Usage:
  audit-decide-cite-ratio.sh                     # human report; exit 1 on violation
  audit-decide-cite-ratio.sh --json              # JSON to stdout
  audit-decide-cite-ratio.sh --soft              # always exit 0 (informational)
  audit-decide-cite-ratio.sh --min-cites N       # override threshold (default 2)
  audit-decide-cite-ratio.sh --grace-days N      # override grace (default 180)
  audit-decide-cite-ratio.sh --self-test         # plant a violation, verify catch
  audit-decide-cite-ratio.sh -h | --help

Buckets:
  GOOD       cites ≥ min_cites
  GRACE      cites < min_cites, age < grace_days  (informational)
  VIOLATION  cites < min_cites, age ≥ grace_days  (CONTRACT-126 trim candidate)
USAGE
}

mode="human"
soft=0
min_cites=2
grace_days=180

while [[ $# -gt 0 ]]; do
    case "$1" in
        --json)        mode="json"; shift ;;
        --soft)        soft=1; shift ;;
        --min-cites)
            if [[ $# -lt 2 ]]; then usage; exit 2; fi
            min_cites="$2"; shift 2 ;;
        --grace-days)
            if [[ $# -lt 2 ]]; then usage; exit 2; fi
            grace_days="$2"; shift 2 ;;
        --self-test)
            # A policy gate that never plants a violation has never
            # demonstrated it fires.  Build a throwaway git repository whose
            # history and cite counts are chosen so the bucketing MUST land
            # one procedure in each bucket:
            #
            #   procedure #1  1 cite, Decide.h token introduced 400 days ago
            #                 -> cites < min_cites, age >= grace -> VIOLATION
            #   procedure #2  1 cite, token introduced today
            #                 -> cites < min_cites, age <  grace -> GRACE
            #   every other   2 cites                            -> GOOD
            #
            # This covers the WHOLE script, git-history join included: the
            # only thing separating procedure #1 from procedure #2 is the
            # commit date that `git log -S` resolves, so a broken pickaxe
            # collapses the VIOLATION into GRACE and fails the test.
            for selftest_tool in rg git python3; do
                if ! command -v "$selftest_tool" >/dev/null 2>&1; then
                    printf 'audit-decide-cite-ratio: %s is required\n' "$selftest_tool" >&2
                    exit 2
                fi
            done

            # The catalog is owned by the sibling audit; read it from there
            # so the fixture cannot desync when CONTRACT-126 trims a name.
            selftest_sibling="$root/scripts/audit-pre-callsite-count.sh"
            if [[ ! -x "$selftest_sibling" ]]; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — missing %s\n' \
                    "$selftest_sibling" >&2
                exit 2
            fi
            mapfile -t selftest_procs < <("$selftest_sibling" --json | python3 -c '
import json, sys
for name in json.load(sys.stdin)["decide_per_procedure"]:
    print(name)
')
            if [[ "${#selftest_procs[@]}" -lt 3 ]]; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — catalog too small (%d procedures).\n' \
                    "${#selftest_procs[@]}" >&2
                exit 2
            fi
            violation_proc="${selftest_procs[0]}"
            grace_proc="${selftest_procs[1]}"
            expected_goods=$(( ${#selftest_procs[@]} - 2 ))

            tmp_root="$(mktemp -d)"
            trap 'rm -rf "$tmp_root"' EXIT
            mkdir -p "$tmp_root/scripts" "$tmp_root/include/crucible/safety" "$tmp_root/src"

            # The sibling audit resolves its own root from BASH_SOURCE, so a
            # copy inside the temp root scans the temp tree.  The env pin
            # below makes that explicit and immune to an inherited override.
            cp "$selftest_sibling" "$tmp_root/scripts/audit-pre-callsite-count.sh"
            chmod +x "$tmp_root/scripts/audit-pre-callsite-count.sh"

            # Every pattern the sibling counts must match at least once: `rg`
            # exits 1 on no-match and `set -o pipefail` turns that into an
            # abort, so a fixture missing ONE form would read as a self-test
            # failure rather than the missing form it is.  The per-procedure
            # regex is line-anchored, so each cite needs its own line.
            selftest_fixture="$tmp_root/include/selftest_cites.h"
            cat >"$selftest_fixture" <<'FIXTURE'
#pragma once
// Synthetic cite fixture for --self-test.
namespace crucible::selftest {
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
FIXTURE
            for proc in "${selftest_procs[@]}"; do
                printf '    decide::%s(n);\n' "$proc" >>"$selftest_fixture"
                # The two under-cited procedures get exactly one cite each.
                if [[ "$proc" != "$violation_proc" && "$proc" != "$grace_proc" ]]; then
                    printf '    decide::%s(n);\n' "$proc" >>"$selftest_fixture"
                fi
            done
            cat >>"$selftest_fixture" <<'FIXTURE_TAIL'
}
}  // namespace crucible::selftest
FIXTURE_TAIL
            cat >"$tmp_root/src/selftest_anchor.cpp" <<'ANCHOR'
// Synthetic translation unit for --self-test.
int crucible_selftest_anchor = 0;
ANCHOR

            # Decide.h carries its tokens on comment-only lines: the sibling's
            # counter rejects lines opening with a comment, so this file
            # contributes ZERO cites, while `git log -S` still sees the bytes.
            selftest_decide_h="$tmp_root/include/crucible/safety/_Decide.h"
            {
                printf '#pragma once\n'
                printf '// Synthetic Decide.h for --self-test.\n'
                printf '//   decide::%s\n' "$violation_proc"
            } >"$selftest_decide_h"

            # Identity and dates come from the environment.  `git -c
            # user.email=...` is banned by CLAUDE.md; GIT_CONFIG_GLOBAL and
            # GIT_CONFIG_SYSTEM are pinned to /dev/null so a developer's own
            # gpgsign / hooksPath settings cannot break the fixture.
            export GIT_CONFIG_GLOBAL=/dev/null
            export GIT_CONFIG_SYSTEM=/dev/null
            export GIT_AUTHOR_NAME="crucible-selftest"
            export GIT_AUTHOR_EMAIL="crucible-selftest@invalid"
            export GIT_COMMITTER_NAME="crucible-selftest"
            export GIT_COMMITTER_EMAIL="crucible-selftest@invalid"

            old_day="$(date -d '400 days ago' +%Y-%m-%d)"
            new_day="$(date +%Y-%m-%d)"

            if ! git init -q -b selftest "$tmp_root" >/dev/null 2>&1; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — git init failed.\n' >&2
                exit 2
            fi
            git -C "$tmp_root" add -A >/dev/null 2>&1
            GIT_AUTHOR_DATE="${old_day}T12:00:00" \
            GIT_COMMITTER_DATE="${old_day}T12:00:00" \
                git -C "$tmp_root" commit -q -m "selftest: introduce ${violation_proc}" \
                >/dev/null 2>&1

            printf '//   decide::%s\n' "$grace_proc" >>"$selftest_decide_h"
            git -C "$tmp_root" add -A >/dev/null 2>&1
            GIT_AUTHOR_DATE="${new_day}T12:00:00" \
            GIT_COMMITTER_DATE="${new_day}T12:00:00" \
                git -C "$tmp_root" commit -q -m "selftest: introduce ${grace_proc}" \
                >/dev/null 2>&1

            selftest_env=(CRUCIBLE_DECIDE_CITE_TEST_ROOT="$tmp_root"
                          CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root")

            # ── Direction 1 — the planted VIOLATION MUST fail the gate ──
            selftest_out="$tmp_root/report.txt"
            if env "${selftest_env[@]}" bash "${BASH_SOURCE[0]}" \
               >"$selftest_out" 2>&1; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — planted VIOLATION not caught.\n' >&2
                printf '── report ───────────\n%s\n────────────────────\n' \
                    "$(cat "$selftest_out")" >&2
                exit 2
            fi
            if ! grep -qF "decide::${violation_proc}" "$selftest_out"; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — report never names decide::%s.\n' \
                    "$violation_proc" >&2
                printf '── report ───────────\n%s\n────────────────────\n' \
                    "$(cat "$selftest_out")" >&2
                exit 2
            fi

            # ── Bucketing + git-history join, asserted field by field ───
            selftest_json="$tmp_root/report.json"
            if ! env "${selftest_env[@]}" bash "${BASH_SOURCE[0]}" --json --soft \
                 >"$selftest_json" 2>/dev/null; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — --json --soft aborted.\n' >&2
                exit 2
            fi
            for expected in \
                "\"summary\":{\"good\":${expected_goods},\"grace\":1,\"violation\":1}" \
                "\"name\":\"${violation_proc}\",\"cites\":1," \
                "\"intro\":\"${old_day}\",\"bucket\":\"VIOLATION\"" \
                "\"name\":\"${grace_proc}\",\"cites\":1," \
                "\"intro\":\"${new_day}\",\"bucket\":\"GRACE\""; do
                if ! grep -qF "$expected" "$selftest_json"; then
                    printf 'audit-decide-cite-ratio: SELF-TEST FAILED — missing JSON fragment: %s\n' \
                        "$expected" >&2
                    printf '── json ─────────────\n%s\n────────────────────\n' \
                        "$(cat "$selftest_json")" >&2
                    exit 2
                fi
            done

            # ── Direction 2 — --soft downgrades the same tree to exit 0 ──
            if ! env "${selftest_env[@]}" bash "${BASH_SOURCE[0]}" --soft \
                 >/dev/null 2>&1; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — --soft did not suppress the VIOLATION.\n' >&2
                exit 2
            fi

            # ── Direction 3 — both policy knobs clear the same VIOLATION ─
            if ! env "${selftest_env[@]}" bash "${BASH_SOURCE[0]}" --min-cites 1 \
                 >/dev/null 2>&1; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — --min-cites 1 did not clear the VIOLATION.\n' >&2
                exit 2
            fi
            if ! env "${selftest_env[@]}" bash "${BASH_SOURCE[0]}" --grace-days 100000 \
                 >/dev/null 2>&1; then
                printf 'audit-decide-cite-ratio: SELF-TEST FAILED — --grace-days 100000 did not move the VIOLATION into GRACE.\n' >&2
                exit 2
            fi

            printf 'audit-decide-cite-ratio: self-test passed — VIOLATION caught (decide::%s, intro %s), GRACE honoured (decide::%s, intro %s), %d GOOD, --soft / --min-cites / --grace-days all live.\n' \
                "$violation_proc" "$old_day" "$grace_proc" "$new_day" "$expected_goods" >&2
            exit 0
            ;;
        -h|--help)     usage; exit 0 ;;
        *)
            printf 'audit-decide-cite-ratio: unknown argument: %s\n' "$1" >&2
            usage; exit 2 ;;
    esac
done

for tool in rg git python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'audit-decide-cite-ratio: %s is required\n' "$tool" >&2
        exit 2
    fi
done

# Sibling audit script is the SINGLE source of truth for the catalog
# enumeration AND per-procedure cite counts.  This script is policy
# only: it consumes the JSON, joins with git introduction dates, and
# applies the threshold.  When CONTRACT-126 trims a procedure it
# disappears from audit-pre-callsite-count.sh's catalog list and
# automatically falls out of this audit too.
# ── Scan-root override for --self-test recursion ─────────────────────
# The script's OWN location still resolves through BASH_SOURCE above;
# only the tree it audits — sibling audit, Decide.h, git history — moves.
scan_root="${CRUCIBLE_DECIDE_CITE_TEST_ROOT:-$root}"

audit_script="$scan_root/scripts/audit-pre-callsite-count.sh"
if [[ ! -x "$audit_script" ]]; then
    printf 'audit-decide-cite-ratio: missing %s\n' "$audit_script" >&2
    exit 2
fi

# The old catalog is marked superseded (_Decide.h) until Stage D deletes
# it; the pickaxe below names both spellings so the introduction dates
# survive the rename.
decide_h="$scan_root/include/crucible/safety/_Decide.h"
if [[ ! -f "$decide_h" ]]; then
    printf 'audit-decide-cite-ratio: missing %s\n' "$decide_h" >&2
    exit 2
fi

# ── Catalog + cite counts via the sibling audit ──────────────────────
# Single rg pass through the production tree; we extract the
# per-procedure subobject and feed it into this script's policy layer.
audit_json="$("$audit_script" --json)"

# Extract per-procedure name + count pairs.  Python json is the
# self-contained parser (already required by the test suite).  Output
# format: tab-separated `name<TAB>count` lines, one per procedure.
proc_count_pairs="$(printf '%s' "$audit_json" | python3 -c '
import json, sys
data = json.load(sys.stdin)
per = data["decide_per_procedure"]
for name, n in per.items():
    print(f"{name}\t{n}")
')"

# ── Per-procedure introduction date via git pickaxe ──────────────────
# `git log -S "TOKEN" --reverse` finds commits that CHANGED the count
# of lines containing TOKEN — the first such commit is the one that
# introduced the token (zero → nonzero).  We restrict to Decide.h to
# avoid catching cite-site introductions in unrelated files.
intro_date_for() {
    # `git log --reverse | head -n 1` would race against pipefail —
    # head closes the pipe after reading line 1, git's still writing,
    # SIGPIPE → 141, set -o pipefail propagates the failure.  Materialize
    # the full output and slice the first line in bash instead.
    local proc="$1" all
    all="$(git -C "$scan_root" log --reverse --format=%ad --date=short \
        -S "decide::${proc}" \
        -- "include/crucible/safety/_Decide.h" "include/crucible/safety/Decide.h" 2>/dev/null || true)"
    printf '%s' "${all%%$'\n'*}"
}

# Days between two YYYY-MM-DD dates, via `date +%s` (POSIX).  Empty
# input → 0 days (procedure introduction date couldn't be resolved;
# treated as "fresh" so it can't trigger a false VIOLATION).
days_since() {
    local d="$1"
    if [[ -z "$d" ]]; then printf '0'; return; fi
    local now then
    now="$(date +%s)"
    then="$(date -d "$d" +%s 2>/dev/null || printf '%s' "$now")"
    printf '%s' "$(( (now - then) / 86400 ))"
}

# ── Bucketing ────────────────────────────────────────────────────────
# We accumulate four parallel arrays (procedure name, cites, age days,
# bucket) then format per-mode.  The bucket assignment is the single
# load-bearing decision; everything else is presentation.

declare -a procs cites ages buckets dates
violations=0
graces=0
goods=0

while IFS=$'\t' read -r name n; do
    [[ -z "$name" ]] && continue
    intro="$(intro_date_for "$name")"
    age="$(days_since "$intro")"
    if [[ "$n" -ge "$min_cites" ]]; then
        bucket="GOOD"
        goods=$((goods + 1))
    elif [[ "$age" -ge "$grace_days" ]]; then
        bucket="VIOLATION"
        violations=$((violations + 1))
    else
        bucket="GRACE"
        graces=$((graces + 1))
    fi
    procs+=("$name")
    cites+=("$n")
    ages+=("$age")
    buckets+=("$bucket")
    dates+=("${intro:-unknown}")
done <<<"$proc_count_pairs"

# ── Output ──────────────────────────────────────────────────────────
print_human() {
    cat <<HEADER
=== Crucible Decide.h cite-ratio audit (CONTRACT-125) ===
Threshold: ≥ ${min_cites} production cites within ${grace_days} days of introduction.

──────────────────────────────────────────────────────────────────
HEADER

    if [[ "$violations" -gt 0 ]]; then
        printf '── VIOLATIONS — past grace period, candidate for CONTRACT-126 trim ──\n'
        for i in "${!procs[@]}"; do
            if [[ "${buckets[$i]}" == "VIOLATION" ]]; then
                printf '  decide::%-30s  %d cites,  %d days old (intro %s)\n' \
                    "${procs[$i]}" "${cites[$i]}" "${ages[$i]}" "${dates[$i]}"
            fi
        done
        printf '\n'
    fi

    if [[ "$graces" -gt 0 ]]; then
        printf '── GRACE — under-cited but within %d-day window (informational) ──\n' "$grace_days"
        for i in "${!procs[@]}"; do
            if [[ "${buckets[$i]}" == "GRACE" ]]; then
                printf '  decide::%-30s  %d cites,  %d days old (intro %s)\n' \
                    "${procs[$i]}" "${cites[$i]}" "${ages[$i]}" "${dates[$i]}"
            fi
        done
        printf '\n'
    fi

    printf '── GOOD — earned its place (cites ≥ %d) ──\n' "$min_cites"
    for i in "${!procs[@]}"; do
        if [[ "${buckets[$i]}" == "GOOD" ]]; then
            printf '  decide::%-30s  %d cites\n' \
                "${procs[$i]}" "${cites[$i]}"
        fi
    done

    cat <<FOOTER

──────────────────────────────────────────────────────────────────
Summary: ${goods} good, ${graces} grace, ${violations} violation(s).

Per CLAUDE.md §XII + feedback_decide_catalog.md: catalog grows
bottom-up — a citing site lands first, the named predicate second.
A procedure with zero cites past the grace window is either
undefended speculation or a name that didn't fit reality.

Remediation for VIOLATIONs (in order of preference):
  (1) Cite it.  Find the second site that wants this name and
      migrate it under CONTRACT-100..127 batch discipline.
  (2) Defend it.  Add a "reserved for <work>, blocked on <gate>"
      comment in Decide.h above the procedure documenting WHY no
      production cite has materialized yet.
  (3) Trim it.  Delete the procedure under CONTRACT-126.  The CI
      grep guard (scripts/check-refined-pre-subsumption.sh) will
      catch any cite that survives the trim.
FOOTER
}

print_json() {
    printf '{'
    printf '"min_cites":%s,' "$min_cites"
    printf '"grace_days":%s,' "$grace_days"
    printf '"summary":{"good":%s,"grace":%s,"violation":%s},' \
        "$goods" "$graces" "$violations"
    printf '"procedures":['
    local first=1
    for i in "${!procs[@]}"; do
        if [[ $first -eq 0 ]]; then printf ','; fi
        first=0
        printf '{"name":"%s","cites":%s,"age_days":%s,"intro":"%s","bucket":"%s"}' \
            "${procs[$i]}" "${cites[$i]}" "${ages[$i]}" \
            "${dates[$i]}" "${buckets[$i]}"
    done
    printf ']'
    printf '}\n'
}

case "$mode" in
    human) print_human ;;
    json)  print_json  ;;
esac

if [[ "$violations" -gt 0 && "$soft" -eq 0 ]]; then
    exit 1
fi
exit 0
