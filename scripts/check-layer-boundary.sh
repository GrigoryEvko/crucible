#!/usr/bin/env bash
# check-layer-boundary.sh — the three-layer rule, stated in the positive.
#
# The tree has three layers with three namespace roots and three include
# roots, and each layer may name only the roots at or below it:
#
#   include/foundation/  src/foundation/   names  foundation::            <foundation/...>
#   include/fixy/        src/fixy/os/      names  fixy:: foundation::     <fixy/...> <foundation/...>
#   include/crucible/    src/**            names  crucible:: fixy:: foundation::  (any project root)
#
# std:: and every system header are outside the rule.  The rule is stated as
# what a file MAY name, so a fourth project root added later is a violation
# everywhere until this table admits it — the guard cannot be widened by
# forgetting to list something.
#
# Two things are scanned per file: `#include <root/...>` lines, and every
# `root::` qualified name in code.  Trailing `//` comments are stripped before
# the name scan and full-line comments are skipped, so prose may cite the
# layer above ("ported from crucible::algebra") without tripping the guard.
#
# There is no allowlist.  A file that needs a name from a higher layer is in
# the wrong layer.
#
# Exit status:
#   0 — clean
#   1 — at least one violation
#   2 — bad invocation / missing dependency / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-layer-boundary.sh — the three-layer rule (foundation < fixy < crucible).

Usage:
  check-layer-boundary.sh              # scan; exit 1 on violation
  check-layer-boundary.sh --self-test  # plant violations, verify catch
  check-layer-boundary.sh -h | --help  # usage
USAGE
}

# All project roots.  A name `X::` or an include `<X/` is a project-root
# reference iff X is in this list.
project_roots='foundation|fixy|crucible'

# What each layer may name.  The layer of a file is decided by its path.
allowed_for_layer() {
    case "$1" in
        foundation) printf 'foundation' ;;
        fixy)       printf 'foundation|fixy' ;;
        crucible)   printf 'foundation|fixy|crucible' ;;
        *)          return 1 ;;
    esac
}

layer_of_path() {
    # $1 = path relative to the scan root.  Prints the layer or nothing.
    case "$1" in
        include/foundation/*|src/foundation/*) printf 'foundation' ;;
        include/fixy/*|src/fixy/os/*)          printf 'fixy' ;;
        include/crucible/*|src/*)              printf 'crucible' ;;
        *)                                     return 0 ;;
    esac
}

# The crucible layer may name every root, so only the two lower layers can
# violate and only they are scanned.  One ripgrep pass finds every line that
# mentions a project root at all; bash then strips the comment part and asks,
# per root, whether that root is allowed for the file's layer.  No process is
# spawned per line.
candidate_pattern="(?<![A-Za-z0-9_])(${project_roots})::|#include\s*<(${project_roots})/"

scan() {
    local scan_root="$1" rc=0
    local match file rest lineno text rel layer allowed stripped code hit
    local -a dirs=()
    local d
    for d in include/foundation src/foundation include/fixy src/fixy/os; do
        [[ -d "$scan_root/$d" ]] && dirs+=("$scan_root/$d")
    done
    [[ "${#dirs[@]}" -eq 0 ]] && return 0

    while IFS= read -r match; do
        file="${match%%:*}"
        rest="${match#*:}"
        lineno="${rest%%:*}"
        text="${rest#*:}"
        rel="${file#"$scan_root"/}"
        layer="$(layer_of_path "$rel")"
        [[ -z "$layer" ]] && continue
        allowed="$(allowed_for_layer "$layer")"

        stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'*'*|'/*'*) continue ;;
        esac
        code="${text%%//*}"

        # Include roots.
        if [[ "$code" =~ \#include[[:space:]]*\<(foundation|fixy|crucible)/ ]]; then
            hit="${BASH_REMATCH[1]}"
            if ! [[ "|$allowed|" == *"|$hit|"* ]]; then
                printf 'LAYER violation: %s:%s — a %s file includes <%s/...>, which is above its layer.\n' \
                    "$rel" "$lineno" "$layer" "$hit" >&2
                rc=1
            fi
        fi

        # Namespace roots.  A root counts only when it is not preceded by an
        # identifier character, so `my_fixy::` and `crucible_flags::` do not.
        for hit in foundation fixy crucible; do
            if [[ "$code" =~ (^|[^A-Za-z0-9_])${hit}:: ]] && ! [[ "|$allowed|" == *"|$hit|"* ]]; then
                printf 'LAYER violation: %s:%s — a %s file names %s::, which is above its layer.\n' \
                    "$rel" "$lineno" "$layer" "$hit" >&2
                rc=1
            fi
        done
    done < <(
        rg -nP --no-heading --type=cpp --glob '!build*/**' \
           "$candidate_pattern" "${dirs[@]}" 2>/dev/null | sort || true
    )
    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/foundation/algebra" "$tmp_root/include/fixy" \
                 "$tmp_root/include/crucible" "$tmp_root/src/foundation"

        # 1. A foundation header that names crucible:: in code — FLAGGED.
        cat >"$tmp_root/include/foundation/algebra/Planted.h" <<'PLANTED'
#pragma once
// Prose may mention crucible::algebra freely.  This line must not be flagged.
namespace foundation::algebra {
using Bad = ::crucible::Arena;  // planted_up_reference
inline int fine() { return 1; }  // trailing comment naming fixy::fn is fine
}  // namespace foundation::algebra
PLANTED
        # 2. A fixy header including <foundation/...> and naming foundation:: — CLEAN.
        cat >"$tmp_root/include/fixy/Good.h" <<'PLANTED'
#pragma once
#include <foundation/algebra/Planted.h>
namespace fixy {
using Ok = ::foundation::algebra::Bad;
}  // namespace fixy
PLANTED
        # 3. A fixy header including <crucible/...> — FLAGGED.
        cat >"$tmp_root/include/fixy/BadInclude.h" <<'PLANTED'
#pragma once
#include <crucible/Arena.h>
namespace fixy {}
PLANTED
        # 4. A crucible header naming everything — CLEAN.
        cat >"$tmp_root/include/crucible/Top.h" <<'PLANTED'
#pragma once
#include <fixy/Good.h>
#include <foundation/Platform.h>
namespace crucible {
using A = ::fixy::Ok;
using B = ::foundation::algebra::Bad;
}  // namespace crucible
PLANTED
        # 5. A foundation .cpp naming fixy:: — FLAGGED; and an identifier that
        #    merely ends in a root name — CLEAN.
        cat >"$tmp_root/src/foundation/Planted.cpp" <<'PLANTED'
#include <foundation/Platform.h>
namespace my_fixy { struct X {}; }
static my_fixy::X planted_lookalike;  // must NOT be flagged
static int planted_bad = fixy::value;  // planted_src_up_reference
PLANTED

        out="$(mktemp)"
        rc=0
        scan "$tmp_root" 2>"$out" || rc=$?
        fail() {
            printf 'check-layer-boundary: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"
            exit 2
        }
        [[ "$rc" -eq 1 ]] || fail "expected exit 1 on the planted tree, got $rc"
        grep -qF 'include/foundation/algebra/Planted.h:4' "$out" || fail "foundation header naming crucible:: not caught"
        grep -qF 'include/fixy/BadInclude.h:2' "$out" || fail "fixy header including <crucible/...> not caught"
        grep -qF 'src/foundation/Planted.cpp:4' "$out" || fail "foundation .cpp naming fixy:: not caught"
        if grep -qF 'include/foundation/algebra/Planted.h:2' "$out"; then fail "a full-line comment was flagged"; fi
        if grep -qF 'include/foundation/algebra/Planted.h:5' "$out"; then fail "a trailing comment was flagged"; fi
        if grep -qF 'include/fixy/Good.h' "$out"; then fail "a fixy header naming foundation:: was flagged"; fi
        if grep -qF 'include/crucible/Top.h' "$out"; then fail "a crucible header was flagged"; fi
        if grep -qF 'src/foundation/Planted.cpp:3' "$out"; then fail "an identifier ending in a root name was flagged"; fi
        count="$(grep -c '^LAYER violation' "$out" || true)"
        [[ "$count" -eq 3 ]] || fail "expected exactly 3 violations, got $count"
        rm -f "$out"
        printf 'check-layer-boundary: self-test passed — up-references caught in headers and sources, comments and lookalike identifiers ignored, downward references clean.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-layer-boundary: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-layer-boundary: ripgrep (rg) is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_LAYER_TEST_ROOT:-$root}"
rc=0
scan "$scan_root" || rc=$?

if [[ "$rc" -ne 0 ]]; then
    cat >&2 <<'HINT'

check-layer-boundary: a file names a layer above its own.  The order is
foundation < fixy < crucible.  Move the file up, or move the thing it names
down.  There is no allowlist for this guard.
HINT
    exit 1
fi

printf 'check-layer-boundary: clean — every file names only its own layer and the layers below it.\n' >&2
exit 0
