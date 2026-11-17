#pragma once

// The bit sets of the Region family: a set of indexes, with one bit for each
// index.
//
//   BitSet<N>           a set of the indexes Index<N>, in N bits in the object
//                       itself.  The type of an index proves it below N, so no
//                       verb checks it.  A copy copies the bits
//   HeapBitSet<Tag>     a set of the ids Id<Tag, std::uint32_t> below a count
//                       that its mint fixes, in bits on the heap.  The tag names
//                       the domain of the ids, such as the slots of a plan, so
//                       an id of a different domain does not compile.  The
//                       count is a run-time value, so each verb that takes an
//                       id compares it with the count, and an id at or past
//                       the count ends the process in each build
//
// THE MINT
//   mint_heap_bit_set<Tag>(alloc, count)
//                       the empty set of count ids, in new storage that holds
//                       zero bits.  A count of zero allocates nothing
//
// THE OPERATIONS
//   set(index), reset(index)    put the index into the set, take it out
//   test(index)                 true when the set holds the index
//   size()                      the count of the indexes that the set holds:
//                               the popcount of each word.  The verb table of
//                               the base gives size() for each count, so the
//                               popcount has no name of its own
//   is_empty()                  true when the set holds no index
//   clear()                     takes each index out
//   for (index : set)           each index of the set, in the order of the
//                               indexes.  The walk reads the words from the
//                               first, so no order comes from an address or a
//                               hash
//   unite(other)                each index of other joins this set
//   intersect(other)            only the indexes that other holds stay
//   subtract(other)             each index of other leaves this set
//   flip(other)                 each index of other changes its state: the
//                               symmetric difference
//   is_subset_of(other)         true when other holds each index of this set
//   is_disjoint_from(other)     true when no index is in the two sets
//   ==                          true when the two sets hold the same indexes
//   extent()                    of a HeapBitSet: the count of its mint
//
// Each operation of two sets takes the other set as its one parameter, so no
// signature has two parameters of one type.  Two BitSets of one N share the
// index type, because each Index<N> is below N in the two sets.  An Index<M>
// with M < N converts to an Index<N>, and an Index<M> with M > N does not, so
// an index of a larger set does not reach a smaller one.  The four changes of
// two HeapBitSets need one count: two counts that differ give LengthMismatch,
// and the change writes nothing.  The three queries read the two heap sets as
// sets of ids, so two counts can differ.
//
// THE WORDS
//   A set holds its bits in words of 64 bits: the index i is the bit i % 64 of
//   the word i / 64.  The bits past the last index of the last word stay zero
//   in each state, because no verb sets one and each operation of two sets
//   keeps a zero bit zero.  So size(), ==, the queries and the walk read
//   whole words with no mask.
//
// A HeapBitSet owns its words alone, on the owner core of
// foundation/core/Owner.h.  A move gives the words to the target and leaves
// the source empty, with the count zero.  A copy would allocate with no
// allocation capability at its call, so a HeapBitSet does not copy.  The
// default HeapBitSet is the empty set of no id.

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

template <std::size_t N>
    requires IndexCount<N>
class BitSet;

template <class Tag>
    requires std::is_class_v<Tag>
class HeapBitSet;

// The id of a HeapBitSet of the domain Tag.
template <class Tag>
using BitId = Id<Tag, std::uint32_t>;

namespace detail {

// The bits of a word.
inline constexpr std::size_t bits_of_word_ = 64;

// The count of the words that hold count bits.  Complexity: O(1).
[[nodiscard]] constexpr std::size_t word_count_of_(std::size_t count) noexcept {
    return (count + bits_of_word_ - 1) / bits_of_word_;
}

// The word with the one bit of an index in its word.
[[nodiscard]] constexpr std::uint64_t bit_of_(std::size_t index) noexcept {
    return std::uint64_t{1} << (index % bits_of_word_);
}

// The member of a set at a bit that the set proves below its count.
template <class Member>
struct BitMember;

template <std::size_t N>
struct BitMember<Index<N>> {
    [[nodiscard]] static constexpr Index<N> of_(std::size_t bit) noexcept { return Index<N>{IndexDoor::key(), bit}; }
};

template <class Tag>
struct BitMember<BitId<Tag>> {
    [[nodiscard]] static constexpr BitId<Tag> of_(std::size_t bit) noexcept {
        return BitId<Tag>{static_cast<std::uint32_t>(bit)};
    }
};

// The fatal exits of a cursor at its end, and of an id at or past the count
// of a HeapBitSet.  The calls take no argument, so each inlined check adds
// only a compare and a branch to one call.
[[noreturn]] CRUCIBLE_COLD inline void report_bit_cursor_at_end_() noexcept {
    fatal("a read or a step through the end cursor of a walk of a bit set");
}

[[noreturn]] CRUCIBLE_COLD inline void report_bit_id_outside_() noexcept {
    fatal("a HeapBitSet got an id at or past the count of its mint");
}

namespace owner {

// The fields of a HeapBitSet: the words, and the count of the ids.  The words
// are null when the count is zero.  The release gives the words back with
// the bytes of their allocation.
struct HeapBitSetState {
    std::uint64_t* words = nullptr;
    std::uint32_t count = 0;
    std::uint32_t padding_ = 0;

    [[nodiscard]] static constexpr HeapBitSetState emptied(HeapBitSetState const& /*state*/) noexcept {
        return HeapBitSetState{};
    }

    static constexpr void release(HeapBitSetState const& state) noexcept {
        if (state.words != nullptr) {
            heap_release_(static_cast<void*>(state.words), word_count_of_(state.count) * sizeof(std::uint64_t),
                          alignof(std::uint64_t));
        }
    }
};

}  // namespace owner

}  // namespace detail

// The position of a walk over the indexes of a bit set: the word of the next
// index, and the bits of that word from that index on.  At the end the word
// is the count of the words and the bits are zero.  Only the sets build a
// cursor, and a cursor borrows the words of its set.  A read or a step at the
// end ends the process in each build, so a cursor never makes an index past
// its set.
template <class Member>
class BitCursor final {
    std::uint64_t const* words_ = nullptr;
    std::size_t word_ = 0;
    std::size_t word_count_ = 0;
    std::uint64_t bits_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t N>
        requires IndexCount<N>
    friend class BitSet;
    template <class Tag>
        requires std::is_class_v<Tag>
    friend class HeapBitSet;

    // The cursor at the first index of the words, or at their end.
    constexpr explicit BitCursor(View<std::uint64_t const> words) noexcept : word_count_{words.size()} {
        if (word_count_ != 0) {
            words_ = first_of_(detail::ViewDoor::key(), words);
            bits_ = words_[0];
            skip_empty_words_();
        }
    }

    // Moves to the first word with a bit, or to the end.  Complexity: linear
    // in the words that hold no bit.
    constexpr void skip_empty_words_() noexcept {
        while (bits_ == 0 && ++word_ < word_count_) {
            bits_ = words_[word_];
        }
    }

    // The end cursor of a run of words.
    [[nodiscard]] static constexpr BitCursor end_of_(View<std::uint64_t const> words) noexcept {
        BitCursor cursor{View<std::uint64_t const>{}};
        cursor.word_ = words.size();
        cursor.word_count_ = words.size();
        return cursor;
    }

public:
    [[nodiscard]] constexpr Member operator*() const noexcept {
        if (bits_ == 0) [[unlikely]] {
            detail::report_bit_cursor_at_end_();
        }
        return detail::BitMember<Member>::of_(word_ * detail::bits_of_word_
                                              + static_cast<std::size_t>(countr_zero(bits_)));
    }

    constexpr BitCursor& operator++() noexcept {
        if (bits_ == 0) [[unlikely]] {
            detail::report_bit_cursor_at_end_();
        }
        bits_ &= bits_ - 1;
        skip_empty_words_();
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(BitCursor const& left, BitCursor const& right) noexcept {
        return left.word_ == right.word_ && left.bits_ == right.bits_;
    }
};

template <std::size_t N>
    requires IndexCount<N>
class BitSet final {
    static constexpr std::size_t word_count_ = detail::word_count_of_(N);
    using WordIndex = Index<word_count_>;

    FixedArray<std::uint64_t, word_count_> words_{};

    // The word of an index.  The index is below N, so its word is below the
    // count of the words, and the door of the base layer makes the Index of
    // the word with no check.
    [[nodiscard]] static constexpr WordIndex word_of_(Index<N> index) noexcept {
        return WordIndex{detail::IndexDoor::key(), index.raw() / detail::bits_of_word_};
    }

public:
    using index_type = Index<N>;

    // The empty set.
    constexpr BitSet() noexcept = default;

    constexpr void set(Index<N> index) & noexcept { words_.at(word_of_(index)) |= detail::bit_of_(index.raw()); }

    constexpr void reset(Index<N> index) & noexcept { words_.at(word_of_(index)) &= ~detail::bit_of_(index.raw()); }

    [[nodiscard]] constexpr bool test(Index<N> index) const noexcept {
        return (words_.at(word_of_(index)) & detail::bit_of_(index.raw())) != 0;
    }

    // The count of the indexes of the set.  Complexity: O(N / 64).
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t count = 0;
        for (std::uint64_t const word : words_) {
            count += static_cast<std::size_t>(popcount(word));
        }
        return count;
    }

    // Complexity: O(N / 64).
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        for (std::uint64_t const word : words_) {
            if (word != 0) {
                return false;
            }
        }
        return true;
    }

    // Complexity: O(N / 64).
    constexpr void clear() & noexcept {
        for (std::uint64_t& word : words_) {
            word = 0;
        }
    }

    // The walk `for (Index<N> index : set)`.  Complexity of a full walk:
    // O(N / 64 + size()).
    [[nodiscard]] constexpr BitCursor<Index<N>> begin() const& noexcept { return BitCursor<Index<N>>{words_.view()}; }
    [[nodiscard]] constexpr BitCursor<Index<N>> end() const& noexcept {
        return BitCursor<Index<N>>::end_of_(words_.view());
    }
    void begin() const&& = delete("a cursor of a temporary BitSet points into an object that the full expression ends");
    void end() const&& = delete("a cursor of a temporary BitSet points into an object that the full expression ends");

    // The four changes of THE OPERATIONS.  Each one keeps the zero bits past
    // N zero.  Complexity: O(N / 64).
    constexpr void unite(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            words_.at(word) |= other.words_.at(word);
        }
    }

    constexpr void intersect(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            words_.at(word) &= other.words_.at(word);
        }
    }

    constexpr void subtract(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            words_.at(word) &= ~other.words_.at(word);
        }
    }

    constexpr void flip(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            words_.at(word) ^= other.words_.at(word);
        }
    }

    // Complexity: O(N / 64).
    [[nodiscard]] constexpr bool is_subset_of(BitSet const& other) const noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if ((words_.at(word) & ~other.words_.at(word)) != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool is_disjoint_from(BitSet const& other) const noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if ((words_.at(word) & other.words_.at(word)) != 0) {
                return false;
            }
        }
        return true;
    }

    // The compare reads each word.  It is written by hand, because GCC 16
    // gives a wrong defaulted == of a class with an array in a constant
    // evaluation.  Complexity: O(N / 64).
    [[nodiscard]] friend constexpr bool operator==(BitSet const& left, BitSet const& right) noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if (left.words_.at(word) != right.words_.at(word)) {
                return false;
            }
        }
        return true;
    }
};

// The arguments of mint_heap_bit_set: a class that names the domain of the
// ids, and the allocation capability.
template <class Tag, class Marker>
concept CanMintHeapBitSet = std::is_class_v<Tag> && AllocationCapability<Marker>;

template <class Tag, class Marker>
    requires CanMintHeapBitSet<Tag, Marker>
[[nodiscard]] HeapBitSet<Tag> mint_heap_bit_set(Marker allocation,
                                                Quantity<unit::Element, std::uint32_t> count) noexcept;

template <class Tag>
    requires std::is_class_v<Tag>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] HeapBitSet final
    : public detail::OwnerCore<detail::owner::HeapBitSetState> {
    using State = detail::owner::HeapBitSetState;
    using Core = detail::OwnerCore<State>;

    // The words of a count of ids.  Only the mint calls it.
    constexpr explicit HeapBitSet(State const& state) noexcept : Core{state} {}

    template <class OtherTag, class Marker>
        requires CanMintHeapBitSet<OtherTag, Marker>
    friend HeapBitSet<OtherTag> mint_heap_bit_set(Marker allocation,
                                                  Quantity<unit::Element, std::uint32_t> count) noexcept;

    // The count of the words.
    [[nodiscard]] constexpr std::size_t word_count_() const noexcept { return detail::word_count_of_(state_.count); }

    // The words as a View.  The words live while the set holds them.
    [[nodiscard]] constexpr View<std::uint64_t const> words_() const noexcept {
        return View<std::uint64_t const>{detail::ViewDoor::key(), state_.words, word_count_()};
    }

    // The bit of an id, after the compare with the count.  An id at or past
    // the count ends the process, so no verb reads a word past the words.
    [[nodiscard]] constexpr std::size_t bit_of_(BitId<Tag> id) const noexcept {
        std::uint32_t const bit = id.raw();
        if (bit >= state_.count) [[unlikely]] {
            detail::report_bit_id_outside_();
        }
        return bit;
    }

public:
    using index_type = BitId<Tag>;

    // The empty set of no id.  It holds no storage.
    constexpr HeapBitSet() noexcept = default;

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // set.
    constexpr HeapBitSet(HeapBitSet&&) noexcept = default;
    constexpr HeapBitSet& operator=(HeapBitSet&&) noexcept = default;

    HeapBitSet(HeapBitSet const&) = delete("a copy of a HeapBitSet allocates, and no allocation capability shows at a "
                                           "copy");
    HeapBitSet& operator=(HeapBitSet const&) = delete("a copy of a HeapBitSet allocates, and no allocation capability "
                                                      "shows at a copy");

    // The count of the ids of the mint.
    [[nodiscard]] constexpr Quantity<unit::Element, std::uint32_t> extent() const noexcept {
        return Quantity<unit::Element, std::uint32_t>{state_.count};
    }

    // One compare of the id with the count, then the write of its bit.
    constexpr void set(BitId<Tag> id) & noexcept {
        std::size_t const bit = bit_of_(id);
        state_.words[bit / detail::bits_of_word_] |= detail::bit_of_(bit);
    }

    constexpr void reset(BitId<Tag> id) & noexcept {
        std::size_t const bit = bit_of_(id);
        state_.words[bit / detail::bits_of_word_] &= ~detail::bit_of_(bit);
    }

    [[nodiscard]] constexpr bool test(BitId<Tag> id) const noexcept {
        std::size_t const bit = bit_of_(id);
        return (state_.words[bit / detail::bits_of_word_] & detail::bit_of_(bit)) != 0;
    }

    // Complexity: O(extent() / 64).
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t count = 0;
        for (std::uint64_t const word : words_()) {
            count += static_cast<std::size_t>(popcount(word));
        }
        return count;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept {
        for (std::uint64_t const word : words_()) {
            if (word != 0) {
                return false;
            }
        }
        return true;
    }

    constexpr void clear() & noexcept {
        for (std::size_t word = 0; word != word_count_(); ++word) {
            state_.words[word] = 0;
        }
    }

    // The walk `for (BitId<Tag> id : set)`.  Complexity of a full walk:
    // O(extent() / 64 + size()).
    [[nodiscard]] constexpr BitCursor<BitId<Tag>> begin() const& noexcept { return BitCursor<BitId<Tag>>{words_()}; }
    [[nodiscard]] constexpr BitCursor<BitId<Tag>> end() const& noexcept {
        return BitCursor<BitId<Tag>>::end_of_(words_());
    }
    void begin() const&& = delete("a cursor of a temporary HeapBitSet points into storage that the full expression "
                                  "frees");
    void end() const&& = delete("a cursor of a temporary HeapBitSet points into storage that the full expression "
                                "frees");

    // The four changes of THE OPERATIONS.  Two counts that differ give
    // LengthMismatch, and the change writes nothing.  Complexity:
    // O(extent() / 64).
    [[nodiscard]] constexpr Result<Unit, LengthMismatch> unite(HeapBitSet const& other) & noexcept {
        if (other.state_.count != state_.count) {
            return err(LengthMismatch{});
        }
        for (std::size_t word = 0; word != word_count_(); ++word) {
            state_.words[word] |= other.state_.words[word];
        }
        return Unit{};
    }

    [[nodiscard]] constexpr Result<Unit, LengthMismatch> intersect(HeapBitSet const& other) & noexcept {
        if (other.state_.count != state_.count) {
            return err(LengthMismatch{});
        }
        for (std::size_t word = 0; word != word_count_(); ++word) {
            state_.words[word] &= other.state_.words[word];
        }
        return Unit{};
    }

    [[nodiscard]] constexpr Result<Unit, LengthMismatch> subtract(HeapBitSet const& other) & noexcept {
        if (other.state_.count != state_.count) {
            return err(LengthMismatch{});
        }
        for (std::size_t word = 0; word != word_count_(); ++word) {
            state_.words[word] &= ~other.state_.words[word];
        }
        return Unit{};
    }

    [[nodiscard]] constexpr Result<Unit, LengthMismatch> flip(HeapBitSet const& other) & noexcept {
        if (other.state_.count != state_.count) {
            return err(LengthMismatch{});
        }
        for (std::size_t word = 0; word != word_count_(); ++word) {
            state_.words[word] ^= other.state_.words[word];
        }
        return Unit{};
    }

    // The queries read the two sets as sets of ids.  The words past the
    // words of the shorter set hold no id of it.  Complexity:
    // O(max(extent(), other.extent()) / 64).
    [[nodiscard]] constexpr bool is_subset_of(HeapBitSet const& other) const noexcept {
        std::size_t const other_words = other.word_count_();
        for (std::size_t word = 0; word != word_count_(); ++word) {
            std::uint64_t const held = word < other_words ? other.state_.words[word] : 0;
            if ((state_.words[word] & ~held) != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool is_disjoint_from(HeapBitSet const& other) const noexcept {
        std::size_t const common = state_.count < other.state_.count ? word_count_() : other.word_count_();
        for (std::size_t word = 0; word != common; ++word) {
            if ((state_.words[word] & other.state_.words[word]) != 0) {
                return false;
            }
        }
        return true;
    }

    // True when the two sets hold the same ids.  The two words of one
    // position must be equal, and a word that only one set has must hold no
    // bit.  The relation is symmetric.  Complexity:
    // O(max(left.extent(), right.extent()) / 64).
    [[nodiscard]] friend constexpr bool operator==(HeapBitSet const& left, HeapBitSet const& right) noexcept {
        return left.is_subset_of(right) && right.is_subset_of(left);
    }
};

// The empty set of count ids: one allocation of the words, which hold zero
// bits.  A count of zero allocates nothing.  Complexity: one allocation and
// O(count / 64) writes.
template <class Tag, class Marker>
    requires CanMintHeapBitSet<Tag, Marker>
[[nodiscard]] HeapBitSet<Tag> mint_heap_bit_set(Marker /*allocation*/,
                                                Quantity<unit::Element, std::uint32_t> count) noexcept {
    std::uint32_t const ids = count.raw();
    if (ids == 0) {
        return HeapBitSet<Tag>{};
    }
    std::size_t const bytes = detail::word_count_of_(ids) * sizeof(std::uint64_t);
    void* const storage = detail::heap_allocate_(bytes, alignof(std::uint64_t));
    __builtin_memset(storage, 0, bytes);
    return HeapBitSet<Tag>{detail::owner::HeapBitSetState{static_cast<std::uint64_t*>(storage), ids, 0}};
}

}  // namespace foundation::core
