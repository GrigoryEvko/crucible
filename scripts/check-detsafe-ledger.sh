#!/usr/bin/env bash
# check-detsafe-ledger.sh — axiom 8 (DetSafe) containment for the
# hardware-capability ledger.
#
# ── THE RULE ──────────────────────────────────────────────────────────
#
# A measured verdict may change how fast Crucible runs.  It may never
# change what Crucible computes.
#
# The ledger caches answers to questions like "is AVX-512 faster than
# AVX2 on this part", "where is the real cache-tier knee", "what does a
# NUMA hop cost".  Every one of those is a property of a machine, and
# every one of them differs between two machines that must nonetheless
# produce bit-identical output under a BITEXACT recipe.
#
# So the ledger is allowed to steer a scheduler, size a tile, pick a
# thread count.  It is NOT allowed to reach:
#
#   • content_hash / merkle_hash — a DAG node's identity must depend on
#     the computation and nothing else.  If a verdict entered the hash,
#     the same model on two hosts would produce two different DAGs, the
#     KernelCache would stop being content-addressed, and cross-host
#     replay would break.
#
#   • the memory plan — offsets are content-addressed precisely so the
#     same DAG yields the same addresses everywhere.  A verdict-derived
#     offset would make addresses host-dependent, and with them every
#     kernel that reads uninitialized padding or depends on alignment.
#
#   • a BITEXACT recipe's chosen path — the whole point of the tier is
#     that the reduction topology and rounding are pinned regardless of
#     what any particular host happens to be fast at.
#
# CogMimic already draws this line for itself: its comment at the top of
# mimic/CogMimic.h records that calibrated throughput is deliberately NOT
# folded into the binary-compatibility class, because measurements vary
# across same-SKU parts and folding them would split the cache on noise.
# The ledger preserves that direction — it folds the caps CLASS into its
# own fingerprint, never the reverse.  This guard is what keeps the arrow
# pointing one way.
#
# ── WHAT IS CHECKED ───────────────────────────────────────────────────
#
# For each header on the hashing path (the ROOTS array below), the guard
# walks the TRANSITIVE include closure and fails if any file in that
# closure is under include/crucible/ledger/.  A direct include is the
# obvious case; the transitive walk is the one that matters, because a
# leak will almost certainly arrive through an intermediate header
# rather than as an edit to MerkleDag.h itself.
#
# It additionally scans every file in each closure for a textual
# reference to the crucible::ledger namespace, which catches a forward
# declaration used to dodge the include.  Comments are stripped before
# that scan, so a hashing-path header may — and should — explain in
# prose why it does not consult the ledger.
#
# ── NO ALLOWLIST ──────────────────────────────────────────────────────
#
# Every other guard in scripts/ carries a per-line allowlist for
# grandfathered sites.  This one deliberately does not, and the absence
# is the point: an "approved exception" here is a DetSafe violation with
# paperwork.  There is no measurement whose value justifies making a
# hash host-dependent.  If this guard fires, the fix is to move the
# consumer off the hashing path, never to record the site.
#
# Exit status:
#   0 — clean: no ledger header and no ledger symbol is reachable from
#       the hashing path
#   1 — at least one violation
#   2 — bad invocation, missing dependency, or a rotted ROOTS entry

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── The hashing path ──────────────────────────────────────────────────
#
# Cited by PATH, and every entry is existence-checked below.  A guard
# whose root list silently rots is a guard that passes for the wrong
# reason, which is worse than no guard at all — so a missing root is
# exit 2, not a skip.
ROOTS=(
    # content_hash / merkle_hash themselves
    "include/crucible/Types.h"
    "include/crucible/MerkleDag.h"
    "include/crucible/DimHash.h"
    "include/crucible/TraceGraph.h"
    # the IR whose shape the hashes are taken over
    "include/crucible/Graph.h"
    "include/crucible/Expr.h"
    "include/crucible/ExprPool.h"
    "include/crucible/CKernel.h"
    "include/crucible/StorageNbytes.h"
    # the memory plan
    "include/crucible/PoolAllocator.h"
    # deterministic RNG — same (counter, key) must give the same bits
    "include/crucible/Philox.h"
    # BITEXACT recipe selection
    "include/crucible/NumericalRecipe.h"
    "include/crucible/RecipeRegistry.h"
    # the on-disk form of all of the above
    "include/crucible/Serialize.h"
    # per-Cog identity and the kernel-cache key derived from it
    "include/crucible/cog/CogIdentity.h"
    "include/crucible/mimic/CogMimic.h"
)

FORBIDDEN_DIR="include/crucible/ledger"
FORBIDDEN_INCLUDE="crucible/ledger/"
FORBIDDEN_SYMBOL="crucible::ledger"

usage() {
    cat >&2 <<'USAGE'
check-detsafe-ledger.sh — DetSafe containment for the hardware ledger.

Usage:
  check-detsafe-ledger.sh              # scan; exit 1 on violation
  check-detsafe-ledger.sh --self-test  # plant a violation, verify catch
  check-detsafe-ledger.sh -h | --help  # usage

A measured verdict may change how fast Crucible runs and must never
change what it computes.  No header on the hashing path — content_hash,
merkle_hash, the memory plan, BITEXACT recipe selection — may reach
include/crucible/ledger/, directly or transitively.

There is no allowlist, on purpose.  An exception here is a DetSafe
violation with paperwork.
USAGE
}

# ── Closure walk ──────────────────────────────────────────────────────
#
# Resolves `#include <crucible/...>` and `#include "..."` against the
# scan root.  Anything it cannot resolve (a system header, a third-party
# header) is simply not followed, which is correct: the ledger lives
# under include/crucible and nothing outside the tree can reach it.

declare -A VISITED
declare -A PARENT_OF

# Emits the include chain from a root down to $1, one arrow per hop.
chain_to() {
    local node="$1"
    local chain="$node"
    local guard=0
    while [[ -n "${PARENT_OF[$node]:-}" && $guard -lt 64 ]]; do
        node="${PARENT_OF[$node]}"
        chain="$node -> $chain"
        guard=$((guard + 1))
    done
    printf '%s' "$chain"
}

# Prints the includes of $1 (a path relative to $scan_root), one per line,
# already resolved to repo-relative paths.  Trailing `//` comments are
# stripped first so a commented-out include is not followed.
includes_of() {
    local rel="$1"
    local abs="$scan_root/$rel"
    [[ -f "$abs" ]] || return 0

    local line code target candidate
    while IFS= read -r line; do
        code="${line%%//*}"
        case "$code" in
            *'#include'*) ;;
            *) continue ;;
        esac
        # Pull whatever sits between <> or "".
        target=""
        if [[ "$code" =~ \#[[:space:]]*include[[:space:]]*\<([^\>]+)\> ]]; then
            target="${BASH_REMATCH[1]}"
        elif [[ "$code" =~ \#[[:space:]]*include[[:space:]]*\"([^\"]+)\" ]]; then
            target="${BASH_REMATCH[1]}"
        fi
        [[ -n "$target" ]] || continue

        # Angle form rooted at include/.
        candidate="include/$target"
        if [[ -f "$scan_root/$candidate" ]]; then
            printf '%s\n' "$candidate"
            continue
        fi
        # Quote form, relative to the including file's directory.
        candidate="$(dirname "$rel")/$target"
        # Collapse any ./ and ../ so two spellings of one file do not
        # visit it twice and so the chain reads cleanly.
        if [[ -f "$scan_root/$candidate" ]]; then
            printf '%s\n' "$(cd "$scan_root/$(dirname "$candidate")" && pwd)/$(basename "$candidate")" \
                | while IFS= read -r absolute; do printf '%s\n' "${absolute#"$scan_root"/}"; done
            continue
        fi
        # Unresolvable: a system or third-party header.  Not followed.
    done < "$abs"
}

violation_count=0

walk_from_root() {
    local origin="$1"
    local -a queue=("$origin")
    VISITED["$origin"]=1
    PARENT_OF["$origin"]=""

    local current child code
    while [[ ${#queue[@]} -gt 0 ]]; do
        current="${queue[0]}"
        queue=("${queue[@]:1}")

        # (a) Is this file itself under the forbidden tree?
        case "$current" in
            "$FORBIDDEN_DIR"/*)
                printf 'DETSAFE violation: %s reaches the hardware ledger.\n' "$origin" >&2
                printf '  include chain: %s\n' "$(chain_to "$current")" >&2
                violation_count=$((violation_count + 1))
                continue
                ;;
        esac

        # (b) Does it name the ledger namespace without including it?
        # Comments are stripped so a hashing-path header may explain in
        # prose why it stays clear of the ledger.
        if [[ -f "$scan_root/$current" ]]; then
            while IFS= read -r line; do
                code="${line%%//*}"
                case "$code" in
                    *"$FORBIDDEN_SYMBOL"*)
                        printf 'DETSAFE violation: %s names %s outside a comment.\n' \
                            "$current" "$FORBIDDEN_SYMBOL" >&2
                        printf '  include chain: %s\n' "$(chain_to "$current")" >&2
                        printf '  offending line: %s\n' "$code" >&2
                        violation_count=$((violation_count + 1))
                        ;;
                esac
            done < "$scan_root/$current"
        fi

        while IFS= read -r child; do
            [[ -n "$child" ]] || continue
            if [[ -z "${VISITED[$child]:-}" ]]; then
                VISITED["$child"]=1
                PARENT_OF["$child"]="$current"
                queue+=("$child")
            fi
        done < <(includes_of "$current")
    done
}

# ── Self-test ─────────────────────────────────────────────────────────

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ "${1:-}" == "--self-test" ]]; then
    tmp_root="$(mktemp -d)"
    trap 'rm -rf "$tmp_root"' EXIT
    mkdir -p "$tmp_root/include/crucible/ledger" "$tmp_root/include/crucible/planted" "$tmp_root/scripts"

    # A stand-in ledger header.
    cat >"$tmp_root/include/crucible/ledger/Verdict.h" <<'LEDGER'
#pragma once
namespace crucible::ledger { struct PlantedVerdict {}; }
LEDGER

    # A clean hashing-path root: mentions the ledger only in a comment,
    # which must NOT be flagged.  A hashing-path header explaining why it
    # stays clear of the ledger is good practice, not a violation.
    cat >"$tmp_root/include/crucible/planted/CleanRoot.h" <<'CLEAN'
#pragma once
// Deliberately does not consult crucible::ledger — a measured verdict
// must never reach content_hash.  This comment must not be flagged.
// #include <crucible/ledger/Verdict.h>   <- commented out, not followed
namespace crucible::planted { inline int clean_hash() { return 0; } }
CLEAN

    # An intermediate header, so the walk has to be transitive to catch
    # the leak.  A one-hop-only guard would pass this.
    cat >"$tmp_root/include/crucible/planted/Middle.h" <<'MIDDLE'
#pragma once
#include <crucible/ledger/Verdict.h>
MIDDLE

    # The dirty root reaches the ledger only through Middle.h.
    cat >"$tmp_root/include/crucible/planted/DirtyRoot.h" <<'DIRTY'
#pragma once
#include <crucible/planted/Middle.h>
namespace crucible::planted { inline int dirty_hash() { return 1; } }
DIRTY

    # A root that dodges the include and forward-declares instead.
    cat >"$tmp_root/include/crucible/planted/SneakyRoot.h" <<'SNEAKY'
#pragma once
namespace crucible::ledger { struct PlantedVerdict; }
namespace crucible::planted { inline int sneaky_hash(crucible::ledger::PlantedVerdict*) { return 2; } }
SNEAKY

    self_test_report="$(mktemp)"
    # shellcheck disable=SC2064
    trap "rm -rf '$tmp_root' '$self_test_report'" EXIT

    fail() {
        printf 'check-detsafe-ledger: SELF-TEST FAILED — %s\n' "$1" >&2
        printf '── scanner stderr ───\n%s\n─────────────────────\n' "$(cat "$self_test_report")" >&2
        exit 2
    }

    # (1) The clean root alone must pass.
    if ! CRUCIBLE_DETSAFE_LEDGER_TEST_ROOT="$tmp_root" \
         CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS="include/crucible/planted/CleanRoot.h" \
         bash "${BASH_SOURCE[0]}" 2>"$self_test_report"; then
        fail "a clean root was flagged — the comment strip or the walk is over-matching."
    fi

    # (2) The transitive leak must be caught.
    if CRUCIBLE_DETSAFE_LEDGER_TEST_ROOT="$tmp_root" \
       CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS="include/crucible/planted/DirtyRoot.h" \
       bash "${BASH_SOURCE[0]}" 2>"$self_test_report"; then
        fail "a transitive ledger include was NOT caught — the walk is not transitive."
    fi
    grep -qF 'include/crucible/ledger/Verdict.h' "$self_test_report" \
        || fail "the diagnostic did not name the offending ledger header."
    grep -qF 'Middle.h' "$self_test_report" \
        || fail "the diagnostic did not show the include chain through the intermediate header."

    # (3) The forward-declaration dodge must be caught.
    if CRUCIBLE_DETSAFE_LEDGER_TEST_ROOT="$tmp_root" \
       CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS="include/crucible/planted/SneakyRoot.h" \
       bash "${BASH_SOURCE[0]}" 2>"$self_test_report"; then
        fail "a forward-declared ledger symbol was NOT caught — the include walk alone is not enough."
    fi

    # (4) A rotted root path must be exit 2, never a silent pass.
    set +e
    CRUCIBLE_DETSAFE_LEDGER_TEST_ROOT="$tmp_root" \
    CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS="include/crucible/planted/NoSuchFile.h" \
    bash "${BASH_SOURCE[0]}" 2>"$self_test_report"
    rotted_status=$?
    set -e
    [[ $rotted_status -eq 2 ]] \
        || fail "a missing root exited ${rotted_status}, expected 2 — the root list could rot unnoticed."

    printf 'check-detsafe-ledger: self-test passed — clean root stays clean, transitive leak caught with its chain, forward-declaration dodge caught, rotted root list is exit 2.\n' >&2
    exit 0
fi

if [[ -n "${1:-}" ]]; then
    printf 'check-detsafe-ledger: unknown argument: %s\n' "$1" >&2
    usage
    exit 2
fi

# ── Scan ──────────────────────────────────────────────────────────────

scan_root="${CRUCIBLE_DETSAFE_LEDGER_TEST_ROOT:-$root}"

if [[ -n "${CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS:-}" ]]; then
    # Space-separated override, used only by --self-test.
    read -r -a ROOTS <<< "$CRUCIBLE_DETSAFE_LEDGER_TEST_ROOTS"
fi

if [[ ${#ROOTS[@]} -eq 0 ]]; then
    printf 'check-detsafe-ledger: the hashing-path root list is empty.\n' >&2
    exit 2
fi

missing=0
for entry in "${ROOTS[@]}"; do
    if [[ ! -f "$scan_root/$entry" ]]; then
        printf 'check-detsafe-ledger: hashing-path root does not exist: %s\n' "$entry" >&2
        missing=$((missing + 1))
    fi
done
if [[ $missing -ne 0 ]]; then
    cat >&2 <<'ROTTED'

The ROOTS array in this script names a file that is no longer there.
That is exit 2 rather than a skip on purpose: a guard whose root list
has rotted passes for the wrong reason, which is worse than no guard.

Update the ROOTS array to the file's new path in the SAME commit that
moves it.
ROTTED
    exit 2
fi

for entry in "${ROOTS[@]}"; do
    # Each root gets a fresh visited set, so a violation is reported once
    # per root that can reach it rather than once for the whole run.  A
    # reader fixing this needs to know every entry point, not just one.
    VISITED=()
    PARENT_OF=()
    walk_from_root "$entry"
done

if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-detsafe-ledger found ${violation_count} path(s) from the hashing
path into the hardware-capability ledger.  This breaks axiom 8
(DetSafe): the same inputs would stop producing the same outputs across
hosts, because a ledger verdict is a property of a machine.

There is no allowlist.  The fix is one of:

  (1) Move the consumer off the hashing path.  A verdict may steer a
      scheduler, a tile size or a thread count; it may not participate
      in content_hash, merkle_hash or a memory-plan offset.

  (2) Pass the verdict in as a parameter at a call site ABOVE the
      hashing path, so the hashed structure never sees it.

  (3) If the value is genuinely part of the computation's identity, it
      is not a verdict — it belongs in cog::TargetCaps and in the
      CogMimic caps-class projection, which the ledger already folds
      into its own fingerprint.
HINT
    exit 1
fi

printf 'check-detsafe-ledger: clean — no ledger header or symbol is reachable from the hashing path (%d roots walked).\n' \
    "${#ROOTS[@]}" >&2
exit 0
