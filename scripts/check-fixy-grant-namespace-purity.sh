#!/usr/bin/env bash
#
# check-fixy-grant-namespace-purity.sh — namespace-purity guard.
#
# `crucible::fixy::grant` is the closed-world authoring namespace for
# every shipped grant tag.  C++ has no namespace-scoped specialization
# access control: a foreign translation unit that reopens
#
#     namespace crucible::fixy::grant { ... }
#
# can register a foreign type as a `which_dim<T>` specialization and
# thread that type through the IsAcceptedGrants engagement check.
#
# This script enforces the discipline at build time: ONLY
# `include/crucible/fixy/_Grant.h` may open the namespace.  Any other
# file doing so fails the build with a diagnostic naming the offending
# location.
#
# Approved exceptions (specialization-only):
#
#   * include/crucible/fixy/_Grant.h              — the canonical authoring site
#                                                    (superseded by fixy/Atom.h).
#   * include/crucible/fixy/_Fs.h                 — SyscallSurface axis-specialized
#                                                    catalog (4 fs::* parametric grants:
#                                                    mode<>/with_flag<>/durable<>/atomic_write<>).
#   * include/crucible/fixy/_Mmap.h                 — SyscallSurface axis-specialized
#                                                    catalog (5 mmap::* parametric/leaf grants:
#                                                    with_prot<>/with_share<>/with_advice<>/
#                                                    trusted_jit/release_aware<>).
#   * include/crucible/fixy/_Io.h                   — SyscallSurface axis-specialized
#                                                    catalog (5 io::* parametric grants:
#                                                    engine<>/zerocopy<>/ring_flag<>/
#                                                    sq_entries<N>/cq_entries<N>).
#
# The allowlisted headers all SPECIALIZE which_dim<>; they do NOT
# extend the grant_base hierarchy or introduce new structural-validation
# concepts.  They are functionally part of Grant.h's authoring discipline,
# split per-axis for human readability and per-axis self-tests.
#
# All other openings of `namespace crucible::fixy::grant` (including
# `test/`, `bench/`, `vis/`, `src/`, `examples/`) are review-rejected
# and CI-rejected.  The lone exception is documented attack regression
# fixtures under `test/safety_attack/` which intentionally exercise
# the residual gap; those files MUST be named `attack_fixy_grant_*`
# AND carry a comment that holds the words `known residual gap`, and
# they live under the explicit attack-regression discipline.  The old
# fixtures spell the comment with a prefix, and the guard matches only
# the words, so the prefix is not required.

# Exit status:
#   0 — clean (no forbidden namespace reopen)
#   1 — at least one violation
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-grant-namespace-purity.sh — namespace-purity guard.

Usage:
  check-fixy-grant-namespace-purity.sh              # scan; exit 1 on violation
  check-fixy-grant-namespace-purity.sh --self-test  # plant a violation, verify catch
  check-fixy-grant-namespace-purity.sh -h | --help  # usage

Exemption axes:
  include/crucible/fixy/**                          — which_dim<> authoring catalogs
  test/safety_attack/attack_fixy_grant_*.cpp        — attack fixture, needs the
  test/test_fixy_cheat_probe*.cpp                     acknowledgement comment
  test/fixy_neg/neg_fixy_project_per_domain_*.cpp     "// known residual gap"
  **/*.md, build/**, third_party/**, misc/** ...    — rg glob exclusions

Only include/crucible/fixy/_Grant.h and the per-axis catalogs beside it
may open namespace crucible::fixy::grant.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE forbidden reopen in a synthetic file under a temp
        # root; the scanner must flag it.  Also plant one instance of
        # EVERY exemption axis the guard actually has, and assert none
        # of them leak into the diagnostics:
        #
        #   * the canonical authoring site        (path allowlist)
        #   * a per-axis which_dim<> catalog      (path allowlist)
        #   * an attack fixture WITH the ack      (glob + ack comment)
        #   * an attack fixture WITHOUT the ack   (must be flagged — the
        #                                          conditional arm fires)
        #   * a Markdown file carrying the text   (rg glob exclusion)
        if ! command -v rg >/dev/null 2>&1; then
            printf 'fixy_grant_purity: SELF-TEST ABORTED — ripgrep (rg) is required.\n' >&2
            exit 2
        fi
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" \
                 "$tmp_root/include/crucible/fixy" \
                 "$tmp_root/test/safety_attack"

        # FLAGGED — a foreign TU reopening the closed-world namespace.
        cat >"$tmp_root/src/planted/planted_grant.cpp" <<'PLANTED'
// Synthetic namespace-purity fixture for --self-test.
namespace crucible::fixy::grant {
struct planted_foreign_tag final {};
}  // namespace crucible::fixy::grant
PLANTED

        # EXEMPT (path allowlist) — the canonical authoring site.
        cat >"$tmp_root/include/crucible/fixy/_Grant.h" <<'CANON'
// Synthetic canonical authoring site for --self-test.
namespace crucible::fixy::grant {
struct planted_canonical final {};
}  // namespace crucible::fixy::grant
CANON

        # EXEMPT (path allowlist) — a per-axis which_dim<> catalog.
        cat >"$tmp_root/include/crucible/fixy/_Io.h" <<'AXIS'
// Synthetic per-axis catalog for --self-test.
namespace crucible::fixy::grant {
struct planted_axis_catalog final {};
}  // namespace crucible::fixy::grant
AXIS

        # EXEMPT (glob + acknowledgement comment).
        cat >"$tmp_root/test/safety_attack/attack_fixy_grant_ack.cpp" <<'ACK'
// known residual gap — synthetic --self-test fixture.
namespace crucible::fixy::grant {
struct planted_acknowledged final {};
}  // namespace crucible::fixy::grant
ACK

        # FLAGGED (glob matches, acknowledgement comment absent).
        cat >"$tmp_root/test/safety_attack/attack_fixy_grant_noack.cpp" <<'NOACK'
// Synthetic attack fixture with NO acknowledgement comment.
namespace crucible::fixy::grant {
struct planted_unacknowledged final {};
}  // namespace crucible::fixy::grant
NOACK

        # EXEMPT (rg glob exclusion) — prose carrying the same text.
        cat >"$tmp_root/src/planted/planted_doc.md" <<'DOC'
Documentation prose for --self-test.
namespace crucible::fixy::grant {
DOC

        # EXEMPT (rg glob exclusion) — a reopen under misc/.  The glob holds
        # only when rg sees a root-relative path, so this file shows whether
        # the scan depends on the working directory.
        mkdir -p "$tmp_root/misc"
        cat >"$tmp_root/misc/planted_misc.h" <<'MISC'
namespace crucible::fixy::grant {
struct planted_misc final {};
}  // namespace crucible::fixy::grant
MISC

        result_file="$(mktemp)"
        if CRUCIBLE_FIXY_GRANT_PURITY_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'fixy_grant_purity: SELF-TEST FAILED — planted reopen not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi

        # The same scan from another working directory must give the same
        # report, byte for byte.
        script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
        other_file="$(mktemp)"
        (cd / && CRUCIBLE_FIXY_GRANT_PURITY_TEST_ROOT="$tmp_root" bash "$script_path") 2>"$other_file" || true
        if ! cmp -s "$result_file" "$other_file"; then
            printf 'fixy_grant_purity: SELF-TEST FAILED — the report from / differs from the report from the scan root.\n' >&2
            diff "$result_file" "$other_file" >&2 || true
            rm -f "$result_file" "$other_file"
            exit 2
        fi
        rm -f "$other_file"

        self_test_fail() {
            printf 'fixy_grant_purity: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }

        # The planted reopen (line 2) must be flagged.
        grep -qF 'reopen at src/planted/planted_grant.cpp:2' "$result_file" \
            || self_test_fail 'expected diagnostic for planted_grant.cpp:2 missing.'

        # The un-acknowledged attack fixture must be flagged by the
        # conditional arm, NOT by the generic reopen arm.
        grep -qF 'attack_fixy_grant_noack.cpp missing acknowledgement comment' "$result_file" \
            || self_test_fail 'un-acknowledged attack fixture was not flagged.'

        # The canonical authoring site must NOT be flagged.
        if grep -qF 'reopen at include/crucible/fixy/_Grant.h:' "$result_file"; then
            self_test_fail 'path allowlist leaked — _Grant.h was flagged.'
        fi

        # The per-axis catalog must NOT be flagged.
        if grep -qF 'reopen at include/crucible/fixy/_Io.h:' "$result_file"; then
            self_test_fail 'path allowlist leaked — per-axis catalog _Io.h was flagged.'
        fi

        # The acknowledged attack fixture must NOT be flagged.
        if grep -qF 'attack_fixy_grant_ack.cpp' "$result_file"; then
            self_test_fail 'acknowledgement comment ignored — ack fixture was flagged.'
        fi

        # The Markdown file must NOT be flagged.
        if grep -qF 'planted_doc.md' "$result_file"; then
            self_test_fail 'rg glob exclusion leaked — Markdown file was flagged.'
        fi

        # The misc/ file must NOT be flagged.
        if grep -qF 'planted_misc.h' "$result_file"; then
            self_test_fail 'rg glob exclusion leaked — misc/ file was flagged.'
        fi

        rm -f "$result_file"
        printf 'fixy_grant_purity: self-test passed — forbidden reopen caught, missing-ack arm fires, path allowlist + ack comment + Markdown glob all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'fixy_grant_purity: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_FIXY_GRANT_PURITY_TEST_ROOT:-$root}"
pattern='namespace\s+crucible\s*::\s*fixy\s*::\s*grant\s*\{'
status=0

while IFS=: read -r file line text; do
    rel="${file#./}"
    file="$scan_root/$rel"

    case "$rel" in
        include/crucible/fixy/_Grant.h)
            continue
            ;;
        include/crucible/fixy/grant/_Ctrl.h)
            # ControlFlow axis-specialized catalog (6 grant::ctrl::*
            # grants: throws<>/abort<Rationale>/longjmp_unsafe<Rationale>/
            # exit<CleanupPolicy>/coroutine<SuspensionPolicy> + builtin_trap_ok
            # / unreachable_ok markers + accept_default_strict_for_ControlFlow).
            # Specializes which_dim<> only; does NOT extend grant_base
            # hierarchy or introduce new structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/_Fs.h)
            # SyscallSurface axis-specialized catalog (4 fs::*
            # parametric grants: mode<>/with_flag<>/durable<>/atomic_write<>
            # routing the filesystem open-flag / sync-op / atomicity tiers
            # to DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/_Mmap.h)
            # SyscallSurface axis-specialized catalog (5 mmap::*
            # grants: with_prot<>/with_share<>/with_advice<>/trusted_jit/
            # release_aware<> routing the mmap-prot / share-mode / madvise
            # / Exec-gating / release-witness tiers to
            # DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/_Io.h)
            # SyscallSurface axis-specialized catalog (5 io::*
            # grants: engine<>/zerocopy<>/ring_flag<>/sq_entries<N>/
            # cq_entries<N> routing the async-engine / zerocopy /
            # io_uring_setup / queue-depth tiers to
            # DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/Hw.h)
            # HwInstruction / BarrierStrength / SimdIsa / Representation
            # axis-specialized catalog (10 grant::hw::* families: cache<>/
            # barrier<>/tsc<>/rng<>/cpuid<>/msr<>/port_io<>/asm_<>/
            # simd_width<>/vendor_intrinsic<> routing onto the hardware
            # axes).  Specializes which_dim<> only; does NOT extend grant_base
            # hierarchy or introduce new structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/_Time.h)
            # SyscallSurface / HwInstruction axis-specialized catalog
            # (3 grant::time::* families: clock_read<Source>/sleep<MaxNanos>
            # → SyscallSurface, tsc_read<Mode> → HwInstruction).  Specializes
            # which_dim<> only; does NOT extend grant_base hierarchy.
            continue
            ;;
        include/crucible/fixy/Sched.h)
            # SyscallSurface axis-specialized catalog (4 grant::sched::*
            # families: affinity / scheduler_policy<Policy> / priority<Nice> /
            # thread_name → SyscallSurface).  Specializes which_dim<> only;
            # does NOT extend grant_base hierarchy.
            continue
            ;;
        test/safety_attack/attack_fixy_grant_*.cpp)
            # Attack-regression fixture — must carry the explicit
            # acknowledgement comment of the attack-regression discipline.
            if rg -q 'known residual gap' "$file"; then
                continue
            fi
            printf 'fixy_grant_purity: attack fixture %s missing acknowledgement comment\n' \
                "$rel" >&2
            printf 'fixy_grant_purity:   add a comment "// known residual gap" near the namespace reopen\n' >&2
            status=1
            continue
            ;;
        test/test_fixy_cheat_probe.cpp | test/test_fixy_cheat_probe_theory.cpp)
            # Pre-existing cheat-probe TUs that demonstrate the attack
            # vector (foreign which_dim specialization).  Predate this
            # guard and serve as inline-static_assert regressions for the
            # IsGrantTag gate.  Must carry the same acknowledgement
            # comment so review intent is locally documented.
            if rg -q 'known residual gap' "$file"; then
                continue
            fi
            printf 'fixy_grant_purity: cheat probe %s missing acknowledgement comment\n' \
                "$rel" >&2
            printf 'fixy_grant_purity:   add a comment "// known residual gap" near the namespace reopen\n' >&2
            status=1
            continue
            ;;
        test/fixy_neg/neg_fixy_project_per_domain_*.cpp)
            # Negative-compile fixtures: a per-domain grant
            # tag with a which_dim<> spec but NO project<> spec must red at
            # the structured static_assert.  The which_dim reopen is
            # intrinsic to the test (it manufactures the structurally-valid
            # but unprojected tag), so it exercises the same residual gap as
            # the cheat probes above and carries the same acknowledgement.
            if rg -q 'known residual gap' "$file"; then
                continue
            fi
            printf 'fixy_grant_purity: neg fixture %s missing acknowledgement comment\n' \
                "$rel" >&2
            printf 'fixy_grant_purity:   add a comment "// known residual gap" near the namespace reopen\n' >&2
            status=1
            continue
            ;;
    esac

    printf 'fixy_grant_purity: forbidden namespace reopen at %s:%s\n' "$rel" "$line" >&2
    printf 'fixy_grant_purity: %s\n' "$text" >&2
    status=1
# rg matches an exclude glob against the path it prints, which is relative
# to the search argument.  Search `.` from inside the scan root, so each
# glob sees a root-relative path from any working directory.
done < <(
    cd "$scan_root" && rg -n --no-heading --pcre2 \
        --glob '!build*/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        --glob '!misc/**' \
        --glob '!**/*.md' \
        --glob '!scripts/check-fixy-grant-namespace-purity.sh' \
        "$pattern" . || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'fixy_grant_purity: only include/crucible/fixy/_Grant.h may open namespace crucible::fixy::grant.\n' >&2
    printf 'fixy_grant_purity: attack regression fixtures (test/safety_attack/attack_fixy_grant_*.cpp) must carry an explicit "// known residual gap" acknowledgement comment.\n' >&2
fi

exit "$status"
