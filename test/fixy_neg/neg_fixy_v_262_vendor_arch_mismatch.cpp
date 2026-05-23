// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-262 fixture #2 of 2 — distinct mismatch class:
// "a SwissTable arm declaring a vendor::intrinsic from an incompatible
//  backend↔ISA pair".
//
// V-262's per-arm declarations pin `vendor::intrinsic<V, I>` aliases
// (avx512bw_/avx2_/sse2_/neon_intrinsic) that are all CPU-backend with a
// host ISA family.  Those declarations rely on V-258's
// vendor_isa_consistent_v<V, I> requires-clause to make a nonsensical
// pairing ill-formed at the grant template-id.  This fixture proves that
// reliance is load-bearing: an x86 control-byte probe arm that tried to
// declare an NVIDIA backend issuing AVX2 (a GPU cannot decode x86) reds
// at the intrinsic<> template-id.
//
// Distinct from neg_fixy_v_262_width_byte_mismatch.cpp: that exercises
// V-262's width↔group-bytes consistency invariant; this exercises the
// V-258 vendor↔ISA gate the vendor declarations depend on.
//
// Expected diagnostic substring: "constraints not satisfied" /
// "vendor_isa_consistent" / "intrinsic".

#include <crucible/fixy/Vendor.h>

int main() {
    namespace fv = ::crucible::fixy::vendor;
    namespace gv = ::crucible::fixy::grant::vendor;
    // Should FAIL: an NVIDIA backend cannot decode the x86 AVX2 ISA, so
    // the intrinsic<NV, AVX2> template-id is ill-formed — a SwissTable
    // x86 arm could never legitimately declare this pairing.
    [[maybe_unused]] gv::intrinsic<fv::VendorBackend::NV, fv::IsaTag::AVX2> bad{};
    return 0;
}
