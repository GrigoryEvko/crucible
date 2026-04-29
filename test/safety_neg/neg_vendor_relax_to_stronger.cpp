// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Vendor<SpecificBackend, T>::relax<Portable>()
// on a vendor-pinned wrapper.  Specific backend is below Portable
// in the partial-order lattice; relaxing UP to Portable would CLAIM
// more vendor portability than the source provides.
//
// THE LOAD-BEARING REJECTION FOR THE Mimic cross-vendor numerics CI
// DISCIPLINE (MIMIC.md + CRUCIBLE.md §L2).  Without it, a kernel
// pinned to NV could be silently re-typed as Portable and sent to
// a Portable-required CI gate (the cross-vendor reference oracle),
// which would then attempt to run the NV-specific kernel on an AMD
// or TPU backend, producing a runtime driver-level failure.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `VendorLattice::leq(WeakerBackend, Backend)` to a permissive
// form — would silently allow an NV-tier value to claim Portable
// compatibility.  The dispatcher's Portable-required gate would
// then admit NV-specific PTX into the cross-vendor numerics CI.
//
// Lattice direction (per VendorLattice.h docblock): Portable is at
// the TOP (strongest claim — runs everywhere); each specific vendor
// is in the middle (admits only its own consumer).  Going DOWN
// (Portable → vendor → None) is allowed.  Going UP from a specific
// vendor to Portable is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Vendor.h>

using namespace crucible::safety;

int main() {
    // Pinned at NV — bytes are NVIDIA SASS / PTX bitstream.  This
    // is what the Portable-required gate MUST reject; the relax<>
    // below is the bug-introduction path the wrapper fences.
    Vendor<VendorBackend_v::NV, int> nv_value{42};

    // Should FAIL: relax<Portable> on a Vendor<NV> wrapper.  The
    // requires-clause `VendorLattice::leq(Portable, NV)` is FALSE
    // — Portable is above NV in the lattice — so the relax<>
    // overload is excluded.  Without this fence, NV-specific
    // kernels could claim Portable compatibility and silently
    // enter the cross-vendor numerics CI's reference oracle path,
    // producing runtime driver-level failures on non-NV backends.
    auto portable_claim = std::move(nv_value).relax<VendorBackend_v::Portable>();
    return portable_claim.peek();
}
