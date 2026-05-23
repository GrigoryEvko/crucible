#!/usr/bin/env bash
#
# fixy-CR-09 — namespace-purity guard.
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
# `include/crucible/fixy/Grant.h` may open the namespace.  Any other
# file doing so fails the build with a diagnostic naming the offending
# location.
#
# Approved exceptions (specialization-only):
#
#   * include/crucible/fixy/Grant.h               — the canonical authoring site.
#   * include/crucible/fixy/Fp.h                  — V-092 FpMode axis-specialized catalog
#                                                    (12 with_fp_* parametric grants + fp_strict_ieee).
#   * include/crucible/fixy/Fs.h                  — V-224 SyscallSurface axis-specialized
#                                                    catalog (4 fs::* parametric grants:
#                                                    mode<>/with_flag<>/durable<>/atomic_write<>).
#   * include/crucible/fixy/Mmap.h                 — V-225 SyscallSurface axis-specialized
#                                                    catalog (5 mmap::* parametric/leaf grants:
#                                                    with_prot<>/with_share<>/with_advice<>/
#                                                    trusted_jit/release_aware<>).
#   * include/crucible/fixy/Io.h                   — V-226 SyscallSurface axis-specialized
#                                                    catalog (5 io::* parametric grants:
#                                                    engine<>/zerocopy<>/ring_flag<>/
#                                                    sq_entries<N>/cq_entries<N>).
#   * include/crucible/fixy/syscall/Family.h      — V-098 SyscallSurface axis-specialized
#                                                    catalog (9 family-tier grants).
#   * include/crucible/fixy/syscall/Per.h         — V-098 SyscallSurface axis-specialized
#                                                    catalog (per<SyscallId> parametric grants).
#   * include/crucible/fixy/syscall/Ioctl.h       — V-099 SyscallSurface axis-specialized
#                                                    catalog (ioctl::vendor<> + ioctl::subsystem<>
#                                                    parametric grants).
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
# AND carry a `// fixy-CR-09: known residual gap` comment, and they
# live under the explicit attack-regression discipline (CR-05 pattern).

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pattern='namespace\s+crucible\s*::\s*fixy\s*::\s*grant\s*\{'
status=0

while IFS=: read -r file line text; do
    rel="${file#"$root"/}"

    case "$rel" in
        include/crucible/fixy/Grant.h)
            continue
            ;;
        include/crucible/fixy/grant/Ctrl.h)
            # V-244 ControlFlow axis-specialized catalog (6 grant::ctrl::*
            # grants: throws<>/abort<Rationale>/longjmp_unsafe<Rationale>/
            # exit<CleanupPolicy>/coroutine<SuspensionPolicy> + builtin_trap_ok
            # / unreachable_ok markers + accept_default_strict_for_ControlFlow).
            # Specializes which_dim<> only; does NOT extend grant_base
            # hierarchy or introduce new structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/Fp.h)
            # V-092 FpMode axis-specialized catalog (12 with_fp_* parametric
            # grants + fp_strict_ieee).  Specializes which_dim<> only;
            # does NOT extend grant_base hierarchy or introduce structural
            # validation concepts.
            continue
            ;;
        include/crucible/fixy/Fs.h)
            # V-224 SyscallSurface axis-specialized catalog (4 fs::*
            # parametric grants: mode<>/with_flag<>/durable<>/atomic_write<>
            # routing the filesystem open-flag / sync-op / atomicity tiers
            # to DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/Mmap.h)
            # V-225 SyscallSurface axis-specialized catalog (5 mmap::*
            # grants: with_prot<>/with_share<>/with_advice<>/trusted_jit/
            # release_aware<> routing the mmap-prot / share-mode / madvise
            # / Exec-gating / Bug-5 release-witness tiers to
            # DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/Io.h)
            # V-226 SyscallSurface axis-specialized catalog (5 io::*
            # grants: engine<>/zerocopy<>/ring_flag<>/sq_entries<N>/
            # cq_entries<N> routing the async-engine / zerocopy /
            # io_uring_setup / queue-depth tiers to
            # DimensionAxis::SyscallSurface).  Specializes which_dim<>
            # only; does NOT extend grant_base hierarchy or introduce new
            # structural-validation concepts.
            continue
            ;;
        include/crucible/fixy/syscall/Family.h)
            # V-098 SyscallSurface axis-specialized catalog (9 family-tier
            # grants).  Specializes which_dim<> + family_tier<> only.
            continue
            ;;
        include/crucible/fixy/syscall/Per.h)
            # V-098 SyscallSurface axis-specialized catalog
            # (per<SyscallId> parametric grants).  Specializes which_dim<>
            # + family_tier<> only.
            continue
            ;;
        include/crucible/fixy/syscall/Ioctl.h)
            # V-099 SyscallSurface axis-specialized catalog
            # (ioctl::vendor<IoctlVendor> + ioctl::subsystem<IoctlSubsystem>
            # parametric grants).  Specializes which_dim<> + family_tier<>
            # only.
            continue
            ;;
        test/safety_attack/attack_fixy_grant_*.cpp)
            # Attack-regression fixture — must carry the explicit
            # acknowledgement comment per the CR-05 attack pattern.
            if rg -q 'fixy-CR-09: known residual gap' "$file"; then
                continue
            fi
            printf 'fixy_grant_purity: attack fixture %s missing acknowledgement comment\n' \
                "$rel" >&2
            printf 'fixy_grant_purity:   add a comment "// fixy-CR-09: known residual gap" near the namespace reopen\n' >&2
            status=1
            continue
            ;;
        test/test_fixy_cheat_probe.cpp | test/test_fixy_cheat_probe_theory.cpp)
            # Pre-existing cheat-probe TUs that demonstrate the attack
            # vector (foreign which_dim specialization).  Predate CR-09
            # and serve as inline-static_assert regressions for the
            # IsGrantTag gate.  Must carry the same acknowledgement
            # comment so review intent is locally documented.
            if rg -q 'fixy-CR-09: known residual gap' "$file"; then
                continue
            fi
            printf 'fixy_grant_purity: cheat probe %s missing acknowledgement comment\n' \
                "$rel" >&2
            printf 'fixy_grant_purity:   add a comment "// fixy-CR-09: known residual gap" near the namespace reopen\n' >&2
            status=1
            continue
            ;;
    esac

    printf 'fixy_grant_purity: forbidden namespace reopen at %s:%s\n' "$rel" "$line" >&2
    printf 'fixy_grant_purity: %s\n' "$text" >&2
    status=1
done < <(
    rg -n --no-heading --pcre2 \
        --glob '!build/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        --glob '!misc/**' \
        --glob '!**/*.md' \
        --glob '!scripts/check-fixy-grant-namespace-purity.sh' \
        "$pattern" "$root" || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'fixy_grant_purity: only include/crucible/fixy/Grant.h may open namespace crucible::fixy::grant.\n' >&2
    printf 'fixy_grant_purity: attack regression fixtures (test/safety_attack/attack_fixy_grant_*.cpp) must carry an explicit "// fixy-CR-09: known residual gap" acknowledgement comment.\n' >&2
fi

exit "$status"
