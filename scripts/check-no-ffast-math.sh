#!/usr/bin/env bash
# check-no-ffast-math.sh — fast-math-family flag ban enforcement (FIXY-V-094).
#
# CLAUDE.md §V "NEVER" matrix lists the fast-math family as
# determinism-breaking flags banned project-wide:
#
#   -ffast-math                    root umbrella — bundles 8 sub-flags
#   -funsafe-math-optimizations    global blanket disable
#   -fassociative-math             reorders FP — F101 trigger
#   -fno-signed-zeros              elides sign-of-zero — V-093 break
#   -ffinite-math-only             assumes no NaN/Inf — V-093 dead
#   -ffp-contract=fast             cross-statement FMA — F104 trigger
#   -freciprocal-math              x/y → x*(1/y) — vendor ULP drift
#
# Plus pragma-level overrides that bypass the build-system floor:
#   #pragma GCC optimize("fast-math")
#   __attribute__((optimize("fast-math")))
#   __attribute__((optimize("-ffast-math")))
#
# The `crucible_fp_strict` INTERFACE target (cmake/FpStrict.cmake)
# defines the build-system floor.  This guard catches any per-TU
# override that would punch through it.
#
# Two scan paths:
#
#   (a) Build files (CMakeLists.txt, *.cmake, *.json presets,
#       Makefile) — any of the deny-listed flags appearing here
#       would actually apply to compilation and break the floor.
#
#   (b) Source files (*.h, *.cpp, *.hpp, *.cc) — only `#pragma GCC
#       optimize` and `__attribute__((optimize(...)))` containing
#       the deny-listed flags can override the floor at TU level.
#       Plain occurrences inside string literals, comments, or
#       diagnostic messages are NOT flagged (the project documents
#       these flags' meanings in safety/CollisionCatalog.h and the
#       FpModeLattice.h doc-block by design).
#
# Per-line allowlist: scripts/no-ffast-math-allowlist.txt.  Each
# line is `path:line` (relative to project root).  Empty initially —
# every entry that ever gets added requires a documented rationale
# in the surrounding code.
#
# Inline suppression: `// NO-FFAST-MATH-OK: <reason>` on the call
# line exempts that single line (for the rare bench harness that
# *deliberately* wants to measure code under fast-math).
#
# Exempt directories: test/, bench/, examples/, fuzz/, third_party/.
# Bench targets that opt out of strict-FP do so by NOT linking
# `crucible_fp_strict`, not by setting flags per-TU.
#
# Exit status:
#   0 — clean (no NEW fast-math sites beyond the allowlist)
#   1 — at least one violation outside the allowlist
#   2 — bad invocation / missing dependency / stale allowlist entry

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-no-ffast-math.sh — fast-math-family flag ban enforcement.

Usage:
  check-no-ffast-math.sh              # scan; exit 1 on violation, 2 on stale
  check-no-ffast-math.sh --self-test  # plant violations across both axes,
                                      # verify each is caught
  check-no-ffast-math.sh -h | --help  # usage

Suppression:
  // NO-FFAST-MATH-OK: <reason>             on call line — exempts THIS line
  scripts/no-ffast-math-allowlist.txt:
    path:line                                — exempts the call at that line
    # comment                                — ignored

Banned patterns (build files + per-TU pragma overrides):
  -ffast-math                       -funsafe-math-optimizations
  -fassociative-math                -freciprocal-math
  -fno-signed-zeros                 -ffinite-math-only
  -ffp-contract=fast
  #pragma GCC optimize("...fast-math...")
  __attribute__((optimize("...fast-math...")))
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant TWO orthogonal violation classes:
        #
        #   (A) Build-file violation: a CMakeLists.txt with
        #       `-ffast-math` — the scanner MUST flag.
        #   (B) Source-file pragma violation: a header with
        #       `#pragma GCC optimize("fast-math")` — the scanner
        #       MUST flag.  But the SAME header MUST NOT flag the
        #       documentation strings that mention these flags in
        #       comments (safety/CollisionCatalog.h pattern).
        #
        # Plus one allowlisted entry and one inline-suppressed entry
        # to verify both exemption paths.  Plus one STALE allowlist
        # entry to verify stale detection.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/CMakeLists" \
                 "$tmp_root/include/crucible/planted" \
                 "$tmp_root/scripts"
        cat >"$tmp_root/CMakeLists/CMakeLists.txt" <<'PLANTED_CMAKE'
# Planted build-file violation — line 3 below is the catch.
add_compile_options(-ffast-math)
add_compile_options(-fassociative-math)  # FLAGGED line 3
# This documentation comment also names -ffast-math but is
# excluded — comment-line filter must catch it.
# NO-FFAST-MATH-OK: planted suppression on next line
add_compile_options(-funsafe-math-optimizations)  # SUPPRESSED line 7
# Path-substring negative case — the script name itself contains
# `-ffast-math` but it is part of a path, not a compiler flag.  A
# bad word-boundary regex (`-ffast-math\b`) would false-flag this
# line because `h.` IS a word boundary; the corrected regex
# requires the flag to terminate at whitespace/quote/paren/EOL.
add_test(NAME planted_path COMMAND bash scripts/check-no-ffast-math.sh)
PLANTED_CMAKE
        cat >"$tmp_root/include/crucible/planted/planted_pragma.h" <<'PLANTED_PRAGMA'
#pragma once
// Synthetic no-ffast-math fixture for --self-test.  This comment
// names -ffast-math and -fassociative-math in plain text — must
// NOT be flagged (comment lines are excluded from source-file
// scans).
#pragma GCC optimize("fast-math")    // FLAGGED line 6
inline double drift_double(double a, double b) {
    return a * b;
}
// Allowlisted second pragma — exempted via allowlist entry.
#pragma GCC optimize("fast-math")    // ALLOWLISTED line 11
inline double allowed_double(double a, double b) {
    return a * b;
}
// Inline-suppressed third pragma.
#pragma GCC optimize("fast-math")    // NO-FFAST-MATH-OK: synthetic
inline double suppressed_double(double a, double b) {
    return a * b;
}
PLANTED_PRAGMA
        cat >"$tmp_root/scripts/no-ffast-math-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry
include/crucible/planted/planted_pragma.h:11
# self-test stale entry (no pragma at this line)
include/crucible/planted/planted_pragma.h:99
ALLOW
        result_file="$(mktemp)"
        rc=0
        CRUCIBLE_NO_FFAST_MATH_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        # First sub-test: violations present → expect exit 1.
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — expected violation exit 1, got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The build-file violation MUST be flagged.
        if ! grep -qF 'CMakeLists/CMakeLists.txt:3' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — build-file flag not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The source-file pragma violation MUST be flagged.
        if ! grep -qF 'planted_pragma.h:6' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — source-pragma not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Allowlisted line MUST NOT be flagged.
        if grep -qF 'planted_pragma.h:11 — ' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Inline-suppressed line MUST NOT be flagged.
        if grep -qF 'planted_pragma.h:16 — ' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — NO-FFAST-MATH-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The doc-comment lines (mentioning the flag names in text)
        # MUST NOT be flagged.  Header has comments on lines 2, 3,
        # 4, 5, 10, 15 — any of those flagging means the comment
        # filter is broken.
        for forbidden_line in 2 3 4 5 10 15; do
            if grep -qF "planted_pragma.h:${forbidden_line} — " "$result_file"; then
                printf 'check-no-ffast-math: SELF-TEST FAILED — comment line %d leaked.\n' "$forbidden_line" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$result_file")" >&2
                rm -f "$result_file"
                exit 2
            fi
        done
        # Path-substring negative case (planted CMakeLists.txt:14).
        # A bad word-boundary regex (`-ffast-math\b`) would match the
        # `check-no-ffast-math.sh` path component and false-flag the
        # ctest entry; correct regex (`(?=$|[whitespace|quote|paren|EOL])`)
        # must NOT match.  Belt-and-braces guard: regression-prove the
        # bug doesn't return on a future refactor.
        if grep -qF 'CMakeLists/CMakeLists.txt:14 — ' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — path-substring case leaked (regression of build_file_pattern terminator).\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Second sub-test: remove both violations so only the stale
        # allowlist entry remains; expect exit 2.
        cat >"$tmp_root/CMakeLists/CMakeLists.txt" <<'PLANTED_CLEAN'
# Clean CMakeLists.txt
add_executable(planted_clean planted.cpp)
PLANTED_CLEAN
        cat >"$tmp_root/include/crucible/planted/planted_pragma.h" <<'PLANTED_CLEAN_HEADER'
#pragma once
// Clean header — no pragma overrides.
inline double clean_double(double a, double b) {
    return a * b;
}
PLANTED_CLEAN_HEADER
        cat >"$tmp_root/scripts/no-ffast-math-allowlist.txt" <<'ALLOW_STALE'
# self-test stale entry (no pragma at this line anymore)
include/crucible/planted/planted_pragma.h:99
ALLOW_STALE
        rc=0
        CRUCIBLE_NO_FFAST_MATH_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 2 ]]; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — stale-only pass expected exit 2, got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -qF 'STALE allowlist entry' "$result_file"; then
            printf 'check-no-ffast-math: SELF-TEST FAILED — stale diagnostic missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-no-ffast-math: self-test passed — build-file flags, source-pragma overrides, allowlist + inline marker + comment-filter + stale-detection all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-ffast-math: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_NO_FFAST_MATH_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/no-ffast-math-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-no-ffast-math: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Build-file pattern ───────────────────────────────────────────────
#
# Match any of the deny-listed flag strings appearing as a STANDALONE
# compiler argument in CMake / preset / Makefile artifacts.  Require
# the flag to be followed by end-of-line, whitespace, quote, comma,
# semicolon, or close-paren — NOT by `.` or alphanumeric.  Without
# this stricter terminator, `-ffast-math\b` matches the substring
# `-ffast-math.sh` inside the script's own path (`-ffast-math.sh` —
# `h.` is a word boundary), false-flagging every ctest entry that
# references this very script by name.
build_file_pattern='(\-ffast-math|\-funsafe-math-optimizations|\-fassociative-math|\-freciprocal-math|\-fno-signed-zeros|\-ffinite-math-only|\-ffp-contract=fast)(?=$|[[:space:]"'\'',;)\\])'

# ── Source-file pattern ──────────────────────────────────────────────
#
# Per-TU overrides bypass the build-system floor.  Match `#pragma
# GCC optimize(...)` AND `__attribute__((optimize(...)))` when the
# argument list mentions any fast-math flag/name.  Plain occurrences
# in strings / comments don't reach here because the comment filter
# below strips `//`-prefixed lines.
source_pragma_pattern='(#pragma[[:space:]]+GCC[[:space:]]+optimize|__attribute__\s*\(\s*\(\s*optimize)\s*\(\s*"[^"]*(fast-math|unsafe-math-optimizations|associative-math|reciprocal-math|finite-math-only|signed-zeros|ffp-contract=fast)'

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

# ── Phase 1: scan build files ────────────────────────────────────────
while IFS= read -r match; do
    [[ -z "$match" ]] && continue
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"
    case "$stripped" in
        '#'*) continue ;;     # CMake-comment line
    esac
    case "$text" in
        *'NO-FFAST-MATH-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"
    printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'NO-FFAST-MATH violation (build): %s:%s — fast-math-family flag banned (CLAUDE.md §V).\n' \
        "$rel" "$line" >&2
    violation_count=$((violation_count + 1))
done < <(
    rg -nP \
       --no-heading \
       --glob '*.cmake' --glob 'CMakeLists.txt' --glob '*.json' \
       --glob 'Makefile' --glob '*.mk' \
       --glob '!build/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       --glob '!test/**' \
       --glob '!bench/**' \
       --glob '!examples/**' \
       --glob '!fuzz/**' \
       "$build_file_pattern" "$scan_root" 2>/dev/null || true
)

# ── Phase 2: scan source files for pragma/attribute overrides ────────
while IFS= read -r match; do
    [[ -z "$match" ]] && continue
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac
    case "$text" in
        *'NO-FFAST-MATH-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"
    printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'NO-FFAST-MATH violation (pragma): %s:%s — per-TU fast-math override banned (CLAUDE.md §V; floor is `crucible_fp_strict`).\n' \
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
       --glob '!fuzz/**' \
       "$source_pragma_pattern" "$scan_root" 2>/dev/null || true
)

# ── Outcome (violations) ─────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    # Quoted heredoc (<<'HINT') suppresses ALL expansion, including
    # backtick command substitution.  The body contains literal
    # backticks around CMake target / macro names; without the quote
    # bash would try to execute `crucible_fp_strict` as a command.
    printf '\ncheck-no-ffast-math detected %d fast-math-family ' "$violation_count" >&2
    cat >&2 <<'HINT'
override(s) outside the allowlist.  CLAUDE.md §V "NEVER" list bans
the fast-math family because each flag in the family breaks at least
one Crucible numerical invariant:

  -ffast-math / -funsafe-math-optimizations
                          umbrella; one flag undoes the entire
                          strict-IEEE-754 discipline.

  -fassociative-math      reorders FP additions — V-091 F101
                          (DetSafe × FpMode replay drift).

  -fno-signed-zeros       elides sign-of-zero — V-093 ±0
                          canonicalization break.

  -ffinite-math-only      constexpr-folds std::isnan to false —
                          V-093 NaN canonicalization dead code.

  -ffp-contract=fast      cross-statement FMA — F104 (Vendor ×
                          FMA realization) bit-divergence.

  -freciprocal-math       x/y → x*(1/y); the reciprocal step
                          introduces a vendor-divergent ULP.

The build-system floor is the INTERFACE library `crucible_fp_strict`
(cmake/FpStrict.cmake) — PUBLIC-linked to `crucible` so every
consumer inherits it.  Do NOT override per-TU; do NOT add to
CMAKE_CXX_FLAGS.  Bench targets that need fast-math for measurement
opt out by NOT linking `crucible` (and document the rationale).

If the override is genuinely needed (rare):
  - Add an entry to scripts/no-ffast-math-allowlist.txt with a
    comment explaining WHY, then re-run this guard.
  - OR add `// NO-FFAST-MATH-OK: <reason>` inline on the call line.
HINT
    rm -f "$live_set_file"
    trap - EXIT
    exit 1
fi

# ── Stale-allowlist detection ────────────────────────────────────────
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        [[ -z "$entry" ]] && continue
        if ! grep -Fxq -- "$entry" "$live_set_file"; then
            printf 'check-no-ffast-math: STALE allowlist entry %s — no fast-math override at that line; remove from allowlist.\n' \
                "$entry" >&2
            stale_count=$((stale_count + 1))
        fi
    done < <(grep -E -v '^[[:space:]]*(#|$)' "$allowlist" || true)
fi

rm -f "$live_set_file"
trap - EXIT

if [[ "$stale_count" -ne 0 ]]; then
    printf 'check-no-ffast-math: %d stale allowlist entries — guard refuses to silently drift.\n' "$stale_count" >&2
    exit 2
fi

# Clean — silently exit 0.
exit 0
