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
//   CyclicIndex<N>    a position that goes around the first live positions
//                     of N, where the live count only decreases, such as the
//                     turn of a ring of lanes.  at() gives an Index<N>.
//
// An operation that takes an Index<N>, such as at() of a FixedArray<T, N>,
// does no check: the type proves the bound, and the check ran one time,
// where the index was made.  The only roads to an Index are check(), mask()
// for a count that is a power of two, the conversion from a StaticIndex, the
// conversion from an Index of a smaller N, the loop of an IndexRange, the
// position of a CyclicIndex, and the walk of the keys of a BitSet<N>, whose
// road states its proof.  The constructor from a number is private, and the
// seal refuses std::bit_cast and a lifetime start over bytes, so no number
// becomes an Index without its check.  The one friend that a different
// header defines is the cursor of that walk, so no other class of a
// different header makes an Index with no check.
//
// Check after a compare
//   A word that a different invariant already bounds, such as a position of
//   a loop or the tag of a slot, takes check() too.  When the code before it
//   compares the word with a bound that is at most N, the optimizer knows the
//   answer of check(), and the Index costs no instruction.  The count of a
//   power of two takes mask(), one and, with no compare.
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

template <std::size_t Count>
    requires IndexCount<Count>
class CyclicIndex;

// The cursor of the walk of the keys of a bit set (foundation/core/BitSet.h),
// the one friend of the road of_bit_of_set_.
template <class Member, class Word>
class BitCursor;

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
    friend class CyclicIndex<Count>;
    friend struct niche<Index>;

    constexpr explicit Index(Word value) noexcept : value_{value} {}

    // The road of the walk of the keys of a BitSet<Count>
    // (foundation/core/BitSet.h): the cursor of the walk gives the index of
    // the position of each bit that it reads, with no check.  The proof has
    // two parts.  Each door of the set keeps each bit at or past Count clear,
    // with the mask of the top word.  And the frame door with_keys keeps the
    // walk inside the life of the set, because a cursor does not copy and
    // does not move.  So the position of each bit that the walk reads lies
    // below Count.  The cursor is the one friend of the road, and its home
    // row of utils/rule-table.txt admits its one definition only in BitSet.h.
    [[nodiscard]] static constexpr Index of_bit_of_set_(std::size_t position) noexcept {
        return Index{static_cast<Word>(position)};
    }

    template <class Member, class BitWord>
    friend class BitCursor;

public:
    // The index raw, or no value when raw is not below Count.  One compare.
    // The result is an Option<Index<Count>>.  The return type is deduced, so
    // the Option is named only where this class is complete.
    [[nodiscard]] static constexpr auto check(std::size_t raw) noexcept {
        using Result = Option<Index>;
        if (raw >= Count) return Result{none};
        return Result::some(Index{static_cast<Word>(raw)});
    }

    // The index of word modulo Count, a power of two: the low bits of word,
    // with one and and no compare.  A word below Count gives itself.
    [[nodiscard]] static constexpr Index mask(std::size_t word) noexcept
        requires((Count & (Count - 1)) == 0)
    {
        return Index{static_cast<Word>(word & (Count - 1))};
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
    [[nodiscard]] static constexpr Index<Count> empty(niche_key<Index<Count>> const&) noexcept {
        return Index<Count>{static_cast<detail::IndexWord<Count>>(Count)};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<Index<Count>> const&, Index<Count> const& index) noexcept {
        return index.value_ == Count;
    }
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

// A position that goes around the first live positions of Count, such as the
// turn of a ring of lanes.  The live count starts at Count and only
// decreases: drop() removes the position of the cursor, and the caller moves
// the elements after it down by one.  The position stays below the live count
// while a position lives, and it is zero when no position lives.  So at() is
// an Index<Count> with no check, and step() is one compare and one select.
// The seal refuses std::bit_cast and a lifetime start over bytes, so no state
// past these bounds is made.
template <std::size_t Count>
    requires IndexCount<Count>
class CyclicIndex final {
    using Word = detail::IndexWord<Count>;

    Word at_ = 0;
    Word live_ = static_cast<Word>(Count);
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // Each of the Count positions lives, and the cursor is at position 0.
    constexpr CyclicIndex() noexcept = default;

    // The position of the cursor, which is below Count in each state.
    [[nodiscard]] constexpr Index<Count> at() const noexcept { return Index<Count>{at_}; }

    // The count of the live positions, from 0 to Count.
    [[nodiscard]] constexpr std::size_t live() const noexcept {
        std::size_t const live = live_;
        [[assume(live <= Count)]];
        return live;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return live_ == 0; }

    // A step to the next live position, and from the last live position back
    // to position 0.  With no live position, the cursor stays at position 0.
    constexpr void step() noexcept {
        Word const next = static_cast<Word>(at_ + 1);
        at_ = next < live_ ? next : Word{0};
    }

    // Removes the position of the cursor from the live positions.  The cursor
    // then names the position after it, which moves down by one, or position
    // 0 after the last.  A drop with no live position ends the process in each
    // build.
    constexpr void drop() noexcept {
        if (live_ == 0) [[unlikely]] {
            fatal("a drop of a position of a cyclic index with no live position");
        }
        --live_;
        if (at_ == live_) at_ = 0;
    }
};

}  // namespace foundation::core
