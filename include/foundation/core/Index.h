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
//   next_cyclic(), prev_cyclic()
//                     the index after and before an Index<N> in a ring of N
//                     slots: 0 after N - 1, and N - 1 before 0.  Each step is
//                     total, so a ring cursor needs no check
//   Ranged<Lo, Hi>    an integer in [Lo, Hi], whose range is a part of its
//                     type.  The range of a sum, a difference and a product
//                     comes from the ranges of the operands at compile time
//                     (Ranged below)
//
// An operation that takes an Index<N>, such as at() of a FixedArray<T, N>,
// does no check: the type proves the bound, and the check ran one time,
// where the index was made.  The only roads to an Index are check(), mask()
// for a count that is a power of two, the conversion from a StaticIndex, the
// conversion from an Index of a smaller N, a cyclic step of an Index, the
// conversion from a Ranged whose range is in [0, N), the loop of an
// IndexRange, the position of a CyclicIndex, and the walk of the keys of a
// BitSet<N>, whose road states its proof.  The constructor from a number is
// private, and the seal refuses std::bit_cast and a lifetime start over
// bytes, so no number becomes an Index without its check.  The one friend
// that a different header defines is the cursor of that walk, so no other
// class of a different header makes an Index with no check.
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

template <std::int64_t Lo, std::int64_t Hi>
    requires(Lo <= Hi)
class Ranged;

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

    // The next index of a ring of Count slots, and index 0 after the last.
    // A Count that is a power of two costs one add and one and, and each
    // other Count one compare and one select.  The index is below Count, and
    // Count fits the word, so the add does not wrap.
    [[nodiscard]] constexpr Index next_cyclic() const noexcept {
        if constexpr ((Count & (Count - 1)) == 0) {
            return Index{static_cast<Word>((value_ + Word{1}) & static_cast<Word>(Count - 1))};
        } else {
            auto const next = static_cast<Word>(value_ + Word{1});
            return Index{next == static_cast<Word>(Count) ? Word{0} : next};
        }
    }

    // The previous index of a ring of Count slots, and the last index before
    // index 0.  The costs are the costs of next_cyclic.
    [[nodiscard]] constexpr Index prev_cyclic() const noexcept {
        if constexpr ((Count & (Count - 1)) == 0) {
            return Index{static_cast<Word>((value_ - Word{1}) & static_cast<Word>(Count - 1))};
        } else {
            return Index{value_ == Word{0} ? static_cast<Word>(Count - 1) : static_cast<Word>(value_ - Word{1})};
        }
    }

    // The constant index I, which the type proves below Count.
    template <std::size_t I>
        requires(I < Count)
    constexpr Index(StaticIndex<I>) noexcept : value_{static_cast<Word>(I)} {}

    // An index below a smaller count is below Count too.
    template <std::size_t OtherCount>
        requires(OtherCount < Count)
    constexpr Index(Index<OtherCount> other) noexcept : value_{static_cast<Word>(other.raw())} {}

    // A Ranged whose range is in [0, Count) is an index below Count (Ranged
    // below).  The type of ranged depends on the template, so its body reads
    // the complete class.
    template <std::int64_t Lo, std::int64_t Hi>
        requires(Lo >= 0 && static_cast<std::uint64_t>(Hi) < Count)
    constexpr Index(Ranged<Lo, Hi> ranged) noexcept : value_{static_cast<Word>(ranged.raw())} {}

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

// Ranged
//   Ranged<Lo, Hi> holds an integer in [Lo, Hi].  The bounds are std::int64_t
//   constants, and the value is in the narrowest standard integer that holds
//   each value of the range.  The doors:
//     check(raw)          the Ranged of an Integer of each type, or no value
//                         when raw is outside [Lo, Hi].  The compare is exact
//                         for each type
//     constant<V>()       the Ranged of a constant that the type proves in
//                         [Lo, Hi] at compile time
//     an Index<N>         converts to each Ranged that holds [0, N - 1]
//     raw()               the number, in [Lo, Hi] in each state
//     a + b, a - b, a * b the exact result, in the range of the bounds: the
//                         sum of the bounds, the difference of the opposite
//                         bounds, and the least and the largest product of
//                         the bounds
//     narrow<L, H>()      the value in a smaller range, or no value when it
//                         is outside [L, H]
//   A Ranged converts with no check to each Ranged whose range holds its
//   range, and to each Index<N> when its range is in [0, N).  Each
//   conversion is a constructor of its target, and each operator is a hidden
//   friend, so the public members of the class are only names of its family.
//
//   The range of a result comes from the bounds of the operands at compile
//   time, in 128 bits.  A result range past std::int64_t does not compile, so
//   no operation overflows, and the code of an operation is the plain
//   operation of the words.  The constructor from a number is private, and
//   the seal refuses std::bit_cast and a lifetime start over bytes, so no
//   number outside the range becomes a Ranged.  An operation takes a Ranged
//   on the left, so a plain integer does not mix in it.

namespace detail {

__extension__ using RangedWide = __int128;

// True when a bound of a result is a bound that a Ranged can hold.
[[nodiscard]] consteval bool is_ranged_bound(RangedWide bound) noexcept {
    return bound >= RangedWide{min_of<std::int64_t>} && bound <= RangedWide{max_of<std::int64_t>};
}

[[nodiscard]] consteval RangedWide least_of_(RangedWide first, RangedWide second) noexcept {
    return first < second ? first : second;
}

[[nodiscard]] consteval RangedWide largest_of_(RangedWide first, RangedWide second) noexcept {
    return first < second ? second : first;
}

// The least and the largest product of a value of [LeftLo, LeftHi] and a
// value of [RightLo, RightHi]: the least and the largest product of the
// bounds.
template <std::int64_t LeftLo, std::int64_t LeftHi, std::int64_t RightLo, std::int64_t RightHi>
inline constexpr RangedWide least_product_ =
    least_of_(least_of_(RangedWide{LeftLo} * RightLo, RangedWide{LeftLo} * RightHi),
              least_of_(RangedWide{LeftHi} * RightLo, RangedWide{LeftHi} * RightHi));

template <std::int64_t LeftLo, std::int64_t LeftHi, std::int64_t RightLo, std::int64_t RightHi>
inline constexpr RangedWide largest_product_ =
    largest_of_(largest_of_(RangedWide{LeftLo} * RightLo, RangedWide{LeftLo} * RightHi),
                largest_of_(RangedWide{LeftHi} * RightLo, RangedWide{LeftHi} * RightHi));

// The narrowest standard integer that holds each value of [Lo, Hi].
template <std::int64_t Lo, std::int64_t Hi>
using RangedWord = std::conditional_t<
    (Lo >= 0),
    std::conditional_t<(Hi <= 0xFF), std::uint8_t,
                       std::conditional_t<(Hi <= 0xFFFF), std::uint16_t,
                                          std::conditional_t<(Hi <= 0xFFFFFFFF), std::uint32_t, std::uint64_t>>>,
    std::conditional_t<
        (Lo >= -0x80 && Hi <= 0x7F), std::int8_t,
        std::conditional_t<(Lo >= -0x8000 && Hi <= 0x7FFF), std::int16_t,
                           std::conditional_t<(Lo >= -0x80000000LL && Hi <= 0x7FFFFFFF), std::int32_t, std::int64_t>>>>;

// True when raw is in [Lo, Hi], for an Integer of each type: a signed value
// compares as a std::int64_t, and an unsigned value compares as a
// std::uint64_t with the bounds that are not negative.
template <std::int64_t Lo, std::int64_t Hi, Integer T>
[[nodiscard]] constexpr bool is_in_range_(T raw) noexcept {
    if constexpr (std::is_signed_v<T>) {
        auto const wide = static_cast<std::int64_t>(raw);
        return wide >= Lo && wide <= Hi;
    } else {
        if constexpr (Hi < 0) {
            return false;
        } else {
            auto const wide = static_cast<std::uint64_t>(raw);
            bool const is_above_lo = Lo <= 0 || wide >= static_cast<std::uint64_t>(Lo);
            return is_above_lo && wide <= static_cast<std::uint64_t>(Hi);
        }
    }
}

}  // namespace detail

template <std::int64_t Lo, std::int64_t Hi>
    requires(Lo <= Hi)
class Ranged final {
    using Word = detail::RangedWord<Lo, Hi>;

    Word value_ = static_cast<Word>(Lo);
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(OtherLo <= OtherHi)
    friend class Ranged;

    constexpr explicit Ranged(Word value) noexcept : value_{value} {}

    // The number as a std::int64_t, which holds each value of a range.
    [[nodiscard]] constexpr std::int64_t wide_() const noexcept { return static_cast<std::int64_t>(raw()); }

    // The Ranged of a value that the caller proves in [Lo, Hi].
    [[nodiscard]] static constexpr Ranged of_wide_(std::int64_t value) noexcept {
        return Ranged{static_cast<Word>(value)};
    }

    // The Ranged of the range Result of a value that the bounds of the
    // operands prove in that range.
    template <class Result>
    [[nodiscard]] static constexpr Result result_of_(std::int64_t value) noexcept {
        return Result::of_wide_(value);
    }

public:
    static constexpr std::int64_t lo = Lo;
    static constexpr std::int64_t hi = Hi;

    // The Ranged of raw, or no value when raw is outside [Lo, Hi].  Two
    // compares.  The result is an Option<Ranged>.  The return type is
    // deduced, so the Option is named only where this class is complete.
    template <Integer T>
    [[nodiscard]] static constexpr auto check(T raw) noexcept {
        using Result = Option<Ranged>;
        if (!detail::is_in_range_<Lo, Hi>(raw)) return Result{none};
        return Result::some(Ranged{static_cast<Word>(raw)});
    }

    // The constant Value, which the type proves in [Lo, Hi].
    template <std::int64_t Value>
        requires(Lo <= Value && Value <= Hi)
    [[nodiscard]] static constexpr Ranged constant() noexcept {
        return Ranged{static_cast<Word>(Value)};
    }

    // An index below Count is a value of [0, Count - 1].
    template <std::size_t Count>
        requires(IndexCount<Count> && Lo <= 0 && Hi >= 0 && Count - 1 <= static_cast<std::uint64_t>(Hi))
    constexpr Ranged(Index<Count> index) noexcept : value_{static_cast<Word>(index.raw())} {}

    // The number, which is in [Lo, Hi] in each state.  The optimizer reads
    // the bounds.
    [[nodiscard]] constexpr Word raw() const noexcept {
        Word const value = value_;
        [[assume(static_cast<std::int64_t>(value) >= Lo && static_cast<std::int64_t>(value) <= Hi)]];
        return value;
    }

    // A range that holds the range of other takes each value of other with no
    // check.
    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(Lo <= OtherLo && OtherHi <= Hi && (OtherLo != Lo || OtherHi != Hi))
    constexpr Ranged(Ranged<OtherLo, OtherHi> other) noexcept : value_{static_cast<Word>(other.raw())} {}

    // The value in the smaller range [OtherLo, OtherHi], or no value when it
    // is outside.  An Option of the Ranged of that range.
    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(Lo <= OtherLo && OtherHi <= Hi && OtherLo <= OtherHi)
    [[nodiscard]] constexpr auto narrow() const noexcept {
        return Ranged<OtherLo, OtherHi>::check(raw());
    }

    // The operators are hidden friends, and they build each result through
    // result_of_, a member that each other Ranged befriends.
    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(detail::is_ranged_bound(detail::RangedWide{Lo} + OtherLo)
                 && detail::is_ranged_bound(detail::RangedWide{Hi} + OtherHi))
    [[nodiscard]] friend constexpr Ranged<Lo + OtherLo, Hi + OtherHi>
    operator+(Ranged left, Ranged<OtherLo, OtherHi> right) noexcept {
        return result_of_<Ranged<Lo + OtherLo, Hi + OtherHi>>(left.wide_() + static_cast<std::int64_t>(right.raw()));
    }

    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(detail::is_ranged_bound(detail::RangedWide{Lo} - OtherHi)
                 && detail::is_ranged_bound(detail::RangedWide{Hi} - OtherLo))
    [[nodiscard]] friend constexpr Ranged<Lo - OtherHi, Hi - OtherLo>
    operator-(Ranged left, Ranged<OtherLo, OtherHi> right) noexcept {
        return result_of_<Ranged<Lo - OtherHi, Hi - OtherLo>>(left.wide_() - static_cast<std::int64_t>(right.raw()));
    }

    template <std::int64_t OtherLo, std::int64_t OtherHi>
        requires(detail::is_ranged_bound(detail::least_product_<Lo, Hi, OtherLo, OtherHi>)
                 && detail::is_ranged_bound(detail::largest_product_<Lo, Hi, OtherLo, OtherHi>))
    [[nodiscard]] friend constexpr auto operator*(Ranged left, Ranged<OtherLo, OtherHi> right) noexcept {
        using Product = Ranged<static_cast<std::int64_t>(detail::least_product_<Lo, Hi, OtherLo, OtherHi>),
                               static_cast<std::int64_t>(detail::largest_product_<Lo, Hi, OtherLo, OtherHi>)>;
        return result_of_<Product>(left.wide_() * static_cast<std::int64_t>(right.raw()));
    }

    [[nodiscard]] friend constexpr bool operator==(Ranged left, Ranged right) noexcept {
        return left.value_ == right.value_;
    }
    [[nodiscard]] friend constexpr auto operator<=>(Ranged left, Ranged right) noexcept {
        return left.value_ <=> right.value_;
    }
};

}  // namespace foundation::core
