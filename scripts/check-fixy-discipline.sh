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
#
# ── Opt-in criteria (canonical, durable record) ──────────────────
#
# A directory qualifies as a FIXY_ONLY opt-in candidate when ALL
# four conditions hold simultaneously.  These criteria live HERE
# rather than in a doc because the script is the canonical record
# of which-dirs-are-policed; the doc drifts, the script does not.
#
#   (a) GREENFIELD.  The directory was created AFTER 17 May 2026
#       (fixy ship date — misc/16_05_2026_fixy.md §5).  Pre-fixy
#       directories carry pre-fixy code that uses raw safety::*
#       legitimately; forcing migration creates churn without
#       discipline gain.
#
#   (b) LOW FILE COUNT.  ≤ 5 headers + ≤ 5 sources in the subtree.
#       Opt-in adds a CI gate; large subtrees turn a discipline
#       improvement into a many-file refactor with attendant
#       review/merge friction.
#
#   (c) AT-LEAST-ONE REAL VIOLATION.  Running the linter on the
#       directory BEFORE adding it must surface at least one raw
#       `safety::fn::Fn<...>` use site (or alias/using evasion).
#       Opting in a directory that's already fixy-clean is a no-op
#       — the value of the gate is that it CATCHES a violation,
#       not that it sits there checking nothing.
#
#   (d) AUTHOR CONSENSUS.  The directory's primary author (per
#       `git log --format='%an' <dir>` top hitter) has acknowledged
#       the band-3 classification (misc/16_05_2026_fixy.md §5.1).
#       Code is going to be edited under the discipline; the
#       authors should know that the discipline applies.
#
# When all four hold: add the directory path to FIXY_ONLY_DIRS,
# fix the violations the linter reports (do NOT add the path and
# leave the build red — the linter is a gate, not a backlog), and
# commit both moves as a single PR.
#
# As of this script's ship date, NO directory in include/crucible/
# satisfies (a) ∧ (b) ∧ (c) — fixy/ itself is the only post-fixy
# header tree, but it's exempt by definition (it IS the substrate
# the discipline composes against; see EXEMPT_PREFIXES below).
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
# (1) Direct qualified use:
#       safety::fn::Fn<        — direct instantiation
#       safety::fn::mint_fn<   — substrate token-mint (bypass IsAccepted)
#       safety::fn::mint_fn_for<
#
# (2) Namespace alias evasion:
#       namespace sf = crucible::safety::fn;
#       namespace sf = ::crucible::safety::fn;
#       namespace sf = safety::fn;
#     Authors who establish such an alias then write `sf::Fn<...>` —
#     the alias declaration IS the violation; we flag the declaration
#     site so reviewers fix it before any sf::Fn use appears.
#
# (3) `using` declaration evasion:
#       using crucible::safety::fn::Fn;
#       using safety::fn::Fn;
#     Same logic — the using-declaration is the entry point.
#
# We use ripgrep's `--pcre2` flag and alternation across the three
# patterns.  All three patterns are anchored to `safety::fn::` so
# unrelated code that mentions `safety::` for diagnostics or sources
# stays clean.

PATTERN_DIRECT='safety::fn::(Fn|mint_fn|mint_fn_for)<'
PATTERN_NSALIAS='namespace\s+\w+\s*=\s*(::)?(crucible::)?safety::fn\s*;'
PATTERN_USING='using\s+(::)?(crucible::)?safety::fn::(Fn|mint_fn|mint_fn_for)\s*;'
PATTERN="(${PATTERN_DIRECT})|(${PATTERN_NSALIAS})|(${PATTERN_USING})"

mode="${1:---check}"

if [[ "$mode" == "--self-test" ]]; then
    # Plant each evasion in its own synthetic file; verify every
    # pattern variant fires.  A failing self-test means a ripgrep
    # version regression OR the regex drifted.
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT

    # (1) Direct use
    printf 'auto x = safety::fn::Fn<int>{};\n' > "$tmp/01_direct.cpp"
    # (1b) Direct mint
    printf 'auto y = safety::fn::mint_fn<int>(0);\n' > "$tmp/02_mint.cpp"
    # (2) Namespace alias (3 spellings)
    printf 'namespace sf = safety::fn;\n' > "$tmp/03_alias_short.cpp"
    printf 'namespace sf = crucible::safety::fn;\n' > "$tmp/04_alias_med.cpp"
    printf 'namespace sf = ::crucible::safety::fn;\n' > "$tmp/05_alias_full.cpp"
    # (3) Using-declaration
    printf 'using safety::fn::Fn;\n' > "$tmp/06_using.cpp"
    printf 'using crucible::safety::fn::mint_fn;\n' > "$tmp/07_using_full.cpp"
    # (negative) — should NOT fire on safety:: alone (only safety::fn::*)
    printf 'safety::Linear<int> z;\n' > "$tmp/08_unrelated_safety.cpp"

    fail=0
    for f in "$tmp"/0[1-7]_*.cpp; do
        if ! rg -n --no-heading --pcre2 "$PATTERN" "$f" > /dev/null; then
            printf 'fixy_discipline: self-test FAILED — pattern missed %s\n' \
                "$(basename "$f")" >&2
            fail=1
        fi
    done
    if rg -n --no-heading --pcre2 "$PATTERN" "$tmp/08_unrelated_safety.cpp" > /dev/null; then
        printf 'fixy_discipline: self-test FAILED — pattern fires on unrelated safety:: use\n' >&2
        fail=1
    fi

    if [[ "$fail" -eq 0 ]]; then
        printf 'fixy_discipline: self-test OK — 7 evasion shapes detected, 1 unrelated case spared\n'
        exit 0
    else
        printf 'fixy_discipline: self-test FAILED — regex drift; review check-fixy-discipline.sh PATTERN_*\n' >&2
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
