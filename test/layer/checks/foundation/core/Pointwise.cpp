// The compile-time checks of foundation/core/Pointwise.h.

#include <foundation/core/Pointwise.h>

#include <foundation/core/Array.h>
#include <foundation/core/Scalar.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::pointwise_checks {

// A generic op: a lane op for each element of the lane path.
struct Add final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return static_cast<X>(left + right);
    }
};

// An op with a member is no pointwise op: it could reach a run.
struct Scaled final {
    float factor = 2.0f;
    constexpr float operator()(float value) const noexcept { return value * factor; }
};

using Floats = StridedView<float const, 2>;
using Shorts = StridedView<std::uint16_t const, 2>;

static_assert(PointwiseOp<Add, float, Floats, Floats>);
static_assert(!PointwiseOp<Scaled, float, Floats>);
static_assert(LaneOpOf<Add, float, float, float>);
static_assert(LaneOpOf<Add, double, double, double>);
static_assert(LaneOpOf<Add, std::int64_t, std::int64_t, std::int64_t>);
// A narrow integer computes in int in a scalar step, so it is no lane element.
static_assert(!LaneOpOf<Add, std::uint16_t, std::uint16_t, std::uint16_t>);
static_assert(PointwiseOp<Add, std::uint16_t, Shorts, Shorts>);
// Lanes of different widths do not fill one register each.
static_assert(!LaneOpOf<Add, float, double, double>);
// A lane op gives a register of the output element.
using NativeFloats = ::foundation::simd::vec<float, ::foundation::simd::native_lanes<float>>;
static_assert(std::is_same_v<std::invoke_result_t<Add const&, NativeFloats, NativeFloats>, NativeFloats>);

// An op that keeps its bits, and an op with a call on elements only.
struct Copy final {
    static constexpr NanRule nan_rule = NanRule::KeepBits;

    template <class X>
    constexpr X operator()(X value) const noexcept {
        return value;
    }
};
struct ScalarOnly final {
    constexpr float operator()(float left, float right) const noexcept { return left + right; }
};

// THE NAN RULE: the default is Canonical, and the op under the rule is a lane
// op for the same elements as the op.
static_assert(nan_rule_of_<Add>() == NanRule::Canonical && nan_rule_of_<Copy>() == NanRule::KeepBits);
static_assert(std::is_same_v<decltype(ruled_op_<float>(Add{})), CanonicalNanOp<Add, float> const>
              || std::is_same_v<decltype(ruled_op_<float>(Add{})), CanonicalNanOp<Add, float>>);
static_assert(std::is_same_v<decltype(ruled_op_<float>(Copy{})), Copy>);
static_assert(std::is_same_v<decltype(ruled_op_<std::int64_t>(Add{})), Add>);
static_assert(LaneOpOf<CanonicalNanOp<Add, float>, float, float, float>);
static_assert(LaneOpOf<CanonicalNanOp<Add, double>, double, double, double>);
static_assert(!LaneOpOf<ScalarOnly, float, float, float>);
static_assert(!LaneOpOf<CanonicalNanOp<ScalarOnly, float>, float, float, float>);
static_assert(
    std::is_nothrow_invocable_r_v<float, CanonicalNanOp<ScalarOnly, float> const&, float const&, float const&>);

// The test of no overlap of an input with the output, the order of the axes
// and the axis of the tile path.
static_assert(lane_class_of_(0) == LaneClass::Splat && lane_class_of_(1) == LaneClass::Dense);
static_assert(lane_class_of_(2) == LaneClass::StepTwo && lane_class_of_(3) == LaneClass::General);
static_assert(tile_class_of_(64, 1) == TileClass::Transposed && tile_class_of_(1, 64) == TileClass::Dense);
static_assert(tile_class_of_(0, 64) == TileClass::Splat && tile_class_of_(3, 64) == TileClass::General);
static_assert(tile_class_of_(1, 1) == TileClass::Dense, "a stride of one on a reads a dense register");

// THE TRANSPOSE NETWORK gives the transpose for each shape of the targets:
// 16, 8 and 4 lanes of 4 bytes, and 8, 4 and 2 lanes of 8 bytes.
static_assert(transpose_map_<16, 4>.is_transpose && transpose_map_<8, 4>.is_transpose);
static_assert(transpose_map_<4, 4>.is_transpose && transpose_map_<8, 2>.is_transpose);
static_assert(transpose_map_<4, 2>.is_transpose && transpose_map_<2, 2>.is_transpose);
// The first stages of 16 floats are unpcklps and unpcklpd, and the third is
// vshuff32x4 with the selection 0x88: lane 4 takes the run 2 of the first
// register.
static_assert(transpose_source_(16, 4, 0, false, 1) == 16 && transpose_source_(16, 4, 1, false, 2) == 16);
static_assert(transpose_source_(16, 4, 2, false, 4) == 8 && transpose_source_(16, 4, 2, true, 8) == 20);

// The walk in a constant evaluation: the stated row-major order, with the op
// on lanes of one live element.  The second input is the output in place.
[[nodiscard]] consteval bool walks_in_constant_evaluation() noexcept {
    FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    FixedArray<int, 6> const addends{10, 20, 30, 40, 50, 60};
    FixedArray<std::size_t, 2> const sizes{2, 3};
    FixedArray<std::size_t, 2> const strides{3, 1};
    FixedArray<std::size_t, 2> const columns{1, 2};
    StridedView<int const, 2> const source =
        mint_strided_view(addends.view(), sizes.view(), columns.view(), 0).expect("the matrix fits");
    bool const is_written = values.with_mut([&sizes, &strides, &source](auto run) noexcept {
        auto out = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("a dense matrix");
        StridedView<int const, 2> const in_place = out.view();
        return write_pointwise(out, Add{}, in_place, source).is_ok();
    });
    // The source reads the addends in column-major order.
    return is_written && values == FixedArray<int, 6>{11, 32, 53, 24, 45, 66};
}
static_assert(walks_in_constant_evaluation());

// The walk of floats in a constant evaluation: a NaN input with a payload
// gives the canonical NaN, and the copy keeps the payload.
[[nodiscard]] consteval bool gives_canonical_nan_in_constant_evaluation() noexcept {
    FixedArray<float, 2> sums{};
    FixedArray<float, 2> copies{};
    FixedArray<float, 2> const values{bit_cast<float>(0x7FC12345u), 1.5f};
    FixedArray<std::size_t, 1> const sizes{2};
    FixedArray<std::size_t, 1> const strides{1};
    StridedView<float const, 1> const source =
        mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("the row fits");
    bool const is_summed = sums.with_mut([&sizes, &strides, &source](auto run) noexcept {
        auto out = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("a dense row");
        return write_pointwise(out, Add{}, source, source).is_ok();
    });
    bool const is_copied = copies.with_mut([&sizes, &strides, &source](auto run) noexcept {
        auto out = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("a dense row");
        return write_pointwise(out, Copy{}, source).is_ok();
    });
    // The sum of the NaN is the canonical NaN, and 1.5 + 1.5 is 3.0.  The copy
    // keeps the payload and the bits of 1.5.
    FixedArray<std::uint32_t, 2> const expected_sums{0x7FC00000u, 0x40400000u};
    FixedArray<std::uint32_t, 2> const expected_copies{0x7FC12345u, 0x3FC00000u};
    FixedArray<std::uint32_t, 2> sum_bits{};
    FixedArray<std::uint32_t, 2> copy_bits{};
    sum_bits.with_mut([&sums](auto run) noexcept {
        auto sum_cursor = run.begin();
        for (float const value : sums) {
            *sum_cursor = bit_cast<std::uint32_t>(value);
            ++sum_cursor;
        }
    });
    copy_bits.with_mut([&copies](auto run) noexcept {
        auto copy_cursor = run.begin();
        for (float const value : copies) {
            *copy_cursor = bit_cast<std::uint32_t>(value);
            ++copy_cursor;
        }
    });
    return is_summed && is_copied && sum_bits == expected_sums && copy_bits == expected_copies;
}
static_assert(gives_canonical_nan_in_constant_evaluation());

}  // namespace detail::pointwise_checks

}  // namespace foundation::core
