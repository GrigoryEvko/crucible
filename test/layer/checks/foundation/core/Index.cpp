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

// mask gives the low bits of a word for a count that is a power of two.  The
// fixture neg_core_index_mask_not_power_of_two refuses a count of another
// value.
static_assert(Index<64>::mask(0).raw() == 0 && Index<64>::mask(63).raw() == 63);
static_assert(Index<64>::mask(64).raw() == 0 && Index<64>::mask(4096 + 5).raw() == 5);
static_assert(Index<1>::mask(~std::size_t{0}).raw() == 0);
static_assert(Index<0x100000000ull>::mask(0x1FFFFFFFFull).raw() == 0xFFFFFFFFull);

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

// A cyclic index holds two words of its count, and no number makes one.  The
// seal refuses std::bit_cast, so no state past the bounds is made.
static_assert(sizeof(CyclicIndex<4>) == 2 * sizeof(std::uint32_t));
static_assert(!std::is_trivially_copyable_v<CyclicIndex<4>>);
static_assert(!std::is_constructible_v<CyclicIndex<4>, std::uint32_t>);
static_assert(!std::is_constructible_v<CyclicIndex<4>, std::uint32_t, std::uint32_t>);
static_assert(std::is_same_v<decltype(CyclicIndex<4>{}.at()), Index<4>>);

// The position and ten times the live count after StepsBefore steps, Drops
// drops and StepsAfter steps, in that order.
template <int StepsBefore, int Drops, int StepsAfter>
[[nodiscard]] consteval std::size_t position_after() noexcept {
    CyclicIndex<4> turn{};
    for (int step = 0; step < StepsBefore; ++step) {
        turn.step();
    }
    for (int drop = 0; drop < Drops; ++drop) {
        turn.drop();
    }
    for (int step = 0; step < StepsAfter; ++step) {
        turn.step();
    }
    return turn.at().raw() + 10 * turn.live();
}

// Each position of a full turn lives, and a step from the last position goes
// back to the first.
static_assert(position_after<0, 0, 0>() == 0 + 40);
static_assert(position_after<0, 0, 3>() == 3 + 40);
static_assert(position_after<0, 0, 4>() == 0 + 40);
// A drop keeps the cursor on the position after it, which moves down by one,
// and a drop of the last position goes back to the first.
static_assert(position_after<1, 1, 0>() == 1 + 30);
static_assert(position_after<3, 1, 0>() == 0 + 30);
static_assert(position_after<1, 1, 2>() == 0 + 30);
// With no live position, the cursor stays at the first position.
static_assert(position_after<0, 4, 0>() == 0);
static_assert(position_after<0, 4, 3>() == 0);

// A drop with no live position calls the fatal exit, which is not a constant
// expression.
template <int Drops>
concept DropsInside = requires { typename std::integral_constant<std::size_t, position_after<0, Drops, 0>()>; };
static_assert(DropsInside<4>);
static_assert(!DropsInside<5>);

// next_cyclic visits each index of a ring in order and comes back to 0, and
// prev_cyclic walks the ring the other way.  A count that is a power of two
// and a count of another value take different code.
template <std::size_t Count>
[[nodiscard]] consteval bool walks_each_index() noexcept {
    Index<Count> forward{index<0>()};
    for (Index<Count> const expected : IndexRange<Count>{}) {
        if (forward != expected) return false;
        forward = forward.next_cyclic();
    }
    if (forward != Index<Count>{index<0>()}) return false;
    Index<Count> backward{index<0>()};
    for (std::size_t step = 0; step < Count; ++step) {
        backward = backward.prev_cyclic();
        if (backward.raw() != Count - 1 - step) return false;
    }
    return backward == Index<Count>{index<0>()};
}
static_assert(walks_each_index<1>() && walks_each_index<2>() && walks_each_index<5>());
static_assert(walks_each_index<7>() && walks_each_index<8>() && walks_each_index<64>());
static_assert(Index<0xFFFFFFFFull>::check(0xFFFFFFFEull).expect("the last index").next_cyclic().raw() == 0);
static_assert(Index<0x100000000ull>::check(0xFFFFFFFFull).expect("the last index").next_cyclic().raw() == 0);
static_assert(Index<0x100000000ull>{index<0>()}.prev_cyclic().raw() == 0xFFFFFFFFull);

// A Ranged holds the narrowest word of its range, and the seal adds no byte.
static_assert(sizeof(Ranged<0, 255>) == 1 && sizeof(Ranged<-128, 127>) == 1 && sizeof(Ranged<0, 256>) == 2);
static_assert(sizeof(Ranged<-129, 0>) == 2 && sizeof(Ranged<0, 0xFFFFFFFF>) == 4);
static_assert(sizeof(Ranged<-1, 0xFFFFFFFF>) == 8 && sizeof(Ranged<0, 0x100000000>) == 8);
static_assert(std::is_same_v<decltype(Ranged<-1, 1>::constant<0>().raw()), std::int8_t>);
static_assert(Ranged<3, 9>::lo == 3 && Ranged<3, 9>::hi == 9);

// No number becomes a Ranged with no check, and the seal refuses
// std::bit_cast.
static_assert(!std::is_constructible_v<Ranged<0, 7>, int> && !std::is_constructible_v<Ranged<0, 7>, std::uint8_t>);
static_assert(!std::is_default_constructible_v<Ranged<0, 7>> && !std::is_trivially_copyable_v<Ranged<0, 7>>);

// check compares exactly for each integer type.
static_assert(Ranged<0, 7>::check(7).is_some() && Ranged<0, 7>::check(8).is_none());
static_assert(Ranged<0, 7>::check(-1).is_none() && Ranged<1, 5>::check(std::uint8_t{0}).is_none());
static_assert(Ranged<-3, 3>::check(std::uint64_t{3}).is_some() && Ranged<-3, 3>::check(std::uint64_t{4}).is_none());
static_assert(Ranged<-3, 3>::check(min_of<std::int64_t>).is_none() && Ranged<-3, -1>::check(0u).is_none());
static_assert(Ranged<0, max_of<std::int64_t>>::check(max_of<std::uint64_t>).is_none());
static_assert(Ranged<-3, 3>::check(-3).expect("-3 is in the range").raw() == -3);

// The range of a result comes from the bounds of the operands.
constexpr auto three = Ranged<0, 7>::constant<3>();
constexpr auto minus_two = Ranged<-4, 4>::constant<-2>();
static_assert(std::is_same_v<decltype(three + minus_two), Ranged<-4, 11>> && (three + minus_two).raw() == 1);
static_assert(std::is_same_v<decltype(three - minus_two), Ranged<-4, 11>> && (three - minus_two).raw() == 5);
static_assert(std::is_same_v<decltype(minus_two - three), Ranged<-11, 4>> && (minus_two - three).raw() == -5);
static_assert(std::is_same_v<decltype(three * minus_two), Ranged<-28, 28>> && (three * minus_two).raw() == -6);
static_assert(std::is_same_v<decltype(minus_two * minus_two), Ranged<-16, 16>> && (minus_two * minus_two).raw() == 4);

// A result range past std::int64_t does not compile, and a plain integer
// does not mix in an operation.
template <class Left, class Right>
concept Adds = requires(Left left, Right right) { left + right; };
template <class Left, class Right>
concept Multiplies = requires(Left left, Right right) { left * right; };
static_assert(Adds<Ranged<0, max_of<std::int64_t> - 1>, Ranged<0, 1>>);
static_assert(!Adds<Ranged<0, max_of<std::int64_t>>, Ranged<0, 1>>);
static_assert(!Adds<Ranged<min_of<std::int64_t>, 0>, Ranged<-1, 0>> && !Adds<Ranged<0, 7>, int>);
static_assert(Multiplies<Ranged<0, std::int64_t{1} << 32>, Ranged<0, (std::int64_t{1} << 31) - 1>>);
static_assert(!Multiplies<Ranged<0, std::int64_t{1} << 32>, Ranged<0, std::int64_t{1} << 31>>);
static_assert(!Multiplies<Ranged<min_of<std::int64_t>, 0>, Ranged<-1, 0>>);

// A Ranged in [0, N) converts to Index<N>, an Index<N> converts to each
// range that holds [0, N - 1], and a range widens.  No conversion narrows.
static_assert(std::is_convertible_v<Ranged<0, 3>, Index<4>> && std::is_convertible_v<Ranged<1, 3>, Index<9>>);
static_assert(!std::is_convertible_v<Ranged<0, 4>, Index<4>> && !std::is_convertible_v<Ranged<-1, 3>, Index<4>>);
static_assert(std::is_convertible_v<Index<4>, Ranged<0, 3>> && std::is_convertible_v<Index<4>, Ranged<-1, 9>>);
static_assert(!std::is_convertible_v<Index<5>, Ranged<0, 3>> && !std::is_convertible_v<Index<4>, Ranged<1, 9>>);
static_assert(std::is_convertible_v<Ranged<0, 3>, Ranged<-1, 3>> && !std::is_convertible_v<Ranged<0, 4>, Ranged<0, 3>>);
static_assert(Index<4>{Ranged<0, 3>::constant<2>()}.raw() == 2);
static_assert(Ranged<0, 3>{Index<4>{index<3>()}}.raw() == 3);
static_assert(Ranged<-1, 3>{Ranged<0, 3>::constant<1>()}.raw() == 1);

// narrow gives the value in a smaller range, or no value.
static_assert(Ranged<0, 100>::constant<50>().narrow<0, 63>().is_some());
static_assert(Ranged<0, 100>::constant<70>().narrow<0, 63>().is_none());
static_assert(Ranged<0, 7>::constant<3>() == Ranged<0, 7>::constant<3>());
static_assert(Ranged<0, 7>::constant<3>() < Ranged<0, 7>::constant<4>());

}  // namespace detail::index_checks

}  // namespace foundation::core
