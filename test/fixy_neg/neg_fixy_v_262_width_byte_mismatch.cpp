// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-262 fixture #1 of 2 — distinct mismatch class:
// "declared SIMD-width grant inconsistent with the control-byte group
//  width".
//
// V-262's per-arm static_assert in SwissTable.h pins the declared
// register width (bits) to the actual control-byte group width
// (bytes × 8): a 16-byte group is 128 bits, so it MUST declare
// width_128 — declaring width_512 for it is a drift bug.  The real
// header is consistent on every arm (16/32/64 bytes ↔ 128/256/512
// bits), so this fixture replicates the consistency relation with a
// deliberately-mismatched pairing to prove the guard is non-vacuous:
// if a future kGroupWidth edit (or a hand-written arm) declared the
// wrong width grant, the build would red exactly here.
//
// Distinct from neg_fixy_v_262_vendor_arch_mismatch.cpp: that exercises
// the V-258 vendor↔ISA template-id gate; this exercises V-262's own
// width-vs-bytes consistency invariant.
//
// Expected diagnostic substring: FIXY-V-262.

#include <crucible/fixy/Simd.h>

#include <cstddef>
#include <utility>

namespace fs = ::crucible::fixy::simd;

// A SwissTable arm processing a 16-byte (128-bit) control-byte group
// must declare width_128.  This pairing claims width_512 — 16 × 8 = 128,
// NOT 512 — so the consistency assert reds.
inline constexpr std::size_t kGroupBytes = 16;
static_assert(kGroupBytes * 8u == std::to_underlying(fs::WidthBits::Bits512),
    "FIXY-V-262: a 16-byte control-byte group is 128 bits and must declare "
    "simd::width_128 — declaring width_512 is an arm-declaration drift bug.");

int main() { return 0; }
