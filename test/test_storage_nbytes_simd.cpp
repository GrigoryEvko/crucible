// The vector path must return output bit-identical to the scalar path.
// The cases below are hand-picked to cover the algorithmically distinct
// parts of the input space.

#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>

#include <array>
#include "test_assert.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <random>
#include <type_traits>
#include <utility>

using namespace crucible;
using namespace crucible::detail;

using StorageNbytesDet = safety::DetSafe<safety::DetSafeTier_v::Pure, safety::Saturated<uint64_t>>;
static_assert(
    std::is_same_v<decltype(compute_storage_nbytes_scalar_det(external_tensor_meta(std::declval<const TensorMeta&>()))),
                   StorageNbytesDet>);
static_assert(
    std::is_same_v<decltype(compute_storage_nbytes_simd_det(external_tensor_meta(std::declval<const TensorMeta&>()))),
                   StorageNbytesDet>);
static_assert(sizeof(StorageNbytesDet) == sizeof(safety::Saturated<uint64_t>));

[[nodiscard]] static TensorMeta make_meta(std::initializer_list<int64_t> sizes, std::initializer_list<int64_t> strides,
                                          ScalarType dtype = ScalarType::Float) noexcept {
    assert(sizes.size() == strides.size());
    assert(sizes.size() <= 8);

    TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    meta.dtype = dtype;

    auto si = sizes.begin();
    auto ti = strides.begin();
    for (size_t d = 0; d < sizes.size(); ++d) {
        meta.sizes[d] = ::crucible::tensor_dim(*si++);
        meta.strides[d] = ::crucible::tensor_dim(*ti++);
    }
    return meta;
}

[[nodiscard]] static ExternalTensorMeta external_meta(const TensorMeta& meta) noexcept {
    return external_tensor_meta(meta);
}

static void check_equiv(const TensorMeta& meta, const char* what) noexcept {
    // Equality on the saturated type is defaulted, so it compares the
    // value and the clamped flag together.  Both paths must agree on
    // both: a difference in the flag alone would mean one path saturated
    // silently while the other returned a real byte count.
    const auto external = external_meta(meta);
    const auto scalar = compute_storage_nbytes_scalar(external);
    const auto simd_v = compute_storage_nbytes_simd(external);
    const auto scalar_det = compute_storage_nbytes_scalar_det(external);
    const auto simd_det = compute_storage_nbytes_simd_det(external);
    assert(scalar_det.peek() == scalar);
    assert(simd_det.peek() == simd_v);
    if (scalar != simd_v) {
        std::fprintf(stderr,
                     "[%s] MISMATCH: scalar=%llu(clamped=%d) simd=%llu(clamped=%d)\n"
                     "  ndim=%u dtype=%d sizes=[",
                     what, static_cast<unsigned long long>(scalar.value()), int(scalar.was_clamped()),
                     static_cast<unsigned long long>(simd_v.value()), int(simd_v.was_clamped()), unsigned(meta.ndim),
                     int(meta.dtype));
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            std::fprintf(stderr, "%lld%s", static_cast<long long>(crucible::raw_tensor_dim(meta.sizes[d])),
                         d + 1 < meta.ndim ? "," : "");
        }
        std::fprintf(stderr, "] strides=[");
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            std::fprintf(stderr, "%lld%s", static_cast<long long>(crucible::raw_tensor_dim(meta.strides[d])),
                         d + 1 < meta.ndim ? "," : "");
        }
        std::fprintf(stderr, "]\n");
        std::abort();
    }
}

static void test_scalar_tensor() {
    TensorMeta meta{};
    meta.ndim = 0;
    meta.dtype = ScalarType::Float;
    check_equiv(meta, "scalar-float");
    assert(compute_storage_nbytes_simd(external_meta(meta)) == element_size(ScalarType::Float).raw());

    meta.dtype = ScalarType::Double;
    check_equiv(meta, "scalar-double");

    meta.dtype = ScalarType::Byte;
    check_equiv(meta, "scalar-byte");

    std::printf("  test_scalar_tensor: PASSED\n");
}

static void test_zero_size_tensor() {
    // A zero in any dimension takes the whole tensor to zero bytes.
    auto m1 = make_meta({0}, {1});
    assert(compute_storage_nbytes_simd(external_meta(m1)) == 0);
    check_equiv(m1, "zero-1d");

    auto m2 = make_meta({3, 0, 5}, {1, 1, 1});
    assert(compute_storage_nbytes_simd(external_meta(m2)) == 0);
    check_equiv(m2, "zero-mid-3d");

    auto m3 = make_meta({3, 4, 0}, {1, 1, 1});
    assert(compute_storage_nbytes_simd(external_meta(m3)) == 0);
    check_equiv(m3, "zero-trailing-3d");

    std::printf("  test_zero_size_tensor: PASSED\n");
}

static void test_common_shapes() {
    // 4096 floats at four bytes each make 16384 bytes.
    auto m1d = make_meta({4096}, {1});
    check_equiv(m1d, "1D-contig");
    assert(compute_storage_nbytes_simd(external_meta(m1d)) == 16384);

    // 128 by 256 is 32768 elements, which at four bytes each make
    // 131072 bytes.
    auto m2d = make_meta({128, 256}, {256, 1});
    check_equiv(m2d, "2D-matrix");
    assert(compute_storage_nbytes_simd(external_meta(m2d)) == 131072);

    auto mnchw = make_meta({32, 64, 224, 224}, {64 * 224 * 224, 224 * 224, 224, 1});
    check_equiv(mnchw, "4D-NCHW");

    auto m8d = make_meta({2, 3, 5, 7, 11, 13, 17, 19}, {1, 2, 3, 4, 5, 6, 7, 8});
    check_equiv(m8d, "8D");

    std::printf("  test_common_shapes: PASSED\n");
}

static void test_natural_tensor_meta_alignment() {
    // The buffers a TensorMeta arrives in guarantee its natural
    // alignment only, never 64-byte vector alignment.  The vector path
    // must therefore load the metadata fields element-aligned.
    alignas(TensorMeta) std::array<std::byte, sizeof(TensorMeta) + 64> storage{};
    std::uintptr_t chosen = 0;
    const std::uintptr_t base = std::bit_cast<std::uintptr_t>(storage.data());
    for (std::size_t offset = 0; offset < 64; ++offset) {
        const std::uintptr_t candidate = base + offset;
        if (candidate % alignof(TensorMeta) == 0 && candidate % 64 != 0) {
            chosen = candidate;
            break;
        }
    }
    assert(chosen != 0);

    auto* meta = std::construct_at(std::bit_cast<TensorMeta*>(chosen), make_meta({16, 32, 64}, {2048, 64, 1}));
    assert(std::bit_cast<std::uintptr_t>(meta->sizes.raw_data()) % 64 != 0);
    check_equiv(*meta, "natural-align-not-vector-align");
    std::destroy_at(meta);

    std::printf("  test_natural_tensor_meta_alignment: PASSED\n");
}

static void test_negative_strides() {
    // A flipped dimension of eight spans eight elements, but its lowest
    // offset is minus seven.
    auto m1 = make_meta({8}, {-1});
    check_equiv(m1, "neg-1d");

    auto m2 = make_meta({4, 8}, {-8, 1});
    check_equiv(m2, "neg-row-2d");

    auto m3 = make_meta({4, 8}, {8, -1});
    check_equiv(m3, "mixed-signs-2d");

    auto m4 = make_meta({3, 4, 5, 6}, {-120, 30, -6, 1});
    check_equiv(m4, "neg-multi-4d");

    std::printf("  test_negative_strides: PASSED\n");
}

static void test_stride_zero() {
    // A stride of zero means the dimension contributes no offset, which
    // is how a broadcast dimension is spelled.
    auto m1 = make_meta({4, 8}, {0, 1});
    check_equiv(m1, "stride0-leading");

    auto m2 = make_meta({4, 8}, {1, 0});
    check_equiv(m2, "stride0-trailing");

    auto m3 = make_meta({3, 4, 5}, {0, 0, 1});
    check_equiv(m3, "stride0-multi");

    std::printf("  test_stride_zero: PASSED\n");
}

static void test_dtype_variations() {
    auto sizes_strides = []() {
        return make_meta({3, 4}, {4, 1});  // 12 elements
    };

    for (auto dt : {ScalarType::Byte, ScalarType::Char, ScalarType::Short, ScalarType::Int, ScalarType::Long,
                    ScalarType::Half, ScalarType::Float, ScalarType::Double}) {
        auto m = sizes_strides();
        m.dtype = dt;
        check_equiv(m, "dtype-variation");
        const uint64_t expected = element_size(dt).times(uint64_t{12});
        assert(compute_storage_nbytes_simd(external_meta(m)) == expected);
    }

    std::printf("  test_dtype_variations: PASSED\n");
}

// These inputs overflow int64 in the (size - 1) times stride step.  Both
// paths must return the saturated maximum, the vector one by falling back
// to scalar once the pre-screen rejects the input.

static void test_overflow_multiply() {
    // 2^32 plus one, times 2^32, is about 2^64 and overflows int64.
    auto m1 = make_meta({(int64_t{1} << 32) + 2}, {int64_t{1} << 32});
    {
        const auto sc = compute_storage_nbytes_scalar(external_meta(m1));
        assert(sc.value() == UINT64_MAX);
        assert(sc.was_clamped());
    }
    {
        const auto sv = compute_storage_nbytes_simd(external_meta(m1));
        assert(sv.value() == UINT64_MAX);
        assert(sv.was_clamped());
    }
    check_equiv(m1, "overflow-mul-1d");

    // The extent of the second dimension is 2^32 times 2^31, which is
    // 2^63 and overflows on its own.
    auto m2 = make_meta({3, (int64_t{1} << 32) + 1}, {1, int64_t{1} << 31});
    check_equiv(m2, "overflow-mul-2d");

    std::printf("  test_overflow_multiply: PASSED\n");
}

// Here the multiply stays in range and the additive fold across the
// dimensions is what overflows.

static void test_overflow_add_fold() {
    // Each of the eight dimensions has an extent near 2^60, and eight of
    // those sum to about 2^63, the int64 boundary.
    auto m = make_meta({int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31,
                        int64_t{1} << 31, int64_t{1} << 31, int64_t{1} << 31},
                       {int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29,
                        int64_t{1} << 29, int64_t{1} << 29, int64_t{1} << 29});
    check_equiv(m, "overflow-add-fold-8d");

    std::printf("  test_overflow_add_fold: PASSED\n");
}

// INT64_MIN is the boundary case for the absolute value the pre-screen
// computes: negating it wraps back to itself unless it is special-cased
// to INT64_MAX.  The pre-screen must reject this input and fall back to
// scalar, or the two paths part company.

static void test_int64_min_stride() {
    auto m = make_meta({2}, {INT64_MIN});
    check_equiv(m, "int64-min-stride");

    std::printf("  test_int64_min_stride: PASSED\n");
}

// These bounds keep every generated input on the vector fast path, so
// the fallback never runs here.

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
            m.sizes[d] = ::crucible::tensor_dim(size_dist(rng));
            m.strides[d] = ::crucible::tensor_dim(stride_dist(rng));
        }
        check_equiv(m, "random-bounded");
    }

    std::printf("  test_random_well_bounded: PASSED (%d trials)\n", N_TRIALS);
}

// These bounds are wide enough to fail the pre-screen, so this case
// drives the fallback instead.

static void test_random_extreme() {
    std::mt19937_64 rng{0xFEEDFACEBADC0DE5ULL};
    std::uniform_int_distribution<int> ndim_dist(1, 8);
    std::uniform_int_distribution<int64_t> size_dist(1, int64_t{1} << 40);
    std::uniform_int_distribution<int64_t> stride_dist(-(int64_t{1} << 40), int64_t{1} << 40);

    constexpr int N_TRIALS = 500;
    for (int t = 0; t < N_TRIALS; ++t) {
        TensorMeta m{};
        m.ndim = static_cast<uint8_t>(ndim_dist(rng));
        m.dtype = ScalarType::Float;
        for (uint8_t d = 0; d < m.ndim; ++d) {
            m.sizes[d] = ::crucible::tensor_dim(size_dist(rng));
            m.strides[d] = ::crucible::tensor_dim(stride_dist(rng));
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
    test_natural_tensor_meta_alignment();
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
