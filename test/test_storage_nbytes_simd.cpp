// The vector path must return output bit-identical to the scalar path.
// The cases below are hand-picked to cover the algorithmically distinct
// parts of the input space.  The two random cases draw their inputs from
// Philox4x32-10, so each run and each host draws the same inputs.

#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The bounds of a draw of a random case: the smallest and the largest value.
struct DrawBounds {
    int64_t lowest = 0;
    int64_t highest = 0;
};

// The bounds of the inputs of a random case.
struct RandomInputs {
    DrawBounds sizes{};
    DrawBounds strides{};
};

// The Philox key of each random case.
enum class RandomKey : uint64_t {
    well_bounded = 0xCAFEBABEDEADBEEFULL,
    extreme = 0xFEEDFACEBADC0DE5ULL,
};

// The number of a trial of a random case.
enum class Trial : uint64_t {
};

// The blocks of one trial: one for the rank, and one for each dimension.
constexpr uint64_t kBlocksPerTrial = 9;

// The word of 64 bits of the two words of the block from the first one, the
// high word first.
[[nodiscard]] uint64_t word_pair(::fixy::Philox4x32::Counter const& block, ::fixy::Index<3> first) noexcept {
    uint64_t const high = *block.get(first.raw()).expect("a block holds four words");
    uint64_t const low = *block.get(first.raw() + 1).expect("a block holds four words");
    return (high << 32) | low;
}

// A value of the bounds from the two words of the block from the first one:
// the lowest value plus their word modulo the count of the values of the
// bounds.
[[nodiscard]] int64_t draw_in(DrawBounds bounds, ::fixy::Philox4x32::Counter const& block,
                              ::fixy::Index<3> first) noexcept {
    uint64_t const span = static_cast<uint64_t>(bounds.highest) - static_cast<uint64_t>(bounds.lowest) + 1;
    return static_cast<int64_t>(static_cast<uint64_t>(bounds.lowest) + word_pair(block, first) % span);
}

// The descriptor of a trial of a random case: a rank from one to eight, and a
// size and a stride of the bounds for each dimension, all of Float.
[[nodiscard]] ::crucible::TensorMeta random_meta(RandomKey key, RandomInputs inputs, Trial trial) noexcept {
    uint64_t const first = std::to_underlying(trial) * kBlocksPerTrial;
    ::fixy::Philox4x32::Counter const rank = ::fixy::Philox4x32::generate(first, std::to_underlying(key));
    ::crucible::TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(1 + *rank.get(0).expect("a block holds four words") % 8);
    meta.dtype = ::crucible::ScalarType::Float;
    for (uint8_t d = 0; d < meta.ndim; ++d) {
        ::fixy::Philox4x32::Counter const block = ::fixy::Philox4x32::generate(first + 1 + d, std::to_underlying(key));
        meta.sizes[d] = ::crucible::tensor_dim(draw_in(inputs.sizes, block, ::fixy::index<0>()));
        meta.strides[d] = ::crucible::tensor_dim(draw_in(inputs.strides, block, ::fixy::index<2>()));
    }
    return meta;
}

}  // namespace

using namespace crucible;
using namespace crucible::detail;

using StorageNbytesDet = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::fixy::Saturated<uint64_t>>;
static_assert(
    std::is_same_v<decltype(compute_storage_nbytes_scalar_det(external_tensor_meta(std::declval<const TensorMeta&>()))),
                   StorageNbytesDet>);
static_assert(
    std::is_same_v<decltype(compute_storage_nbytes_simd_det(external_tensor_meta(std::declval<const TensorMeta&>()))),
                   StorageNbytesDet>);
static_assert(sizeof(StorageNbytesDet) == sizeof(::fixy::Saturated<uint64_t>));

[[nodiscard]] static TensorMeta make_meta(std::initializer_list<int64_t> sizes, std::initializer_list<int64_t> strides,
                                          ScalarType dtype = ScalarType::Float) noexcept {
    CRUCIBLE_TEST_ASSERT(kTest, sizes.size() == strides.size());
    CRUCIBLE_TEST_ASSERT(kTest, sizes.size() <= 8);

    TensorMeta meta{};
    meta.ndim = static_cast<uint8_t>(sizes.size());
    meta.dtype = dtype;

    size_t d = 0;
    for (int64_t const size : sizes) {
        meta.sizes[d] = ::crucible::tensor_dim(size);
        ++d;
    }
    d = 0;
    for (int64_t const stride : strides) {
        meta.strides[d] = ::crucible::tensor_dim(stride);
        ++d;
    }
    return meta;
}

[[nodiscard]] static ExternalTensorMeta external_meta(const TensorMeta& meta) noexcept {
    return external_tensor_meta(meta);
}

static void check_equiv(const TensorMeta& meta, ::fixy::TextView what) noexcept {
    // Equality on the saturated type is defaulted, so it compares the
    // value and the clamped flag together.  Both paths must agree on
    // both: a difference in the flag alone would mean one path saturated
    // silently while the other returned a real byte count.
    const auto external = external_meta(meta);
    const auto scalar = compute_storage_nbytes_scalar(external);
    const auto simd_v = compute_storage_nbytes_simd(external);
    const auto scalar_det = compute_storage_nbytes_scalar_det(external);
    const auto simd_det = compute_storage_nbytes_simd_det(external);
    CRUCIBLE_TEST_ASSERT(kTest, scalar_det.peek() == scalar);
    CRUCIBLE_TEST_ASSERT(kTest, simd_det.peek() == simd_v);
    if (scalar != simd_v) {
        ::fixy::report(::fixy::Sink::Err,
                       "[{}] MISMATCH: scalar={}(clamped={}) simd={}(clamped={})\n"
                       "  ndim={} dtype={} sizes=[",
                       what, scalar.value(), static_cast<int>(scalar.was_clamped()), simd_v.value(),
                       static_cast<int>(simd_v.was_clamped()), static_cast<unsigned>(meta.ndim),
                       static_cast<int>(meta.dtype));
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            ::fixy::report(::fixy::Sink::Err, "{}{}", crucible::raw_tensor_dim(meta.sizes[d]),
                           d + 1 < meta.ndim ? ::fixy::TextView{","} : ::fixy::TextView{""});
        }
        ::fixy::report(::fixy::Sink::Err, "] strides=[");
        for (uint8_t d = 0; d < meta.ndim; ++d) {
            ::fixy::report(::fixy::Sink::Err, "{}{}", crucible::raw_tensor_dim(meta.strides[d]),
                           d + 1 < meta.ndim ? ::fixy::TextView{","} : ::fixy::TextView{""});
        }
        ::fixy::report(::fixy::Sink::Err, "]\n");
        ::fixy::fatal("the scalar path and the vector path give two sizes of storage");
    }
}

static void test_scalar_tensor() {
    TensorMeta meta{};
    meta.ndim = 0;
    meta.dtype = ScalarType::Float;
    check_equiv(meta, "scalar-float");
    CRUCIBLE_TEST_ASSERT(kTest,
                         compute_storage_nbytes_simd(external_meta(meta)) == element_size(ScalarType::Float).raw());

    meta.dtype = ScalarType::Double;
    check_equiv(meta, "scalar-double");

    meta.dtype = ScalarType::Byte;
    check_equiv(meta, "scalar-byte");

    ::fixy::check::pass(kTest, "  test_scalar_tensor: PASSED\n");
}

static void test_zero_size_tensor() {
    // A zero in any dimension takes the whole tensor to zero bytes.  The
    // lambda keeps one copy of the inlined vector path in this test.
    const auto expect_zero_bytes = [](const TensorMeta& meta) {
        CRUCIBLE_TEST_ASSERT(kTest, compute_storage_nbytes_simd(external_meta(meta)) == 0);
    };

    auto m1 = make_meta({0}, {1});
    expect_zero_bytes(m1);
    check_equiv(m1, "zero-1d");

    auto m2 = make_meta({3, 0, 5}, {1, 1, 1});
    expect_zero_bytes(m2);
    check_equiv(m2, "zero-mid-3d");

    auto m3 = make_meta({3, 4, 0}, {1, 1, 1});
    expect_zero_bytes(m3);
    check_equiv(m3, "zero-trailing-3d");

    ::fixy::check::pass(kTest, "  test_zero_size_tensor: PASSED\n");
}

static void test_common_shapes() {
    // 4096 floats at four bytes each make 16384 bytes.
    auto m1d = make_meta({4096}, {1});
    check_equiv(m1d, "1D-contig");
    CRUCIBLE_TEST_ASSERT(kTest, compute_storage_nbytes_simd(external_meta(m1d)) == 16384);

    // 128 by 256 is 32768 elements, which at four bytes each make
    // 131072 bytes.
    auto m2d = make_meta({128, 256}, {256, 1});
    check_equiv(m2d, "2D-matrix");
    CRUCIBLE_TEST_ASSERT(kTest, compute_storage_nbytes_simd(external_meta(m2d)) == 131072);

    auto mnchw = make_meta({32, 64, 224, 224}, {64 * 224 * 224, 224 * 224, 224, 1});
    check_equiv(mnchw, "4D-NCHW");

    auto m8d = make_meta({2, 3, 5, 7, 11, 13, 17, 19}, {1, 2, 3, 4, 5, 6, 7, 8});
    check_equiv(m8d, "8D");

    ::fixy::check::pass(kTest, "  test_common_shapes: PASSED\n");
}

// A TensorMeta one natural alignment past a boundary of 64 bytes: the lead
// fills the bytes before it.
struct alignas(64) MisalignedMeta {
    ::fixy::FixedArray<uint8_t, alignof(TensorMeta)> lead{};
    TensorMeta meta{};
};

static void test_natural_tensor_meta_alignment() {
    // The buffers a TensorMeta arrives in guarantee its natural
    // alignment only, never 64-byte vector alignment.  The vector path
    // must therefore load the metadata fields element-aligned.
    MisalignedMeta holder{};
    holder.meta = make_meta({16, 32, 64}, {2048, 64, 1});
    CRUCIBLE_TEST_ASSERT(
        kTest, !::fixy::address_of(::fixy::to(holder.meta.sizes.lanes().at(::fixy::index<0>()))).is_aligned<64>());
    check_equiv(holder.meta, "natural-align-not-vector-align");

    ::fixy::check::pass(kTest, "  test_natural_tensor_meta_alignment: PASSED\n");
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

    ::fixy::check::pass(kTest, "  test_negative_strides: PASSED\n");
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

    ::fixy::check::pass(kTest, "  test_stride_zero: PASSED\n");
}

// The dtypes of test_dtype_variations.
constexpr ::fixy::FixedArray<ScalarType, 8> kDtypes{ScalarType::Byte,  ScalarType::Char,  ScalarType::Short,
                                                    ScalarType::Int,   ScalarType::Long,  ScalarType::Half,
                                                    ScalarType::Float, ScalarType::Double};

static void test_dtype_variations() {
    auto sizes_strides = []() {
        return make_meta({3, 4}, {4, 1});  // 12 elements
    };

    for (ScalarType const dt : kDtypes) {
        auto m = sizes_strides();
        m.dtype = dt;
        check_equiv(m, "dtype-variation");
        const uint64_t expected = element_size(dt).times(uint64_t{12});
        CRUCIBLE_TEST_ASSERT(kTest, compute_storage_nbytes_simd(external_meta(m)) == expected);
    }

    ::fixy::check::pass(kTest, "  test_dtype_variations: PASSED\n");
}

// These inputs overflow int64 in the (size - 1) times stride step.  Both
// paths must return the saturated maximum, the vector one by falling back
// to scalar once the pre-screen rejects the input.

static void test_overflow_multiply() {
    // 2^32 plus one, times 2^32, is about 2^64 and overflows int64.
    auto m1 = make_meta({(int64_t{1} << 32) + 2}, {int64_t{1} << 32});
    {
        const auto sc = compute_storage_nbytes_scalar(external_meta(m1));
        CRUCIBLE_TEST_ASSERT(kTest, sc.value() == UINT64_MAX);
        CRUCIBLE_TEST_ASSERT(kTest, sc.was_clamped());
    }
    {
        const auto sv = compute_storage_nbytes_simd(external_meta(m1));
        CRUCIBLE_TEST_ASSERT(kTest, sv.value() == UINT64_MAX);
        CRUCIBLE_TEST_ASSERT(kTest, sv.was_clamped());
    }
    check_equiv(m1, "overflow-mul-1d");

    // The extent of the second dimension is 2^32 times 2^31, which is
    // 2^63 and overflows on its own.
    auto m2 = make_meta({3, (int64_t{1} << 32) + 1}, {1, int64_t{1} << 31});
    check_equiv(m2, "overflow-mul-2d");

    ::fixy::check::pass(kTest, "  test_overflow_multiply: PASSED\n");
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

    ::fixy::check::pass(kTest, "  test_overflow_add_fold: PASSED\n");
}

// INT64_MIN is the boundary case for the absolute value the pre-screen
// computes: negating it wraps back to itself unless it is special-cased
// to INT64_MAX.  The pre-screen must reject this input and fall back to
// scalar, or the two paths part company.

static void test_int64_min_stride() {
    auto m = make_meta({2}, {INT64_MIN});
    check_equiv(m, "int64-min-stride");

    ::fixy::check::pass(kTest, "  test_int64_min_stride: PASSED\n");
}

// A negative size is not a valid extent, but a hostile descriptor can hold
// one.  The screen bounds each product by the largest size less one, which
// is not a bound when a size is negative: the second case below passes a
// screen of 1 * 2^40 and then multiplies -(2^40 + 1) by 2^40.  A live size
// below one therefore goes to the scalar routine.
static void test_negative_size() {
    constexpr int64_t kBig = int64_t{1} << 40;
    check_equiv(make_meta({-3}, {5}), "negative-size");
    check_equiv(make_meta({2, -kBig}, {0, kBig}), "negative-size-past-the-screen");
    check_equiv(make_meta({INT64_MIN}, {0}), "int64-min-size-stride0");
    check_equiv(make_meta({INT64_MIN}, {1}), "int64-min-size-stride1");
    check_equiv(make_meta({INT64_MIN}, {-1}), "int64-min-size-stride-neg1");
    check_equiv(make_meta({INT64_MIN}, {7}), "int64-min-size-stride7");
    check_equiv(make_meta({INT64_MIN, 3}, {INT64_MIN, 2}), "int64-min-size-and-stride");

    // A modular subtraction gives INT64_MAX for INT64_MIN, and a stride of
    // one keeps the product in range, so the span is one past INT64_MAX: it
    // saturates.
    const auto saturated = compute_storage_nbytes_scalar(external_meta(make_meta({INT64_MIN}, {1})));
    CRUCIBLE_TEST_ASSERT(kTest, saturated.was_clamped());

    ::fixy::check::pass(kTest, "  test_negative_size: PASSED\n");
}

// The lanes past ndim take part in every vector operation, and a hostile
// descriptor can hold any value in them.  Each lane holds the most negative
// value here, and the answer must still be the one live dimension alone.
static void test_dead_lanes_hold_extremes() {
    TensorMeta m{};
    m.dtype = ScalarType::Float;
    for (uint8_t d = 0; d < 8; ++d) {
        m.sizes[d] = ::crucible::tensor_dim(INT64_MIN);
        m.strides[d] = ::crucible::tensor_dim(INT64_MIN);
    }
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(4);
    m.strides[0] = ::crucible::tensor_dim(8);
    check_equiv(m, "dead-lanes-int64-min");

    // Three steps of eight elements past the first, plus the first, of four
    // bytes each.
    const auto span = compute_storage_nbytes_simd(external_meta(m));
    CRUCIBLE_TEST_ASSERT(kTest, !span.was_clamped() && span.value() == 100);

    ::fixy::check::pass(kTest, "  test_dead_lanes_hold_extremes: PASSED\n");
}

// These bounds keep every generated input on the vector fast path, so
// the fallback never runs here.

static void test_random_well_bounded() {
    constexpr RandomInputs kInputs{.sizes = {.lowest = 1, .highest = 1024},
                                   .strides = {.lowest = -1024, .highest = 1024}};

    constexpr int N_TRIALS = 1000;
    for (int t = 0; t < N_TRIALS; ++t) {
        check_equiv(random_meta(RandomKey::well_bounded, kInputs, Trial{static_cast<uint64_t>(t)}), "random-bounded");
    }

    ::fixy::check::pass(kTest, "  test_random_well_bounded: PASSED ({} trials)\n", N_TRIALS);
}

// These bounds are wide enough to fail the pre-screen, so this case
// drives the fallback instead.

static void test_random_extreme() {
    constexpr RandomInputs kInputs{.sizes = {.lowest = 1, .highest = int64_t{1} << 40},
                                   .strides = {.lowest = -(int64_t{1} << 40), .highest = int64_t{1} << 40}};

    constexpr int N_TRIALS = 500;
    for (int t = 0; t < N_TRIALS; ++t) {
        check_equiv(random_meta(RandomKey::extreme, kInputs, Trial{static_cast<uint64_t>(t)}), "random-extreme");
    }

    ::fixy::check::pass(kTest, "  test_random_extreme: PASSED ({} trials)\n", N_TRIALS);
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_storage_nbytes_simd:\n");

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
    test_negative_size();
    test_dead_lanes_hold_extremes();
    test_random_well_bounded();
    test_random_extreme();

    ::fixy::check::pass(kTest, "test_storage_nbytes_simd: ALL PASSED\n");
    return 0;
}
