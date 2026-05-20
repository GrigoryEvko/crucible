#!/usr/bin/env bash
# check-fixy-clean-headers.sh — FIXY-U-096* migration lock-in (FIXY-U-096z).
#
# The FIXY-U-096* sweep migrated the top-level include/crucible/*.h
# headers from direct `safety::` namespace references onto the `fixy::`
# umbrella re-exports.  This guard LOCKS IN that work: every header
# listed in scripts/fixy-clean-headers.txt is a CERTIFIED-CLEAN header
# that reaches the safety:: substrate only through fixy::, and must
# therefore contain ZERO `safety::` namespace tokens — in code AND
# comments.  A migrated header spells its wrappers `fixy::wrap::Tagged`,
# never `safety::Tagged`; prose that writes `safety::` is the regression
# signal that bit the DimHash comment near-miss (FIXY-U-096v).
#
# This is the inverse polarity of check-no-reserve.sh / check-fixy-
# discipline.sh: those scan a wide tree for a BANNED pattern with an
# exemption allowlist.  This scans a NARROW positive registry and
# asserts each listed file stays clean — the registry is a promise,
# not an exemption.  A lexical `safety::`-vs-comment ambiguity that
# would defeat a tree-wide scan does not arise here: registered files
# are at literal zero, so ANY occurrence (code or comment) is drift.
#
# `safety/` include PATHS are NOT matched: `\bsafety::` is the namespace
# token only and never matches `<crucible/safety/Foo.h>`.  Registered
# headers may still carry `safety/Decide.h` / `safety/Pre.h` /
# `safety/Simd.h` includes (CRUCIBLE_PRE + crucible::decide +
# crucible::simd — primitives outside the safety:: namespace).
#
# Exit status:
#   0 — clean (every registered header is safety::-free)
#   1 — at least one registered header regressed
#   2 — bad invocation / missing dependency / registry drift

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-clean-headers.sh — FIXY-U-096* certified-clean lock-in.

Usage:
  check-fixy-clean-headers.sh              # scan; exit 1 on regression
  check-fixy-clean-headers.sh --self-test  # plant drift, verify catch
  check-fixy-clean-headers.sh --list       # print the certified registry
  check-fixy-clean-headers.sh -h | --help  # usage

Registry: scripts/fixy-clean-headers.txt — one repo-relative header
path per line; `#` comments and blank lines ignored.  A header joins
the registry the moment it reaches literal-zero `safety::`; the
registry only grows.
USAGE
}

# ── Banned pattern ────────────────────────────────────────────────────
# The `safety::` namespace token in any form: bare `safety::Foo` (resolved
# through an enclosing `namespace crucible`) and fully-qualified
# `::crucible::safety::Foo`.  `\b` before `safety` matches the boundary
# after `:` in `::crucible::safety::`, so both shapes are caught.  The
# slash include form `safety/Foo.h` is NOT matched (`::` != `/`).
banned_pattern='\bsafety::'

scan_root="${CRUCIBLE_FIXY_CLEAN_TEST_ROOT:-$root}"
registry="$scan_root/scripts/fixy-clean-headers.txt"

# ── Registry reader (skips `#` comments and blank lines) ──────────────
registered_paths() {
    [[ -f "$registry" ]] || return 0
    grep -E -v '^[[:space:]]*(#|$)' "$registry"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)
        printf 'check-fixy-clean-headers: certified-clean registry:\n'
        registered_paths | while IFS= read -r p; do printf '  %s\n' "$p"; done
        exit 0
        ;;
    --self-test)
        # Plant TWO registered headers under a temp root: one DIRTY (a
        # `safety::` code ref — must be caught) and one CLEAN (uses the
        # fixy:: umbrella — must NOT be caught).  A scanner that fails to
        # flag the dirty file, or that false-flags the clean file, has a
        # broken pattern and is a placebo.  Mirrors check-no-reserve.sh
        # --self-test discipline.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/clean.h" <<'CLEAN'
#pragma once
// Synthetic CLEAN fixy header for --self-test: reaches the substrate
// only through the fixy:: umbrella — must NOT be flagged.
#include <crucible/fixy/Wrap.h>
namespace crucible::planted {
using CleanAlias = ::crucible::fixy::wrap::Tagged<int, int>;
}  // namespace crucible::planted
CLEAN
        cat >"$tmp_root/include/crucible/planted/dirty.h" <<'DIRTY'
#pragma once
// Synthetic DIRTY header for --self-test: reaches past the umbrella to
// the substrate directly — the regressed line below MUST be caught.
namespace crucible::planted {
using DirtyAlias = ::crucible::safety::Tagged<int, int>;
}  // namespace crucible::planted
DIRTY
        cat >"$tmp_root/scripts/fixy-clean-headers.txt" <<'REG'
# self-test registry
include/crucible/planted/clean.h
include/crucible/planted/dirty.h
REG
        result_file="$(mktemp)"
        if CRUCIBLE_FIXY_CLEAN_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-fixy-clean-headers: SELF-TEST FAILED — planted regression not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The dirty alias (line 5 of dirty.h) must be flagged.
        if ! grep -qF 'planted/dirty.h:5' "$result_file"; then
            printf 'check-fixy-clean-headers: SELF-TEST FAILED — expected diagnostic for dirty.h:5 missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The clean header must NOT be flagged anywhere.
        if grep -qF 'planted/clean.h' "$result_file"; then
            printf 'check-fixy-clean-headers: SELF-TEST FAILED — clean header false-flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-fixy-clean-headers: self-test passed — drift caught, clean header honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-fixy-clean-headers: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-fixy-clean-headers: ripgrep (rg) is required\n' >&2
    exit 2
fi
if [[ ! -f "$registry" ]]; then
    printf 'check-fixy-clean-headers: registry not found: %s\n' "$registry" >&2
    exit 2
fi

violation_count=0
missing_count=0
checked_count=0

while IFS= read -r rel; do
    abs="$scan_root/$rel"
    if [[ ! -f "$abs" ]]; then
        printf 'FIXY-CLEAN registry drift: %s — registered but missing (renamed/deleted?).\n' \
            "$rel" >&2
        missing_count=$((missing_count + 1))
        continue
    fi
    checked_count=$((checked_count + 1))
    while IFS= read -r lineno; do
        [[ -z "$lineno" ]] && continue
        printf 'FIXY-CLEAN regression: %s:%s — `safety::` in a fixy-certified header; route through fixy::.\n' \
            "$rel" "$lineno" >&2
        violation_count=$((violation_count + 1))
    done < <(rg -nP --no-heading "$banned_pattern" "$abs" 2>/dev/null | cut -d: -f1 || true)
done < <(registered_paths)

if [[ "$missing_count" -ne 0 ]]; then
    printf 'check-fixy-clean-headers: %d registry path(s) missing — update scripts/fixy-clean-headers.txt.\n' \
        "$missing_count" >&2
    exit 2
fi

if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-fixy-clean-headers detected ${violation_count} regression(s): a
header certified by the FIXY-U-096* sweep reached past the fixy::
umbrella to the safety:: substrate directly.

Remediations:
  (1) Replace the reference with its fixy:: re-export
      (safety::Tagged -> fixy::wrap::Tagged,
       safety::source::X -> fixy::tags::source::X, ...).
  (2) If the line is doc-comment prose, reword it to describe the
      fixy:: spelling (a certified header documents fixy::, never
      safety::).
  (3) If a header genuinely cannot route through the umbrella
      (circular-blocked like Arena.h / Saturate.h), it should NOT be
      on the registry — remove its line from
      scripts/fixy-clean-headers.txt with a rationale.
HINT
    exit 1
fi

printf 'check-fixy-clean-headers: clean — %d certified header(s) safety::-free.\n' \
    "$checked_count" >&2
exit 0
