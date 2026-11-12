#pragma once

// The proofs of an index of the Region family.
//
//   Index<N>          an index that is below N.  check(raw) gives one, or no
//                     value when raw is not below N.  raw() gives the number.
//   StaticIndex<I>    the proof of the constant index I.  index<I>() gives
//                     one, and it converts to each Index<N> with I < N.
//   IndexRange<N>     each Index<N> from 0 to N - 1, for the loop
//                     `for (Index<N> index : IndexRange<N>{})`.  The owners
//                     and the Views of a fixed extent give it as indices().
//
// An operation that takes an Index<N>, such as at() of a FixedArray<T, N>,
// does no check: the type proves the bound, and the check ran one time,
// where the index was made.  The only roads to an Index are check(), the
// conversion from a StaticIndex, the conversion from an Index of a smaller
// N, and the loop of an IndexRange.  The constructor from a number is
// private, and the seal refuses std::bit_cast and a lifetime start over
// bytes, so no number becomes an Index without its check.
//
// An Index<N> holds a 32-bit word when N fits in one, else a 64-bit word.
// The value N is no index, so an Option<Index<N>> has the size of the index.

#include <foundation/ByteSeal.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// A count that an Index can prove an index below: not zero, and not past the
// largest count of an object.
template <std::size_t Count>
concept IndexCount = Count > 0 && Count <= static_cast<std::size_t>(__PTRDIFF_MAX__);

// The proof of the constant index I.  It is empty, and at() reads I from its
// type.  A constant index is a fact of the program text, so any code can
// make one, and each operation that takes it compares I with its bound at
// compile time.
template <std::size_t I>
struct StaticIndex final {
    explicit constexpr StaticIndex() noexcept = default;
};

// The proof of the constant index I: `array.at(index<2>())`.
template <std::size_t I>
[[nodiscard]] constexpr StaticIndex<I> index() noexcept {
    return StaticIndex<I>{};
}

template <std::size_t Count>
    requires IndexCount<Count>
class Index;

template <std::size_t Count>
    requires IndexCount<Count>
class IndexCursor;

template <std::size_t Count>
    requires IndexCount<Count>
class IndexRange;

namespace detail {

// The word of an Index<Count>: the narrowest word that holds Count, because
// the niche of the Option is the value Count.
template <std::size_t Count>
using IndexWord = std::conditional_t<(Count <= 0xFFFFFFFFull), std::uint32_t, std::size_t>;

}  // namespace detail

template <std::size_t Count>
    requires IndexCount<Count>
class Index final {
    using Word = detail::IndexWord<Count>;

    Word value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t OtherCount>
        requires IndexCount<OtherCount>
    friend class Index;
    friend class IndexCursor<Count>;
    friend struct niche<Index>;

    constexpr explicit Index(Word value) noexcept : value_{value} {}

public:
    // The index raw, or no value when raw is not below Count.  One compare.
    // The result is an Option<Index<Count>>.  The return type is deduced, so
    // the Option is named only where this class is complete.
    [[nodiscard]] static constexpr auto check(std::size_t raw) noexcept {
        using Result = Option<Index>;
        if (raw >= Count) return Result{none};
        return Result::some(Index{static_cast<Word>(raw)});
    }

    // The constant index I, which the type proves below Count.
    template <std::size_t I>
        requires(I < Count)
    constexpr Index(StaticIndex<I>) noexcept : value_{static_cast<Word>(I)} {}

    // An index below a smaller count is below Count too.
    template <std::size_t OtherCount>
        requires(OtherCount < Count)
    constexpr Index(Index<OtherCount> other) noexcept : value_{static_cast<Word>(other.raw())} {}

    // The number, which is below Count in each state.  The optimizer reads
    // the bound, so a subscript with it needs no check.
    [[nodiscard]] constexpr std::size_t raw() const noexcept {
        std::size_t const value = value_;
        [[assume(value < Count)]];
        return value;
    }

    [[nodiscard]] friend constexpr bool operator==(Index left, Index right) noexcept {
        return left.value_ == right.value_;
    }
    [[nodiscard]] friend constexpr auto operator<=>(Index left, Index right) noexcept {
        return left.value_ <=> right.value_;
    }
};

// The empty value of an Option of an Index: the value Count, which no Index
// holds.
template <std::size_t Count>
struct niche<Index<Count>> {
    [[nodiscard]] static constexpr Index<Count> empty() noexcept {
        return Index<Count>{static_cast<detail::IndexWord<Count>>(Count)};
    }
    [[nodiscard]] static constexpr bool is_empty(Index<Count> const& index) noexcept { return index.value_ == Count; }
};

// The position of a loop over an IndexRange.  It holds the next index, or
// Count at the end.  It comes only from begin() and end() of an IndexRange.
// A read or a step at the end ends the process in each build, so a cursor
// never makes an index past its range.  A loop compares the cursor with the
// end before each read and each step, so the optimizer removes the two
// checks there.
template <std::size_t Count>
    requires IndexCount<Count>
class IndexCursor final {
    using Word = detail::IndexWord<Count>;

    Word at_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class IndexRange<Count>;

    constexpr explicit IndexCursor(Word at) noexcept : at_{at} {}

public:
    [[nodiscard]] constexpr Index<Count> operator*() const noexcept {
        if (at_ >= Count) [[unlikely]] {
            fatal("a read through the end cursor of an IndexRange");
        }
        return Index<Count>{at_};
    }

    constexpr IndexCursor& operator++() noexcept {
        if (at_ >= Count) [[unlikely]] {
            fatal("a step past the end cursor of an IndexRange");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(IndexCursor const& left, IndexCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// Each Index<Count> in order, from 0.  An IndexRange holds no state, so any
// code can make one: it gives exactly the indexes below Count.
template <std::size_t Count>
    requires IndexCount<Count>
class IndexRange final {
public:
    [[nodiscard]] static constexpr IndexCursor<Count> begin() noexcept { return IndexCursor<Count>{0}; }
    [[nodiscard]] static constexpr IndexCursor<Count> end() noexcept {
        return IndexCursor<Count>{static_cast<detail::IndexWord<Count>>(Count)};
    }
};

}  // namespace foundation::core
