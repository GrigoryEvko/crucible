// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidUtilization with a negative value
// (-0.5f) in constexpr context.
//
// Per #898 WRAP-CostModel-4, ValidUtilization is
// safety::Refined<safety::in_range<0.0f, 1.0f>, float>.
// -0.5f fails the predicate — `in_range<0.0f, 1.0f>(-0.5f) == false`
// because -0.5f < 0.0f.
//
// The [0, 1] floor pins a structural property of every utilization
// ratio in CostModel.h: every numerator (filled threads, active
// warps, occupied registers) is non-negative by construction, and
// IEEE 754 round-to-nearest preserves the sign of zero.  A negative
// utilization is structurally impossible under documented inputs but
// could escape via:
//   * a signed loop counter wrapping past zero before being
//     converted to float;
//   * a future caller computing `efficiency = filled_threads -
//     wasted_threads` where `wasted_threads > filled_threads`;
//   * a misuse of `compute_storage_nbytes` returning a saturated-up
//     value but feeding a signed-difference into the ratio formula.
//
// A ValidUtilization{-0.5f} escaping into the cost model would:
//   * skew `compute_ns = flops / (peak × 1e3 × wave_eff × occupancy)`
//     in the wrong sign — a negative effective FLOP rate produces a
//     negative compute_ns, which the `std::max(compute_ns,
//     memory_ns)` in evaluate_cost compares but does not clamp;
//     downstream the bottleneck classifier would mis-tag the kernel
//     as memory-bound when it is actually corrupt.
//   * corrupt the `wave_efficiency < 0.1f` UNDERUTIL gate at line
//     612 of CostModel.h's evaluate_cost — a -0.5f efficiency
//     trivially passes the gate as "underutilized" (actually:
//     impossibly-utilized), masking the underlying corruption with a
//     plausible-looking diagnostic.
//   * leak into MAP-Elites bucketization — a negative ratio cannot
//     correspond to any real silicon configuration, so any archive
//     entry tagged with such a value is permanently unreachable
//     from real-world workloads.
//
// Companion fixture: neg_costmodel_utilization_above_one.cpp
//   * That one is the above-one case (1.5f, fails the in_range upper
//     bound — catches inversion of numerator/denominator and wave-
//     counting off-by-one drift).
//   * This one is the below-zero case (-0.5f, fails the in_range
//     lower bound — catches sign flips and signed-to-float conversion
//     bugs).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// In constexpr context, a contract violation makes the expression non-
// constant per P1494R5 — using it where a constant is required is
// ill-formed.

#include <crucible/CostModel.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`in_range<0.0f, 1.0f>(v)`) to be exercised at compile time.
    // -0.5f < 0.0f → InRange evaluates to false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidUtilization bad{-0.5f};
    (void)bad;
    return 0;
}
