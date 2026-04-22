// ═══════════════════════════════════════════════════════════════════
// test_simd — sanity tests for crucible::simd facade (safety/Simd.h)
//
// Covers the SIMD-5 facade: type aliases, iota_v, prefix_mask,
// load_partial, masked select, pinned-shape reductions, microarch
// detection.  Heavier algorithmic SIMD tests (dim_hash equivalence,
// philox batch equivalence) live in dedicated fuzzers under
// fuzz/property/.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Simd.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace simd = crucible::simd;

static void test_type_aliases() {
    static_assert(simd::i64x8::size() == 8);
    static_assert(simd::u64x8::size() == 8);
    static_assert(simd::i32x16::size() == 16);
    static_assert(simd::u8x32::size() == 32);
    std::printf("  test_type_aliases: PASSED\n");
}

static void test_width_by_bits() {
    static_assert(simd::simd_128<int64_t>::size() == 2);
    static_assert(simd::simd_256<int64_t>::size() == 4);
    static_assert(simd::simd_512<int64_t>::size() == 8);
    static_assert(simd::simd_128<uint32_t>::size() == 4);
    static_assert(simd::simd_256<uint32_t>::size() == 8);
    static_assert(simd::simd_512<uint32_t>::size() == 16);
    std::printf("  test_width_by_bits: PASSED\n");
}

static void test_iota() {
    auto indices = simd::iota_v<simd::i64x8>();
    for (size_t lane = 0; lane < simd::i64x8::size(); ++lane) {
        assert(indices[lane] == static_cast<int64_t>(lane));
    }
    std::printf("  test_iota: PASSED\n");
}

static void test_prefix_mask() {
    // count=0: no lanes set
    auto mask0 = simd::prefix_mask<simd::i64x8>(0);
    for (size_t lane = 0; lane < simd::i64x8::size(); ++lane) {
        assert(!mask0[lane]);
    }
    // count=5: lanes 0..4 set, 5..7 unset
    auto mask5 = simd::prefix_mask<simd::i64x8>(5);
    for (size_t lane = 0; lane < 5; ++lane) assert(mask5[lane]);
    for (size_t lane = 5; lane < 8; ++lane) assert(!mask5[lane]);
    // count=8: all lanes set
    auto mask8 = simd::prefix_mask<simd::i64x8>(8);
    for (size_t lane = 0; lane < 8; ++lane) assert(mask8[lane]);
    std::printf("  test_prefix_mask: PASSED\n");
}

static void test_load_partial() {
    std::array<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};

    // Load only first 3 elements; lanes 3..7 must be zero.
    auto v = simd::load_partial<simd::i64x8>(source.data(), 3);
    assert(v[0] == 10);
    assert(v[1] == 20);
    assert(v[2] == 30);
    assert(v[3] == 0);
    assert(v[4] == 0);
    assert(v[5] == 0);
    assert(v[6] == 0);
    assert(v[7] == 0);

    // Load full 8 elements.
    auto vfull = simd::load_partial<simd::i64x8>(source.data(), 8);
    for (size_t lane = 0; lane < 8; ++lane) {
        assert(vfull[lane] == source[lane]);
    }
    std::printf("  test_load_partial: PASSED\n");
}

static void test_xor_reduce() {
    std::array<int64_t, 8> values{0, 1, 2, 3, 4, 5, 6, 7};
    simd::i64x8 input(values.data(), simd::element_aligned);
    int64_t reduced = simd::xor_reduce(input);
    int64_t expected = 0;
    for (auto v : values) expected ^= v;
    assert(reduced == expected);
    std::printf("  test_xor_reduce: PASSED (xor=%lld)\n",
                static_cast<long long>(reduced));
}

static void test_add_reduce() {
    std::array<int64_t, 8> values{10, 20, 30, 40, 50, 60, 70, 80};
    simd::i64x8 input(values.data(), simd::element_aligned);
    int64_t reduced = simd::add_reduce(input);
    assert(reduced == 360);
    std::printf("  test_add_reduce: PASSED (sum=%lld)\n",
                static_cast<long long>(reduced));
}

static void test_or_and_reduce() {
    std::array<uint64_t, 8> values{
        0x0000'0000'0000'0001ULL,
        0x0000'0000'0000'0002ULL,
        0x0000'0000'0000'0004ULL,
        0x0000'0000'0000'0008ULL,
        0x0000'0000'0000'0010ULL,
        0x0000'0000'0000'0020ULL,
        0x0000'0000'0000'0040ULL,
        0x0000'0000'0000'0080ULL};
    simd::u64x8 input(values.data(), simd::element_aligned);
    assert(simd::or_reduce(input)  == 0xFFULL);
    assert(simd::and_reduce(input) == 0ULL);
    std::printf("  test_or_and_reduce: PASSED\n");
}

static void test_max_min_reduce() {
    std::array<int64_t, 8> values{-5, 100, 33, -200, 7, 42, 999, 0};
    simd::i64x8 input(values.data(), simd::element_aligned);
    assert(simd::max_reduce(input) == 999);
    assert(simd::min_reduce(input) == -200);
    std::printf("  test_max_min_reduce: PASSED\n");
}

static void test_select() {
    std::array<int64_t, 8> on_true {100, 200, 300, 400, 500, 600, 700, 800};
    std::array<int64_t, 8> on_false{1,    2,   3,   4,   5,   6,   7,   8};

    simd::i64x8 t_v(on_true.data(),  simd::element_aligned);
    simd::i64x8 f_v(on_false.data(), simd::element_aligned);

    // Mask: select on_true where lane index < 4.
    auto mask = simd::prefix_mask<simd::i64x8>(4);
    auto out = simd::select(mask, t_v, f_v);
    assert(out[0] == 100);
    assert(out[1] == 200);
    assert(out[2] == 300);
    assert(out[3] == 400);
    assert(out[4] == 5);
    assert(out[5] == 6);
    assert(out[6] == 7);
    assert(out[7] == 8);

    // Caller's vectors must NOT have been mutated.
    for (size_t lane = 0; lane < 8; ++lane) {
        assert(t_v[lane] == on_true[lane]);
        assert(f_v[lane] == on_false[lane]);
    }
    std::printf("  test_select: PASSED\n");
}

static void test_masked_xor_for_dim_hash_pattern() {
    // Mirrors the SIMD-1 dim-hash use case: compute 8 independent
    // multiplies, mask out lanes >= ndim, XOR-fold the rest.
    std::array<int64_t, 8> sizes  {2, 3, 4, 5, 6, 7, 8, 9};
    std::array<int64_t, 8> mix_lo {7, 11, 13, 17, 19, 23, 29, 31};
    constexpr size_t ndim = 5;

    simd::i64x8 sizes_v(sizes.data(),  simd::element_aligned);
    simd::i64x8 mix_v  (mix_lo.data(), simd::element_aligned);
    simd::i64x8 product = sizes_v * mix_v;

    auto masked = simd::select(
        simd::prefix_mask<simd::i64x8>(ndim), product, simd::i64x8(0));
    int64_t simd_result = simd::xor_reduce(masked);

    int64_t scalar_result = 0;
    for (size_t d = 0; d < ndim; ++d) {
        scalar_result ^= sizes[d] * mix_lo[d];
    }

    assert(simd_result == scalar_result);
    std::printf("  test_masked_xor_for_dim_hash_pattern: PASSED "
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

int main() {
    test_type_aliases();
    test_width_by_bits();
    test_iota();
    test_prefix_mask();
    test_load_partial();
    test_xor_reduce();
    test_add_reduce();
    test_or_and_reduce();
    test_max_min_reduce();
    test_select();
    test_masked_xor_for_dim_hash_pattern();
    test_microarch_detection();
    std::printf("test_simd: ALL PASSED\n");
    return 0;
}
