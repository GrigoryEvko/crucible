// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-258 cross-vendor fixture 2/3 — host (CPU) backend + GPU ISA.
//
// `intrinsic<V, I>` is gated by `vendor_isa_consistent_v<V, I>`.  The
// host CPU backend cannot issue CUDA PTX (`CUDA_PTX_SM90` is GPU device
// code), so `intrinsic<CPU, CUDA_PTX_SM90>` is ill-formed at the grant
// template-id.  CPU admits the x86 AND ARM families but NO GPU family.
//
// Mismatch class: host backend pinned to a GPU ISA family.  Distinct
// from neg_fixy_v_258_intrinsic_nv_x86.cpp (GPU backend + x86 ISA).
//
// Expected diagnostic: "constraints not satisfied" / "vendor_isa_consistent"
// / "intrinsic".

#include <crucible/fixy/Vendor.h>

int main() {
    namespace fv = ::crucible::fixy::vendor;
    namespace gv = ::crucible::fixy::grant::vendor;
    // Should FAIL: CPU cannot issue CUDA PTX device code.
    [[maybe_unused]] gv::intrinsic<fv::VendorBackend::CPU, fv::IsaTag::CUDA_PTX_SM90> bad{};
    return 0;
}
