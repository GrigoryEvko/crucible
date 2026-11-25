// Tests of the pointwise walk of the strided views
// (foundation/core/Pointwise.h).
//
// Each property draws a shape, a unique layout of the output and a layout of
// each input, and puts the views in one run.  An input can take a region of
// its own, the layout of the output (the in-place form), or a region that
// overlaps the output in a different place.  The run holds bit patterns with
// NaN, infinity, zero of each sign and denormal numbers.  The oracle is walk()
// of foundation/core/Strided.h with the op on scalars over a copy of the run,
// in the stated row-major order.  The oracle applies the NAN rule of
// Pointwise.h in each step with a test of its own: the canonical NaN for each
// NaN output of a float or a double, unless the op keeps its bits.  The
// property compares the two runs bit for bit, each NaN too.  The sizes
// include inner lengths below, at and above each lane count, so the whole
// blocks, the masked blocks and each loop of a view get runs.  The run ends
// at the last element of its last region, so a read past a view is a read
// past the allocation, which the address sanitizer of a Debug build finds.
//
// The directed cases fill a run with NaNs of different payloads and signs,
// signaling NaNs too, or with infinities of the two signs, and take each path
// of the walk with layouts of their own.
//
// This unit holds main and the unit tests.  The units that pointwise_rig.h
// names hold the properties and the directed cases.

#include "pointwise_rig.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::test_pointwise::Add;
using ::test_pointwise::Divide;
using ::test_pointwise::Negate;
using ::test_pointwise::pass;
using ::test_pointwise::same_bits;
using ::test_pointwise::ScalarAdd;

static_assert(::foundation::core::LaneOpOf<Divide, float, float, float>, "the divide property runs the lane path");
static_assert(!::foundation::core::LaneOpOf<ScalarAdd, float, float, float>, "ScalarAdd takes the scalar path");

// ── Unit tests ──────────────────────────────────────────────────────────

// An input of another frame, with a brand of its own, takes the proof of the
// brands, and a dense add over two frames writes each sum.
void branded_inputs(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<float, 40>);
    auto [addends, addends_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<float, 40>);
    values.with_mut(values_key, [](auto run) noexcept {
        float next = 0.5f;
        for (float& value : run) {
            value = next;
            next += 1.0f;
        }
    });
    addends.with_mut(addends_key, [](auto run) noexcept {
        for (float& addend : run) {
            addend = 100.0f;
        }
    });
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 20};
    ::fixy::FixedArray<std::size_t, 2> const strides{20, 1};
    bool const is_written = values.with_mut(values_key, [&](auto target_run) noexcept {
        return addends.with_mut(addends_key, [&](auto source_run) noexcept {
            auto const source = ::fixy::mint_strided_view_mut(std::move(source_run), sizes.view(), strides.view(), 0)
                                    .expect("a dense matrix has no overlap");
            auto out = ::fixy::mint_strided_view_mut(std::move(target_run), sizes.view(), strides.view(), 0)
                           .expect("a dense matrix has no overlap");
            ::fixy::StridedView<float const, 2> const in_place = out.view();
            return ::foundation::core::write_pointwise(out, Add{}, in_place, source).is_ok();
        });
    });
    bool is_summed = true;
    float expected = 100.5f;
    for (float const value : values) {
        is_summed = is_summed && ::fixy::bit_cast<std::uint32_t>(value) == ::fixy::bit_cast<std::uint32_t>(expected);
        expected += 1.0f;
    }
    CRUCIBLE_TEST_ASSERT(test, is_written && is_summed);
    pass(test, "  branded_inputs: PASSED\n");
}

// Views of different sizes give ShapeMismatch, and the walk writes nothing.
void shape_mismatch(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 12> const others{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::FixedArray<std::size_t, 2> const other_sizes{4, 3};
    ::fixy::FixedArray<std::size_t, 2> const other_strides{3, 1};
    ::fixy::StridedView<int const, 2> const wrong =
        ::fixy::mint_strided_view(others.view(), other_sizes.view(), other_strides.view(), 0).expect("it fits");
    auto const [is_refused, values] = ::fixy::build_fixed_array<int, 12>([&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("dense");
        return ::foundation::core::write_pointwise(out, Negate{}, wrong).is_err();
    });
    CRUCIBLE_TEST_ASSERT(test, is_refused && values == (::fixy::FixedArray<int, 12>{}));
    pass(test, "  shape_mismatch: PASSED\n");
}

// An op with no input fills each element of the output, and a view with no
// element and a view of rank zero take one path each.
void fill_and_edges(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{7};
    ::fixy::FixedArray<std::size_t, 1> const strides{3};
    ::fixy::View<std::size_t const> const no_axes{};
    auto const [is_filled, values] = ::fixy::build_fixed_array<double, 21>([&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("it fits");
        return ::foundation::core::write_pointwise(out, [] noexcept { return 2.5; }).is_ok();
    });
    double total = 0.0;
    for (double const value : values) {
        total += value;
    }
    CRUCIBLE_TEST_ASSERT(test,
                         is_filled && ::fixy::bit_cast<std::uint64_t>(total) == ::fixy::bit_cast<std::uint64_t>(17.5));

    auto [point, point_key] =
        ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<double, 3>, ::fixy::FixedArray<double, 3>{1.0, 2.0, 3.0});
    ::fixy::FixedArray<double, 3> const source{4.0, 5.0, 6.0};
    ::fixy::StridedView<double const> const one =
        ::fixy::mint_strided_view(source.view(), no_axes, no_axes, 2).expect("one element fits");
    bool const is_one = point.with_mut(point_key, [&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), no_axes, no_axes, 1).expect("one element fits");
        return ::foundation::core::write_pointwise(out, Negate{}, one).is_ok();
    });
    ::fixy::FixedArray<double, 3> const one_expected{1.0, -6.0, 3.0};
    CRUCIBLE_TEST_ASSERT(
        test,
        is_one && same_bits(::fixy::View<double const>{point.view()}, ::fixy::View<double const>{one_expected.view()}));
    pass(test, "  fill_and_edges: PASSED\n");
}

// A walk of inputs over views of a static rank of one: the plan has room for
// one axis, so it takes the lanes and no tile.  A dense input and a step-two
// input give out[i] = (0.5 + i) + (20.5 + 2i).
void static_rank_one(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<float, 60> const sources = ::fixy::build_fixed_array<float, 60>([](auto run) noexcept {
        float next = 0.5f;
        for (float& source : run) {
            source = next;
            next += 1.0f;
        }
    });
    ::fixy::FixedArray<float, 20> const expected = ::fixy::build_fixed_array<float, 20>([](auto run) noexcept {
        float sum = 21.0f;
        for (float& element : run) {
            element = sum;
            sum += 3.0f;
        }
    });
    ::fixy::FixedArray<std::size_t, 1> const sizes{20};
    ::fixy::FixedArray<std::size_t, 1> const dense{1};
    ::fixy::FixedArray<std::size_t, 1> const step_two{2};
    ::fixy::StridedView<float const, 1> const first =
        ::fixy::mint_strided_view(sources.view(), sizes.view(), dense.view(), 0).expect("the input fits");
    ::fixy::StridedView<float const, 1> const second =
        ::fixy::mint_strided_view(sources.view(), sizes.view(), step_two.view(), 20).expect("the input fits");
    auto const [is_written, values] = ::fixy::build_fixed_array<float, 20>([&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), dense.view(), 0).expect("dense");
        return ::foundation::core::write_pointwise(out, Add{}, first, second).is_ok();
    });
    CRUCIBLE_TEST_ASSERT(
        test,
        is_written && same_bits(::fixy::View<float const>{values.view()}, ::fixy::View<float const>{expected.view()}));
    pass(test, "  static_rank_one: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    branded_inputs(test);
    shape_mismatch(test);
    fill_and_edges(test);
    static_rank_one(test);
    ::test_pointwise::test_the_float_cases(test);
    ::test_pointwise::test_the_float_multiply_add(test);
    ::test_pointwise::test_the_double_cases(test);
    ::test_pointwise::test_the_double_multiply_add(test);
    ::test_pointwise::test_the_integer_cases(test);
    ::test_pointwise::test_the_uint64_multiply_add(test);
    ::test_pointwise::test_the_rows(test);
    pass(test, "test_pointwise: all tests passed\n");
    return 0;
}
