// The compile-time checks of foundation/core/BitSet.h.

#include <foundation/core/BitSet.h>

#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::bit_set_checks {

struct SlotTag final {};
struct OpTag final {};

// The scalar slots of an op and the argument positions of a schema: two
// domains of keys.
enum class ScalarSlot : std::uint8_t {
    first,
    second,
    third,
    fourth,
    fifth
};
enum class ArgumentPosition : std::uint8_t {
    receiver,
    other,
    alpha
};
// An enum of a negative enumerator.
enum class Signed : std::int8_t {
    below = -1,
    zero = 0
};

// A fixed set holds its bits in the smallest word that holds N bits, and in
// words of 64 bits past 64 positions.  It is trivially copyable.  A heap set
// holds a pointer and the count, moves and does not copy, and its state is an
// owner state of the core.
static_assert(sizeof(BitSet<1>) == 1 && sizeof(BitSet<5>) == 1 && sizeof(BitSet<8>) == 1);
static_assert(sizeof(BitSet<15>) == 2 && sizeof(BitSet<16>) == 2 && sizeof(BitSet<17>) == 4);
static_assert(sizeof(BitSet<33>) == 8 && sizeof(BitSet<64>) == 8 && sizeof(BitSet<65>) == 16);
static_assert(sizeof(BitSet<130>) == 3 * sizeof(std::uint64_t) && sizeof(BitSet<5, ScalarSlot>) == 1);
static_assert(std::is_trivially_copyable_v<BitSet<130>> && std::is_trivially_destructible_v<BitSet<130>>);
static_assert(std::is_trivially_copyable_v<BitSet<5, ScalarSlot>>);
static_assert(sizeof(HeapBitSet<SlotTag>) == 2 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<HeapBitSet<SlotTag>>
              && std::is_nothrow_move_assignable_v<HeapBitSet<SlotTag>>);
static_assert(!std::is_copy_constructible_v<HeapBitSet<SlotTag>> && !std::is_copy_assignable_v<HeapBitSet<SlotTag>>);
static_assert(OwnerState<owner::HeapBitSetState>);

// The keys: Index<N> of the same N, a scoped enum whose enumerators lie
// below N, and an Id whose word holds N.  An unscoped enum, a number, an
// Index of a different N and an enum of an enumerator past N or below zero
// are no keys.
static_assert(BitSetKey<Index<64>, 64> && !BitSetKey<Index<65>, 64> && !BitSetKey<Index<63>, 64>);
static_assert(BitSetKey<ScalarSlot, 5> && BitSetKey<ScalarSlot, 8> && !BitSetKey<ScalarSlot, 4>);
static_assert(!BitSetKey<Signed, 4> && !BitSetKey<std::size_t, 64> && !BitSetKey<int, 8>);
static_assert(BitSetKey<Id<OpTag, std::uint8_t>, 255> && !BitSetKey<Id<OpTag, std::uint8_t>, 256>);
static_assert(BitSetKey<Id<OpTag, std::uint64_t>, 1000>);

// A fixed set takes its key, an Index of a smaller count, and no number.  An
// index of a larger set and a key of a different domain do not convert.
template <class Set, class Key>
concept Tests = requires(Set const& set, Key key) { set.test(key); };
template <class Set, class Key>
concept Sets = requires(Set& set, Key key) { set.set(key); };
static_assert(Tests<BitSet<64>, Index<64>> && Tests<BitSet<64>, Index<8>> && Tests<BitSet<64>, StaticIndex<63>>);
static_assert(!Tests<BitSet<64>, Index<65>> && !Tests<BitSet<64>, std::size_t> && !Tests<BitSet<64>, int>);
static_assert(!Tests<BitSet<64>, StaticIndex<64>> && !Sets<BitSet<64>, Index<128>>
              && !Sets<BitSet<64> const, Index<64>>);
static_assert(Tests<BitSet<5, ScalarSlot>, ScalarSlot> && !Tests<BitSet<5, ScalarSlot>, ArgumentPosition>);
static_assert(!Tests<BitSet<5, ScalarSlot>, Index<5>> && !Tests<BitSet<5>, ScalarSlot>);
static_assert(!Sets<BitSet<5, ScalarSlot>, std::uint8_t>
              && Sets<BitSet<16, Id<OpTag, std::uint8_t>>, Id<OpTag, std::uint8_t>>);

// A heap set takes the id of its own domain, and no id of a different domain
// and no number.
static_assert(Tests<HeapBitSet<SlotTag>, BitId<SlotTag>> && Sets<HeapBitSet<SlotTag>, BitId<SlotTag>>);
static_assert(!Tests<HeapBitSet<SlotTag>, BitId<OpTag>> && !Sets<HeapBitSet<SlotTag>, BitId<OpTag>>);
static_assert(!Tests<HeapBitSet<SlotTag>, std::uint32_t> && !Tests<HeapBitSet<SlotTag>, Id<SlotTag, std::uint64_t>>);

// The operations of two sets take a set of the same type.
template <class Set, class Other>
concept Unites = requires(Set& set, Other const& other) { set.unite(other); };
static_assert(Unites<BitSet<64>, BitSet<64>> && !Unites<BitSet<64>, BitSet<65>>);
static_assert(!Unites<BitSet<5, ScalarSlot>, BitSet<5>> && Unites<BitSet<5, ScalarSlot>, BitSet<5, ScalarSlot>>);
static_assert(Unites<HeapBitSet<SlotTag>, HeapBitSet<SlotTag>> && !Unites<HeapBitSet<SlotTag>, HeapBitSet<OpTag>>);

// A walk of a temporary set is refused.
template <class Set>
concept WalksTemporary = requires { std::declval<Set>().begin(); };
static_assert(!WalksTemporary<BitSet<64>> && !WalksTemporary<HeapBitSet<SlotTag>>);

// The word of a set of at most 64 positions is its storage word, and a set
// past 64 positions has no word.
template <class Set>
concept HasBits = requires(Set const& set) { set.bits(); };
static_assert(std::is_same_v<decltype(std::declval<BitSet<5> const&>().bits()), std::uint8_t>);
static_assert(std::is_same_v<decltype(std::declval<BitSet<16> const&>().bits()), std::uint16_t>);
static_assert(std::is_same_v<decltype(std::declval<BitSet<64> const&>().bits()), std::uint64_t>);
static_assert(HasBits<BitSet<64>> && !HasBits<BitSet<65>>);

// The mints take the word type of the set, the allocation capability and a
// count of elements.
template <std::size_t N, class Word>
concept MintsFromBits = requires(Word word) { mint_bit_set_from_bits<N>(word); };
static_assert(MintsFromBits<5, std::uint8_t> && MintsFromBits<15, std::uint16_t> && MintsFromBits<64, std::uint64_t>);
static_assert(!MintsFromBits<5, std::uint16_t> && !MintsFromBits<5, int> && !MintsFromBits<65, std::uint64_t>);
template <class Tag, class Marker>
concept Mints = requires(Marker marker) { mint_heap_bit_set<Tag>(marker, Quantity<unit::Element, std::uint32_t>{1}); };
struct OtherMarker final {};
static_assert(Mints<SlotTag, Alloc> && !Mints<SlotTag, OtherMarker>);

// The surface of each set is closed over the shared verbs and the names of
// the family.
inline constexpr TextView bit_set_names{"index_type set reset test bits is_empty clear unite intersect subtract flip "
                                        "is_subset_of is_disjoint_from extent"};
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitSet<1>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitSet<130>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitSet<5, ScalarSlot>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, HeapBitSet<SlotTag>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitCursor<Index<64>, std::uint64_t>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitCursor<ScalarSlot, std::uint8_t>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitCursor<BitId<SlotTag>, std::uint64_t>, CursorProtocol);

// The set of the indexes at the positions of a list, with the extent N.
template <std::size_t N, class... Positions>
[[nodiscard]] consteval BitSet<N> set_of(Positions... positions) noexcept {
    BitSet<N> set{};
    (set.set(Index<N>::check(positions).expect("each position is below N")), ...);
    return set;
}

// The sum of the indexes of a walk, each plus one, so an index 0 counts.
template <std::size_t N>
[[nodiscard]] consteval std::size_t walk_sum(BitSet<N> const& set) noexcept {
    std::size_t sum = 0;
    for (Index<N> const index : set) {
        sum += index.raw() + 1;
    }
    return sum;
}

// set, reset, test, size, the walk and clear in a constant evaluation, at
// counts that end inside a word, at a word, and past a word.
[[nodiscard]] consteval bool one_set_verbs() noexcept {
    BitSet<65> set = set_of<65>(0u, 5u, 63u, 64u);
    bool const is_held =
        set.test(index<0>()) && set.test(index<63>()) && set.test(index<64>()) && !set.test(index<1>());
    bool const is_counted = set.size() == 4 && !set.is_empty() && walk_sum(set) == 1 + 6 + 64 + 65;
    set.reset(index<63>());
    set.reset(index<62>());
    bool const is_reset = set.size() == 3 && !set.test(index<63>()) && walk_sum(set) == 1 + 6 + 65;
    set.clear();
    BitSet<1> one{};
    one.set(index<0>());
    BitSet<130> wide = set_of<130>(129u, 128u, 2u);
    BitSet<15> narrow = set_of<15>(14u, 0u, 7u, 8u);
    return is_held && is_counted && is_reset && set.is_empty() && set.size() == 0 && walk_sum(set) == 0
        && one.size() == 1 && walk_sum(one) == 1 && walk_sum(wide) == 3 + 129 + 130 && narrow.size() == 4
        && walk_sum(narrow) == 15 + 1 + 8 + 9 && narrow.bits() == 0x4181u;
}
static_assert(one_set_verbs());

// The four changes and the three queries of two sets.
[[nodiscard]] consteval bool two_set_operations() noexcept {
    BitSet<130> const left = set_of<130>(1u, 64u, 129u);
    BitSet<130> const right = set_of<130>(64u, 100u);
    BitSet<130> united = left;
    united.unite(right);
    BitSet<130> common = left;
    common.intersect(right);
    BitSet<130> rest = left;
    rest.subtract(right);
    BitSet<130> flipped = left;
    flipped.flip(right);
    BitSet<5> small = set_of<5>(0u, 4u);
    small.flip(set_of<5>(4u, 3u));
    return united == set_of<130>(1u, 64u, 100u, 129u) && common == set_of<130>(64u) && rest == set_of<130>(1u, 129u)
        && flipped == set_of<130>(1u, 100u, 129u) && common.is_subset_of(left) && common.is_subset_of(right)
        && !left.is_subset_of(right) && rest.is_disjoint_from(right) && !left.is_disjoint_from(right)
        && BitSet<130>{}.is_subset_of(left) && !(left == right) && small.bits() == 0x09u;
}
static_assert(two_set_operations());

// A set of a domain key: its walk gives the keys, and the word of the set
// goes out and comes back.  A word with a bit past the positions is refused.
[[nodiscard]] consteval bool domain_keys_and_words() noexcept {
    BitSet<5, ScalarSlot> slots{};
    slots.set(ScalarSlot::fifth);
    slots.set(ScalarSlot::second);
    std::size_t walked = 0;
    bool is_in_order = true;
    std::uint8_t last = 0;
    for (ScalarSlot const slot : slots) {
        auto const raw = static_cast<std::uint8_t>(slot);
        is_in_order = is_in_order && (walked == 0 || raw > last);
        last = raw;
        ++walked;
    }
    std::uint8_t const word = slots.bits();
    BitSet<5, ScalarSlot> const back =
        mint_bit_set_from_bits<5, ScalarSlot>(word).expect("a word of the set holds no bit past it");
    bool const is_refused = mint_bit_set_from_bits<5, ScalarSlot>(std::uint8_t{0x20}).is_none()
                         && mint_bit_set_from_bits<5>(std::uint8_t{0x80}).is_none();
    bool const is_full_word =
        mint_bit_set_from_bits<8>(std::uint8_t{0xFF}).expect("each bit is a position").size() == 8;
    BitSet<16, Id<OpTag, std::uint8_t>> ops{};
    ops.set(Id<OpTag, std::uint8_t>{15});
    return walked == 2 && is_in_order && last == 4 && word == 0x12u && back == slots && back.test(ScalarSlot::second)
        && !back.test(ScalarSlot::first) && is_refused && is_full_word && ops.bits() == 0x8000u;
}
static_assert(domain_keys_and_words());

}  // namespace detail::bit_set_checks

}  // namespace foundation::core
