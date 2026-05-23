// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-263 fixture #1 of 2 — distinct mismatch class:
// "declared SIMD-width grant inconsistent with the FEC kernel's block
//  stride".
//
// cntp/Fec.h's fec_hw block pins each arm's declared register width
// (bits) to the kernel's block stride (bytes × 8): the AVX2 GF(2^8)
// kernels stride 32-byte blocks = 256 bits, so the AVX2 arm MUST declare
// width_256.  The real header is consistent on every arm, so this
// fixture replicates the consistency relation with a deliberately-
// mismatched pairing (a 32-byte AVX2 block claiming width_128) to prove
// the guard is non-vacuous.
//
// Distinct from neg_fixy_v_263_vendor_arch_mismatch.cpp: that exercises
// the V-258 vendor↔ISA template-id gate; this exercises V-263's own
// stride↔width consistency invariant.  Distinct VALUES from the V-262
// SwissTable fixture (32-byte/256-bit FEC block, not 16-byte/512-bit).
//
// Expected diagnostic substring: FIXY-V-263.

#include <crucible/fixy/Simd.h>

#include <cstddef>
#include <utility>

namespace fs = ::crucible::fixy::simd;

// An AVX2 FEC kernel strides 32-byte (256-bit) blocks and must declare
// width_256.  This pairing claims width_128 — 32 × 8 = 256, NOT 128 —
// so the stride↔width consistency assert reds.
inline constexpr std::size_t kAvx2BlockBytes = 32;
static_assert(kAvx2BlockBytes * 8u == std::to_underlying(fs::WidthBits::Bits128),
    "FIXY-V-263: a 32-byte AVX2 FEC block is 256 bits and must declare "
    "simd::width_256 — declaring width_128 is an arm-declaration drift bug.");

int main() { return 0; }
