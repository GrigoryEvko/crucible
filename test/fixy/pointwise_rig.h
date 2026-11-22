#pragma once

// The rig of test_pointwise: the drawn cases, the oracle walk(), the
// properties and the directed cases of THE NAN RULE of
// foundation/core/Pointwise.h.
//
// The compile of an op through write_pointwise gives one lane plan for each
// mix of the loops of its inputs, with three loops in place of four for each
// input past the second, so each element type and op costs much machine code.
// Thus the tests compile in more than one unit.  Each unit
// pointwise_unit_*.cpp of test_pointwise gives one function at the end of
// this header, which runs the directed cases and the properties of some
// element types and ops.  Each walk of one element type and op under test is
// in one unit only, so no two units compile the same walk.  The unit
// test_pointwise.cpp holds main and the unit tests.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Property.h>
#include <foundation/core/Pointwise.h>
#include <foundation/core/StridedRows.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace test_pointwise {

using ::fixy::check::Draw;
using ::fixy::check::pass;
using ::fixy::check::property;

// The largest rank of a drawn shape.
inline constexpr std::size_t kRank = 4;
// The largest count of the elements of a drawn shape.
inline constexpr std::size_t kMaxCount = 1536;
// The count of the runs of each property.
inline constexpr std::uint32_t kRuns = 300;
// The floor of each outcome of a property.
inline constexpr std::size_t kOutcomeFloor = 20;

using Lanes = ::fixy::FixedArray<std::size_t, kRank>;

// The layout of a view in the run of a property.
struct Layout {
    std::size_t rank = 0;
    Lanes sizes{};
    Lanes strides{};
    std::size_t offset = 0;
};

// The place of an input against the output.
enum class Place : std::uint8_t {
    Apart,
    InPlace,
    Shared
};

// The counts of the places of the runs of one property.
struct Outcomes {
    std::size_t apart = 0;
    std::size_t in_place = 0;
    std::size_t shared = 0;
};

inline void set_lane(Lanes& lanes, std::size_t axis, std::size_t value) noexcept {
    lanes.with_element(::fixy::Index<kRank>::check(axis).expect("an axis is below the lane count"),
                       [value](std::size_t& held) noexcept { held = value; });
}
[[nodiscard]] inline std::size_t lane(Lanes const& lanes, std::size_t axis) noexcept {
    return *lanes.get(axis).expect("an axis is below the lane count");
}

[[nodiscard]] inline std::size_t count_of(Layout const& layout) noexcept {
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        count *= lane(layout.sizes, axis);
    }
    return count;
}

// The count of the elements of the run that the view reaches from its
// offset: zero for a view with no element.
[[nodiscard]] inline std::size_t reach_of(Layout const& layout) noexcept {
    if (count_of(layout) == 0) return 0;
    std::size_t span = 0;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        span += (lane(layout.sizes, axis) - 1) * lane(layout.strides, axis);
    }
    return span + 1;
}

[[nodiscard]] inline ::fixy::View<std::size_t const> sizes_of(Layout const& layout) noexcept {
    return layout.sizes.view().window(0, layout.rank).expect("the rank is at most the lane count");
}
[[nodiscard]] inline ::fixy::View<std::size_t const> strides_of(Layout const& layout) noexcept {
    return layout.strides.view().window(0, layout.rank).expect("the rank is at most the lane count");
}

// A shape of at most kMaxCount elements.  The sizes come near the lane
// counts 8 and 16, and one shape in sixteen has no element.
[[nodiscard]] inline Layout draw_shape(Draw& draw) noexcept {
    ::fixy::FixedArray<std::size_t, 12> const sizes{1, 2, 3, 5, 7, 8, 9, 15, 16, 17, 33, 47};
    Layout layout{};
    layout.rank = draw.integer<std::size_t>(0, kRank);
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        std::size_t size = *sizes.get(draw.integer<std::size_t>(0, 11)).expect("twelve sizes");
        if (count * size > kMaxCount) size = 1;
        count *= size;
        set_lane(layout.sizes, axis, size);
    }
    if (layout.rank > 0 && draw.integer<std::size_t>(0, 15) == 0) {
        set_lane(layout.sizes, draw.integer<std::size_t>(0, layout.rank - 1), 0);
    }
    return layout;
}

// The axes of the shape in a drawn order.
[[nodiscard]] inline Lanes draw_order(Draw& draw, std::size_t rank) noexcept {
    Lanes order{};
    for (std::size_t axis = 0; axis < rank; ++axis) {
        set_lane(order, axis, axis);
    }
    for (std::size_t axis = rank; axis > 1; --axis) {
        std::size_t const other = draw.integer<std::size_t>(0, axis - 1);
        std::size_t const kept = lane(order, axis - 1);
        set_lane(order, axis - 1, lane(order, other));
        set_lane(order, other, kept);
    }
    return order;
}

// Strides with no overlap: the axes in a drawn order, the last axis of the
// order inside, each stride past the span of the axes inside it, and a step
// of one or two on the inner axis.
inline void draw_unique_strides(Draw& draw, Layout& layout) noexcept {
    Lanes const order = draw_order(draw, layout.rank);
    std::size_t stride = draw.integer<std::size_t>(0, 3) == 0 ? 2 : 1;
    for (std::size_t position = layout.rank; position-- > 0;) {
        std::size_t const axis = lane(order, position);
        set_lane(layout.strides, axis, stride);
        std::size_t const size = lane(layout.sizes, axis);
        stride = stride * (size == 0 ? 1 : size) + (draw.integer<std::size_t>(0, 3) == 0 ? 1 : 0);
    }
}

// The strides of an input: the strides of the row-major order, strides with
// no overlap in a drawn order, strides of zero on some axes, a stride from a
// small set on each axis, or the strides of the column-major order, which a
// row-major output reads with the tile path.
inline void draw_input_strides(Draw& draw, Layout& layout) noexcept {
    std::size_t const kind = draw.integer<std::size_t>(0, 4);
    if (kind == 1) {
        draw_unique_strides(draw, layout);
        return;
    }
    if (kind == 4) {
        std::size_t stride = 1;
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            set_lane(layout.strides, axis, stride);
            std::size_t const size = lane(layout.sizes, axis);
            stride *= size == 0 ? 1 : size;
        }
        return;
    }
    if (kind == 3) {
        ::fixy::FixedArray<std::size_t, 6> const steps{0, 1, 2, 3, 4, 9};
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            set_lane(layout.strides, axis, *steps.get(draw.integer<std::size_t>(0, 5)).expect("six steps"));
        }
        return;
    }
    std::size_t stride = 1;
    for (std::size_t axis = layout.rank; axis-- > 0;) {
        set_lane(layout.strides, axis, stride);
        std::size_t const size = lane(layout.sizes, axis);
        stride *= size == 0 ? 1 : size;
    }
    if (kind == 2) {
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            if (draw.boolean()) set_lane(layout.strides, axis, 0);
        }
    }
}

// The bits of a floating element.
template <class E>
using Bits = std::conditional_t<sizeof(E) == 4, std::uint32_t, std::uint64_t>;

// The count of the special values of a floating element.
inline constexpr std::size_t kSpecials = 12;

// The special values of a floating element, as bits: zero of each sign,
// infinity of each sign, a quiet NaN, a NaN with a payload, a negative NaN,
// a signaling NaN of each sign, the smallest and the largest denormal, and
// the largest finite number.
template <class E>
[[nodiscard]] Bits<E> special_bits(std::size_t index) noexcept {
    if constexpr (sizeof(E) == 4) {
        ::fixy::FixedArray<std::uint32_t, kSpecials> const specials{0x00000000u, 0x80000000u, 0x7f800000u, 0xff800000u,
                                                                    0x7fc00000u, 0x7fc12345u, 0xffc00001u, 0x7f800001u,
                                                                    0xffa12345u, 0x00000001u, 0x007fffffu, 0x7f7fffffu};
        return *specials.get(index).expect("an index below the count of the specials");
    } else {
        ::fixy::FixedArray<std::uint64_t, kSpecials> const specials{
            0x0000000000000000u, 0x8000000000000000u, 0x7ff0000000000000u, 0xfff0000000000000u,
            0x7ff8000000000000u, 0x7ff8000000012345u, 0xfff8000000000001u, 0x7ff0000000000001u,
            0xfff4000000012345u, 0x0000000000000001u, 0x000fffffffffffffu, 0x7fefffffffffffffu};
        return *specials.get(index).expect("an index below the count of the specials");
    }
}

// An element: for a floating element, any bit pattern, a special value or a
// small number.
template <class E>
[[nodiscard]] E draw_value(Draw& draw) noexcept {
    if constexpr (std::is_floating_point_v<E>) {
        using Word = Bits<E>;
        std::size_t const kind = draw.integer<std::size_t>(0, 3);
        if (kind == 0) return ::fixy::bit_cast<E>(draw.integer<Word>(0, ::fixy::max_of<Word>));
        if (kind == 1) return ::fixy::bit_cast<E>(special_bits<E>(draw.integer<std::size_t>(0, kSpecials - 1)));
        return static_cast<E>(draw.integer<std::int32_t>(-4000, 4000)) * static_cast<E>(0.125);
    } else if constexpr (std::is_unsigned_v<E>) {
        return draw.integer<E>(0, ::fixy::max_of<E>);
    } else {
        // The negation of such an element does not overflow.
        return draw.integer<E>(-(E{1} << 20), E{1} << 20);
    }
}

// True when the bits of a floating element are a NaN: each bit of the
// exponent set, and a fraction that is not zero.
template <class E>
[[nodiscard]] bool is_nan_bits(Bits<E> bits) noexcept {
    if constexpr (sizeof(E) == 4) {
        return (bits & 0x7fffffffu) > 0x7f800000u;
    } else {
        return (bits & 0x7fffffffffffffffu) > 0x7ff0000000000000u;
    }
}

// The bits of the canonical NaN of a floating element.
template <class E>
[[nodiscard]] constexpr Bits<E> canonical_nan_bits() noexcept {
    if constexpr (sizeof(E) == 4) {
        return 0x7fc00000u;
    } else {
        return 0x7ff8000000000000u;
    }
}

// True when the two runs hold the same bits at each position.  A NaN
// compares by its bits too.  O(n).
template <class E>
[[nodiscard]] bool same_bits(::fixy::View<E const> left, ::fixy::View<E const> right) noexcept {
    if (left.size() != right.size()) return false;
    auto right_cursor = right.begin();
    for (E const& element : left) {
        if constexpr (std::is_floating_point_v<E>) {
            if (::fixy::bit_cast<Bits<E>>(element) != ::fixy::bit_cast<Bits<E>>(*right_cursor)) return false;
        } else {
            if (element != *right_cursor) return false;
        }
        ++right_cursor;
    }
    return true;
}

// True when the op states that it keeps the bits of its outputs.
template <class Op>
[[nodiscard]] consteval bool keeps_nan_bits() noexcept {
    if constexpr (requires { Op::nan_rule; }) {
        return Op::nan_rule == ::foundation::core::NanRule::KeepBits;
    } else {
        return false;
    }
}

// The output of the oracle for the value of the op: by THE NAN RULE, the
// canonical NaN for each NaN of a floating element, unless the op keeps its
// bits.  The test reads the bits with is_nan_bits, and not with the header.
template <class E, class Op, class Value>
[[nodiscard]] E oracle_output(Value const& value) noexcept {
    E const converted = static_cast<E>(value);
    if constexpr (std::is_floating_point_v<E> && !keeps_nan_bits<Op>()) {
        if (is_nan_bits<E>(::fixy::bit_cast<Bits<E>>(converted))) return ::fixy::bit_cast<E>(canonical_nan_bits<E>());
    }
    return converted;
}

// The ops.  Each one is a generic call, so it is a lane op for the elements of
// the lane path, and a scalar op for each other element.
struct Add final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return static_cast<X>(left + right);
    }
};
struct MultiplyAdd final {
    template <class X>
    constexpr X operator()(X first, X second, X third) const noexcept {
        return static_cast<X>(first * second + third);
    }
};
// The divisor is right * right + 1, which is never zero, so no step divides
// by zero: full UBSan refuses a float division by zero in the scalar step of
// the oracle.  A shared input reads outputs of earlier steps, and a quotient
// can underflow to zero, so a draw with no zero would not be sufficient.
struct Divide final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return left / (right * right + X{1});
    }
};
// The negate of a float or a double changes only the sign bit, but it takes
// the rule of each op, so a NaN output is the canonical NaN.
struct Negate final {
    template <class X>
    constexpr X operator()(X value) const noexcept {
        return -value;
    }
};
// The two ops that keep the bits of their outputs, also of a signaling NaN.
struct Copy final {
    static constexpr ::foundation::core::NanRule nan_rule = ::foundation::core::NanRule::KeepBits;

    template <class X>
    constexpr X operator()(X value) const noexcept {
        return value;
    }
};
struct KeptNegate final {
    static constexpr ::foundation::core::NanRule nan_rule = ::foundation::core::NanRule::KeepBits;

    template <class X>
    constexpr X operator()(X value) const noexcept {
        return -value;
    }
};
// An op with a call on elements and no call on lanes: it takes the scalar
// path.
struct ScalarAdd final {
    template <class X>
        requires std::is_floating_point_v<X>
    constexpr X operator()(X left, X right) const noexcept {
        return left + right;
    }
};

// The layouts of one run of a property: the output, then each input, in one
// run of total elements.
template <std::size_t Inputs>
struct Case {
    Layout out{};
    ::fixy::FixedArray<Layout, Inputs> inputs{};
    std::size_t total = 0;
};

// The layouts of one run: the output and each input in a region of its own,
// in a drawn order, and each input in place or over the output with some
// chance.  The last region ends the run.
template <std::size_t Inputs>
[[nodiscard]] Case<Inputs> draw_case(Draw& draw, Outcomes& outcomes) noexcept {
    Case<Inputs> drawn{};
    Layout const shape = draw_shape(draw);
    drawn.out = shape;
    draw_unique_strides(draw, drawn.out);
    ::fixy::FixedArray<Place, Inputs> places{};
    for (std::size_t input = 0; input < Inputs; ++input) {
        auto const slot = ::fixy::Index<Inputs>::check(input).expect("an input is below the count");
        Layout layout = shape;
        std::size_t const place = draw.integer<std::size_t>(0, 7);
        Place const chosen = place == 0 ? Place::InPlace : place == 1 ? Place::Shared : Place::Apart;
        places.with_element(slot, [chosen](Place& held) noexcept { held = chosen; });
        if (chosen == Place::InPlace) {
            layout.strides = drawn.out.strides;
        } else {
            draw_input_strides(draw, layout);
        }
        drawn.inputs.with_element(slot, [&layout](Layout& held) noexcept { held = layout; });
    }
    // The regions of the output and of the inputs apart, in a drawn order.
    Lanes const order = draw_order(draw, Inputs + 1);
    std::size_t position = 0;
    for (std::size_t slot = 0; slot <= Inputs; ++slot) {
        std::size_t const view = lane(order, slot);
        if (view == 0) {
            drawn.out.offset = position;
            position += reach_of(drawn.out);
            continue;
        }
        auto const input = ::fixy::Index<Inputs>::check(view - 1).expect("an input is below the count");
        if (*places.get(view - 1).expect("an input is below the count") != Place::Apart) continue;
        position += drawn.inputs.with_element(input, [position](Layout& layout) noexcept {
            layout.offset = position;
            return reach_of(layout);
        });
    }
    drawn.total = position;
    bool has_shared = false;
    bool has_in_place = false;
    for (std::size_t input = 0; input < Inputs; ++input) {
        auto const slot = ::fixy::Index<Inputs>::check(input).expect("an input is below the count");
        Layout layout = drawn.inputs.at(slot);
        Place const chosen = *places.get(input).expect("an input is below the count");
        if (chosen == Place::InPlace) {
            layout.offset = drawn.out.offset;
            has_in_place = true;
        } else if (chosen == Place::Shared) {
            std::size_t const reach = reach_of(drawn.out);
            layout.offset = drawn.out.offset + (reach == 0 ? 0 : draw.integer<std::size_t>(0, reach - 1));
            std::size_t const end = layout.offset + reach_of(layout);
            drawn.total = end > drawn.total ? end : drawn.total;
            has_shared = has_shared || (reach != 0 && reach_of(layout) != 0);
        }
        drawn.inputs.with_element(slot, [&layout](Layout& held) noexcept { held = layout; });
    }
    ++(has_shared ? outcomes.shared : has_in_place ? outcomes.in_place : outcomes.apart);
    return drawn;
}

// The run of write_pointwise and the run of the oracle for the views of one
// case, from one run of values.
template <class E>
struct Written {
    ::fixy::HeapArray<E> run;
    ::fixy::HeapArray<E> expected;
    bool is_written = false;
};

// The walk under test: write_pointwise, or the walk of rows of
// foundation/core/StridedRows.h with a body that calls the op for each step.
enum class Walker : std::uint8_t {
    Pointwise,
    Rows,
};

// The walk of rows of the views with the op: the counted loop of each row, and
// the NaN rule of the oracle in the body, because the walk of rows applies none.
template <class E, class Op, class Out, class... Inputs>
[[nodiscard]] bool write_rows(Out& out, Op op, Inputs const&... inputs) noexcept {
    return ::foundation::core::mint_strided_rows(
               out,
               [op](auto const& row, auto out_view, auto const&... input_views) noexcept {
                   for (auto const step : row.steps()) {
                       out_view.at(row.offset(out_view), step) =
                           oracle_output<E, Op>(op(input_views.at(row.offset(input_views), step)...));
                   }
               },
               inputs...)
        .is_ok();
}

// Writes the views of a case with the walk under test over run, and with
// walk() and the op on scalars over a copy of run.
template <class E, Walker Under = Walker::Pointwise, class Op, std::size_t... Is>
[[nodiscard]] Written<E> write_case(Case<sizeof...(Is)> const& drawn, Op op, ::fixy::HeapArray<E> run,
                                    std::index_sequence<Is...> /*inputs*/) noexcept {
    auto const test = ::foundation::effects::testing::test();
    ::fixy::HeapArray<E> expected = ::fixy::mint_heap_array_copy<E>(test.alloc, ::fixy::View<E const>{run.view()});

    auto const input_of = [&drawn](::fixy::View<E const> whole, std::size_t input) noexcept {
        Layout const& layout = *drawn.inputs.get(input).expect("an input is below the count");
        return ::fixy::mint_strided_view(whole, sizes_of(layout), strides_of(layout), layout.offset)
            .expect("an input fits the run");
    };

    // The oracle: walk() in the stated order, with the op on scalars and THE
    // NAN RULE in each step, so a shared input reads the canonical NaN of an
    // earlier step.  The walk writes the copy in a frame of the copy.
    auto [is_walked, walked] = ::fixy::with_mut(std::move(expected), [&drawn, &input_of, op](auto frame_run) noexcept {
        ::fixy::View<E> const expected_run = frame_run.view();
        ::fixy::StridedView<E> const expected_out =
            ::fixy::mint_strided_view(expected_run, sizes_of(drawn.out), strides_of(drawn.out), drawn.out.offset)
                .expect("the output fits the run");
        return ::fixy::walk(
                   [op](E& target, auto const&... values) noexcept { target = oracle_output<E, Op>(op(values...)); },
                   expected_out, input_of(::fixy::View<E const>{expected_run}, Is)...)
            .is_ok();
    });

    // The walk under test, in a frame of the run.  The inputs are plain views
    // that the test makes before it mints the unique view of the output.
    auto [is_written, written] = ::fixy::with_mut(std::move(run), [&](auto frame_run) noexcept {
        ::fixy::View<E const> const whole{frame_run.view()};
        auto out = ::fixy::mint_strided_view_mut(std::move(frame_run), sizes_of(drawn.out), strides_of(drawn.out),
                                                 drawn.out.offset)
                       .expect("the output has no overlap");
        if constexpr (Under == Walker::Rows) {
            return write_rows<E>(out, op, input_of(whole, Is)...);
        } else {
            return ::foundation::core::write_pointwise(out, op, input_of(whole, Is)...).is_ok();
        }
    });
    return Written<E>{std::move(written), std::move(walked), is_walked && is_written};
}

// True when the walk under test over the views of a drawn case writes the
// bits of walk() with the op on scalars.
template <class E, Walker Under, class Op, std::size_t... Is>
[[nodiscard]] bool agrees_with_walk(Draw& draw, Op op, Outcomes& outcomes, std::index_sequence<Is...> inputs) noexcept {
    auto const test = ::foundation::effects::testing::test();
    Case<sizeof...(Is)> const drawn = draw_case<sizeof...(Is)>(draw, outcomes);
    ::fixy::HeapArray<E> run = ::fixy::mint_heap_array_with<E>(
        test.alloc, drawn.total, [&draw](std::size_t) noexcept { return draw_value<E>(draw); });
    Written<E> const written = write_case<E, Under>(drawn, op, std::move(run), inputs);
    return written.is_written
        && same_bits(::fixy::View<E const>{written.run.view()}, ::fixy::View<E const>{written.expected.view()});
}

// The property of one element type and one op of Inputs inputs.
template <class E, std::size_t Inputs, class Op, Walker Under = Walker::Pointwise>
void pointwise_agrees_with_walk(::fixy::TestRunnerCtx const& test, ::fixy::TextView name, std::uint64_t seed,
                                Op op) noexcept {
    Outcomes outcomes{};
    property(test, seed, kRuns, [&outcomes, op](Draw& draw) noexcept {
        return agrees_with_walk<E, Under>(draw, op, outcomes, std::make_index_sequence<Inputs>{});
    });
    pass(test, "  {}: {} runs apart, {} in place, {} shared\n", name, outcomes.apart, outcomes.in_place,
         outcomes.shared);
    CRUCIBLE_TEST_ASSERT(test, outcomes.apart >= kOutcomeFloor && outcomes.shared >= kOutcomeFloor);
    if (Inputs > 0) CRUCIBLE_TEST_ASSERT(test, outcomes.in_place >= kOutcomeFloor);
}

// ── The directed cases of THE NAN RULE ──────────────────────────────────

// The layout of one axis of count elements with the stride, from offset.
[[nodiscard]] inline Layout line_of(std::size_t count, std::size_t stride, std::size_t offset) noexcept {
    Layout layout{};
    layout.rank = 1;
    set_lane(layout.sizes, 0, count);
    set_lane(layout.strides, 0, stride);
    layout.offset = offset;
    return layout;
}

// The layout of rows x columns elements with the two strides, from offset.
[[nodiscard]] inline Layout plane_of(std::size_t rows, std::size_t columns, std::size_t row_stride,
                                     std::size_t column_stride, std::size_t offset) noexcept {
    Layout layout{};
    layout.rank = 2;
    set_lane(layout.sizes, 0, rows);
    set_lane(layout.sizes, 1, columns);
    set_lane(layout.strides, 0, row_stride);
    set_lane(layout.strides, 1, column_stride);
    layout.offset = offset;
    return layout;
}

// The layout of rank zero: one element at offset.
[[nodiscard]] inline Layout point_of(std::size_t offset) noexcept {
    Layout layout{};
    layout.offset = offset;
    return layout;
}

// The count of the NaNs of a directed run.
inline constexpr std::size_t kDirectedNans = 6;

// The NaN at a position of a directed run: payloads and signs of each kind,
// signaling NaNs too, and no canonical NaN.
template <class E>
[[nodiscard]] E nan_at(std::size_t position) noexcept {
    std::size_t const index = position % kDirectedNans;
    if constexpr (sizeof(E) == 4) {
        ::fixy::FixedArray<std::uint32_t, kDirectedNans> const nans{0x7fc12345u, 0xffc00001u, 0x7f800001u,
                                                                    0xffa12345u, 0x7fffffffu, 0xff800001u};
        return ::fixy::bit_cast<E>(*nans.get(index).expect("an index below the count of the NaNs"));
    } else {
        ::fixy::FixedArray<std::uint64_t, kDirectedNans> const nans{0x7ff8000000012345u, 0xfff8000000000001u,
                                                                    0x7ff0000000000001u, 0xfff4000000012345u,
                                                                    0x7fffffffffffffffu, 0xfff0000000000001u};
        return ::fixy::bit_cast<E>(*nans.get(index).expect("an index below the count of the NaNs"));
    }
}

// The count of the elements of a run with the bits of the canonical NaN.
// O(n).
template <class E>
[[nodiscard]] std::size_t canonical_count(::fixy::View<E const> run) noexcept {
    std::size_t count = 0;
    for (E const& element : run) {
        if (::fixy::bit_cast<Bits<E>>(element) == canonical_nan_bits<E>()) ++count;
    }
    return count;
}

// True when write_pointwise writes the bits of the oracle for a directed
// case over the run of fill, and the run then holds canonical elements with
// the bits of the canonical NaN.
template <class E, std::size_t Inputs, class Op, class Fill>
[[nodiscard]] bool writes_directed(Case<Inputs> const& directed, Op op, Fill fill, std::size_t canonical) noexcept {
    auto const test = ::foundation::effects::testing::test();
    ::fixy::HeapArray<E> run = ::fixy::mint_heap_array_with<E>(test.alloc, directed.total, fill);
    Written<E> const written = write_case<E>(directed, op, std::move(run), std::make_index_sequence<Inputs>{});
    ::fixy::View<E const> const result{written.run.view()};
    return written.is_written && same_bits(result, ::fixy::View<E const>{written.expected.view()})
        && canonical_count(result) == canonical;
}

// Each path of the walk gives the canonical NaN for each NaN output of an
// arithmetic op: the whole blocks and the masked block of a dense row, the
// loops StepTwo, General and Splat, a General output, an input in place, a
// shared input in the stated order, the tile path, rank zero, and the scalar
// path.  An add of the two infinities gives a NaN with no NaN input.  An op
// that keeps its bits keeps each NaN, also a signaling NaN, on each path.
template <class E>
void nan_rule_on_each_path(::fixy::TestRunnerCtx const& test, ::fixy::TextView name) noexcept {
    auto const nans = [](std::size_t position) noexcept { return nan_at<E>(position); };
    // A row of 37 elements has two or four whole blocks and a masked block.
    constexpr std::size_t row = 37;
    ::fixy::FixedArray<Case<2>, 9> const cases{
        Case<2>{line_of(row, 1, 0), {line_of(row, 1, 37), line_of(row, 1, 74)}, 111},
        Case<2>{line_of(row, 1, 0), {line_of(row, 2, 37), line_of(row, 1, 110)}, 147},
        Case<2>{line_of(row, 1, 0), {line_of(row, 3, 37), line_of(row, 1, 146)}, 183},
        Case<2>{line_of(row, 1, 0), {line_of(row, 1, 37), line_of(row, 0, 74)}, 75},
        Case<2>{line_of(row, 2, 0), {line_of(row, 1, 73), line_of(row, 1, 110)}, 147},
        Case<2>{line_of(row, 1, 0), {line_of(row, 1, 0), line_of(row, 1, 37)}, 74},
        Case<2>{line_of(row, 1, 0), {line_of(row, 1, 1), line_of(row, 1, 38)}, 75},
        Case<2>{plane_of(19, 23, 23, 1, 0), {plane_of(19, 23, 1, 19, 437), plane_of(19, 23, 23, 1, 874)}, 1311},
        Case<2>{point_of(0), {point_of(1), point_of(2)}, 3},
    };
    for (Case<2> const& directed : cases) {
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, Add{}, nans, count_of(directed.out)));
    }
    // The scalar path: a dense row with restrict pointers, an input in
    // place, a step of three, and a shared input.
    for (std::size_t const index : ::fixy::FixedArray<std::size_t, 4>{0, 2, 5, 6}) {
        Case<2> const& directed = *cases.get(index).expect("an index below the count of the cases");
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, ScalarAdd{}, nans, count_of(directed.out)));
    }
    // inf + -inf and -inf + inf.
    Case<2> const& dense = *cases.get(0).expect("the first case");
    auto const infinities = [](std::size_t position) noexcept {
        return position < 37 ? E{0} : position < 74 ? ::fixy::infinity_of<E> : -::fixy::infinity_of<E>;
    };
    auto const reversed = [](std::size_t position) noexcept {
        return position < 37 ? E{0} : position < 74 ? -::fixy::infinity_of<E> : ::fixy::infinity_of<E>;
    };
    CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(dense, Add{}, infinities, row));
    CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(dense, Add{}, reversed, row));
    // The ops that keep their bits: no canonical NaN appears.
    using One = ::fixy::FixedArray<Layout, 1>;
    ::fixy::FixedArray<Case<1>, 6> const copies{
        Case<1>{line_of(row, 1, 0), One{line_of(row, 1, 37)}, 74},
        Case<1>{line_of(row, 1, 0), One{line_of(row, 3, 37)}, 146},
        Case<1>{line_of(row, 2, 0), One{line_of(row, 1, 73)}, 110},
        Case<1>{line_of(row, 1, 0), One{line_of(row, 1, 1)}, 38},
        Case<1>{plane_of(19, 23, 23, 1, 0), One{plane_of(19, 23, 1, 19, 437)}, 874},
        Case<1>{point_of(0), One{point_of(1)}, 2},
    };
    for (Case<1> const& directed : copies) {
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, Copy{}, nans, 0));
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, KeptNegate{}, nans, 0));
    }
    pass(test, "  nan_rule_on_each_path {}: PASSED\n", name);
}

// The tile path over 32 x 48 elements, which is two or four whole tiles on
// each axis for each lane count: a transposed input with a second input of
// each class of a tile, Dense, Splat, General and Transposed, on finite
// values.  The tiles of the edges take a shape of 19 x 23 above.
template <class E>
void tile_classes(::fixy::TestRunnerCtx const& test, ::fixy::TextView name) noexcept {
    auto const values = [](std::size_t position) noexcept {
        return static_cast<E>(position % 1021) * static_cast<E>(0.25) + static_cast<E>(0.5);
    };
    Layout const out = plane_of(32, 48, 48, 1, 0);
    Layout const transposed = plane_of(32, 48, 1, 32, 1536);
    ::fixy::FixedArray<Case<2>, 4> const cases{
        Case<2>{out, {transposed, plane_of(32, 48, 48, 1, 3072)}, 4608},
        Case<2>{out, {transposed, plane_of(32, 48, 1, 0, 3072)}, 3104},
        Case<2>{out, {transposed, plane_of(32, 48, 200, 3, 3072)}, 9414},
        Case<2>{out, {transposed, plane_of(32, 48, 1, 32, 3072)}, 4608},
    };
    for (Case<2> const& directed : cases) {
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, Add{}, values, 0));
        CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(directed, Divide{}, values, 0));
    }
    pass(test, "  tile_classes {}: PASSED\n", name);
}

// A step-two input whose row of 32 elements, a whole count of blocks for each
// lane count, ends the run: the blocks before the last one read the element
// after their last element, and the last block reads no element past the run,
// which the address sanitizer of a Debug build finds.
template <class E>
void step_two_ends_the_run(::fixy::TestRunnerCtx const& test, ::fixy::TextView name) noexcept {
    auto const values = [](std::size_t position) noexcept {
        return static_cast<E>(position % 97) * static_cast<E>(0.5) + static_cast<E>(1);
    };
    Case<2> const ending{line_of(32, 1, 0), {line_of(32, 1, 32), line_of(32, 2, 64)}, 127};
    CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(ending, Add{}, values, 0));
    Case<2> const two_rows{plane_of(2, 32, 32, 1, 0), {plane_of(2, 32, 32, 1, 64), plane_of(2, 32, 64, 2, 128)}, 255};
    CRUCIBLE_TEST_ASSERT(test, writes_directed<E>(two_rows, Add{}, values, 0));
    pass(test, "  step_two_ends_the_run {}: PASSED\n", name);
}

// The count of the loops of an input of the lane path: Dense, Splat, StepTwo
// and General.
inline constexpr std::size_t kLaneLoops = 4;

// Each mix of the loops of the lane path over the three inputs of a dense
// output writes the bits of the oracle: one of the four loops on each input,
// so 64 mixes.  The third input takes Dense and Splat in the lane plan, and
// its steps two and three take the loop Switched.  The output and each input
// are planes of 3 x 37 elements in regions of their own, so each mix takes
// the lane plan, with no tile and no stated order.  A Splat input reads one
// element for each row, and each row reads a different element.  The region
// of the third input ends the run, so a read past that view is a read past
// the allocation.  A run of a floating element holds NaNs among finite
// values.  O(64) walks.
template <class E, std::size_t Inputs, class Op>
void lane_loop_mixes(::fixy::TestRunnerCtx const& test, ::fixy::TextView name, Op op) noexcept {
    static_assert(Inputs == 3, "the mixes cover an op of three inputs");
    constexpr std::size_t rows = 3;
    constexpr std::size_t columns = 37;
    ::fixy::FixedArray<std::size_t, kLaneLoops> const steps{1, 0, 2, 3};
    auto const fill = [](std::size_t position) noexcept {
        if constexpr (std::is_floating_point_v<E>) {
            return position % 7 == 3 ? nan_at<E>(position)
                                     : static_cast<E>(position % 1021) * static_cast<E>(0.25) + static_cast<E>(0.5);
        } else {
            return static_cast<E>(position % 1021 * 7 + 3);
        }
    };
    std::size_t mixes = 1;
    for (std::size_t input = 0; input < Inputs; ++input) {
        mixes *= kLaneLoops;
    }
    for (std::size_t mix = 0; mix < mixes; ++mix) {
        Case<Inputs> directed{};
        directed.out = plane_of(rows, columns, columns, 1, 0);
        std::size_t loops = mix;
        for (std::size_t input = 0; input < Inputs; ++input) {
            std::size_t const step = *steps.get(loops % kLaneLoops).expect("a loop below the count of the loops");
            loops /= kLaneLoops;
            Layout const layout =
                step == 0 ? plane_of(rows, columns, 1, 0, 0) : plane_of(rows, columns, columns * step, step, 0);
            directed.inputs.with_element(::fixy::Index<Inputs>::check(input).expect("an input is below the count"),
                                         [&layout](Layout& held) noexcept { held = layout; });
        }
        std::size_t position = reach_of(directed.out);
        for (std::size_t input = 0; input < Inputs; ++input) {
            position +=
                directed.inputs.with_element(::fixy::Index<Inputs>::check(input).expect("an input is below the count"),
                                             [position](Layout& layout) noexcept {
                                                 layout.offset = position;
                                                 return reach_of(layout);
                                             });
        }
        directed.total = position;
        ::fixy::HeapArray<E> run =
            ::fixy::mint_heap_array_with<E>(::foundation::effects::testing::test().alloc, directed.total, fill);
        Written<E> const written = write_case<E>(directed, op, std::move(run), std::make_index_sequence<Inputs>{});
        bool const is_agreed =
            written.is_written
            && same_bits(::fixy::View<E const>{written.run.view()}, ::fixy::View<E const>{written.expected.view()});
        CRUCIBLE_TEST_ASSERT(test, is_agreed);
    }
    pass(test, "  lane_loop_mixes {}: {} mixes of {} inputs: PASSED\n", name, mixes, Inputs);
}

// ── The cases of the units ──────────────────────────────────────────────
//
// Each function runs the directed cases and the properties of one unit.
// main calls the functions one after the other.

// The seed of each property.
inline constexpr std::uint64_t kSeed = 0x504f494e54574953u;

// pointwise_unit_float.cpp: the directed cases of floats, and the add, the
// divide, the negate, the copy and the scalar add of floats.
void test_the_float_cases(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_float_multiply_add.cpp: the multiply-add of floats.
void test_the_float_multiply_add(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_double.cpp: the directed cases of doubles, the negate that
// keeps its bits and the add of doubles.
void test_the_double_cases(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_double_multiply_add.cpp: the multiply-add of doubles.
void test_the_double_multiply_add(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_integer.cpp: the add of 4-byte integers, the negate of
// 8-byte integers and the add of 2-byte integers on the scalar path.
void test_the_integer_cases(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_uint64_multiply_add.cpp: the multiply-add of 8-byte
// unsigned integers.
void test_the_uint64_multiply_add(::fixy::TestRunnerCtx const& test) noexcept;
// pointwise_unit_rows.cpp: the walk of rows of each op.
void test_the_rows(::fixy::TestRunnerCtx const& test) noexcept;

}  // namespace test_pointwise
