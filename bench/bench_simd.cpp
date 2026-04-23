// ═══════════════════════════════════════════════════════════════════
// bench_simd — micro-bench for crucible::simd primitives + the
// dim-hash and storage-nbytes SIMD helpers built on them.
//
// Coverage:
//   * iota_v<i64x8>() / iota_v<u64x8>() — confirm constexpr-folds
//   * prefix_mask<u64x8>(N) for N=0..8 — runtime mask construction
//   * std::simd::reduce + reduce_min/max + masked variants
//   * dim_hash_simd vs dim_hash_scalar at ndim=1..8 (speedup ratio)
//   * compute_storage_nbytes_simd vs compute_storage_nbytes_scalar
//
// Single-target builds only: prints arch summary at start, no
// multi-target dispatch reporting (per the post-Path-B cleanup).
// Correctness is NOT tested here — that's test_simd / test_dim_hash
// / test_storage_nbytes_simd / prop_*_simd_equivalence fuzzers.
// This file is pure timing.
// ═══════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstdio>

#include <crucible/DimHash.h>
#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/safety/Simd.h>

#include "bench_harness.h"

using crucible::simd::i64x8;
using crucible::simd::u64x8;
using crucible::simd::u32x8;
using crucible::detail::dim_hash_simd;
using crucible::detail::dim_hash_scalar;
using crucible::detail::compute_storage_nbytes_simd;
using crucible::detail::compute_storage_nbytes_scalar;

// ── Helper: build a TensorMeta with given ndim ────────────────────

[[nodiscard]] static crucible::TensorMeta make_meta_ndim(uint8_t ndim) noexcept {
    crucible::TensorMeta m{};
    m.ndim = ndim;
    m.dtype = crucible::ScalarType::Float;
    // Common shape pattern: increasing strides, decreasing sizes.
    int64_t stride = 1;
    for (int8_t d = static_cast<int8_t>(ndim) - 1; d >= 0; --d) {
        m.sizes[d] = (1u << d) + 4;  // 5, 6, 8, 12, 20, 36, 68, 132 for d=0..7
        m.strides[d] = stride;
        stride *= m.sizes[d];
    }
    return m;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // ── Architecture summary ──────────────────────────────────────
    //
    // Single-target build per CLAUDE.md §VIII: one ISA per binary,
    // chosen at -march= time.  Print compile-time + runtime caps
    // so readers know exactly which SIMD path was exercised.

    using namespace crucible::simd;
    std::printf("=== simd architecture ===\n");
    std::printf("  compile-time: avx512=%s avx2=%s sse42=%s neon=%s\n",
        kAvx512Available ? "yes" : "no",
        kAvx2Available   ? "yes" : "no",
        kSse42Available  ? "yes" : "no",
        kNeonAvailable   ? "yes" : "no");
#if defined(__x86_64__) || defined(__i386__)
    std::printf("  runtime CPU:  avx512=%s avx2=%s sse42=%s\n",
        runtime_supports_avx512() ? "yes" : "no",
        runtime_supports_avx2()   ? "yes" : "no",
        runtime_supports_sse42()  ? "yes" : "no");
#endif
    std::printf("\n");

    // ── Volatile inputs to defeat constant folding ────────────────

    volatile uint8_t v_ndim_8 = 8;
    volatile int v_count_0 = 0;
    volatile int v_count_4 = 4;
    volatile int v_count_8 = 8;

    // Prebuilt TensorMetas at various ndim for dim_hash benches.
    auto m1 = make_meta_ndim(1);
    auto m4 = make_meta_ndim(4);
    auto m8 = make_meta_ndim(8);

    // Input vectors for std::simd primitives (volatile-loaded to
    // prevent constant-folding the entire reduction).
    alignas(64) static uint64_t v_in[8] = {
        0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL,
        0xCAFEBABEDEADBEEFULL, 0x1111222233334444ULL,
        0x5555666677778888ULL, 0x9999AAAABBBBCCCCULL,
        0xDDDDEEEEFFFF1234ULL, 0x6666777788889999ULL,
    };
    auto load_u64x8 = []() {
        return std::simd::unchecked_load<u64x8>(
            v_in, u64x8::size(), std::simd::flag_aligned);
    };

    std::printf("=== simd ===\n\n");

    bench::Report reports[] = {
        // ─── Facade primitives ──────────────────────────────────────
        bench::run("iota_v<i64x8>()                    [const constructor]", [&]{
            auto v = crucible::simd::iota_v<i64x8>();
            bench::do_not_optimize(v);
        }),
        bench::run("iota_v<u64x8>()", [&]{
            auto v = crucible::simd::iota_v<u64x8>();
            bench::do_not_optimize(v);
        }),

        bench::run("prefix_mask<u64x8>(0)              [empty mask]", [&]{
            auto m = crucible::simd::prefix_mask<u64x8>(v_count_0);
            bench::do_not_optimize(m);
        }),
        bench::run("prefix_mask<u64x8>(4)              [half mask]", [&]{
            auto m = crucible::simd::prefix_mask<u64x8>(v_count_4);
            bench::do_not_optimize(m);
        }),
        bench::run("prefix_mask<u64x8>(8)              [full mask]", [&]{
            auto m = crucible::simd::prefix_mask<u64x8>(v_count_8);
            bench::do_not_optimize(m);
        }),

        // ─── std::simd reductions ────────────────────────────────────
        bench::run("std::simd::reduce(u64x8, xor)      [unmasked]", [&]{
            auto v = load_u64x8();
            uint64_t r = std::simd::reduce(v, std::bit_xor<>{});
            bench::do_not_optimize(r);
        }),
        bench::run("std::simd::reduce(u64x8, +)        [unmasked]", [&]{
            auto v = load_u64x8();
            uint64_t r = std::simd::reduce(v, std::plus<>{});
            bench::do_not_optimize(r);
        }),
        bench::run("std::simd::reduce_max(u64x8)", [&]{
            auto v = load_u64x8();
            uint64_t r = std::simd::reduce_max(v);
            bench::do_not_optimize(r);
        }),
        bench::run("std::simd::reduce_min(u64x8)", [&]{
            auto v = load_u64x8();
            uint64_t r = std::simd::reduce_min(v);
            bench::do_not_optimize(r);
        }),
        bench::run("std::simd::reduce(u64x8, mask, xor) [masked half]", [&]{
            auto v = load_u64x8();
            auto m = crucible::simd::prefix_mask<u64x8>(v_count_4);
            uint64_t r = std::simd::reduce(v, m, std::bit_xor<>{}, 0ULL);
            bench::do_not_optimize(r);
        }),

        // ─── dim_hash: scalar vs SIMD across ndim ───────────────────
        //
        // At ndim=1, scalar and SIMD do the same single-dim work plus
        // SIMD's load/mask overhead → scalar should win.
        // At ndim=4-8, SIMD's parallel multiply + masked reduce wins
        // by ~3-4×.
        bench::run("dim_hash_scalar(ndim=1)            [scalar baseline]", [&]{
            uint64_t h = dim_hash_scalar(m1);
            bench::do_not_optimize(h);
        }),
        bench::run("dim_hash_simd  (ndim=1)            [SIMD]", [&]{
            uint64_t h = dim_hash_simd(m1);
            bench::do_not_optimize(h);
        }),
        bench::run("dim_hash_scalar(ndim=4)            [scalar baseline]", [&]{
            uint64_t h = dim_hash_scalar(m4);
            bench::do_not_optimize(h);
        }),
        bench::run("dim_hash_simd  (ndim=4)            [SIMD]", [&]{
            uint64_t h = dim_hash_simd(m4);
            bench::do_not_optimize(h);
        }),
        bench::run("dim_hash_scalar(ndim=8)            [scalar baseline]", [&]{
            uint64_t h = dim_hash_scalar(m8);
            bench::do_not_optimize(h);
        }),
        bench::run("dim_hash_simd  (ndim=8)            [SIMD]", [&]{
            uint64_t h = dim_hash_simd(m8);
            bench::do_not_optimize(h);
        }),

        // ─── compute_storage_nbytes: scalar vs SIMD ─────────────────
        //
        // SIMD path includes pre-screen overhead.  At ndim=1 the
        // scalar path is faster (single mul, no load).  At ndim=8
        // the SIMD wins on the dim multiply + masked accumulation.
        bench::run("compute_storage_nbytes_scalar (ndim=1)", [&]{
            uint64_t b = compute_storage_nbytes_scalar(m1);
            bench::do_not_optimize(b);
        }),
        bench::run("compute_storage_nbytes_simd   (ndim=1)", [&]{
            uint64_t b = compute_storage_nbytes_simd(m1);
            bench::do_not_optimize(b);
        }),
        bench::run("compute_storage_nbytes_scalar (ndim=4)", [&]{
            uint64_t b = compute_storage_nbytes_scalar(m4);
            bench::do_not_optimize(b);
        }),
        bench::run("compute_storage_nbytes_simd   (ndim=4)", [&]{
            uint64_t b = compute_storage_nbytes_simd(m4);
            bench::do_not_optimize(b);
        }),
        bench::run("compute_storage_nbytes_scalar (ndim=8)", [&]{
            uint64_t b = compute_storage_nbytes_scalar(m8);
            bench::do_not_optimize(b);
        }),
        bench::run("compute_storage_nbytes_simd   (ndim=8)", [&]{
            uint64_t b = compute_storage_nbytes_simd(m8);
            bench::do_not_optimize(b);
        }),

        // ─── Variable-ndim sweep on the SIMD path ───────────────────
        //
        // Use volatile ndim so the compiler can't specialize per-ndim
        // — measures the realistic cost when ndim isn't known at
        // compile time.
        [&]{
            crucible::TensorMeta mvar{};
            mvar.dtype = crucible::ScalarType::Float;
            return bench::run("dim_hash_simd  (variable ndim, runtime branch)", [&]{
                mvar.ndim = static_cast<uint8_t>(v_ndim_8);
                int64_t stride = 1;
                for (int d = mvar.ndim - 1; d >= 0; --d) {
                    mvar.sizes[d] = (1 << d) + 4;
                    mvar.strides[d] = stride;
                    stride *= mvar.sizes[d];
                }
                uint64_t h = dim_hash_simd(mvar);
                bench::do_not_optimize(h);
            });
        }(),
    };

    bench::emit_reports_text(reports);
    bench::emit_reports_json(reports, json);
    return 0;
}
