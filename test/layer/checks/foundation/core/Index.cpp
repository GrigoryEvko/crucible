// The compile-time checks of foundation/core/Index.h.

#include <foundation/core/Index.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::index_checks {

// An index holds the narrowest word that holds its count, and the seal
// adds no byte.  The value Count is the niche, so an Option of an index has
// the size of the index.
static_assert(sizeof(Index<4>) == sizeof(std::uint32_t));
static_assert(sizeof(Index<0xFFFFFFFFull>) == sizeof(std::uint32_t));
static_assert(sizeof(Index<0x100000000ull>) == sizeof(std::size_t));
static_assert(sizeof(Option<Index<4>>) == sizeof(Index<4>));
static_assert(sizeof(Option<Index<0x100000000ull>>) == sizeof(std::size_t));
static_assert(option_form_of<Index<4>> == OptionForm::niche);
static_assert(std::is_empty_v<StaticIndex<3>>);

// The seal refuses std::bit_cast and a lifetime start over bytes, and the
// copy and the destructor stay trivial for the ABI.
static_assert(!std::is_trivially_copyable_v<Index<4>>);
static_assert(std::is_trivially_copy_constructible_v<Index<4>> && std::is_trivially_destructible_v<Index<4>>);

// No number becomes an index, and an index has no default value.
static_assert(!std::is_constructible_v<Index<4>, std::uint32_t>);
static_assert(!std::is_constructible_v<Index<4>, std::size_t>);
static_assert(!std::is_constructible_v<Index<4>, int>);
static_assert(!std::is_default_constructible_v<Index<4>>);
static_assert(!std::is_constructible_v<IndexCursor<4>, std::uint32_t>);

// A constant index converts when it is below the count, and an index of a
// smaller count widens.  No conversion narrows.
static_assert(std::is_convertible_v<StaticIndex<3>, Index<4>>);
static_assert(!std::is_convertible_v<StaticIndex<4>, Index<4>>);
static_assert(std::is_convertible_v<Index<4>, Index<8>>);
static_assert(!std::is_convertible_v<Index<8>, Index<4>>);
static_assert(std::is_same_v<decltype(index<2>()), StaticIndex<2>>);

// The counts of an index.
static_assert(IndexCount<1> && IndexCount<static_cast<std::size_t>(__PTRDIFF_MAX__)>);
static_assert(!IndexCount<0> && !IndexCount<static_cast<std::size_t>(__PTRDIFF_MAX__) + 1>);

// check gives an index below the count, and no value at or past it.
static_assert(Index<4>::check(0).expect("0 is below 4").raw() == 0);
static_assert(Index<4>::check(3).expect("3 is below 4").raw() == 3);
static_assert(Index<4>::check(4).is_none());
static_assert(Index<4>::check(~std::size_t{0}).is_none());
static_assert(Index<0xFFFFFFFFull>::check(0xFFFFFFFEull).is_some());
static_assert(Index<0xFFFFFFFFull>::check(0xFFFFFFFFull).is_none());
static_assert(Index<4>{index<2>()}.raw() == 2);
static_assert(Index<8>{Index<4>{index<3>()}}.raw() == 3);
static_assert(Index<4>{index<1>()} == Index<4>{index<1>()});
static_assert(Index<4>{index<1>()} < Index<4>{index<2>()});

// The loop of an IndexRange gives each index in order.
[[nodiscard]] consteval std::size_t sum_of_indices() noexcept {
    std::size_t sum = 0;
    for (Index<5> const position : IndexRange<5>{}) {
        sum += position.raw();
    }
    return sum;
}
static_assert(sum_of_indices() == 0 + 1 + 2 + 3 + 4);

// A cursor never makes an index past its range: a read at the end and a
// step past the end call the fatal exit, which is not a constant
// expression.
[[nodiscard]] consteval std::size_t read_after_steps(int steps) noexcept {
    auto cursor = IndexRange<2>::begin();
    for (int step = 0; step < steps; ++step) {
        ++cursor;
    }
    return (*cursor).raw();
}
template <int Steps>
concept ReadsInside = requires { typename std::integral_constant<std::size_t, read_after_steps(Steps)>; };
static_assert(ReadsInside<0> && ReadsInside<1>);
static_assert(!ReadsInside<2>);
static_assert(!ReadsInside<3>);

}  // namespace detail::index_checks

}  // namespace foundation::core
