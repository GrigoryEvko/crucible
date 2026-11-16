// Tests of the pointwise walk of the strided views
// (foundation/core/Pointwise.h).
//
// Each property draws a shape, a unique layout of the output and a layout of
// each input, and puts the views in one run.  An input can take a region of
// its own, the layout of the output (the in-place form), or a region that
// overlaps the output in a different place.  The run holds bit patterns with
// NaN, infinity, zero of each sign and denormal numbers.  The oracle is walk()
// of foundation/core/Strided.h with the op on scalars over a copy of the run,
// in the stated row-major order.  The oracle applies THE NAN RULE of
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

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Property.h>
#include <foundation/core/Pointwise.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::check::Draw;
using ::fixy::check::pass;
using ::fixy::check::property;

// The largest rank of a drawn shape.
constexpr std::size_t kRank = 4;
// The largest count of the elements of a drawn shape.
constexpr std::size_t kMaxCount = 1536;
// The seed of each property.
constexpr std::uint64_t kSeed = 0x504f494e54574953u;
// The count of the runs of each property.
constexpr std::uint32_t kRuns = 300;
// The floor of each outcome of a property.
constexpr std::size_t kOutcomeFloor = 20;

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

[[nodiscard]] std::size_t& lane(Lanes& lanes, std::size_t axis) noexcept {
    return *lanes.get(axis).expect("an axis is below the lane count");
}
[[nodiscard]] std::size_t lane(Lanes const& lanes, std::size_t axis) noexcept {
    return *lanes.get(axis).expect("an axis is below the lane count");
}

[[nodiscard]] std::size_t count_of(Layout const& layout) noexcept {
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        count *= lane(layout.sizes, axis);
    }
    return count;
}

// The count of the elements of the run that the view reaches from its
// offset: zero for a view with no element.
[[nodiscard]] std::size_t reach_of(Layout const& layout) noexcept {
    if (count_of(layout) == 0) return 0;
    std::size_t span = 0;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        span += (lane(layout.sizes, axis) - 1) * lane(layout.strides, axis);
    }
    return span + 1;
}

[[nodiscard]] ::fixy::View<std::size_t const> sizes_of(Layout const& layout) noexcept {
    return layout.sizes.view().window(0, layout.rank).expect("the rank is at most the lane count");
}
[[nodiscard]] ::fixy::View<std::size_t const> strides_of(Layout const& layout) noexcept {
    return layout.strides.view().window(0, layout.rank).expect("the rank is at most the lane count");
}

// A shape of at most kMaxCount elements.  The sizes come near the lane
// counts 8 and 16, and one shape in sixteen has no element.
[[nodiscard]] Layout draw_shape(Draw& draw) noexcept {
    ::fixy::FixedArray<std::size_t, 12> const sizes{1, 2, 3, 5, 7, 8, 9, 15, 16, 17, 33, 47};
    Layout layout{};
    layout.rank = draw.integer<std::size_t>(0, kRank);
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        std::size_t size = *sizes.get(draw.integer<std::size_t>(0, 11)).expect("twelve sizes");
        if (count * size > kMaxCount) size = 1;
        count *= size;
        lane(layout.sizes, axis) = size;
    }
    if (layout.rank > 0 && draw.integer<std::size_t>(0, 15) == 0) {
        lane(layout.sizes, draw.integer<std::size_t>(0, layout.rank - 1)) = 0;
    }
    return layout;
}

// The axes of the shape in a drawn order.
[[nodiscard]] Lanes draw_order(Draw& draw, std::size_t rank) noexcept {
    Lanes order{};
    for (std::size_t axis = 0; axis < rank; ++axis) {
        lane(order, axis) = axis;
    }
    for (std::size_t axis = rank; axis > 1; --axis) {
        std::size_t const other = draw.integer<std::size_t>(0, axis - 1);
        std::size_t const kept = lane(order, axis - 1);
        lane(order, axis - 1) = lane(order, other);
        lane(order, other) = kept;
    }
    return order;
}

// Strides with no overlap: the axes in a drawn order, the last axis of the
// order inside, each stride past the span of the axes inside it, and a step
// of one or two on the inner axis.
void draw_unique_strides(Draw& draw, Layout& layout) noexcept {
    Lanes const order = draw_order(draw, layout.rank);
    std::size_t stride = draw.integer<std::size_t>(0, 3) == 0 ? 2 : 1;
    for (std::size_t position = layout.rank; position-- > 0;) {
        std::size_t const axis = lane(order, position);
        lane(layout.strides, axis) = stride;
        std::size_t const size = lane(layout.sizes, axis);
        stride = stride * (size == 0 ? 1 : size) + (draw.integer<std::size_t>(0, 3) == 0 ? 1 : 0);
    }
}

// The strides of an input: the strides of the row-major order, strides with
// no overlap in a drawn order, strides of zero on some axes, a stride from a
// small set on each axis, or the strides of the column-major order, which a
// row-major output reads with the tile path.
void draw_input_strides(Draw& draw, Layout& layout) noexcept {
    std::size_t const kind = draw.integer<std::size_t>(0, 4);
    if (kind == 1) {
        draw_unique_strides(draw, layout);
        return;
    }
    if (kind == 4) {
        std::size_t stride = 1;
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            lane(layout.strides, axis) = stride;
            std::size_t const size = lane(layout.sizes, axis);
            stride *= size == 0 ? 1 : size;
        }
        return;
    }
    if (kind == 3) {
        ::fixy::FixedArray<std::size_t, 6> const steps{0, 1, 2, 3, 4, 9};
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            lane(layout.strides, axis) = *steps.get(draw.integer<std::size_t>(0, 5)).expect("six steps");
        }
        return;
    }
    std::size_t stride = 1;
    for (std::size_t axis = layout.rank; axis-- > 0;) {
        lane(layout.strides, axis) = stride;
        std::size_t const size = lane(layout.sizes, axis);
        stride *= size == 0 ? 1 : size;
    }
    if (kind == 2) {
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            if (draw.boolean()) lane(layout.strides, axis) = 0;
        }
    }
}

// The bits of a floating element.
template <class E>
using Bits = std::conditional_t<sizeof(E) == 4, std::uint32_t, std::uint64_t>;

// The count of the special values of a floating element.
constexpr std::size_t kSpecials = 12;

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
static_assert(::foundation::core::LaneOpOf<Divide, float, float, float>, "the divide property runs the lane path");
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
static_assert(!::foundation::core::LaneOpOf<ScalarAdd, float, float, float>, "ScalarAdd takes the scalar path");

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
        Layout& layout = *drawn.inputs.get(input).expect("an input is below the count");
        layout = shape;
        std::size_t const place = draw.integer<std::size_t>(0, 7);
        Place const chosen = place == 0 ? Place::InPlace : place == 1 ? Place::Shared : Place::Apart;
        *places.get(input).expect("an input is below the count") = chosen;
        if (chosen == Place::InPlace) {
            layout.strides = drawn.out.strides;
        } else {
            draw_input_strides(draw, layout);
        }
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
        Layout& layout = *drawn.inputs.get(view - 1).expect("an input is below the count");
        if (*places.get(view - 1).expect("an input is below the count") != Place::Apart) continue;
        layout.offset = position;
        position += reach_of(layout);
    }
    drawn.total = position;
    bool has_shared = false;
    bool has_in_place = false;
    for (std::size_t input = 0; input < Inputs; ++input) {
        Layout& layout = *drawn.inputs.get(input).expect("an input is below the count");
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

// Writes the views of a case with write_pointwise over run, and with walk()
// and the op on scalars over a copy of run.
template <class E, class Op, std::size_t... Is>
[[nodiscard]] Written<E> write_case(Case<sizeof...(Is)> const& drawn, Op op, ::fixy::HeapArray<E> run,
                                    std::index_sequence<Is...> /*inputs*/) noexcept {
    auto const test = ::foundation::effects::testing::test();
    ::fixy::HeapArray<E> expected = ::fixy::mint_heap_array_copy<E>(test.alloc, ::fixy::View<E const>{run.view()});

    // The oracle: walk() in the stated order, with the op on scalars and THE
    // NAN RULE in each step, so a shared input reads the canonical NaN of an
    // earlier step.
    ::fixy::View<E> const expected_run = expected.view();
    ::fixy::StridedView<E> const expected_out =
        ::fixy::mint_strided_view(expected_run, sizes_of(drawn.out), strides_of(drawn.out), drawn.out.offset)
            .expect("the output fits the run");
    auto const input_of = [&drawn](::fixy::View<E const> whole, std::size_t input) noexcept {
        Layout const& layout = *drawn.inputs.get(input).expect("an input is below the count");
        return ::fixy::mint_strided_view(whole, sizes_of(layout), strides_of(layout), layout.offset)
            .expect("an input fits the run");
    };
    bool const is_walked =
        ::fixy::walk([op](E& target, auto const&... values) noexcept { target = oracle_output<E, Op>(op(values...)); },
                     expected_out, input_of(::fixy::View<E const>{expected_run}, Is)...)
            .is_ok();

    // The walk under test, in a frame of the run.  The inputs are plain views
    // that the test makes before it mints the unique view of the output.
    auto [is_written, written] = ::fixy::with_mut(std::move(run), [&](auto frame_run) noexcept {
        ::fixy::View<E const> const whole{frame_run.view()};
        auto out = ::fixy::mint_strided_view_mut(std::move(frame_run), sizes_of(drawn.out), strides_of(drawn.out),
                                                 drawn.out.offset)
                       .expect("the output has no overlap");
        return ::foundation::core::write_pointwise(out, op, input_of(whole, Is)...).is_ok();
    });
    return Written<E>{std::move(written), std::move(expected), is_walked && is_written};
}

// True when write_pointwise over the views of a drawn case writes the bits
// of walk() with the op on scalars.
template <class E, class Op, std::size_t... Is>
[[nodiscard]] bool agrees_with_walk(Draw& draw, Op op, Outcomes& outcomes, std::index_sequence<Is...> inputs) noexcept {
    auto const test = ::foundation::effects::testing::test();
    Case<sizeof...(Is)> const drawn = draw_case<sizeof...(Is)>(draw, outcomes);
    ::fixy::HeapArray<E> run = ::fixy::mint_heap_array_with<E>(
        test.alloc, drawn.total, [&draw](std::size_t) noexcept { return draw_value<E>(draw); });
    Written<E> const written = write_case<E>(drawn, op, std::move(run), inputs);
    return written.is_written
        && same_bits(::fixy::View<E const>{written.run.view()}, ::fixy::View<E const>{written.expected.view()});
}

// The property of one element type and one op of Inputs inputs.
template <class E, std::size_t Inputs, class Op>
void pointwise_agrees_with_walk(::fixy::TestRunnerCtx const& test, ::fixy::TextView name, std::uint64_t seed,
                                Op op) noexcept {
    Outcomes outcomes{};
    property(test, seed, kRuns, [&outcomes, op](Draw& draw) noexcept {
        return agrees_with_walk<E>(draw, op, outcomes, std::make_index_sequence<Inputs>{});
    });
    pass(test, "  {}: {} runs apart, {} in place, {} shared\n", name, outcomes.apart, outcomes.in_place,
         outcomes.shared);
    CRUCIBLE_TEST_ASSERT(test, outcomes.apart >= kOutcomeFloor && outcomes.shared >= kOutcomeFloor);
    if (Inputs > 0) CRUCIBLE_TEST_ASSERT(test, outcomes.in_place >= kOutcomeFloor);
}

// ── The directed cases of THE NAN RULE ──────────────────────────────────

// The layout of one axis of count elements with the stride, from offset.
[[nodiscard]] Layout line_of(std::size_t count, std::size_t stride, std::size_t offset) noexcept {
    Layout layout{};
    layout.rank = 1;
    lane(layout.sizes, 0) = count;
    lane(layout.strides, 0) = stride;
    layout.offset = offset;
    return layout;
}

// The layout of rows x columns elements with the two strides, from offset.
[[nodiscard]] Layout plane_of(std::size_t rows, std::size_t columns, std::size_t row_stride, std::size_t column_stride,
                              std::size_t offset) noexcept {
    Layout layout{};
    layout.rank = 2;
    lane(layout.sizes, 0) = rows;
    lane(layout.sizes, 1) = columns;
    lane(layout.strides, 0) = row_stride;
    lane(layout.strides, 1) = column_stride;
    layout.offset = offset;
    return layout;
}

// The layout of rank zero: one element at offset.
[[nodiscard]] Layout point_of(std::size_t offset) noexcept {
    Layout layout{};
    layout.offset = offset;
    return layout;
}

// The count of the NaNs of a directed run.
constexpr std::size_t kDirectedNans = 6;

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

// ── Unit tests ──────────────────────────────────────────────────────────

// An input of another frame, with a brand of its own, takes the proof of the
// brands, and a dense add over two frames writes each sum.
void branded_inputs(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<float, 40> values{};
    ::fixy::FixedArray<float, 40> addends{};
    float next = 0.5f;
    for (float& value : values) {
        value = next;
        next += 1.0f;
    }
    for (float& addend : addends) {
        addend = 100.0f;
    }
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 20};
    ::fixy::FixedArray<std::size_t, 2> const strides{20, 1};
    auto [is_written, kept] = ::fixy::with_mut(std::move(values), [&](auto target_run) noexcept {
        auto [is_inner, inputs_kept] = ::fixy::with_mut(std::move(addends), [&](auto source_run) noexcept {
            auto const source = ::fixy::mint_strided_view_mut(std::move(source_run), sizes.view(), strides.view(), 0)
                                    .expect("a dense matrix has no overlap");
            auto out = ::fixy::mint_strided_view_mut(std::move(target_run), sizes.view(), strides.view(), 0)
                           .expect("a dense matrix has no overlap");
            ::fixy::StridedView<float const, 2> const in_place = out.view();
            return ::foundation::core::write_pointwise(out, Add{}, in_place, source).is_ok();
        });
        static_cast<void>(inputs_kept);
        return is_inner;
    });
    bool is_summed = true;
    float expected = 100.5f;
    for (float const value : kept) {
        is_summed = is_summed && ::fixy::bit_cast<std::uint32_t>(value) == ::fixy::bit_cast<std::uint32_t>(expected);
        expected += 1.0f;
    }
    CRUCIBLE_TEST_ASSERT(test, is_written && is_summed);
    pass(test, "  branded_inputs: PASSED\n");
}

// Views of different sizes give ShapeMismatch, and the walk writes nothing.
void shape_mismatch(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 12> values{};
    ::fixy::FixedArray<int, 12> const others{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::FixedArray<std::size_t, 2> const other_sizes{4, 3};
    ::fixy::FixedArray<std::size_t, 2> const other_strides{3, 1};
    ::fixy::StridedView<int const, 2> const wrong =
        ::fixy::mint_strided_view(others.view(), other_sizes.view(), other_strides.view(), 0).expect("it fits");
    auto [is_refused, kept] = ::fixy::with_mut(std::move(values), [&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("dense");
        return ::foundation::core::write_pointwise(out, Negate{}, wrong).is_err();
    });
    CRUCIBLE_TEST_ASSERT(test, is_refused && kept == (::fixy::FixedArray<int, 12>{}));
    pass(test, "  shape_mismatch: PASSED\n");
}

// An op with no input fills each element of the output, and a view with no
// element and a view of rank zero take one path each.
void fill_and_edges(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<double, 21> values{};
    ::fixy::FixedArray<std::size_t, 1> const sizes{7};
    ::fixy::FixedArray<std::size_t, 1> const strides{3};
    ::fixy::View<std::size_t const> const no_axes{};
    auto [is_filled, kept] = ::fixy::with_mut(std::move(values), [&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("it fits");
        return ::foundation::core::write_pointwise(out, [] noexcept { return 2.5; }).is_ok();
    });
    double total = 0.0;
    for (double const value : kept) {
        total += value;
    }
    CRUCIBLE_TEST_ASSERT(test,
                         is_filled && ::fixy::bit_cast<std::uint64_t>(total) == ::fixy::bit_cast<std::uint64_t>(17.5));

    ::fixy::FixedArray<double, 3> point{1.0, 2.0, 3.0};
    ::fixy::FixedArray<double, 3> const source{4.0, 5.0, 6.0};
    ::fixy::StridedView<double const> const one =
        ::fixy::mint_strided_view(source.view(), no_axes, no_axes, 2).expect("one element fits");
    auto [is_one, one_kept] = ::fixy::with_mut(std::move(point), [&](auto run) noexcept {
        auto out = ::fixy::mint_strided_view_mut(std::move(run), no_axes, no_axes, 1).expect("one element fits");
        return ::foundation::core::write_pointwise(out, Negate{}, one).is_ok();
    });
    ::fixy::FixedArray<double, 3> const one_expected{1.0, -6.0, 3.0};
    CRUCIBLE_TEST_ASSERT(test, is_one
                                   && same_bits(::fixy::View<double const>{one_kept.view()},
                                                ::fixy::View<double const>{one_expected.view()}));
    pass(test, "  fill_and_edges: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    branded_inputs(test);
    shape_mismatch(test);
    fill_and_edges(test);
    nan_rule_on_each_path<float>(test, "float");
    nan_rule_on_each_path<double>(test, "double");
    tile_classes<float>(test, "float");
    tile_classes<double>(test, "double");
    pointwise_agrees_with_walk<float, 2>(test, "float add", kSeed, Add{});
    pointwise_agrees_with_walk<float, 3>(test, "float multiply-add", kSeed + 1, MultiplyAdd{});
    pointwise_agrees_with_walk<float, 2>(test, "float divide", kSeed + 2, Divide{});
    pointwise_agrees_with_walk<float, 1>(test, "float negate", kSeed + 3, Negate{});
    pointwise_agrees_with_walk<float, 1>(test, "float copy that keeps its bits", kSeed + 10, Copy{});
    pointwise_agrees_with_walk<double, 1>(test, "double negate that keeps its bits", kSeed + 11, KeptNegate{});
    pointwise_agrees_with_walk<float, 2>(test, "float add on the scalar path", kSeed + 12, ScalarAdd{});
    pointwise_agrees_with_walk<double, 2>(test, "double add", kSeed + 4, Add{});
    pointwise_agrees_with_walk<double, 3>(test, "double multiply-add", kSeed + 5, MultiplyAdd{});
    pointwise_agrees_with_walk<std::uint32_t, 2>(test, "uint32 add", kSeed + 6, Add{});
    // A shared input reads the outputs of earlier steps, so a chain of products
    // has no bound.  Thus the multiply-add of 8-byte integers is unsigned,
    // which wraps, and the signed lanes take the negate.
    pointwise_agrees_with_walk<std::uint64_t, 3>(test, "uint64 multiply-add", kSeed + 7, MultiplyAdd{});
    pointwise_agrees_with_walk<std::int64_t, 1>(test, "int64 negate", kSeed + 9, Negate{});
    pointwise_agrees_with_walk<std::uint16_t, 2>(test, "uint16 add on the scalar path", kSeed + 8, Add{});
    pass(test, "test_pointwise: all tests passed\n");
    return 0;
}
