#!/usr/bin/env bash
# check-lock-free-asserts.sh — cross-thread atomic lock-free assert
# discipline enforcement (FIXY-U-086).
#
# CLAUDE.md §IX + fixy-A5-029: every `std::atomic<T>` reference in
# a cross-thread substrate header MUST be accompanied by a sibling
# `static_assert(std::atomic<T>::is_always_lock_free)`.  Rationale:
#
#   (1) Silent libatomic fallback: on platforms where `std::atomic<T>`
#       is NOT lock-free for the chosen T (e.g. uint128_t on ARM
#       Apple Silicon without 16-byte CAS), libatomic silently
#       inserts a mutex.  The single-instruction acquire/release
#       turns into a 200-300 ns lock acquire — invisible at compile
#       time, fatal at production scale.
#
#   (2) Cross-platform consistency: x86_64 GCC may inline atomic<int>
#       to a single MOV while ARM64 falls back to LL/SC pairs that
#       fail under contention.  The assert catches the "works on
#       my Tiger Lake, mutex-locks on prod Graviton 4" class.
#
#   (3) The assert is FREE at runtime — it fires only at compile
#       time on a platform that lacks the lock-free guarantee.
#       The cost of ENFORCING the discipline is therefore zero;
#       the cost of FORGETTING it is a silent perf cliff that
#       only manifests in production on a different microarch.
#
# Scope: include/crucible/{canopy,cntp,topology,warden}/ — the
# distributed-stack substrate where ALL atomics are by-design
# cross-thread.  Hot-path SPSC rings (TraceRing.h), one-shot
# flags (handles/OneShotFlag.h), and other tighter-scoped
# atomics live outside this scan and are covered by their own
# discipline (the SPSC ring author documents thread ownership
# directly; the one-shot flag's correctness is single-thread).
# Extending the scan into mimic/ forge/ observe/ and the top-level
# Vigil.h / MerkleDag.h / concurrent/* sites is tracked as a
# follow-up under FIXY-U-029-EXT — those headers may have legitimate
# atomic-T sites that are deliberately not cross-thread, and the
# scan would need a finer-grained per-field discipline marker.
#
# Per-file allowlist: scripts/no-lock-free-asserts-allowlist.txt.
# Each line is `path::type` (path relative to project root, type
# in normalized form: std:: stripped, whitespace stripped).
# Comment lines start with `#`.  Empty lines ignored.
#
# Inline suppression: `// LOCK-FREE-OK: <reason>` on the atomic
# declaration line exempts that single declaration.  Use sparingly,
# only for atomics that genuinely don't need the assert (e.g. a
# debug-only counter that's never cross-thread under -DNDEBUG).
#
# Exempt directories: test/, bench/, examples/ — fixtures may
# deliberately plant the pattern to demonstrate rejection.
#
# Exit status:
#   0 — clean (every atomic-T has a sibling assert)
#   1 — at least one missing assert outside the allowlist
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-lock-free-asserts.sh — sibling-assert discipline for std::atomic.

Usage:
  check-lock-free-asserts.sh              # scan; exit 1 on violation
  check-lock-free-asserts.sh --self-test  # plant violation, verify catch
  check-lock-free-asserts.sh -h | --help  # usage

Suppression:
  // LOCK-FREE-OK: <reason>                on decl line — exempts THIS atomic
  scripts/no-lock-free-asserts-allowlist.txt:
    path::type                             — exempts the (file, type) pair
    # comment                              — ignored
USAGE
}

# ── Normalize a type string for set-equality ─────────────────────────
# Removes whitespace and strips leading `std::` so that `uint64_t`,
# `std::uint64_t`, and `std :: uint64_t` all hash to the same key.
normalize_type() {
    local t="$1"
    # Strip ALL whitespace (parameter expansion + extglob-free trick).
    local cleaned=""
    local i char
    for ((i = 0; i < ${#t}; ++i)); do
        char="${t:i:1}"
        case "$char" in
            ' '|$'\t'|$'\n') ;;
            *) cleaned+="$char" ;;
        esac
    done
    # Strip leading "std::" (canonical normalization).
    printf '%s' "${cleaned#std::}"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/canopy" "$tmp_root/scripts"

        # File A: declares atomic<MissingAssertT> with NO sibling assert
        # for that type.  Must be flagged.  Also declares atomic<uint64_t>
        # WITH a matching assert (covered).  Also has one inline-suppressed
        # declaration that must NOT be flagged.
        cat >"$tmp_root/include/crucible/canopy/planted.h" <<'PLANTED'
#pragma once
#include <atomic>
#include <cstdint>
namespace crucible::planted {
struct Foo {
    std::atomic<std::uint64_t> covered_{0};
    std::atomic<int> missing_assert_{0};
    std::atomic<long> suppressed_{0};  // LOCK-FREE-OK: synthetic --self-test inline marker
    std::atomic<short> allowlisted_{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "covered");
}  // namespace crucible::planted
PLANTED
        # Allowlist entry exempts the (file, short) pair.
        cat >"$tmp_root/scripts/no-lock-free-asserts-allowlist.txt" <<'ALLOW'
# self-test grandfathered (file, type) pair
include/crucible/canopy/planted.h::short
ALLOW

        result_file="$(mktemp)"
        if CRUCIBLE_LOCK_FREE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-lock-free-asserts: SELF-TEST FAILED — planted missing assert not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The `missing_assert_` line MUST be flagged (atomic<int>).
        if ! grep -qF 'planted.h' "$result_file" || ! grep -qF '::int' "$result_file"; then
            printf 'check-lock-free-asserts: SELF-TEST FAILED — expected diagnostic for atomic<int> missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The covered uint64_t MUST NOT be flagged.
        if grep -qE '::std::uint64_t|::uint64_t' "$result_file"; then
            printf 'check-lock-free-asserts: SELF-TEST FAILED — covered uint64_t leaked through.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed atomic<long> MUST NOT be flagged.
        if grep -qF '::long' "$result_file"; then
            printf 'check-lock-free-asserts: SELF-TEST FAILED — inline LOCK-FREE-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted atomic<short> MUST NOT be flagged.
        if grep -qF '::short' "$result_file"; then
            printf 'check-lock-free-asserts: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-lock-free-asserts: self-test passed — missing-assert caught, allowlist + inline marker + coverage all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-lock-free-asserts: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_LOCK_FREE_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/no-lock-free-asserts-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-lock-free-asserts: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Allowlist lookup ─────────────────────────────────────────────────
# Allowlist format: `path::normalized_type` per line.
allowlisted() {
    local rel="$1" type="$2"
    [[ -f "$allowlist" ]] || return 1
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel::$type"
}

violation_count=0

# Discover files to scan.
mapfile -t files < <(
    find "$scan_root/include/crucible/canopy" \
         "$scan_root/include/crucible/cntp" \
         "$scan_root/include/crucible/topology" \
         "$scan_root/include/crucible/warden" \
         -type f \( -name '*.h' -o -name '*.hpp' \) 2>/dev/null | sort
)

for file in "${files[@]}"; do
    rel="${file#"$scan_root"/}"

    # First pass: collect asserted types (the discipline being checked).
    declare -A asserted_in_file=()
    while IFS= read -r line; do
        # Extract type between < and >::is_always_lock_free.
        type_part="${line#*std::atomic<}"
        type="${type_part%%>::is_always_lock_free*}"
        normalized="$(normalize_type "$type")"
        asserted_in_file["$normalized"]=1
    done < <(
        rg -nP 'static_assert\s*\(\s*std::atomic<[^<>]+>::is_always_lock_free' \
            --no-heading --no-line-number "$file" 2>/dev/null || true
    )

    # Second pass: scan each declaration site.
    while IFS= read -r match; do
        # match looks like "linenum:full-line-text"
        line_num="${match%%:*}"
        text="${match#*:}"

        # Strip leading whitespace for comment / suppression checks.
        stripped="${text#"${text%%[![:space:]]*}"}"

        # Skip comment lines outright.
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
        esac

        # Skip lines that are themselves the static_assert (those are
        # the assertion sites — we don't want to flag the discipline
        # marker as "missing its own coverage").
        case "$text" in
            *'static_assert'*'is_always_lock_free'*) continue ;;
        esac

        # Inline suppression — `// LOCK-FREE-OK: <reason>`.
        case "$text" in
            *'LOCK-FREE-OK'*) continue ;;
        esac

        # Extract the type between `std::atomic<` and the matching `>`.
        # Simple capture: everything up to the first `>` after `<`.
        type_part="${text#*std::atomic<}"
        type="${type_part%%>*}"

        # Empty / malformed parse — skip.
        [[ -z "$type" ]] && continue

        normalized="$(normalize_type "$type")"

        # Coverage check: does this file have a matching assert?
        if [[ -n "${asserted_in_file[$normalized]:-}" ]]; then
            continue
        fi

        # Allowlist check.
        if allowlisted "$rel" "$normalized"; then
            continue
        fi

        printf 'LOCK-FREE-MISSING: %s:%s::%s — std::atomic<%s> declared without sibling static_assert(std::atomic<%s>::is_always_lock_free) — fixy-A5-029 / FIXY-U-086 discipline.\n' \
            "$rel" "$line_num" "$normalized" "$type" "$type" >&2
        violation_count=$((violation_count + 1))
    done < <(
        rg -nP 'std::atomic<[^<>]+>' \
           --no-heading "$file" 2>/dev/null || true
    )

    unset asserted_in_file
done

# ── Outcome ──────────────────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-lock-free-asserts detected ${violation_count} atomic-T declaration
site(s) without a sibling static_assert(std::atomic<T>::is_always_lock_free)
in canopy/cntp/topology/warden.

Why this matters: libatomic silently inserts a mutex when the platform
lacks a true lock-free atomic for T.  An x86_64 dev box hides the
fallback; production Graviton 4 turns the hot-path into a 200-300 ns
lock acquire.  The static_assert is free at runtime and fires at
compile time on the offending platform.

Remediations, in order of preference:

  (1) Add 'static_assert(std::atomic<T>::is_always_lock_free,
      "...")'' near the declarations of that type — one assert per
      distinct T covers all atomic<T> sites in the same file.

  (2) Annotate the line with '// LOCK-FREE-OK: <reason>' for
      structurally-justified exceptions (e.g. atomic used only
      for compile-time-deduced single-thread fast path).

  (3) Add 'path::type' to scripts/no-lock-free-asserts-allowlist.txt
      for grandfathered sites awaiting a tracked migration — every
      entry is a TODO referencing its fixy-A5-* ticket.
HINT
    exit 1
fi

printf 'check-lock-free-asserts: clean — every atomic-T in canopy/cntp/topology/warden has a sibling lock-free assert.\n' >&2
exit 0
