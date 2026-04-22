// ═══════════════════════════════════════════════════════════════════
// test_simd — sanity tests for crucible::simd facade + std::simd usage
//
// Covers the (thin) facade: type aliases, iota_v, prefix_mask,
// DetSafeSimd concept, microarch detection.  Also exercises direct
// std::simd use for the operations we don't wrap (unchecked_load,
// partial_load, select, reduce, reduce_max/min) — those are library
// primitives, not facade primitives.  Heavier algorithmic SIMD tests
// (Philox batch equivalence) live in dedicated fuzzers under
// fuzz/property/.
//
// Single-target build per -march=; no multi-target dispatch.  Hand-
// rolled intrinsics live only in SwissTable.h where std::simd has no
// movemask analog.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Simd.h>
#include <crucible/DimHash.h>
#include <crucible/MerkleDag.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <simd>

namespace simd = crucible::simd;

// ── Facade primitives ──────────────────────────────────────────────

static void test_type_aliases() {
    static_assert(simd::i64x8::size() == 8);
    static_assert(simd::u64x8::size() == 8);
    static_assert(simd::i32x16::size() == 16);
    static_assert(simd::u8x32::size() == 32);
    std::printf("  test_type_aliases: PASSED\n");
}

static void test_iota() {
    auto indices = simd::iota_v<simd::i64x8>();
    for (int lane = 0; lane < simd::i64x8::size(); ++lane) {
        assert(indices[lane] == static_cast<int64_t>(lane));
    }
    std::printf("  test_iota: PASSED\n");
}

static void test_prefix_mask() {
    // count=0: no lanes set
    auto mask0 = simd::prefix_mask<simd::i64x8>(0);
    for (int lane = 0; lane < simd::i64x8::size(); ++lane) {
        assert(!mask0[lane]);
    }
    // count=5: lanes 0..4 set, 5..7 unset
    auto mask5 = simd::prefix_mask<simd::i64x8>(5);
    for (int lane = 0; lane < 5; ++lane) assert(mask5[lane]);
    for (int lane = 5; lane < 8; ++lane) assert(!mask5[lane]);
    // count=8: all lanes set
    auto mask8 = simd::prefix_mask<simd::i64x8>(8);
    for (int lane = 0; lane < 8; ++lane) assert(mask8[lane]);
    std::printf("  test_prefix_mask: PASSED\n");
}

static void test_det_safe_simd_concept() {
    // Integral-lane vectors satisfy DetSafeSimd.
    static_assert(simd::DetSafeSimd<simd::i64x8>);
    static_assert(simd::DetSafeSimd<simd::u64x8>);
    static_assert(simd::DetSafeSimd<simd::u32x8>);
    static_assert(simd::DetSafeSimd<simd::u8x32>);

    // Floating-point lanes must NOT — FP reductions are order-sensitive
    // and break DetSafe bit-equality across AVX-512 / AVX2 / NEON.
    using f32x8 = std::simd::vec<float, 8>;
    using f64x4 = std::simd::vec<double, 4>;
    static_assert(!simd::DetSafeSimd<f32x8>);
    static_assert(!simd::DetSafeSimd<f64x4>);

    // Plain scalars also NOT (not a SIMD vec at all).
    static_assert(!simd::DetSafeSimd<int64_t>);
    std::printf("  test_det_safe_simd_concept: PASSED\n");
}

// ── Direct std::simd usage patterns ────────────────────────────────
//
// These used to go through facade wrappers; they're now direct calls.
// The tests remain to verify that the std::simd API does what we need.

static void test_unchecked_load() {
    std::array<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};
    auto v = std::simd::unchecked_load<simd::i64x8>(
        source.data(), simd::i64x8::size());
    for (int lane = 0; lane < 8; ++lane) {
        assert(v[lane] == source[static_cast<size_t>(lane)]);
    }
    std::printf("  test_unchecked_load: PASSED\n");
}

static void test_partial_load() {
    std::array<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};

    // Load only first 3 elements; lanes 3..7 must be zero per partial
    // load semantics.
    auto v = std::simd::partial_load<simd::i64x8>(source.data(), 3);
    assert(v[0] == 10);
    assert(v[1] == 20);
    assert(v[2] == 30);
    assert(v[3] == 0);
    assert(v[7] == 0);

    // Load full 8 elements.
    auto vfull = std::simd::partial_load<simd::i64x8>(source.data(), 8);
    for (int lane = 0; lane < 8; ++lane) {
        assert(vfull[lane] == source[static_cast<size_t>(lane)]);
    }
    std::printf("  test_partial_load: PASSED\n");
}

static void test_reduce_xor_sum() {
    std::array<int64_t, 8> values{0, 1, 2, 3, 4, 5, 6, 7};
    auto input = std::simd::unchecked_load<simd::i64x8>(
        values.data(), simd::i64x8::size());

    int64_t xor_expected = 0;
    int64_t sum_expected = 0;
    for (auto v : values) { xor_expected ^= v; sum_expected += v; }

    int64_t xor_got = std::simd::reduce(input, std::bit_xor<>{});
    int64_t sum_got = std::simd::reduce(input, std::plus<>{});
    assert(xor_got == xor_expected);
    assert(sum_got == sum_expected);
    std::printf("  test_reduce_xor_sum: PASSED (xor=%lld sum=%lld)\n",
                static_cast<long long>(xor_got),
                static_cast<long long>(sum_got));
}

static void test_reduce_or_and() {
    std::array<uint64_t, 8> values{
        0x01ULL, 0x02ULL, 0x04ULL, 0x08ULL,
        0x10ULL, 0x20ULL, 0x40ULL, 0x80ULL};
    auto input = std::simd::unchecked_load<simd::u64x8>(
        values.data(), simd::u64x8::size());
    assert(std::simd::reduce(input, std::bit_or<>{})  == 0xFFULL);
    assert(std::simd::reduce(input, std::bit_and<>{}) == 0ULL);
    std::printf("  test_reduce_or_and: PASSED\n");
}

static void test_reduce_max_min() {
    std::array<int64_t, 8> values{-5, 100, 33, -200, 7, 42, 999, 0};
    auto input = std::simd::unchecked_load<simd::i64x8>(
        values.data(), simd::i64x8::size());
    assert(std::simd::reduce_max(input) == 999);
    assert(std::simd::reduce_min(input) == -200);
    std::printf("  test_reduce_max_min: PASSED\n");
}

static void test_select() {
    std::array<int64_t, 8> on_true {100, 200, 300, 400, 500, 600, 700, 800};
    std::array<int64_t, 8> on_false{1,    2,   3,   4,   5,   6,   7,   8};

    auto t_v = std::simd::unchecked_load<simd::i64x8>(
        on_true.data(),  simd::i64x8::size());
    auto f_v = std::simd::unchecked_load<simd::i64x8>(
        on_false.data(), simd::i64x8::size());

    // Mask: select on_true where lane index < 4.
    auto mask = simd::prefix_mask<simd::i64x8>(4);
    auto out = std::simd::select(mask, t_v, f_v);
    assert(out[0] == 100);
    assert(out[1] == 200);
    assert(out[2] == 300);
    assert(out[3] == 400);
    assert(out[4] == 5);
    assert(out[5] == 6);
    assert(out[6] == 7);
    assert(out[7] == 8);

    // Caller's vectors must NOT have been mutated by select.
    for (int lane = 0; lane < 8; ++lane) {
        assert(t_v[lane] == on_true [static_cast<size_t>(lane)]);
        assert(f_v[lane] == on_false[static_cast<size_t>(lane)]);
    }
    std::printf("  test_select: PASSED\n");
}

static void test_masked_reduce_for_dim_hash_pattern() {
    // Mirrors the SIMD-1 dim-hash use case with std::simd::reduce's
    // masked overload: ONE call aggregates only the valid lanes with
    // identity 0 filling the rest.  No explicit select+intermediate.
    std::array<int64_t, 8> sizes  {2, 3, 4, 5, 6, 7, 8, 9};
    std::array<int64_t, 8> mix_lo {7, 11, 13, 17, 19, 23, 29, 31};
    constexpr int ndim = 5;

    auto sizes_v = std::simd::unchecked_load<simd::i64x8>(
        sizes.data(),  simd::i64x8::size());
    auto mix_v   = std::simd::unchecked_load<simd::i64x8>(
        mix_lo.data(), simd::i64x8::size());
    simd::i64x8 product = sizes_v * mix_v;

    int64_t simd_result = std::simd::reduce(
        product,
        simd::prefix_mask<simd::i64x8>(ndim),
        std::bit_xor<>{},
        static_cast<int64_t>(0));

    int64_t scalar_result = 0;
    for (int d = 0; d < ndim; ++d) {
        scalar_result ^= sizes[static_cast<size_t>(d)]
                       * mix_lo[static_cast<size_t>(d)];
    }

    assert(simd_result == scalar_result);
    std::printf("  test_masked_reduce_for_dim_hash_pattern: PASSED "
                "(simd=%lld scalar=%lld)\n",
                static_cast<long long>(simd_result),
                static_cast<long long>(scalar_result));
}

static void test_microarch_detection() {
    // Compile-time flags reflect the build target.  Debug presets
    // typically build at -O0 without -march=native, so ALL k*Available
    // may legitimately be false — we don't assert on the compile-time
    // values, just print them for diagnostic visibility.
    //
    // Runtime probes use __builtin_cpu_supports — these reflect the
    // CURRENT CPU and at least one of (sse42, avx2, avx512) MUST hold
    // on any x86-64 host running our test suite (sse4.2 is part of
    // x86-64-v2, the project baseline per CRUCIBLE.md §XIV).
#if defined(__x86_64__) || defined(__i386__)
    bool any_runtime = simd::runtime_supports_sse42() ||
                       simd::runtime_supports_avx2() ||
                       simd::runtime_supports_avx512();
    assert(any_runtime && "no SIMD ISA detected at runtime on x86-64");
#endif
    std::printf("  test_microarch_detection: PASSED "
                "(compile: sse42=%d avx2=%d avx512=%d neon=%d; "
                "runtime: sse42=%d avx2=%d avx512=%d)\n",
                simd::kSse42Available, simd::kAvx2Available,
                simd::kAvx512Available, simd::kNeonAvailable,
                simd::runtime_supports_sse42(),
                simd::runtime_supports_avx2(),
                simd::runtime_supports_avx512());
}

// ── DimHash equivalence: scalar reference vs SIMD ──────────────────
//
// Sanity-level equivalence over a hand-curated set of TensorMetas.
// The Philox-driven 100K-iter fuzzer lives separately in SIMD-8.

static crucible::TensorMeta make_meta(
    std::initializer_list<int64_t> sizes,
    std::initializer_list<int64_t> strides,
    crucible::ScalarType dtype = crucible::ScalarType::Float) {
    crucible::TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    assert(strides.size() == sizes.size());
    auto size_it = sizes.begin();
    auto stride_it = strides.begin();
    for (uint8_t d = 0; d < meta.ndim; ++d) {
        meta.sizes[d]   = *size_it++;
        meta.strides[d] = *stride_it++;
    }
    meta.dtype = dtype;
    return meta;
}

static void test_dim_hash_equivalence_handcoded() {
    using namespace crucible::detail;

    // Empty tensor (ndim=0): both must return 0.
    auto m_empty = make_meta({}, {});
    assert(dim_hash_scalar(m_empty) == 0);
    assert(dim_hash_simd(m_empty)   == 0);

    // 1-D contiguous.
    auto m_1d = make_meta({4096}, {1});
    assert(dim_hash_simd(m_1d) == dim_hash_scalar(m_1d));

    // 2-D contiguous (matrix).
    auto m_2d = make_meta({128, 256}, {256, 1});
    assert(dim_hash_simd(m_2d) == dim_hash_scalar(m_2d));

    // 4-D NCHW (typical conv).
    auto m_nchw = make_meta({32, 64, 224, 224}, {64*224*224, 224*224, 224, 1});
    assert(dim_hash_simd(m_nchw) == dim_hash_scalar(m_nchw));

    // Full 8-D worst case.
    auto m_8d = make_meta({2,3,5,7,11,13,17,19}, {1,2,3,4,5,6,7,8});
    assert(dim_hash_simd(m_8d) == dim_hash_scalar(m_8d));

    // Negative strides (transpose / flip view).
    auto m_neg = make_meta({4, 8}, {-8, 1});
    assert(dim_hash_simd(m_neg) == dim_hash_scalar(m_neg));

    // Distinct meta produces DISTINCT hash with overwhelming probability.
    assert(dim_hash_scalar(m_1d) != dim_hash_scalar(m_2d));
    assert(dim_hash_simd  (m_1d) != dim_hash_simd  (m_2d));

    std::printf("  test_dim_hash_equivalence_handcoded: PASSED\n");
}

int main() {
    test_type_aliases();
    test_iota();
    test_prefix_mask();
    test_det_safe_simd_concept();
    test_unchecked_load();
    test_partial_load();
    test_reduce_xor_sum();
    test_reduce_or_and();
    test_reduce_max_min();
    test_select();
    test_masked_reduce_for_dim_hash_pattern();
    test_microarch_detection();
    test_dim_hash_equivalence_handcoded();
    std::printf("test_simd: ALL PASSED\n");
    return 0;
}
