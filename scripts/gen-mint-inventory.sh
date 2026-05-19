#!/usr/bin/env bash
# gen-mint-inventory.sh — generate misc/mint-inventory.md auditor snapshot.
#
# Companion to FIXY-U-002 (test/test_fixy_umbrella_reach.cpp).  U-002 is
# the CI GATE — a constexpr static_assert matrix that fails CI if any
# substrate mint is not reachable through fixy::.  This script is the
# AUDITOR SURFACE — a human-readable inventory of every `mint_*` factory
# in the substrate, its fixy:: re-export status, §XXI compliance, and
# HS14 fixture count.
#
# The inventory is COMMITTED at HEAD and REGENERATED on demand.  CI
# does NOT fail on inventory regeneration (it's a snapshot, not a
# gate).  Future PRs that add or remove a mint should regenerate the
# inventory in the same commit so the file stays current.
#
# Usage:
#   scripts/gen-mint-inventory.sh                 # write misc/mint-inventory.md
#   scripts/gen-mint-inventory.sh --stdout        # print to stdout
#   scripts/gen-mint-inventory.sh --check         # diff against HEAD copy; exit 0 clean / 1 drift
#   scripts/gen-mint-inventory.sh --self-test     # plant a mint, verify capture
#   scripts/gen-mint-inventory.sh -h | --help     # usage
#
# Acceptance gates (per FIXY-U-106):
#   - runs in <5s on full codebase
#   - one section per substrate tree, mints sorted by name
#   - gap markers: [✗ NO-FIXY] / [⚠ NO-NODISCARD] / [⚠ NO-CONSTEXPR] /
#     [⚠ NO-NOEXCEPT] / [⚠ NO-REQUIRES] / [⚠ <2 HS14]
#   - --self-test verifies the scanner captures a planted mint declaration

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
gen-mint-inventory.sh — substrate mint inventory generator.

Usage:
  gen-mint-inventory.sh                 # write misc/mint-inventory.md
  gen-mint-inventory.sh --stdout        # print to stdout
  gen-mint-inventory.sh --check         # diff against HEAD copy
  gen-mint-inventory.sh --self-test     # plant a mint, verify capture
  gen-mint-inventory.sh -h | --help     # usage
USAGE
}

mode="write"
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --stdout)   mode="stdout" ;;
    --check)    mode="check" ;;
    --self-test) mode="self-test" ;;
    "") ;;
    *) printf 'gen-mint-inventory: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'gen-mint-inventory: ripgrep (rg) is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_MINT_INVENTORY_TEST_ROOT:-$root}"
trees=(safety effects algebra concurrent sessions permissions bridges handles cipher warden)

# ── Extract qualifiers for one declaration site ──────────────────────
# Inputs: file path (absolute), line number.
# Reads lines [N-2 .. N+5] and inspects them for [[nodiscard]],
# constexpr/consteval, noexcept, requires, and a ctx-bound first
# parameter (Ctx const& / eff::IsExecCtx).
#
# Outputs TAB-separated: nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb (each 0 or 1).
extract_qualifiers() {
    local file="$1" line="$2"
    local start=$(( line - 2 ))
    local end=$(( line + 5 ))
    (( start < 1 )) && start=1

    local window
    window="$(sed -n "${start},${end}p" "$file" 2>/dev/null || true)"

    local nd=0 cx=0 ne=0 rq=0 cb=0
    if grep -qE '\[\[nodiscard\]\]' <<<"$window"; then nd=1; fi
    if grep -qE '\b(constexpr|consteval)\b' <<<"$window"; then cx=1; fi
    if grep -qE '\bnoexcept\b' <<<"$window"; then ne=1; fi
    if grep -qE '\brequires\b' <<<"$window"; then rq=1; fi
    if grep -qE '(Ctx[[:space:]]+const[[:space:]]*&|effects::IsExecCtx|eff::IsExecCtx)' <<<"$window"; then cb=1; fi

    printf '%d\t%d\t%d\t%d\t%d\n' "$nd" "$cx" "$ne" "$rq" "$cb"
}

# ── Scan one substrate tree for mint_* declarations ──────────────────
# Emits TAB-separated rows: tree<TAB>name<TAB>file:line<TAB>nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb
#
# Filter: skips comment lines (//, ///, *, /*) and pure-call lines
# (lines whose first non-whitespace token is `return` or `auto X =`).
scan_substrate() {
    local tree="$1"
    local dir="$scan_root/include/crucible/$tree"
    [[ -d "$dir" ]] || return 0

    while IFS=: read -r file line text; do
        # Skip comment lines.
        local stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
            'return '*|'return('*|'auto '*=*) continue ;;
        esac

        # Extract mint name from the matched line.  awk avoids head -1 +
        # pipefail traps; rg's pattern guarantees at least one match.
        local name
        name="$(awk 'match($0, /mint_[a-z_]+/) { print substr($0, RSTART, RLENGTH); exit }' <<<"$text")"
        [[ -z "$name" ]] && continue

        # Get qualifiers.
        local quals
        quals="$(extract_qualifiers "$file" "$line")"

        local rel="${file#"$scan_root"/}"
        printf '%s\t%s\t%s:%s\t%s\n' "$tree" "$name" "$rel" "$line" "$quals"
    done < <(
        rg -nP \
           --no-heading \
           --type=cpp \
           --glob '!_*.h' \
           'mint_[a-z_]+\s*\(' "$dir" 2>/dev/null || true
    )
}

# ── Cross-reference one mint_name into fixy/ for re-export status ────
# Returns: fixy_path (e.g. "include/crucible/fixy/Sess.h:123") or empty.
# We accept the FIRST match (sorted by file then line) as the canonical
# re-export site; a using-decl or namespace-alias is the typical shape.
fixy_reexport_for() {
    local name="$1"
    # rg exits 1 on no-match; under pipefail we'd inherit that.
    # Capture via command substitution + `|| true` to keep the function safe.
    # `sort` before `head -1` canonicalizes the choice; without it the
    # winner depends on rg's `readdir` order which varies by filesystem.
    local raw
    raw="$(rg -nP --no-heading --type=cpp \
              "\b${name}\b" "$scan_root/include/crucible/fixy/" 2>/dev/null || true)"
    [[ -z "$raw" ]] && return 0
    printf '%s\n' "$raw" | sort | head -1 | awk -F: '{print $1 ":" $2}'
}

# ── Count fixy_neg fixtures mentioning a mint_name ───────────────────
hs14_count_for() {
    local name="$1"
    local raw
    raw="$(rg -lP --no-heading "\b${name}\b" \
              "$scan_root/test/fixy_neg/" 2>/dev/null || true)"
    if [[ -z "$raw" ]]; then
        printf '0'
    else
        # `printf '%s\n'` ensures a trailing newline so wc -l counts the
        # final line; without it `wc -l` reports an off-by-one (counts
        # only embedded \n, not the implicit final line).
        printf '%s\n' "$raw" | wc -l | awk '{print $1}'
    fi
}

# ── --self-test ──────────────────────────────────────────────────────
if [[ "$mode" == "self-test" ]]; then
    tmp_root="$(mktemp -d)"
    trap 'rm -rf "$tmp_root"' EXIT
    mkdir -p "$tmp_root/include/crucible/safety" \
             "$tmp_root/include/crucible/fixy" \
             "$tmp_root/test/fixy_neg"
    cat >"$tmp_root/include/crucible/safety/PlantedSelfTest.h" <<'PLANTED'
#pragma once
// Synthetic mint-inventory fixture.
namespace crucible::planted {
struct PlantedToken {};
[[nodiscard]] constexpr PlantedToken
mint_planted_token() noexcept { return {}; }
}
PLANTED
    cat >"$tmp_root/include/crucible/fixy/Planted.h" <<'FIXY'
#pragma once
#include <crucible/safety/PlantedSelfTest.h>
namespace crucible::fixy::planted {
using ::crucible::planted::mint_planted_token;
}
FIXY
    cat >"$tmp_root/test/fixy_neg/neg_fixy_planted_a.cpp" <<'NEG'
#include <crucible/fixy/Planted.h>
// references mint_planted_token to verify HS14 counter
NEG
    cat >"$tmp_root/test/fixy_neg/neg_fixy_planted_b.cpp" <<'NEG'
#include <crucible/fixy/Planted.h>
// references mint_planted_token (second fixture for HS14 threshold)
NEG
    out="$(mktemp)"
    CRUCIBLE_MINT_INVENTORY_TEST_ROOT="$tmp_root" \
        bash "${BASH_SOURCE[0]}" --stdout >"$out" 2>/dev/null

    if ! grep -qF 'mint_planted_token' "$out"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — planted mint not captured.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # Verify fixy re-export captured.
    if ! grep -F 'mint_planted_token' "$out" | grep -qF 'fixy/Planted.h'; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — fixy re-export not captured.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # Verify HS14 count >= 2 (we planted 2 fixtures).
    line="$(grep -F 'mint_planted_token' "$out" | head -1)"
    if ! grep -qE 'HS14:[[:space:]]*2' <<<"$line"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — HS14 count not 2.\n' >&2
        printf '── line ───\n%s\n──────────\n' "$line" >&2
        rm -f "$out"
        exit 2
    fi
    rm -f "$out"
    printf 'gen-mint-inventory: self-test passed — mint capture + fixy re-export + HS14 count all honoured.\n' >&2
    exit 0
fi

# ── Build the inventory ──────────────────────────────────────────────
inventory_tmp="$(mktemp)"
trap 'rm -f "$inventory_tmp"' EXIT

for tree in "${trees[@]}"; do
    scan_substrate "$tree"
done | sort -t$'\t' -k1,1 -k2,2 -k3,3 | \
    awk -F'\t' '!seen[$1"\t"$2]++' >"$inventory_tmp"
# Note: some mint names are declared in multiple files (e.g.
# `mint_consumer_session` in both CalendarGridSession.h and
# SpscSession.h).  We canonicalize on the FIRST declaration when
# sorted by (tree, name, file:line); awk-based dedup keeps that
# choice deterministic across runs.  Future PRs that add an
# alphabetically-earlier declaration will surface as drift via
# `--check`, prompting the auditor to re-snapshot or reconcile.

# ── Emit markdown ────────────────────────────────────────────────────
emit_inventory() {
    local generated_at
    generated_at="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    cat <<HEADER
# Mint inventory — auditor snapshot

Generated by \`scripts/gen-mint-inventory.sh\` (FIXY-U-106).

This is the auditor-facing companion to \`test/test_fixy_umbrella_reach.cpp\`
(FIXY-U-002, the CI gate).  The inventory below is a SNAPSHOT — regenerate
in the same PR that adds, removes, or renames a substrate \`mint_*\` factory:

\`\`\`bash
scripts/gen-mint-inventory.sh                 # write misc/mint-inventory.md
scripts/gen-mint-inventory.sh --check         # diff against HEAD; exit 1 on drift
\`\`\`

Per CLAUDE.md §XXI Universal Mint Pattern, every cross-tier composition
factory is named \`mint_<noun>\`.  Each row records:

| Column | Meaning |
|---|---|
| \`mint_name\` | The factory's identifier. |
| \`file:line\` | Substrate declaration site (canonical). |
| \`nd cx ne rq cb\` | §XXI compliance flags: \`[[nodiscard]]\` / \`constexpr\` (or \`consteval\`) / \`noexcept\` / \`requires\`-clause / ctx-bound (vs token). \`Y\` = present, \`-\` = absent. |
| \`fixy\` | fixy:: re-export site (\`include/crucible/fixy/...\`) or \`[✗ NO-FIXY]\` gap. |
| \`HS14\` | Count of \`test/fixy_neg/\` fixtures mentioning this mint (HS14 floor is 2). |

Gap markers: \`[✗ NO-FIXY]\` (substrate mint not re-exported through fixy::),
\`[⚠ <2 HS14]\` (HS14 fixture floor not met).  §XXI compliance shortfalls
appear as \`-\` in the flag columns.

Snapshot generated: \`$generated_at\`.

HEADER

    local current_tree=""
    local violation_count=0
    while IFS=$'\t' read -r tree name fileline nd cx ne rq cb; do
        if [[ "$tree" != "$current_tree" ]]; then
            [[ -n "$current_tree" ]] && printf '\n'
            printf '## %s/\n\n' "$tree"
            printf '| mint_name | file:line | nd | cx | ne | rq | cb | fixy | HS14 |\n'
            printf '|---|---|---|---|---|---|---|---|---|\n'
            current_tree="$tree"
        fi

        local fixy hs14
        fixy="$(fixy_reexport_for "$name")"
        hs14="$(hs14_count_for "$name")"

        local fixy_cell
        if [[ -z "$fixy" ]]; then
            fixy_cell='[✗ NO-FIXY]'
        else
            fixy_cell="\`${fixy#"$scan_root"/}\`"
        fi

        local nd_cell="-" cx_cell="-" ne_cell="-" rq_cell="-" cb_cell="token"
        [[ "$nd" == "1" ]] && nd_cell="Y"
        [[ "$cx" == "1" ]] && cx_cell="Y"
        [[ "$ne" == "1" ]] && ne_cell="Y"
        [[ "$rq" == "1" ]] && rq_cell="Y"
        [[ "$cb" == "1" ]] && cb_cell="ctx"

        local hs14_cell="HS14: $hs14"
        if (( hs14 < 2 )); then
            hs14_cell="HS14: $hs14 ⚠"
        fi

        printf '| `%s` | `%s` | %s | %s | %s | %s | %s | %s | %s |\n' \
            "$name" "$fileline" "$nd_cell" "$cx_cell" "$ne_cell" \
            "$rq_cell" "$cb_cell" "$fixy_cell" "$hs14_cell"

        [[ -z "$fixy" ]] && violation_count=$((violation_count + 1))
    done <"$inventory_tmp"

    printf '\n'
    printf '## Summary\n\n'
    printf -- '- Total substrate mints: %d\n' "$(wc -l <"$inventory_tmp")"
    printf -- '- Missing fixy re-export: %d\n' "$violation_count"
    printf -- '- See `test/test_fixy_umbrella_reach.cpp` for the CI-enforced reach matrix.\n'
}

case "$mode" in
    stdout)
        emit_inventory
        ;;
    write)
        mkdir -p "$root/misc"
        emit_inventory >"$root/misc/mint-inventory.md"
        printf 'gen-mint-inventory: wrote misc/mint-inventory.md (%d mints across %d trees).\n' \
            "$(wc -l <"$inventory_tmp")" "${#trees[@]}" >&2
        ;;
    check)
        head_copy="$root/misc/mint-inventory.md"
        if [[ ! -f "$head_copy" ]]; then
            printf 'gen-mint-inventory: misc/mint-inventory.md missing — run without --check to create.\n' >&2
            exit 1
        fi
        # Strip the `Snapshot generated:` timestamp line from both sides;
        # it embeds wall-clock time and would always diff between runs.
        # CI cares about CONTENT drift, not when the file was last touched.
        if diff -q \
            <(grep -v '^Snapshot generated:' "$head_copy") \
            <(emit_inventory | grep -v '^Snapshot generated:') \
            >/dev/null 2>&1; then
            printf 'gen-mint-inventory: misc/mint-inventory.md is up-to-date.\n' >&2
            exit 0
        fi
        printf 'gen-mint-inventory: DRIFT — misc/mint-inventory.md out of date.  Run scripts/gen-mint-inventory.sh to refresh.\n' >&2
        diff -u \
            <(grep -v '^Snapshot generated:' "$head_copy") \
            <(emit_inventory | grep -v '^Snapshot generated:') | \
            head -40 >&2 || true
        exit 1
        ;;
esac
