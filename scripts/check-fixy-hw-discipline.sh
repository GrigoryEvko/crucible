#!/usr/bin/env bash
# check-fixy-hw-discipline.sh — hardware-axis grant-declaration presence gate
# (FIXY-V-264, Agent 11 Tier 2 #17).
#
# Agent 11 Tier 2 wired the hardware-instruction axis (V-250..V-261) into
# four production sites whose behaviour depends on a compile-time-selected
# hardware construct — a SIMD ISA, a cache instruction, or a memory fence.
# Each site declares the (vendor::intrinsic / simd::width / grant::hw::*)
# grant the construct uses, inside a dedicated `namespace <site>_hw` block
# with `IsGrantTag<>` well-formedness + `which_dim_v<>` axis-routing
# static_asserts (and, where a concrete compile-time number exists, a
# stride/width or fence-strength consistency assert).
#
#   include/crucible/SwissTable.h            → swiss_hw      (V-262, SimdIsa)
#   include/crucible/cntp/Fec.h              → fec_hw        (V-263, SimdIsa)
#   include/crucible/TraceRing.h             → tracering_hw  (V-264, HwInstruction)
#   include/crucible/concurrent/ChaseLevDeque.h → chaselev_hw (V-264, BarrierStrength)
#
# Those static_asserts are verified by the C++ BUILD (the V-262/263/264
# sentinels + every TU that includes the header).  This script is the
# cheaper, complementary PRESENCE gate: it guards against the declaration
# being silently DELETED or GUTTED (namespace kept, asserts stripped) in a
# refactor that does not happen to recompile a sentinel.  Two layers:
# script = "the grant block is present", build = "the grant is correct".
#
# It is a POSITIVE-presence check (not a negative-grep ban): each manifest
# file MUST contain (a) its `namespace <site>_hw` marker, (b) a fully-
# qualified `::crucible::fixy::grant::IsGrantTag<` assertion, and (c) a
# fully-qualified `::crucible::fixy::grant::which_dim_v<` assertion.  A
# missing file or any missing substring is a violation.
#
# A NEW hardware-axis site (a future #if-arm / prefetch / fence annotation)
# adds a row to the manifest below in the same commit that ships the
# declaration — exactly like CRUCIBLE_FIXY_ONLY_PATHS grows for band-3.
#
# Exit status:
#   0 — clean (every manifest site has its grant declaration intact)
#   1 — at least one manifest site is missing or has a gutted declaration
#   2 — bad invocation / missing dependency / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-hw-discipline.sh — hardware-axis grant-declaration presence gate.

Usage:
  check-fixy-hw-discipline.sh              # scan; exit 1 if a site lost its grant block
  check-fixy-hw-discipline.sh --self-test  # plant gutted/missing sites, verify catch
  check-fixy-hw-discipline.sh -h | --help  # usage
USAGE
}

# ── Manifest: parallel arrays (repo-relative file : namespace marker) ─
# Every entry is a production site that declares a hardware-axis grant.
manifest_files=(
    "include/crucible/SwissTable.h"
    "include/crucible/cntp/Fec.h"
    "include/crucible/TraceRing.h"
    "include/crucible/concurrent/ChaseLevDeque.h"
)
manifest_markers=(
    "namespace swiss_hw"
    "namespace fec_hw"
    "namespace tracering_hw"
    "namespace chaselev_hw"
)

# ── Universal substrings every grant block MUST carry ────────────────
# Fully qualified so a partial-namespace alias cannot satisfy the gate by
# accident, and so a "namespace present but asserts stripped" gutting is
# caught.
required_substrings=(
    "::crucible::fixy::grant::IsGrantTag<"
    "::crucible::fixy::grant::which_dim_v<"
)

run_scan() {
    local scan_root="$1"
    local violations=0
    local i
    for i in "${!manifest_files[@]}"; do
        local rel="${manifest_files[$i]}"
        local marker="${manifest_markers[$i]}"
        local path="$scan_root/$rel"

        if [[ ! -f "$path" ]]; then
            printf 'FIXY-HW-DISCIPLINE violation: %s — manifest file MISSING (the hardware-axis grant site was moved or deleted; update the manifest in scripts/check-fixy-hw-discipline.sh).\n' \
                "$rel" >&2
            violations=$((violations + 1))
            continue
        fi

        if ! grep -qF -- "$marker" "$path"; then
            printf 'FIXY-HW-DISCIPLINE violation: %s — missing grant block "%s" (FIXY-V-262/263/264). The hardware-axis declaration was removed or renamed.\n' \
                "$rel" "$marker" >&2
            violations=$((violations + 1))
            continue
        fi

        local sub
        for sub in "${required_substrings[@]}"; do
            if ! grep -qF -- "$sub" "$path"; then
                printf 'FIXY-HW-DISCIPLINE violation: %s — grant block "%s" present but GUTTED (missing "%s"). The declaration must keep its well-formedness + axis-routing static_asserts.\n' \
                    "$rel" "$marker" "$sub" >&2
                violations=$((violations + 1))
            fi
        done
    done
    return "$violations"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant the four manifest files under a temp root: three COMPLETE
        # (marker + both asserts), one GUTTED (marker, no asserts).  Phase 1
        # expects exit 1 with ONLY the gutted file flagged.  Phase 2 makes it
        # complete → exit 0.  Phase 3 deletes a file → exit 1 (MISSING).
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/cntp" \
                 "$tmp_root/include/crucible/concurrent"

        write_complete() {  # $1=path  $2=namespace-name
            cat >"$1" <<EOF
#pragma once
namespace $2 {
using ActiveGrant = int;  // synthetic
static_assert(::crucible::fixy::grant::IsGrantTag<ActiveGrant>, "x");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveGrant> == 0, "x");
}  // namespace $2
EOF
        }
        write_gutted() {    # $1=path  $2=namespace-name (marker present, asserts stripped)
            cat >"$1" <<EOF
#pragma once
namespace $2 {
using ActiveGrant = int;  // synthetic — asserts deliberately stripped
}  // namespace $2
EOF
        }

        write_complete "$tmp_root/include/crucible/SwissTable.h"            "swiss_hw"
        write_complete "$tmp_root/include/crucible/cntp/Fec.h"             "fec_hw"
        write_gutted   "$tmp_root/include/crucible/TraceRing.h"            "tracering_hw"
        write_complete "$tmp_root/include/crucible/concurrent/ChaseLevDeque.h" "chaselev_hw"

        # ── Phase 1: gutted site must be flagged, complete ones must not ──
        result_file="$(mktemp)"
        rc=0
        CRUCIBLE_HW_DISCIPLINE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 1) — expected exit 1 (gutted), got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$result_file")" >&2
            rm -f "$result_file"; exit 2
        fi
        # Inspect ONLY the violation lines — the remediation HINT mentions
        # the complete-site filenames (e.g. "see SwissTable.h"), so a raw
        # whole-stderr grep would false-positive on the hint text.
        violation_lines="$(grep '^FIXY-HW-DISCIPLINE violation:' "$result_file" || true)"
        if ! grep -qF 'TraceRing.h' <<<"$violation_lines"; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 1) — gutted tracering_hw not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$result_file")" >&2
            rm -f "$result_file"; exit 2
        fi
        if grep -qF 'SwissTable.h' <<<"$violation_lines" \
           || grep -qF 'Fec.h' <<<"$violation_lines" \
           || grep -qF 'ChaseLevDeque.h' <<<"$violation_lines"; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 1) — a COMPLETE site was falsely flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$result_file")" >&2
            rm -f "$result_file"; exit 2
        fi
        rm -f "$result_file"

        # ── Phase 2: complete the gutted site → clean (exit 0) ───────────
        write_complete "$tmp_root/include/crucible/TraceRing.h" "tracering_hw"
        if ! CRUCIBLE_HW_DISCIPLINE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" 2>/dev/null; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 2) — all-complete manifest did not pass clean.\n' >&2
            exit 2
        fi

        # ── Phase 3: delete a manifest file → MISSING violation (exit 1) ─
        rm -f "$tmp_root/include/crucible/cntp/Fec.h"
        missing_file="$(mktemp)"
        rc=0
        CRUCIBLE_HW_DISCIPLINE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$missing_file" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 3) — expected exit 1 (missing file), got %d.\n' "$rc" >&2
            rm -f "$missing_file"; exit 2
        fi
        if ! grep -qF 'MISSING' "$missing_file"; then
            printf 'check-fixy-hw-discipline: SELF-TEST FAILED (phase 3) — deleted manifest file not reported MISSING.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$missing_file")" >&2
            rm -f "$missing_file"; exit 2
        fi
        rm -f "$missing_file"

        printf 'check-fixy-hw-discipline: self-test passed — gutted block caught, complete blocks pass, missing file caught.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-fixy-hw-discipline: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Production scan ──────────────────────────────────────────────────
scan_root="${CRUCIBLE_HW_DISCIPLINE_TEST_ROOT:-$root}"

violations=0
run_scan "$scan_root" || violations=$?

if [[ "$violations" -ne 0 ]]; then
    cat >&2 <<HINT

check-fixy-hw-discipline detected ${violations} hardware-axis grant
declaration defect(s).  Each Agent 11 Tier 2 site (SwissTable / Fec /
TraceRing / ChaseLevDeque) MUST keep its 'namespace <site>_hw' block with
the IsGrantTag<> + which_dim_v<> static_asserts that pin the SIMD ISA /
cache / fence grant the compile-time-selected hardware construct uses.

Remediations:
  (1) If you removed/renamed a hardware construct, restore (or relocate)
      the matching grant block — see SwissTable.h swiss_hw for the shape.
  (2) If you moved a site to a new file, update the manifest arrays in
      scripts/check-fixy-hw-discipline.sh in the SAME commit.
HINT
    exit 1
fi

printf 'check-fixy-hw-discipline: clean — all %d hardware-axis grant declarations intact.\n' \
    "${#manifest_files[@]}" >&2
exit 0
