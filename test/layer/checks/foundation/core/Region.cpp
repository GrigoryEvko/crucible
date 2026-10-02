// The compile-time checks of foundation/core/Region.h.

#include <foundation/core/Region.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::region_checks {

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

struct NotCopyable {
    NotCopyable() = default;
    NotCopyable(NotCopyable const&) = delete;
    NotCopyable& operator=(NotCopyable const&) = delete;
};

// A pointer and a count, or a pointer alone for a fixed extent.
static_assert(sizeof(View<int>) == 2 * sizeof(void*));
static_assert(sizeof(View<int, 16>) == sizeof(void*));
static_assert(sizeof(View<Record, 65536>) == sizeof(void*));
// A cursor holds its place and the end of its View.
static_assert(sizeof(ViewCursor<int>) == 2 * sizeof(void*));

// An Option of a View has the size of the View: the niche marks the empty
// state.
static_assert(sizeof(Option<View<int>>) == sizeof(View<int>));
static_assert(sizeof(Option<View<int, 16>>) == sizeof(void*));
static_assert(option_form_of<View<int>> == OptionForm::niche);
static_assert(option_form_of<View<int, 16>> == OptionForm::niche);
static_assert(std::is_trivially_copy_constructible_v<Option<View<int>>>);
static_assert(std::is_trivially_destructible_v<Option<View<int, 16>>>);

// The seal refuses std::bit_cast and a lifetime start over bytes, and the
// copy and the destructor stay trivial for the ABI.
static_assert(!std::is_trivially_copyable_v<View<int>>);
static_assert(std::is_trivially_copy_constructible_v<View<int>>);
static_assert(std::is_trivially_destructible_v<View<int>>);
static_assert(!std::is_trivially_copyable_v<ViewCursor<int>>);

// No pointer becomes a View, and no count does.
static_assert(!std::is_constructible_v<View<int>, int*, std::size_t>);
static_assert(!std::is_constructible_v<View<int, 4>, int*>);
static_assert(!std::is_default_constructible_v<View<int, 4>>);
static_assert(std::is_default_constructible_v<View<int>>);
static_assert(!std::is_constructible_v<ViewCursor<int>, int*>);
static_assert(!std::is_constructible_v<ViewCursor<int>, int*, int*>);

// The conversions: a const added, a fixed extent dropped.  Never the other
// way.
static_assert(std::is_convertible_v<View<int>, View<int const>>);
static_assert(std::is_convertible_v<View<int, 8>, View<int>>);
static_assert(std::is_convertible_v<View<int, 8>, View<int const>>);
static_assert(std::is_convertible_v<View<int, 8>, View<int const, 8>>);
static_assert(!std::is_convertible_v<View<int const>, View<int>>);
static_assert(!std::is_constructible_v<View<int>, View<int const>>);
static_assert(!std::is_convertible_v<View<int>, View<int, 8>>);
static_assert(!std::is_convertible_v<View<int, 4>, View<int, 8>>);
static_assert(!std::is_convertible_v<View<int>, View<long>>);

// The element and the extent gates.
static_assert(ViewElement<int> && ViewElement<int const> && ViewElement<Record> && ViewElement<NotCopyable>);
static_assert(!ViewElement<int&>);
static_assert(!ViewElement<int[2]>);
static_assert(!ViewElement<int volatile>);
static_assert(!ViewElement<void>);
static_assert(ViewExtent<int, dynamic_extent> && ViewExtent<int, 1>);
static_assert(!ViewExtent<int, 0>);
static_assert(ViewExtent<int, max_view_count(sizeof(int))>);
static_assert(!ViewExtent<int, max_view_count(sizeof(int)) + 1>);

// copy and fill write only an element whose assignment is a byte copy.
static_assert(RegionCopyElement<int> && RegionCopyElement<Record>);
static_assert(!RegionCopyElement<int const>);
static_assert(!RegionCopyElement<NotCopyable>);
// The probe is a variable template and not a concept, because it gates
// nothing: it asks which pairs of Views copy accepts.
template <class Dst, class Src>
constexpr bool copy_accepts = requires(Dst dst, Src src) { copy(dst, src); };
static_assert(copy_accepts<View<int>, View<int const>>);
static_assert(copy_accepts<View<int>, View<int>>);
static_assert(copy_accepts<View<int, 4>, View<int const, 4>>);
static_assert(copy_accepts<View<int>, View<int, 4>>);
static_assert(!copy_accepts<View<int, 4>, View<int, 8>>);
static_assert(!copy_accepts<View<int, 4>, View<int>>);
static_assert(!copy_accepts<View<int const>, View<int const>>);
static_assert(!copy_accepts<View<int>, View<long>>);

// The windows in a constant evaluation, over one constant object, which is
// a run of one element.  The object has internal linkage, so a constant
// evaluation can compare its address with null.
constexpr int single_value = 7;

[[nodiscard]] consteval int sum_of(View<int const> view) noexcept {
    int total = 0;
    for (int const value : view) {
        total += value;
    }
    return total;
}

[[nodiscard]] consteval View<int const> single_view() noexcept { return detail::view_over_(&single_value, 1); }

static_assert(single_view().size() == 1);
static_assert(sum_of(single_view()) == 7);
static_assert(sum_of(single_view().window(0, 1).expect("the window fits")) == 7);
static_assert(single_view().window(1, 0).is_some());
static_assert(single_view().window(1, 0).expect("the empty window fits").size() == 0);
static_assert(single_view().window(0, 2).is_none());
static_assert(single_view().window(2, 0).is_none());
static_assert(single_view().window(1, dynamic_extent).is_none());
static_assert(single_view().window<1>(0).is_some());
static_assert(single_view().window<1>(1).is_none());
static_assert(sum_of(single_view().window<1>(0).expect("the fixed window fits")) == 7);
static_assert(sum_of(View<int const>{}) == 0);

// A cursor never leaves its View: a read at the end and a step past the
// end call the fatal exit, which is not a constant expression.
[[nodiscard]] consteval int read_after_steps(int steps) noexcept {
    auto cursor = single_view().begin();
    for (int step = 0; step < steps; ++step) {
        ++cursor;
    }
    return *cursor;
}
template <int Steps>
concept ReadsInside = requires { typename std::integral_constant<int, read_after_steps(Steps)>; };
static_assert(ReadsInside<0>);
static_assert(!ReadsInside<1>);
static_assert(!ReadsInside<2>);

}  // namespace detail::region_checks

}  // namespace foundation::core
