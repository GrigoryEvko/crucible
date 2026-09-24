// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-263 fixture #2 of 2 — distinct mismatch class:
// "a FEC arm declaring a vendor::intrinsic from an incompatible
//  backend↔ISA pair".
//
// cntp/Fec.h's fec_hw block pins `vendor::intrinsic<V, I>` aliases
// (avx2_/neon_intrinsic) that are CPU-backend with a host ISA family.
// Those declarations rely on V-258's vendor_isa_consistent_v<V, I>
// requires-clause to make a nonsensical pairing ill-formed at the grant
// template-id.  This fixture proves that reliance is load-bearing: a
// NEON FEC arm that tried to declare an AMD GPU backend issuing ARM
// NEON (an AMD GPU cannot decode the host ARM NEON ISA) reds at the
// intrinsic<> template-id.
//
// Distinct from neg_fixy_v_263_width_byte_mismatch.cpp: that exercises
// V-263's stride↔width consistency; this exercises the V-258 vendor↔ISA
// gate.  Distinct PAIRING from the V-262 SwissTable fixture (AMD+NEON
// here, not NV+AVX2) — the FEC-relevant trap since FEC has a NEON arm.
//
// Expected diagnostic substring: "constraints not satisfied" /
// "vendor_isa_consistent" / "intrinsic".

#include <crucible/fixy/Vendor.h>

int main() {
    namespace fv = ::crucible::fixy::vendor;
    namespace gv = ::crucible::fixy::grant::vendor;
    // Should FAIL: an AMD GPU backend cannot decode the host ARM NEON
    // ISA, so the intrinsic<AMD, NEON> template-id is ill-formed — a FEC
    // NEON arm could never legitimately declare this pairing.
    [[maybe_unused]] gv::intrinsic<fv::VendorBackend::AMD, fv::IsaTag::NEON> bad{};
    return 0;
}
