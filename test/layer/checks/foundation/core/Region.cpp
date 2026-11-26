// The compile-time checks of foundation/core/Region.h.

#include <foundation/core/Region.h>

#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Grow.h>
#include <foundation/core/List.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

enum class Shade : std::uint8_t {
    dark,
    light,
    bright
};

// A record of one byte.
struct Tint {
    std::uint8_t level = 0;
};

// One byte of two bit-fields.
struct Halves {
    std::uint8_t low : 4;
    std::uint8_t high : 4;
};

struct Empty {};

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

// fill writes an element of one byte with one memset when decode reads the
// byte as the whole value.  A wider element, a bit-field and an empty class
// keep the loop.
static_assert(detail::ByteFillElement<std::uint8_t> && detail::ByteFillElement<char> && detail::ByteFillElement<bool>);
static_assert(detail::ByteFillElement<Shade> && detail::ByteFillElement<std::byte> && detail::ByteFillElement<Tint>);
static_assert(!detail::ByteFillElement<std::uint16_t> && !detail::ByteFillElement<Record>);
static_assert(!detail::ByteFillElement<Halves> && !detail::ByteFillElement<Empty>);

// The windows in a constant evaluation, over one constant object, which is
// a run of one element.
constexpr int single_value = 7;

[[nodiscard]] consteval int sum_of(View<int const> view) noexcept {
    int total = 0;
    for (int const value : view) {
        total += value;
    }
    return total;
}

[[nodiscard]] consteval View<int const> single_view() noexcept {
    return View<int const>{detail::view_of_object_(single_value)};
}

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

// prefix_up_to is total: a count past the size gives the whole View, and the
// prefix starts at the first element.
static_assert(single_view().prefix_up_to(0).size() == 0);
static_assert(sum_of(single_view().prefix_up_to(1)) == 7);
static_assert(sum_of(single_view().prefix_up_to(2)) == 7);
static_assert(single_view().prefix_up_to(dynamic_extent).size() == 1);
static_assert(View<int const>{}.prefix_up_to(5).size() == 0);
static_assert(sum_of(detail::view_of_object_(single_value).prefix_up_to(4)) == 7, "a View of a fixed extent too");

// fill in a constant evaluation keeps the loop.  Each case fills a run of
// one element, through a View of a dynamic extent and of the fixed extent 1.
template <class T>
[[nodiscard]] consteval T filled(T initial, T value) noexcept {
    T element = initial;
    fill(View<T>{detail::view_of_object_(element)}, value);
    return element;
}

template <class T>
[[nodiscard]] consteval T filled_fixed(T initial, T value) noexcept {
    T element = initial;
    fill(detail::view_of_object_(element), value);
    return element;
}

static_assert(filled<std::uint8_t>(0x11, 0xA5) == 0xA5);
static_assert(filled<Tint>(Tint{3}, Tint{200}).level == 200);
static_assert(filled<bool>(false, true) && !filled<bool>(true, false));
static_assert(filled<Shade>(Shade::dark, Shade::bright) == Shade::bright);
static_assert(filled<std::uint64_t>(1, 0x0123456789ABCDEFu) == 0x0123456789ABCDEFu);
static_assert(filled_fixed<std::uint8_t>(1, 2) == 2);
static_assert(filled_fixed<bool>(true, false) == false);
static_assert(filled_fixed<Shade>(Shade::bright, Shade::light) == Shade::light);

// The empty View has no buffer, and its fill writes nothing.
[[nodiscard]] consteval bool fills_empty_views() noexcept {
    fill(View<std::uint8_t>{}, std::uint8_t{7});
    fill(View<bool>{}, true);
    fill(View<std::uint64_t>{}, std::uint64_t{7});
    return true;
}
static_assert(fills_empty_views());

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

// An Option of a window of a text has the size of a TextView: the niche
// marks the empty state.
static_assert(sizeof(Option<TextView>) == sizeof(TextView));
static_assert(option_form_of<TextView> == OptionForm::niche);
static_assert(std::is_same_v<decltype(TextView{}.window(0, 0)), Option<TextView>>);
static_assert(std::is_same_v<decltype(TextView{}.begin()), ViewCursor<char const>>);

// The loop and the windows of a text in a constant evaluation.
[[nodiscard]] consteval std::size_t digits_of(TextView text) noexcept {
    std::size_t count = 0;
    for (char const character : text) {
        if (character >= '0' && character <= '9') ++count;
    }
    return count;
}

static_assert(digits_of(TextView{"cpu12-15"}) == 4);
static_assert(digits_of(TextView{}) == 0);
static_assert(TextView{"cpulist"}.window(3, 4).expect("the window fits") == TextView{"list"});
static_assert(TextView{"cpulist"}.window(7, 0).expect("the empty window fits") == TextView{});
static_assert(TextView{"cpulist"}.window(7, 1).is_none());
static_assert(TextView{"cpulist"}.window(8, 0).is_none());
static_assert(TextView{"cpulist"}.window(1, dynamic_extent).is_none());
static_assert(TextView{}.window(0, 0).is_some());

// A cursor of a text never leaves the text, as a cursor of a View.
[[nodiscard]] consteval char read_text_after_steps(int steps) noexcept {
    auto cursor = TextView{"ab"}.begin();
    for (int step = 0; step < steps; ++step) {
        ++cursor;
    }
    return *cursor;
}
template <int Steps>
concept ReadsInsideText = requires { typename std::integral_constant<char, read_text_after_steps(Steps)>; };
static_assert(ReadsInsideText<1>);
static_assert(!ReadsInsideText<2>);
static_assert(!ReadsInsideText<3>);

// A FixedText gives the loop over its characters, and clear() empties it.
[[nodiscard]] consteval std::size_t characters_after_clear(bool clears) noexcept {
    FixedText<8> text;
    text_resize_(text, 3);
    if (clears) text.clear();
    std::size_t count = 0;
    for ([[maybe_unused]] char const character : text) {
        ++count;
    }
    return count;
}
static_assert(characters_after_clear(false) == 3);
static_assert(characters_after_clear(true) == 0);
template <class Text>
concept LoopsOverTemporary = requires { static_cast<Text&&>(std::declval<Text>()).begin(); };
static_assert(!LoopsOverTemporary<FixedText<8>>);

// The iteration verbs in a constant evaluation, over the View of an array.
[[nodiscard]] consteval int reverse_digits() noexcept {
    FixedArray<int, 4> const digits{1, 2, 3, 4};
    int number = 0;
    for (int const digit : digits.view().reverse()) {
        number = number * 10 + digit;
    }
    return number;
}
static_assert(reverse_digits() == 4321);

// Each window of three of five elements: the first and the last element of
// each run, and the count of the runs.
[[nodiscard]] consteval int window_marks() noexcept {
    FixedArray<int, 5> const values{1, 2, 3, 4, 5};
    int marks = 0;
    int count = 0;
    for (View<int const, 3> const run : values.windows<3>()) {
        marks += run.at(index<0>()) * 100 + run.at(index<2>());
        ++count;
    }
    return marks * 10 + count;
}
static_assert(window_marks() == (103 + 204 + 305) * 10 + 3);

// The chunks of two of five elements, and the rest of one element.
[[nodiscard]] consteval int chunk_sums() noexcept {
    FixedArray<int, 5> const values{1, 2, 3, 4, 5};
    int total = 0;
    int count = 0;
    auto const runs = values.chunks<2>();
    for (View<int const, 2> const run : runs) {
        total += run.at(index<0>()) * 10 + run.at(index<1>());
        ++count;
    }
    int rest_total = 0;
    for (int const value : runs.rest()) {
        rest_total += value;
    }
    return total * 1000 + count * 100 + rest_total * 10 + static_cast<int>(runs.rest().size());
}
static_assert(chunk_sums() == (12 + 34) * 1000 + 2 * 100 + 5 * 10 + 1);

// A View with fewer elements than a window gives no window, and an empty
// View gives no element and no run.
[[nodiscard]] consteval int turns_of_short_views() noexcept {
    FixedArray<int, 2> const pair{7, 8};
    int turns = 0;
    for ([[maybe_unused]] View<int const, 3> const run : View<int const>{pair.view()}.windows<3>()) {
        ++turns;
    }
    for ([[maybe_unused]] int const value : View<int const>{}.reverse()) {
        ++turns;
    }
    for ([[maybe_unused]] View<int const, 2> const run : View<int const>{}.chunks<2>()) {
        ++turns;
    }
    return turns + static_cast<int>(View<int const>{}.chunks<2>().rest().size());
}
static_assert(turns_of_short_views() == 0);

// A fixed extent refuses a window or a chunk past it at compile time, and a
// width of zero is no width.  A loop over a temporary array is refused.
template <class Elements, std::size_t Width>
concept HasWindows = requires(Elements elements) { elements.template windows<Width>(); };
template <class Elements, std::size_t Width>
concept HasChunks = requires(Elements elements) { elements.template chunks<Width>(); };
static_assert(HasWindows<View<int, 4>, 4> && !HasWindows<View<int, 4>, 5> && !HasWindows<View<int>, 0>);
static_assert(HasChunks<View<int, 4>, 1> && !HasChunks<View<int, 4>, 5> && !HasChunks<View<int>, 0>);
static_assert(HasWindows<View<int>, 1000>);
template <class Array>
concept ReversesTemporary = requires { std::declval<Array>().reverse(); };
static_assert(!ReversesTemporary<FixedArray<int, 4>> && ReversesTemporary<FixedArray<int, 4>&>);

// A cursor of a verb never leaves its loop: a read at the end calls the
// fatal exit, which is not a constant expression.
[[nodiscard]] consteval int read_reverse_after_steps(int steps) noexcept {
    FixedArray<int, 2> const pair{7, 8};
    auto cursor = pair.reverse().begin();
    for (int step = 0; step < steps; ++step) {
        ++cursor;
    }
    return *cursor;
}
template <int Steps>
concept ReadsInsideReverse = requires { typename std::integral_constant<int, read_reverse_after_steps(Steps)>; };
static_assert(ReadsInsideReverse<1> && !ReadsInsideReverse<2> && !ReadsInsideReverse<3>);
static_assert(read_reverse_after_steps(0) == 8 && read_reverse_after_steps(1) == 7);

// ── The position of a run inside a run ───────────────────────────────

// offset_of takes two Views of one element type, with or without a const
// and of each extent, and gives an Option of a position.  The element form
// takes an element of the first View as an lvalue.  The fixtures
// neg_core_offset_of_different_elements, neg_core_offset_of_pointer and
// neg_core_offset_of_temporary show the refusals of two element types, of
// two pointers and of a temporary, which is no element of a View.
static_assert(std::is_same_v<decltype(offset_of(std::declval<View<int>>(), std::declval<View<int const, 4>>())),
                             Option<std::size_t>>);
static_assert(
    std::is_same_v<decltype(offset_of(std::declval<View<int const>>(), std::declval<int&>())), Option<std::size_t>>);

// classify_overlap takes two Views of one element type, with or without a
// const and of each extent, and gives a RunOverlap of one byte, the answer
// type of the plan of foundation/core/Pointwise.h too.  Two Views of two
// element types name no common position, so the query refuses them, and
// shares_bytes compares their bytes.
template <class Left, class Right>
constexpr bool classify_overlap_accepts = requires(Left left, Right right) { classify_overlap(left, right); };
static_assert(std::is_same_v<decltype(classify_overlap(std::declval<View<int>>(), std::declval<View<int const, 4>>())),
                             RunOverlap>);
static_assert(sizeof(RunOverlap) == 1);
static_assert(classify_overlap_accepts<View<int const, 4>, View<int>>);
static_assert(!classify_overlap_accepts<View<int>, View<long>>);
static_assert(
    std::is_same_v<decltype(shares_bytes(std::declval<View<int>>(), std::declval<View<long const, 4>>())), bool>);

// ── The unique view ───────────────────────────────────────────────────

// A ViewMut holds a pointer and a count.  It does not copy, and its end is
// trivial.  Its moves empty the source, so no move is trivial, and
// std::bit_cast builds none.  Only a frame door builds one: no constructor
// takes a pointer, a count or a View with no key.  Its View and its Refs read
// the run, also from a ViewMut that is not const, and the loop over a ViewMut
// that is not const writes it.
struct UniqueBrand final {};
static_assert(sizeof(ViewMut<int, UniqueBrand>) == 2 * sizeof(void*));
static_assert(!std::is_copy_constructible_v<ViewMut<int, UniqueBrand>>);
static_assert(std::is_nothrow_move_constructible_v<ViewMut<int, UniqueBrand>>);
static_assert(std::is_nothrow_move_assignable_v<ViewMut<int, UniqueBrand>>);
static_assert(!std::is_trivially_move_constructible_v<ViewMut<int, UniqueBrand>>);
static_assert(!std::is_trivially_move_assignable_v<ViewMut<int, UniqueBrand>>);
static_assert(std::is_trivially_destructible_v<ViewMut<int, UniqueBrand>>);
static_assert(!std::is_trivially_copyable_v<ViewMut<int, UniqueBrand>>);
static_assert(!std::is_constructible_v<ViewMut<int, UniqueBrand>, int*, std::size_t>);
static_assert(!std::is_constructible_v<ViewMut<int, UniqueBrand>, View<int>>);
static_assert(std::is_same_v<decltype(*std::declval<ViewMut<int, UniqueBrand>&>().begin()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<ViewMut<int, UniqueBrand> const&>().begin()), int const&>);
static_assert(
    std::is_same_v<decltype(std::declval<ViewMut<int, UniqueBrand>&>().get(0)), Option<Ref<int const, UniqueBrand>>>);
static_assert(std::is_same_v<decltype(std::declval<ViewMut<int, UniqueBrand> const&>().get(0)),
                             Option<Ref<int const, UniqueBrand>>>);
static_assert(std::is_same_v<decltype(std::declval<ViewMut<int, UniqueBrand>&>().view()), View<int const>>);
static_assert(std::is_same_v<decltype(std::declval<ViewMut<int, UniqueBrand> const&>().view()), View<int const>>);

// A ViewMut of a fixed extent holds a pointer alone, its moves empty the
// source too, and its View has the fixed extent.  It converts to the
// ViewMut of a dynamic extent when it moves in, and no conversion gives a
// fixed extent.  A split takes its ViewMut by move and gives parts of the
// brand of the parent: two parts of a dynamic extent in an Option, two parts
// of fixed extents, or a range of chunks that does not copy.  The fixtures
// neg_core_view_mut_split_of_named and neg_core_view_mut_split_of_const show
// the refusals of a named and of a const ViewMut.
static_assert(sizeof(ViewMut<int, UniqueBrand, 4>) == sizeof(void*));
static_assert(!std::is_copy_constructible_v<ViewMut<int, UniqueBrand, 4>>);
static_assert(std::is_nothrow_move_constructible_v<ViewMut<int, UniqueBrand, 4>>);
static_assert(!std::is_trivially_move_constructible_v<ViewMut<int, UniqueBrand, 4>>);
static_assert(!std::is_trivially_move_assignable_v<ViewMut<int, UniqueBrand, 4>>);
static_assert(std::is_same_v<decltype(std::declval<ViewMut<int, UniqueBrand, 4>&>().view()), View<int const, 4>>);
static_assert(std::is_convertible_v<ViewMut<int, UniqueBrand, 4>&&, ViewMut<int, UniqueBrand>>);
static_assert(!std::is_constructible_v<ViewMut<int, UniqueBrand>, ViewMut<int, UniqueBrand, 4>&>);
static_assert(!std::is_constructible_v<ViewMut<int, UniqueBrand, 4>, ViewMut<int, UniqueBrand>&&>);
static_assert(std::is_same_v<decltype(split_at(std::declval<ViewMut<int, UniqueBrand, 4>>(), 1)),
                             Option<::foundation::core::Record<ViewMut<int, UniqueBrand>, ViewMut<int, UniqueBrand>>>>);
static_assert(std::is_same_v<decltype(split_at<1>(std::declval<ViewMut<int, UniqueBrand, 4>>())),
                             ::foundation::core::Record<ViewMut<int, UniqueBrand, 1>, ViewMut<int, UniqueBrand, 3>>>);
static_assert(std::is_same_v<decltype(split_chunks(std::declval<ViewMut<int, UniqueBrand>>(), 2)),
                             ViewMutChunks<int, UniqueBrand>>);
static_assert(std::is_same_v<decltype(std::declval<ViewMutChunks<int, UniqueBrand>&>().next()),
                             Option<ViewMut<int, UniqueBrand>>>);
static_assert(!std::is_copy_constructible_v<ViewMutChunks<int, UniqueBrand>>);
static_assert(!std::is_trivially_move_constructible_v<ViewMutChunks<int, UniqueBrand>>
              && !std::is_trivially_move_assignable_v<ViewMutChunks<int, UniqueBrand>>);

// The doors of a kernel.  A ViewMut and a View are inputs, and only a ViewMut
// of a different brand proves that its run does not overlap the output.  An
// op holds no state.  A copy and write_each give a Result of the counts.
struct OtherBrand final {};
struct PlusOne final {
    int operator()(int const& value) const noexcept { return value + 1; }
};
static_assert(KernelInput<ViewMut<int, UniqueBrand>> && KernelInput<View<int const>> && KernelInput<View<int, 4>>);
static_assert(!KernelInput<int*> && !KernelInput<FixedArray<int, 4>> && !KernelInput<TextView>);
static_assert(DisjointInput<ViewMut<int, OtherBrand>, UniqueBrand>);
static_assert(!DisjointInput<ViewMut<int, UniqueBrand>, UniqueBrand> && !DisjointInput<View<int const>, UniqueBrand>);
static_assert(KernelOp<PlusOne, int, ViewMut<int, OtherBrand>> && KernelOp<PlusOne, int, View<int const>>);
static_assert(!KernelOp<PlusOne, int, int*>);

// The restrict loop needs inputs of other brands whose elements hold no
// borrow, and an output whose element holds none.  An element that is a View
// could name the run of the output, so it takes the loop of plain pointers.
static_assert(RestrictKernel<int, UniqueBrand, ViewMut<int, OtherBrand>, ViewMut<long, OtherBrand>>);
static_assert(RestrictKernel<int, UniqueBrand>);
static_assert(!RestrictKernel<int, UniqueBrand, ViewMut<int, UniqueBrand>>);
static_assert(!RestrictKernel<int, UniqueBrand, View<int const>>);
static_assert(!RestrictKernel<int, UniqueBrand, ViewMut<View<int>, OtherBrand>>);
static_assert(!RestrictKernel<View<int>, UniqueBrand, ViewMut<int, OtherBrand>>);
static_assert(std::is_same_v<decltype(copy(std::declval<ViewMut<int, UniqueBrand>>(),
                                           std::declval<ViewMut<int, OtherBrand> const&>())),
                             Result<Unit, LengthMismatch>>);
static_assert(std::is_same_v<decltype(copy(std::declval<ViewMut<int, UniqueBrand>>(), std::declval<View<int>>())),
                             Result<Unit, LengthMismatch>>);
static_assert(std::is_same_v<decltype(write_each(std::declval<ViewMut<int, UniqueBrand>>(), PlusOne{},
                                                 std::declval<ViewMut<int, OtherBrand> const&>())),
                             Result<Unit, LengthMismatch>>);

// update_each gives op the element of its output as the first input.
struct PlusOld final {
    int operator()(int const& old, int const& value) const noexcept { return old + value; }
};
static_assert(std::is_same_v<decltype(update_each(std::declval<ViewMut<int, UniqueBrand>>(), PlusOld{},
                                                  std::declval<View<int const> const&>())),
                             Result<Unit, LengthMismatch>>);
static_assert(std::is_same_v<decltype(update_each(std::declval<ViewMut<int, UniqueBrand>>(), PlusOne{})),
                             Result<Unit, LengthMismatch>>);

// ── The closed surface ────────────────────────────────────────────────

// The family list of plan section 11.4, with the names of the headers of
// the family that are on main: element_type and extent of a View, capacity
// of an owner, raw, check and mask of an Index, rest of the chunks, prefix and
// suffix of a View of a fixed extent, is_at_end and remaining of a cursor that
// a holder keeps between two calls, and get_mut of a ViewMut, the writer of
// one element.  The niche of a View, of a TextView and of an Index is the
// protocol of the Choice family, and its check reads it.
inline constexpr TextView region_names{
    "View ViewMut Index StaticIndex IndexRange BrandedIndex TextView CText FixedText GrowText HeapText FixedArray "
    "FixedList HeapArray HeapRing RingView FixedTable HeapTable Full Held LengthMismatch dynamic_extent "
    "element_type brand_type extent capacity window split_at prefix prefix_up_to suffix get_mut indices index parallel "
    "check mask raw rest take take_first take_last expect_empty clear with_view with_element with_mut build_at "
    "append_self freeze is_at_end remaining set exchange update insert remove drain fill copy_from copy_window "
    "next_cyclic prev_cyclic Ranged lo hi constant narrow"};

// The brand that the check of a branded type names.  A program gets each
// brand from mint_branded_scope.
struct SurfaceBrand final {};

// Each type of the family gives only the shared verbs, the language
// protocols and the names of the list.
CRUCIBLE_CLOSED_SURFACE(region_names, View<int>);
CRUCIBLE_CLOSED_SURFACE(region_names, View<int const, 4>);
CRUCIBLE_CLOSED_SURFACE(region_names, TextView);
CRUCIBLE_CLOSED_SURFACE(region_names, CText);
CRUCIBLE_CLOSED_SURFACE(region_names, FixedText<8>);
CRUCIBLE_CLOSED_SURFACE(region_names, Index<4>);
CRUCIBLE_CLOSED_SURFACE(region_names, Index<0x100000000ull>);
CRUCIBLE_CLOSED_SURFACE(region_names, StaticIndex<2>);
CRUCIBLE_CLOSED_SURFACE(region_names, Ranged<0, 7>);
CRUCIBLE_CLOSED_SURFACE(region_names, Ranged<-4, 4>);
CRUCIBLE_CLOSED_SURFACE(region_names, IndexRange<4>);
CRUCIBLE_CLOSED_SURFACE(region_names, FixedArray<int, 4>);
CRUCIBLE_CLOSED_SURFACE(region_names, FixedArray<Record, 1>);
CRUCIBLE_CLOSED_SURFACE(region_names, HeapArray<int>);
CRUCIBLE_CLOSED_SURFACE(region_names, HeapArray<std::uint8_t, 64>);
CRUCIBLE_CLOSED_SURFACE(region_names, FixedList<int, 4>);
CRUCIBLE_CLOSED_SURFACE(region_names, FixedList<Record, 2>);
CRUCIBLE_CLOSED_SURFACE(region_names, Full<int>);
CRUCIBLE_CLOSED_SURFACE(region_names, GrowText);
CRUCIBLE_CLOSED_SURFACE(region_names, HeapText);
CRUCIBLE_CLOSED_SURFACE(region_names, BrandedIndex<SurfaceBrand>);
CRUCIBLE_CLOSED_SURFACE(region_names, BrandedView<int, SurfaceBrand>);
CRUCIBLE_CLOSED_SURFACE(region_names, ViewMut<int, SurfaceBrand>);
CRUCIBLE_CLOSED_SURFACE(region_names, LengthMismatch);

// The cursors of the loops take the cursor protocol, and no other type of
// the family takes a dereference.
CRUCIBLE_CLOSED_SURFACE(region_names, ViewCursor<int>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(region_names, ViewCursor<char const>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(region_names, IndexCursor<4>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(region_names, detail::ReverseCursor<int>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(region_names, detail::WindowCursor<int, 2>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(region_names, detail::ChunkCursor<int const, 3>, CursorProtocol);
static_assert(!closed_surface<ViewCursor<int>>(region_names).is_closed());
static_assert(!closed_surface<IndexCursor<4>>(region_names).is_closed());

// A View refers to its elements (the transfer).  A View of const elements
// moves to another thread and two threads read it.  A View of elements that
// are not const gives T& from a const View, so it holds neither property for
// a plain int.
static_assert(::foundation::transfer::Sendable<View<int const>> && ::foundation::transfer::Shareable<View<int const>>);
static_assert(::foundation::transfer::Sendable<View<int const, 4>>);
static_assert(!::foundation::transfer::Sendable<View<int>> && !::foundation::transfer::Shareable<View<int>>);
static_assert(::foundation::transfer::Sendable<View<int const> const&>);

// foreign_first gives the address of the first element of a view, and a view
// of const elements gives a pointer to const.
consteval bool gives_the_first_address() {
    FixedArray<int, 3> values{};
    View<int const, 3> const read = static_cast<FixedArray<int, 3> const&>(values).view();
    return foreign_first(values.view()) == &values.view().at(index<0>()) && foreign_first(read) == &read.at(index<0>());
}
static_assert(gives_the_first_address());
static_assert(std::is_same_v<decltype(foreign_first(std::declval<View<int const>>())), int const*>);

}  // namespace detail::region_checks

}  // namespace foundation::core
