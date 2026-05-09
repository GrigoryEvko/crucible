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
audit_script="$root/scripts/audit-pre-callsite-count.sh"
if [[ ! -x "$audit_script" ]]; then
    printf 'audit-decide-cite-ratio: missing %s\n' "$audit_script" >&2
    exit 2
fi

decide_h="$root/include/crucible/safety/Decide.h"
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
    all="$(git -C "$root" log --reverse --format=%ad --date=short \
        -S "decide::${proc}" \
        -- "include/crucible/safety/Decide.h" 2>/dev/null || true)"
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
