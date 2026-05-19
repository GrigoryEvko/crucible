#!/usr/bin/env bash
# check-permission-storage.sh — Permission<Tag> long-lived-struct
# antipattern guard (fixy-L-03 #1519,
# include/crucible/permissions/Permission.h:144-148 doc-block).
#
# ─── Background ──────────────────────────────────────────────────────
#
# CSL Permission<Tag> tokens are linear (deleted copy, [[nodiscard]],
# sizeof = 1).  Storing a Permission as a struct member is FINE when
# the enclosing class IS a handle that proves single-ownership through
# lifetime semantics (Pinned + move-only + nested inside a channel /
# session class).  The canonical handle storage shape is:
#
#     class FooHandle {
#         FooState& state_;
#         [[no_unique_address]] safety::Permission<tag> perm_;
#         ...
#     };
#
# The `[[no_unique_address]]` attribute is the OBSERVABLE marker that
# the storage is intentional: it (a) collapses the 1-byte Permission
# to 0 bytes via EBO and (b) announces "this is the handle-storage
# pattern".  A bare `Permission<Tag> p_;` without the attribute is the
# documented antipattern: the holder may be aliased across threads
# without the type system catching it (CSL frame rule defeated —
# Permission.h:144-148, CLAUDE.md §IX, fixy-L-03).
#
# ─── Rule ────────────────────────────────────────────────────────────
#
# Every `Permission<*>` declared as a class/struct member field MUST
# be on a line containing `[[no_unique_address]]`.  Function
# parameters, local variables, return values, and friend declarations
# are unaffected — the regex matches only the member-decl shape
# (`Permission<...> name;` at end-of-line).
#
# ─── Suppression ─────────────────────────────────────────────────────
#
#  * Inline marker on the violating line:
#       // PERMISSION-STORAGE-OK: <reason>
#  * Allowlist file: scripts/permission-storage-allowlist.txt
#       one repo-relative "path:line" per entry, '#' begins a comment.
#  * Permission.h itself is exempt — that's the substrate definition;
#    its own template parameter references aren't member declarations.
#
# ─── Exit status ─────────────────────────────────────────────────────
#
#   0 — clean OR ignorable matches only (no real violations)
#   1 — at least one violation
#   2 — bad invocation / missing dependency / self-test regression

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-permission-storage.sh — Permission<Tag> long-lived-struct guard.

Usage:
  check-permission-storage.sh              # scan; exit 1 on violation
  check-permission-storage.sh --self-test  # plant a violation, verify catch
  check-permission-storage.sh -h | --help  # this message

Suppression:
  // PERMISSION-STORAGE-OK: <reason>             inline marker
  scripts/permission-storage-allowlist.txt        repo-relative "path:line"

Rule:
  Every `Permission<*>` declared as a class/struct member MUST be on
  a line containing `[[no_unique_address]]`.  See script docstring or
  include/crucible/permissions/Permission.h:144-148.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant a synthetic violation in a temp tree and re-run the
        # scanner scoped to that tree.  Failure here means the regex
        # broke (rg upgrade, anchor drift, allowlist-path regression)
        # — a regex that never matches anything is a placebo, not a
        # guard.  Mirrors check-fixy-discipline.sh's --self-test
        # discipline.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/test_planted" \
                 "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/test_planted/violation.h" <<'PLANTED'
// Synthetic PERMISSION-STORAGE violation for --self-test verification.
// The canonical handle storage shape uses [[no_unique_address]]; a
// bare Permission<Tag> member field is the documented antipattern
// (Permission.h:144-148).
#pragma once
namespace crucible::planted {
struct tag_t {};
struct SharedAcrossThreads {
    ::crucible::safety::Permission<tag_t> perm_;
};
}  // namespace crucible::planted
PLANTED
        # Empty allowlist so the planted file is NOT exempted.
        : >"$tmp_root/scripts/permission-storage-allowlist.txt"
        result_file="$(mktemp)"
        if CRUCIBLE_PERMISSION_STORAGE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-permission-storage: SELF-TEST FAILED — planted violation was not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -q 'PERMISSION-STORAGE violation.*violation.h' "$result_file"; then
            printf 'check-permission-storage: SELF-TEST FAILED — diagnostic missing the planted file.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-permission-storage: self-test passed — regex fires on planted violation.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-permission-storage: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

scan_root="${CRUCIBLE_PERMISSION_STORAGE_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/permission-storage-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-permission-storage: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Detection regex ─────────────────────────────────────────────────
#
# Match the member-declaration shape:
#   <whitespace> [optional ::qualifier] Permission<...> <name>;
#
# `\bPermission<.+?>` — non-greedy template-arg capture so nested
# templates like `Permission<FederatedPeer<Org>>` match correctly.
# `\s+\w+_?\s*;` — identifier name + optional trailing underscore +
# semicolon.  `\s*$` anchors to end-of-line, excluding function
# parameter list lines (which continue with `,` or `)`), local
# initializers (which have `=`), and return expressions (which use
# `(...)` / `{...}` after the name).
#
# Friend declarations (`friend Permission<Tag>;` with no name) are
# excluded because the name-capture `\w+_?` requires an identifier.
banned_pattern='\bPermission<.+?>\s+\w+_?\s*;\s*$'

scan_paths=()
for p in include src; do
    abs="$scan_root/$p"
    if [[ -d "$abs" ]]; then
        scan_paths+=("$abs")
    fi
done

if [[ ${#scan_paths[@]} -eq 0 ]]; then
    printf 'check-permission-storage: no include/ or src/ under %s — nothing to scan.\n' "$scan_root" >&2
    exit 0
fi

violation_count=0

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    rel="${file#"$scan_root/"}"

    # Substrate exempt — Permission.h's own internal references aren't
    # member declarations and the doc-block already lists the
    # antipattern.
    case "$rel" in
        include/crucible/permissions/Permission.h) continue ;;
    esac

    # Skip comment-only lines (pure prose mentions of the spelling).
    stripped="${text#"${text%%[![:space:]]*}"}"
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # The canonical handle-storage pattern.
    if [[ "$text" == *'[[no_unique_address]]'* ]]; then
        continue
    fi

    # Inline suppression marker.
    if [[ "$text" == *'PERMISSION-STORAGE-OK'* ]]; then
        continue
    fi

    # Per-line allowlist (repo-relative "path:line"; exact match).
    if [[ -f "$allowlist" ]] && grep -Fxq -- "$rel:$line" "$allowlist"; then
        continue
    fi

    printf 'PERMISSION-STORAGE violation: %s:%s — Permission<*> member without [[no_unique_address]].\n' \
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

if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-permission-storage detected ${violation_count} long-lived-struct
storage site(s).  Each site declares a Permission<*> as a class/struct
member without the [[no_unique_address]] discipline marker.  This is
the CSL frame-rule antipattern documented at
include/crucible/permissions/Permission.h:144-148: holders of a bare
Permission<Tag> member may be aliased across threads without the type
system catching it (Permission is move-only at the value level, but
the type system can't see pointer-based escape through struct
sharing).

Three remediations:

  (1) Wrap the storage in a handle class (Pinned + move-only +
      nested-class pattern, mirror PermissionedSpscChannel /
      PermissionedMpscChannel / SwmrSession / KernelCacheSlot etc.)
      and add [[no_unique_address]] to the member declaration.  This
      is the canonical fix.
  (2) If the site is a deliberate single-threaded design (e.g. a
      compile-time witness fixture), annotate the violating line with
      '// PERMISSION-STORAGE-OK: <reason>' so review can trace the
      decision back to the suppression.
  (3) If the file is pre-fixy code awaiting a tracked migration, add
      its repo-relative "path:line" entry to
      scripts/permission-storage-allowlist.txt with a TODO referencing
      the migration task.  Trim the allowlist as files migrate.
HINT
    exit 1
fi

printf 'check-permission-storage: clean — every Permission<*> member uses [[no_unique_address]].\n' >&2
exit 0
