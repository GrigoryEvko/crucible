// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling HotPath<WeakerTier, T>::relax<StrongerTier>()
// when StrongerTier > WeakerTier in the HotPathLattice.
//
// THE LOAD-BEARING REJECTION FOR HOT-PATH ADMISSION.  Without it,
// a value sourced from a COLD/WARM context could be re-typed as
// Hot-path-safe and silently flow into the foreground hot path,
// defeating the per-call shape budget (atomic ops + cache-line
// touches; no kernel-mediated transition per CLAUDE.md §IX).
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `HotPathLattice::leq(WeakerTier, Tier)` to a permissive form —
// would silently allow a Cold-tier value (potentially containing
// blocking I/O semantics) to claim Hot compliance.  The dispatcher's
// hot-path admission gate (per 28_04 §6.4) would then admit the
// value into a TraceRing::try_push or Vessel::dispatch_op call site,
// silently introducing a syscall / block on the foreground path.
//
// Lattice direction: Hot is at the TOP (strongest budget); Cold is
// at the BOTTOM (weakest).  Going DOWN (Hot → Warm → Cold) is
// allowed — stronger budget trivially serves weaker requirement.
// Going UP (Cold → Warm → Hot) is FORBIDDEN — would CLAIM more
// hot-path compliance than the source provides.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/HotPath.h>

using namespace crucible::safety;

int main() {
    // Pinned at Cold — the value's source is allowed to block / IO /
    // syscall.  This is what hot-path call sites MUST reject; the
    // relax<> below is the bug-introduction path the wrapper fences.
    HotPath<HotPathTier_v::Cold, int> cold_value{42};

    // Should FAIL: relax<Hot> on a Cold-pinned wrapper.  The
    // requires-clause `HotPathLattice::leq(Hot, Cold)` is FALSE —
    // Hot is above Cold in the chain — so the relax<> overload is
    // excluded.  Without this fence, a cold-context value could
    // claim Hot compliance and silently enter a hot-path call site,
    // breaking the per-call shape budget.
    auto hot_claim = std::move(cold_value).relax<HotPathTier_v::Hot>();
    return hot_claim.peek();
}
