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

// A fixed set holds its words inline, copies its bits, and is trivially
// copyable.  A heap set holds a pointer and the count, moves and does not
// copy, and its state is an owner state of the core.
static_assert(sizeof(BitSet<1>) == sizeof(std::uint64_t) && sizeof(BitSet<64>) == sizeof(std::uint64_t));
static_assert(sizeof(BitSet<65>) == 2 * sizeof(std::uint64_t) && sizeof(BitSet<130>) == 3 * sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<BitSet<130>> && std::is_trivially_destructible_v<BitSet<130>>);
static_assert(sizeof(HeapBitSet<SlotTag>) == 2 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<HeapBitSet<SlotTag>>
              && std::is_nothrow_move_assignable_v<HeapBitSet<SlotTag>>);
static_assert(!std::is_copy_constructible_v<HeapBitSet<SlotTag>> && !std::is_copy_assignable_v<HeapBitSet<SlotTag>>);
static_assert(OwnerState<owner::HeapBitSetState>);

// A fixed set takes an Index of its own count or of a smaller one, and no
// number.  An index of a larger set does not convert.
template <class Set, class Key>
concept Tests = requires(Set const& set, Key key) { set.test(key); };
template <class Set, class Key>
concept Sets = requires(Set& set, Key key) { set.set(key); };
static_assert(Tests<BitSet<64>, Index<64>> && Tests<BitSet<64>, Index<8>> && Tests<BitSet<64>, StaticIndex<63>>);
static_assert(!Tests<BitSet<64>, Index<65>> && !Tests<BitSet<64>, std::size_t> && !Tests<BitSet<64>, int>);
static_assert(!Tests<BitSet<64>, StaticIndex<64>> && !Sets<BitSet<64>, Index<128>>
              && !Sets<BitSet<64> const, Index<64>>);

// A heap set takes the id of its own domain, and no id of a different domain
// and no number.
static_assert(Tests<HeapBitSet<SlotTag>, BitId<SlotTag>> && Sets<HeapBitSet<SlotTag>, BitId<SlotTag>>);
static_assert(!Tests<HeapBitSet<SlotTag>, BitId<OpTag>> && !Sets<HeapBitSet<SlotTag>, BitId<OpTag>>);
static_assert(!Tests<HeapBitSet<SlotTag>, std::uint32_t> && !Tests<HeapBitSet<SlotTag>, Id<SlotTag, std::uint64_t>>);

// The operations of two sets take a set of the same type.
template <class Set, class Other>
concept Unites = requires(Set& set, Other const& other) { set.unite(other); };
static_assert(Unites<BitSet<64>, BitSet<64>> && !Unites<BitSet<64>, BitSet<65>>);
static_assert(Unites<HeapBitSet<SlotTag>, HeapBitSet<SlotTag>> && !Unites<HeapBitSet<SlotTag>, HeapBitSet<OpTag>>);

// A walk of a temporary set is refused.
template <class Set>
concept WalksTemporary = requires { std::declval<Set>().begin(); };
static_assert(!WalksTemporary<BitSet<64>> && !WalksTemporary<HeapBitSet<SlotTag>>);

// The mint takes the allocation capability and a count of elements.
template <class Tag, class Marker>
concept Mints = requires(Marker marker) { mint_heap_bit_set<Tag>(marker, Quantity<unit::Element, std::uint32_t>{1}); };
struct OtherMarker final {};
static_assert(Mints<SlotTag, Alloc> && !Mints<SlotTag, OtherMarker>);

// The surface of each set is closed over the shared verbs and the names of
// the family.
inline constexpr TextView bit_set_names{
    "index_type set reset test is_empty clear unite intersect subtract flip is_subset_of is_disjoint_from extent"};
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitSet<1>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitSet<130>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, HeapBitSet<SlotTag>);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitCursor<Index<64>>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(bit_set_names, BitCursor<BitId<SlotTag>>, CursorProtocol);

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
    return is_held && is_counted && is_reset && set.is_empty() && set.size() == 0 && walk_sum(set) == 0
        && one.size() == 1 && walk_sum(one) == 1 && walk_sum(wide) == 3 + 129 + 130;
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
    return united == set_of<130>(1u, 64u, 100u, 129u) && common == set_of<130>(64u) && rest == set_of<130>(1u, 129u)
        && flipped == set_of<130>(1u, 100u, 129u) && common.is_subset_of(left) && common.is_subset_of(right)
        && !left.is_subset_of(right) && rest.is_disjoint_from(right) && !left.is_disjoint_from(right)
        && BitSet<130>{}.is_subset_of(left) && !(left == right);
}
static_assert(two_set_operations());

}  // namespace detail::bit_set_checks

}  // namespace foundation::core
