#pragma once

// The slot map of the Region family: a table of values, each behind a key
// that holds the index of its slot and a generation.
//
//   SlotMap<T, Tag, Brand, Generation>
//                       at most a capacity of values of T in slots on the
//                       heap.  The tag names the domain of the keys, such as
//                       the shadow handles of one Vigil, and the brand names
//                       the mint of the map.  So a key of a different domain,
//                       or of a different map of the same domain, does not
//                       compile
//   SlotKey<Tag, Brand> the key of a value: an Id of the domain and the brand,
//                       with the index of the slot in the low 32 bits and the
//                       generation of the slot in the high 32 bits.  The key is
//                       one word, so a foreign caller, such as Python, holds
//                       it as one integer
//
// Mint
//   mint_slot_map<T, Tag>(alloc, capacity)
//                       the empty map of capacity slots, in one allocation, with
//                       the fresh brand of the call.  A capacity of zero
//                       allocates nothing.  The third template argument is the
//                       word of a generation: std::uint8_t, std::uint16_t or
//                       std::uint32_t, and std::uint32_t when the call names
//                       none.  A pack refuses each further template argument,
//                       so no caller names the brand
//   mint_member_slot_map<T, Tag, MemberTag>(alloc, capacity)
//                       the same map with the member tag MemberTag as its
//                       brand, for a class that holds the map as a member.
//                       The fourth template argument is the word of a
//                       generation
//
// Brands
//   Each call site of mint_slot_map gives its own brand, so the maps of two
//   call sites with one tag take no key of each other.  The maps that one
//   call site gives, such as the maps of a loop or of each call of one
//   helper, share the brand, and a key of one of them names a slot of the
//   others (the residual of a brand of a call site).
//
//   A class that holds a map as a member names a member tag (ArenaMemberTag
//   of foundation/core/ArenaTag.h): a class that it nests beside the member,
//   with the mark arena_member_brand.  The map carries the mark
//   branded_arena, so the rule brand-escape of the quarantine plugin refuses
//   a tag that does not name exactly one member, and a local or a static
//   object of a map of the tag.  So no second map shares the brand, and a key
//   of the member names no slot of a different map.  Two objects of the class
//   still share the brand, which no type tells apart (the residual of a
//   member brand).  A holder names the key type SlotKey<Tag, MemberTag>.
//
// Operations
//   insert(value)       puts value in a free slot, and gives its key, or
//                       Full<T> when no slot is free.  No allocation: the mint
//                       took the storage of each slot
//   get(key)            the value of key as an Option of a Ref with the fresh
//                       brand of the call, or no value for a stale key, a key
//                       of a slot that holds no value, or a key past the slots
//   remove(key)         moves the value of key out as an Option<T> and frees
//                       its slot, or gives no value as get does
//   size(), is_empty()  the count of the values
//   std::move(map).expect_empty(why)
//                       ends an empty map, and ends the process with why when
//                       the map still holds a value.  A map of a linear value
//                       ends here
//   capacity()          the count of the slots of the mint
//   for (x : map)       each value, in the order of the indexes of the slots
//   keys()              each key of a value, in the same order
//
// Keys
//   A key is a number, and a caller can build one from a word, so get and
//   remove read each key as a claim.  A key that comes back from outside the
//   program, such as from Python, is outside data: a door of that boundary
//   checks it once (the range of the index, the generation and the owner)
//   before it becomes a SlotKey.  That door waits for its first caller.  The index of a key must be below the
//   count of the slots that a value took at least one time, and the
//   generation of the key must be the generation of its slot.  A slot holds a
//   value when its generation is odd.  An insert adds one to the generation,
//   and so does a remove, so a key of a removed value never matches its slot
//   again.  Each key that insert gives has an odd generation and an index
//   below 0xFFFFFFFF, so no key is the empty value of an Option of a key.
//
// Free slots
//   The free slots are a stack of indexes, so an insert searches nothing: it
//   takes the last index that a remove freed, or else the first slot that no
//   value took yet.  The order of the indexes, the keys and the walk depend
//   only on the order of the calls, never on an address or a hash.
//
// Wrap of a generation
//   A remove at the largest generation of the word would wrap the generation
//   to zero, and the next insert would give the generation 1 again: a key of
//   a value that the map removed long ago would then match a new value.  So
//   the remove at the largest generation retires the slot for ever: the slot
//   holds the generation zero, and its index never goes onto the stack of the
//   free slots.  A slot of a 32-bit generation takes 2^31 values before it
//   retires, and a retired slot costs its bytes and nothing more.  A wider
//   generation would make a key wider than one word.  The test of the wrap
//   uses std::uint8_t, so a slot retires after 128 values.
//
// A map owns its slots alone, on the owner core of foundation/core/Owner.h.
// A move gives the slots to the target and leaves the source empty, with no
// slot.  The end of a map ends each value in the order of the slots.  A copy
// would allocate with no allocation capability at its call, so a map does not
// copy.  A map of a linear value is linear itself, and it has no move
// assignment, because the assignment drops the values of the target.  A Ref
// of get names its value until a remove of its key or the end of the map,
// as a Ref of a FixedList does.  The slots never move, so no insert ends it.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/ArenaTag.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Id.h>
#include <foundation/core/List.h>
#include <foundation/core/Number.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace foundation::core {

// The domain of the keys of the maps of one tag and one brand.
template <class Tag, ::foundation::brand::IsBrand Brand>
struct SlotDomain final {};

// The key of a value of a SlotMap of the domain Tag and the brand Brand.
template <class Tag, ::foundation::brand::IsBrand Brand>
using SlotKey = Id<SlotDomain<Tag, Brand>, std::uint64_t>;

// The word of the generation of a slot.
template <class Generation>
concept SlotGeneration = std::same_as<Generation, std::uint8_t> || std::same_as<Generation, std::uint16_t>
                      || std::same_as<Generation, std::uint32_t>;

// The value of a SlotMap: an owned element that moves with no throw, because
// remove moves the value out.
template <class T>
concept SlotMapElement = OwnedElement<T> && std::is_nothrow_move_constructible_v<T>;

template <class T, class Tag, ::foundation::brand::IsBrand Brand, class Generation>
    requires SlotMapElement<T> && std::is_class_v<Tag> && SlotGeneration<Generation>
class SlotMap;

// The door of a SlotMap to the door of a Ref (the doors of the owners above
// foundation/core/Ref.h).  Its one member is private, and SlotMap is its one
// friend.  The invariant: get() lends the value of a slot only after the
// generation of the slot matches the generation of the key, so the slot holds
// the live value of the key, and the brand is the fresh brand of the call of
// get().  A home owner row of utils/rule-table.txt keeps this definition in
// this header.
class SlotMapRefDoor final : ::foundation::NoObject<SlotMapRefDoor> {
    template <class T, class Tag, ::foundation::brand::IsBrand Brand, class Generation>
        requires SlotMapElement<T> && std::is_class_v<Tag> && SlotGeneration<Generation>
    friend class SlotMap;

    // The live value of a slot, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

namespace detail {

// The bits of the index in a key.
inline constexpr unsigned slot_index_bits_ = 32;

// The parts of a key: the index of its slot and its generation.
[[nodiscard]] constexpr std::uint32_t slot_index_of_(std::uint64_t key) noexcept {
    return static_cast<std::uint32_t>(key);
}

[[nodiscard]] constexpr std::uint64_t slot_generation_of_(std::uint64_t key) noexcept {
    return key >> slot_index_bits_;
}

// True when a slot of this generation holds a value: the generation is odd.
template <class Generation>
[[nodiscard]] constexpr bool holds_value_(Generation generation) noexcept {
    return (std::uint32_t{generation} & 1u) != 0;
}

// The places of the three runs of the one allocation of a map: the values
// from the start, then the stack of the free indexes, then the generations.
struct SlotMapLayout {
    std::size_t stack_offset = 0;
    std::size_t generation_offset = 0;
    std::size_t bytes = 0;
};

// The alignment of the allocation of a map of T.
template <class T>
inline constexpr std::size_t slot_map_alignment_ =
    alignof(T) < alignof(std::uint32_t) ? alignof(std::uint32_t) : alignof(T);

// The layout of capacity slots.  A count of bytes that does not fit in
// size_t ends the process before the allocation, and the strict forms report
// the operands.  Complexity: O(1).
template <class T, class Generation>
[[nodiscard]] constexpr SlotMapLayout slot_map_layout_(std::uint32_t capacity) noexcept {
    std::size_t const slots = capacity;
    std::size_t const stack_offset = strict_align_up(strict_mul(slots, sizeof(T)), alignof(std::uint32_t));
    std::size_t const generation_offset = strict_add(stack_offset, strict_mul(slots, sizeof(std::uint32_t)));
    std::size_t const used_bytes = strict_add(generation_offset, strict_mul(slots, sizeof(Generation)));
    // The allocator of an alignment past 16 takes a count of bytes that is a
    // multiple of the alignment, so the bytes round up to it, as the bytes of
    // a HeapArray do.
    std::size_t const bytes = strict_align_up(used_bytes, slot_map_alignment_<T>);
    return SlotMapLayout{stack_offset, generation_offset, bytes};
}

// The layout of the one allocation of capacity slots, for the allocation
// door.  capacity is above zero.  Complexity: O(1).
template <class T, class Generation>
[[nodiscard]] constexpr ::foundation::alloc::Layout slot_map_storage_layout_(std::uint32_t capacity) noexcept {
    return ::foundation::alloc::mint_layout(ByteCount{slot_map_layout_<T, Generation>(capacity).bytes},
                                            ::foundation::alloc::Alignment{slot_map_alignment_<T>})
        .expect("the storage of at least one slot of a SlotMap is a layout");
}

namespace owner {

// The fields of a SlotMap.  The values, the stack of the free indexes and the
// generations are three runs of one allocation, and they are null when the
// capacity is zero.  The slots below used took a value at least one time, so
// each one has a generation.  No code reads a slot at or past used.  The
// stack holds free_count indexes.  live is the count of the values.
template <class T, class Generation>
struct SlotMapState {
    using owner_state_mark = OwnerStateMark;
    static constexpr bool runs_element_code = !std::is_trivially_destructible_v<T>;

    T* values = nullptr;
    std::uint32_t* free_stack = nullptr;
    Generation* generations = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
    std::uint32_t free_count = 0;
    std::uint32_t live = 0;

    [[nodiscard]] static constexpr SlotMapState emptied(SlotMapState const& /*state*/) noexcept {
        return SlotMapState{};
    }

    // The door that ends the value in one slot: remove ends the value that it
    // moved out, and the end of the map ends each value that it holds.
    [[= ::foundation::linear::drop_door{}]] static constexpr void end_slot_(T& value) noexcept { value.~T(); }

    // Ends each value in the order of the slots, and gives the allocation
    // back, with each slot unpoisoned (the spare storage of Heap.h).
    // Complexity: O(used).
    static constexpr void release(SlotMapState const& state) noexcept {
        if (state.values == nullptr) {
            return;
        }
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::uint32_t index = 0; index != state.used; ++index) {
                if (holds_value_(state.generations[index])) {
                    end_slot_(state.values[index]);
                }
            }
        }
        unpoison_slots_(state.values, state.capacity);
        heap_release_(static_cast<void*>(state.values), slot_map_storage_layout_<T, Generation>(state.capacity));
    }
};

}  // namespace owner

// The fatal exit of a cursor of a walk of a map at its end.
[[noreturn]] CRUCIBLE_COLD inline void report_slot_cursor_at_end_() noexcept {
    fatal("a read or a step through the end cursor of a walk of a SlotMap");
}

}  // namespace detail

// The position of a walk over the slots of a map that hold a value.  It
// holds the index of the next such slot, or the count of the used slots at
// the end.  Yield is a reference to the value, or the key of the value.  A
// read or a step at the end ends the process in each build.
template <class Element, class Generation, class Yield>
class SlotCursor final {
    Element* values_ = nullptr;
    Generation const* generations_ = nullptr;
    std::uint32_t at_ = 0;
    std::uint32_t used_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class OtherElement, class OtherGeneration, class OtherYield>
    friend class SlotWalk;

    // The cursor at the first slot of the state that holds a value.
    template <class State>
    constexpr explicit SlotCursor(State const& state) noexcept
        : values_{state.values}, generations_{state.generations}, used_{state.used} {
        skip_free_slots_();
    }

    // Moves to the next slot that holds a value, or to the end.  Complexity:
    // linear in the free slots that it passes.
    constexpr void skip_free_slots_() noexcept {
        while (at_ != used_ && !detail::holds_value_(generations_[at_])) {
            ++at_;
        }
    }

public:
    [[nodiscard]] constexpr Yield operator*() const noexcept {
        if (at_ == used_) [[unlikely]] {
            detail::report_slot_cursor_at_end_();
        }
        if constexpr (std::is_reference_v<Yield>) {
            return values_[at_];
        } else {
            return Yield{std::uint64_t{at_} | (std::uint64_t{generations_[at_]} << detail::slot_index_bits_)};
        }
    }

    constexpr SlotCursor& operator++() noexcept {
        if (at_ == used_) [[unlikely]] {
            detail::report_slot_cursor_at_end_();
        }
        ++at_;
        skip_free_slots_();
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(SlotCursor const& left, SlotCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// A walk over the slots of a map that hold a value, in the order of their
// indexes.  It borrows the slots of the map, and only the map builds it.
template <class Element, class Generation, class Yield>
class SlotWalk final {
    using Cursor = SlotCursor<Element, Generation, Yield>;
    using State = detail::owner::SlotMapState<std::remove_const_t<Element>, Generation>;

    State const* state_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, class Tag, ::foundation::brand::IsBrand Brand, class OtherGeneration>
        requires SlotMapElement<T> && std::is_class_v<Tag> && SlotGeneration<OtherGeneration>
    friend class SlotMap;

    constexpr explicit SlotWalk(State const& state) noexcept : state_{__builtin_addressof(state)} {}

public:
    [[nodiscard]] constexpr Cursor begin() const noexcept { return Cursor{*state_}; }
    [[nodiscard]] constexpr Cursor end() const noexcept {
        Cursor cursor{State{}};
        cursor.values_ = state_->values;
        cursor.generations_ = state_->generations;
        cursor.at_ = state_->used;
        cursor.used_ = state_->used;
        return cursor;
    }
};

// The arguments of mint_slot_map: a value of a map, a class that names the
// domain of the keys, a word of a generation, and the allocation capability.
template <class T, class Tag, class Generation, class Marker>
concept CanMintSlotMap =
    SlotMapElement<T> && std::is_class_v<Tag> && SlotGeneration<Generation> && AllocationCapability<Marker>;

template <class T, class Tag, class Generation = std::uint32_t, class... Refused,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND, class Marker>
    requires(sizeof...(Refused) == 0) && CanMintSlotMap<T, Tag, Generation, Marker>
[[nodiscard]] SlotMap<T, Tag, ::foundation::brand::undeducible_brand_t<Fresh>, Generation>
mint_slot_map(Marker allocation, Quantity<unit::Element, std::uint32_t> capacity) noexcept;

// The arguments of mint_member_slot_map: the arguments of mint_slot_map, and
// a member tag, which is the brand of the map (the brands).
template <class T, class Tag, class MemberTag, class Generation, class Marker>
concept CanMintMemberSlotMap = CanMintSlotMap<T, Tag, Generation, Marker> && ArenaMemberTag<MemberTag>;

template <class T, class Tag, class MemberTag, class Generation = std::uint32_t, class... Refused, class Marker>
    requires(sizeof...(Refused) == 0) && CanMintMemberSlotMap<T, Tag, MemberTag, Generation, Marker>
[[nodiscard]] SlotMap<T, Tag, MemberTag, Generation>
mint_member_slot_map(Marker allocation, Quantity<unit::Element, std::uint32_t> capacity) noexcept;

template <class T, class Tag, ::foundation::brand::IsBrand Brand, class Generation = std::uint32_t>
    requires SlotMapElement<T> && std::is_class_v<Tag> && SlotGeneration<Generation>
class [[nodiscard]][[= ::foundation::linear::linear_when<::foundation::linear::is_linear_type(^^T)>]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]][[= ::foundation::brand::branded_arena{}]]
    SlotMap final : public OwnerCore<detail::owner::SlotMapState<T, Generation>, SlotMap<T, Tag, Brand, Generation>> {
    using State = detail::owner::SlotMapState<T, Generation>;
    using Core = OwnerCore<State, SlotMap>;

    // The slots of a mint.  Only the mints call it.
    constexpr explicit SlotMap(State const& state) noexcept : Core{state} {}

    // The empty map of slots slots: one allocation of the values, the stack
    // of the free indexes and the generations.  No slot holds a value, and no
    // byte of a slot is read before an insert writes it, so each slot of the
    // values is poisoned (the spare storage of Heap.h).  A count of zero
    // allocates nothing.  The two mints call it.  It inlines into each mint:
    // when GCC 16 at -O3 keeps it apart, the empty map of a count of zero
    // reaches an inlined insert, and -Wstringop-overflow refuses a write
    // through its null generations that no path takes.  Complexity: one
    // allocation.
    [[nodiscard]] CRUCIBLE_INLINE static SlotMap build_slots_(std::uint32_t slots) noexcept {
        if (slots == 0) {
            return SlotMap{};
        }
        detail::SlotMapLayout const layout = detail::slot_map_layout_<T, Generation>(slots);
        unsigned char* const storage =
            static_cast<unsigned char*>(detail::heap_allocate_(detail::slot_map_storage_layout_<T, Generation>(slots)));
        State const state{
            .values = static_cast<T*>(static_cast<void*>(storage)),
            .free_stack = static_cast<std::uint32_t*>(static_cast<void*>(storage + layout.stack_offset)),
            .generations = static_cast<Generation*>(static_cast<void*>(storage + layout.generation_offset)),
            .capacity = slots,
        };
        detail::poison_slots_(state.values, slots);
        return SlotMap{state};
    }

    template <class OtherT, class OtherTag, class OtherGeneration, class... Refused, ::foundation::brand::IsBrand Fresh,
              class Marker>
        requires(sizeof...(Refused) == 0) && CanMintSlotMap<OtherT, OtherTag, OtherGeneration, Marker>
    friend SlotMap<OtherT, OtherTag, ::foundation::brand::undeducible_brand_t<Fresh>, OtherGeneration>
    mint_slot_map(Marker allocation, Quantity<unit::Element, std::uint32_t> capacity) noexcept;

    template <class OtherT, class OtherTag, class MemberTag, class OtherGeneration, class... Refused, class Marker>
        requires(sizeof...(Refused) == 0) && CanMintMemberSlotMap<OtherT, OtherTag, MemberTag, OtherGeneration, Marker>
    friend SlotMap<OtherT, OtherTag, MemberTag, OtherGeneration>
    mint_member_slot_map(Marker allocation, Quantity<unit::Element, std::uint32_t> capacity) noexcept;

    // True when the slot of key holds the value of key: the index is below
    // the used slots, the generation of the key is odd, and it is the
    // generation of the slot.  Two compares and one test of a bit.  The test
    // of the bit reads the key, and it comes before the load of the
    // generation.  So each test is its own branch, and -fharden-compares adds
    // no second compare.  A test of the bit of the loaded generation after
    // the compare makes GCC join the two tests into one value, and the flag
    // then computes the compare two times.
    [[nodiscard]] constexpr bool holds_(std::uint64_t key) const noexcept {
        std::uint32_t const index = detail::slot_index_of_(key);
        std::uint64_t const claimed = detail::slot_generation_of_(key);
        if (index >= this->state_.used) {
            return false;
        }
        if ((claimed & 1u) == 0) {
            return false;
        }
        return claimed == this->state_.generations[index];
    }

    // The value of a slot that holds_ admitted.  The used slots are above
    // zero, so the mint gave the values a pointer that is not null.  The
    // assume reads a local pointer, so the Option of a Ref needs no test of
    // its niche, as window() of a View does.  A storage door: the values are
    // in the storage that the map owns.
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]] constexpr T*
        slot_of_(std::uint64_t key) const noexcept {
        T* const value = this->state_.values + detail::slot_index_of_(key);
        [[assume(value != nullptr)]];
        return value;
    }

public:
    using element_type = T;
    using key_type = SlotKey<Tag, Brand>;

    // The empty map of no slot.  It holds no storage, and each insert gives
    // Full.
    constexpr SlotMap() noexcept = default;

    // The core of foundation/core/Owner.h gives the moves and the end of a
    // map.  The assignment ends each value that this map holds after it takes
    // the slots of other, so other can live in one of those values.  The
    // assignment drops the values, so a map of a linear value has none.
    constexpr SlotMap(SlotMap&&) noexcept = default;
    constexpr SlotMap& operator=(SlotMap&&) noexcept
        requires DroppablePayload<T>
    = default;
    SlotMap& operator=(SlotMap&&)
        requires(!DroppablePayload<T>)
    = delete("an assignment drops the values that the SlotMap holds, and a linear value must not be dropped: build "
             "a new SlotMap instead");

    SlotMap(SlotMap const&) = delete("a copy of a SlotMap allocates, and no allocation capability shows at a copy");
    SlotMap& operator=(SlotMap const&) = delete("a copy of a SlotMap allocates, and no allocation capability shows at "
                                                "a copy");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return this->state_.live; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return this->state_.live == 0; }

    // The end of an empty map that the caller gives up: the process ends
    // with the reason when the map still holds a value.  A map of a linear
    // value ends here after the caller gives each value to its consumer, as
    // an empty Option ends at expect_none.  One compare.
    constexpr void expect_empty(Fmt<> why) && noexcept {
        if (this->state_.live != 0) [[unlikely]] {
            fatal(why);
        }
    }

    [[nodiscard]] constexpr Quantity<unit::Element, std::uint32_t> capacity() const noexcept {
        return Quantity<unit::Element, std::uint32_t>{this->state_.capacity};
    }

    // Puts value in a free slot (the free slots), and gives its key, or
    // Full<T> when no slot is free.  For a linear T the Full holds the value.
    // No allocation and no search.
    [[nodiscard]] constexpr Result<SlotKey<Tag, Brand>, Full<T>> insert(T value) & noexcept {
        State& state = this->state_;
        std::uint32_t index = 0;
        if (state.free_count != 0) {
            --state.free_count;
            index = state.free_stack[state.free_count];
        } else if (state.used != state.capacity) {
            index = state.used;
            state.generations[index] = 0;
            ++state.used;
        } else [[unlikely]] {
            if constexpr (::foundation::linear::is_linear_type(^^T)) {
                return err(Full<T>{static_cast<T&&>(value)});
            } else {
                return err(Full<T>{});
            }
        }
        Generation const generation = wrapping_add(state.generations[index], Generation{1});
        state.generations[index] = generation;
        detail::unpoison_slots_(state.values + index, 1);
        ::new(static_cast<void*>(state.values + index)) T(static_cast<T&&>(value));
        ++state.live;
        return SlotKey<Tag, Brand>{std::uint64_t{index} | (std::uint64_t{generation} << detail::slot_index_bits_)};
    }
    void insert(T) && = delete("an insert into a temporary SlotMap gives a key of a map that the full expression ends");

    // The value of key, as an Option of a Ref with the fresh brand of the
    // call, or no value when the slot of key does not hold the value of key
    // (the keys).  The pack refuses each explicit argument, and the result
    // type holds the brand in undeducible_brand_t.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(SlotKey<Tag, Brand> key) & noexcept {
        if (!holds_(key.raw())) {
            return Option<Ref<T, Fresh>>{none};
        }
        return Option<Ref<T, Fresh>>::some(SlotMapRefDoor::lend_<Fresh>(*slot_of_(key.raw())));
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] constexpr Option<Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>>>
    get(SlotKey<Tag, Brand> key) const& noexcept {
        if (!holds_(key.raw())) {
            return Option<Ref<T const, Fresh>>{none};
        }
        T const& value = *slot_of_(key.raw());
        return Option<Ref<T const, Fresh>>::some(SlotMapRefDoor::lend_<Fresh>(value));
    }
    void
    get(SlotKey<Tag, Brand>) && = delete("a value of a temporary SlotMap dangles at the end of the full expression");
    void get(SlotKey<Tag, Brand>) const&& =
        delete("a value of a temporary SlotMap dangles at the end of the full expression");

    // Moves the value of key out, and frees its slot: the generation of the
    // slot takes one more, and its index goes onto the stack of the free
    // slots.  The slot is then poisoned (the spare storage of Heap.h).  At
    // the largest generation the slot retires (the wrap of a generation).  No
    // value for a key that the slot does not hold.
    [[nodiscard]] constexpr Option<T> remove(SlotKey<Tag, Brand> key) & noexcept {
        if (!holds_(key.raw())) {
            return Option<T>{none};
        }
        State& state = this->state_;
        std::uint32_t const index = detail::slot_index_of_(key.raw());
        Option<T> result = Option<T>::some(static_cast<T&&>(*slot_of_(key.raw())));
        State::end_slot_(*slot_of_(key.raw()));
        detail::poison_slots_(state.values + index, 1);
        Generation const generation = state.generations[index];
        if (generation == max_of<Generation>) [[unlikely]] {
            state.generations[index] = 0;
        } else {
            state.generations[index] = wrapping_add(generation, Generation{1});
            state.free_stack[state.free_count] = index;
            ++state.free_count;
        }
        --state.live;
        return result;
    }

    // The walk `for (T& value : map)`: each value, in the order of the slots.
    [[nodiscard]] constexpr SlotCursor<T, Generation, T&> begin() & noexcept {
        return SlotWalk<T, Generation, T&>{this->state_}.begin();
    }
    [[nodiscard]] constexpr SlotCursor<T, Generation, T&> end() & noexcept {
        return SlotWalk<T, Generation, T&>{this->state_}.end();
    }
    [[nodiscard]] constexpr SlotCursor<T const, Generation, T const&> begin() const& noexcept {
        return SlotWalk<T const, Generation, T const&>{this->state_}.begin();
    }
    [[nodiscard]] constexpr SlotCursor<T const, Generation, T const&> end() const& noexcept {
        return SlotWalk<T const, Generation, T const&>{this->state_}.end();
    }
    void begin() && = delete("a cursor of a temporary SlotMap points into storage that the full expression frees");
    void end() && = delete("a cursor of a temporary SlotMap points into storage that the full expression frees");
    void begin() const&& = delete("a cursor of a temporary SlotMap points into storage that the full expression frees");
    void end() const&& = delete("a cursor of a temporary SlotMap points into storage that the full expression frees");

    // The key of each value, in the order of the slots:
    // `for (SlotKey<Tag, Brand> key : map.keys())`.
    [[nodiscard]] constexpr SlotWalk<T const, Generation, SlotKey<Tag, Brand>> keys() const& noexcept {
        return SlotWalk<T const, Generation, SlotKey<Tag, Brand>>{this->state_};
    }
    void keys() const&& = delete("a walk of a temporary SlotMap points into storage that the full expression frees");
};

// The empty map of capacity slots, in one allocation (build_slots_), with the
// fresh brand of the call (the brands).  A capacity of zero allocates
// nothing.  Complexity: one allocation.
template <class T, class Tag, class Generation, class... Refused, ::foundation::brand::IsBrand Fresh, class Marker>
    requires(sizeof...(Refused) == 0) && CanMintSlotMap<T, Tag, Generation, Marker>
[[nodiscard]] SlotMap<T, Tag, ::foundation::brand::undeducible_brand_t<Fresh>, Generation>
mint_slot_map(Marker /*allocation*/, Quantity<unit::Element, std::uint32_t> capacity) noexcept {
    return SlotMap<T, Tag, Fresh, Generation>::build_slots_(capacity.raw());
}

// The empty map of capacity slots, in one allocation (build_slots_), with the
// member tag MemberTag as its brand, for the one member of the class that
// nests the tag (the brands).  The rule brand-escape refuses each other
// object of the map type.  A capacity of zero allocates nothing.
// Complexity: one allocation.
template <class T, class Tag, class MemberTag, class Generation, class... Refused, class Marker>
    requires(sizeof...(Refused) == 0) && CanMintMemberSlotMap<T, Tag, MemberTag, Generation, Marker>
[[nodiscard]] SlotMap<T, Tag, MemberTag, Generation>
mint_member_slot_map(Marker /*allocation*/, Quantity<unit::Element, std::uint32_t> capacity) noexcept {
    return SlotMap<T, Tag, MemberTag, Generation>::build_slots_(capacity.raw());
}

}  // namespace foundation::core
