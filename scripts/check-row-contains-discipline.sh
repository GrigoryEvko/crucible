#!/usr/bin/env bash
# check-row-contains-discipline.sh — row-membership discipline (FIXY-U-101).
#
# Closes the regression-prevention surface for fixy-A5-039: "effects::
# row_contains_v not asserted at function-signature level — discipline
# scaling gap".  The fix shipped named lifts (CtxOwnsCapability /
# CtxOwnsAnyOf / CtxOwnsAllOf in effects/ExecCtx.h, commit c0b53827)
# plus a worked-example migration of IncastControlRuntime.  This guard
# prevents new inline `row_contains_v<row_type_of_t<Ctx>, ...>` uses
# in production code; future code must use the named lifts.
#
# ── DISCIPLINE ────────────────────────────────────────────────────────
#
# In production code (cntp/, canopy/, topology/, perf/, warden/,
# sessions/, ...), every Ctx row-membership check at function signature
# level MUST use the named lift:
#
#   row_contains_v<row_type_of_t<Ctx>, Effect::X>            →  use:
#     effects::CtxOwnsCapability<Ctx, Effect::X>
#
#   row_contains_v<..., X> || row_contains_v<..., Y>         →  use:
#     effects::CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>
#
#   row_contains_v<..., X> && row_contains_v<..., Y>         →  use:
#     effects::CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>
#
# The named lifts are grep-discoverable (`grep CtxOwns` surfaces every
# capability admission in the substrate) and semantically clearer than
# the row_contains_v expansion.  They also future-proof the discipline:
# if row_type_of_t<Ctx> is renamed, CtxOwnsCapability tracks the
# rename; inline `row_contains_v<row_type_of_t<Ctx>, ...>` would
# silently break.
#
# Scope: scan production code (include/ + src/, EXCLUDING effects/
# itself — the primitives live there and reference row_contains_v
# internally; that's where the named lifts ARE DEFINED).
#
# Inline suppression (rare): `// ROW-CONTAINS-OK: <reason>` on the
# matching line.  Use when the inline form is structurally required
# (e.g. inside the named lift's own definition).
#
# ── COVERED PATTERN ──────────────────────────────────────────────────
#
# `effects::row_contains_v<` invocations in production code.  This is
# the canonical pattern fixy-A5-039 was scaling out from.
#
# Exit status:
#   0 — clean (no NEW inline sites, no stale allowlist entries)
#   1 — at least one violation (takes precedence over stale)
#   2 — stale allowlist entry (a path:line where row_contains_v no
#       longer exists — the site moved on a line shift, or was migrated
#       to a named lift; prune it) OR bad invocation / missing dependency
#
# Stale-entry detection (parity with check-no-reinterpret-cast.sh):
# every allowlist entry must correspond to a LIVE inline site.  A
# line-shifting edit silently invalidates a grandfather entry; without
# this gate a migrated-away entry lingers and could re-grandfather a
# future inline site re-introduced at the same line.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-row-contains-discipline.sh — Ctx row-membership discipline.

Usage:
  check-row-contains-discipline.sh              # scan; exit 1 on violation
  check-row-contains-discipline.sh --self-test  # plant a violation, verify catch
  check-row-contains-discipline.sh -h | --help  # usage

Suppression:
  // ROW-CONTAINS-OK: <reason>                  on call line — exempts THIS line
  scripts/row-contains-discipline-allowlist.txt:
    path:line — <migration ticket / structural rationale>
    # comment                                   — ignored

The fix is usually one of:
  effects::CtxOwnsCapability<Ctx, Effect::X>            (single effect)
  effects::CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>      (disjunction)
  effects::CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>      (conjunction)
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_row.h" <<'PLANTED'
#pragma once
// Synthetic row-contains-discipline fixture for --self-test.
namespace crucible::planted {
template <class Ctx>
    requires effects::row_contains_v<Ctx, X>  // FLAGGED — must be caught
void planted_drift(Ctx const&) {}
template <class Ctx>
    requires effects::row_contains_v<Ctx, X>  // ALLOWLISTED — must NOT be caught
void planted_allowlisted(Ctx const&) {}
template <class Ctx>
    requires effects::row_contains_v<Ctx, X>  // ROW-CONTAINS-OK: synthetic --self-test marker
void planted_suppressed(Ctx const&) {}
// requires effects::row_contains_v<Ctx, X>   // commented — must NOT be caught
}  // namespace crucible::planted
PLANTED
        cat >"$tmp_root/scripts/row-contains-discipline-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry
include/crucible/planted/planted_row.h:8 — synthetic test (CtxOwnsCapability migration pending)
ALLOW
        result_file="$(mktemp)"
        if CRUCIBLE_ROW_CONTAINS_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED — planted violation not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # planted_drift on line 5 must be flagged.
        if ! grep -qF 'planted_row.h:5' "$result_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED — line 5 not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # planted_allowlisted on line 8 must NOT be flagged.
        if grep -qF 'planted_row.h:8' "$result_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED — allowlist leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # planted_suppressed on line 11 must NOT be flagged.
        if grep -qF 'planted_row.h:11' "$result_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED — ROW-CONTAINS-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # commented line 13 must NOT be flagged.
        if grep -qF 'planted_row.h:13' "$result_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED — comment leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"

        # ── Phase 2: stale-allowlist-entry detection ─────────────────
        # Reuse the planted fixture.  Suppress BOTH live sites (lines 5
        # and 8) with allowlist entries — each carrying the ` — ticket`
        # suffix this guard's grammar permits, exercising the same first-
        # token extraction allowlisted() uses — so the scan reaches the
        # stale pass with zero violations, then add a stale entry
        # (line 99 — no row_contains_v there) that MUST be flagged.
        cat >"$tmp_root/scripts/row-contains-discipline-allowlist.txt" <<'ALLOW'
# self-test live entries (real inline sites)
include/crucible/planted/planted_row.h:5 — synthetic live (migration pending)
include/crucible/planted/planted_row.h:8 — synthetic live (migration pending)
# self-test stale entry (no row_contains_v at this line)
include/crucible/planted/planted_row.h:99 — synthetic stale (no site here)
ALLOW
        stale_file="$(mktemp)"
        stale_rc=0
        CRUCIBLE_ROW_CONTAINS_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$stale_file" || stale_rc=$?
        if [[ "$stale_rc" -ne 2 ]]; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED (phase 2) — expected exit 2 (stale), got %d.\n' \
                "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        # Exactly one stale diagnostic — the live entries (5, 8) must NOT
        # be flagged (count==1 implies no false positive; also confirms
        # the ` — ticket` suffix did not break first-token extraction).
        stale_emitted="$(grep -c '^ROW-CONTAINS stale:' "$stale_file" || true)"
        if [[ "$stale_emitted" -ne 1 ]]; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED (phase 2) — expected 1 stale diagnostic, got %s.\n' \
                "$stale_emitted" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        if ! grep -qF -- 'planted_row.h:99' "$stale_file"; then
            printf 'check-row-contains-discipline: SELF-TEST FAILED (phase 2) — stale entry :99 not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        rm -f "$stale_file"

        printf 'check-row-contains-discipline: self-test passed — drift caught, allowlist + inline marker + comment filter honoured; stale detection flags dead allowlist entries (none on live).\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-row-contains-discipline: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_ROW_CONTAINS_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/row-contains-discipline-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-row-contains-discipline: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Pattern ──────────────────────────────────────────────────────────
# Match `effects::row_contains_v<` invocations.  The literal namespace
# qualifier disambiguates from other row_contains_v usages (none exist
# elsewhere today, but the qualifier makes the audit explicit).
candidate_pattern='effects::row_contains_v\s*<'

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        awk -F' ' '{print $1}' | \
        grep -Fxq -- "$rel:$line"
}

# ── Live set of (path:line) for stale-entry detection ────────────────
# Every candidate inline site that survives the comment / inline-marker
# / effects-exclusion filters records its (rel:line) here, allowlist-
# blind.  After the scan, every allowlist entry's path:line key must be
# in this set; entries that aren't (the site moved on a line shift, or
# was migrated to a named lift) are STALE and flagged — parity with
# check-no-reinterpret-cast.sh.
live_set_file="$(mktemp)"
trap 'rm -f "$live_set_file"' EXIT

violation_count=0
scan_dirs=("$scan_root/include" "$scan_root/src")

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip comment lines.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression.
    case "$text" in
        *'ROW-CONTAINS-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"

    # Exclude effects/ itself — primitives live there and reference
    # row_contains_v internally; that's where named lifts are DEFINED.
    case "$rel" in
        include/crucible/effects/*) continue ;;
    esac

    printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'ROW-CONTAINS violation: %s:%s — inline row_contains_v should use CtxOwnsCapability / CtxOwnsAnyOf / CtxOwnsAllOf (fixy-A5-039).\n' \
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
       --glob '!test/**' \
       --glob '!bench/**' \
       --glob '!examples/**' \
       --glob '!**/fixy/**' \
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

# ── Outcome (violations — take precedence over stale) ────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-row-contains-discipline detected ${violation_count} inline
row_contains_v site(s) outside the allowlist.  fixy-A5-039 named-lift
discipline requires CtxOwnsCapability / CtxOwnsAnyOf / CtxOwnsAllOf
for grep-discoverable capability admission.

Remediations, in order of preference:

  (1) Single-effect: replace
        effects::row_contains_v<effects::row_type_of_t<Ctx>, Effect::X>
      with
        effects::CtxOwnsCapability<Ctx, Effect::X>

  (2) Disjunction: replace
        row_contains_v<..., X> || row_contains_v<..., Y>
      with
        effects::CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>

  (3) Conjunction: replace
        row_contains_v<..., X> && row_contains_v<..., Y>
      with
        effects::CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>

  (4) Annotate with '// ROW-CONTAINS-OK: <reason>' for structurally-
      justified exceptions (rare — typically only inside the named
      lift's own definition).

  (5) Add 'path:line — <migration-ticket>' to
      scripts/row-contains-discipline-allowlist.txt for grandfathered
      sites awaiting a tracked migration sweep.
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
# Every allowlist entry must correspond to a LIVE inline site.  Entries
# carry an optional ` — <ticket>` suffix after the path:line key (the
# same shape allowlisted() parses with awk '{print $1}'), so extract the
# first whitespace-delimited token before matching the live set.  An
# entry whose path:line is not live points at a site that moved on a
# line shift or was migrated to a named lift — flag it so the allowlist
# drains and can never silently re-grandfather a future inline site.
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        trimmed="${entry%%#*}"
        trimmed="${trimmed#"${trimmed%%[![:space:]]*}"}"
        [[ -z "$trimmed" ]] && continue
        key="${trimmed%%[[:space:]]*}"          # path:line, dropping ` — ticket`
        if ! grep -Fxq -- "$key" "$live_set_file" 2>/dev/null; then
            printf 'ROW-CONTAINS stale: %s — STALE allowlist entry (no inline row_contains_v at this line; the site moved on a line shift or was migrated to a named lift — remove from allowlist).\n' \
                "$key" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-row-contains-discipline detected ${stale_count} stale allowlist
entr(y/ies).  Each points at a path:line where effects::row_contains_v
no longer exists — the site moved (line shift) or was migrated to a
named lift (CtxOwnsCapability / CtxOwnsAnyOf / CtxOwnsAllOf).  Prune the
stale entries from scripts/row-contains-discipline-allowlist.txt so the
guard regains drift coverage at those lines.
HINT
    exit 2
fi

printf 'check-row-contains-discipline: clean — no new inline row_contains_v sites, no stale allowlist entries.\n' >&2
exit 0
