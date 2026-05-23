// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-258 cross-vendor fixture 1/3 — NVIDIA backend + x86 ISA.
//
// `intrinsic<V, I>` is gated by `vendor_isa_consistent_v<V, I>`.  An
// NVIDIA GPU backend (`NV`) cannot decode x86 AVX2 instructions, so
// `intrinsic<NV, AVX2>` is ill-formed at the grant template-id.  Without
// the gate a kernel legalized for NVIDIA PTX could be relabelled as
// issuing host AVX2 and dispatched to silicon that cannot execute it.
//
// Mismatch class: GPU-vendor backend pinned to a host (x86) ISA family.
// Distinct from neg_fixy_v_258_intrinsic_cpu_ptx.cpp (host backend + GPU
// ISA) and neg_fixy_v_258_intrinsic_amd_neon.cpp (GPU backend + ARM ISA).
//
// Expected diagnostic: "constraints not satisfied" / "vendor_isa_consistent"
// / "intrinsic".

#include <crucible/fixy/Vendor.h>

int main() {
    namespace fv = ::crucible::fixy::vendor;
    namespace gv = ::crucible::fixy::grant::vendor;
    // Should FAIL: NV cannot issue x86 AVX2.
    [[maybe_unused]] gv::intrinsic<fv::VendorBackend::NV, fv::IsaTag::AVX2> bad{};
    return 0;
}
