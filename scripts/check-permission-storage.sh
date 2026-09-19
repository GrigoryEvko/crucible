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
// Synthetic PERMISSION-STORAGE fixture for --self-test verification.
// A bare Permission<Tag> member field is the documented antipattern
// (Permission.h doc-block); the canonical shape carries the
// no-unique-address attribute.  Each declarator carries a UNIQUE
// member name; the assertions resolve their line numbers by grepping
// for those names, so editing this fixture cannot silently desync the
// expectations (a line-keyed list would).
#pragma once
namespace crucible::planted {
struct tag_t {};
// FLAGGED — bare member, no marker anywhere in the window.
struct SharedAcrossThreads {
    ::crucible::safety::Permission<tag_t> flagged_bare_;
};
// CLEAN — canonical shape, attribute on the declarator line.
struct SameLineAttribute {
    [[no_unique_address]] ::crucible::safety::Permission<tag_t> clean_same_line_;
};
// CLEAN — clang-format pushed the declarator below the attribute, so
// the marker sits ABOVE the match (backward window).
struct WrappedAttribute {
    [[no_unique_address]]
    ::crucible::safety::Permission<tag_t> clean_wrapped_attr_;
};
// CLEAN — trailing suppression marker on the declarator line.
// Unreachable before the pattern dropped its end-of-line anchor.
struct TrailingSuppression {
    ::crucible::safety::Permission<tag_t> clean_trailing_;  // PERMISSION-STORAGE-OK: synthetic
};
// FLAGGED — a LATER sibling's attribute must not leak backwards onto
// this bare member.
struct LeakProbe {
    ::crucible::safety::Permission<tag_t> flagged_leak_probe_;
    [[no_unique_address]] ::crucible::safety::Permission<tag_t> clean_sibling_;
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
        # Per-shape assertions.  The bare members (lines 10, 30) MUST be
        # flagged; every marked shape (same-line attribute 14, wrapped
        # attribute 20, trailing suppression 25, later sibling 31) MUST
        # NOT be.  Without these the window walk could regress to
        # flag-everything or suppress-everything and still "pass".
        selftest_fail() {
            printf 'check-permission-storage: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }
        planted_file="$tmp_root/include/crucible/test_planted/violation.h"
        line_of() {
            grep -n -- "$1" "$planted_file" | head -1 | cut -d: -f1
        }
        for member in flagged_bare_ flagged_leak_probe_; do
            expect_line="$(line_of "$member")"
            grep -q "violation.h:${expect_line} " "$result_file" || \
                selftest_fail "bare member ${member} (line ${expect_line}) not flagged."
        done
        for member in clean_same_line_ clean_wrapped_attr_ clean_trailing_ clean_sibling_; do
            clean_line="$(line_of "$member")"
            if grep -q "violation.h:${clean_line} " "$result_file"; then
                selftest_fail "marked declaration ${member} (line ${clean_line}) leaked through the window walk."
            fi
        done
        rm -f "$result_file"
        printf 'check-permission-storage: self-test passed — bare members flagged; same-line, wrapped-above, trailing-comment and later-sibling markers all honoured.\n' >&2
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
# The declarator terminator is the `;`, NOT end-of-line.  Anchoring on
# `;\s*$` made the inline `// PERMISSION-STORAGE-OK:` marker
# UNREACHABLE: the trailing comment pushes the `;` off end-of-line, so
# the scanner never saw a marked line and the suppression branch was
# dead code.  A single-line `struct S { Permission<T> p_; };` was
# likewise invisible.  Accept an optional trailing comment or brace
# after the `;` so both shapes reach the window walk below.
banned_pattern='\bPermission<.+?>\s+\w+_?\s*;\s*(//.*|/\*.*|\}.*)?$'

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
        include/crucible/permissions/Permission.h | \
        include/foundation/permissions/Permission.h) continue ;;
    esac

    # Skip comment-only lines (pure prose mentions of the spelling).
    stripped="${text#"${text%%[![:space:]]*}"}"
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Discipline markers — `[[no_unique_address]]` (the canonical
    # handle-storage pattern) and `// PERMISSION-STORAGE-OK: <reason>`
    # (inline suppression).  BOTH scope to the member DECLARATION, not
    # to the line: clang-format may wrap a long declaration so the
    # attribute lands on the line ABOVE the `Permission<` token and the
    # trailing comment lands on a line BELOW it.  Line-scoped matching
    # breaks on the next format pass — a failure mode that has already
    # recurred four times across this guard family (check-syscall-
    # capability.sh, check-row-contains-discipline.sh,
    # check-lock-free-asserts.sh, check-fixy-discipline.sh all walk a
    # 12-line statement window for exactly this reason).
    #
    # Walk the declaration in both directions, bounded at 12 lines each
    # way.  A line bearing ';' or ending in '{' / '}' terminates the
    # statement, so a NEIGHBOURING declaration's marker cannot leak in.
    ps_suppressed=0

    # Backward: the attribute precedes the declarator.  Stop one line
    # past the end of the previous statement.
    ps_probe=$((line - 1))
    ps_floor=$((line - 12))
    (( ps_floor < 1 )) && ps_floor=1
    while (( ps_probe >= ps_floor )); do
        ps_text="$(sed -n "${ps_probe}p" "$file" 2>/dev/null)"
        case "$ps_text" in
            *'[[no_unique_address]]'*|*'PERMISSION-STORAGE-OK'*)
                ps_suppressed=1; break ;;
        esac
        case "$ps_text" in
            *';'*) break ;;
        esac
        case "${ps_text%"${ps_text##*[![:space:]]}"}" in
            *'{'|*'}') break ;;
        esac
        ps_probe=$((ps_probe - 1))
    done

    # Forward: the trailing comment follows the declarator.  Include the
    # match line itself, and stop at the line that ends the statement.
    if (( ! ps_suppressed )); then
        ps_probe=$line
        ps_limit=$((line + 12))
        while (( ps_probe <= ps_limit )); do
            ps_text="$(sed -n "${ps_probe}p" "$file" 2>/dev/null)"
            case "$ps_text" in
                *'[[no_unique_address]]'*|*'PERMISSION-STORAGE-OK'*)
                    ps_suppressed=1; break ;;
            esac
            case "$ps_text" in
                *';'*) break ;;
            esac
            case "${ps_text%"${ps_text##*[![:space:]]}"}" in
                *'}') break ;;
            esac
            ps_probe=$((ps_probe + 1))
        done
    fi

    (( ps_suppressed )) && continue

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
       --glob '!build*/**' \
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
