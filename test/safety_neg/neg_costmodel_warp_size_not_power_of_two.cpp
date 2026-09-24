// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidWarpSize with a non-power-of-two value
// (33) in constexpr context.
//
// Per #896 WRAP-CostModel-2, ValidWarpSize is
// safety::Refined<safety::all_of<safety::power_of_two,
//                                 safety::bounded_above<uint16_t{128}>>,
//                  uint16_t>.
// 33 is NOT a power of two — `power_of_two(33) == false` because
// 33 == 0b100001 has more than one bit set.
//
// HardwareProfile::warp_size = 33 escaping into:
//   * `max_threads_per_sm()` — would compute 33 × max_warps_per_sm,
//      producing a thread count not aligned to any hardware warp dispatch
//      granularity (32 NVIDIA / 64 AMD), and feed nonsense into
//      validate_config's C4 check (warps_per_block × warp_size ≤ 1024).
//   * `wave_efficiency()` — `tpw = num_sms * 33` produces a per-wave
//      thread count that does not correspond to any physical hardware
//      dispatch.  Downstream MAP-Elites bucketization would assign the
//      kernel to a non-existent wave-efficiency cell.
//   * `sm_occupancy()` — warp-granularity round-down `(reg_limited / 33)
//      * 33` produces wrong occupancy bands; MAP-Elites archive entries
//      for "33-thread warp" do not correspond to any real silicon.
//
// Companion fixture: neg_costmodel_warp_size_too_large.cpp
//   * That one is the past-the-ceiling case (256, power-of-two but
//     beyond bounded_above<128> — catches drift where a future
//     architecture is rumoured to widen wavefronts further than 128
//     and a deserialiser accepts the rumour as truth).
//   * This one is the non-power-of-two case (33, fails the structural
//     ALU-dispatch invariant — catches off-by-one drift where a caller
//     computes `warp_size = nominal + 1` for a fence-padding scheme).
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
    // at compile time.  power_of_two(33) == false → AllOf evaluates to
    // false → contract violation → not a constant expression →
    // ill-formed.
    constexpr crucible::ValidWarpSize bad{uint16_t{33}};
    (void)bad;
    return 0;
}
