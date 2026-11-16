// Tests of strided_view_of of crucible/TensorMeta.h: the strided view of a
// tensor over the run of its slot in the memory plan.  The run starts at the
// element at the zero index, so the view never reads storage_offset.  Each
// refusal names one claim that the descriptor breaks.  The elements are
// integers of 32 bits, so the test compares them with ==.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

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
[[nodiscard]] Elements walk_order(::fixy::StridedView<std::int32_t> view) noexcept {
    Elements order{};
    std::size_t visits = 0;
    bool const is_walked = ::fixy::walk(
                               [&order, &visits](std::int32_t& element) noexcept {
                                   *order.get(visits).expect("at most six elements") = element;
                                   ++visits;
                               },
                               view)
                               .is_ok();
    if (!is_walked) ::fixy::fatal("a walk over one view has one shape");
    return order;
}

// A dense matrix, its transpose, a broadcast row and a scalar, over one run.
void views_of_descriptors(::fixy::TestRunnerCtx const& test) noexcept {
    Elements values{0, 1, 2, 3, 4, 5};
    ::fixy::View<std::int32_t> const storage{values.view()};
    ::fixy::FixedArray<std::int64_t, 2> const sizes{2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const strides{3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const turned_sizes{3, 2};
    ::fixy::FixedArray<std::int64_t, 2> const turned_strides{1, 3};
    ::fixy::FixedArray<std::int64_t, 2> const row_strides{0, 1};

    TensorMeta matrix = int_meta(sizes.view(), strides.view());
    // The storage offset counts from the storage of PyTorch, which the plan
    // does not hold, so the door does not read it.
    matrix.storage_offset = 1000;
    ::fixy::StridedView<std::int32_t> const dense =
        ::crucible::strided_view_of(matrix, storage).ok().expect("a dense matrix fits its slot");
    CRUCIBLE_TEST_ASSERT(test, dense.rank() == 2 && dense.count() == 6 && dense.is_dense());
    CRUCIBLE_TEST_ASSERT(test, walk_order(dense) == (Elements{0, 1, 2, 3, 4, 5}));

    ::fixy::StridedView<std::int32_t> const turned =
        ::crucible::strided_view_of(int_meta(turned_sizes.view(), turned_strides.view()), storage)
            .ok()
            .expect("a transpose fits its slot");
    CRUCIBLE_TEST_ASSERT(test, walk_order(turned) == (Elements{0, 3, 1, 4, 2, 5}));

    ::fixy::StridedView<std::int32_t> const rows =
        ::crucible::strided_view_of(int_meta(sizes.view(), row_strides.view()), storage)
            .ok()
            .expect("a broadcast row fits its slot");
    CRUCIBLE_TEST_ASSERT(test, walk_order(rows) == (Elements{0, 1, 2, 0, 1, 2}));

    // A tensor of rank zero reads the first element of its slot.
    ::fixy::View<std::int64_t const> const no_axes{};
    ::fixy::StridedView<std::int32_t> const scalar =
        ::crucible::strided_view_of(int_meta(no_axes, no_axes), storage).ok().expect("a scalar fits its slot");
    CRUCIBLE_TEST_ASSERT(test, scalar.rank() == 0 && scalar.count() == 1);
    pass(test, "  views_of_descriptors: PASSED\n");
}

// The refusal of the descriptor, or the value None when the door gives a
// view.
[[nodiscard]] ::fixy::Option<TensorViewRefusal> refusal_of(TensorMeta const& meta,
                                                           ::fixy::View<std::int32_t> storage) noexcept {
    return ::crucible::strided_view_of(meta, storage).err();
}

[[nodiscard]] bool is_refused(TensorMeta const& meta, ::fixy::View<std::int32_t> storage,
                              TensorViewRefusal expected) noexcept {
    return refusal_of(meta, storage)
        .match([expected](TensorViewRefusal found) noexcept { return found == expected; },
               [] noexcept { return false; });
}

// Each refusal of the door.
void refusals(::fixy::TestRunnerCtx const& test) noexcept {
    Elements values{};
    ::fixy::View<std::int32_t> const storage{values.view()};
    ::fixy::FixedArray<std::int64_t, 2> const sizes{2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const strides{3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const negative_size{-2, 3};
    ::fixy::FixedArray<std::int64_t, 2> const negative_stride{-3, 1};
    ::fixy::FixedArray<std::int64_t, 2> const wide_strides{4, 1};

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
    CRUCIBLE_TEST_ASSERT(
        test, is_refused(int_meta(sizes.view(), negative_stride.view()), storage, TensorViewRefusal::NegativeStride));
    CRUCIBLE_TEST_ASSERT(
        test, is_refused(int_meta(sizes.view(), wide_strides.view()), storage, TensorViewRefusal::OutsideStorage));

    // A slot of fewer elements than the span refuses a dense matrix too.
    ::fixy::View<std::int32_t> const short_storage = storage.window(0, 5).expect("five elements fit");
    CRUCIBLE_TEST_ASSERT(
        test, is_refused(int_meta(sizes.view(), strides.view()), short_storage, TensorViewRefusal::OutsideStorage));
    CRUCIBLE_TEST_ASSERT(test, refusal_of(int_meta(sizes.view(), strides.view()), storage).is_none());
    pass(test, "  refusals: PASSED\n");
}

// A storage format of the base is the element of a tensor of its dtype, and
// of no other dtype of its size.
void storage_formats(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::FixedArray<::fixy::f16, 4> halves{};
    ::fixy::View<::fixy::f16> const storage{halves.view()};
    ::fixy::FixedArray<std::int64_t, 1> const sizes{4};
    ::fixy::FixedArray<std::int64_t, 1> const strides{1};
    TensorMeta half = int_meta(sizes.view(), strides.view());
    half.dtype = ScalarType::Half;
    CRUCIBLE_TEST_ASSERT(test, ::crucible::strided_view_of(half, storage).is_ok());
    TensorMeta brain = half;
    brain.dtype = ScalarType::BFloat16;
    CRUCIBLE_TEST_ASSERT(test, ::crucible::strided_view_of(brain, storage).err().is_some());
    CRUCIBLE_TEST_ASSERT(test, ::crucible::scalar_type_of<::fixy::bf16> == ScalarType::BFloat16
                                   && ::crucible::scalar_type_of<::fixy::fp8_e4m3> == ScalarType::Float8_e4m3fn
                                   && ::crucible::scalar_type_of<::fixy::fp8_e5m2> == ScalarType::Float8_e5m2);
    pass(test, "  storage_formats: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    views_of_descriptors(test);
    refusals(test);
    storage_formats(test);
    pass(test, "test_tensor_meta_strided: all tests passed\n");
    return 0;
}
