#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════
# check-fixy-discipline.sh — FIXY-F / Phase F CI grep linter
#
# Per misc/16_05_2026_fixy.md §4 Phase F + §5 band-classification:
# every greenfield Crucible directory added after 17 May 2026 SHOULD
# compose against `fixy::fn<Type, Grants...>` via the
# reject-by-default surface, NOT against raw `safety::fn::Fn<...>`.
#
# This linter enforces the policy on directories that have OPTED IN
# (listed in the FIXY_ONLY_DIRS array below).  Directories not in
# the list are unchecked — the discipline is opt-in, not blanket.
#
# Carve-outs: fixy's own internals MUST use raw `safety::fn::Fn`
# (they ARE the resolver + aggregator); the substrate header MUST
# define `safety::fn::Fn` (it IS the definition).  Both are listed
# in EXEMPT_FILES below.
#
# ── Modes ────────────────────────────────────────────────────────
#
#   --check       (default)  scan production tree, fail on hit
#   --self-test              plant a fake raw usage + verify regex
#                            fires on it (regression guard for
#                            ripgrep version drift)
# ════════════════════════════════════════════════════════════════════

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Opt-in directory list ───────────────────────────────────────
#
# Directories that have committed to the reject-by-default fixy
# surface.  Empty initially (Phase F ships the SCAFFOLDING; the
# first directory opts in via a separate PR).  Reviewers ADD a
# path here when they want the discipline enforced for new files
# in that subtree.
FIXY_ONLY_DIRS=(
    # examples/fixy           — already uses fixy::fn exclusively;
    #                           leave unlisted because the existing
    #                           bench/audit dance is separate.
    # include/crucible/greenfield_dir  — pattern for future opt-ins
)

# ── Exempt files (always allowed to mention raw safety::fn::Fn) ─
#
# fixy/ internals MUST reference the substrate template — they
# resolve INTO it.  The substrate header MUST define it.  Both are
# permissive carve-outs.  Examples/tests reference both surfaces
# pedagogically and are unchecked.
EXEMPT_PREFIXES=(
    include/crucible/fixy/      # resolver + aggregator
    include/crucible/safety/Fn.h
    include/crucible/safety/CollisionCatalog.h
    test/                       # contrast fixtures, neg-compile probes
    examples/                   # both fn/ and fixy/ pedagogy
    bench/
    misc/
    scripts/                    # this script itself
    src/                        # legacy production (pre-fixy)
)

is_exempt() {
    local rel="$1"
    for prefix in "${EXEMPT_PREFIXES[@]}"; do
        case "$rel" in
            "$prefix"*) return 0 ;;
        esac
    done
    return 1
}

is_in_fixy_only_dir() {
    local rel="$1"
    for dir in "${FIXY_ONLY_DIRS[@]}"; do
        case "$rel" in
            "$dir"/*) return 0 ;;
        esac
    done
    return 1
}

# ── Forbidden patterns ──────────────────────────────────────────
#
# `safety::fn::Fn<` — direct instantiation of the substrate template.
# `safety::fn::mint_fn<` / `safety::fn::mint_fn_for<` — substrate
# mint factories that bypass fixy's IsAccepted gate.

PATTERN='safety::fn::(Fn|mint_fn|mint_fn_for)<'

mode="${1:---check}"

if [[ "$mode" == "--self-test" ]]; then
    # Plant a synthetic match in a tmp file; verify the regex fires.
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    printf 'auto x = safety::fn::Fn<int, /*grants*/>{};\n' > "$tmp/synthetic.cpp"

    if rg -n --no-heading "$PATTERN" "$tmp/synthetic.cpp" > /dev/null; then
        printf 'fixy_discipline: self-test OK — pattern fires on synthetic raw substrate use\n'
        exit 0
    else
        printf 'fixy_discipline: self-test FAILED — pattern did not match expected raw substrate use\n' >&2
        printf '  (likely a ripgrep version regression, or the regex was edited without updating the self-test)\n' >&2
        exit 1
    fi
fi

if [[ "$mode" != "--check" ]]; then
    printf 'usage: %s [--check | --self-test]\n' "$0" >&2
    exit 2
fi

# ── Production scan ──────────────────────────────────────────────

status=0
hit_count=0

while IFS=: read -r file line text; do
    rel="${file#"$root"/}"

    # Skip files in exempt prefixes (fixy internals, substrate
    # definition, examples, tests, etc.).
    if is_exempt "$rel"; then
        continue
    fi

    # Only flag if the file lives under a directory that opted in.
    if ! is_in_fixy_only_dir "$rel"; then
        continue
    fi

    printf 'fixy_discipline: raw substrate use at %s:%s\n' "$rel" "$line" >&2
    printf 'fixy_discipline: %s\n' "$text" >&2
    hit_count=$((hit_count + 1))
    status=1
done < <(
    rg -n --no-heading --pcre2 \
        --glob '!build/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        --glob '!.git/**' \
        "$PATTERN" "$root" 2>/dev/null || true
)

if [[ "$status" -ne 0 ]]; then
    cat >&2 <<EOF
fixy_discipline: $hit_count violation(s).  Files in FIXY_ONLY_DIRS must
compose against fixy::fn<Type, Grants...> (the reject-by-default
surface), not safety::fn::Fn<...> directly.  See:

  - misc/16_05_2026_fixy.md §5    — band-classification policy
  - include/crucible/fixy/Fn.h    — fixy::fn aggregator
  - include/crucible/fixy/Stance.h — 8 canonical stance shortcuts
  - examples/fixy/example_fixy_*.cpp — worked production patterns

Each violation can be resolved by replacing
  safety::fn::Fn<T, /*19 axes positional*/>
with
  fixy::fn<T, /*20 grants*/>
or, for the 8 canonical stances,
  fixy::stance::mint_fn_for<fixy::stance::BgWorker>(value).
EOF
fi

# Report success even when FIXY_ONLY_DIRS is empty — the scaffold
# is in place, the discipline is opt-in, no production directory
# has committed to it yet.
if [[ ${#FIXY_ONLY_DIRS[@]} -eq 0 && "$status" -eq 0 ]]; then
    printf 'fixy_discipline: scaffold OK (no directories opted in yet; first opt-in adds to FIXY_ONLY_DIRS in this script)\n'
fi

exit "$status"
