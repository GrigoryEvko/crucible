// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Vendor<NV, T>::relax<AMD>() — relaxing across
// two distinct, mutually INCOMPARABLE middle vendors.
//
// THIS IS THE SINGLE MOST LOAD-BEARING TEST FOR THE PARTIAL-ORDER
// VendorLattice DESIGN.  In a chain interpretation (the spec's
// suggested approach at 28_04 §4.3.9), `leq(AMD, NV)` would be
// either TRUE or FALSE based on the chain's arbitrary ordering of
// vendors — silently admitting one direction of cross-vendor
// re-typing while rejecting the other.  In the partial-order
// interpretation we ship, BOTH directions are correctly rejected
// because NV and AMD are incomparable.
//
// Concrete bug-class this catches: a refactor that "simplifies"
// VendorLattice into a chain (perhaps to share the ChainLatticeOps
// base used by the eight sister chain wrappers) would silently
// allow `Vendor<NV>::relax<AMD>()`.  An NV-pinned kernel could then
// be re-typed as AMD-pinned and sent to mimic::am::launch_kernel,
// where the AMDGPU driver would reject the NVIDIA SASS bitstream
// at runtime — minutes into the run, on the wrong continent.
//
// VendorLattice.h's `non_distributive_witness` block at compile
// time is the algebraic-shape guard against the same regression
// (a chain is always distributive; the partial-order is not).
// This neg-compile is the wrapper-surface guard.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Vendor.h>

using namespace crucible::safety;

int main() {
    // Pinned at NV — bytes are NVIDIA SASS / PTX bitstream.
    Vendor<VendorBackend_v::NV, int> nv_value{42};

    // Should FAIL: relax<AMD> on a Vendor<NV>.  The requires-clause
    // `VendorLattice::leq(AMD, NV)` is FALSE — NV and AMD are
    // mutually incomparable in the partial-order lattice — so the
    // relax<> overload is excluded.  Without this fence, NVIDIA
    // SASS could re-type as AMDGPU and silently flow into
    // mimic::am::launch_kernel.
    auto amd_claim = std::move(nv_value).relax<VendorBackend_v::AMD>();
    return amd_claim.peek();
}
