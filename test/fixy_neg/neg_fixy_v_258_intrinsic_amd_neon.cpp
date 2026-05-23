// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-258 cross-vendor fixture 3/3 — AMD GPU backend + ARM ISA.
//
// `intrinsic<V, I>` is gated by `vendor_isa_consistent_v<V, I>`.  An AMD
// GPU backend issues AMDGCN, not ARM NEON; `intrinsic<AMD, NEON>` is
// ill-formed at the grant template-id.  This is the cross-GPU-vendor
// trap distinct from the host↔GPU mismatches: NEON is a host (ARM CPU)
// ISA, and AMD only admits the AMDGCN family.
//
// Mismatch class: GPU-vendor backend pinned to a host (ARM) ISA family.
// Distinct from neg_fixy_v_258_intrinsic_nv_x86.cpp (x86) and
// neg_fixy_v_258_intrinsic_cpu_ptx.cpp (host backend + GPU ISA).
//
// Expected diagnostic: "constraints not satisfied" / "vendor_isa_consistent"
// / "intrinsic".

#include <crucible/fixy/Vendor.h>

int main() {
    namespace fv = ::crucible::fixy::vendor;
    namespace gv = ::crucible::fixy::grant::vendor;
    // Should FAIL: AMD GPU cannot issue ARM NEON.
    [[maybe_unused]] gv::intrinsic<fv::VendorBackend::AMD, fv::IsaTag::NEON> bad{};
    return 0;
}
