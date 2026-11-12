// The compile-time checks of foundation/core/Array.h.

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::array_checks {

struct NoDefault {
    int value;
    constexpr explicit NoDefault(int from) noexcept : value{from} {}
    [[nodiscard]] friend constexpr bool operator==(NoDefault, NoDefault) noexcept = default;
};

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// An array holds its elements and nothing else, and it copies as its
// elements do.
static_assert(sizeof(FixedArray<std::uint32_t, 4>) == 4 * sizeof(std::uint32_t));
static_assert(sizeof(FixedArray<Record, 3>) == 3 * sizeof(Record));
static_assert(std::is_trivially_copyable_v<FixedArray<std::uint64_t, 2>>);
static_assert(std::is_trivially_destructible_v<FixedArray<Record, 3>>);
static_assert(FixedArray<int, 7>::size() == 7);

// The gates: no zero count, no array, no const and no reference element.
template <class T, std::size_t N>
concept HasFixedArray = requires { typename FixedArray<T, N>; };
static_assert(HasFixedArray<int, 1> && HasFixedArray<NoDefault, 2>);
static_assert(!HasFixedArray<int, 0>);
static_assert(!HasFixedArray<int const, 2>);
static_assert(!HasFixedArray<int[2], 2>);
static_assert(!HasFixedArray<int&, 2>);

// The default constructor value-initializes each element.  The list
// constructor takes exactly N arguments, and its form of one argument is
// explicit.
static_assert(std::is_default_constructible_v<FixedArray<int, 4>>);
static_assert(!std::is_default_constructible_v<FixedArray<NoDefault, 2>>);
static_assert(std::is_constructible_v<FixedArray<int, 3>, int, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int, int, int>);
static_assert(std::is_constructible_v<FixedArray<int, 1>, int>);
static_assert(!std::is_convertible_v<int, FixedArray<int, 1>>);
static_assert(std::is_constructible_v<FixedArray<NoDefault, 2>, NoDefault, NoDefault>);

// No subscript, no pointer and no data() reach an element.
template <class Array>
concept HasSubscript = requires(Array array) { array[0]; };
template <class Array>
concept HasData = requires(Array array) { array.data(); };
static_assert(!HasSubscript<FixedArray<int, 4>&> && !HasData<FixedArray<int, 4>&>);
static_assert(!std::is_convertible_v<FixedArray<int, 4>&, int*>);

// view() of an lvalue gives a View of the fixed extent.  A view, a cursor
// and an element of a temporary are refused.
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4>&>().view()), View<int, 4>>);
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4> const&>().view()), View<int const, 4>>);
template <class Array>
concept ViewsTemporary = requires { std::declval<Array>().view(); };
template <class Array>
concept LoopsOverTemporary = requires { std::declval<Array>().begin(); };
template <class Array>
concept ReadsTemporary = requires { std::declval<Array>().at(std::declval<Index<4>>()); };
static_assert(!ViewsTemporary<FixedArray<int, 4>> && !ViewsTemporary<FixedArray<int, 4> const>);
static_assert(!LoopsOverTemporary<FixedArray<int, 4>> && !ReadsTemporary<FixedArray<int, 4>>);

// at takes an Index<N> or a constant index below N, and an Index of a
// different count does not convert down.
template <class Array, class Proof>
concept ReadsAt = requires(Array& array, Proof proof) { array.at(proof); };
static_assert(ReadsAt<FixedArray<int, 4>, Index<4>> && ReadsAt<FixedArray<int, 4>, StaticIndex<3>>);
static_assert(ReadsAt<FixedArray<int, 4>, Index<2>>);
static_assert(!ReadsAt<FixedArray<int, 4>, StaticIndex<4>>);
static_assert(!ReadsAt<FixedArray<int, 4>, Index<8>>);
static_assert(!ReadsAt<FixedArray<int, 4>, std::size_t>);

// The elements in a constant evaluation.
[[nodiscard]] consteval int sum_of(FixedArray<int, 4> const& array) noexcept {
    int total = 0;
    for (int const value : array) {
        total += value;
    }
    return total;
}

static_assert(sum_of(FixedArray<int, 4>{}) == 0);
static_assert(sum_of(FixedArray<int, 4>{1, 2, 3, 4}) == 10);
static_assert(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 6});
static_assert(!(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 7}));
static_assert(FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}}
              == FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}});

[[nodiscard]] consteval int write_and_read() noexcept {
    FixedArray<int, 3> array{};
    array.at(index<0>()) = 10;
    array.at(Index<3>::check(2).expect("2 is below 3")) = 30;
    fill(array.view().window(1, 1).expect("one element fits"), 20);
    int total = 0;
    for (Index<3> const position : array.indices()) {
        total += array.at(position) * static_cast<int>(position.raw() + 1);
    }
    return total;
}
static_assert(write_and_read() == 10 * 1 + 20 * 2 + 30 * 3);

// The View of an array reads each element at its proven index.
[[nodiscard]] consteval int view_reads() noexcept {
    FixedArray<int, 4> array{7, 8, 9, 10};
    View<int const, 4> const elements = array.view();
    int total = 0;
    for (Index<4> const position : elements.indices()) {
        total += elements.at(position);
    }
    return total + elements.at(index<3>());
}
static_assert(view_reads() == 7 + 8 + 9 + 10 + 10);

}  // namespace detail::array_checks

}  // namespace foundation::core
