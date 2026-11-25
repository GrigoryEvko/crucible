// SPDX-License-Identifier: Apache-2.0
//
// Cost-regression bench for the foundation::decide predicate catalog.
//
// Why a bench?  Every `decide::Procedure(...)` is `constexpr` and the
// production discipline embeds it inside `CRUCIBLE_PRE(...)` /
// `CRUCIBLE_POST(...)` macros that fan out into:
//
//   1) consteval evaluation at every static_assert / neg-compile
//      fixture call site (literal compile-time cost),
//   2) runtime-mode contract-violation checks under the `enforce`
//      semantic (release-mode cost when contracts are armed),
//   3) [[assume]] hoisting under the `ignore` semantic (no runtime
//      cost — but the constexpr body's complexity still translates to
//      consteval evaluation cost at every cite site).
//
// The cost we track here is the runtime evaluation of the predicate
// body — a faithful proxy for consteval evaluation time, since the
// constant-evaluator runs the SAME function body the runtime branch
// does.  A future Decide procedure that adds template depth or heavy
// SFINAE will register as a runtime regression first.
//
// Methodology — every input is volatile-seeded outside the lambda so
// constant-folding can't elide the body.  Lambda body uses
// `bench::do_not_optimize(result)` (the asm sink of the harness) to
// mark the bool return as observed.  Each procedure
// covers the canonical instantiation it ships in production: integer
// types for the overflow / range family, and a View of Interval<uint64_t>
// for the interval predicate.  We do NOT iterate every primitive type — the body's
// asymptotic shape is independent of T width, and each extra
// instantiation adds template-noise to the bench list without
// signal.
//
// Usage:
//
//   cmake --preset bench && cmake --build --preset bench --target bench_decide_compile_time
//   ./build-bench/bench/bench_decide_compile_time
//
// The bench prints per-procedure p50 / p99 / p99.9 / max latency.
// CI compares medians against a recorded baseline; a regression
// > 5% on any procedure flags a Decide-catalog change for review.

#include <cstddef>
#include <cstdint>

#include <foundation/contracts/Decide.h>
#include <foundation/core/Array.h>

#include "bench_harness.h"

namespace dc = ::foundation::decide;

// ── TierTag for tier_replaces — CipherTier is the canonical tag ──
//
// The procedure docstring in foundation/contracts/Decide.h names the
// cipher tiers as its production cite slot.  Use the Hot/Warm/Cold
// ordinals.

enum class CipherBenchTier : std::uint8_t {
    Cold = 0,
    Warm = 1,
    Hot = 2,
};

namespace {

// The buffer that valid_span points into, 64 elements long.
constexpr std::size_t kSeqLen = 64;

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // ── Volatile seeds — defeat constant-folding ───────────────────
    //
    // Each volatile read forces the compiler to load from memory at
    // every iteration, foiling the constant evaluator's attempt to
    // fold the entire bench body to a single boolean.  Without this
    // the bench measures `(void)0` and reports 0 ns for everything.

    volatile std::int32_t v_int_pos = 12345;
    volatile std::int32_t v_lo = -1000;
    volatile std::int32_t v_hi = 100000;
    volatile std::uint64_t v_u64_a = 0x1234567890ABCDEFULL;
    volatile std::uint64_t v_u64_b = 0xCAFEBABEDEADBEEFULL;
    volatile std::uint32_t v_u32_a = 35;
    volatile std::uint64_t v_pow2 = 0x100;
    volatile std::uint64_t v_pow2_bound = 0x10000;
    volatile bool v_bool_t = true;
    volatile bool v_bool_f = false;

    // The buffer of valid_span, built once outside the lambda.
    ::fixy::FixedArray<std::int32_t, kSeqLen> inc_seq{};
    for (::fixy::Index<kSeqLen> const i : inc_seq.indices())
        inc_seq.set(i, static_cast<std::int32_t>(i.raw() * 7));

    // The input of intervals_pairwise_disjoint: 8 disjoint intervals that
    // tile [0, 800).  The pass of well-formedness and the quadratic pass of
    // the pairs both run.
    ::foundation::core::FixedArray<dc::Interval<std::uint64_t>, 8> intervals_disjoint{};
    for (auto const position : intervals_disjoint.indices()) {
        std::uint64_t const low = 100 * position.raw();
        intervals_disjoint.set(position, dc::Interval<std::uint64_t>{.lo = low, .hi = low + 100});
    }

    bench::print("=== decide ===\n\n");

    // Each `bench::run(name, body)` is one Report.  Auto-batching
    // averages the lambda over 2^k calls until the timed region
    // exceeds 1000 cycles; reports are labeled "[batch-avg]" for
    // sub-cycle predicates (positive, non_negative, in_range).
    ::fixy::FixedArray<bench::Report, 10> reports{
        // ── Scalar predicates (sub-nanosecond expected) ───────────
        bench::run("decide::positive<int32>",
                   [&] {
                       bool r = dc::positive<std::int32_t>(v_int_pos);
                       bench::do_not_optimize(r);
                   }),
        bench::run("decide::non_negative<int32>",
                   [&] {
                       bool r = dc::non_negative<std::int32_t>(v_int_pos);
                       bench::do_not_optimize(r);
                   }),
        bench::run("decide::in_range<int32>",
                   [&] {
                       bool r = dc::in_range<std::int32_t>(v_int_pos, v_lo, v_hi);
                       bench::do_not_optimize(r);
                   }),
        bench::run("decide::is_non_zero<u64>",
                   [&] {
                       // is_non_zero<T> takes T const& — peel volatile via prvalue
                       // copy so the reference can bind without qualifier discard.
                       std::uint64_t x = v_u64_a;
                       bool r = dc::is_non_zero<std::uint64_t>(x);
                       bench::do_not_optimize(r);
                   }),
        bench::run("decide::valid_span<u32>",
                   [&] {
                       bool r = dc::valid_span<std::uint32_t>(v_u32_a, &inc_seq.at(::fixy::index<0>()));
                       bench::do_not_optimize(r);
                   }),

        // ── Overflow guards (single-instruction expected) ─────────
        bench::run("decide::no_overflow_sum<u64>",
                   [&] {
                       bool r = dc::no_overflow_sum<std::uint64_t>(v_u64_a, v_u64_b);
                       bench::do_not_optimize(r);
                   }),

        // ── Power of two (single-instruction expected) ────────────
        bench::run("decide::is_power_of_two_le<u64>",
                   [&] {
                       bool r = dc::is_power_of_two_le<std::uint64_t>(v_pow2, v_pow2_bound);
                       bench::do_not_optimize(r);
                   }),

        // ── Intervals (quadratic pass of the pairs, 8 intervals) ──
        bench::run("decide::intervals_pairwise_disjoint<u64, 8>",
                   [&] {
                       bool r = dc::intervals_pairwise_disjoint(intervals_disjoint.view());
                       bench::do_not_optimize(r);
                   }),

        bench::run("decide::implies(bool,bool)",
                   [&] {
                       bool r = dc::implies(v_bool_t, v_bool_f);
                       bench::do_not_optimize(r);
                   }),

        // ── Domain predicates ─────────────────────────────────────
        bench::run("decide::tier_replaces<CipherBenchTier>",
                   [&] {
                       // Hot-replaces-Warm: the canonical promotion case
                       // exercised at the CipherTierPromotion sites.  The two
                       // tiers go through the input sink, so the compiler
                       // does not fold the call to a constant.
                       bool r = dc::tier_replaces<CipherBenchTier>(bench::make_opaque(CipherBenchTier::Hot),
                                                                   bench::make_opaque(CipherBenchTier::Warm));
                       bench::do_not_optimize(r);
                   }),
    };

    bench::emit_reports_text(reports.view());
    bench::emit_reports_json(reports.view(), json);
    return 0;
}
