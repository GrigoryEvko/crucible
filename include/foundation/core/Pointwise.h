#pragma once

// The pointwise walk of the strided views of foundation/core/Strided.h: the
// output element at each index is a function of the input elements at the
// same index.
//
// THE WALK
//   write_pointwise(out, op, inputs...)
//                       writes op(input...) into the element of out at each
//                       index of the sizes, with the element of each input at
//                       that index.  out is a StridedViewMut, which the walk
//                       borrows for the call.  Each input is a StridedView or
//                       a StridedViewMut of the same rank parameter.  The
//                       views must have equal sizes, else the walk gives
//                       ShapeMismatch and writes nothing
//
// THE OP
// The op is an empty class (PointwiseOp) that takes one element of each input
// and gives the element of out.  An op with no state reads no element through
// a capture, so a visit can read only the elements at its own index.  The
// proof assumes that the op reaches no element through an object of static
// storage duration either, as the proof of write_each of
// foundation/core/Region.h does.
//
// THE ORDER
// A unique view names a different element at each index.  So when no input
// shares an element with out, each visit reads only elements that no visit
// writes, and the order of the visits changes no bit.  The walk then picks
// the order: it puts the axis with the smallest stride of out inside, drops
// each axis of size one, and merges two neighbor axes when each view reads
// them as one axis.  An input with the same first element and the same
// strides as out is the in-place form: each visit reads its own element of
// out before it writes it, so the order changes no bit there either.
//
// An input that can share an element with out in a different place takes
// the stated order of walk() of foundation/core/Strided.h: the row-major
// order of the logical sizes, one index after the other.  Each visit then
// reads the inputs after the writes of each visit before it.
//
// THE ALIAS PROOF
// An input that is a StridedViewMut of a brand other than the brand of out
// names a run of a different frame, so it shares no element with out (THE
// ALIAS PROOF of foundation/core/Frame.h), with no check at run time.  For
// each other input, the walk compares the address range of its elements with
// the address range of out, one time for each walk.  Two ranges that touch
// count as shared, also when the two views interleave and share no element.
// A walk with no shared input reads through restrict pointers.
//
// THE LANES
// An op that also takes a vec<E, W> of the Simd family for each input and
// gives a vec<T, W> is a lane op (LaneOpOf).  The lane count W fills the
// widest register of the target, native_vector_bytes of foundation/Simd.h: 16
// floats with AVX-512, 8 with AVX2, and 4 with SSE2 and NEON.  A wider vector
// would be split into registers by GCC.  The op computes each element alone,
// so W changes no bit of a result.  Each element
// of a lane op has the width of the output, and it is a float, a double, or an
// integer of at least the width of int.  A narrower integer computes in int
// in a scalar step and in its own width in a lane, so it takes the scalar
// path.  The walk calls a lane op only on lanes, also for one element, so each
// path gives the same bits.  A lane computes the IEEE operation of the op in
// each lane, and the build has no contraction of a multiply and an add.  A
// generic op whose body does not compile for lanes must state that in its
// signature, such as with a constraint, because the walk tests the call.
//
// The walk picks the loop of each view one time for each walk, from the
// stride of the view on the inner axis:
//   Dense    a stride of one: one load of W elements
//   Splat    a stride of zero: one broadcast for each row
//   StepTwo  a stride of two: a load of W elements and a load of W - 1
//            elements, and the even lanes of the two
//   General  each other stride: W loads of one element
// The output takes the Dense store or the General store of one element for
// each lane.  A General output gives each input the General loop.  The last
// block of a row with fewer than W elements is a masked block: each load
// reads only the elements of the block, and the store writes only them.  Each
// dead lane holds a copy of lane 0, so the op raises no floating-point flag
// that the scalar step of lane 0 does not raise.  No load reads past the
// last element of its view.
//
// An op that is no lane op takes the scalar path: the same order with a call
// of the op for each element, in loops that the optimizer can vectorize.

#include <foundation/Simd.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/core/Strided.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

// An input of the pointwise walk: a StridedView or a StridedViewMut of the
// rank parameter Rank.
template <class In, std::size_t Rank>
concept PointwiseInput = (::foundation::reflect::IsInstanceOf<In, ^^StridedView>
                          || ::foundation::reflect::IsInstanceOf<In, ^^StridedViewMut>)
                      && In::static_rank == Rank;

// The element of an input, with no const.
template <class In>
using pointwise_element_t = std::remove_const_t<typename In::element_type>;

// The op of a pointwise walk with the output element T: an object with no
// state, which takes one element of each input by const reference and gives
// a value that converts to T, with no throw.
template <class Op, class T, class... Inputs>
concept PointwiseOp = std::is_empty_v<Op> && std::is_nothrow_copy_constructible_v<Op>
                   && std::is_nothrow_invocable_r_v<T, Op const&, pointwise_element_t<Inputs> const&...>;

// The arguments of write_pointwise: inputs of the rank parameter of the
// output and a pointwise op.
template <class Op, class T, std::size_t Rank, class... Inputs>
concept CanWritePointwise = (PointwiseInput<Inputs, Rank> && ...) && PointwiseOp<Op, T, Inputs...>;

// An element of the lane path: a float, a double, or an integer of the width
// of int or wider.
template <class E>
concept LaneElement = std::same_as<E, float> || std::same_as<E, double>
                   || (std::integral<E> && !std::same_as<E, bool> && sizeof(E) >= sizeof(int) && sizeof(E) <= 8);

// A lane op for the output element T and the input elements Es: each element
// is a LaneElement of the width of T, and the op takes a vec<E, W> of each
// input and gives a vec<T, W>, with no throw.  W is native_lanes<T> of
// foundation/Simd.h, the lanes of T in the widest register of the target.
template <class Op, class T, class... Es>
concept LaneOpOf =
    LaneElement<T> && (LaneElement<Es> && ...) && ((sizeof(Es) == sizeof(T)) && ...)
    && std::is_nothrow_invocable_v<Op const&, ::foundation::simd::vec<Es, ::foundation::simd::native_lanes<T>>...>
    && std::same_as<
        std::invoke_result_t<Op const&, ::foundation::simd::vec<Es, ::foundation::simd::native_lanes<T>>...>,
        ::foundation::simd::vec<T, ::foundation::simd::native_lanes<T>>>;

namespace detail {

// The relation of an input to the output.
enum class PointwiseRelation : std::uint8_t {
    // No element of the input is an element of the output.
    Disjoint,
    // The input names the elements of the output at the same indexes.
    InPlace,
    // The input can name an element of the output at a different index.
    Shared,
};

// The loop of a view on the inner axis of a pointwise walk.
enum class LaneClass : std::uint8_t {
    Dense,
    Splat,
    StepTwo,
    General,
};

// The loops of the views of one instance of the lane path.
template <LaneClass... Classes>
struct LaneClasses {};

// The class General for each input of a General output.
template <class E>
inline constexpr LaneClass general_class_ = LaneClass::General;

// The plain view of an input or of the output.
template <class T, std::size_t Rank>
[[nodiscard]] constexpr StridedView<T, Rank> plain_view_of_(StridedView<T, Rank> const& view) noexcept {
    return view;
}
template <class T, std::size_t Rank, class Brand>
[[nodiscard]] constexpr StridedView<T, Rank> plain_view_of_(StridedViewMut<T, Rank, Brand> const& view) noexcept {
    return strided_view_of_(StridedDoor::key(), view);
}

// True when the type proves that the input names a run of a frame other than
// the frame of Brand.
template <class In, class Brand>
[[nodiscard]] consteval bool is_branded_apart_() noexcept {
    if constexpr (::foundation::reflect::IsInstanceOf<In, ^^StridedViewMut>) {
        return !std::same_as<typename In::brand_type, Brand>;
    } else {
        return false;
    }
}

// The highest offset of an element of lanes that hold an element.  The
// invariant of a view bounds it by max_strided_count, so no sum wraps.
template <class Lanes>
[[nodiscard]] constexpr std::size_t span_of_(Lanes const& lanes) noexcept {
    std::size_t span = 0;
    for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
        span += (lanes.sizes[axis] - 1) * lanes.strides[axis];
    }
    return span;
}

// The relation of an input of the elements E to an output of the elements
// T, for views of equal sizes that hold an element.  The address ranges are
// compared as integers, so two views of different objects give no undefined
// compare of pointers.  The in-place form needs the same element type, the
// same first element and the same stride on each axis of a size above one.
// O(rank).
template <class T, class E, class Lanes>
[[nodiscard]] PointwiseRelation relation_of_(T const* out_first, Lanes const& out_lanes, E const* in_first,
                                             Lanes const& in_lanes) noexcept {
    std::uintptr_t const out_low = __builtin_bit_cast(std::uintptr_t, out_first);
    std::uintptr_t const out_high = out_low + (span_of_(out_lanes) + 1) * sizeof(T);
    std::uintptr_t const in_low = __builtin_bit_cast(std::uintptr_t, in_first);
    std::uintptr_t const in_high = in_low + (span_of_(in_lanes) + 1) * sizeof(E);
    if (in_high <= out_low || out_high <= in_low) return PointwiseRelation::Disjoint;
    if constexpr (std::same_as<T, E>) {
        bool is_same_layout = in_low == out_low;
        for (std::size_t axis = 0; axis < out_lanes.rank(); ++axis) {
            is_same_layout =
                is_same_layout && (out_lanes.sizes[axis] <= 1 || out_lanes.strides[axis] == in_lanes.strides[axis]);
        }
        if (is_same_layout) return PointwiseRelation::InPlace;
    }
    return PointwiseRelation::Shared;
}

// True when the axis first must be outside the axis second: the first view,
// out first, whose strides on the two axes differ and are not zero has the
// larger stride on first.  A stride of zero tells nothing about an order.
// Two axes with no such view keep their order.
template <std::size_t Views, class Lanes>
[[nodiscard]] constexpr bool is_outer_axis_(Lanes const (&lanes)[Views], std::size_t first,
                                            std::size_t second) noexcept {
    for (std::size_t view = 0; view < Views; ++view) {
        std::size_t const first_stride = lanes[view].strides[first];
        std::size_t const second_stride = lanes[view].strides[second];
        if (first_stride == 0 || second_stride == 0 || first_stride == second_stride) continue;
        return first_stride > second_stride;
    }
    return false;
}

// The lanes of each view with the axes in the order of the walk: the outer
// axis first and the axis with the smallest stride of out last.  The sort is
// an insertion sort, which keeps the row-major order of two axes that no view
// orders.  O(rank^2 * views).
template <std::size_t Views, class Lanes>
constexpr void order_axes_(Lanes const (&lanes)[Views], Lanes (&ordered)[Views]) noexcept {
    std::size_t const rank = lanes[0].rank();
    std::size_t order[Lanes::capacity]{};
    for (std::size_t axis = 0; axis < rank; ++axis) {
        order[axis] = axis;
    }
    for (std::size_t position = 1; position < rank; ++position) {
        std::size_t const axis = order[position];
        std::size_t place = position;
        while (place > 0 && is_outer_axis_(lanes, axis, order[place - 1])) {
            order[place] = order[place - 1];
            --place;
        }
        order[place] = axis;
    }
    for (std::size_t view = 0; view < Views; ++view) {
        ordered[view] = Lanes::of_rank(rank);
        for (std::size_t position = 0; position < rank; ++position) {
            ordered[view].sizes[position] = lanes[view].sizes[order[position]];
            ordered[view].strides[position] = lanes[view].strides[order[position]];
        }
    }
}

// The plan of a walk over the lanes of each view, after the merge of
// plan_walk_ of Strided.h.
template <std::size_t Views, class Lanes, std::size_t... Is>
[[nodiscard]] constexpr WalkPlan<Views, Lanes::capacity> plan_of_lanes_(Lanes const (&lanes)[Views],
                                                                        std::index_sequence<Is...> /*views*/) noexcept {
    return plan_walk_(lanes[0], lanes[Is]...);
}

// The plan of a walk over views that are each dense: one axis of count
// elements with a stride of one in each view.
template <std::size_t Views, std::size_t Capacity>
[[nodiscard]] constexpr WalkPlan<Views, Capacity> dense_plan_(std::size_t count) noexcept {
    WalkPlan<Views, Capacity> plan{};
    plan.rank = 1;
    plan.sizes[0] = count;
    for (std::size_t view = 0; view < Views; ++view) {
        plan.strides[view][0] = 1;
    }
    return plan;
}

// The count of the rows of a plan of at least one axis: the product of the
// sizes of each axis but the inner one.  The count of the plan bounds it.
template <std::size_t Views, std::size_t Capacity>
[[nodiscard]] constexpr std::size_t rows_of_(WalkPlan<Views, Capacity> const& plan, std::size_t inner_axes) noexcept {
    std::size_t rows = 1;
    for (std::size_t axis = 0; axis + inner_axes < plan.rank; ++axis) {
        rows *= plan.sizes[axis];
    }
    return rows;
}

// The loop of a view of the lane path, from its stride on the inner axis.
[[nodiscard]] constexpr LaneClass lane_class_of_(std::size_t step) noexcept {
    if (step == 1) return LaneClass::Dense;
    if (step == 0) return LaneClass::Splat;
    if (step == 2) return LaneClass::StepTwo;
    return LaneClass::General;
}

// The lane count W as a count of array elements.
template <int W>
inline constexpr std::size_t lane_count_ = static_cast<std::size_t>(W);

// An input of the tile path: the element at the zero position of the tile
// axes for the current outer row, the strides on a and on b, and the loop of
// the input on a.  A transposed input has a stride of one on b.
template <int W, class E>
struct TileInput {
    E const* first = nullptr;
    std::size_t step_a = 0;
    std::size_t step_b = 0;
    LaneClass row_class = LaneClass::General;
    bool is_transposed = false;
};

// The plan of the outer rows of the tile path: the axes of plan other than a
// and b, in their order, and a last axis of size one with no stride.
template <std::size_t Views, std::size_t Capacity>
[[nodiscard]] constexpr WalkPlan<Views, Capacity> outer_plan_(WalkPlan<Views, Capacity> const& plan, std::size_t a,
                                                              std::size_t b) noexcept {
    WalkPlan<Views, Capacity> outer{};
    for (std::size_t axis = 0; axis < plan.rank; ++axis) {
        if (axis == a || axis == b) continue;
        outer.sizes[outer.rank] = plan.sizes[axis];
        for (std::size_t view = 0; view < Views; ++view) {
            outer.strides[view][outer.rank] = plan.strides[view][axis];
        }
        ++outer.rank;
    }
    outer.sizes[outer.rank] = 1;
    ++outer.rank;
    return outer;
}

// The axis b of the tile path for a plan whose out has a stride of one on the
// inner axis a: an outer axis on which an input with a stride other than zero
// and one on a has a stride of one.  The inner axis when no input has such
// an axis, or when a or b holds fewer than half a register of positions, so
// that a tile would be mostly dead lanes.  O(rank * views).
template <int W, std::size_t Views, std::size_t Capacity>
[[nodiscard]] constexpr std::size_t tile_axis_of_(WalkPlan<Views, Capacity> const& plan) noexcept {
    std::size_t const inner = plan.rank - 1;
    std::size_t const half = static_cast<std::size_t>(W) / 2;
    if (plan.rank < 2 || plan.strides[0][inner] != 1 || plan.sizes[inner] < half) return inner;
    for (std::size_t view = 1; view < Views; ++view) {
        if (plan.strides[view][inner] <= 1) continue;
        for (std::size_t axis = inner; axis-- > 0;) {
            if (plan.strides[view][axis] == 1 && plan.sizes[axis] >= half) return axis;
        }
    }
    return inner;
}

// The body of the scalar path for one index.
template <class Op, class T>
struct ScalarStep {
    Op op;

    template <class... Es>
    constexpr void operator()(T& target, Es const&... values) const noexcept {
        target = op(values...);
    }
};

// A dense row of the scalar path when no input shares an element with out.
// The pointers are restrict parameters, and GCC keeps the restrict of a
// parameter of an inlined function, so the vectorized loop has no alias
// check.  O(length).
template <class Op, class T, class... Es>
CRUCIBLE_INLINE void run_scalar_dense_restrict_(Op const& op, std::size_t length, T* __restrict out,
                                                Es const* __restrict... inputs) noexcept {
    for (std::size_t position = 0; position < length; ++position) {
        out[position] = op(inputs[position]...);
    }
}

// A dense row of the scalar path with an input in place.  O(length).
template <class Op, class T, class... Es>
CRUCIBLE_INLINE void run_scalar_dense_(Op const& op, std::size_t length, T* out, Es const*... inputs) noexcept {
    for (std::size_t position = 0; position < length; ++position) {
        out[position] = op(inputs[position]...);
    }
}

// The scalar path over a plan of at least one axis, in the order that the
// walk picked.  A row with a stride of one in each view takes a dense loop,
// with restrict pointers when no input is in place.  Each other row takes the
// strided row of walk().  O(count).
template <class Op, std::size_t Capacity, std::size_t... Is, class T, class... Es>
[[gnu::hot, gnu::noinline]] void run_scalar_plan_(Op const& op, std::index_sequence<Is...> inputs_sequence,
                                                  WalkPlan<1 + sizeof...(Es), Capacity> const& plan, bool is_restrict,
                                                  T* out, Es const*... inputs) noexcept {
    std::size_t const inner = plan.rank - 1;
    std::size_t const length = plan.sizes[inner];
    std::size_t const rows = rows_of_(plan, 1);
    std::size_t counters[Capacity]{};
    std::size_t offsets[1 + sizeof...(Es)]{};
    bool const is_dense = plan.strides[0][inner] == 1 && ((plan.strides[Is + 1][inner] == 1) && ...);
    if (is_dense && is_restrict) {
        for (std::size_t row = 0; row < rows; ++row) {
            run_scalar_dense_restrict_(op, length, out + offsets[0], (inputs + offsets[Is + 1])...);
            step_rows_(plan, counters, offsets);
        }
        return;
    }
    if (is_dense) {
        for (std::size_t row = 0; row < rows; ++row) {
            run_scalar_dense_(op, length, out + offsets[0], (inputs + offsets[Is + 1])...);
            step_rows_(plan, counters, offsets);
        }
        return;
    }
    ScalarStep<Op, T> step{op};
    std::size_t const steps[1 + sizeof...(Es)] = {plan.strides[0][inner], plan.strides[Is + 1][inner]...};
    for (std::size_t row = 0; row < rows; ++row) {
        walk_strided_row_(step, inputs_sequence, length, steps, out + offsets[0], (inputs + offsets[Is + 1])...);
        step_rows_(plan, counters, offsets);
    }
}

// ── The lanes ───────────────────────────────────────────────────────────
//
// Each part from here to the end of this namespace calls the Simd family, or
// calls a part that does, so its body depends on the target of the unit.  The
// parts are in the inline namespace CRUCIBLE_SIMD_TARGET (THE TARGET
// NAMESPACE of foundation/Platform.h), so each target has its own symbols.

inline namespace CRUCIBLE_SIMD_TARGET {

// The signed integer of the width of the element E: the lane of a mask over
// lanes of E.
template <class E>
using lane_number_t = std::conditional_t<sizeof(E) == 4, std::int32_t, std::int64_t>;

// The vector with each lane at or past live replaced by lane 0.  The select
// is a vector condition on the number of each lane, which a target with
// masks compiles to one blend.  live is at most W.
template <class E, int W, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE ::foundation::simd::vec<E, W>
fill_dead_lanes_(::foundation::simd::vec<E, W> value, std::size_t live,
                 ::foundation::simd::detail::LaneList<Lane...> /*lanes*/) noexcept {
    using Number = lane_number_t<E>;
    using Numbers = ::foundation::simd::detail::raw_vector_t<Number, W>;
    using Raw = typename ::foundation::simd::vec<E, W>::raw_type;
    Numbers const numbers{static_cast<Number>(Lane)...};
    Number const limit = static_cast<Number>(live);
    Numbers const limits{((void)Lane, limit)...};
    E const first = value.raw()[0];
    Raw const copies{((void)Lane, first)...};
    return ::foundation::simd::vec<E, W>{numbers < limits ? value.raw() : copies};
}

template <class E, int W>
[[nodiscard]] CRUCIBLE_INLINE ::foundation::simd::vec<E, W> fill_dead_lanes_(::foundation::simd::vec<E, W> value,
                                                                             std::size_t live) noexcept {
    return fill_dead_lanes_(value, live, ::foundation::simd::detail::lane_list_t<W>{});
}

// The reads of one row of an input of the lane path: the element at the zero
// position of the row, the stride of the inner axis, and the broadcast of a
// Splat row.  full(position) reads the W elements from position, and
// tail(position, live) reads live elements, with copies of lane 0 in the
// other lanes.  A read touches only elements of the row, and the elements
// between them inside the span of the view.
template <LaneClass Class, int W, class E>
struct LaneRow {
    using V = ::foundation::simd::vec<E, W>;

    E const* row = nullptr;
    std::size_t step = 0;
    V splat{};

    [[nodiscard]] CRUCIBLE_INLINE V full(std::size_t position) const noexcept {
        if constexpr (Class == LaneClass::Dense) {
            return ::foundation::simd::load<V>(row + position);
        } else if constexpr (Class == LaneClass::Splat) {
            return splat;
        } else if constexpr (Class == LaneClass::StepTwo) {
            E const* const at = row + 2 * position;
            return ::foundation::simd::pick_even_lanes(::foundation::simd::load<V>(at),
                                                       ::foundation::simd::load_prefix<V>(at + W, W - 1));
        } else {
            E const* const at = row + position * step;
            std::size_t const stride = step;
            return V{[at, stride](auto lane) noexcept {
                return at[static_cast<std::size_t>(decltype(lane)::value) * stride];
            }};
        }
    }

    [[nodiscard]] CRUCIBLE_INLINE V tail(std::size_t position, std::size_t live) const noexcept {
        if constexpr (Class == LaneClass::Dense) {
            return fill_dead_lanes_(::foundation::simd::load_prefix<V>(row + position, live), live);
        } else if constexpr (Class == LaneClass::Splat) {
            return splat;
        } else if constexpr (Class == LaneClass::StepTwo) {
            // The live elements span 2 * live - 1 elements from at.
            E const* const at = row + 2 * position;
            std::size_t const span = 2 * live - 1;
            std::size_t const width = static_cast<std::size_t>(W);
            V const low = ::foundation::simd::load_prefix<V>(at, span < width ? span : width);
            V const high = span > width ? ::foundation::simd::load_prefix<V>(at + W, span - width) : V{};
            return fill_dead_lanes_(::foundation::simd::pick_even_lanes(low, high), live);
        } else {
            E const* const at = row + position * step;
            std::size_t const stride = step;
            return V{[at, stride, live](auto lane) noexcept {
                std::size_t const index = static_cast<std::size_t>(decltype(lane)::value);
                return at[(index < live ? index : 0) * stride];
            }};
        }
    }
};

template <LaneClass Class, int W, class E>
[[nodiscard]] CRUCIBLE_INLINE LaneRow<Class, W, E> lane_row_(E const* row, std::size_t step) noexcept {
    using V = ::foundation::simd::vec<E, W>;
    return LaneRow<Class, W, E>{row, step, Class == LaneClass::Splat ? V{*row} : V{}};
}

// Writes the W lanes of value from position of the output row.
template <LaneClass Class, int W, class T>
CRUCIBLE_INLINE void store_full_(T* row, std::size_t step, std::size_t position,
                                 ::foundation::simd::vec<T, W> value) noexcept {
    if constexpr (Class == LaneClass::Dense) {
        ::foundation::simd::store(value, row + position);
    } else {
        auto const lanes = value.raw();
        T* const at = row + position * step;
        for (int lane = 0; lane < W; ++lane) {
            at[static_cast<std::size_t>(lane) * step] = lanes[lane];
        }
    }
}

// Writes the first live lanes of value from position of the output row.
template <LaneClass Class, int W, class T>
CRUCIBLE_INLINE void store_tail_(T* row, std::size_t step, std::size_t position, std::size_t live,
                                 ::foundation::simd::vec<T, W> value) noexcept {
    if constexpr (Class == LaneClass::Dense) {
        ::foundation::simd::store_prefix(value, row + position, live);
    } else {
        auto const lanes = value.raw();
        T* const at = row + position * step;
        for (int lane = 0; lane < W; ++lane) {
            if (static_cast<std::size_t>(lane) < live) at[static_cast<std::size_t>(lane) * step] = lanes[lane];
        }
    }
}

// One row of the lane path: the whole blocks of W elements, then the masked
// block of the rest.  O(length).
template <int W, LaneClass OutClass, class Op, class T, class... Rows>
CRUCIBLE_INLINE void run_lane_row_(Op const& op, std::size_t length, T* out, std::size_t out_step,
                                   Rows const&... rows) noexcept {
    std::size_t const width = static_cast<std::size_t>(W);
    std::size_t position = 0;
    for (; position + width <= length; position += width) {
        store_full_<OutClass, W>(out, out_step, position, op(rows.full(position)...));
    }
    std::size_t const live = length - position;
    if (live != 0) store_tail_<OutClass, W>(out, out_step, position, live, op(rows.tail(position, live)...));
}

// The lane path over a plan of at least one axis, for the loops of the views.
// The rows come in the order of the plan.  The function is not inlined, so
// each instance of the classes is one hot function.  O(count).
template <int W, LaneClass OutClass, LaneClass... InClasses, std::size_t... Is, class Op, std::size_t Capacity, class T,
          class... Es>
[[gnu::hot, gnu::noinline]] void
run_lane_plan_(LaneClasses<InClasses...> /*classes*/, std::index_sequence<Is...> /*inputs*/, Op const& op,
               WalkPlan<1 + sizeof...(Es), Capacity> const& plan, T* out, Es const*... inputs) noexcept {
    std::size_t const inner = plan.rank - 1;
    std::size_t const length = plan.sizes[inner];
    std::size_t const rows = rows_of_(plan, 1);
    std::size_t counters[Capacity]{};
    std::size_t offsets[1 + sizeof...(Es)]{};
    for (std::size_t row = 0; row < rows; ++row) {
        run_lane_row_<W, OutClass>(op, length, out + offsets[0], plan.strides[0][inner],
                                   lane_row_<InClasses, W>(inputs + offsets[Is + 1], plan.strides[Is + 1][inner])...);
        step_rows_(plan, counters, offsets);
    }
}

// ── The tile path ───────────────────────────────────────────────────────
//
// When out reads its inner axis a with a stride of one and an input reads a
// different axis b with a stride of one, no order of the axes makes both
// loops dense.  The tile path then walks tiles of W x W elements of the two
// axes.  For each tile, an input with a stride of one on b (a transposed
// input) loads W rows along b, one for each position of a, and transposes
// them in registers, so each register then holds W positions of a.  Each
// other input reads its register for each position of b with the loop of its
// class on a.  The op then gives one register of out for each position of b,
// and the store is dense.

// Writes the transpose of the W x W matrix of the registers rows: lane l of
// the register r becomes lane r of the register l.  Each stage of the
// distance D swaps the bit D of the number of a register with the bit D of
// the number of a lane: W / 2 pairs of registers, two shuffles for each pair.
// The log2(W) stages give W * log2(W) shuffles.
template <int W, int D, class Raw, int... Lane>
CRUCIBLE_INLINE void transpose_stage_(Raw (&rows)[lane_count_<W>],
                                      ::foundation::simd::detail::LaneList<Lane...> /*lanes*/) noexcept {
    for (int row = 0; row < W; ++row) {
        if ((row & D) != 0) continue;
        Raw const low = rows[row];
        Raw const high = rows[row + D];
        rows[row] = __builtin_shufflevector(low, high, ((Lane & D) != 0 ? W + Lane - D : Lane)...);
        rows[row + D] = __builtin_shufflevector(low, high, ((Lane & D) != 0 ? W + Lane : Lane + D)...);
    }
}

template <int W, int D = 1, class Raw>
CRUCIBLE_INLINE void transpose_registers_(Raw (&rows)[lane_count_<W>]) noexcept {
    if constexpr (D < W) {
        transpose_stage_<W, D>(rows, ::foundation::simd::detail::lane_list_t<W>{});
        transpose_registers_<W, D * 2>(rows);
    }
}

// The register of an input on a for one position of b: the loop of its class,
// with live lanes from a0.
template <int W, class E>
[[nodiscard]] CRUCIBLE_INLINE ::foundation::simd::vec<E, W> read_tile_row_(TileInput<W, E> const& input, E const* row,
                                                                           std::size_t a0, std::size_t live) noexcept {
    std::size_t const width = static_cast<std::size_t>(W);
    switch (input.row_class) {
        case LaneClass::Dense: {
            LaneRow<LaneClass::Dense, W, E> const reader{row, 1, {}};
            return live == width ? reader.full(a0) : reader.tail(a0, live);
        }
        case LaneClass::Splat:
            return ::foundation::simd::vec<E, W>{*row};
        case LaneClass::StepTwo: {
            LaneRow<LaneClass::StepTwo, W, E> const reader{row, 2, {}};
            return live == width ? reader.full(a0) : reader.tail(a0, live);
        }
        case LaneClass::General: {
            LaneRow<LaneClass::General, W, E> const reader{row, input.step_a, {}};
            return live == width ? reader.full(a0) : reader.tail(a0, live);
        }
        default:
            ::foundation::core::unreachable();
    }
}

// The registers of one tile of an input: tile[j] holds the elements of the
// position b0 + j of b, at the positions a0 to a0 + live_a - 1 of a, and
// copies of lane 0 in the other lanes.  Only the first live_b registers hold
// elements.  A transposed input reads live_b elements along b at each of the
// live_a positions of a, so no load reads past the view.  The registers keep
// the bits of E in the register type Raw of the output, which has the same
// width.
template <int W, class E, class Raw>
CRUCIBLE_INLINE void read_tile_(TileInput<W, E> const& input, E const* first, std::size_t a0, std::size_t b0,
                                std::size_t live_a, std::size_t live_b, Raw (&tile)[lane_count_<W>]) noexcept {
    using V = ::foundation::simd::vec<E, W>;
    using Lanes = typename V::raw_type;
    std::size_t const width = static_cast<std::size_t>(W);
    if (!input.is_transposed) return;
    Lanes registers[lane_count_<W>]{};
    for (std::size_t position = 0; position < live_a; ++position) {
        E const* const at = first + (a0 + position) * input.step_a + b0;
        registers[position] = live_b == width ? ::foundation::simd::load<V>(at).raw()
                                              : ::foundation::simd::load_prefix<V>(at, live_b).raw();
    }
    transpose_registers_<W>(registers);
    for (std::size_t position = 0; position < live_b; ++position) {
        Lanes const lanes =
            live_a == width ? registers[position] : fill_dead_lanes_(V{registers[position]}, live_a).raw();
        tile[position] = __builtin_bit_cast(Raw, lanes);
    }
}

// The register of an input for the position b0 + position of b in a tile:
// the transposed register of the tile, or the read of its row on a.
template <int W, class E, class Raw>
[[nodiscard]] CRUCIBLE_INLINE ::foundation::simd::vec<E, W>
tile_register_(TileInput<W, E> const& input, E const* first, Raw const (&tile)[lane_count_<W>], std::size_t a0,
               std::size_t b0, std::size_t position, std::size_t live_a) noexcept {
    using V = ::foundation::simd::vec<E, W>;
    if (input.is_transposed) return V{__builtin_bit_cast(typename V::raw_type, tile[position])};
    return read_tile_row_(input, first + (b0 + position) * input.step_b, a0, live_a);
}

// One tile of W x W elements from the positions a0 of a and b0 of b: the
// transposed registers of each transposed input, then one call of the op and
// one store for each position of b.
template <int W, class Op, std::size_t... Is, class T, class Raw, class... Es>
CRUCIBLE_INLINE void
run_tile_(Op const& op, std::index_sequence<Is...> /*inputs*/, T* out_row, std::size_t out_step_b, std::size_t a0,
          std::size_t b0, std::size_t live_a, std::size_t live_b, Raw (&tiles)[sizeof...(Es)][lane_count_<W>],
          std::size_t const (&offsets)[1 + sizeof...(Es)], TileInput<W, Es> const&... inputs) noexcept {
    std::size_t const width = static_cast<std::size_t>(W);
    (read_tile_(inputs, inputs.first + offsets[Is + 1], a0, b0, live_a, live_b, tiles[Is]), ...);
    for (std::size_t position = 0; position < live_b; ++position) {
        ::foundation::simd::vec<T, W> const value =
            op(tile_register_(inputs, inputs.first + offsets[Is + 1], tiles[Is], a0, b0, position, live_a)...);
        T* const at = out_row + (b0 + position) * out_step_b + a0;
        if (live_a == width) {
            ::foundation::simd::store(value, at);
        } else {
            ::foundation::simd::store_prefix(value, at, live_a);
        }
    }
}

// The tile path over the outer rows of outer, for tiles of the axes a and b
// of the sizes size_a and size_b.  outer holds the axes other than a and b,
// and a last axis of size one.  The tiles come in blocks of 8 x 8 tiles, so
// the lines and the pages of the views of a block stay in L2 and in its TLB
// while the block runs.  With AVX-512, three views of floats hold about
// 192 KB of a block, in 384 pages of 4 KB.  The function is not inlined, so each instance is one hot function.
// O(count) calls of the op, and W * log2(W) shuffles for each tile of each
// transposed input.
template <int W, class Op, std::size_t Capacity, std::size_t... Is, class T, class... Es>
[[gnu::hot, gnu::noinline]] void run_tile_plan_(Op const& op, std::index_sequence<Is...> sequence,
                                                WalkPlan<1 + sizeof...(Es), Capacity> const& outer, std::size_t rows,
                                                std::size_t size_a, std::size_t size_b, T* out, std::size_t out_step_b,
                                                TileInput<W, Es> const&... inputs) noexcept {
    using Raw = typename ::foundation::simd::vec<T, W>::raw_type;
    std::size_t const width = static_cast<std::size_t>(W);
    std::size_t const block = 8 * width;
    std::size_t counters[Capacity]{};
    std::size_t offsets[1 + sizeof...(Es)]{};
    Raw tiles[sizeof...(Es)][lane_count_<W>]{};
    for (std::size_t row = 0; row < rows; ++row) {
        T* const out_row = out + offsets[0];
        for (std::size_t b_block = 0; b_block < size_b; b_block += block) {
            std::size_t const b_end = size_b - b_block < block ? size_b : b_block + block;
            for (std::size_t a_block = 0; a_block < size_a; a_block += block) {
                std::size_t const a_end = size_a - a_block < block ? size_a : a_block + block;
                for (std::size_t b0 = b_block; b0 < b_end; b0 += width) {
                    std::size_t const live_b = b_end - b0 < width ? b_end - b0 : width;
                    for (std::size_t a0 = a_block; a0 < a_end; a0 += width) {
                        std::size_t const live_a = a_end - a0 < width ? a_end - a0 : width;
                        run_tile_<W>(op, sequence, out_row, out_step_b, a0, b0, live_a, live_b, tiles, offsets,
                                     inputs...);
                    }
                }
            }
        }
        step_rows_(outer, counters, offsets);
    }
}

// Calls next with the loops of the inputs from the index Done on, as template
// arguments.
template <std::size_t Done, std::size_t Inputs, LaneClass... Chosen, class Next>
CRUCIBLE_INLINE void choose_lane_classes_(LaneClass const (&classes)[Inputs], Next const& next) noexcept {
    if constexpr (Done == Inputs) {
        next(LaneClasses<Chosen...>{});
    } else {
        switch (classes[Done]) {
            case LaneClass::Dense:
                choose_lane_classes_<Done + 1, Inputs, Chosen..., LaneClass::Dense>(classes, next);
                return;
            case LaneClass::Splat:
                choose_lane_classes_<Done + 1, Inputs, Chosen..., LaneClass::Splat>(classes, next);
                return;
            case LaneClass::StepTwo:
                choose_lane_classes_<Done + 1, Inputs, Chosen..., LaneClass::StepTwo>(classes, next);
                return;
            case LaneClass::General:
                choose_lane_classes_<Done + 1, Inputs, Chosen..., LaneClass::General>(classes, next);
                return;
            default:
                ::foundation::core::unreachable();
        }
    }
}

// The tile path for the tile axes inner and tile_axis of plan: the outer plan,
// and the reads of each input on the two axes.
template <int W, class Op, std::size_t Capacity, std::size_t... Is, class T, class... Es>
CRUCIBLE_INLINE void run_tiles_(Op const& op, std::index_sequence<Is...> sequence,
                                WalkPlan<1 + sizeof...(Es), Capacity> const& plan, std::size_t tile_axis, T* out,
                                Es const*... inputs) noexcept {
    std::size_t const inner = plan.rank - 1;
    std::size_t const size_a = plan.sizes[inner];
    std::size_t const size_b = plan.sizes[tile_axis];
    WalkPlan<1 + sizeof...(Es), Capacity> const outer = outer_plan_(plan, inner, tile_axis);
    run_tile_plan_<W>(op, sequence, outer, rows_of_(outer, 1), size_a, size_b, out, plan.strides[0][tile_axis],
                      TileInput<W, Es>{inputs, plan.strides[Is + 1][inner], plan.strides[Is + 1][tile_axis],
                                       lane_class_of_(plan.strides[Is + 1][inner]),
                                       plan.strides[Is + 1][tile_axis] == 1 && plan.strides[Is + 1][inner] > 1}...);
}

// The lane path over a plan of at least one axis in the order that the walk
// picked: the tile path when out and an input read different axes with a
// stride of one, else the loop of each view, and one instance for them.
template <class Op, std::size_t Capacity, class T, class... Es>
void run_lanes_(Op const& op, WalkPlan<1 + sizeof...(Es), Capacity> const& plan, T* out, Es const*... inputs) noexcept {
    constexpr int lanes = ::foundation::simd::native_lanes<T>;
    constexpr std::size_t input_count = sizeof...(Es);
    using Sequence = std::index_sequence_for<Es...>;
    std::size_t const inner = plan.rank - 1;
    if (plan.strides[0][inner] != 1) {
        run_lane_plan_<lanes, LaneClass::General>(LaneClasses<general_class_<Es>...>{}, Sequence{}, op, plan, out,
                                                  inputs...);
        return;
    }
    if constexpr (input_count > 0) {
        std::size_t const tile_axis = tile_axis_of_<lanes>(plan);
        if (tile_axis != inner) {
            run_tiles_<lanes>(op, Sequence{}, plan, tile_axis, out, inputs...);
            return;
        }
    }
    if constexpr (input_count == 0) {
        run_lane_plan_<lanes, LaneClass::Dense>(LaneClasses<>{}, Sequence{}, op, plan, out);
    } else {
        LaneClass classes[input_count]{};
        for (std::size_t input = 0; input < input_count; ++input) {
            classes[input] = lane_class_of_(plan.strides[input + 1][inner]);
        }
        choose_lane_classes_<0, input_count>(
            classes, [&]<LaneClass... InClasses>(LaneClasses<InClasses...> chosen) noexcept {
                run_lane_plan_<lanes, LaneClass::Dense>(chosen, Sequence{}, op, plan, out, inputs...);
            });
    }
}

// The body of the lane path for one index: a broadcast of each input
// element, one call of the op, and lane 0 of the result.
template <class Op, class T>
struct LaneStep {
    Op op;

    template <class... Es>
    constexpr void operator()(T& target, Es const&... values) const noexcept {
        constexpr int lanes = ::foundation::simd::native_lanes<T>;
        target = op(::foundation::simd::vec<Es, lanes>{values}...).raw()[0];
    }
};

// The body of write_pointwise over the plain views: the shape check, the
// alias proof, the plan and the path.  apart[i] is true when the type of
// input i proves that it names the run of another frame.
template <class Op, class T, std::size_t Rank, std::size_t... Is, class... Es>
[[nodiscard]] constexpr Result<Unit, ShapeMismatch>
write_pointwise_(Op const& op, std::index_sequence<Is...> sequence, bool const (&apart)[sizeof...(Es) + 1],
                 StridedView<T, Rank> const& target, StridedView<Es, Rank> const&... sources) noexcept {
    using Lanes = StridedLanes<Rank>;
    constexpr std::size_t views = 1 + sizeof...(Es);
    constexpr bool is_lane_op = LaneOpOf<Op, T, std::remove_const_t<Es>...>;
    Lanes const& shape = strided_lanes_of_(StridedDoor::key(), target);
    if (!(same_sizes_(shape, strided_lanes_of_(StridedDoor::key(), sources)) && ...)) return err(ShapeMismatch{});
    std::size_t const count = target.count();
    if (count == 0) return Unit{};
    Lanes const lanes[views] = {shape, strided_lanes_of_(StridedDoor::key(), sources)...};
    T* const out = strided_first_of_(StridedDoor::key(), target);
    using Views = std::make_index_sequence<views>;

    bool is_shared = false;
    bool is_restrict = true;
    if consteval {
        // A constant evaluation reads no address as a number, so it takes
        // the stated order.
        is_shared = true;
    } else {
        if constexpr (sizeof...(Es) > 0) {
            auto const relate = [&](bool is_apart, auto const& source) noexcept {
                PointwiseRelation const relation =
                    is_apart
                        ? PointwiseRelation::Disjoint
                        : relation_of_(static_cast<T const*>(out), shape, strided_first_of_(StridedDoor::key(), source),
                                       strided_lanes_of_(StridedDoor::key(), source));
                is_shared = is_shared || relation == PointwiseRelation::Shared;
                is_restrict = is_restrict && relation == PointwiseRelation::Disjoint;
            };
            (relate(apart[Is + 1], sources), ...);
        }
    }

    if (is_shared) {
        WalkPlan<views, Lanes::capacity> const plan = plan_of_lanes_(lanes, Views{});
        if constexpr (is_lane_op) {
            LaneStep<Op, T> step{op};
            run_walk_(step, sequence, plan, count, out, strided_first_of_(StridedDoor::key(), sources)...);
        } else {
            ScalarStep<Op, T> step{op};
            run_walk_(step, sequence, plan, count, out, strided_first_of_(StridedDoor::key(), sources)...);
        }
        return Unit{};
    }
    // Views that are each dense read one run of count elements, so they need
    // no sort and no merge of the axes.
    WalkPlan<views, Lanes::capacity> plan{};
    if (target.is_dense() && (sources.is_dense() && ...)) {
        plan = dense_plan_<views, Lanes::capacity>(count);
    } else {
        Lanes ordered[views]{};
        order_axes_(lanes, ordered);
        plan = plan_of_lanes_(ordered, Views{});
    }
    if constexpr (is_lane_op) {
        if (plan.rank == 0) {
            LaneStep<Op, T> step{op};
            run_walk_(step, sequence, plan, count, out, strided_first_of_(StridedDoor::key(), sources)...);
        } else {
            run_lanes_(op, plan, out, strided_first_of_(StridedDoor::key(), sources)...);
        }
    } else {
        if (plan.rank == 0) {
            ScalarStep<Op, T> step{op};
            run_walk_(step, sequence, plan, count, out, strided_first_of_(StridedDoor::key(), sources)...);
        } else {
            run_scalar_plan_(op, sequence, plan, is_restrict, out, strided_first_of_(StridedDoor::key(), sources)...);
        }
    }
    return Unit{};
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// Writes op(input...) into the element of out at each index of the sizes, in
// an order that the walk picks when no input shares an element with out:
// THE WALK, THE ORDER and THE LANES above.  The walk gives ShapeMismatch, and
// writes nothing, when the sizes of an input differ from the sizes of out.
// O(count) calls of the op, O(rank^2 * views) for the plan, and O(rank) for
// the alias check of each input.
template <class T, std::size_t Rank, class Brand, class Op, class... Inputs>
    requires CanWritePointwise<Op, T, Rank, Inputs...>
[[nodiscard]] constexpr Result<Unit, ShapeMismatch> write_pointwise(StridedViewMut<T, Rank, Brand>& out, Op op,
                                                                    Inputs const&... inputs) noexcept {
    bool const apart[1 + sizeof...(Inputs)] = {false, detail::is_branded_apart_<Inputs, Brand>()...};
    return detail::write_pointwise_(op, std::index_sequence_for<Inputs...>{}, apart, detail::plain_view_of_(out),
                                    detail::plain_view_of_(inputs)...);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core
