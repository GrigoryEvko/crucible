// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidWarpSize with a power-of-two value past
// the structural ceiling — 256 (= bounded_above<128> + 128) in
// constexpr context.
//
// Per #896 WRAP-CostModel-2, ValidWarpSize is
// safety::Refined<safety::all_of<safety::power_of_two,
//                                 safety::bounded_above<uint16_t{128}>>,
//                  uint16_t>.
// 256 IS a power of two (`power_of_two(256) == true`) but
// `bounded_above<128>(256) == false`.
//
// The 128 ceiling is the structural cap on every shipped GPU
// (NVIDIA Volta-and-later: 32 threads, AMD GCN/CDNA/RDNA: 64 threads),
// with one bit of headroom (128) for forward compatibility.  256 is
// past every plausible silicon — beyond that point per-warp register
// pressure becomes intractable: at 65536 registers per SM and 256
// threads per warp, each thread has 256 registers, leaving zero room
// for occupancy-multiplier blocks.
//
// A HardwareProfile::warp_size = 256 escaping into the cost model would:
//   * `max_threads_per_sm()` returns `256 × max_warps_per_sm` — not
//     representative of any real silicon's threads-per-SM.
//   * `wave_efficiency()` uses tpw = num_sms × 256 — over-counts
//     available parallelism, classifies kernels as under-utilising
//     hardware that cannot exist.
//   * `sm_occupancy()` computes warp-granularity round-down using 256
//     — assigns the kernel to MAP-Elites occupancy bands for a chip
//     model that doesn't exist, corrupting the archive.
//
// Companion fixture: neg_costmodel_warp_size_not_power_of_two.cpp
//   * That one is the non-power-of-two case (33, fails power_of_two —
//     catches off-by-one drift in callers computing warp_size as
//     nominal+1).
//   * This one is the past-the-ceiling case (256, power-of-two but
//     past bounded_above<128> — catches drift where a future
//     architecture is rumoured to widen wavefronts and a deserialiser
//     accepts the rumour as truth).
//
// In constexpr context, a contract violation makes the expression non-
// constant per P1494R5 — using it where a constant is required is
// ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/CostModel.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`all_of<power_of_two, bounded_above<128>>(v)`) to be exercised
    // at compile time.  bounded_above<128>(256) == false → AllOf
    // evaluates to false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidWarpSize bad{uint16_t{256}};
    (void)bad;
    return 0;
}
