// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-234 fixture #2 — advise_release_aware without Permission proof.
//
// V-225 shipped `advise_release_aware<Advice, RegionTag>(ctx, region)`
// with the dangerous-Advice concept gate; V-234 extends it to demand
// a `Permission<RegionTag> const&` borrow proof so the type system
// witnesses that the caller holds exclusive access to RegionTag.
//
// The two-step compile-time + runtime gate that closes Agent 9 Bug 5:
//
//   (1) The dangerous-Advice gate (CtxFitsReleaseAwareAdvise — V-225
//       shipped) statically routes DontNeed off the safe surface onto
//       this one.
//   (2) The Permission<RegionTag> const& parameter (V-234 ships here)
//       makes "I am the exclusive holder" load-bearing in the
//       signature.  Calling without the proof fires THIS fixture —
//       the type system rejects a call that hasn't named WHICH
//       region's exclusive permission witnesses the upgrade.
//
// Combined with `SharedPermissionPool<RegionTag>::try_upgrade()`'s
// atomic CAS that only transitions when all outstanding shares have
// been deposited, the borrow's existence is the runtime proof that no
// live shared reader exists at the call site.  CollisionCatalog rule
// `M001_DontNeedRequiresReleaseAware` names this collision class for
// audit purposes.
//
// Mismatch class: missing-permission-proof.  Distinct from
// `neg_fixy_v_225_advise_dontneed_safe_surface` (which fires on the
// SAFE surface — wrong routing) and from V-234 fixture #3 (cross-tag
// permission — wrong region).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "too few arguments" /
//   "Permission" / "constraints not satisfied".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct DontNeedRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace advice = fwmm::advice;
    namespace prot   = fwmm::prot;
    namespace share  = fwmm::share;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Construct a dummy OwnedMmap; the concept check fires BEFORE
    // the body runs, so an unmapped region is fine.
    fwmm::OwnedMmap<DontNeedRegion, prot::ReadOnly, share::Private> region{};

    // Should FAIL: missing `Permission<DontNeedRegion> const&` argument
    // demanded by V-234's signature.  The compiler emits "too few
    // arguments" / "no matching function" against the four-parameter
    // signature (ctx, region, exclusive_proof).
    //
    // V-225 signature was (Ctx, OwnedMmap&) — 2 params; V-234 is
    // (Ctx, OwnedMmap&, Permission<RegionTag> const&) — 3.  Callers
    // who haven't upgraded their SharedPermissionPool to V-234 see
    // THIS error at every release-aware call site.
    [[maybe_unused]] auto r = fwmm::advise_release_aware<advice::DontNeed,
                                                        DontNeedRegion>(ctx, region);
    return 0;
}
