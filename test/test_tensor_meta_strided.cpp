// Tests of strided_view_of of crucible/TensorMeta.h: the strided view of a
// tensor over the run of its slot in the memory plan.  The run starts at the
// first element of the storage, so the view of a slice starts at its storage
// offset.  Each refusal names one claim that the descriptor breaks.  The
// elements are integers of 32 bits, so the test compares them with ==.  The
// view of a View reads, and the unique view of the ViewMut of a frame writes.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::crucible::ScalarType;
using ::crucible::TensorMeta;
using ::crucible::TensorViewRefusal;
using ::fixy::check::pass;

using Elements = ::fixy::FixedArray<std::int32_t, 6>;

// The descriptor of an Int tensor of the sizes and the strides.
[[nodiscard]] TensorMeta int_meta(::fixy::View<std::int64_t const> sizes,
                                  ::fixy::View<std::int64_t const> strides) noexcept {
    TensorMeta meta{};
    meta.dtype = ScalarType::Int;
    meta.ndim = static_cast<std::uint8_t>(sizes.size());
    std::size_t axis = 0;
    for (std::int64_t const size : sizes) {
        meta.sizes[axis] = ::crucible::tensor_dim(size);
        ++axis;
    }
    axis = 0;
    for (std::int64_t const stride : strides) {
        meta.strides[axis] = ::crucible::tensor_dim(stride);
        ++axis;
    }
    return meta;
}

// The elements of the view in the order of the walk.
[[nodiscard]] Elements walk_order(::fixy::StridedView<std::int32_t const> view) noexcept {
    Elements order{};
    std::size_t visits = 0;
    bool const is_walked =
        ::fixy::walk(
            [&order, &visits](std::int32_t const& element) noexcept {
                order.set(::fixy::Index<Elements::size()>::check(visits).expect("at most six elements"), element);
                ++visits;
            },
            view)
            .is_ok();
    if (!is_walked) ::fixy::fatal("a walk over one view has one shape");
    return order;
}

// A dense matrix, its transpose, a broadcast row and a scalar, over one run.
void views_of_descriptors(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::int32_t, 6>,
                                                         ::fixy::FixedArray<std::int32_t, 6>{0, 1, 2, 3, 4, 5});
    ::fixy::FixedArray<std::int64_t, 2> const sizes{2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const strides{3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const turned_sizes{3, 2};
    ::fixy::FixedArray<std::int64_t, 2> const turned_strides{1, 3};
    ::fixy::FixedArray<std::int64_t, 2> const row_strides{0, 1};

    values.with_mut(
        values_key, [&test, &sizes, &strides, &turned_sizes, &turned_strides, &row_strides](auto run) noexcept {
            ::fixy::View<std::int32_t const> const storage = run.view();
            TensorMeta const matrix = int_meta(sizes.view(), strides.view());
            ::fixy::StridedView<std::int32_t const> const dense =
                ::crucible::strided_view_of(matrix, storage).ok().expect("a dense matrix fits its slot");
            CRUCIBLE_TEST_ASSERT(test, dense.rank() == 2 && dense.count() == 6 && dense.is_dense());
            CRUCIBLE_TEST_ASSERT(test, walk_order(dense) == (Elements{0, 1, 2, 3, 4, 5}));

            ::fixy::StridedView<std::int32_t const> const turned =
                ::crucible::strided_view_of(int_meta(turned_sizes.view(), turned_strides.view()), storage)
                    .ok()
                    .expect("a transpose fits its slot");
            CRUCIBLE_TEST_ASSERT(test, walk_order(turned) == (Elements{0, 3, 1, 4, 2, 5}));

            ::fixy::StridedView<std::int32_t const> const rows =
                ::crucible::strided_view_of(int_meta(sizes.view(), row_strides.view()), storage)
                    .ok()
                    .expect("a broadcast row fits its slot");
            CRUCIBLE_TEST_ASSERT(test, walk_order(rows) == (Elements{0, 1, 2, 0, 1, 2}));

            // A tensor of rank zero reads the first element of its slot.
            ::fixy::View<std::int64_t const> const no_axes{};
            ::fixy::StridedView<std::int32_t const> const scalar =
                ::crucible::strided_view_of(int_meta(no_axes, no_axes), storage).ok().expect("a scalar fits its slot");
            CRUCIBLE_TEST_ASSERT(test, scalar.rank() == 0 && scalar.count() == 1);

            // Two indexes of the broadcast row name one element, so the row has
            // no unique view, which writes.
            CRUCIBLE_TEST_ASSERT(
                test, ::crucible::strided_view_of(int_meta(sizes.view(), row_strides.view()), std::move(run)).err()
                          == ::fixy::some(TensorViewRefusal::Overlap));
        });
    pass(test, "  views_of_descriptors: PASSED\n");
}

// Slices of the matrix {0, 1, 2}, {3, 4, 5} over the run of its storage.  A
// slice keeps the slot of its base, so its view starts at its storage offset
// and reads the elements of the base.
void slices_at_an_offset(::fixy::TestRunnerCtx const& test) noexcept {
    auto [values, values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::int32_t, 6>,
                                                         ::fixy::FixedArray<std::int32_t, 6>{0, 1, 2, 3, 4, 5});
    ::fixy::FixedArray<std::int64_t, 1> const row_sizes{3};
    ::fixy::FixedArray<std::int64_t, 1> const row_strides{1};
    ::fixy::FixedArray<std::int64_t, 1> const column_sizes{2};
    ::fixy::FixedArray<std::int64_t, 1> const column_strides{3};

    values.with_mut(values_key, [&test, &row_sizes, &row_strides, &column_sizes, &column_strides](auto run) noexcept {
        ::fixy::View<std::int32_t const> const storage = run.view();
        TensorMeta second_row = int_meta(row_sizes.view(), row_strides.view());
        second_row.storage_offset = 3;
        ::fixy::StridedView<std::int32_t const> const row =
            ::crucible::strided_view_of(second_row, storage).ok().expect("the second row fits the storage");
        CRUCIBLE_TEST_ASSERT(test, walk_order(row) == (Elements{3, 4, 5, 0, 0, 0}));

        TensorMeta second_column = int_meta(column_sizes.view(), column_strides.view());
        second_column.storage_offset = 1;
        ::fixy::StridedView<std::int32_t const> const column =
            ::crucible::strided_view_of(second_column, storage).ok().expect("the second column fits the storage");
        CRUCIBLE_TEST_ASSERT(test, walk_order(column) == (Elements{1, 4, 0, 0, 0, 0}));

        // A write through the unique view of the slice writes the storage of
        // the base.
        auto unique_row =
            ::crucible::strided_view_of(second_row, std::move(run)).ok().expect("the second row fits the storage");
        bool const is_walked = ::fixy::walk([](std::int32_t& cell) noexcept { cell = cell * 10; }, unique_row).is_ok();
        CRUCIBLE_TEST_ASSERT(test, is_walked);
    });
    Elements const expected{0, 1, 2, 30, 40, 50};
    CRUCIBLE_TEST_ASSERT(test, ::fixy::equal(values.view(), expected.view()));
    pass(test, "  slices_at_an_offset: PASSED\n");
}

// The refusal of the descriptor, or the value None when the door gives a
// view.
[[nodiscard]] ::fixy::Option<TensorViewRefusal> refusal_of(TensorMeta const& meta,
                                                           ::fixy::View<std::int32_t const> storage) noexcept {
    return ::crucible::strided_view_of(meta, storage).err();
}

[[nodiscard]] bool is_refused(TensorMeta const& meta, ::fixy::View<std::int32_t const> storage,
                              TensorViewRefusal expected) noexcept {
    return refusal_of(meta, storage)
        .match([expected](TensorViewRefusal found) noexcept { return found == expected; },
               [] noexcept { return false; });
}

// Each refusal of the door.
void refusals(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<std::int64_t, 2> const sizes{2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const strides{3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const negative_size{-2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const negative_stride{-3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const wide_strides{4, 1};

    static_cast<void>(::fixy::build_fixed_array<std::int32_t, 6>([&test, &sizes, &strides, &negative_size,
                                                                  &negative_stride, &wide_strides](auto run) noexcept {
        ::fixy::View<std::int32_t const> const storage = run.view();
        TensorMeta sparse = int_meta(sizes.view(), strides.view());
        sparse.layout = ::crucible::Layout::Sparse;
        CRUCIBLE_TEST_ASSERT(test, is_refused(sparse, storage, TensorViewRefusal::NotStrided));

        TensorMeta wide_element = int_meta(sizes.view(), strides.view());
        wide_element.dtype = ScalarType::Long;
        CRUCIBLE_TEST_ASSERT(test, is_refused(wide_element, storage, TensorViewRefusal::ElementType));
        // An element of four bytes of another dtype is no Int either.
        TensorMeta floating = int_meta(sizes.view(), strides.view());
        floating.dtype = ScalarType::Float;
        CRUCIBLE_TEST_ASSERT(test, is_refused(floating, storage, TensorViewRefusal::ElementType));

        CRUCIBLE_TEST_ASSERT(
            test, is_refused(int_meta(negative_size.view(), strides.view()), storage, TensorViewRefusal::NegativeSize));
        CRUCIBLE_TEST_ASSERT(test, is_refused(int_meta(sizes.view(), negative_stride.view()), storage,
                                              TensorViewRefusal::NegativeStride));
        CRUCIBLE_TEST_ASSERT(
            test, is_refused(int_meta(sizes.view(), wide_strides.view()), storage, TensorViewRefusal::OutsideStorage));

        // A slot of fewer elements than the span refuses a dense matrix too.
        ::fixy::View<std::int32_t const> const short_storage = storage.window(0, 5).expect("five elements fit");
        CRUCIBLE_TEST_ASSERT(
            test, is_refused(int_meta(sizes.view(), strides.view()), short_storage, TensorViewRefusal::OutsideStorage));

        // An offset that moves the last element past the storage, and a negative
        // offset, refuse the view.
        TensorMeta shifted = int_meta(sizes.view(), strides.view());
        shifted.storage_offset = 1;
        CRUCIBLE_TEST_ASSERT(test, is_refused(shifted, storage, TensorViewRefusal::OutsideStorage));
        shifted.storage_offset = -1;
        CRUCIBLE_TEST_ASSERT(test, is_refused(shifted, storage, TensorViewRefusal::OutsideStorage));
        CRUCIBLE_TEST_ASSERT(test, refusal_of(int_meta(sizes.view(), strides.view()), storage).is_none());
    }));
    pass(test, "  refusals: PASSED\n");
}

// A storage format of the base is the element of a tensor of its dtype, and
// of no other dtype of its size.
void storage_formats(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<std::int64_t, 1> const sizes{4};
    ::fixy::FixedArray<std::int64_t, 1> const strides{1};
    static_cast<void>(::fixy::build_fixed_array<::fixy::f16, 4>([&test, &sizes, &strides](auto run) noexcept {
        ::fixy::View<::fixy::f16 const> const storage = run.view();
        TensorMeta half = int_meta(sizes.view(), strides.view());
        half.dtype = ScalarType::Half;
        CRUCIBLE_TEST_ASSERT(test, ::crucible::strided_view_of(half, storage).is_ok());
        TensorMeta brain = half;
        brain.dtype = ScalarType::BFloat16;
        CRUCIBLE_TEST_ASSERT(test, ::crucible::strided_view_of(brain, storage).err().is_some());
    }));
    CRUCIBLE_TEST_ASSERT(test, ::crucible::scalar_type_of<::fixy::bf16> == ScalarType::BFloat16
                                   && ::crucible::scalar_type_of<::fixy::fp8_e4m3> == ScalarType::Float8_e4m3fn
                                   && ::crucible::scalar_type_of<::fixy::fp8_e5m2> == ScalarType::Float8_e5m2);
    pass(test, "  storage_formats: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    views_of_descriptors(test);
    slices_at_an_offset(test);
    refusals(test);
    storage_formats(test);
    pass(test, "test_tensor_meta_strided: all tests passed\n");
    return 0;
}
