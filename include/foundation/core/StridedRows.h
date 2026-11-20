#pragma once

// The walk of rows of the strided views: the plan of the pointwise walk of
// foundation/core/Pointwise.h, with a body of the caller for each row.
//
// THE WALK OF ROWS
//   mint_strided_rows(out, body, inputs...)
//                       calls body(row, out_view, input_view...) one time for
//                       each row of the plan of write_pointwise over out and the
//                       inputs, or for each part of a row in THE TILES.  out
//                       is a StridedViewMut, which the walk borrows for the
//                       call.  Each input is a StridedView or a
//                       StridedViewMut of the same rank parameter.  The views
//                       must have equal sizes, else the walk gives ShapeMismatch
//                       and calls body for no row
//
// THE ROW
// A row is the inner axis of the plan at one position of the outer axes, or
// a part of it in THE TILES.  The row gives the number of its row in the plan
// (RowIndex), the count of its positions (RowCount), its positions as a
// counted loop (steps(), each one a RowStep), and the offset of each view at
// its zero position (offset(view), a RowOffset).  A view of the body reads its
// element with at(offset, step).  So the body is the counted loop
//   for (auto step : row.steps()) out_view.at(row.offset(out_view), step) = ...
// and the optimizer can make it a vector loop.
//
// THE BRANDS
// Each call of the mint has a fresh brand, and each view of the call has a
// brand of its own, from the fresh brand and its place.  A RowStep, a RowIndex
// and a RowCount carry the fresh brand, and a RowOffset carries the brand of
// its view.  So the offset of one view does not compile with another view, and
// a step of one walk does not compile with the views of another walk.  The
// body gives no value, so no index of the walk leaves it.
//
// THE PLAN
// The walk takes the plan of write_pointwise (plan_pointwise_): THE ORDER and
// THE ALIAS PROOF of Pointwise.h.  When an input can share an element with out
// in a different place, the rows come in the row-major order of the logical
// sizes.  Else the walk picks the order of the axes, and it merges the axes
// that each view reads as one.  The rows come from the row loop of the lanes of
// write_pointwise (for_each_row_).  A view with a stride of one on the inner
// axis reads its row densely, with no product of the stride: RowRead::Unit, as
// a template argument of the view.  So a body has 2^(1 + inputs) instances,
// and THE TILES add 2^inputs - 1: one for each list of reads with out Unit
// and an input Strided.
//
// THE TILES
// A transposed input has a stride of one on an outer axis b and a larger
// stride on the inner axis a, so a full row reads one line of the input for
// each position, and the next row reads the same lines again.  When no input
// shares an element with out, and out reads a with a stride of one, the walk
// takes the axis b of the tile path of Pointwise.h (tile_axis_of_), with the
// constant row_tile_lanes in place of the lanes of the target.  When the
// plane of a and b holds at least row_tile_plane positions, it then gives the
// body the parts of row_tile_extent positions of a, in blocks of
// row_tile_extent rows: for each block of b, for each part of a, each row of
// the block.  The lines and the pages of the views that a block reads stay in
// L1 and in its TLB while the block runs.  A smaller plane stays in L1 in the
// order of the rows, so it takes no tile.  The parts of one row come in the
// order of a, and each part has the number of its row.  The extents are
// constants, so the sequence of the calls of the body does not depend on the
// target.  The loop of the body still reads the transposed input with one
// load for each step.
//
// THE NAN RULE of Pointwise.h does not hold here, because the body computes
// each value.  A body of float arithmetic that must give one NaN on each host
// applies fp::canonicalize_nan of foundation/core/Float.h to its values.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Pointwise.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The count of the positions of a part of a row of THE TILES, and the count
// of the rows of a block: a block of three views of floats reads 48 lines in
// 48 pages.  A constant, so the sequence of the calls of the body is the same
// on each target.
inline constexpr std::size_t row_tile_extent = 16;

// The least count of the positions of the plane of the two tile axes that
// takes THE TILES: 16384 floats are 64 KB, more than the L1 of a core.
inline constexpr std::size_t row_tile_plane = 16384;

// The lanes that the choice of the tile axis of THE TILES reads: an axis of
// fewer than half of them gives no tile.  A constant for the same reason.
inline constexpr int row_tile_lanes = 16;

// The brand of the view at Position of one walk of rows of the brand Walk.
template <class Walk, std::size_t Position>
struct RowViewBrand final {};

// The read of a view of a walk of rows along a row: Unit for a stride of one
// on the inner axis, which reads with no product of the stride, and Strided
// for each other stride.
enum class RowRead : std::uint8_t {
    Unit,
    Strided,
};

template <class Walk>
class RowIndex;
template <class Walk>
class RowCount;
template <class Walk>
class RowStep;
template <class View>
class RowOffset;
template <class Walk>
class RowSteps;
template <class T, class Walk, std::size_t Position, RowRead Read>
class RowView;
template <class Walk, std::size_t Views>
class StridedRow;

namespace detail {

// The door of the walk of rows to the private constructors of its types.
struct RowDoor {
    template <class Walk>
    [[nodiscard]] static constexpr RowIndex<Walk> index(std::size_t value) noexcept {
        return RowIndex<Walk>{value};
    }
    template <class Walk>
    [[nodiscard]] static constexpr RowCount<Walk> count(std::size_t value) noexcept {
        return RowCount<Walk>{value};
    }
    template <class Walk>
    [[nodiscard]] static constexpr RowStep<Walk> step(std::size_t value) noexcept {
        return RowStep<Walk>{value};
    }
    template <class Walk>
    [[nodiscard]] static constexpr RowSteps<Walk> steps(RowCount<Walk> count) noexcept {
        return RowSteps<Walk>{count};
    }
    template <class View>
    [[nodiscard]] static constexpr RowOffset<View> offset(std::size_t value) noexcept {
        return RowOffset<View>{value};
    }
    template <class Walk, std::size_t Views>
    [[nodiscard]] static constexpr StridedRow<Walk, Views> row(RowIndex<Walk> index, RowCount<Walk> count,
                                                               std::size_t const* offsets) noexcept {
        return StridedRow<Walk, Views>{index, count, offsets};
    }
    template <class T, class Walk, std::size_t Position, RowRead Read>
    [[nodiscard]] static constexpr RowView<T, Walk, Position, Read> view(T* first, std::size_t step) noexcept {
        return RowView<T, Walk, Position, Read>{first, step};
    }
};

}  // namespace detail

// The number of a row of the walk of rows of Walk, from zero.  The parts of
// one row in THE TILES have the number of the row.
template <class Walk>
class RowIndex final {
    std::size_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr explicit RowIndex(std::size_t value) noexcept : value_{value} {}

public:
    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
};

// The count of the positions of a row of the walk of rows of Walk.
template <class Walk>
class RowCount final {
    std::size_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr explicit RowCount(std::size_t value) noexcept : value_{value} {}

public:
    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
};

// A position of a row of the walk of rows of Walk, below the count of the row.
// Only steps() of a row makes one.
template <class Walk>
class RowStep final {
    std::size_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr explicit RowStep(std::size_t value) noexcept : value_{value} {}

public:
    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
};

// The offset of the zero position of a row in the view of the brand View.
// Only offset() of a row makes one.
template <class View>
class RowOffset final {
    std::size_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr explicit RowOffset(std::size_t value) noexcept : value_{value} {}

public:
    [[nodiscard]] constexpr std::size_t value() const noexcept { return value_; }
};

// The positions of a row: the counted loop `for (auto step : row.steps())`.
template <class Walk>
class RowSteps final {
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr explicit RowSteps(RowCount<Walk> count) noexcept : count_{count.value()} {}

public:
    // The cursor of the loop: the position of the next step.
    class Cursor final {
        std::size_t at_ = 0;
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        friend class RowSteps;

        constexpr explicit Cursor(std::size_t at) noexcept : at_{at} {}

    public:
        [[nodiscard]] constexpr RowStep<Walk> operator*() const noexcept { return detail::RowDoor::step<Walk>(at_); }
        constexpr Cursor& operator++() noexcept {
            ++at_;
            return *this;
        }
        [[nodiscard]] friend constexpr bool operator==(Cursor const& left, Cursor const& right) noexcept {
            return left.at_ == right.at_;
        }
    };

    [[nodiscard]] constexpr Cursor begin() const noexcept { return Cursor{0}; }
    [[nodiscard]] constexpr Cursor end() const noexcept { return Cursor{count_}; }
};

// The view at Position of one walk of rows: the element at the zero index,
// and the stride of the inner axis.  A view of RowRead::Unit has a stride of
// one on the inner axis, so at() reads with no product.
template <class T, class Walk, std::size_t Position, RowRead Read>
class RowView final {
    T* first_ = nullptr;
    std::size_t step_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr RowView(T* first, std::size_t step) noexcept : first_{first}, step_{step} {}

public:
    using brand_type = RowViewBrand<Walk, Position>;

    // The element of the position step of the row of offset.  The plan proves
    // each such element inside the view, so the read has no check.
    [[nodiscard]] constexpr T& at(RowOffset<brand_type> offset, RowStep<Walk> step) const noexcept {
        if constexpr (Read == RowRead::Unit) {
            return first_[offset.value() + step.value()];
        } else {
            return first_[offset.value() + step.value() * step_];
        }
    }
};

// One row of a walk of rows of Walk over Views views.  The row reads the
// offsets of the row loop, so it lives for one call of the body.
template <class Walk, std::size_t Views>
class StridedRow final {
    RowIndex<Walk> index_ = detail::RowDoor::index<Walk>(0);
    RowCount<Walk> count_ = detail::RowDoor::count<Walk>(0);
    std::size_t const* offsets_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::RowDoor;

    constexpr StridedRow(RowIndex<Walk> index, RowCount<Walk> count, std::size_t const* offsets) noexcept
        : index_{index}, count_{count}, offsets_{offsets} {}

public:
    StridedRow(StridedRow const&) = delete("a row holds the offsets of one step of the walk: pass it by reference");
    StridedRow& operator=(StridedRow const&) = delete("a row holds the offsets of one step of the walk");

    [[nodiscard]] constexpr RowIndex<Walk> index() const noexcept { return index_; }
    [[nodiscard]] constexpr RowCount<Walk> count() const noexcept { return count_; }
    [[nodiscard]] constexpr RowSteps<Walk> steps() const noexcept { return detail::RowDoor::steps<Walk>(count_); }

    // The offset of the zero position of this row in a view of the walk.
    template <class T, std::size_t Position, RowRead Read>
        requires(Position < Views)
    [[nodiscard]] constexpr RowOffset<RowViewBrand<Walk, Position>>
    offset(RowView<T, Walk, Position, Read> const& /*view*/) const noexcept {
        return detail::RowDoor::offset<RowViewBrand<Walk, Position>>(offsets_[Position]);
    }
};

namespace detail {

// The reads of the views of a walk of rows, as template arguments.
template <RowRead... Reads>
struct RowReads final {};

// The read of a view of the walk of rows from the stride of its inner axis.
[[nodiscard]] constexpr RowRead row_read_of_(std::size_t step) noexcept {
    return step == 1 ? RowRead::Unit : RowRead::Strided;
}

// Calls next with the reads of the views from the index Done on, as template
// arguments: 2^Views lists.
template <std::size_t Done, std::size_t Views, RowRead... Chosen, class Next>
constexpr void choose_row_reads_(RowRead const (&reads)[Views], Next const& next) noexcept {
    if constexpr (Done == Views) {
        next(RowReads<Chosen...>{});
    } else if (reads[Done] == RowRead::Unit) {
        choose_row_reads_<Done + 1, Views, Chosen..., RowRead::Unit>(reads, next);
    } else {
        choose_row_reads_<Done + 1, Views, Chosen..., RowRead::Strided>(reads, next);
    }
}

// The rows of a plan of at least one axis, with the views of the reads.
template <class Walk, RowRead OutRead, RowRead... InReads, std::size_t... Is, class Body, std::size_t Capacity, class T,
          class... Es>
constexpr void run_rows_(RowReads<OutRead, InReads...> /*reads*/, std::index_sequence<Is...> /*inputs*/, Body& body,
                         WalkPlan<1 + sizeof...(Es), Capacity> const& plan, T* out, Es const*... inputs) noexcept {
    constexpr std::size_t views = 1 + sizeof...(Es);
    std::size_t const inner = plan.rank - 1;
    RowCount<Walk> const length = RowDoor::count<Walk>(plan.sizes[inner]);
    auto const out_view = RowDoor::view<T, Walk, 0, OutRead>(out, plan.strides[0][inner]);
    for_each_row_(plan, [&](std::size_t index, std::size_t const(&offsets)[views]) noexcept {
        StridedRow<Walk, views> const row = RowDoor::row<Walk, views>(RowDoor::index<Walk>(index), length, offsets);
        body(row, out_view, RowDoor::view<Es const, Walk, Is + 1, InReads>(inputs, plan.strides[Is + 1][inner])...);
    });
}

// The rows of a plan in THE TILES, for the tile axis b: the axes of the plan
// other than a and b in their order, then the blocks of row_tile_extent
// positions of b, then the parts of row_tile_extent positions of a, then the
// rows of the block.  The number of a part is the number of its row in the
// plan: the axes of the plan before the inner axis in row-major order, so the
// axes after b give the weight of b.  The function is not inlined, so the
// loop of a row of the caller keeps its registers.  O(rows * parts) calls of
// body.
template <class Walk, RowRead OutRead, RowRead... InReads, std::size_t... Is, class Body, std::size_t Capacity, class T,
          class... Es>
[[gnu::noinline]] constexpr void run_row_tiles_(RowReads<OutRead, InReads...> /*reads*/,
                                                std::index_sequence<Is...> /*inputs*/, Body& body,
                                                WalkPlan<1 + sizeof...(Es), Capacity> const& plan,
                                                std::size_t tile_axis, T* out, Es const*... inputs) noexcept {
    constexpr std::size_t views = 1 + sizeof...(Es);
    std::size_t const inner = plan.rank - 1;
    std::size_t const size_a = plan.sizes[inner];
    std::size_t const size_b = plan.sizes[tile_axis];
    std::size_t weight = 1;
    for (std::size_t axis = tile_axis + 1; axis < inner; ++axis) {
        weight *= plan.sizes[axis];
    }
    WalkPlan<views, Capacity> const outer = outer_plan_(plan, inner, tile_axis);
    std::size_t const outer_rows = rows_of_(outer, 1);
    auto const out_view = RowDoor::view<T, Walk, 0, OutRead>(out, plan.strides[0][inner]);
    std::size_t counters[Capacity]{};
    std::size_t outer_offsets[views]{};
    std::size_t offsets[views]{};
    for (std::size_t outer_row = 0; outer_row < outer_rows; ++outer_row) {
        std::size_t const first_index = (outer_row / weight) * size_b * weight + outer_row % weight;
        for (std::size_t b_block = 0; b_block < size_b; b_block += row_tile_extent) {
            std::size_t const b_end = size_b - b_block < row_tile_extent ? size_b : b_block + row_tile_extent;
            for (std::size_t a_start = 0; a_start < size_a; a_start += row_tile_extent) {
                std::size_t const length = size_a - a_start < row_tile_extent ? size_a - a_start : row_tile_extent;
                RowCount<Walk> const count = RowDoor::count<Walk>(length);
                for (std::size_t view = 0; view < views; ++view) {
                    offsets[view] = outer_offsets[view] + b_block * plan.strides[view][tile_axis]
                                  + a_start * plan.strides[view][inner];
                }
                for (std::size_t b = b_block; b < b_end; ++b) {
                    StridedRow<Walk, views> const row =
                        RowDoor::row<Walk, views>(RowDoor::index<Walk>(first_index + b * weight), count, offsets);
                    body(row, out_view,
                         RowDoor::view<Es const, Walk, Is + 1, InReads>(inputs, plan.strides[Is + 1][inner])...);
                    for (std::size_t view = 0; view < views; ++view) {
                        offsets[view] += plan.strides[view][tile_axis];
                    }
                }
            }
        }
        step_rows_(outer, counters, outer_offsets);
    }
}

}  // namespace detail

// The arguments of mint_strided_rows: inputs of the rank parameter of the
// output, and a body that takes a row and a view of each class with no throw
// and gives no value.
template <class Body, class Walk, class T, std::size_t Rank, class... Inputs>
concept CanWalkRows =
    (PointwiseInput<Inputs, Rank> && ...)
    && std::is_nothrow_invocable_v<Body&, StridedRow<Walk, 1 + sizeof...(Inputs)> const&,
                                   RowView<T, Walk, 0, RowRead::Strided>,
                                   RowView<typename Inputs::element_type const, Walk, 1, RowRead::Strided>...>
    && std::is_void_v<std::invoke_result_t<Body&, StridedRow<Walk, 1 + sizeof...(Inputs)> const&,
                                           RowView<T, Walk, 0, RowRead::Strided>,
                                           RowView<typename Inputs::element_type const, Walk, 1, RowRead::Strided>...>>;

namespace detail {

// The body of mint_strided_rows over the plain views: the shape check, the plan
// of write_pointwise, and the reads of the views.
template <class Walk, class Body, class T, std::size_t Rank, std::size_t... Is, class... Es>
[[nodiscard]] constexpr Result<Unit, ShapeMismatch>
walk_rows_(Body& body, std::index_sequence<Is...> sequence, bool const (&apart)[sizeof...(Es) + 1],
           StridedView<T, Rank> const& target, StridedView<Es, Rank> const&... sources) noexcept {
    using Lanes = StridedLanes<Rank>;
    constexpr std::size_t views = 1 + sizeof...(Es);
    Lanes const& shape = strided_lanes_of_(StridedDoor::key(), target);
    if (!(same_sizes_(shape, strided_lanes_of_(StridedDoor::key(), sources)) && ...)) return err(ShapeMismatch{});
    if (target.count() == 0) return Unit{};
    PointwisePlan<views, Lanes::capacity> planned = plan_pointwise_(sequence, apart, target, sources...);
    // THE TILES: a plan with no shared input whose out and an input read
    // different axes with a stride of one, on a plane of at least
    // row_tile_plane positions.  Its out reads the inner axis with a stride of
    // one and an input steps on it, so only such reads take the tiles.  A
    // plan of fewer than two axes has no second axis to tile, and a walk of
    // fewer than row_tile_plane positions has no such plane.
    bool is_tiled = false;
    std::size_t tile_axis = 0;
    if constexpr (Lanes::capacity >= 2) {
        if (!planned.is_shared && planned.plan.rank >= 2 && target.count() >= row_tile_plane) {
            std::size_t const last = planned.plan.rank - 1;
            tile_axis = tile_axis_of_<row_tile_lanes>(planned.plan);
            is_tiled = tile_axis != last && planned.plan.sizes[tile_axis] * planned.plan.sizes[last] >= row_tile_plane;
        }
    }
    // A plan of no axis holds one element: one row of one position.
    if (planned.plan.rank == 0) planned.plan = dense_plan_<views, Lanes::capacity>(1);
    WalkPlan<views, Lanes::capacity> const& plan = planned.plan;
    std::size_t const inner = plan.rank - 1;
    RowRead reads[views]{};
    for (std::size_t view = 0; view < views; ++view) {
        reads[view] = row_read_of_(plan.strides[view][inner]);
    }
    T* const out = strided_first_of_(StridedDoor::key(), target);
    choose_row_reads_<0, views>(
        reads, [&]<RowRead OutRead, RowRead... InReads>(RowReads<OutRead, InReads...> chosen) noexcept {
            if constexpr (Lanes::capacity >= 2 && OutRead == RowRead::Unit && ((InReads == RowRead::Strided) || ...)) {
                if (is_tiled) {
                    run_row_tiles_<Walk>(chosen, sequence, body, plan, tile_axis, out,
                                         strided_first_of_(StridedDoor::key(), sources)...);
                    return;
                }
            }
            run_rows_<Walk>(chosen, sequence, body, plan, out, strided_first_of_(StridedDoor::key(), sources)...);
        });
    return Unit{};
}

}  // namespace detail

// Calls body for each row of the plan of write_pointwise over out and the
// inputs: THE WALK OF ROWS, THE ROW, THE BRANDS and THE PLAN above.  The walk
// gives ShapeMismatch, and calls body for no row, when the sizes of an input
// differ from the sizes of out.  The pack refuses each explicit argument, so
// no caller names the brand.  O(rows) calls of body, O(rank^2 * views) for the
// plan, and O(rank) for the alias check of each input.
template <class... Refused, class T, std::size_t Rank, class Brand, class Body, class... Inputs,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0 && CanWalkRows<Body, Fresh, T, Rank, Inputs...>)
[[nodiscard]] constexpr Result<Unit, ShapeMismatch> mint_strided_rows(StridedViewMut<T, Rank, Brand>& out, Body body,
                                                                      Inputs const&... inputs) noexcept {
    bool const apart[1 + sizeof...(Inputs)] = {false, detail::is_branded_apart_<Inputs, Brand>()...};
    return detail::walk_rows_<Fresh>(body, std::index_sequence_for<Inputs...>{}, apart, detail::plain_view_of_(out),
                                     detail::plain_view_of_(inputs)...);
}

}  // namespace foundation::core
