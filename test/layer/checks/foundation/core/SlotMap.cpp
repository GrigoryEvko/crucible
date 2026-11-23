// The compile-time checks of foundation/core/SlotMap.h.

#include <foundation/core/SlotMap.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::slot_map_checks {

struct HandleTag final {};
struct OtherTag final {};

// A linear value: it moves, it does not copy, and the seal makes its
// assignment user-provided, so it is not trivially copyable.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr explicit LinearToken(int from) noexcept : id{from} {}
    constexpr LinearToken(LinearToken&& other) noexcept : id{other.id} {}
    constexpr LinearToken& operator=(LinearToken&& other) noexcept {
        id = other.id;
        return *this;
    }
    LinearToken(LinearToken const&) = delete;
};

// A value whose move can throw: remove could lose it, so the map refuses it.
struct ThrowingMove final {
    int value = 0;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

// Two brands of maps of one tag, as two calls of the mint give.
struct MapBrand final {};
struct OtherMapBrand final {};
template <class T, class Generation = std::uint32_t>
using HandleMap = SlotMap<T, HandleTag, MapBrand, Generation>;
using HandleKey = SlotKey<HandleTag, MapBrand>;

// A key is one word, and an Option of a key has the size of the word.  A map
// holds three pointers and four counts, moves, and does not copy.  Its state
// is an owner state of the core.
static_assert(sizeof(HandleKey) == sizeof(std::uint64_t));
static_assert(sizeof(Option<HandleKey>) == sizeof(std::uint64_t));
static_assert(sizeof(HandleMap<int>) == 3 * sizeof(void*) + 4 * sizeof(std::uint32_t));
static_assert(std::is_nothrow_move_constructible_v<HandleMap<int>>
              && std::is_nothrow_move_assignable_v<HandleMap<int>>);
static_assert(!std::is_copy_constructible_v<HandleMap<int>> && !std::is_copy_assignable_v<HandleMap<int>>);
static_assert(OwnerState<owner::SlotMapState<int, std::uint32_t>>
              && OwnerState<owner::SlotMapState<LinearToken, std::uint8_t>>);

// A map of a linear value is linear and has no move assignment.  A value
// whose move can throw and a word of a generation of 64 bits are refused.
static_assert(::foundation::linear::is_linear_v<HandleMap<LinearToken>>);
static_assert(!::foundation::linear::is_linear_v<HandleMap<int>>);
static_assert(!std::is_move_assignable_v<HandleMap<LinearToken>>);
static_assert(SlotMapElement<LinearToken> && !SlotMapElement<ThrowingMove> && !SlotMapElement<int const>);
static_assert(SlotGeneration<std::uint8_t> && SlotGeneration<std::uint32_t> && !SlotGeneration<std::uint64_t>);
static_assert(!SlotGeneration<int> && !SlotGeneration<bool>);

// get and remove take the key of their own domain and brand, and no key of a
// different domain, no key of a different map of the same domain, no number
// and no id of a different word.
template <class Map, class Key>
concept Gets = requires(Map& map, Key key) { map.get(key); };
template <class Map, class Key>
concept Removes = requires(Map& map, Key key) { map.remove(key); };
static_assert(Gets<HandleMap<int>, HandleKey> && Removes<HandleMap<int>, HandleKey>);
static_assert(!Gets<HandleMap<int>, SlotKey<OtherTag, MapBrand>>
              && !Removes<HandleMap<int>, SlotKey<OtherTag, MapBrand>>);
static_assert(!Gets<HandleMap<int>, SlotKey<HandleTag, OtherMapBrand>>
              && !Removes<HandleMap<int>, SlotKey<HandleTag, OtherMapBrand>>);
static_assert(!Gets<HandleMap<int>, std::uint64_t> && !Gets<HandleMap<int>, Id<HandleTag, std::uint32_t>>);
static_assert(Gets<HandleMap<int> const, HandleKey> && !Removes<HandleMap<int> const, HandleKey>);

// A read, an insert and a walk of a temporary map are refused.
template <class Map>
concept GetsFromTemporary = requires(HandleKey key) { std::declval<Map>().get(key); };
template <class Map>
concept InsertsIntoTemporary = requires { std::declval<Map>().insert(1); };
template <class Map>
concept WalksTemporary = requires { std::declval<Map>().begin(); };
template <class Map>
concept WalksKeysOfTemporary = requires { std::declval<Map>().keys(); };
static_assert(!GetsFromTemporary<HandleMap<int>> && !GetsFromTemporary<HandleMap<int> const>);
static_assert(!InsertsIntoTemporary<HandleMap<int>> && !WalksTemporary<HandleMap<int>>);
static_assert(!WalksKeysOfTemporary<HandleMap<int>> && !WalksKeysOfTemporary<HandleMap<int> const>);

// The constraint of the mint: a value, a class that names a domain, a word of
// a generation and the allocation capability.
struct OtherMarker final {};
static_assert(CanMintSlotMap<int, HandleTag, std::uint32_t, Alloc>
              && !CanMintSlotMap<int, HandleTag, std::uint32_t, OtherMarker>);
static_assert(!CanMintSlotMap<int, HandleTag, std::uint64_t, Alloc>
              && !CanMintSlotMap<ThrowingMove, HandleTag, std::uint8_t, Alloc>);

// Each call site of the mint gives a brand of its own, so two maps of one tag
// have two types, and a key of one does not compile against the other.  The
// brands stay in the scope of the function.
consteval bool are_brands_of_two_mints_apart() {
    using FirstMap = decltype(mint_slot_map<int, HandleTag>(std::declval<Alloc>(),
                                                            std::declval<Quantity<unit::Element, std::uint32_t>>()));
    using SecondMap = decltype(mint_slot_map<int, HandleTag>(std::declval<Alloc>(),
                                                             std::declval<Quantity<unit::Element, std::uint32_t>>()));
    return !std::is_same_v<FirstMap, SecondMap> && Gets<FirstMap, typename FirstMap::key_type>
        && !Gets<SecondMap, typename FirstMap::key_type>;
}
static_assert(are_brands_of_two_mints_apart());

// The surface of the map and its walk is closed over the shared verbs and
// the names of the family.
inline constexpr TextView slot_map_names{"element_type key_type insert remove is_empty expect_empty capacity keys"};
CRUCIBLE_CLOSED_SURFACE(slot_map_names, HandleMap<int>);
CRUCIBLE_CLOSED_SURFACE(slot_map_names, HandleMap<LinearToken, std::uint8_t>);
CRUCIBLE_CLOSED_SURFACE(slot_map_names, SlotWalk<int const, std::uint32_t, HandleKey>);
CRUCIBLE_CLOSED_SURFACE(slot_map_names, SlotCursor<int, std::uint32_t, int&>, CursorProtocol);
CRUCIBLE_CLOSED_SURFACE(slot_map_names, SlotCursor<int const, std::uint32_t, HandleKey>, CursorProtocol);

// A value of a line of the cache, whose map takes its storage from the
// allocator of an alignment past 16.
struct alignas(64) Line final {
    std::uint64_t first = 0;
};

// The parts of a key, and the layout of the three runs of a map.  The
// generations of a map of bytes start after the values and the stack, and the
// bytes round up to the alignment of the allocation.
static_assert(slot_index_of_(0x0000000500000003ull) == 3 && slot_generation_of_(0x0000000500000003ull) == 5);
static_assert(holds_value_(std::uint8_t{255}) && !holds_value_(std::uint8_t{0}) && !holds_value_(std::uint32_t{2}));
static_assert(slot_map_layout_<std::uint8_t, std::uint8_t>(3).stack_offset == 4);
static_assert(slot_map_layout_<std::uint8_t, std::uint8_t>(3).generation_offset == 4 + 12);
static_assert(slot_map_layout_<std::uint8_t, std::uint8_t>(3).bytes == 20);
static_assert(slot_map_layout_<std::uint64_t, std::uint32_t>(2).bytes == 16 + 8 + 8);
static_assert(slot_map_layout_<Line, std::uint32_t>(3).generation_offset == 192 + 12);
static_assert(slot_map_layout_<Line, std::uint32_t>(3).bytes == 256);
static_assert(slot_map_alignment_<std::uint8_t> == 4 && slot_map_alignment_<std::uint64_t> == 8);
static_assert(slot_map_alignment_<Line> == 64);

}  // namespace detail::slot_map_checks

}  // namespace foundation::core
