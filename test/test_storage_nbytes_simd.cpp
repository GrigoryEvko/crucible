// ═══════════════════════════════════════════════════════════════════
// test_storage_nbytes_simd — SIMD-2 bit-equivalence proof
//
// Hand-crafted edge cases proving compute_storage_nbytes_simd
// produces output bit-identical to compute_storage_nbytes_scalar
// across the algorithmically-significant input space.  Heavier
// randomized fuzzing lives in
// fuzz/property/prop_storage_nbytes_simd_equivalence.cpp.
//
// What we cover:
//   1. Edge: ndim == 0 (scalar tensor returns element_size)
//   2. Edge: zero-size tensor in any dim (returns 0)
//   3. Common shapes (1D contiguous, 2D matrix, 4D NCHW, 8D)
//   4. Negative strides (transpose / flip / as_strided)
//   5. Mixed positive/negative strides
//   6. Stride = 0 (broadcasting)
//   7. dtype variations (uint8 / float / double / int64)
//   8. Adversarial overflow inputs (scalar + SIMD must both
//      return UINT64_MAX)
//   9. Pre-screen edge: INT64_MIN stride (fallback to scalar)
//  10. Bit-equality across hundreds of well-bounded random inputs
// ═══════════════════════════════════════════════════════════════════

#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>

#include <array>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <random>

using namespace crucible;
using namespace crucible::detail;

// ── Helper: build TensorMeta from sizes/strides arrays ────────────

[[nodiscard]] static TensorMeta make_meta(
    std::initializer_list<int64_t> sizes,
    std::initializer_list<int64_t> strides,
    ScalarType dtype = ScalarType::Float) noexcept
{
    assert(sizes.size() == strides.size());
    assert(sizes.size() <= 8);

    TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    meta.dtype = dtype;

    auto si = sizes.begin();
    auto ti = strides.begin();
    for (size_t d = 0; d < sizes.size(); ++d) {
        meta.sizes[d] = *si++;
        meta.strides[d] = *ti++;
    }
    return meta;
}

// ── Assertion helper: SIMD == scalar ──────────────────────────────

static void check_equiv(const TensorMeta& meta, const char* what) noexcept {
    const uint64_t scalar = compute_storage_nbytes_scalar(meta);
    const uint64_t simd_v = compute_storage_nbytes_simd(meta);
    if (scalar != simd_v) {
        std::fprintf(stderr,
            "[%s] MISMATCH: scalar=%llu simd=%llu\n"
            "  ndim=%u dtype=%d sizes=[",
            what,
            static_cast<unsigned long long>(scalar),
            static_cast<unsigned long long>(simd_v),
            unsigned(meta.ndim),
            int(meta.dtype));
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            std::fprintf(stderr, "%lld%s",
                static_cast<long long>(meta.sizes[d]),
                d + 1 < meta.ndim ? "," : "");
        }
        std::fprintf(stderr, "] strides=[");
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            std::fprintf(stderr, "%lld%s",
                static_cast<long long>(meta.strides[d]),
                d + 1 < meta.ndim ? "," : "");
        }
        std::fprintf(stderr, "]\n");
        std::abort();
    }
}

// ── Edge: scalar tensor (ndim == 0) ───────────────────────────────

static void test_scalar_tensor() {
    TensorMeta meta{};
    meta.ndim = 0;
    meta.dtype = ScalarType::Float;
    check_equiv(meta, "scalar-float");
    assert(compute_storage_nbytes_simd(meta) == element_size(ScalarType::Float).raw());

    meta.dtype = ScalarType::Double;
    check_equiv(meta, "scalar-double");

    meta.dtype = ScalarType::Byte;
    check_equiv(meta, "scalar-byte");

    std::printf("  test_scalar_tensor: PASSED\n");
}

// ── Edge: zero-size tensor ────────────────────────────────────────

static void test_zero_size_tensor() {
    // Zero in any dim → total bytes = 0.
    auto m1 = make_meta({0}, {1});
    assert(compute_storage_nbytes_simd(m1) == 0);
    check_equiv(m1, "zero-1d");

    auto m2 = make_meta({3, 0, 5}, {1, 1, 1});
    assert(compute_storage_nbytes_simd(m2) == 0);
    check_equiv(m2, "zero-mid-3d");

    auto m3 = make_meta({3, 4, 0}, {1, 1, 1});
    assert(compute_storage_nbytes_simd(m3) == 0);
    check_equiv(m3, "zero-trailing-3d");

    std::printf("  test_zero_size_tensor: PASSED\n");
}

// ── Common shapes ─────────────────────────────────────────────────

static void test_common_shapes() {
    // 1D contiguous float [4096] → 4096 × 4 = 16384 bytes.
    auto m1d = make_meta({4096}, {1});
    check_equiv(m1d, "1D-contig");
    assert(compute_storage_nbytes_simd(m1d) == 16384);

    // 2D matrix [128, 256] strides [256, 1] → 128*256 = 32768 elems × 4 = 131072 bytes.
    auto m2d = make_meta({128, 256}, {256, 1});
    check_equiv(m2d, "2D-matrix");
    assert(compute_storage_nbytes_simd(m2d) == 131072);

    // 4D NCHW [32, 64, 224, 224]
    auto mnchw = make_meta({32, 64, 224, 224},
                           {64*224*224, 224*224, 224, 1});
    check_equiv(mnchw, "4D-NCHW");

    // 8D worst case
    auto m8d = make_meta({2, 3, 5, 7, 11, 13, 17, 19},
                         {1, 2, 3, 4, 5, 6, 7, 8});
    check_equiv(m8d, "8D");

    std::printf("  test_common_shapes: PASSED\n");
}

// ── Negative strides (transpose / flip) ───────────────────────────

static void test_negative_strides() {
    // 1D flipped [8] strides [-1] — span = 8, but min offset = -7.
    auto m1 = make_meta({8}, {-1});
    check_equiv(m1, "neg-1d");

    // 2D flipped row [4, 8] strides [-8, 1]
    auto m2 = make_meta({4, 8}, {-8, 1});
    check_equiv(m2, "neg-row-2d");

    // 2D mixed signs
    auto m3 = make_meta({4, 8}, {8, -1});
    check_equiv(m3, "mixed-signs-2d");

    // Larger: 4D with multiple negative strides
    auto m4 = make_meta({3, 4, 5, 6}, {-120, 30, -6, 1});
    check_equiv(m4, "neg-multi-4d");

    std::printf("  test_negative_strides: PASSED\n");
}

// ── Stride zero (broadcasting) ────────────────────────────────────

static void test_stride_zero() {
    // Broadcast-style: stride 0 means dim contributes no offset.
    auto m1 = make_meta({4, 8}, {0, 1});
    check_equiv(m1, "stride0-leading");

    auto m2 = make_meta({4, 8}, {1, 0});
    check_equiv(m2, "stride0-trailing");

    auto m3 = make_meta({3, 4, 5}, {0, 0, 1});
    check_equiv(m3, "stride0-multi");

    std::printf("  test_stride_zero: PASSED\n");
}

// ── dtype variations ──────────────────────────────────────────────

static void test_dtype_variations() {
    auto sizes_strides = []() {
        return make_meta({3, 4}, {4, 1});  // 12 elements
    };

    // Iterate through several dtypes; each should multiply
    // span (12) by element_size.
    for (auto dt : {ScalarType::Byte, ScalarType::Char,
                    ScalarType::Short, ScalarType::Int,
                    ScalarType::Long, ScalarType::Half,
                    ScalarType::Float, ScalarType::Double}) {
        auto m = sizes_strides();
        m.dtype = dt;
        check_equiv(m, "dtype-variation");
        const uint64_t expected = element_size(dt).times(uint64_t{12});
        assert(compute_storage_nbytes_simd(m) == expected);
    }

    std::printf("  test_dtype_variations: PASSED\n");
}

// ── Adversarial: overflow at multiply step ────────────────────────
//
// Adversarial TensorMeta that overflows int64 in (size-1)*stride.
// Both scalar AND simd MUST return UINT64_MAX (simd via fallback
// to scalar after pre-screen rejects the input).

static void test_overflow_multiply() {
    // sizes = [2^32 + 2], strides = [2^32] → (2^32+1) * 2^32 ≈ 2^64,
    // overflows int64.
    auto m1 = make_meta({(int64_t{1} << 32) + 2}, {int64_t{1} << 32});
    assert(compute_storage_nbytes_scalar(m1) == UINT64_MAX);
    assert(compute_storage_nbytes_simd(m1) == UINT64_MAX);
    check_equiv(m1, "overflow-mul-1d");

    // 2D where one dim's product overflows: sizes=[3, 2^32+1],
    // strides=[1, 2^31].  Per-dim extent at d=1: 2^32 * 2^31 = 2^63 → overflows.
    auto m2 = make_meta({3, (int64_t{1} << 32) + 1},
                        {1, int64_t{1} << 31});
    check_equiv(m2, "overflow-mul-2d");

    std::printf("  test_overflow_multiply: PASSED\n");
}

// ── Adversarial: overflow at fold (add) step ──────────────────────
//
// Inputs where multiply doesn't overflow but the additive fold
// across dims does.

static void test_overflow_add_fold() {
    // 8 dims, each with extent ~2^60.  Sum of 8 × 2^60 = 2^63
    // which is at the int64 boundary.
    // sizes=[2^31+1] × 8, strides=[2^29] × 8 → per-dim extent ≈ 2^60.
    auto m = make_meta(
        {int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31,
         int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31},
        {int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29,
         int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29});
    check_equiv(m, "overflow-add-fold-8d");

    std::printf("  test_overflow_add_fold: PASSED\n");
}

// ── Adversarial: INT64_MIN stride ─────────────────────────────────
//
// INT64_MIN as a stride is the boundary case for the abs() compute
// in the pre-screen.  Without the special-case mapping to INT64_MAX,
// -INT64_MIN would wrap to INT64_MIN.  The pre-screen MUST reject
// this input (force scalar fallback) so the result matches scalar.

static void test_int64_min_stride() {
    // Single dim with INT64_MIN stride — extent calculation
    // overflows int64, scalar returns UINT64_MAX, SIMD must too.
    auto m = make_meta({2}, {INT64_MIN});
    check_equiv(m, "int64-min-stride");

    std::printf("  test_int64_min_stride: PASSED\n");
}

// ── Random fuzz: many well-bounded inputs ─────────────────────────
//
// Generate hundreds of random TensorMetas with bounded sizes and
// strides; assert SIMD == scalar bit-for-bit.  Bounded inputs
// stay on the SIMD fast-path (no fallback).

static void test_random_well_bounded() {
    std::mt19937_64 rng{0xCAFEBABEDEADBEEFULL};
    std::uniform_int_distribution<int> ndim_dist(1, 8);
    std::uniform_int_distribution<int64_t> size_dist(1, 1024);
    std::uniform_int_distribution<int64_t> stride_dist(-1024, 1024);

    constexpr int N_TRIALS = 1000;
    for (int t = 0; t < N_TRIALS; ++t) {
        TensorMeta m{};
        m.ndim = static_cast<uint8_t>(ndim_dist(rng));
        m.dtype = ScalarType::Float;
        for (uint8_t d = 0; d < m.ndim; ++d) {
            m.sizes[d] = size_dist(rng);
            m.strides[d] = stride_dist(rng);
        }
        check_equiv(m, "random-bounded");
    }

    std::printf("  test_random_well_bounded: PASSED (%d trials)\n", N_TRIALS);
}

// ── Random fuzz: extreme sizes/strides (tests fallback path) ──────
//
// Larger random ranges that force pre-screen failures and
// fallback to scalar.  SIMD must still match scalar exactly.

static void test_random_extreme() {
    std::mt19937_64 rng{0xFEEDFACEBADC0DE5ULL};
    std::uniform_int_distribution<int> ndim_dist(1, 8);
    std::uniform_int_distribution<int64_t> size_dist(1, int64_t{1} << 40);
    std::uniform_int_distribution<int64_t> stride_dist(
        -(int64_t{1} << 40), int64_t{1} << 40);

    constexpr int N_TRIALS = 500;
    for (int t = 0; t < N_TRIALS; ++t) {
        TensorMeta m{};
        m.ndim = static_cast<uint8_t>(ndim_dist(rng));
        m.dtype = ScalarType::Float;
        for (uint8_t d = 0; d < m.ndim; ++d) {
            m.sizes[d] = size_dist(rng);
            m.strides[d] = stride_dist(rng);
        }
        check_equiv(m, "random-extreme");
    }

    std::printf("  test_random_extreme: PASSED (%d trials)\n", N_TRIALS);
}

int main() {
    std::printf("test_storage_nbytes_simd:\n");

    test_scalar_tensor();
    test_zero_size_tensor();
    test_common_shapes();
    test_negative_strides();
    test_stride_zero();
    test_dtype_variations();
    test_overflow_multiply();
    test_overflow_add_fold();
    test_int64_min_stride();
    test_random_well_bounded();
    test_random_extreme();

    std::printf("test_storage_nbytes_simd: ALL PASSED\n");
    return 0;
}
