#!/usr/bin/env bash
# check-derived-pins.sh — a cardinality pin must have a derived operand.
#
# The pattern this guard kills:
#
#     inline constexpr int surface_cardinality = 28;
#     static_assert(surface_cardinality == 28, "the count and the list must move together");
#
# The constant and the literal are written by the same hand in the same edit,
# so the assertion compares a number to itself and can never fire.  The tree
# had 82 of these when the guard shipped, and one of them (SessGlobal.h: 28
# using-declarations, sentinel 27) proved the failure mode.  A pin is real only
# when one side is derived — a reflection query (`enumerators_of(^^E).size()`),
# a `tuple_size_v`, an array's `.size()`, a `sizeof` — so a real edit moves one
# side and not the other.
#
# Detection is textual: a `constexpr <type> NAME = <integer literal>;` followed
# within four lines by `static_assert(` whose condition compares NAME (either
# side) with `==`, `>=` or `<=` against the SAME literal.  A different literal,
# a derived right-hand side, or a `>=` floor against a smaller number is not
# flagged.
#
# Scope: include/, src/, test/, bench/, tools/, vessel/, minus the frozen old
# substrate and its fixtures (scripts/check-frozen-tree.sh lists them); those
# die with the old tree and are not worth allowlisting one by one.
#
# Allowlist: scripts/derived-pins-allowlist.txt, content-keyed as
# `path:NAME`.  An entry names a constant, not a line, so it survives edits
# above it.  A stale entry (no such pin left in the file) is exit 2.
#
# Exit status:
#   0 — clean
#   1 — at least one literal-vs-literal pin outside the allowlist
#   2 — stale allowlist entry, bad invocation, or self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-derived-pins.sh — reject a static_assert that pins a literal to itself.

Usage:
  check-derived-pins.sh              # scan; exit 1 on violation
  check-derived-pins.sh --self-test  # plant the pattern, verify catch
  check-derived-pins.sh -h | --help  # usage

Allowlist: scripts/derived-pins-allowlist.txt, one `path:NAME` per line.
USAGE
}

frozen_glob_args=(
    --glob '!include/crucible/safety/**'
    --glob '!include/crucible/fixy/**'
    --glob '!include/crucible/algebra/**'
    --glob '!include/crucible/effects/**'
    --glob '!include/crucible/permissions/**'
    --glob '!include/crucible/sessions/**'
    --glob '!include/crucible/bridges/**'
    --glob '!include/crucible/handles/**'
    --glob '!include/crucible/concurrent/**'
    --glob '!test/safety_neg/**'
    --glob '!test/fixy_neg/**'
    --glob '!test/effects_neg/**'
    --glob '!test/sessions_neg/**'
    --glob '!test/bridges_neg/**'
    --glob '!test/safety_attack/**'
    --glob '!build*/**'
)

# The candidate: `constexpr <type...> NAME = <integer literal>;`.  One
# ripgrep pass with four lines of trailing context finds every candidate and
# its window; bash then checks the window for a static_assert pinning NAME to
# the same literal.  No process is spawned per line.
candidate_pattern='(?<![A-Za-z0-9_])constexpr\s+(?:[A-Za-z_:][A-Za-z0-9_:<>]*\s+)+[A-Za-z_][A-Za-z0-9_]*\s*=\s*[0-9]+[uUlL]*\s*;'

# Prints `rel:line:NAME:LITERAL` for every literal-vs-literal pin under
# $1, allowlist-blind.
# A pin may sit up to this many lines below its declaration.  Three
# declarations followed by three asserts put the last pair six lines apart.
readonly WINDOW=6

find_pins() {
    local scan_root="$1"
    local line file lineno text rest i
    local -a dirs=()
    local d
    for d in include src test bench tools vessel; do
        [[ -d "$scan_root/$d" ]] && dirs+=("$d")
    done
    [[ "${#dirs[@]}" -eq 0 ]] && return 0

    # ERE, kept in variables so no operator is escaped inside [[ ]].
    local decl_re='(^|[^A-Za-z0-9_])constexpr[[:space:]]+([A-Za-z_:][A-Za-z0-9_:<>]*[[:space:]]+)+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*([0-9]+)[uUlL]*[[:space:]]*;'
    local cmp_re='(==|>=|<=)'

    # Open candidates: a declaration whose window is still filling.  Several
    # can be open at once, because consecutive declarations are match lines
    # for each other and would otherwise cut each other's window short.
    local -a c_file=() c_line=() c_name=() c_lit=() c_left=() c_win=()

    close_candidate() {  # $1 = index
        local pin_re="static_assert[[:space:]]*\\([[:space:]]*(${c_name[$1]}[[:space:]]*${cmp_re}[[:space:]]*${c_lit[$1]}([^0-9]|\$)|${c_lit[$1]}[[:space:]]*${cmp_re}[[:space:]]*${c_name[$1]}([^A-Za-z0-9_]|\$))"
        if [[ "${c_win[$1]}" =~ $pin_re ]]; then
            printf '%s:%s:%s:%s\n' "${c_file[$1]}" "${c_line[$1]}" "${c_name[$1]}" "${c_lit[$1]}"
        fi
    }
    close_all() {
        for ((i = 0; i < ${#c_file[@]}; i++)); do close_candidate "$i"; done
        c_file=(); c_line=(); c_name=(); c_lit=(); c_left=(); c_win=()
    }
    feed_open() {  # $1 = text; extend every open window, close the exhausted
        local -a keep_idx=()
        for ((i = 0; i < ${#c_file[@]}; i++)); do
            c_win[$i]+="$1"$'\n'
            c_left[$i]=$(( c_left[i] - 1 ))
            if (( c_left[i] <= 0 )); then close_candidate "$i"; else keep_idx+=("$i"); fi
        done
        local -a nf=() nl=() nn=() nt=() nx=() nw=()
        for i in "${keep_idx[@]:-}"; do
            [[ -z "$i" ]] && continue
            nf+=("${c_file[$i]}"); nl+=("${c_line[$i]}"); nn+=("${c_name[$i]}")
            nt+=("${c_lit[$i]}"); nx+=("${c_left[$i]}"); nw+=("${c_win[$i]}")
        done
        c_file=("${nf[@]:-}"); c_line=("${nl[@]:-}"); c_name=("${nn[@]:-}")
        c_lit=("${nt[@]:-}"); c_left=("${nx[@]:-}"); c_win=("${nw[@]:-}")
        # A rebuilt-empty array carries one empty element; drop it.  The
        # explicit return keeps a false test from being the function's exit
        # status, which under `set -e` would end the scan silently.
        if [[ "${#nf[@]}" -eq 0 ]]; then
            c_file=(); c_line=(); c_name=(); c_lit=(); c_left=(); c_win=()
        fi
        return 0
    }

    # rg is told to separate fields with \x01 on a match line and \x02 on a
    # context line, so a path containing ':' or '-' cannot confuse the parse.
    # Paths come out relative to $scan_root because the search runs there,
    # which is also what makes the frozen-tree globs apply.
    local m=$'\x01' c=$'\x02'
    while IFS= read -r line; do
        if [[ "$line" == "--" ]]; then
            close_all; continue
        fi
        if [[ "$line" == *"$m"* ]]; then
            file="${line%%"$m"*}"; rest="${line#*"$m"}"
            lineno="${rest%%"$m"*}"; text="${rest#*"$m"}"
            feed_open "$text"
            if [[ "$text" =~ $decl_re ]]; then
                c_file+=("$file"); c_line+=("$lineno")
                c_name+=("${BASH_REMATCH[3]}"); c_lit+=("${BASH_REMATCH[4]}")
                c_left+=("$WINDOW"); c_win+=("")
            fi
            continue
        fi
        if [[ "$line" == *"$c"* ]]; then
            rest="${line#*"$c"}"
            feed_open "${rest#*"$c"}"
        fi
    done < <(
        cd "$scan_root" && \
        rg -nHP -A"$WINDOW" --no-heading --type=cpp "${frozen_glob_args[@]}" \
           --field-match-separator="$m" --field-context-separator="$c" \
           "$candidate_pattern" "${dirs[@]}" 2>/dev/null || true
    )
    close_all
}

scan() {
    local scan_root="$1" allowlist="$2" rc=0
    local hit rel line name lit key
    local live_keys
    live_keys="$(mktemp)"
    while IFS= read -r hit; do
        [[ -z "$hit" ]] && continue
        rel="${hit%%:*}"; hit="${hit#*:}"
        line="${hit%%:*}"; hit="${hit#*:}"
        name="${hit%%:*}"; lit="${hit#*:}"
        key="$rel:$name"
        printf '%s\n' "$key" >>"$live_keys"
        if [[ -f "$allowlist" ]] && grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | grep -Fxq -- "$key"; then
            continue
        fi
        printf 'DERIVED-PIN violation: %s:%s — `%s = %s` is pinned against the literal %s, so the assertion cannot fire.  Derive one side (reflection, tuple_size_v, .size()).  Allowlist key: %s\n' \
            "$rel" "$line" "$name" "$lit" "$lit" "$key" >&2
        rc=1
    done < <(find_pins "$scan_root")

    # Stale entries.
    if [[ -f "$allowlist" ]]; then
        while IFS= read -r entry; do
            entry="${entry#"${entry%%[![:space:]]*}"}"
            case "$entry" in ''|'#'*) continue ;; esac
            entry="${entry%"${entry##*[![:space:]]}"}"
            if ! grep -Fxq -- "$entry" "$live_keys"; then
                printf 'DERIVED-PIN stale: %s — no literal-vs-literal pin with this name is left in the file; remove the entry.\n' "$entry" >&2
                [[ "$rc" -eq 0 ]] && rc=2
            fi
        done <"$allowlist"
    fi
    rm -f "$live_keys"
    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/foundation" "$tmp_root/include/crucible/fixy" "$tmp_root/scripts"
        cat >"$tmp_root/include/foundation/Planted.h" <<'PLANTED'
#pragma once
namespace foundation::planted {
inline constexpr int planted_tautology = 28;
static_assert(planted_tautology == 28, "planted — must be caught");

inline constexpr std::size_t planted_reversed = 7;
// a comment between the two lines is still within the window
static_assert(7 == planted_reversed, "planted reversed — must be caught");

inline constexpr int planted_allowlisted = 5;
static_assert(planted_allowlisted == 5, "planted — allowlisted, must not be reported");

inline constexpr int planted_derived = 3;
static_assert(planted_derived == std::tuple_size_v<Roster>, "derived operand — clean");

inline constexpr int planted_floor = 30;
static_assert(planted_floor >= 20, "different literal — a real floor, clean");

inline constexpr int planted_far = 9;
inline int a();
inline int b();
inline int c();
inline int d();
inline int e();
inline int f();
static_assert(planted_far == 9, "beyond the six-line window — not this guard's business");
}  // namespace foundation::planted
PLANTED
        cat >"$tmp_root/include/crucible/fixy/Frozen.h" <<'PLANTED'
#pragma once
inline constexpr int frozen_tautology = 4;
static_assert(frozen_tautology == 4, "frozen tree — excluded from the scan");
PLANTED
        cat >"$tmp_root/scripts/derived-pins-allowlist.txt" <<'ALLOW'
# self-test
include/foundation/Planted.h:planted_allowlisted
ALLOW
        out="$(mktemp)"
        fail() {
            printf 'check-derived-pins: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        }
        rc=0; scan "$tmp_root" "$tmp_root/scripts/derived-pins-allowlist.txt" 2>"$out" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "expected exit 1, got $rc"
        grep -qF 'planted_tautology' "$out" || fail "the plain tautology was not caught"
        grep -qF 'planted_reversed' "$out" || fail "the reversed-operand tautology was not caught"
        if grep -qF 'planted_allowlisted' "$out"; then fail "an allowlisted pin was reported"; fi
        if grep -qF 'planted_derived' "$out"; then fail "a derived-operand pin was flagged"; fi
        if grep -qF 'planted_floor' "$out"; then fail "a real floor against a different literal was flagged"; fi
        if grep -qF 'planted_far' "$out"; then fail "a pin outside the window was flagged"; fi
        if grep -qF 'frozen_tautology' "$out"; then fail "the frozen tree was scanned"; fi
        count="$(grep -c '^DERIVED-PIN violation' "$out" || true)"
        [[ "$count" -eq 2 ]] || fail "expected 2 violations, got $count"

        # Stale entry: allowlist a constant that no longer exists.
        cat >"$tmp_root/scripts/derived-pins-allowlist.txt" <<'ALLOW'
include/foundation/Planted.h:planted_allowlisted
include/foundation/Planted.h:planted_tautology
include/foundation/Planted.h:planted_reversed
include/foundation/Planted.h:planted_vanished
ALLOW
        rc=0; scan "$tmp_root" "$tmp_root/scripts/derived-pins-allowlist.txt" 2>"$out" || rc=$?
        [[ "$rc" -eq 2 ]] || fail "expected exit 2 on a stale entry, got $rc"
        grep -qF 'planted_vanished' "$out" || fail "the stale entry was not named"
        rm -f "$out"
        printf 'check-derived-pins: self-test passed — plain and reversed tautologies caught, derived / floor / far / frozen / allowlisted pins clean, stale entry flagged.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-derived-pins: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-derived-pins: ripgrep (rg) is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_DERIVED_PINS_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/derived-pins-allowlist.txt"
rc=0
scan "$scan_root" "$allowlist" || rc=$?

if [[ "$rc" -eq 1 ]]; then
    cat >&2 <<'HINT'

check-derived-pins: a static_assert compares a hand-written constant to the
same literal it was assigned from.  Derive one side: reflect the enum
(`std::meta::enumerators_of(^^E).size()`), read the tuple
(`std::tuple_size_v<Roster>`), or take the array's `.size()`.  If the count
has no derivable source, the pin is a comment; delete it rather than
allowlist it.
HINT
fi
exit "$rc"
