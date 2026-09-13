// The facade under test is built on compiler vector extensions rather
// than on the standard library's vector header, because that header is
// gated behind an x86 feature macro and compiles to nothing on the
// other supported architecture.

#include <crucible/safety/Simd.h>
#include <crucible/DimHash.h>
#include <crucible/MerkleDag.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>

namespace simd = crucible::simd;

static void test_type_aliases() {
    static_assert(simd::i64x8::size() == 8);
    static_assert(simd::u64x8::size() == 8);
    static_assert(simd::i32x16::size() == 16);
    static_assert(simd::u8x32::size() == 32);
    std::printf("  test_type_aliases: PASSED\n");
}

static void test_iota() {
    auto indices = simd::iota_v<simd::i64x8>();
    for (int lane = 0; lane < static_cast<int>(simd::i64x8::size()); ++lane) {
        assert(indices[lane] == static_cast<int64_t>(lane));
    }
    std::printf("  test_iota: PASSED\n");
}

static void test_prefix_mask() {
    // Both ends of the range are covered as well as a partial count,
    // because an off-by-one implementation passes the middle case.
    auto mask0 = simd::prefix_mask<simd::i64x8>(0);
    for (int lane = 0; lane < static_cast<int>(simd::i64x8::size()); ++lane) {
        assert(!mask0[lane]);
    }
    auto mask5 = simd::prefix_mask<simd::i64x8>(5);
    for (int lane = 0; lane < 5; ++lane)
        assert(mask5[lane]);
    for (int lane = 5; lane < 8; ++lane)
        assert(!mask5[lane]);
    auto mask8 = simd::prefix_mask<simd::i64x8>(8);
    for (int lane = 0; lane < 8; ++lane)
        assert(mask8[lane]);
    std::printf("  test_prefix_mask: PASSED\n");
}

static void test_det_safe_simd_concept() {
    static_assert(simd::DetSafeSimd<simd::i64x8>);
    static_assert(simd::DetSafeSimd<simd::u64x8>);
    static_assert(simd::DetSafeSimd<simd::u32x8>);
    static_assert(simd::DetSafeSimd<simd::u8x32>);

    // Floating-point lanes are excluded on purpose.  A reduction folds
    // the lanes in an order the vector width decides, and rounding
    // makes that order visible in the result, so the same input would
    // produce different bits on different machines.
    using f32x8 = simd::vec<float, 8>;
    using f64x4 = simd::vec<double, 4>;
    static_assert(!simd::DetSafeSimd<f32x8>);
    static_assert(!simd::DetSafeSimd<f64x4>);

    static_assert(!simd::DetSafeSimd<int64_t>);
    std::printf("  test_det_safe_simd_concept: PASSED\n");
}

static void test_load() {
    std::array<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};
    auto v = simd::load<simd::i64x8>(source.data());
    for (int lane = 0; lane < 8; ++lane) {
        assert(v[lane] == source[static_cast<size_t>(lane)]);
    }
    std::printf("  test_load: PASSED\n");
}

static void test_partial_load() {
    std::array<int64_t, 8> source{10, 20, 30, 40, 50, 60, 70, 80};

    // The lanes past the count read zero rather than whatever follows
    // in memory, which is what makes a partial load usable as an
    // operand of an identity-0 reduction.
    auto v = simd::partial_load<simd::i64x8>(source.data(), 3);
    assert(v[0] == 10);
    assert(v[1] == 20);
    assert(v[2] == 30);
    assert(v[3] == 0);
    assert(v[7] == 0);

    auto vfull = simd::partial_load<simd::i64x8>(source.data(), 8);
    for (int lane = 0; lane < 8; ++lane) {
        assert(vfull[lane] == source[static_cast<size_t>(lane)]);
    }
    std::printf("  test_partial_load: PASSED\n");
}

static void test_reduce_xor_sum() {
    std::array<int64_t, 8> values{0, 1, 2, 3, 4, 5, 6, 7};
    auto input = simd::load<simd::i64x8>(values.data());

    int64_t xor_expected = 0;
    int64_t sum_expected = 0;
    for (auto v : values) {
        xor_expected ^= v;
        sum_expected += v;
    }

    int64_t xor_got = simd::reduce_xor(input);
    int64_t sum_got = simd::reduce_add(input);
    assert(xor_got == xor_expected);
    assert(sum_got == sum_expected);
    std::printf("  test_reduce_xor_sum: PASSED (xor=%lld sum=%lld)\n", static_cast<long long>(xor_got),
                static_cast<long long>(sum_got));
}

static void test_reduce_or_and() {
    std::array<uint64_t, 8> values{0x01ULL, 0x02ULL, 0x04ULL, 0x08ULL, 0x10ULL, 0x20ULL, 0x40ULL, 0x80ULL};
    auto input = simd::load<simd::u64x8>(values.data());
    assert(simd::reduce_or(input) == 0xFFULL);
    assert(simd::reduce_and(input) == 0ULL);
    std::printf("  test_reduce_or_and: PASSED\n");
}

static void test_reduce_max_min() {
    std::array<int64_t, 8> values{-5, 100, 33, -200, 7, 42, 999, 0};
    auto input = simd::load<simd::i64x8>(values.data());
    assert(simd::reduce_max(input) == 999);
    assert(simd::reduce_min(input) == -200);
    std::printf("  test_reduce_max_min: PASSED\n");
}

static void test_select() {
    std::array<int64_t, 8> on_true{100, 200, 300, 400, 500, 600, 700, 800};
    std::array<int64_t, 8> on_false{1, 2, 3, 4, 5, 6, 7, 8};

    auto t_v = simd::load<simd::i64x8>(on_true.data());
    auto f_v = simd::load<simd::i64x8>(on_false.data());

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

    // The two operands are re-read afterwards: a select that wrote
    // into one of them would still produce the right result above.
    for (int lane = 0; lane < 8; ++lane) {
        assert(t_v[lane] == on_true[static_cast<size_t>(lane)]);
        assert(f_v[lane] == on_false[static_cast<size_t>(lane)]);
    }
    std::printf("  test_select: PASSED\n");
}

// This is the shape the dimension hash uses: one masked reduction over
// a full-width vector, with the identity covering the lanes past the
// dimension count, instead of a select and a second pass.
static void test_masked_reduce_for_dim_hash_pattern() {
    std::array<int64_t, 8> sizes{2, 3, 4, 5, 6, 7, 8, 9};
    std::array<int64_t, 8> mix_lo{7, 11, 13, 17, 19, 23, 29, 31};
    constexpr int ndim = 5;

    auto sizes_v = simd::load<simd::i64x8>(sizes.data());
    auto mix_v = simd::load<simd::i64x8>(mix_lo.data());
    simd::i64x8 product = sizes_v * mix_v;

    int64_t simd_result = simd::reduce_xor(product, simd::prefix_mask<simd::i64x8>(ndim));

    int64_t scalar_result = 0;
    for (int d = 0; d < ndim; ++d) {
        scalar_result ^= sizes[static_cast<size_t>(d)] * mix_lo[static_cast<size_t>(d)];
    }

    assert(simd_result == scalar_result);
    std::printf("  test_masked_reduce_for_dim_hash_pattern: PASSED "
                "(simd=%lld scalar=%lld)\n",
                static_cast<long long>(simd_result), static_cast<long long>(scalar_result));
}

static void test_microarch_detection() {
    // The compile-time flags describe the build target, and a debug
    // build names no target at all, so all of them may legitimately be
    // false.  They are printed and not asserted.
    //
    // The runtime probes describe the machine this is running on, and
    // there at least one extension has to be present: the project's
    // own baseline already requires more than the oldest of them.
#if defined(__x86_64__) || defined(__i386__)
    bool any_runtime =
        simd::runtime_supports_sse42() || simd::runtime_supports_avx2() || simd::runtime_supports_avx512();
    assert(any_runtime && "no SIMD ISA detected at runtime on x86-64");
#endif
    std::printf("  test_microarch_detection: PASSED "
                "(compile: sse42=%d avx2=%d avx512=%d neon=%d; "
                "runtime: sse42=%d avx2=%d avx512=%d)\n",
                simd::kSse42Available, simd::kAvx2Available, simd::kAvx512Available, simd::kNeonAvailable,
                simd::runtime_supports_sse42(), simd::runtime_supports_avx2(), simd::runtime_supports_avx512());
}

// The shapes below are chosen by hand, one per structural case the
// hash has to handle.  Broad random coverage is a fuzzer's job.

static crucible::TensorMeta make_meta(std::initializer_list<int64_t> sizes, std::initializer_list<int64_t> strides,
                                      crucible::ScalarType dtype = crucible::ScalarType::Float) {
    crucible::TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    assert(strides.size() == sizes.size());
    auto size_it = sizes.begin();
    auto stride_it = strides.begin();
    for (uint8_t d = 0; d < meta.ndim; ++d) {
        meta.sizes[d] = ::crucible::tensor_dim(*size_it++);
        meta.strides[d] = ::crucible::tensor_dim(*stride_it++);
    }
    meta.dtype = dtype;
    return meta;
}

static void test_dim_hash_equivalence_handcoded() {
    using namespace crucible::detail;

    // A tensor with no dimensions has nothing to fold, and both paths
    // must agree that the answer is zero rather than an identity that
    // leaked out of the reduction.
    auto m_empty = make_meta({}, {});
    assert(dim_hash_scalar(m_empty) == 0);
    assert(dim_hash_simd(m_empty) == 0);
    static_assert(std::is_same_v<decltype(dim_hash_scalar_det(m_empty)), crucible::DimHashDet>);
    static_assert(std::is_same_v<decltype(dim_hash_simd_det(m_empty)), crucible::DimHashDet>);
    assert(crucible::raw_dim_hash(dim_hash_scalar_det(m_empty)) == 0);
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_empty)) == 0);

    auto m_1d = make_meta({4096}, {1});
    assert(dim_hash_simd(m_1d) == dim_hash_scalar(m_1d));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_1d)) == crucible::raw_dim_hash(dim_hash_scalar_det(m_1d)));

    auto m_2d = make_meta({128, 256}, {256, 1});
    assert(dim_hash_simd(m_2d) == dim_hash_scalar(m_2d));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_2d)) == crucible::raw_dim_hash(dim_hash_scalar_det(m_2d)));

    auto m_nchw = make_meta({32, 64, 224, 224}, {64 * 224 * 224, 224 * 224, 224, 1});
    assert(dim_hash_simd(m_nchw) == dim_hash_scalar(m_nchw));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_nchw)) == crucible::raw_dim_hash(dim_hash_scalar_det(m_nchw)));

    // Eight dimensions fill the vector exactly, so the masked path
    // degenerates to an unmasked one here.
    auto m_8d = make_meta({2, 3, 5, 7, 11, 13, 17, 19}, {1, 2, 3, 4, 5, 6, 7, 8});
    assert(dim_hash_simd(m_8d) == dim_hash_scalar(m_8d));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_8d)) == crucible::raw_dim_hash(dim_hash_scalar_det(m_8d)));

    // A reversed view carries a negative stride, which is where a
    // signed and an unsigned lane type would disagree.
    auto m_neg = make_meta({4, 8}, {-8, 1});
    assert(dim_hash_simd(m_neg) == dim_hash_scalar(m_neg));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(m_neg)) == crucible::raw_dim_hash(dim_hash_scalar_det(m_neg)));

    // Real callers hand over metadata that is only aligned as the type
    // requires, never to a vector boundary.  The search below picks
    // such an address deliberately, because a load that assumed vector
    // alignment would pass on every case above.
    alignas(crucible::TensorMeta) std::array<std::byte, sizeof(crucible::TensorMeta) + 64> storage{};
    std::uintptr_t chosen = 0;
    const std::uintptr_t base = std::bit_cast<std::uintptr_t>(storage.data());
    for (std::size_t offset = 0; offset < 64; ++offset) {
        const std::uintptr_t candidate = base + offset;
        if (candidate % alignof(crucible::TensorMeta) == 0 && candidate % 64 != 0) {
            chosen = candidate;
            break;
        }
    }
    assert(chosen != 0);
    auto* unaligned =
        std::construct_at(std::bit_cast<crucible::TensorMeta*>(chosen), make_meta({16, 32, 64}, {2048, 64, 1}));
    assert(std::bit_cast<std::uintptr_t>(unaligned->sizes.raw_data()) % 64 != 0);
    assert(dim_hash_simd(*unaligned) == dim_hash_scalar(*unaligned));
    assert(crucible::raw_dim_hash(dim_hash_simd_det(*unaligned))
           == crucible::raw_dim_hash(dim_hash_scalar_det(*unaligned)));
    std::destroy_at(unaligned);

    // Agreement alone would be satisfied by a hash that returned a
    // constant, so two different shapes are required to differ.
    assert(dim_hash_scalar(m_1d) != dim_hash_scalar(m_2d));
    assert(dim_hash_simd(m_1d) != dim_hash_simd(m_2d));

    std::printf("  test_dim_hash_equivalence_handcoded: PASSED\n");
}

int main() {
    test_type_aliases();
    test_iota();
    test_prefix_mask();
    test_det_safe_simd_concept();
    test_load();
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
