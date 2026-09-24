// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidUtilization with a value above 1.0f
// (1.5f) in constexpr context.
//
// Per #898 WRAP-CostModel-4, ValidUtilization is
// safety::Refined<safety::in_range<0.0f, 1.0f>, float>.
// 1.5f fails the predicate — `in_range<0.0f, 1.0f>(1.5f) == false`
// because 1.5f > 1.0f.
//
// The [0, 1] ceiling pins a structural property of every utilization
// ratio in CostModel.h:
//   * `wave_efficiency()` — ratio of filled threads / total dispatched
//     threads.  By construction `waves × tpw ≥ elements`, so the
//     quotient lands in [0, 1] exactly under IEEE 754 round-to-nearest
//     (monotonic on non-negative inputs).
//   * `sm_occupancy()` — ratio of `min(reg_limited, smem_limited,
//     max_threads) / max_threads`.  By construction the numerator is
//     std::min'd with the denominator, so the quotient lands in [0, 1]
//     exactly.
//
// A ValidUtilization{1.5f} escaping into the cost model would:
//   * inflate `compute_ns = flops / (peak × 1e3 × wave_eff × occupancy)`
//     downward — wave_eff > 1 implies an effective FLOP rate beyond
//     the silicon's peak, which classifies kernels as compute-bound
//     when they are actually memory-bound (or worse, masks a
//     misclassified bottleneck).
//   * corrupt MAP-Elites occupancy/utilization bucketization — a 1.5f
//     occupancy maps to a non-existent cell, leaking archive entries
//     into a phantom "more-than-100%-occupied SM" partition.
//   * silently bypass the `wave_efficiency < 0.1f` UNDERUTIL gate at
//     line 612 of CostModel.h's evaluate_cost — a wrap-around or sign
//     flip that pushed a small ratio above 1.0 would no longer fire
//     UNDERUTIL.
//
// Companion fixture: neg_costmodel_utilization_negative.cpp
//   * That one is the below-zero case (-0.5f, fails the
//     in_range lower bound — catches sign flips and signed-to-float
//     conversion bugs).
//   * This one is the above-one case (1.5f, fails the in_range upper
//     bound — catches inversion of numerator/denominator and
//     wave-counting off-by-one drift where the formula divides by a
//     rounded-down denominator).
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
    // 1.5f > 1.0f → InRange evaluates to false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidUtilization bad{1.5f};
    (void)bad;
    return 0;
}
