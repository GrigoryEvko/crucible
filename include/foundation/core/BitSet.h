#pragma once

// The bit sets of the Region family: a set of keys, with one bit for each
// key.
//
//   BitSet<N, Key>      a set of the keys of N positions, in the smallest
//                       unsigned word that holds N bits in the object itself:
//                       one byte up to 8 positions, two bytes up to 16, four
//                       bytes up to 32 and eight bytes up to 64, and words of
//                       64 bits past 64 positions.  A copy copies the bits.
//                       A seal refuses a byte image, so the mint from a word
//                       is the one road from bits (A PACKED RECORD)
//   HeapBitSet<Tag>     a set of the ids Id<Tag, std::uint32_t> below a count
//                       that its mint fixes, in bits on the heap.  The tag names
//                       the domain of the ids, such as the slots of a plan, so
//                       an id of a different domain does not compile.  The
//                       count is a run-time value, so each verb that takes an
//                       id compares it with the count, and an id at or past
//                       the count ends the process in each build
//
// THE KEYS OF A BitSet
//   The key type is the index type of the domain of the set, so a set of the
//   scalar slots of an op refuses an argument position.  Four kinds of key
//   pass, and the key is Index<N> when the set names none:
//   Index<N>            the type proves the key below N, so no verb checks it.
//                       An Index<M> with M < N converts to an Index<N>, and an
//                       Index<M> with M > N does not
//   Side<Tag, Index<N>> the Index<N> in the role Tag (foundation/core/Side.h).
//                       The Index proves the key below N, so no verb checks
//                       it, and a Side converts to no Side of another tag.  So
//                       the sets of two domains of one count, or of two counts
//                       where the smaller Index converts, take keys that never
//                       mix
//   a scoped enum       each enumerator must lie below N, which the check
//                       reads by reflection.  A value of the enum that no
//                       enumerator names can still exist, so each verb compares
//                       the key with N
//   Id<Tag, Word>       N must not pass the largest Word, and each verb
//                       compares the key with N
//   A key past N ends the process in each build.  The walk gives the keys of
//   the set in the type of the key.
//
// THE MINTS
//   mint_heap_bit_set<Tag>(alloc, count)
//                       the empty set of count ids, in new storage that holds
//                       zero bits.  A count of zero allocates nothing
//   mint_bit_set_from_bits<N, Key>(bits)
//                       the BitSet of N positions of at most 64 that holds the
//                       bits of a word of its own word type, such as a field
//                       of a packed record, or no value when the word holds a
//                       bit at or past N.  bits() of a set of at most 64
//                       positions is the word back
//
// A PACKED RECORD
//   The seal makes a BitSet not trivially copyable, so a record that the code
//   copies by its bytes, such as an entry of a trace, holds no BitSet.  Such a
//   record stores the raw word of the set: bits() gives the word out, and
//   mint_bit_set_from_bits gives the set back as an Option, with no value for
//   a word that holds a bit at or past N.  So no set with a bit past N exists,
//   and two byte images of one set hold one word and give one digest.
//
// THE OPERATIONS
//   set(key), reset(key)        put the key into the set, take it out
//   test(key)                   true when the set holds the key
//   size()                      the count of the keys that the set holds: the
//                               popcount of each word.  The verb table of the
//                               base gives size() for each count, so the
//                               popcount has no name of its own
//   is_empty()                  true when the set holds no key
//   clear()                     takes each key out
//   with_keys(body)             lends body the walk of the keys of the set
//                               (THE WALK), so `for (key : keys)` gives each
//                               key of the set in the order of the positions.
//                               The walk reads the words from the first, so no
//                               order comes from an address or a hash
//   unite(other)                each key of other joins this set
//   intersect(other)            only the keys that other holds stay
//   subtract(other)             each key of other leaves this set
//   flip(other)                 each key of other changes its state: the
//                               symmetric difference
//   is_subset_of(other)         true when other holds each key of this set
//   is_disjoint_from(other)     true when no key is in the two sets
//   ==                          true when the two sets hold the same keys
//   extent()                    of a HeapBitSet: the count of its mint
//
// Each operation of two sets takes the other set as its one parameter, so no
// signature has two parameters of one type.  Two BitSets of one N and one key
// type are one type, because each key of that type is below N in the two
// sets.  The four changes of two HeapBitSets need one count: two counts that
// differ give LengthMismatch, and the change writes nothing.  The three
// queries read the two heap sets as sets of ids, so two counts can differ.
//
// THE WORDS
//   A set holds its bits in words: the position i is the bit i % W of the word
//   i / W, for the W bits of a word.  The bits past the last position of the
//   last word stay zero in each state, because no verb sets one, the mint from
//   a word refuses one, and each operation of two sets keeps a zero bit zero.
//   So size(), ==, the queries and the walk read whole words with no mask.
//
// THE WALK
//   The frame door with_keys lends its body a walk of the keys, BitKeys, for
//   the time of the call.  The walk and its cursors do not copy and do not
//   move, and a set has no begin() and no end(), so no cursor outlives its
//   set.  So the words that a cursor reads are the words of the live set, and
//   by THE WORDS each of their bits lies below N.  A cursor of a BitSet<N>
//   then makes each Index<N> through the road of the walk of Index.h, with no
//   check.  The ids of a HeapBitSet prove no bound, so each verb that takes
//   an id compares it with the count.
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
#include <foundation/core/Number.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Side.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The id of a HeapBitSet of the domain Tag.
template <class Tag>
using BitId = Id<Tag, std::uint32_t>;

template <class Member, class Word>
class BitCursor;

namespace detail {

// The bits of a word of a HeapBitSet, and of a BitSet past 64 positions.
inline constexpr std::size_t bits_of_word_ = 64;

// The count of the words of 64 bits that hold count bits.  Complexity: O(1).
[[nodiscard]] constexpr std::size_t word_count_of_(std::size_t count) noexcept {
    return (count + bits_of_word_ - 1) / bits_of_word_;
}

// The word of a BitSet of N positions: the smallest unsigned word that holds N
// bits, and a word of 64 bits past 64 positions.
template <std::size_t N>
using BitWordOf = std::conditional_t<
    (N <= 8), std::uint8_t,
    std::conditional_t<(N <= 16), std::uint16_t, std::conditional_t<(N <= 32), std::uint32_t, std::uint64_t>>>;

// The bits of a word.
template <class Word>
inline constexpr std::size_t bits_in_ = sizeof(Word) * 8;

// A value of 64 bits as a word, which holds each bit of the value that the
// caller keeps.  The word of 64 bits takes the value with no cast.
template <class Word>
[[nodiscard]] constexpr Word word_from_(std::uint64_t value) noexcept {
    if constexpr (std::is_same_v<Word, std::uint64_t>) {
        return value;
    } else {
        return static_cast<Word>(value);
    }
}

// The bit of a position in a word of 64 bits.
[[nodiscard]] constexpr std::uint64_t bit_of_(std::size_t position) noexcept {
    return std::uint64_t{1} << (position % bits_of_word_);
}

// The place of the bit of a position in a word of 64 bits.
[[nodiscard]] constexpr unsigned shift_of_(std::size_t position) noexcept {
    return static_cast<unsigned>(position % bits_of_word_);
}

// The bit at a place of a word, as a bool.  The shift and the mask give the
// bool with no compare, so -fharden-compares adds no second compare, and the
// test has the instructions of the test of a raw bitmap.
[[nodiscard]] constexpr bool holds_bit_(std::uint64_t word, unsigned shift) noexcept {
    return static_cast<bool>((word >> shift) & 1u);
}

// A position as a value of Word, which holds it.
template <class Word>
[[nodiscard]] constexpr Word position_as_(std::size_t position) noexcept {
    if constexpr (std::is_same_v<Word, std::size_t>) {
        return position;
    } else {
        return static_cast<Word>(position);
    }
}

// True when an unsigned word of the bytes of type holds each position below
// count.  A word of 8 bytes holds each count of a BitSet.
[[nodiscard]] consteval bool word_holds_positions_(std::meta::info type, std::size_t count) {
    std::size_t const bytes = std::meta::size_of(type);
    return bytes >= sizeof(std::size_t) || count - 1 <= (std::size_t{1} << (8 * bytes)) - 1;
}

// True when type is an Id whose word holds each position below count.  The
// largest word of an Id is the empty value of its niche, so it holds no
// position.
[[nodiscard]] consteval bool is_id_of_positions_(std::meta::info type, std::size_t count) {
    std::meta::info const bare = std::meta::dealias(type);
    if (!std::meta::has_template_arguments(bare) || std::meta::template_of(bare) != ^^Id) return false;
    std::size_t const bytes = std::meta::size_of(std::meta::template_arguments_of(bare)[1]);
    return bytes >= sizeof(std::size_t) || count <= (std::size_t{1} << (8 * bytes)) - 1;
}

// True when the key is no scoped enum, or when each enumerator of the enum
// lies below count.  Linear in the enumerators, at compile time only.
template <class Key>
[[nodiscard]] consteval bool enumerators_below_(std::size_t count) {
    if constexpr (std::is_scoped_enum_v<Key>) {
        using Raw = std::make_unsigned_t<std::underlying_type_t<Key>>;
        std::size_t const declared = std::meta::enumerators_of(^^Key).size();
        for (std::size_t index = 0; index < declared; ++index) {
            Raw const value = static_cast<Raw>(std::meta::extract<Key>(std::meta::enumerators_of(^^Key)[index]));
            if (std::size_t{value} >= count) return false;
        }
    }
    return true;
}

// THE KEYS OF A BitSet: the position of each kind of key.  The walk of a set
// makes the key at a position itself, from the positions of its own bits, so
// no function of the base makes a key from a number with no check.  A
// BitKeyKind holds no key and makes none, so it is no passkey.
template <class Key>
struct BitKeyKind;

template <std::size_t M>
struct BitKeyKind<Index<M>> {
    static constexpr bool is_proven = true;

    [[nodiscard]] static constexpr std::size_t position_of_(Index<M> key) noexcept { return key.raw(); }
};

template <class Tag, std::size_t M>
struct BitKeyKind<Side<Tag, Index<M>>> {
    static constexpr bool is_proven = true;

    [[nodiscard]] static constexpr std::size_t position_of_(Side<Tag, Index<M>> const& key) noexcept {
        return key.value().raw();
    }
};

template <class Tag, UnsignedInteger Word>
struct BitKeyKind<Id<Tag, Word>> {
    static constexpr bool is_proven = false;

    [[nodiscard]] static constexpr std::size_t position_of_(Id<Tag, Word> key) noexcept {
        return std::size_t{key.raw()};
    }
};

template <class E>
    requires std::is_scoped_enum_v<E>
struct BitKeyKind<E> {
    using Raw = std::make_unsigned_t<std::underlying_type_t<E>>;

    static constexpr bool is_proven = false;

    [[nodiscard]] static constexpr std::size_t position_of_(E key) noexcept {
        return std::size_t{static_cast<Raw>(key)};
    }
};

// The fatal exits of a cursor at its end, of a key past the positions of a
// BitSet, and of an id at or past the count of a HeapBitSet.  The calls take
// no argument, so each inlined check adds only a compare and a branch to one
// call.
[[noreturn]] CRUCIBLE_COLD inline void report_bit_cursor_at_end_() noexcept {
    fatal("a read or a step through the end cursor of a walk of a bit set");
}

[[noreturn]] CRUCIBLE_COLD inline void report_bit_key_outside_() noexcept {
    fatal("a BitSet got a key at or past its count of positions");
}

[[noreturn]] CRUCIBLE_COLD inline void report_bit_id_outside_() noexcept {
    fatal("a HeapBitSet got an id at or past the count of its mint");
}

namespace owner {

// The fields of a HeapBitSet: the words, and the count of the ids.  The words
// are null when the count is zero.  The release gives the words back with
// the bytes of their allocation.
struct HeapBitSetState {
    using owner_state_mark = OwnerStateMark;

    std::uint64_t* words = nullptr;
    std::uint32_t count = 0;
    std::uint32_t padding_ = 0;

    [[nodiscard]] static constexpr HeapBitSetState emptied(HeapBitSetState const& /*state*/) noexcept {
        return HeapBitSetState{};
    }

    static constexpr void release(HeapBitSetState const& state) noexcept {
        if (state.words != nullptr) {
            heap_release_(static_cast<void*>(state.words), slots_layout_<std::uint64_t>(word_count_of_(state.count)));
        }
    }
};

}  // namespace owner

}  // namespace detail

// A key of a role: the Index<N> of a set in the role of a tag.  A key with no
// member tag_type is no Side, and the atomic constraint is then false.
template <class Key, std::size_t N>
concept RoleIndexKey = std::is_same_v<Key, Side<typename Key::tag_type, Index<N>>>;

// A key of a BitSet of N positions (THE KEYS OF A BitSet): Index<N>, the Side
// of an Index<N>, a scoped enum whose word holds each position below N, or an
// Id whose word holds each position below N.  The gate reads no template that
// a unit can specialize.  The class of the set reads the enumerators of an
// enum key.
template <class Key, std::size_t N>
concept BitSetKey =
    std::is_same_v<Key, Index<N>> || RoleIndexKey<Key, N>
    || (std::is_scoped_enum_v<Key> && detail::word_holds_positions_(^^Key, N)) || detail::is_id_of_positions_(^^Key, N);

template <std::size_t N, class Key = Index<N>>
    requires IndexCount<N> && BitSetKey<Key, N>
class BitSet;

template <class Tag>
    requires std::is_class_v<Tag>
class HeapBitSet;

// The arguments of mint_bit_set_from_bits: a set of at most 64 positions and
// a word of the word type of that set.
template <std::size_t N, class Key, class Word>
concept CanMintBitSetFromBits =
    IndexCount<N> && BitSetKey<Key, N> && (N <= 64) && std::is_same_v<Word, detail::BitWordOf<N>>;

template <std::size_t N, class Key = Index<N>, class Word>
    requires CanMintBitSetFromBits<N, Key, Word>
[[nodiscard]] constexpr Option<BitSet<N, Key>> mint_bit_set_from_bits(Word bits) noexcept;

template <class Member, class Word>
class BitKeys;

namespace detail {

// The mark of the constructor of the end cursor of a run of words.
struct BitCursorEnd final {};

}  // namespace detail

// The position of a walk over the keys of a bit set: the word of the next
// key, and the bits of that word from that key on.  At the end the word is
// the count of the words and the bits are zero.  Only the walk of a set
// (BitKeys) builds a cursor, and a cursor borrows the words of its set.  A
// cursor does not copy and does not move, so it lives only in the frame door
// with_keys of its set, and each key that it makes is a key of the live set.
// A read or a step at the end ends the process in each build.
template <class Member, class Word>
class BitCursor final {
    Word const* words_ = nullptr;
    std::size_t word_ = 0;
    std::size_t word_count_ = 0;
    std::uint64_t bits_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class BitKeys<Member, Word>;

    // The cursor at the first key of the words, or at their end.
    constexpr explicit BitCursor(View<Word const> words) noexcept : word_count_{words.size()} {
        if (word_count_ != 0) {
            words_ = first_of_(detail::BaseLayerRead{}, words);
            bits_ = words_[0];
            skip_empty_words_();
        }
    }

    // The end cursor of a run of words.
    constexpr BitCursor(detail::BitCursorEnd, View<Word const> words) noexcept
        : word_{words.size()}, word_count_{words.size()} {}

    // Moves to the first word with a bit, or to the end.  Complexity: linear
    // in the words that hold no bit.
    constexpr void skip_empty_words_() noexcept {
        while (bits_ == 0 && ++word_ < word_count_) {
            bits_ = words_[word_];
        }
    }

public:
    BitCursor(BitCursor const&) = delete("a cursor borrows the words of its set: a copy could outlive the set");
    BitCursor& operator=(BitCursor const&) = delete("a cursor borrows the words of its set: a copy could outlive "
                                                    "the set");
    BitCursor(BitCursor&&) = delete("a cursor borrows the words of its set: a move could carry it past the set");
    BitCursor& operator=(BitCursor&&) = delete("a cursor borrows the words of its set: a move could carry it past "
                                               "the set");
    ~BitCursor() = default;

    [[nodiscard]] constexpr Member operator*() const noexcept {
        if (bits_ == 0) [[unlikely]] {
            detail::report_bit_cursor_at_end_();
        }
        // The position comes from a bit of the live set, so it is below the
        // count of the set, and the key of each kind holds it.  A proven key
        // takes the road of its Index for the walk of a set
        // (Index::of_bit_of_set_), whose comment states the proof.
        std::size_t const position = word_ * detail::bits_in_<Word> + static_cast<std::size_t>(countr_zero(bits_));
        if constexpr (std::is_scoped_enum_v<Member>) {
            return static_cast<Member>(position);
        } else if constexpr (requires { typename Member::tag_type; }) {
            using Proven = typename Member::value_type;
            return Member{Proven::of_bit_of_set_(position)};
        } else if constexpr (detail::BitKeyKind<Member>::is_proven) {
            return Member::of_bit_of_set_(position);
        } else {
            using IdWord = std::remove_cvref_t<decltype(std::declval<Member const&>().raw())>;
            return Member{detail::position_as_<IdWord>(position)};
        }
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

// The walk of the keys of a bit set, which the frame door with_keys of the
// set lends to its body for the time of the call: the loop
// `for (Key key : keys)` gives each key of the set in order.  The walk and
// its cursors do not copy and do not move, so no walk and no cursor outlives
// the call.  A cursor is a prvalue of begin() and end(), so the loop needs no
// copy of it.
template <class Member, class Word>
class [[nodiscard]] BitKeys final {
    View<Word const> words_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t N, class Key>
        requires IndexCount<N> && BitSetKey<Key, N>
    friend class BitSet;
    template <class Tag>
        requires std::is_class_v<Tag>
    friend class HeapBitSet;

    constexpr explicit BitKeys(View<Word const> words) noexcept : words_{words} {}

public:
    BitKeys(BitKeys const&) = delete("the walk borrows the words of its set: a copy could outlive the set");
    BitKeys& operator=(BitKeys const&) = delete("the walk borrows the words of its set: a copy could outlive the set");
    BitKeys(BitKeys&&) = delete("the walk borrows the words of its set: a move could carry it past the set");
    BitKeys& operator=(BitKeys&&) = delete("the walk borrows the words of its set: a move could carry it past the "
                                           "set");
    ~BitKeys() = default;

    [[nodiscard]] constexpr BitCursor<Member, Word> begin() const& noexcept { return BitCursor<Member, Word>{words_}; }
    [[nodiscard]] constexpr BitCursor<Member, Word> end() const& noexcept {
        return BitCursor<Member, Word>{detail::BitCursorEnd{}, words_};
    }
};

template <std::size_t N, class Key>
    requires IndexCount<N> && BitSetKey<Key, N>
class BitSet final {
    using Word = detail::BitWordOf<N>;
    using KeyKind = detail::BitKeyKind<Key>;

    // The enumerators of an enum key lie below N, so each enumerator is a
    // position of the set.  A value that no enumerator names gets the compare
    // of each verb.
    static_assert(detail::enumerators_below_<Key>(N), "each enumerator of the key of a BitSet lies below the count "
                                                      "of its positions");
    static constexpr std::size_t word_bits_ = detail::bits_in_<Word>;
    static constexpr std::size_t word_count_ = (N + word_bits_ - 1) / word_bits_;
    using WordIndex = Index<word_count_>;

    FixedArray<Word, word_count_> words_{};
    // The seal refuses std::bit_cast and a lifetime start over bytes, so no
    // byte image becomes a set with a bit past N, whose walk would make a key
    // past N.  mint_bit_set_from_bits is the road from a word.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <std::size_t M, class OtherKey, class OtherWord>
        requires CanMintBitSetFromBits<M, OtherKey, OtherWord>
    friend constexpr Option<BitSet<M, OtherKey>> mint_bit_set_from_bits(OtherWord bits) noexcept;

    // The position of a key, below N.  A key that no type proves below N is
    // compared with N, and a key at or past N ends the process, so no verb
    // reads a word past the words.
    [[nodiscard]] static constexpr std::size_t position_of_(Key key) noexcept {
        std::size_t const position = KeyKind::position_of_(key);
        if constexpr (!KeyKind::is_proven) {
            if (position >= N) [[unlikely]] {
                detail::report_bit_key_outside_();
            }
        }
        return position;
    }

    // The word of a position below N.  Its word is below the count of the
    // words.  Each caller reads the position from a proven key or from
    // position_of_, which compares it with N, so the optimizer removes the
    // check of the Index.
    [[nodiscard]] static constexpr WordIndex word_of_(std::size_t position) noexcept {
        return WordIndex::check(position / word_bits_).expect("a position of a set lies below the count of the set");
    }

    // The place of the bit of a position in its word.
    [[nodiscard]] static constexpr unsigned shift_in_word_(std::size_t position) noexcept {
        return static_cast<unsigned>(position % word_bits_);
    }

public:
    using index_type = Key;

    // The empty set.
    constexpr BitSet() noexcept = default;

    constexpr void set(Key key) & noexcept {
        std::size_t const position = position_of_(key);
        std::uint64_t const bit = std::uint64_t{1} << shift_in_word_(position);
        words_.with_element(word_of_(position),
                            [bit](Word& word) noexcept { word = detail::word_from_<Word>(std::uint64_t{word} | bit); });
    }

    constexpr void reset(Key key) & noexcept {
        std::size_t const position = position_of_(key);
        std::uint64_t const bit = std::uint64_t{1} << shift_in_word_(position);
        words_.with_element(word_of_(position), [bit](Word& word) noexcept {
            word = detail::word_from_<Word>(std::uint64_t{word} & ~bit);
        });
    }

    [[nodiscard]] constexpr bool test(Key key) const noexcept {
        std::size_t const position = position_of_(key);
        return detail::holds_bit_(std::uint64_t{words_.at(word_of_(position))}, shift_in_word_(position));
    }

    // The word of a set of at most 64 positions: the field of a packed record.
    // mint_bit_set_from_bits gives the set of a word back.
    [[nodiscard]] constexpr Word bits() const noexcept
        requires(N <= 64)
    {
        return words_.at(index<0>());
    }

    // The count of the keys of the set.  Complexity: O(N / W).
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t count = 0;
        for (Word const word : words_) {
            count += static_cast<std::size_t>(popcount(word));
        }
        return count;
    }

    // Complexity: O(N / W).
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        for (Word const word : words_) {
            if (word != 0) {
                return false;
            }
        }
        return true;
    }

    // Complexity: O(N / W).
    constexpr void clear() & noexcept { words_ = FixedArray<Word, word_count_>{}; }

    // The frame door of the keys (THE WALK): body gets the walk of the keys
    // of the set for the time of the call, as in
    // `set.with_keys([&](auto const& keys) noexcept { for (Key key : keys) { ... } })`,
    // and the door gives the result of body, which holds no borrow.  The walk
    // and its cursors do not copy and do not move, so each key that a cursor
    // makes is a key of the live set.  Complexity of a full walk:
    // O(N / W + size()).
    template <class Body>
        requires std::is_invocable_v<Body, BitKeys<Key, Word> const&>
              && HoldsNoBorrow<std::invoke_result_t<Body, BitKeys<Key, Word> const&>>
    constexpr std::invoke_result_t<Body, BitKeys<Key, Word> const&>
    with_keys(Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, BitKeys<Key, Word> const&>) {
        BitKeys<Key, Word> const keys{words_.view()};
        return static_cast<Body&&>(body)(keys);
    }
    template <class Body>
    void with_keys(Body&& body) const&& =
        delete("a walk of a temporary BitSet reads an object that the full expression ends");

    // A cursor that a caller held could outlive the set, and its keys would
    // read words that a different object reuses: walk the keys in the body of
    // with_keys.
    void begin() const = delete("a cursor that a caller holds could outlive its set: walk the keys in the body of "
                                "with_keys");
    void end() const = delete("a cursor that a caller holds could outlive its set: walk the keys in the body of "
                              "with_keys");

    // The four changes of THE OPERATIONS.  Each one keeps the zero bits past
    // N zero.  Complexity: O(N / W).
    constexpr void unite(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            std::uint64_t const theirs = other.words_.at(word);
            words_.with_element(
                word, [theirs](Word& mine) noexcept { mine = detail::word_from_<Word>(std::uint64_t{mine} | theirs); });
        }
    }

    constexpr void intersect(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            std::uint64_t const theirs = other.words_.at(word);
            words_.with_element(
                word, [theirs](Word& mine) noexcept { mine = detail::word_from_<Word>(std::uint64_t{mine} & theirs); });
        }
    }

    constexpr void subtract(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            std::uint64_t const theirs = other.words_.at(word);
            words_.with_element(word, [theirs](Word& mine) noexcept {
                mine = detail::word_from_<Word>(std::uint64_t{mine} & ~theirs);
            });
        }
    }

    constexpr void flip(BitSet const& other) & noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            std::uint64_t const theirs = other.words_.at(word);
            words_.with_element(
                word, [theirs](Word& mine) noexcept { mine = detail::word_from_<Word>(std::uint64_t{mine} ^ theirs); });
        }
    }

    // Complexity: O(N / W).
    [[nodiscard]] constexpr bool is_subset_of(BitSet const& other) const noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if ((std::uint64_t{words_.at(word)} & ~std::uint64_t{other.words_.at(word)}) != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool is_disjoint_from(BitSet const& other) const noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if ((std::uint64_t{words_.at(word)} & std::uint64_t{other.words_.at(word)}) != 0) {
                return false;
            }
        }
        return true;
    }

    // The compare reads each word.  It is written by hand, because GCC 16
    // gives a wrong defaulted == of a class with an array in a constant
    // evaluation.  Complexity: O(N / W).
    [[nodiscard]] friend constexpr bool operator==(BitSet const& left, BitSet const& right) noexcept {
        for (WordIndex const word : IndexRange<word_count_>{}) {
            if (left.words_.at(word) != right.words_.at(word)) {
                return false;
            }
        }
        return true;
    }
};

// The set of the bits of a word, or no value when the word holds a bit at or
// past N.  Complexity: O(1).
template <std::size_t N, class Key, class Word>
    requires CanMintBitSetFromBits<N, Key, Word>
[[nodiscard]] constexpr Option<BitSet<N, Key>> mint_bit_set_from_bits(Word bits) noexcept {
    std::uint64_t const past = std::uint64_t{bits} & ~low_mask<std::uint64_t>(static_cast<int>(N));
    if (past != 0) {
        return Option<BitSet<N, Key>>{none};
    }
    BitSet<N, Key> set{};
    set.words_.with_element(index<0>(), [bits](Word& word) noexcept { word = bits; });
    return Option<BitSet<N, Key>>::some(set);
}

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
    : public OwnerCore<detail::owner::HeapBitSetState> {
    using State = detail::owner::HeapBitSetState;
    using Core = OwnerCore<State>;

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
    [[nodiscard]] constexpr std::uint32_t bit_of_(BitId<Tag> id) const noexcept {
        std::uint32_t const bit = id.raw();
        if (bit >= state_.count) [[unlikely]] {
            detail::report_bit_id_outside_();
        }
        return bit;
    }

    // The word of a bit below the count.  The division is a shift of 32 bits,
    // as in the code of a raw bitmap of 32-bit positions.
    [[nodiscard]] constexpr std::uint64_t& word_of_(std::uint32_t bit) const noexcept {
        return state_.words[bit / 64u];
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
        std::uint32_t const bit = bit_of_(id);
        word_of_(bit) |= detail::bit_of_(bit);
    }

    constexpr void reset(BitId<Tag> id) & noexcept {
        std::uint32_t const bit = bit_of_(id);
        word_of_(bit) &= ~detail::bit_of_(bit);
    }

    [[nodiscard]] constexpr bool test(BitId<Tag> id) const noexcept {
        std::uint32_t const bit = bit_of_(id);
        return detail::holds_bit_(word_of_(bit), detail::shift_of_(bit));
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

    // The frame door of the ids (THE WALK): body gets the walk of the ids of
    // the set for the time of the call, as in
    // `set.with_keys([&](auto const& ids) noexcept { for (BitId<Tag> id : ids) { ... } })`,
    // and the door gives the result of body, which holds no borrow.  The walk
    // and its cursors do not copy and do not move, so no cursor outlives the
    // storage of the set.  An id is not a proven index: each door of the set
    // compares an id with the count of the set.  Complexity of a full walk:
    // O(extent() / 64 + size()).
    template <class Body>
        requires std::is_invocable_v<Body, BitKeys<BitId<Tag>, std::uint64_t> const&>
              && HoldsNoBorrow<std::invoke_result_t<Body, BitKeys<BitId<Tag>, std::uint64_t> const&>>
    constexpr std::invoke_result_t<Body, BitKeys<BitId<Tag>, std::uint64_t> const&> with_keys(
        Body&& body) const& noexcept(std::is_nothrow_invocable_v<Body, BitKeys<BitId<Tag>, std::uint64_t> const&>) {
        BitKeys<BitId<Tag>, std::uint64_t> const ids{words_()};
        return static_cast<Body&&>(body)(ids);
    }
    template <class Body>
    void with_keys(Body&& body) const&& =
        delete("a walk of a temporary HeapBitSet reads storage that the full expression frees");

    // A cursor that a caller held could outlive the storage of the set: walk
    // the ids in the body of with_keys.
    void begin() const = delete("a cursor that a caller holds could outlive the storage of its set: walk the ids in "
                                "the body of with_keys");
    void end() const = delete("a cursor that a caller holds could outlive the storage of its set: walk the ids in "
                              "the body of with_keys");

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
    ::foundation::alloc::Layout const layout = detail::slots_layout_<std::uint64_t>(detail::word_count_of_(ids));
    void* const storage = detail::heap_allocate_(layout);
    __builtin_memset(storage, 0, layout.bytes().raw());
    return HeapBitSet<Tag>{detail::owner::HeapBitSetState{static_cast<std::uint64_t*>(storage), ids, 0}};
}

}  // namespace foundation::core
