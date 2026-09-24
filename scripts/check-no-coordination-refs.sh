#!/usr/bin/env bash
# check-no-coordination-refs.sh — no coordination breadcrumbs in the new tree.
#
# A comment, a docstring, a diagnostic, a script or an allowlist states
# what the code does.  It does not name the task, the stage or the tracker
# entry that produced the code.  Such a name means nothing to a reader
# who has no access to the tracker, and it becomes incorrect when the
# plan changes.  This guard makes the rule mechanical for the trees in
# SCAN_DIRS.
#
# The guard fails on three families of text:
#
#   1. A task reference: `#` and two to four digits (#147, task #193,
#      #1519).  The pattern ignores a single digit, because prose numbers
#      the items of a list as `#1` and `#2`.  It also ignores five or more
#      digits, because a six-digit color such as #374151 is not a task.
#   2. A tracker tag: the FIXY-U-, FIXY-V-, FIXY-FOUND-, FOUND-, GAPS-,
#      SEPLOG-, CONTRACT-, METX-, WRAP- and BC- families, the lowercase
#      fixy-A5-016 and fix-18 forms, the short U-002 and V-073 forms, and
#      the CR-05 audit form.
#   3. A stage label: a dotted stage such as A13.2 or A10.x, the phrase
#      "Stage D" or "Stage B4", "at A9", "Phase F6", and "Agent 11".
#
# Three kinds of text look similar and pass by construction:
#
#   - A preprocessor directive.  `#include` and `#pragma` put a letter
#     after the `#`, and the task pattern needs a digit there.
#   - A collision rule code.  S004, I002, F101 and W001 are identifiers
#     that fixy/Collision.h defines, and they carry no hyphen.  The rule
#     that a rule code appears only beside its meaning is a review rule.
#   - A suppression marker such as FIXY-DISCIPLINE-OK or REFINED-PRE-OK.
#     The tracker families need a digit, or a wildcard, after the prefix.
#
# The guard has no allowlist.  A legitimate construct that the guard
# reports is a reason to narrow the pattern, not a reason to exempt a line.
#
# Exit status:
#   0 — clean
#   1 — at least one coordination reference
#   2 — bad invocation, missing dependency or self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The trees that obey the rule.  The old tree under include/crucible is
# frozen, so the guard does not scan it.
SCAN_DIRS=(include/foundation include/fixy test/foundation test/fixy scripts vessel)

# This file plants breadcrumbs in its self-test, so the scan skips it.
SELF_PATH='scripts/check-no-coordination-refs.sh'

# One branch per family.  The lookbehind on the task branch prevents a
# match on an HTML entity (&#123;), the shell argument count ($#), a printf
# flag (%#08x), a regex bracket ([#0-9]), an escape (\#) and token pasting.
branches=(
    '(?<![&\w$%{\[\\#])#[0-9]{2,4}\b'
    '\bFIXY-(?:U|V|FOUND)-[0-9]'
    '\bFIXY-[0-9]'
    '\bFOUND-(?:[A-Z][0-9]*|[0-9]+)\b'
    '\bGAPS-[0-9]'
    '\bSEPLOG-[A-Z]?[0-9]'
    '\bCONTRACT-[0-9]'
    '\bMETX-[A-Z0-9]'
    '\bWRAP-(?:\*|[A-Z][A-Za-z]*(?:-[A-Za-z]+)*-[0-9]+)'
    '\bBC-[0-9]+\b'
    '\bCR-[0-9]{2}\b'
    '\bfixy-[A-Z]+[0-9]*-(?:[0-9]+\b|\*|X{3}\b)'
    '\bfix-[0-9]+\b'
    '(?<![\w-])[UV]-[0-9]{3}[a-z]?\b'
    '(?<![\w.\-/])A[0-9]{1,2}(?:\.(?:[0-9]+|x))+(?![\w\-])'
    '\bStage\s+[A-D](?:[0-9]{1,2}(?:\.[0-9]+)*)?\b'
    '\b(?:at|task|tasks|since|until)\s+A[0-9]{1,2}\b'
    '\bPhase\s+[A-Z][0-9]+\b'
    '\bAgent\s+[0-9]+\b'
)
pattern="$(IFS='|'; printf '%s' "${branches[*]}")"

usage() {
    cat >&2 <<'USAGE'
check-no-coordination-refs.sh — no task, stage or tracker references.

Usage:
  check-no-coordination-refs.sh              # scan; exit 1 on a reference
  check-no-coordination-refs.sh --self-test  # plant references and clean
                                             # look-alikes, prove the verdicts
  check-no-coordination-refs.sh -h | --help  # usage

Scope:
  include/foundation include/fixy test/foundation test/fixy scripts vessel
USAGE
}

# Scan one root.  Print each hit as `path:line: text` on stdout and return
# the number of hits through the global `hits`.
hits=0
scan() {
    local scan_root="$1"
    local -a dirs=()
    local d
    for d in "${SCAN_DIRS[@]}"; do
        [[ -d "$scan_root/$d" ]] && dirs+=("$d")
    done
    hits=0
    [[ ${#dirs[@]} -eq 0 ]] && return 0
    local line
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        printf '%s\n' "$line"
        hits=$((hits + 1))
    done < <(
        cd "$scan_root" && rg -n --no-heading --with-filename --pcre2 \
            --glob "!$SELF_PATH" \
            -e "$pattern" "${dirs[@]}" 2>/dev/null || true
    )
    return 0
}

self_test() {
    local tmp_root out
    tmp_root="$(mktemp -d)"
    out="$(mktemp)"
    trap 'rm -rf "$tmp_root" "$out"' RETURN
    mkdir -p "$tmp_root/include/fixy" "$tmp_root/include/foundation" \
             "$tmp_root/scripts" "$tmp_root/include/crucible"

    # Positive controls.  Every line holds one breadcrumb family, and the
    # scan must report every line with its own line number.
    cat >"$tmp_root/include/fixy/PlantedBreadcrumbs.h" <<'PLANTED'
// Folded with its two siblings at #147.
// See task #193 for the fold.
// The forgery (#172, Door 2) is closed.
// Wired by Agent 11 in its second tier.
// Ported at A13.2.
// The wrappers arrive in tasks A10.x.
// The grant tier retired at A9.
// Stage D deletes the old tree.
// Tracked as FIXY-V-264.
// The cache row fence of FOUND-I02.
// The cluster WRAP-Cipher-2.
// Per fixy-A5-016.
// Content-keyed since fix-18.
// Migrated at V-073.
// The attack pattern of CR-05.
// Part of Phase F6.
// Filed as #1519.
// The gate of GAPS-096.
PLANTED
    local planted_count=18

    # Negative controls.  Nothing in these two files is a breadcrumb.
    cat >"$tmp_root/include/foundation/PlantedClean.h" <<'CLEAN'
#pragma once
#include <cstdint>
#define PLANTED_FLAG 1
#if PLANTED_FLAG
#endif
// S004 refuses an async launch that carries no completion witness.
// I002, F101, W001, P010 and V101 are collision rule codes.
// UTF-8, SHA-256, AVX-512, FNV-1a, x86-64 and C++26 are not tags.
// template <class B1, class B2> and A1 are template parameters.
// FIXY-DISCIPLINE-OK: a marker.  REFINED-PRE-OK: a marker too.
// Headline #1 and item #2 number the items of a list.
// &#123; is an entity, and #374151 is a color.
CLEAN
    cat >"$tmp_root/scripts/planted_clean.sh" <<'SHELL'
echo "$#" "${#arr[@]}"
printf '%#08x\n' 255
[[ "x" =~ [#0-9] ]]
SHELL

    # A scope control.  The old tree is out of scope, so the scan does not
    # report this line.
    printf '// Folded at #147.\n' >"$tmp_root/include/crucible/PlantedOld.h"

    local rc=0
    CRUCIBLE_COORD_REFS_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >"$out" 2>/dev/null || rc=$?
    if (( rc != 1 )); then
        printf 'check-no-coordination-refs --self-test: FAIL — the planted references gave exit %d, want 1.\n' "$rc" >&2
        cat "$out" >&2
        return 2
    fi
    local n
    for (( n = 1; n <= planted_count; n++ )); do
        if ! rg -q -F "include/fixy/PlantedBreadcrumbs.h:${n}:" "$out"; then
            printf 'check-no-coordination-refs --self-test: FAIL — planted line %d was not reported.\n' "$n" >&2
            cat "$out" >&2
            return 2
        fi
    done
    local reported
    reported="$(rg -c -F 'PlantedBreadcrumbs.h:' "$out" || true)"
    if [[ "$reported" != "$planted_count" ]]; then
        printf 'check-no-coordination-refs --self-test: FAIL — %s planted lines reported, want %d.\n' "${reported:-0}" "$planted_count" >&2
        cat "$out" >&2
        return 2
    fi
    if rg -q -F -e 'PlantedClean.h' -e 'planted_clean.sh' -e 'PlantedOld.h' "$out"; then
        printf 'check-no-coordination-refs --self-test: FAIL — a clean look-alike or an out-of-scope file was reported.\n' >&2
        cat "$out" >&2
        return 2
    fi
    printf 'check-no-coordination-refs --self-test: all %d planted references reported, as expected.\n' "$planted_count"

    # The clean arm.  Without the breadcrumbs the same tree scans clean.
    rm -f "$tmp_root/include/fixy/PlantedBreadcrumbs.h"
    rc=0
    CRUCIBLE_COORD_REFS_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >"$out" 2>/dev/null || rc=$?
    if (( rc != 0 )); then
        printf 'check-no-coordination-refs --self-test: FAIL — the clean look-alikes gave exit %d, want 0.\n' "$rc" >&2
        cat "$out" >&2
        return 2
    fi
    printf 'check-no-coordination-refs --self-test: rule codes, directives and look-alikes scan clean, as expected.\n'
    return 0
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        if ! command -v rg >/dev/null 2>&1; then
            printf 'check-no-coordination-refs: ripgrep (rg) is required.\n' >&2
            exit 2
        fi
        self_test || exit 2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-coordination-refs: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-no-coordination-refs: ripgrep (rg) is required.\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_COORD_REFS_TEST_ROOT:-$root}"
scan "$scan_root"

if (( hits != 0 )); then
    cat >&2 <<HINT

check-no-coordination-refs: ${hits} line(s) name a task, a stage or a
tracker entry.  State what the code does, and remove the reference:

  "Folded into one concept at #147"  →  "Folded into one concept"
  "the forgery #172 closed"          →  "a context built from nothing"
  "arrives with #190"                →  "arrives with the endpoint bridge"

If the sentence says nothing without the reference, remove the
sentence.  A collision rule code (S004, W001) is not a reference.
HINT
    exit 1
fi

printf 'check-no-coordination-refs: clean — no task, stage or tracker references in %s.\n' "${SCAN_DIRS[*]}" >&2
exit 0
