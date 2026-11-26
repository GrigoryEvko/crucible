// Tests of the strided views of the Region family (foundation/core/Strided.h):
// the door, the operations on the shape, the walk and the scope.
//
// The properties compare each view with a naive model.  The model holds a
// layout: the rank, the sizes, the strides and the offset.  It gives the
// offsets of the elements in row-major order with an odometer and one sum for
// each index.  Each element of the run of the properties holds its own
// position, so a walk that reads an element reads its offset.  A property
// then compares the order of the visits, and not only the elements.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Property.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::fixy::check::Draw;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;
using ::fixy::check::property;

// The count of the elements of the run of the properties.
constexpr std::size_t kRun = 512;
// The largest count of the elements of a drawn layout.
constexpr std::size_t kMaxCount = 256;
// The largest stride of a drawn layout.  A layout of at most kMaxCount
// elements and of this stride spans fewer than kRun elements.
constexpr std::size_t kMaxStride = 9;
constexpr std::size_t kLanes = ::fixy::max_strided_rank;
// The seed of each property.
constexpr std::uint64_t kSeed = 0x5752494445445649u;
// The count of the runs of each property.
constexpr std::uint32_t kRuns = 600;

using Run = ::fixy::FixedArray<std::uint32_t, kRun>;
using Lanes = ::fixy::FixedArray<std::size_t, kLanes>;
using SharedView = ::fixy::StridedView<std::uint32_t const>;

// The run of the properties: each element holds its position.
[[nodiscard]] Run make_run() noexcept {
    Run run{};
    for (::fixy::Index<kRun> const position : run.indices()) {
        std::uint32_t const value = static_cast<std::uint32_t>(position.raw());
        run.set(position, value);
    }
    return run;
}

// The lane of an axis below the lane count.
[[nodiscard]] std::size_t lane(Lanes const& lanes, std::size_t axis) noexcept {
    return *lanes.get(axis).expect("an axis is below the lane count");
}

// Writes value into the lane of an axis below the lane count.
void set_lane(Lanes& lanes, std::size_t axis, std::size_t value) noexcept {
    lanes.set(::fixy::Index<kLanes>::check(axis).expect("an axis is below the lane count"), value);
}

// The offsets of the visits of a walk, in order.
struct Sequence {
    ::fixy::FixedArray<std::uint32_t, kMaxCount> offsets{};
    std::size_t count = 0;
};

void push(Sequence& sequence, std::size_t offset) noexcept {
    std::uint32_t const value = static_cast<std::uint32_t>(offset);
    sequence.offsets.set(
        ::fixy::Index<kMaxCount>::check(sequence.count).expect("a sequence holds at most kMaxCount offsets"), value);
    ++sequence.count;
}

[[nodiscard]] std::uint32_t offset_at(Sequence const& sequence, std::size_t position) noexcept {
    return *sequence.offsets.get(position).expect("a position is below the count of the sequence");
}

[[nodiscard]] bool same_sequence(Sequence const& left, Sequence const& right) noexcept {
    if (left.count != right.count) return false;
    for (std::size_t position = 0; position < left.count; ++position) {
        if (offset_at(left, position) != offset_at(right, position)) return false;
    }
    return true;
}

// The layout of a model view.
struct Layout {
    std::size_t rank = 0;
    Lanes sizes{};
    Lanes strides{};
    std::size_t offset = 0;
};

[[nodiscard]] std::size_t count_of(Layout const& layout) noexcept {
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        count *= lane(layout.sizes, axis);
    }
    return count;
}

[[nodiscard]] std::size_t span_of(Layout const& layout) noexcept {
    if (count_of(layout) == 0) return 0;
    std::size_t span = 0;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        span += (lane(layout.sizes, axis) - 1) * lane(layout.strides, axis);
    }
    return span;
}

// The naive model: each index in row-major order, with an odometer, and the
// offset of each index as one sum.
[[nodiscard]] Sequence model_sequence(Layout const& layout) noexcept {
    Sequence sequence{};
    if (count_of(layout) == 0) return sequence;
    Lanes index{};
    while (true) {
        std::size_t offset = layout.offset;
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            offset += lane(index, axis) * lane(layout.strides, axis);
        }
        push(sequence, offset);
        std::size_t axis = layout.rank;
        while (true) {
            if (axis == 0) return sequence;
            --axis;
            set_lane(index, axis, lane(index, axis) + 1);
            if (lane(index, axis) < lane(layout.sizes, axis)) break;
            set_lane(index, axis, 0);
        }
    }
}

[[nodiscard]] ::fixy::View<std::size_t const> sizes_of(Layout const& layout CRUCIBLE_LIFETIMEBOUND) noexcept {
    return layout.sizes.view().window(0, layout.rank).expect("the rank is at most the lane count");
}
void sizes_of(Layout const&&) = delete("the View borrows the layout, and a temporary ends with the full expression");
[[nodiscard]] ::fixy::View<std::size_t const> strides_of(Layout const& layout CRUCIBLE_LIFETIMEBOUND) noexcept {
    return layout.strides.view().window(0, layout.rank).expect("the rank is at most the lane count");
}
void strides_of(Layout const&&) = delete("the View borrows the layout, and a temporary ends with the full expression");

[[nodiscard]] ::fixy::Option<SharedView> view_of(Run const& run CRUCIBLE_LIFETIMEBOUND, Layout const& layout) noexcept {
    return ::fixy::mint_strided_view(run.view(), sizes_of(layout), strides_of(layout), layout.offset);
}
void view_of(Run const&&, Layout const&) = delete("the view borrows the run, and a temporary ends with the full "
                                                  "expression");

// The sequence of a walk over one view.
[[nodiscard]] Sequence walk_sequence(SharedView view) noexcept {
    Sequence sequence{};
    bool const is_walked =
        ::fixy::walk([&sequence](std::uint32_t const& element) noexcept { push(sequence, element); }, view).is_ok();
    if (!is_walked) ::fixy::fatal("a walk over one view has one shape");
    return sequence;
}

// The sequence of the indices of the scope of the view, read with at().  The
// cursor of the scope merges no axis, so it is the path with no merge.
[[nodiscard]] Sequence scope_sequence(SharedView view) noexcept {
    return ::fixy::mint_strided_scope(view, [](auto branded) noexcept {
        Sequence sequence{};
        for (auto const index : branded.indices()) {
            push(sequence, branded.at(index));
        }
        return sequence;
    });
}

// The sequence of the walk over the branded view of the scope of the view.
// The walk takes the plan of walk(), so it merges the axes as the walk of the
// plain view does.
[[nodiscard]] Sequence scope_walk_sequence(SharedView view) noexcept {
    return ::fixy::mint_strided_scope(view, [](auto branded) noexcept {
        Sequence sequence{};
        ::fixy::walk([&sequence](std::uint32_t const& element) noexcept { push(sequence, element); }, branded);
        return sequence;
    });
}

// A layout of at most kMaxCount elements and of rank at most kLanes.  One
// third of the layouts have the strides of the row-major order, so the dense
// loop of the walk and the merge of axes get runs too.
[[nodiscard]] Layout draw_shape(Draw& draw) noexcept {
    Layout layout{};
    layout.rank = draw.integer<std::size_t>(0, kLanes);
    std::size_t count = 1;
    for (std::size_t axis = 0; axis < layout.rank; ++axis) {
        std::size_t size = draw.integer<std::size_t>(0, 4);
        if (count * size > kMaxCount) size = 1;
        count *= size;
        set_lane(layout.sizes, axis, size);
        set_lane(layout.strides, axis, draw.integer<std::size_t>(0, kMaxStride));
    }
    if (draw.integer<std::size_t>(0, 2) == 0) {
        std::size_t stride = 1;
        for (std::size_t axis = layout.rank; axis-- > 0;) {
            set_lane(layout.strides, axis, stride);
            stride *= lane(layout.sizes, axis) == 0 ? 1 : lane(layout.sizes, axis);
        }
    }
    return layout;
}

// A layout whose elements lie inside the run.
[[nodiscard]] Layout draw_fitting_layout(Draw& draw) noexcept {
    Layout layout = draw_shape(draw);
    layout.offset = draw.integer<std::size_t>(0, kRun - 1 - span_of(layout));
    return layout;
}

// True when the view gives the sequence of the model on each path: the walk,
// the indices of the scope, and the walk over the branded view of the scope.
[[nodiscard]] bool agrees_with_model(SharedView view, Layout const& layout) noexcept {
    Sequence const model = model_sequence(layout);
    return view.count() == count_of(layout) && view.rank() == layout.rank && same_sequence(walk_sequence(view), model)
        && same_sequence(scope_sequence(view), model) && same_sequence(scope_walk_sequence(view), model);
}

// ── Unit tests ──────────────────────────────────────────────────────────

// A 3 x 4 matrix in row-major order, its transpose, and its reads.
void matrix_and_transpose(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(
        ::fixy::fixed_array_owner<int, 12>, ::fixy::FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::StridedView<int const, 2> const matrix =
        ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("the matrix fits");
    CRUCIBLE_TEST_ASSERT(test, matrix.count() == 12 && matrix.rank() == 2 && matrix.is_dense());
    CRUCIBLE_TEST_ASSERT(test, matrix.size(::fixy::index<0>()) == 3 && matrix.stride(::fixy::index<1>()) == 1);

    ::fixy::StridedView<int const, 2> const columns = matrix.transpose(::fixy::index<0>(), ::fixy::index<1>());
    CRUCIBLE_TEST_ASSERT(test, !columns.is_dense() && columns.size(::fixy::index<0>()) == 4);
    ::fixy::FixedArray<int, 12> order{};
    std::size_t visits = 0;
    bool const is_walked = ::fixy::walk(
                               [&order, &visits](int const& element) noexcept {
                                   order.set(::fixy::Index<12>::check(visits).expect("twelve elements"), element);
                                   ++visits;
                               },
                               ::fixy::StridedView<int const, 2>{columns})
                               .is_ok();
    CRUCIBLE_TEST_ASSERT(test, is_walked && visits == 12);
    CRUCIBLE_TEST_ASSERT(test, order == (::fixy::FixedArray<int, 12>{0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11}));

    // A write through the walk reaches the run, and the second view of the
    // pair reads each element at the same index.
    bool const is_added = values.with_mut(values_key, [&sizes, &strides](auto run) noexcept {
        auto unique_writer =
            ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the matrix fits");
        auto writer_columns = std::move(unique_writer).transpose(::fixy::index<0>(), ::fixy::index<1>());
        return ::fixy::walk([](int& target, int const& source) noexcept { target += source * 100; }, writer_columns,
                            writer_columns.view())
            .is_ok();
    });
    ::fixy::FixedArray<int, 12> const added{0, 101, 202, 303, 404, 505, 606, 707, 808, 909, 1010, 1111};
    CRUCIBLE_TEST_ASSERT(test, is_added && ::fixy::equal(values.view(), added.view()));
    pass(test, "  matrix_and_transpose: PASSED\n");
}

// A walk of three views of 2 x 4 elements gives the sum of the two inputs at
// each index, for each step of one or two on the inner axis of each view.  A
// dense output takes the loop with the read of each input as a template
// argument, so each of its four loops runs, and an output of a step of two
// takes the loop of no view known dense.
void walk_reads_of_each_mix(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 16> left{};
    ::fixy::FixedArray<int, 16> right{};
    for (std::size_t position = 0; position < 16; ++position) {
        int const left_value = static_cast<int>(position);
        int const right_value = static_cast<int>(100 * position);
        left.set(::fixy::Index<16>::check(position).expect("sixteen elements"), left_value);
        right.set(::fixy::Index<16>::check(position).expect("sixteen elements"), right_value);
    }
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 4};
    auto const view_of = [&sizes](auto run, std::size_t step) noexcept {
        ::fixy::FixedArray<std::size_t, 2> const strides{4 * step, step};
        return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the view fits");
    };
    auto const unique_view_of = [&sizes](auto run, std::size_t step) noexcept {
        ::fixy::FixedArray<std::size_t, 2> const strides{4 * step, step};
        return ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the view fits");
    };
    std::size_t mixes = 0;
    for (std::size_t mix = 0; mix < 8; ++mix) {
        std::size_t const out_step = 1 + (mix & 1);
        std::size_t const left_step = 1 + ((mix >> 1) & 1);
        std::size_t const right_step = 1 + ((mix >> 2) & 1);
        auto [out, out_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 16>);
        bool const is_walked = out.with_mut(out_key, [&](auto run) noexcept {
            auto unique_out = unique_view_of(std::move(run), out_step);
            return ::fixy::walk([](int& sum, int const& first, int const& second) noexcept { sum = first + second; },
                                unique_out, ::fixy::StridedView<int const, 2>{view_of(left.view(), left_step)},
                                ::fixy::StridedView<int const, 2>{view_of(right.view(), right_step)})
                .is_ok();
        });
        bool is_summed = is_walked;
        for (std::size_t row = 0; row < 2; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                int const expected = *left.get(row * 4 * left_step + column * left_step).expect("inside")
                                   + *right.get(row * 4 * right_step + column * right_step).expect("inside");
                is_summed = is_summed && *out.get(row * 4 * out_step + column * out_step).expect("inside") == expected;
            }
        }
        CRUCIBLE_TEST_ASSERT(test, is_summed);
        ++mixes;
    }
    CRUCIBLE_TEST_ASSERT(test, mixes == 8);
    pass(test, "  walk_reads_of_each_mix: PASSED\n");
}

// The door refuses each layout that leaves the run or wraps.
void door_refusals(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 8> values{};
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::FixedArray<std::size_t, 1> const one_stride{1};
    ::fixy::FixedArray<std::size_t, 2> const huge_sizes{::fixy::max_strided_count, 2};
    ::fixy::FixedArray<std::size_t, 2> const past_sizes{::fixy::max_strided_count + 1, 0};
    ::fixy::FixedArray<std::size_t, 9> const nine{1, 1, 1, 1, 1, 1, 1, 1, 1};
    ::fixy::View<std::size_t const> const dynamic_sizes = sizes.view();
    ::fixy::View<std::size_t const> const dynamic_one = one_stride.view();
    ::fixy::View<std::size_t const> const dynamic_nine = nine.view();

    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).is_some());
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 1).is_none());
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(values.view(), dynamic_sizes, dynamic_one, 0).is_none());
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::mint_strided_view(values.view(), huge_sizes.view(), strides.view(), 0).is_none());
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::mint_strided_view(values.view(), past_sizes.view(), strides.view(), 0).is_none());
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(values.view(), dynamic_nine, dynamic_nine, 0).is_none());

    // A stride that wraps the span is refused, and a view with no element
    // reads no offset and no stride.
    ::fixy::FixedArray<std::size_t, 2> const wrapping{::fixy::max_strided_count, 1};
    ::fixy::FixedArray<std::size_t, 2> const empty_sizes{0, 4};
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(values.view(), sizes.view(), wrapping.view(), 0).is_none());
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::mint_strided_view(values.view(), empty_sizes.view(), wrapping.view(), 99).is_some());

    // An empty run holds only views with no element.
    ::fixy::View<int> const no_run{};
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(no_run, sizes.view(), strides.view(), 0).is_none());
    CRUCIBLE_TEST_ASSERT(test, ::fixy::mint_strided_view(no_run, empty_sizes.view(), strides.view(), 0).is_some());
    pass(test, "  door_refusals: PASSED\n");
}

// The cases of the rule of reshape.
void reshape_cases(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 12> values{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::FixedArray<std::size_t, 1> const flat{12};
    ::fixy::FixedArray<std::size_t, 3> const split{3, 2, 2};
    ::fixy::FixedArray<std::size_t, 1> const eleven{11};
    ::fixy::FixedArray<std::size_t, 9> const nine{12, 1, 1, 1, 1, 1, 1, 1, 1};
    ::fixy::View<std::size_t const> const dynamic_nine = nine.view();
    ::fixy::StridedView<int const, 2> const matrix =
        ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("the matrix fits");

    ::fixy::StridedView<int const, 1> const line =
        matrix.reshape(flat.view()).value_or(matrix.select(::fixy::index<0>(), 0).expect("row 0 exists"));
    CRUCIBLE_TEST_ASSERT(test, line.count() == 12 && line.stride(::fixy::index<0>()) == 1);

    ::fixy::StridedView<int const, 3> const blocks =
        matrix.reshape(split.view()).ok().expect("a split of a dense axis");
    CRUCIBLE_TEST_ASSERT(test, blocks.stride(::fixy::index<0>()) == 4 && blocks.stride(::fixy::index<1>()) == 2
                                   && blocks.stride(::fixy::index<2>()) == 1);

    ::fixy::StridedView<int const, 2> const columns = matrix.transpose(::fixy::index<0>(), ::fixy::index<1>());
    CRUCIBLE_TEST_ASSERT(test, columns.reshape(flat.view()).err().expect("a transpose is no run")
                                   == ::fixy::ReshapeError::NotAView);
    CRUCIBLE_TEST_ASSERT(test, matrix.reshape(eleven.view()).err().expect("eleven is not twelve")
                                   == ::fixy::ReshapeError::CountMismatch);
    CRUCIBLE_TEST_ASSERT(test, matrix.reshape(dynamic_nine).err().expect("nine axes are past the limit")
                                   == ::fixy::ReshapeError::RankPastLimit);

    // A broadcast column is no run of twelve elements, and a split of its
    // broadcast axis keeps the stride of zero.
    ::fixy::FixedArray<std::size_t, 2> const column_sizes{3, 1};
    ::fixy::FixedArray<std::size_t, 2> const column_strides{4, 1};
    ::fixy::FixedArray<std::size_t, 3> const widened{3, 2, 2};
    ::fixy::StridedView<int const, 2> const column =
        ::fixy::StridedView<int const, 2>{
            ::fixy::mint_strided_view(values.view(), column_sizes.view(), column_strides.view(), 0).expect("fits")}
            .broadcast_to(sizes.view())
            .expect("a column of size one broadcasts");
    CRUCIBLE_TEST_ASSERT(test, column.stride(::fixy::index<1>()) == 0 && column.count() == 12);
    CRUCIBLE_TEST_ASSERT(test, column.reshape(flat.view()).err().expect("a broadcast is no run")
                                   == ::fixy::ReshapeError::NotAView);
    ::fixy::StridedView<int const, 3> const widened_view = column.reshape(widened.view()).ok().expect("a split");
    CRUCIBLE_TEST_ASSERT(test,
                         widened_view.stride(::fixy::index<1>()) == 0 && widened_view.stride(::fixy::index<2>()) == 0);

    // A view with no element takes the strides of the row-major order.
    ::fixy::FixedArray<std::size_t, 2> const empty_sizes{0, 4};
    ::fixy::FixedArray<std::size_t, 3> const empty_target{2, 0, 3};
    ::fixy::StridedView<int const, 3> const empty =
        ::fixy::mint_strided_view(values.view(), empty_sizes.view(), strides.view(), 0)
            .expect("no element")
            .reshape(empty_target.view())
            .ok()
            .expect("a view with no element takes each shape of no element");
    CRUCIBLE_TEST_ASSERT(test, empty.stride(::fixy::index<0>()) == 3 && empty.stride(::fixy::index<1>()) == 3
                                   && empty.stride(::fixy::index<2>()) == 1);
    pass(test, "  reshape_cases: PASSED\n");
}

// shares_bytes of a strided view counts each byte from its first element to
// its last one: the elements 10, 13, 16 and 19 of a run reach from element 10
// to element 19.  A view with no element and a run with no element share no
// byte.
void shares_bytes_from_the_first_element_to_the_last(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    ::fixy::View<std::uint32_t const> const all{run.view()};
    ::fixy::FixedArray<std::size_t, 1> const sizes{4};
    ::fixy::FixedArray<std::size_t, 1> const none{0};
    ::fixy::FixedArray<std::size_t, 1> const strides{3};
    ::fixy::StridedView<std::uint32_t const, 1> const strided =
        ::fixy::mint_strided_view(all, sizes.view(), strides.view(), 10).expect("the view fits the run");
    ::fixy::StridedView<std::uint32_t const, 1> const empty =
        ::fixy::mint_strided_view(all, none.view(), strides.view(), 10).expect("an empty view fits the run");
    CRUCIBLE_TEST_ASSERT(test, ::fixy::shares_bytes(strided, all));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::shares_bytes(strided, all.window(10, 1).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::shares_bytes(strided, all.window(19, 1).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, ::fixy::shares_bytes(strided, all.window(11, 2).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, !::fixy::shares_bytes(strided, all.window(0, 10).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, !::fixy::shares_bytes(strided, all.window(20, 5).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, !::fixy::shares_bytes(strided, all.window(12, 0).expect("the window fits")));
    CRUCIBLE_TEST_ASSERT(test, !::fixy::shares_bytes(empty, all));
}

// A view of rank zero holds one element, and a view with a size of zero
// holds none.
void rank_zero_and_empty(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 4> values{7, 8, 9, 10};
    ::fixy::View<std::size_t const> const no_axes{};
    ::fixy::StridedView<int const> const scalar =
        ::fixy::mint_strided_view(values.view(), no_axes, no_axes, 2).expect("one element fits");
    int total = 0;
    bool const is_walked = ::fixy::walk([&total](int const& element) noexcept { total += element; }, scalar).is_ok();
    CRUCIBLE_TEST_ASSERT(test, is_walked && total == 9 && scalar.count() == 1 && scalar.rank() == 0);
    ::fixy::StridedView<int const, 0> const fixed = scalar.ranked<0>().expect("the rank is zero");
    CRUCIBLE_TEST_ASSERT(test, fixed.count() == 1 && scalar.ranked<1>().is_none());

    ::fixy::FixedArray<std::size_t, 1> const four{4};
    ::fixy::FixedArray<std::size_t, 1> const unit{1};
    ::fixy::StridedView<int const, 1> const line =
        ::fixy::mint_strided_view(values.view(), four.view(), unit.view(), 0).expect("four elements fit");
    ::fixy::StridedView<int const, 0> const third = line.select(::fixy::index<0>(), 3).expect("position 3 exists");
    total = 0;
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::walk([&total](int const& element) noexcept { total += element; }, third).is_ok());
    CRUCIBLE_TEST_ASSERT(test, total == 10 && line.select(::fixy::index<0>(), 4).is_none());

    std::size_t const turns = ::fixy::mint_strided_scope(fixed, [](auto branded) noexcept {
        std::size_t count = 0;
        for ([[maybe_unused]] auto const index : branded.indices()) {
            ++count;
        }
        return count;
    });
    CRUCIBLE_TEST_ASSERT(test, turns == 1);

    // An empty view: no visit, no index, and an empty slice of the other
    // axis.
    ::fixy::FixedArray<std::size_t, 2> const empty_sizes{2, 0};
    ::fixy::FixedArray<std::size_t, 2> const strides{1, 1};
    ::fixy::StridedView<int const, 2> const empty =
        ::fixy::mint_strided_view(values.view(), empty_sizes.view(), strides.view(), 0).expect("no element");
    std::size_t visits = 0;
    CRUCIBLE_TEST_ASSERT(test,
                         ::fixy::walk([&visits](int const&) noexcept { ++visits; }, empty).is_ok() && visits == 0);
    ::fixy::StridedView<int const, 2> const slice =
        empty.slice(::fixy::index<0>(), 1, 1).expect("one position of two fits");
    CRUCIBLE_TEST_ASSERT(test, slice.count() == 0 && slice.is_dense());
    pass(test, "  rank_zero_and_empty: PASSED\n");
}

// A walk over views of different sizes calls the body for no index, and
// an axis of a dynamic rank past the rank gives no value.
void mismatch_and_dynamic_axes(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 12> values{};
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const other_sizes{4, 3};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    ::fixy::FixedArray<std::size_t, 2> const other_strides{3, 1};
    ::fixy::FixedArray<std::size_t, 2> const repeated{1, 1};
    ::fixy::FixedArray<std::size_t, 2> const swapped{1, 0};
    ::fixy::View<std::size_t const> const dynamic_sizes = sizes.view();
    ::fixy::View<std::size_t const> const dynamic_strides = strides.view();
    ::fixy::StridedView<int const> const matrix =
        ::fixy::mint_strided_view(values.view(), dynamic_sizes, dynamic_strides, 0).expect("the matrix fits");
    ::fixy::StridedView<int const> const other =
        ::fixy::mint_strided_view(values.view(), other_sizes.view(), other_strides.view(), 0).expect("it fits");
    std::size_t visits = 0;
    bool const is_refused =
        ::fixy::walk([&visits](int const&, int const&) noexcept { ++visits; }, matrix, other).is_err() && visits == 0;
    CRUCIBLE_TEST_ASSERT(test, is_refused);

    CRUCIBLE_TEST_ASSERT(test, matrix.transpose(0, 2).is_none() && matrix.transpose(1, 0).is_some());
    CRUCIBLE_TEST_ASSERT(test, matrix.slice(2, 0, 1).is_none() && matrix.select(2, 0).is_none());
    CRUCIBLE_TEST_ASSERT(test, matrix.permute(repeated.view()).is_none());
    CRUCIBLE_TEST_ASSERT(test, matrix.permute(swapped.view()).is_some());
    CRUCIBLE_TEST_ASSERT(test, matrix.slice(0, 0, 2, 0).is_none() && matrix.slice(0, 1, 2, 2).is_none());
    CRUCIBLE_TEST_ASSERT(test, matrix.slice(0, 0, 2, 2).is_some() && matrix.slice(0, 3, 0).is_some());
    pass(test, "  mismatch_and_dynamic_axes: PASSED\n");
}

// The scope of the unique view of an output writes the output, and one index
// of the scope reads each parallel view.
void scope_and_parallel(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<int, 6> left{1, 2, 3, 4, 5, 6};
    ::fixy::FixedArray<int, 6> right{10, 20, 30, 40, 50, 60};
    auto [output, output_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 6>);
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::fixy::FixedArray<std::size_t, 2> const row_major{3, 1};
    ::fixy::FixedArray<std::size_t, 2> const column_major{1, 2};
    ::fixy::FixedArray<std::size_t, 2> const wrong_sizes{3, 2};
    ::fixy::StridedView<int const, 2> const left_view =
        ::fixy::mint_strided_view(left.view(), sizes.view(), row_major.view(), 0).expect("fits");
    ::fixy::StridedView<int const, 2> const right_view =
        ::fixy::mint_strided_view(right.view(), sizes.view(), column_major.view(), 0).expect("fits");
    // A view of the sizes 3 by 2 over a different array: parallel() refuses it on
    // its sizes.
    ::fixy::StridedView<int const, 2> const wrong_view =
        ::fixy::mint_strided_view(right.view(), wrong_sizes.view(), column_major.view(), 0).expect("fits");

    bool const is_summed = output.with_mut(output_key, [&](auto run) noexcept {
        auto unique_output =
            ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), row_major.view(), 0).expect("fits");
        return ::fixy::mint_strided_scope(unique_output, [&](auto target) noexcept {
            if (target.parallel(wrong_view).is_some()) return false;
            if (target.index(std::size_t{2}, std::size_t{0}).is_some()) return false;
            if (target.index(std::size_t{1}, std::size_t{3}).is_some()) return false;
            auto const lefts = target.parallel(left_view).expect("the sizes are the same");
            auto const others = target.parallel(right_view).expect("the sizes are the same");
            for (auto const index : target.indices()) {
                target.at(index) = lefts.at(index) + others.at(index);
            }
            auto const corner = target.index(std::size_t{1}, std::size_t{2}).expect("the corner is inside");
            return lefts.at(corner) == 6 && others.at(corner) == 60;
        });
    });
    // The right view reads the run in column-major order, so its element at
    // (i, j) is the element at the position i + 2 * j.
    ::fixy::FixedArray<int, 6> const sums{11, 32, 53, 24, 45, 66};
    CRUCIBLE_TEST_ASSERT(test, is_summed && ::fixy::equal(output.view(), sums.view()));

    // The walk over the branded views of the scope visits the indices in
    // row-major order and writes the same sums, with no compare of the sizes.
    auto [walked, walked_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<int, 6>);
    ::fixy::FixedArray<int, 6> order{};
    std::size_t const visits = walked.with_mut(walked_key, [&](auto run) noexcept {
        auto unique_walked =
            ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), column_major.view(), 0).expect("fits");
        return ::fixy::mint_strided_scope(unique_walked, [&](auto target) noexcept {
            auto const lefts = target.parallel(left_view).expect("the sizes are the same");
            auto const others = target.parallel(right_view).expect("the sizes are the same");
            std::size_t visited = 0;
            ::fixy::walk(
                [&order, &visited](int& sum, int const& first, int const& second) noexcept {
                    sum = first + second;
                    order.set(::fixy::Index<6>::check(visited).expect("six elements"), first);
                    ++visited;
                },
                target, lefts, others);
            return visited;
        });
    });
    // The output is column-major, so its element at (i, j) is at the
    // position i + 2 * j.
    CRUCIBLE_TEST_ASSERT(test, visits == 6 && order == (::fixy::FixedArray<int, 6>{1, 2, 3, 4, 5, 6}));
    ::fixy::FixedArray<int, 6> const walked_sums{11, 24, 32, 45, 53, 66};
    CRUCIBLE_TEST_ASSERT(test, ::fixy::equal(walked.view(), walked_sums.view()));
    pass(test, "  scope_and_parallel: PASSED\n");
}

// True when the unique door gives a view of the 3 x 4 run of a frame for the
// sizes and the strides.
[[nodiscard]] bool mints_unique(::fixy::View<std::size_t const> sizes, ::fixy::View<std::size_t const> strides,
                                std::size_t offset) noexcept {
    auto const [is_minted, values] = ::fixy::build_fixed_array<int, 12>([sizes, strides, offset](auto run) noexcept {
        return ::fixy::mint_strided_view_mut(std::move(run), sizes, strides, offset).is_some();
    });
    return is_minted;
}

// The unique door refuses each layout in which two indexes can name one
// element, and a layout that does not fit, and it takes each other layout.
void unique_door_cases(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const row_major{4, 1};
    ::fixy::FixedArray<std::size_t, 2> const column_major{1, 3};
    ::fixy::FixedArray<std::size_t, 2> const repeated{0, 1};
    ::fixy::FixedArray<std::size_t, 2> const overlapping{2, 1};
    ::fixy::FixedArray<std::size_t, 2> const small_sizes{3, 2};
    ::fixy::FixedArray<std::size_t, 2> const interleaved{2, 3};
    ::fixy::FixedArray<std::size_t, 2> const empty_sizes{0, 4};
    ::fixy::FixedArray<std::size_t, 2> const one_row{1, 4};
    CRUCIBLE_TEST_ASSERT(test, mints_unique(sizes.view(), row_major.view(), 0));
    CRUCIBLE_TEST_ASSERT(test, mints_unique(sizes.view(), column_major.view(), 0));
    CRUCIBLE_TEST_ASSERT(test, !mints_unique(sizes.view(), row_major.view(), 1));
    CRUCIBLE_TEST_ASSERT(test, !mints_unique(sizes.view(), repeated.view(), 0));
    CRUCIBLE_TEST_ASSERT(test, !mints_unique(sizes.view(), overlapping.view(), 0));
    // The offsets of sizes 3 and 2 with strides 2 and 3 are 0, 3, 2, 5, 4
    // and 7: no overlap, but the stride 3 is not above the span 4 of the axis
    // of stride 2, so the sufficient test refuses it.
    CRUCIBLE_TEST_ASSERT(test, !mints_unique(small_sizes.view(), interleaved.view(), 0));
    // An axis of size one and a view with no element have no overlap at each
    // stride.
    CRUCIBLE_TEST_ASSERT(test, mints_unique(one_row.view(), repeated.view(), 0));
    CRUCIBLE_TEST_ASSERT(test, mints_unique(empty_sizes.view(), repeated.view(), 99));
    pass(test, "  unique_door_cases: PASSED\n");
}

// A unique view writes through the walk, and a transpose keeps each index on
// its own element.  A unique view of a different layout of the same run comes
// from the mint, after a reborrow gives the run back.
void unique_view_writes(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(
        ::fixy::fixed_array_owner<int, 12>, ::fixy::FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    // Columns 1 and 3 of the matrix: the rows 1 and 3 of the transpose.
    ::fixy::FixedArray<std::size_t, 2> const odd_sizes{2, 3};
    ::fixy::FixedArray<std::size_t, 2> const odd_strides{2, 4};
    bool const is_written = values.with_mut(values_key, [&](auto run) noexcept {
        auto [is_scaled, back] = ::fixy::reborrow(std::move(run), [&sizes, &strides](auto lent) noexcept {
            auto unique = ::fixy::mint_strided_view_mut(std::move(lent), sizes.view(), strides.view(), 0)
                              .expect("a dense matrix has no overlap");
            bool const is_shaped = unique.count() == 12 && unique.rank() == 2 && unique.is_dense()
                                && unique.size(::fixy::index<1>()) == 4 && unique.stride(::fixy::index<0>()) == 4;
            auto columns = std::move(unique).transpose(::fixy::index<0>(), ::fixy::index<1>());
            bool const is_walked = ::fixy::walk([](int& element) noexcept { element *= 10; }, columns).is_ok();
            return is_shaped && is_walked && !columns.is_dense() && columns.stride(::fixy::index<1>()) == 4;
        });
        auto odd = ::fixy::mint_strided_view_mut(std::move(back), odd_sizes.view(), odd_strides.view(), 1)
                       .expect("columns 1 and 3 fit");
        bool const is_added = ::fixy::walk([](int& element) noexcept { element += 1; }, odd).is_ok();
        return is_scaled && is_added;
    });
    ::fixy::FixedArray<int, 12> const expected{0, 11, 20, 31, 40, 51, 60, 71, 80, 91, 100, 111};
    CRUCIBLE_TEST_ASSERT(test, is_written && ::fixy::equal(values.view(), expected.view()));
    pass(test, "  unique_view_writes: PASSED\n");
}

// The erased writer of a unique view keeps its layout and writes the run of
// its frame.  A scope on the erased writer writes each element in row-major
// order, and the frame holds the bits after the door gives the run back.
void erased_writer_writes(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(
        ::fixy::fixed_array_owner<int, 12>, ::fixy::FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const column_major{1, 3};
    bool const is_written = values.with_mut(values_key, [&sizes, &column_major](auto run) noexcept {
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), column_major.view(), 0)
                          .expect("a column-major matrix has no overlap");
        auto erased = ::fixy::erase_brand(std::move(unique));
        bool const is_shaped = erased.count() == 12 && erased.rank() == 2 && !erased.is_dense()
                            && erased.size(::fixy::index<0>()) == 3 && erased.stride(::fixy::index<1>()) == 3;
        std::size_t const visits = ::fixy::mint_strided_scope(erased, [](auto target) noexcept {
            std::size_t visit = 0;
            for (auto const index : target.indices()) {
                target.at(index) = target.at(index) * 100 + static_cast<int>(visit);
                ++visit;
            }
            return visit;
        });
        return is_shaped && visits == 12;
    });
    // The element at (i, j) is at the position i + 3 * j, and the scope visits
    // it as the visit 4 * i + j.
    ::fixy::FixedArray<int, 12> const expected{0, 104, 208, 301, 405, 509, 602, 706, 810, 903, 1007, 1111};
    CRUCIBLE_TEST_ASSERT(test, is_written && ::fixy::equal(values.view(), expected.view()));
    pass(test, "  erased_writer_writes: PASSED\n");
}

// Gives door one unique view of a 3 x 4 matrix through two parameters, so the
// rule use-after-move does not see that the second one moved.  The sizes and
// the strides of a fixed extent give a view of rank 2, and the ones of a
// dynamic extent give a view of a dynamic rank.  The kind two-names of the rule
// view-escape refuses each call that gives one unique view to two parameters,
// so each such call of a test stands in a PROBE region.
template <std::size_t Extent, class Door>
void check_old_name(Door door) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{3, 4};
    ::fixy::FixedArray<std::size_t, 2> const strides{4, 1};
    static_cast<void>(::fixy::build_fixed_array<int, 12>([&sizes, &strides, door](auto run) noexcept {
        ::fixy::View<std::size_t const, Extent> const shape{sizes.view()};
        ::fixy::View<std::size_t const, Extent> const steps{strides.view()};
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), shape, steps, 0).expect("the matrix fits");
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        door(unique, unique);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }));
}

// A move, and each door that takes a unique view by rvalue reference, leaves no
// element under the old name.  A self move keeps the elements.  The mint of a
// unique view leaves no run under the name of its ViewMut.
void old_name_of_a_unique_view(::fixy::TestRunnerCtx const& test) noexcept {
    auto const move_out = [&test](auto& view, auto& old_name) noexcept {
        auto const moved = std::move(view);
        CRUCIBLE_TEST_ASSERT(test, moved.count() == 12 && moved.rank() == 2);
        CRUCIBLE_TEST_ASSERT(test, old_name.count() == 0 && old_name.view().count() == 0);
    };
    check_old_name<2>(move_out);
    check_old_name<::fixy::dynamic_extent>(move_out);
    auto const move_over = [&test](auto& view, auto& old_name) noexcept {
        auto moved = std::move(view);
        auto const move_into = [&test](auto& target, auto& source, auto& source_name) noexcept {
            target = std::move(source);
            CRUCIBLE_TEST_ASSERT(test, target.count() == 12 && source_name.count() == 0);
        };
        auto const move_into_itself = [&test](auto& target, auto& same) noexcept {
            target = std::move(same);
            CRUCIBLE_TEST_ASSERT(test, target.count() == 12);
        };
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        move_into(old_name, moved, moved);
        move_into_itself(old_name, old_name);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    };
    check_old_name<2>(move_over);
    check_old_name<::fixy::dynamic_extent>(move_over);
    check_old_name<2>([&test](auto& view, auto& old_name) noexcept {
        auto const columns = std::move(view).transpose(::fixy::index<0>(), ::fixy::index<1>());
        CRUCIBLE_TEST_ASSERT(test, columns.count() == 12 && old_name.count() == 0);
    });
    auto const erase = [&test](auto& view, auto& old_name) noexcept {
        auto const erased = ::fixy::erase_brand(std::move(view));
        CRUCIBLE_TEST_ASSERT(test, erased.count() == 12 && old_name.count() == 0);
    };
    check_old_name<2>(erase);
    check_old_name<::fixy::dynamic_extent>(erase);

    static_cast<void>(::fixy::build_fixed_array<int, 12>([&test](auto run) noexcept {
        ::fixy::View<int const> const elements = run.view();
        auto const mint_and_read = [&test, elements](auto& view, auto& old_name) noexcept {
            ::fixy::FixedArray<std::size_t, 1> const sizes{12};
            ::fixy::FixedArray<std::size_t, 1> const strides{1};
            auto const unique =
                ::fixy::mint_strided_view_mut(std::move(view), sizes.view(), strides.view(), 0).expect("it fits");
            CRUCIBLE_TEST_ASSERT(test, unique.count() == 12);
            CRUCIBLE_TEST_ASSERT(test, ::fixy::offset_of(elements, ::fixy::View<int const>{old_name.view()}).is_none());
        };
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one unique view to read its old name")
        mint_and_read(run, run);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    }));
    pass(test, "  old_name_of_a_unique_view: PASSED\n");
}

// A cursor of a scope never leaves its loop: a read and a step with no index
// left end the process in each build.
void read_past_the_end() noexcept {
    ::fixy::FixedArray<int, 2> values{1, 2};
    ::fixy::FixedArray<std::size_t, 1> const sizes{2};
    ::fixy::FixedArray<std::size_t, 1> const strides{1};
    ::fixy::StridedView<int const, 1> const line =
        ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("fits");
    std::size_t const total = ::fixy::mint_strided_scope(line, [](auto branded) noexcept {
        auto cursor = branded.indices().begin();
        ++cursor;
        ++cursor;
        return branded.at(*cursor) == 0 ? std::size_t{0} : std::size_t{1};
    });
    static_cast<void>(total);
}

void step_past_the_end() noexcept {
    ::fixy::FixedArray<int, 2> values{1, 2};
    ::fixy::FixedArray<std::size_t, 2> const sizes{2, 0};
    ::fixy::FixedArray<std::size_t, 2> const strides{1, 1};
    ::fixy::StridedView<int const, 2> const empty =
        ::fixy::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("no element");
    std::size_t const total = ::fixy::mint_strided_scope(empty, [](auto branded) noexcept {
        auto cursor = branded.indices().begin();
        ++cursor;
        return std::size_t{0};
    });
    static_cast<void>(total);
}

// ── Properties ──────────────────────────────────────────────────────────

// The counts of the outcomes of the runs of one property.  Each property
// asserts a floor for each outcome, so no property passes with one branch of
// its oracle alone.
struct Outcomes {
    std::size_t accepted = 0;
    std::size_t refused = 0;
    std::size_t other = 0;
};

// The floor of each outcome of a property.
constexpr std::size_t kOutcomeFloor = 20;

// The door gives a view exactly when the elements lie inside the run, and
// the walk and the scope give the sequence of the model.  A dense view is a
// view whose sequence is a run of consecutive offsets.  The other outcome
// counts the dense views of two elements or more, which take the dense loop
// of the walk.
void door_and_walk_agree_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    Outcomes outcomes{};
    property(test, kSeed, kRuns, [&run, &outcomes](Draw& draw) noexcept {
        Layout layout = draw_shape(draw);
        layout.offset = draw.integer<std::size_t>(0, kRun + 64);
        bool const fits = count_of(layout) == 0 || layout.offset + span_of(layout) < kRun;
        return view_of(run, layout)
            .match(
                [&layout, &outcomes, fits](SharedView view) noexcept {
                    ++outcomes.accepted;
                    Sequence const model = model_sequence(layout);
                    bool is_consecutive = true;
                    for (std::size_t position = 1; position < model.count; ++position) {
                        is_consecutive =
                            is_consecutive && offset_at(model, position) == offset_at(model, position - 1) + 1;
                    }
                    if (is_consecutive && model.count >= 2) ++outcomes.other;
                    return fits && agrees_with_model(view, layout) && view.is_dense() == is_consecutive;
                },
                [&outcomes, fits] noexcept {
                    ++outcomes.refused;
                    return !fits;
                });
    });
    pass(test, "  door_and_walk_agree_with_model: {} views, {} refusals, {} dense views\n", outcomes.accepted,
         outcomes.refused, outcomes.other);
    CRUCIBLE_TEST_ASSERT(test, outcomes.accepted >= kOutcomeFloor && outcomes.refused >= kOutcomeFloor
                                   && outcomes.other >= kOutcomeFloor);
}

// A walk over two views of the same sizes visits the pairs of the model in
// order, whatever their strides.
void two_view_walk_agrees_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    property(test, kSeed + 1, kRuns, [&run](Draw& draw) noexcept {
        Layout const first = draw_fitting_layout(draw);
        Layout second = first;
        for (std::size_t axis = 0; axis < second.rank; ++axis) {
            set_lane(second.strides, axis, draw.integer<std::size_t>(0, kMaxStride));
        }
        second.offset = draw.integer<std::size_t>(0, kRun - 1 - span_of(second));
        SharedView const first_view = view_of(run, first).expect("the layout fits");
        SharedView const second_view = view_of(run, second).expect("the layout fits");
        Sequence firsts{};
        Sequence seconds{};
        bool const is_walked = ::fixy::walk(
                                   [&firsts, &seconds](std::uint32_t const& left, std::uint32_t const& right) noexcept {
                                       push(firsts, left);
                                       push(seconds, right);
                                   },
                                   first_view, second_view)
                                   .is_ok();
        return is_walked && same_sequence(firsts, model_sequence(first))
            && same_sequence(seconds, model_sequence(second));
    });
    pass(test, "  two_view_walk_agrees_with_model: PASSED\n");
}

// A permutation and a transpose reorder the axes of the model.
void permute_agrees_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    property(test, kSeed + 2, kRuns, [&run](Draw& draw) noexcept {
        Layout const layout = draw_fitting_layout(draw);
        SharedView const view = view_of(run, layout).expect("the layout fits");
        Lanes order{};
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            set_lane(order, axis, axis);
        }
        for (std::size_t axis = layout.rank; axis > 1; --axis) {
            std::size_t const other = draw.integer<std::size_t>(0, axis - 1);
            std::size_t const kept = lane(order, axis - 1);
            set_lane(order, axis - 1, lane(order, other));
            set_lane(order, other, kept);
        }
        Layout permuted = layout;
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            set_lane(permuted.sizes, axis, lane(layout.sizes, lane(order, axis)));
            set_lane(permuted.strides, axis, lane(layout.strides, lane(order, axis)));
        }
        ::fixy::View<std::size_t const> const order_view =
            order.view().window(0, layout.rank).expect("the rank is at most the lane count");
        bool const is_permuted =
            view.permute(order_view)
                .match([&permuted](SharedView result) noexcept { return agrees_with_model(result, permuted); },
                       [] noexcept { return false; });
        if (layout.rank < 2) return is_permuted;
        std::size_t const first = draw.integer<std::size_t>(0, layout.rank - 1);
        std::size_t const second = draw.integer<std::size_t>(0, layout.rank - 1);
        Layout transposed = layout;
        set_lane(transposed.sizes, first, lane(layout.sizes, second));
        set_lane(transposed.strides, first, lane(layout.strides, second));
        set_lane(transposed.sizes, second, lane(layout.sizes, first));
        set_lane(transposed.strides, second, lane(layout.strides, first));
        return is_permuted
            && agrees_with_model(view.transpose(first, second).expect("two axes below the rank"), transposed);
    });
    pass(test, "  permute_agrees_with_model: PASSED\n");
}

// A slice and a select keep the positions of the model that they name, and
// give no value exactly when the positions do not fit.
void slice_and_select_agree_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    Outcomes outcomes{};
    property(test, kSeed + 3, kRuns, [&run, &outcomes](Draw& draw) noexcept {
        Layout const layout = draw_fitting_layout(draw);
        if (layout.rank == 0) return true;
        SharedView const view = view_of(run, layout).expect("the layout fits");
        std::size_t const axis = draw.integer<std::size_t>(0, layout.rank - 1);
        std::size_t const size = lane(layout.sizes, axis);
        std::size_t const stride = lane(layout.strides, axis);
        std::size_t const start = draw.integer<std::size_t>(0, size + 1);
        std::size_t const count = draw.integer<std::size_t>(0, size + 1);
        std::size_t const step = draw.integer<std::size_t>(0, 3);
        bool const fits = step != 0 && (count == 0 ? start <= size : start < size && start + (count - 1) * step < size);
        Layout sliced = layout;
        set_lane(sliced.sizes, axis, count);
        set_lane(sliced.strides, axis, stride * step);
        sliced.offset = layout.offset + start * stride;
        bool const is_sliced = view.slice(axis, start, count, step)
                                   .match(
                                       [&sliced, &outcomes, fits](SharedView result) noexcept {
                                           ++outcomes.accepted;
                                           return fits && agrees_with_model(result, sliced);
                                       },
                                       [&outcomes, fits] noexcept {
                                           ++outcomes.refused;
                                           return !fits;
                                       });

        std::size_t const position = draw.integer<std::size_t>(0, size);
        Layout selected{};
        selected.rank = layout.rank - 1;
        selected.offset = layout.offset + position * stride;
        std::size_t target = 0;
        for (std::size_t source = 0; source < layout.rank; ++source) {
            if (source == axis) continue;
            set_lane(selected.sizes, target, lane(layout.sizes, source));
            set_lane(selected.strides, target, lane(layout.strides, source));
            ++target;
        }
        bool const is_selected = view.select(axis, position)
                                     .match(
                                         [&selected, position, size](SharedView result) noexcept {
                                             return position < size && agrees_with_model(result, selected);
                                         },
                                         [position, size] noexcept { return position >= size; });
        return is_sliced && is_selected;
    });
    CRUCIBLE_TEST_ASSERT(test, outcomes.accepted >= kOutcomeFloor && outcomes.refused >= kOutcomeFloor);
    pass(test, "  slice_and_select_agree_with_model: PASSED\n");
}

// A broadcast gives the model of the new sizes, with a stride of zero on each
// new axis and on each axis of size one that grows, or no value when an axis
// breaks the rule.
void broadcast_agrees_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    Outcomes outcomes{};
    property(test, kSeed + 4, kRuns, [&run, &outcomes](Draw& draw) noexcept {
        Layout const layout = draw_fitting_layout(draw);
        SharedView const view = view_of(run, layout).expect("the layout fits");
        std::size_t const lead = draw.integer<std::size_t>(0, kLanes - layout.rank);
        Layout target{};
        target.rank = layout.rank + lead;
        target.offset = layout.offset;
        bool fits = true;
        std::size_t count = 1;
        for (std::size_t axis = 0; axis < target.rank; ++axis) {
            std::size_t size = 0;
            std::size_t stride = 0;
            if (axis < lead) {
                size = draw.integer<std::size_t>(1, 3);
            } else {
                std::size_t const old_size = lane(layout.sizes, axis - lead);
                stride = lane(layout.strides, axis - lead);
                size = old_size;
                if (old_size == 1) {
                    size = draw.integer<std::size_t>(0, 3);
                    if (size != 1) stride = 0;
                } else if (draw.integer<std::size_t>(0, 7) == 0) {
                    size = old_size + 1;
                    fits = false;
                }
            }
            if (count * size > kMaxCount) {
                size = 1;
                stride = axis < lead ? 0 : lane(layout.strides, axis - lead);
                fits = fits && (axis < lead || lane(layout.sizes, axis - lead) == 1);
            }
            count *= size;
            set_lane(target.sizes, axis, size);
            set_lane(target.strides, axis, stride);
        }
        return view.broadcast_to(sizes_of(target))
            .match(
                [&target, &outcomes, fits](SharedView result) noexcept {
                    ++outcomes.accepted;
                    return fits && agrees_with_model(result, target);
                },
                [&outcomes, fits] noexcept {
                    ++outcomes.refused;
                    return !fits;
                });
    });
    CRUCIBLE_TEST_ASSERT(test, outcomes.accepted >= kOutcomeFloor && outcomes.refused >= kOutcomeFloor);
    pass(test, "  broadcast_agrees_with_model: PASSED\n");
}

// The oracle of a reshape of a view with elements: a view of the new sizes
// exists exactly when the offset of each element of the sequence is the
// offset of the first element plus the sum of each new coordinate times the
// step of its unit vector.
[[nodiscard]] bool reshape_has_view(Sequence const& sequence, Layout const& target) noexcept {
    Lanes steps{};
    std::size_t inner = 1;
    for (std::size_t axis = target.rank; axis-- > 0;) {
        std::size_t const size = lane(target.sizes, axis);
        if (size > 1) set_lane(steps, axis, offset_at(sequence, inner) - offset_at(sequence, 0));
        inner *= size;
    }
    Lanes index{};
    for (std::size_t position = 0; position < sequence.count; ++position) {
        std::size_t offset = offset_at(sequence, 0);
        for (std::size_t axis = 0; axis < target.rank; ++axis) {
            offset += lane(index, axis) * lane(steps, axis);
        }
        if (offset != offset_at(sequence, position)) return false;
        for (std::size_t axis = target.rank; axis-- > 0;) {
            set_lane(index, axis, lane(index, axis) + 1);
            if (lane(index, axis) < lane(target.sizes, axis)) break;
            set_lane(index, axis, 0);
        }
    }
    return true;
}

// A reshape gives a view exactly when the oracle finds one, and the view
// visits the same offsets in the same order as the source.  A count that
// differs gives CountMismatch.  Half of the sources have the strides of the
// row-major order times a base stride, with the strides of a group of axes
// changed at random, so many reshapes merge or split a group.
void reshape_agrees_with_oracle(::fixy::TestRunnerCtx const& test) noexcept {
    Run const run = make_run();
    Outcomes outcomes{};
    property(test, kSeed + 5, kRuns * 2, [&run, &outcomes](Draw& draw) noexcept {
        Layout layout = draw_shape(draw);
        if (draw.boolean()) {
            std::size_t stride = draw.integer<std::size_t>(1, 2);
            std::size_t const changed = draw.integer<std::size_t>(0, kLanes);
            for (std::size_t axis = layout.rank; axis-- > 0;) {
                set_lane(layout.strides, axis, axis == changed ? lane(layout.strides, axis) : stride);
                stride *= lane(layout.sizes, axis) == 0 ? 1 : lane(layout.sizes, axis);
            }
        }
        if (span_of(layout) >= kRun) return true;
        layout.offset = draw.integer<std::size_t>(0, kRun - 1 - span_of(layout));
        SharedView const view = view_of(run, layout).expect("the layout fits");
        std::size_t const count = count_of(layout);
        Layout target{};
        target.rank = draw.integer<std::size_t>(0, kLanes);
        std::size_t remaining = count;
        for (std::size_t axis = 0; axis < target.rank; ++axis) {
            std::size_t size = remaining;
            if (axis + 1 < target.rank) {
                // A divisor of the remaining count.  A count of zero takes
                // each size, and the size zero keeps the count zero.
                size = draw.integer<std::size_t>(0, 4);
                while (remaining != 0 && (size == 0 || remaining % size != 0)) {
                    size = size == 0 ? 1 : size - 1;
                }
                remaining = size == 0 ? remaining : remaining / size;
            }
            set_lane(target.sizes, axis, size);
        }
        if (draw.integer<std::size_t>(0, 9) == 0 && target.rank > 0) {
            set_lane(target.sizes, 0, lane(target.sizes, 0) + 1);
        }
        bool const is_same_count = count_of(target) == count;
        Sequence const source = model_sequence(layout);
        bool const has_view = is_same_count && (count == 0 || reshape_has_view(source, target));
        return view.reshape(sizes_of(target))
            .match(
                [&source, &outcomes, has_view, count](SharedView result) noexcept {
                    if (count >= 2) ++outcomes.accepted;
                    return has_view && same_sequence(walk_sequence(result), source)
                        && same_sequence(scope_sequence(result), source);
                },
                [&outcomes, has_view, is_same_count](::fixy::ReshapeError error) noexcept {
                    ++(is_same_count ? outcomes.refused : outcomes.other);
                    return !has_view
                        && error
                               == (is_same_count ? ::fixy::ReshapeError::NotAView
                                                 : ::fixy::ReshapeError::CountMismatch);
                });
    });
    CRUCIBLE_TEST_ASSERT(test, outcomes.accepted >= kOutcomeFloor && outcomes.refused >= kOutcomeFloor
                                   && outcomes.other >= kOutcomeFloor);
    pass(test, "  reshape_agrees_with_oracle: PASSED, {} views, {} refusals of a view, {} refusals of a count\n",
         outcomes.accepted, outcomes.refused, outcomes.other);
}

// True when no offset of the sequence occurs two times.  O(count^2).
[[nodiscard]] bool has_distinct_offsets(Sequence const& sequence) noexcept {
    for (std::size_t first = 0; first < sequence.count; ++first) {
        for (std::size_t second = first + 1; second < sequence.count; ++second) {
            if (offset_at(sequence, first) == offset_at(sequence, second)) return false;
        }
    }
    return true;
}

// The rule of the unique door, as a model: the axes of a size above one in
// the order of their strides, and each stride above the span of the axes
// before it.  The model picks the smallest stride that it did not take yet,
// so it shares no sort with the header.  O(rank^2).
[[nodiscard]] bool passes_sorted_strides(Layout const& layout) noexcept {
    if (count_of(layout) == 0) return true;
    Lanes taken{};
    std::size_t span = 0;
    for (std::size_t step = 0; step < layout.rank; ++step) {
        std::size_t best = layout.rank;
        for (std::size_t axis = 0; axis < layout.rank; ++axis) {
            if (lane(taken, axis) != 0 || lane(layout.sizes, axis) <= 1) continue;
            if (best == layout.rank || lane(layout.strides, axis) < lane(layout.strides, best)) best = axis;
        }
        if (best == layout.rank) return true;
        set_lane(taken, best, 1);
        if (lane(layout.strides, best) <= span) return false;
        span += (lane(layout.sizes, best) - 1) * lane(layout.strides, best);
    }
    return true;
}

// The unique door gives a view exactly when the door of a view gives one and
// the model of the rule passes.  Each view that it gives names each element
// at most one time, and it visits the sequence of the model.  The other
// outcome counts the refusals of layouts with no overlap, which the
// sufficient test refuses.
void unique_door_agrees_with_model(::fixy::TestRunnerCtx const& test) noexcept {
    Outcomes outcomes{};
    property(test, kSeed + 6, kRuns, [&outcomes](Draw& draw) noexcept {
        Layout const layout = draw_fitting_layout(draw);
        Sequence const model = model_sequence(layout);
        bool const is_distinct = has_distinct_offsets(model);
        bool const passes = passes_sorted_strides(layout);
        Run const source = make_run();
        auto [fresh, fresh_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::uint32_t, kRun>);
        fresh.with_mut(fresh_key, [&source](auto run) noexcept { ::fixy::copy(std::move(run), source.view()); });
        bool const is_agreed = fresh.with_mut(fresh_key, [&](auto run) noexcept {
            return ::fixy::mint_strided_view_mut(std::move(run), sizes_of(layout), strides_of(layout), layout.offset)
                .match(
                    [&](auto unique) noexcept {
                        ++outcomes.accepted;
                        return passes && is_distinct && same_sequence(walk_sequence(unique.view()), model);
                    },
                    [&] noexcept {
                        ++(is_distinct ? outcomes.other : outcomes.refused);
                        return !passes;
                    });
        });
        return is_agreed;
    });
    pass(test, "  unique_door_agrees_with_model: {} views, {} refusals of an overlap, {} refusals of no overlap\n",
         outcomes.accepted, outcomes.refused, outcomes.other);
    CRUCIBLE_TEST_ASSERT(test, outcomes.accepted >= kOutcomeFloor && outcomes.refused >= kOutcomeFloor);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    matrix_and_transpose(test);
    walk_reads_of_each_mix(test);
    door_refusals(test);
    reshape_cases(test);
    shares_bytes_from_the_first_element_to_the_last(test);
    rank_zero_and_empty(test);
    mismatch_and_dynamic_axes(test);
    scope_and_parallel(test);
    unique_door_cases(test);
    unique_view_writes(test);
    erased_writer_writes(test);
    old_name_of_a_unique_view(test);
    expect_dies(test, read_past_the_end,
                ::fixy::TextView{"a read through the end cursor of a loop over the indexes of a strided view"});
    expect_dies(test, step_past_the_end,
                ::fixy::TextView{"a step past the end cursor of a loop over the indexes of a strided view"});
    door_and_walk_agree_with_model(test);
    two_view_walk_agrees_with_model(test);
    permute_agrees_with_model(test);
    slice_and_select_agree_with_model(test);
    broadcast_agrees_with_model(test);
    reshape_agrees_with_oracle(test);
    unique_door_agrees_with_model(test);
    pass(test, "test_strided_view: all tests passed\n");
    return 0;
}
