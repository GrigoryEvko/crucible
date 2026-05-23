#!/usr/bin/env bash
# check-no-raw-ofstream.sh — raw C++ file-stream ban in band-3 dirs (FIXY-V-237).
#
# Agent 9 Phase F6.  The eight band-3 subsystems (cntp, canopy, cog,
# topology, forge, mimic, observe, warden) are "fixy-only from 17 May
# 2026 onward" (fixy.md §5.3, FIXY-V-070).  Every filesystem touch in
# those dirs MUST route through the typed RAII capability surface —
# safety::OwnedFile (std::FILE* RAII, FIXY-V-230) / safety::FileHandle
# (fd RAII) / fixy::mint_file (Ctx-bound, effects::IO-admitting).  Raw
# C++ iostream file streams (std::ofstream / std::ifstream / std::fstream
# and their basic_* templates) bypass that surface entirely:
#
#   (1) No effects::IO capability admission — the syscall-capability
#       discipline (FIXY-U-100) and the fixy:: I/O grants never see them.
#   (2) Exception-throwing on failure (we build -fno-exceptions); a
#       failed open silently sets failbit instead of being handled at
#       the type level via std::expected / OwnedFile's nullptr sentinel.
#   (3) Hidden locale + buffering + sync_with_stdio cost, none of which
#       the deterministic Cipher / observe write paths want.
#
# This guard flags any std::[basic_][oi]?fstream usage AND any
# `#include <fstream>` inside the production band-3 dirs.  It is a
# regression-prevention surface: band-3 has ZERO raw file-stream usage
# today (verified at ship), so the guard ships green with an empty
# allowlist, exactly like check-fixy-spawn-discipline.sh (FIXY-V-210).
#
# Scope: include/crucible/{8 band-3} + src/{8 band-3} only.  Fixture
# dirs (examples/fn/, test/fixy_neg/) are EXEMPT — a neg-compile fixture
# may deliberately demonstrate the rejected pattern.  The authoritative
# band-3 list lives in CMakeLists.txt (CRUCIBLE_FIXY_ONLY_PATHS) /
# check-fixy-discipline.sh; adding a band-3 directory updates BOTH this
# array and that list (the configure-time drift check guards the latter).
#
# Per-line allowlist: scripts/no-raw-ofstream-allowlist.txt.  Each line
# is `path:line` (relative to project root).  Comment lines start with
# `#`.  Empty lines are ignored.
#
# Inline suppression: `// NO-RAW-OFSTREAM-OK: <reason>` on the line
# exempts that single line.
#
# Exit status:
#   0 — clean (no NEW file-stream sites, no stale allowlist entries)
#   1 — at least one violation (takes precedence over stale)
#   2 — stale allowlist entry (a path:line where no file-stream remains —
#       the use moved on a line shift, or the site was migrated away;
#       prune it) OR bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-no-raw-ofstream.sh — raw C++ file-stream ban in band-3 dirs.

Usage:
  check-no-raw-ofstream.sh              # scan; exit 1 on violation
  check-no-raw-ofstream.sh --self-test  # plant a violation, verify catch
  check-no-raw-ofstream.sh -h | --help  # usage

Suppression:
  // NO-RAW-OFSTREAM-OK: <reason>        on the line — exempts THIS line
  scripts/no-raw-ofstream-allowlist.txt:
    path:line                           — exempts the use at that line
    # comment                           — ignored
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE violation in a synthetic band-3 file under a temp
        # root.  The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted use, ONE inline-suppressed use, and
        # ONE commented use to verify the suppression mechanisms.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/cntp" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/cntp/planted_ofstream.h" <<'PLANTED'
#pragma once
// Synthetic no-raw-ofstream fixture for --self-test.
#include <fstream>
namespace crucible::cntp::planted {
inline void planted_drift() {
    std::ofstream out{"/tmp/x"};      // FLAGGED — must be caught
}
inline void planted_allowlisted() {
    std::ifstream in{"/tmp/x"};       // ALLOWLISTED — must NOT be caught
}
inline void planted_suppressed() {
    std::fstream io{"/tmp/x"};  // NO-RAW-OFSTREAM-OK: synthetic --self-test marker
}
inline void planted_in_comment() {
    // std::ofstream commented{"/tmp/x"};  // commented — must NOT be caught
}
}  // namespace crucible::cntp::planted
PLANTED
        # Allowlist entry targets the SECOND file-stream site (the
        # std::ifstream on line 9 of planted_ofstream.h — count manually
        # for determinism).  Note line 3 (#include <fstream>) is ALSO a
        # violation; suppress it with a live allowlist entry so the
        # phase-1 "drift caught" assertion isolates line 6.
        cat >"$tmp_root/scripts/no-raw-ofstream-allowlist.txt" <<'ALLOW'
# self-test grandfathered entries
include/crucible/cntp/planted_ofstream.h:3
include/crucible/cntp/planted_ofstream.h:9
ALLOW
        result_file="$(mktemp)"
        if CRUCIBLE_NO_OFSTREAM_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED — planted violation not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The planted_drift line (line 6, std::ofstream) must be flagged.
        if ! grep -qF 'planted_ofstream.h:6' "$result_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED — expected diagnostic for line 6 missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted line (line 9, std::ifstream) must NOT be flagged.
        if grep -qF 'planted_ofstream.h:9' "$result_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed line (line 12, std::fstream) must NOT be flagged.
        if grep -qF 'planted_ofstream.h:12' "$result_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED — inline NO-RAW-OFSTREAM-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out use (line 15) must NOT be flagged.
        if grep -qF 'planted_ofstream.h:15' "$result_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"

        # ── Phase 2: stale-allowlist-entry detection ─────────────────
        # Suppress BOTH real sites that are NOT inline-suppressed/commented
        # (line 3 #include, line 6 ofstream, line 9 ifstream) with live
        # allowlist entries so the scan reaches the stale pass with zero
        # violations, then add a stale entry (line 99 — no file stream
        # there) that MUST be flagged.
        cat >"$tmp_root/scripts/no-raw-ofstream-allowlist.txt" <<'ALLOW'
# self-test live entries (real file-stream sites)
include/crucible/cntp/planted_ofstream.h:3
include/crucible/cntp/planted_ofstream.h:6
include/crucible/cntp/planted_ofstream.h:9
# self-test stale entry (no file stream at this line)
include/crucible/cntp/planted_ofstream.h:99
ALLOW
        stale_file="$(mktemp)"
        stale_rc=0
        CRUCIBLE_NO_OFSTREAM_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$stale_file" || stale_rc=$?
        if [[ "$stale_rc" -ne 2 ]]; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED (phase 2) — expected exit 2 (stale), got %d.\n' \
                "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        # Exactly one stale diagnostic — the live entries (3, 6, 9) must
        # NOT be flagged (count==1 implies no false positive).
        stale_emitted="$(grep -c '^NO-RAW-OFSTREAM stale:' "$stale_file" || true)"
        if [[ "$stale_emitted" -ne 1 ]]; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED (phase 2) — expected 1 stale diagnostic, got %s.\n' \
                "$stale_emitted" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        if ! grep -qF -- 'planted_ofstream.h:99' "$stale_file"; then
            printf 'check-no-raw-ofstream: SELF-TEST FAILED (phase 2) — stale entry :99 not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        rm -f "$stale_file"

        printf 'check-no-raw-ofstream: self-test passed — drift caught, allowlist + inline marker + comment-filter honoured; stale detection flags dead allowlist entries (none on live).\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-raw-ofstream: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_NO_OFSTREAM_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/no-raw-ofstream-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-no-raw-ofstream: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Pattern ───────────────────────────────────────────────────────────
# `std::[basic_][oi]?fstream` covers std::ofstream / std::ifstream /
# std::fstream and their basic_* template spellings.  `#include <fstream>`
# catches the header pull-in even before a type is named (intent signal).
# Word boundary `\b` keeps `std::ofstream_something` from matching only
# the prefix — but any identifier literally starting with one of these
# IS the stream type, so the boundary just avoids matching mid-token.
candidate_pattern='std::(basic_)?[oi]?fstream\b|#[[:space:]]*include[[:space:]]*<fstream>'

# ── Band-3 production scan dirs (FIXY-V-070 mirror) ──────────────────
# Authoritative list: CMakeLists.txt CRUCIBLE_FIXY_ONLY_PATHS /
# check-fixy-discipline.sh.  examples/fn/ + test/fixy_neg/ are band-3 but
# EXEMPT here (fixture dirs may demonstrate the rejected pattern).
band3_subsystems=(cntp canopy cog topology forge mimic observe warden)
scan_dirs=()
for sub in "${band3_subsystems[@]}"; do
    scan_dirs+=("$scan_root/include/crucible/$sub" "$scan_root/src/$sub")
done

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$line"
}

# ── Live set of (path:line) for stale-entry detection ────────────────
live_set_file="$(mktemp)"
trap 'rm -f "$live_set_file"' EXIT

violation_count=0

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip comment lines outright (the candidate may appear in prose).
    # NOTE: `#include <fstream>` starts with `#`, NOT `//`/`*`, so it is
    # not swallowed by this comment filter — only C++ comments are.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression — `// NO-RAW-OFSTREAM-OK: <reason>`.
    case "$text" in
        *'NO-RAW-OFSTREAM-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"
    printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'NO-RAW-OFSTREAM violation: %s:%s — raw C++ file stream banned in band-3 (FIXY-V-237).\n' \
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
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

# ── Outcome (violations — take precedence over stale) ────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-raw-ofstream detected ${violation_count} raw C++ file-stream
site(s) in band-3 dirs outside the allowlist.  The eight band-3
subsystems are fixy-only (fixy.md §5.3); all filesystem touches route
through the typed RAII capability surface.

Remediations, in order of preference:

  (1) Route through fixy::mint_file (Ctx-bound, effects::IO-admitting)
      when you need the full capability discipline.

  (2) Use safety::OwnedFile (std::FILE* RAII, FIXY-V-230) or
      safety::FileHandle (fd RAII) — both close on RAII drop and expose
      an std::expected / nullptr-sentinel surface instead of failbit.

  (3) Annotate the line with '// NO-RAW-OFSTREAM-OK: <reason>' for a
      structurally-justified one-off exception.

  (4) Add 'path:line' to scripts/no-raw-ofstream-allowlist.txt for a
      grandfathered site awaiting migration — each entry is a TODO.
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        trimmed="${entry%%#*}"
        trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
        trimmed="${trimmed#"${trimmed%%[![:space:]]*}"}"
        [[ -z "$trimmed" ]] && continue
        if ! grep -Fxq -- "$trimmed" "$live_set_file" 2>/dev/null; then
            printf 'NO-RAW-OFSTREAM stale: %s — STALE allowlist entry (no raw file stream at this line; the use moved on a line shift or the site was migrated away — remove from allowlist).\n' \
                "$trimmed" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-raw-ofstream detected ${stale_count} stale allowlist entr(y/ies).
Each entry points at a path:line where no raw file stream remains — the
use moved (line shift) or the site was migrated away.  Prune the stale
entries from scripts/no-raw-ofstream-allowlist.txt so the guard regains
drift coverage and cannot re-grandfather a future file stream at the
same line.
HINT
    exit 2
fi

printf 'check-no-raw-ofstream: clean — no raw C++ file streams in band-3 dirs, no stale allowlist entries.\n' >&2
exit 0
