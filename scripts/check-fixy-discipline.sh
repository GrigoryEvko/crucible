#!/usr/bin/env bash
# check-fixy-discipline.sh — Phase F discipline gate for the fixy
# unification layer (misc/16_05_2026_fixy.md §4 Phase F).
#
# Greenfield discipline: under directories explicitly opted into
# CRUCIBLE_FIXY_ONLY, downstream consumers must spell
#   fixy::fn<Type, Grants...>
# instead of reaching past the umbrella to
#   safety::fn::Fn<Type, ...>
# directly.  The aggregator IS the umbrella; routing around it
# defeats the IsAccepted engagement gate, leaks the 19-positional
# substrate signature into consumer code, and breaks the federation
# cache-key story (FIXY-U-004 + FOUND-I02 row_hash_contribution).
#
# Opt-in surface (mirror of the CMake CRUCIBLE_FIXY_ONLY target
# property — kept in-script for self-contained CI invocation):
#   examples/fn/
#   test/fixy_neg/
# Plus any future cog/, mimic/, forge/, cntp/, canopy/, topology/
# headers added after the fixy/ ship date (added via the
# CRUCIBLE_FIXY_ONLY_PATHS extension below as those land).
#
# Exempt — fixy/ itself defines the aggregator and substrate
# defines the slot type, so both refer to safety::fn::Fn freely.
# Existing pre-fixy code is grandfathered through
# scripts/fixy-discipline-allowlist.txt (one repo-relative path
# per line; lines beginning with '#' are comments).
#
# Suppression for deliberate one-off references (e.g. doc
# fixtures that DEMONSTRATE the round-trip): annotate the line
# with `// FIXY-DISCIPLINE-OK: <reason>`.  Comment-only mentions
# (//, ///, /* */ lines) are skipped automatically.
#
# Exit status:
#   0  — clean (no raw substrate spellings, no stale allowlist entries)
#   1  — at least one violation (takes precedence over stale)
#   2  — stale allowlist entry (a grandfathered path with no remaining
#        reach-past-the-umbrella site — the file migrated to fixy::fn;
#        prune it) OR bad invocation / missing dependency
#
# Stale-entry detection (parity with check-no-reinterpret-cast.sh):
# every allowlist path must correspond to a file that STILL contains a
# raw substrate spelling.  Once a grandfathered file migrates to
# fixy::fn, its allowlist entry is dead weight and could silently
# re-grandfather a future raw spelling re-introduced in that file.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-discipline.sh — Phase F discipline gate for fixy::.

Usage:
  check-fixy-discipline.sh              # scan; exit 1 on violation
  check-fixy-discipline.sh --list       # list fixy-only opt-in paths
  check-fixy-discipline.sh --self-test  # plant a violation and verify catch
  check-fixy-discipline.sh -h | --help  # usage

Suppression:
  // FIXY-DISCIPLINE-OK: <reason>   on the same line skips that line.
  scripts/fixy-discipline-allowlist.txt — one repo-relative path per line.
USAGE
}

# ── Greenfield opt-ins ────────────────────────────────────────────────
# Mirror of CMake CRUCIBLE_FIXY_ONLY target property.  Add paths here
# AND register `set_target_properties(... PROPERTIES CRUCIBLE_FIXY_ONLY ON)`
# when a new directory opts in.  Order is presentational only.
#
# FIXY-V-070 — Band-3 expansion (Agent 2 CRITICAL-1).  fixy.md §5.3
# names cntp/, canopy/, cog/, topology/, forge/, mimic/, observe/,
# warden/ as "fixy-only from 17 May 2026 onward".  Each is added
# below for both include/ and src/ trees so the safety::fn::Fn<
# reach-past-the-umbrella gate fires on any future raw instantiation
# (current site count in these dirs: 0 — allowlist stays empty;
# CI catches new regressions).  Wider substrate types (Refined,
# Tagged, Linear, etc.) remain on the original safety:: spelling
# inside band-3 dirs; their fixy:: re-export migration is tracked
# under FIXY-V-073 + FIXY-U-099.
CRUCIBLE_FIXY_ONLY_PATHS=(
    examples/fn
    test/fixy_neg
    include/crucible/cntp
    include/crucible/canopy
    include/crucible/cog
    include/crucible/topology
    include/crucible/forge
    include/crucible/mimic
    include/crucible/observe
    include/crucible/warden
    src/cntp
    src/canopy
    src/cog
    src/topology
    src/forge
    src/mimic
    src/observe
    src/warden
)

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)
        printf 'check-fixy-discipline: fixy-only opt-in paths:\n'
        for p in "${CRUCIBLE_FIXY_ONLY_PATHS[@]}"; do
            printf '  %s\n' "$p"
        done
        exit 0
        ;;
    --self-test)
        # Plant a synthetic violation and re-run the script scoped
        # to a temp tree.  Failure here means the regex broke (rg
        # upgrade, anchor drift, allowlist-path regression) — a
        # regex that never matches anything is a placebo, not a
        # guard.  Mirrors the discipline of
        # scripts/check-refined-pre-subsumption.sh --self-test.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/examples/fn" "$tmp_root/scripts"
        cat >"$tmp_root/examples/fn/planted_violation.cpp" <<'PLANTED'
// Synthetic FIXY-DISCIPLINE violation for --self-test verification.
// Production callers should spell fixy::fn<int> — reaching past the
// umbrella to safety::fn::Fn<int> directly is the rejected pattern.
namespace crucible::planted {
using PlantedDirect = ::crucible::safety::fn::Fn<int>;
}  // namespace crucible::planted
PLANTED
        # Empty allowlist so the planted file is NOT exempted.
        : >"$tmp_root/scripts/fixy-discipline-allowlist.txt"
        result_file="$(mktemp)"
        if CRUCIBLE_FIXY_DISCIPLINE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — planted violation was not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -q 'FIXY-DISCIPLINE violation.*safety::fn::Fn' "$result_file"; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — diagnostic missing substrate spelling.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-fixy-discipline: self-test phase 1 passed — regex fires on planted violation.\n' >&2

        # ── Phase 2: stale allowlist entry detection ─────────────────
        # Reuse the planted tree but allowlist the live violation (so
        # violation_count stays 0 and the file lands in the live set) and
        # add a SECOND entry for a file that does NOT exist — it is absent
        # from the live set, hence STALE.  Expect rc=2 with exactly one
        # stale diagnostic naming the migrated-away path.  A live entry
        # being false-flagged, or the stale one being missed, fails here.
        printf 'examples/fn/planted_violation.cpp\nexamples/fn/migrated_away.cpp\n' \
            >"$tmp_root/scripts/fixy-discipline-allowlist.txt"
        result_file="$(mktemp)"
        stale_rc=0
        CRUCIBLE_FIXY_DISCIPLINE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$result_file" || stale_rc=$?
        if [[ "$stale_rc" -ne 2 ]]; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — stale entry expected rc=2, got %s.\n' "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        stale_hits="$(grep -c '^FIXY-DISCIPLINE stale:' "$result_file" || true)"
        if [[ "$stale_hits" -ne 1 ]]; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — expected exactly 1 stale diagnostic, got %s.\n' "$stale_hits" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -q 'FIXY-DISCIPLINE stale:.*migrated_away.cpp' "$result_file"; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — stale diagnostic did not name migrated_away.cpp.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if grep -q 'FIXY-DISCIPLINE stale:.*planted_violation.cpp' "$result_file"; then
            printf 'check-fixy-discipline: SELF-TEST FAILED — live (allowlisted) entry was false-flagged stale.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-fixy-discipline: self-test passed — regex fires on planted violation; stale allowlist entry detected and live entry preserved.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-fixy-discipline: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_FIXY_DISCIPLINE_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/fixy-discipline-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-fixy-discipline: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Banned substrate spellings ────────────────────────────────────────
# The discipline rejects raw reach-past-the-umbrella references to
# the 19-axis aggregator.  Match the INSTANTIATION form (open-angle-
# bracket immediately following) — bare mentions in prose comments
# are filtered out below.
banned_pattern='\b(::)?(crucible::)?safety::fn::Fn\s*<'

# ── Per-path scan ─────────────────────────────────────────────────────
violation_count=0
scan_paths=()
for p in "${CRUCIBLE_FIXY_ONLY_PATHS[@]}"; do
    abs="$scan_root/$p"
    if [[ -d "$abs" ]]; then
        scan_paths+=("$abs")
    fi
done

if [[ ${#scan_paths[@]} -eq 0 ]]; then
    printf 'check-fixy-discipline: no fixy-only paths exist under %s — nothing to scan.\n' "$scan_root" >&2
    exit 0
fi

# ── Live set of file paths for stale-entry detection ─────────────────
# Every file with a reach-past-the-umbrella candidate that survives the
# comment / inline-marker filters records its path here, allowlist-
# blind.  After the scan, every allowlist path must be in this set;
# entries that aren't (the file migrated to fixy::fn) are STALE and
# flagged — parity with check-no-reinterpret-cast.sh.  This build sits
# AFTER the no-scan-paths early exit so absent opt-in dirs never make
# every allowlist entry look stale.
live_set_file="$(mktemp)"
trap 'rm -f "$live_set_file"' EXIT

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    # Strip leading whitespace for comment-prefix detection.
    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip C++ line comments — pure prose mentions of the spelling.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression marker.
    if [[ "$text" == *'FIXY-DISCIPLINE-OK'* ]]; then
        continue
    fi

    rel="${file#"$scan_root"/}"
    printf '%s\n' "$rel" >> "$live_set_file"

    # Per-line allowlist (repo-relative path; full-line exact match).
    if [[ -f "$allowlist" ]] && grep -Fxq -- "$rel" "$allowlist"; then
        continue
    fi

    printf 'FIXY-DISCIPLINE violation: %s:%s — raw safety::fn::Fn< spelling.  Use fixy::fn<Type, Grants...> instead.\n' \
        "$rel" "$line" >&2
    violation_count=$((violation_count + 1))
done < <(
    rg -nP \
       --no-heading \
       --type=cpp \
       --glob '!build/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       "$banned_pattern" "${scan_paths[@]}" 2>/dev/null || true
)

# ── Outcome (violations — take precedence over stale) ─────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-fixy-discipline detected ${violation_count} reach-past-the-umbrella site(s).
Each site lives under a CRUCIBLE_FIXY_ONLY directory and spells the
substrate aggregator (safety::fn::Fn<...>) directly instead of the
fixy::fn<Type, Grants...> umbrella entry point.  This breaks the
IsAccepted engagement gate and bypasses the federation cache-key
story (FIXY-U-004 + FOUND-I02 row_hash_contribution).

Three remediations:

  (1) Rewrite the binding as fixy::fn<Type, Grants...> using the
      grant tag catalog under crucible::fixy::grant::.  This is
      the strongly preferred fix and matches the umbrella story.
  (2) If the site is a deliberate round-trip demonstration that
      MUST spell the substrate form (e.g. a doc fixture), annotate
      the line with '// FIXY-DISCIPLINE-OK: <reason>'.
  (3) If the file is grandfathered pre-fixy code that is awaiting
      a tracked migration, add its repo-relative path to
      scripts/fixy-discipline-allowlist.txt with a TODO referencing
      the migration task.  Trim the allowlist as files migrate.
HINT
    exit 1
fi

# ── Stale allowlist entries (parity with check-no-reinterpret-cast.sh) ─
# A grandfathered path whose file no longer holds a raw substrate spelling
# is dead weight: it would silently re-grandfather a future raw spelling
# re-introduced in that file.  The live set above is allowlist-blind, so an
# entry absent from it means the file migrated to fixy::fn.  Keys are bare
# repo-relative paths (no line suffix, no trailing prose) — compare the whole
# trimmed entry, NOT a first whitespace token.
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        trimmed="${entry%%#*}"
        trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
        trimmed="${trimmed#"${trimmed%%[![:space:]]*}"}"
        [[ -z "$trimmed" ]] && continue
        if ! grep -Fxq -- "$trimmed" "$live_set_file" 2>/dev/null; then
            printf 'FIXY-DISCIPLINE stale: %s — STALE allowlist entry (no raw safety::fn::Fn< spelling remains in this file; it migrated to fixy::fn — remove from allowlist).\n' \
                "$trimmed" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-fixy-discipline found ${stale_count} stale allowlist entry/entries.
Each grandfathered path no longer contains a raw safety::fn::Fn< spelling —
the file already migrated to the fixy::fn umbrella.  A stale entry is a
latent re-grandfather hazard: a future raw spelling re-introduced in that
file would slip past the gate unnoticed.  Prune the listed path(s) from
scripts/fixy-discipline-allowlist.txt.
HINT
    exit 2
fi

printf 'check-fixy-discipline: clean — no reach-past-the-umbrella sites detected, no stale allowlist entries.\n' >&2
exit 0
