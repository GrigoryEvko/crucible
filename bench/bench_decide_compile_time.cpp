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
// `bench::do_not_optimize(result)` (the `[[gnu::noipa]]` shim) to
// mark the bool return as observed.  Each procedure
// covers the canonical instantiation it ships in production: integer
// types for the overflow / range family, span<int32_t> for the
// ordered-sequence predicate, span<Interval<uint64_t>> for the interval
// predicate.  We do NOT iterate every primitive type — the body's
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

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include <foundation/contracts/Decide.h>
#include <foundation/effects/Row.h>

#include "bench_harness.h"

namespace dc = ::foundation::decide;
namespace eff = ::foundation::effects;

// ── Row instances for row_subset(^^P, ^^C) ────────────────────────
//
// row_subset reads the reflections of two row types; we name one
// canonical Subrow case (Payload ⊆ Ctx) so the bench covers the
// is_subrow path.  eff::Row<...> is a phantom carrier —
// no runtime state — so the call resolves to a single bool constant
// folded under -O3.  We still measure to confirm the foldability.

using PayloadRow = eff::Row<eff::Effect::Bg>;
using CtxRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc>;

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

// The sequence input of the ordered-sequence predicate, 64 elements long.
constexpr std::size_t kSeqLen = 64;

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

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

    // The sequence of weakly_increasing, built once outside the lambda:
    // 64 elements with stride 7, so the scan reaches the end.
    std::array<std::int32_t, kSeqLen> inc_seq{};
    for (std::size_t i = 0; i < kSeqLen; ++i)
        inc_seq[i] = static_cast<std::int32_t>(i * 7);

    // intervals_pairwise_disjoint fast path: 8 disjoint intervals
    // tiling [0, 800).  Both the well-formedness pass and the n²
    // pairwise pass exercise.
    std::array<dc::Interval<std::uint64_t>, 8> ivs_disjoint{};
    for (std::size_t i = 0; i < ivs_disjoint.size(); ++i) {
        ivs_disjoint[i] = dc::Interval<std::uint64_t>{
            .lo = i * 100,
            .hi = i * 100 + 100,
        };
    }

    std::printf("=== decide ===\n\n");

    // Each `bench::run(name, body)` is one Report.  Auto-batching
    // averages the lambda over 2^k calls until the timed region
    // exceeds 1000 cycles; reports are labeled "[batch-avg]" for
    // sub-cycle predicates (positive, non_negative, in_range).
    bench::Report reports[] = {
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
                       bool r = dc::valid_span<std::uint32_t>(v_u32_a, &inc_seq[0]);
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

        // ── Ordered sequence (linear scan, 64 elts) ───────────────
        bench::run("decide::weakly_increasing<int32, 64>",
                   [&] {
                       bool r = dc::weakly_increasing<std::int32_t>(
                           std::span<const std::int32_t>(inc_seq.data(), inc_seq.size()));
                       bench::do_not_optimize(r);
                   }),

        // ── Intervals (quadratic pairwise scan, 8 intervals) ──────
        bench::run("decide::intervals_pairwise_disjoint<u64, 8>",
                   [&] {
                       bool r = dc::intervals_pairwise_disjoint<std::uint64_t>(
                           std::span<const dc::Interval<std::uint64_t>>(ivs_disjoint.data(), ivs_disjoint.size()));
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
                       // exercised at the CipherTierPromotion sites.
                       bool r = dc::tier_replaces<CipherBenchTier>(CipherBenchTier::Hot, CipherBenchTier::Warm);
                       bench::do_not_optimize(r);
                   }),
        bench::run("decide::row_subset(Row<Bg>, Row<Bg,Alloc>)",
                   [] {
                       // Pure compile-time: row_subset is consteval, so the
                       // call is a constant.  The bench measures the .text
                       // overhead of the noipa do_not_optimize call —
                       // confirming the predicate body contributes nothing.
                       bool r = dc::row_subset(^^PayloadRow, ^^CtxRow);
                       bench::do_not_optimize(r);
                   }),
    };

    bench::emit_reports_text(reports);
    bench::emit_reports_json(reports, json);
    return 0;
}
