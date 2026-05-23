// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-225 fixture #7 — advise<DontNeed> on the safe surface rejected.
//
// `CtxFitsSafeAdvise<Ctx, Advice>` requires:
//
//     !is_dangerous_advice_v<Advice>
//
// — i.e., Advice must NOT be in the dangerous set (currently
// {DontNeed}).  The safe `advise<>` surface refuses DontNeed because
// MADV_DONTNEED zeros pages out from under any concurrent ReadView
// reader (Agent 9 Bug 5: SenseHub MAP_SHARED region read race vs
// warden/Hardening.h MADV_HUGEPAGE/COLLAPSE pipeline).  Callers
// needing DontNeed must route through advise_release_aware with the
// `release_aware<RegionTag>` witness.
//
// Mismatch class: dangerous-advice on the safe surface.
// Distinct from fixtures #1-#6 (all on mmap mints) — fires on the
// advise() call boundary.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsSafeAdvise" /
//   "constraints not satisfied" / "is_dangerous_advice".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

struct AdviseRegion {};

int main() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace advice = fwmm::advice;
    namespace prot   = fwmm::prot;
    namespace share  = fwmm::share;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Construct a dummy OwnedMmap; not bound to any real region —
    // the concept check fires BEFORE the body runs.
    fwmm::OwnedMmap<AdviseRegion, prot::ReadOnly, share::Private> region{};

    // Should FAIL: advise<DontNeed> on the safe surface.  Caller
    // must use advise_release_aware<DontNeed, RegionTag>.
    [[maybe_unused]] auto r = fwmm::advise<advice::DontNeed>(ctx, region);
    return 0;
}
