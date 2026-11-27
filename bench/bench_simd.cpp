// ═══════════════════════════════════════════════════════════════════
// bench_simd — micro-bench for foundation::simd primitives + the
// dim-hash and storage-nbytes SIMD helpers built on them.
//
// Coverage:
//   * iota_v<i64x8>() / iota_v<u64x8>() — confirm constexpr-folds
//   * prefix_mask<u64x8>(N) for N=0..8 — runtime mask construction
//   * simd::reduce_xor/add + reduce_min/max + masked variants
//   * dim_hash_simd vs dim_hash_scalar at ndim=1..8 (speedup ratio)
//   * compute_storage_nbytes_simd vs compute_storage_nbytes_scalar
//
// Single-target builds only: prints arch summary at start, no
// multi-target dispatch reporting (per the post-Path-B cleanup).
// Correctness is not tested here — that's test_simd / test_dim_hash
// / test_storage_nbytes_simd / prop_*_simd_equivalence fuzzers.
// This file is pure timing.
// ═══════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>

#include <crucible/DimHash.h>
#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <foundation/Simd.h>

#include "bench_harness.h"

using foundation::simd::i64x8;
using foundation::simd::u64x8;
using foundation::simd::u32x8;
using crucible::detail::dim_hash_simd;
using crucible::detail::dim_hash_scalar;
using crucible::detail::compute_storage_nbytes_simd;
using crucible::detail::compute_storage_nbytes_scalar;

// ── Helper: build a TensorMeta with given ndim ────────────────────

[[nodiscard]] static crucible::TensorMeta make_meta_ndim(uint8_t ndim) noexcept {
    // Common shape pattern: the sizes 5, 6, 8, 12, 20, 36, 68 and 132 for d = 0
    // to 7, and the strides of a dense tensor, which grow from the last lane.
    ::fixy::FixedArray<crucible::TensorDim, crucible::kMaxTensorNDim> sizes = crucible::zero_tensor_dims();
    for (const crucible::TensorDimArray::LaneIndex lane : sizes.indices()) {
        if (lane.raw() >= ndim) break;
        sizes.set(lane, crucible::tensor_dim((int64_t{1} << lane.raw()) + 4));
    }
    crucible::TensorMeta m{};
    m.set_shape(crucible::contiguous_tensor_shape(
                    crucible::TensorSizes{sizes.view().window(0, ndim).expect("the rank is at most 8")})
                    .expect("a contiguous shape"));
    m.dtype = crucible::ScalarType::Float;
    return m;
}

namespace {

// The inputs of the rows.  Each row holds its own copy in its home
// (bench::RowHome), so the distance from the inputs to the frame of the
// timed loop depends only on the code of the row.
struct SimdInputs {
    // Volatile counts, so the optimizer cannot fold a mask.
    volatile int count_0 = 0;
    volatile int count_4 = 4;
    volatile int count_8 = 8;

    // The descriptors of the rows of dim_hash and of the storage size.
    crucible::TensorMeta m1 = make_meta_ndim(1);
    crucible::TensorMeta m4 = make_meta_ndim(4);
    crucible::TensorMeta m8 = make_meta_ndim(8);

    // The input lanes of the reductions.  Each body of a reduction gives the
    // array to the input sink first, so each call loads the lanes again (the
    // sink of bench_harness.h).  With no sink, the optimizer reduces the array
    // one time before the timed loop.  The View of the eight lanes proves the
    // count of the load, and the member has the alignment of one register.
    alignas(64)::fixy::FixedArray<uint64_t, 8> lanes{
        0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, 0xCAFEBABEDEADBEEFULL, 0x1111222233334444ULL,
        0x5555666677778888ULL, 0x9999AAAABBBBCCCCULL, 0xDDDDEEEEFFFF1234ULL, 0x6666777788889999ULL,
    };
};

// The inputs of the row of a rank that the row reads at run time: the
// descriptor of each rank, which the body copies at the volatile rank, and
// the copy.
struct RankInputs {
    volatile uint8_t ndim_8 = 8;
    ::fixy::FixedArray<crucible::TensorMeta, crucible::kMaxTensorNDim + 1> by_rank{
        make_meta_ndim(0), make_meta_ndim(1), make_meta_ndim(2), make_meta_ndim(3), make_meta_ndim(4),
        make_meta_ndim(5), make_meta_ndim(6), make_meta_ndim(7), make_meta_ndim(8)};
    crucible::TensorMeta mvar{};
};

// The report of a row whose step runs on the inputs in a home of its own.
template <typename Step>
[[nodiscard]] bench::Report run_on_inputs(::fixy::TextView name, Step step) {
    return bench::run_in_home(name, []() noexcept { return SimdInputs{}; }, step);
}

// The eight lanes of the inputs as one vector.
[[nodiscard, gnu::always_inline]] inline u64x8 load_lanes(SimdInputs const& row) noexcept {
    return foundation::simd::load<u64x8>(row.lanes.view());
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // ── Architecture summary ──────────────────────────────────────
    //
    // Single-target build per CLAUDE.md §VIII: one ISA per binary,
    // chosen at -march= time.  Print compile-time + runtime caps
    // so readers know exactly which SIMD path was exercised.

    using namespace foundation::simd;
    using ::fixy::TextView;
    constexpr TextView yes{"yes"};
    constexpr TextView no{"no"};
    bench::print("=== simd architecture ===\n");
    bench::print("  compile-time: avx512={} avx2={} sse42={} neon={}\n", kAvx512Available ? yes : no,
                 kAvx2Available ? yes : no, kSse42Available ? yes : no, kNeonAvailable ? yes : no);
#if defined(__x86_64__) || defined(__i386__)
    bench::print("  runtime CPU:  avx512={} avx2={} sse42={}\n", runtime_supports_avx512() ? yes : no,
                 runtime_supports_avx2() ? yes : no, runtime_supports_sse42() ? yes : no);
#endif
    bench::print("\n");

    bench::print("=== simd ===\n\n");

    // Each body is always inline, so the timed loop of each row holds the
    // operation itself and not a call, whatever the inliner decides for a
    // main of 23 rows.
    ::fixy::FixedArray<bench::Report, 23> reports{
        // ─── Facade primitives ──────────────────────────────────────
        run_on_inputs("iota_v<i64x8>()                    [const constructor]",
                      [] [[gnu::always_inline]] (auto&) noexcept {
                          auto v = foundation::simd::iota_v<i64x8>();
                          bench::do_not_optimize(v);
                      }),
        run_on_inputs("iota_v<u64x8>()",
                      [] [[gnu::always_inline]] (auto&) noexcept {
                          auto v = foundation::simd::iota_v<u64x8>();
                          bench::do_not_optimize(v);
                      }),

        run_on_inputs("prefix_mask<u64x8>(0)              [empty mask]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto m = foundation::simd::prefix_mask<u64x8>(row.count_0);
                          bench::do_not_optimize(m);
                      }),
        run_on_inputs("prefix_mask<u64x8>(4)              [half mask]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto m = foundation::simd::prefix_mask<u64x8>(row.count_4);
                          bench::do_not_optimize(m);
                      }),
        run_on_inputs("prefix_mask<u64x8>(8)              [full mask]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto m = foundation::simd::prefix_mask<u64x8>(row.count_8);
                          bench::do_not_optimize(m);
                      }),

        // ─── facade reductions ───────────────────────────────────────
        run_on_inputs("simd::reduce_xor(u64x8)            [unmasked]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.lanes);
                          auto v = load_lanes(row);
                          uint64_t r = foundation::simd::reduce_xor(v);
                          bench::do_not_optimize(r);
                      }),
        run_on_inputs("simd::reduce_add(u64x8)            [unmasked]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.lanes);
                          auto v = load_lanes(row);
                          uint64_t r = foundation::simd::reduce_add(v);
                          bench::do_not_optimize(r);
                      }),
        run_on_inputs("simd::reduce_max(u64x8)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.lanes);
                          auto v = load_lanes(row);
                          uint64_t r = foundation::simd::reduce_max(v);
                          bench::do_not_optimize(r);
                      }),
        run_on_inputs("simd::reduce_min(u64x8)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.lanes);
                          auto v = load_lanes(row);
                          uint64_t r = foundation::simd::reduce_min(v);
                          bench::do_not_optimize(r);
                      }),
        run_on_inputs("simd::reduce_xor(u64x8, mask)      [masked half]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.lanes);
                          auto v = load_lanes(row);
                          auto m = foundation::simd::prefix_mask<u64x8>(row.count_4);
                          uint64_t r = foundation::simd::reduce_xor(v, m);
                          bench::do_not_optimize(r);
                      }),

        // ─── dim_hash: scalar vs SIMD across ndim ───────────────────
        //
        // At ndim=1, scalar and SIMD do the same single-dim work plus
        // SIMD's load/mask overhead → scalar should win.
        // At ndim=4-8, SIMD's parallel multiply + masked reduce wins
        // by ~3-4×.  Each body gives its descriptor to the input sink, so
        // each call hashes it again.
        run_on_inputs("dim_hash_scalar(ndim=1)            [scalar baseline]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m1);
                          uint64_t h = dim_hash_scalar(row.m1);
                          bench::do_not_optimize(h);
                      }),
        run_on_inputs("dim_hash_simd  (ndim=1)            [SIMD]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m1);
                          uint64_t h = dim_hash_simd(row.m1);
                          bench::do_not_optimize(h);
                      }),
        run_on_inputs("dim_hash_scalar(ndim=4)            [scalar baseline]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m4);
                          uint64_t h = dim_hash_scalar(row.m4);
                          bench::do_not_optimize(h);
                      }),
        run_on_inputs("dim_hash_simd  (ndim=4)            [SIMD]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m4);
                          uint64_t h = dim_hash_simd(row.m4);
                          bench::do_not_optimize(h);
                      }),
        run_on_inputs("dim_hash_scalar(ndim=8)            [scalar baseline]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m8);
                          uint64_t h = dim_hash_scalar(row.m8);
                          bench::do_not_optimize(h);
                      }),
        run_on_inputs("dim_hash_simd  (ndim=8)            [SIMD]",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          bench::mark_changed(row.m8);
                          uint64_t h = dim_hash_simd(row.m8);
                          bench::do_not_optimize(h);
                      }),

        // ─── compute_storage_nbytes: scalar vs SIMD ─────────────────
        //
        // SIMD path includes pre-screen overhead.  At ndim=1 the
        // scalar path is faster (single mul, no load).  At ndim=8
        // the SIMD wins on the dim multiply + masked accumulation.
        run_on_inputs("compute_storage_nbytes_scalar (ndim=1)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_scalar(external_tensor_meta(row.m1));
                          bench::do_not_optimize(b);
                      }),
        run_on_inputs("compute_storage_nbytes_simd   (ndim=1)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_simd(external_tensor_meta(row.m1));
                          bench::do_not_optimize(b);
                      }),
        run_on_inputs("compute_storage_nbytes_scalar (ndim=4)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_scalar(external_tensor_meta(row.m4));
                          bench::do_not_optimize(b);
                      }),
        run_on_inputs("compute_storage_nbytes_simd   (ndim=4)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_simd(external_tensor_meta(row.m4));
                          bench::do_not_optimize(b);
                      }),
        run_on_inputs("compute_storage_nbytes_scalar (ndim=8)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_scalar(external_tensor_meta(row.m8));
                          bench::do_not_optimize(b);
                      }),
        run_on_inputs("compute_storage_nbytes_simd   (ndim=8)",
                      [] [[gnu::always_inline]] (auto& row) noexcept {
                          auto b = compute_storage_nbytes_simd(external_tensor_meta(row.m8));
                          bench::do_not_optimize(b);
                      }),

        // ─── Variable-ndim sweep on the SIMD path ───────────────────
        //
        // Use volatile ndim so the compiler can't specialize per-ndim
        // — measures the realistic cost when ndim isn't known at
        // compile time.
        bench::run_in_home(
            "dim_hash_simd  (variable ndim, runtime branch)", []() noexcept { return RankInputs{}; },
            [] [[gnu::always_inline]] (auto& row) noexcept {
                row.mvar = row.by_rank.at(
                    ::fixy::Index<crucible::kMaxTensorNDim + 1>::check(static_cast<std::size_t>(row.ndim_8))
                        .expect("the rank of the bench is at most 8"));
                uint64_t h = dim_hash_simd(row.mvar);
                bench::do_not_optimize(h);
            }),
    };

    bench::emit_reports_text(reports.view());
    bench::emit_reports_json(reports.view(), json);
    return 0;
}
