#pragma once

// THE ENDS OF A SINGLE-PRODUCER, SINGLE-CONSUMER OWNER
//   An owner of one producer side and one consumer side, such as a ring or a
//   log, keeps the state of each side in an end.  The producer end holds the
//   state of the producer, such as its copy of the head and its cached tail.
//   The consumer end holds the state of the consumer, such as its copy of the
//   tail.  Each end holds the Ref of its owner, so an end opens no other
//   owner.
//
//   - mint_spsc_split builds the owner in a Box and its two ends in one call.
//     No other door builds an end, and no door splits an owner that exists.
//     So an owner has at most one end of each side, with no run-time claim.
//   - An end only moves.  A copy would give a second writer of one side.  An
//     end takes no assignment, because an assignment would end a live end.
//   - The owner is a friend of its two ends.  The side operations are static
//     members of the owner that take the end, and they read the Ref and the
//     state of the end.  No other code reads them, so no code reaches a side
//     of the owner without its end.
//   - The owner does not move, so the object in the Box keeps its address,
//     and the Ref of each end stays valid while the Box lives.  The holder of
//     the split declares the Box before the ends, or gives each end to an
//     object that ends before the Box.  The ends then end first.
//
//   An end proves one holder of a side, not one thread.  The holder of an end
//   that more than one thread can reach keeps its own proof of the thread.

#include <foundation/core/Ref.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The side of an end.
enum class SpscSide : std::uint8_t {
    producer = 0,
    consumer = 1,
};

// The state of one side: it starts with no argument, and it moves with no
// failure.
template <class State>
concept SpscSideState = std::is_nothrow_default_constructible_v<State> && std::is_nothrow_move_constructible_v<State>;

// An owner names the state of each side.
template <class Owner>
concept SpscSideStates = requires {
    typename Owner::producer_state;
    typename Owner::consumer_state;
};

// An owner that splits into two ends: it names the state of each side, and
// the owner does not move.
template <class Owner>
concept SpscSplitOwner = std::is_class_v<Owner> && !std::is_move_constructible_v<Owner> && SpscSideStates<Owner>
                      && SpscSideState<typename Owner::producer_state> && SpscSideState<typename Owner::consumer_state>;

// The brand of the Ref that an end of Owner holds.  The Ref of an end then has
// a type of its own, apart from each other Ref of the owner type.
template <class Owner>
struct SpscSplitBrand final {};

// The gate of mint_spsc_split: an owner that splits, an allocation capability,
// and arguments that build the owner.
template <class Owner, class Marker, class... Args>
concept CanMintSpscSplit = SpscSplitOwner<Owner> && ::foundation::core::CanMintBox<Owner, Marker, Args...>;

template <class Owner, SpscSide Side>
class SpscEnd;

template <class Owner>
using ProducerEnd = SpscEnd<Owner, SpscSide::producer>;
template <class Owner>
using ConsumerEnd = SpscEnd<Owner, SpscSide::consumer>;

// The result of mint_spsc_split: the owner and its two ends.  The Box comes
// first, so a holder that keeps the three in this order ends the two ends
// before the owner.
template <class Owner>
struct [[nodiscard]] SpscSplit {
    ::foundation::core::Box<Owner> owner;
    ProducerEnd<Owner> producer;
    ConsumerEnd<Owner> consumer;
};

template <class Owner, class Marker, class... Args>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplit<Owner> mint_spsc_split(Marker allocation, Args&&... args) noexcept;

// One end of an owner.  Its two parts are private, and the owner reads them.
template <class Owner, SpscSide Side>
class [[nodiscard]] SpscEnd {
public:
    using owner_type = Owner;
    using state_type =
        std::conditional_t<Side == SpscSide::producer, typename Owner::producer_state, typename Owner::consumer_state>;
    static constexpr SpscSide side = Side;

    SpscEnd(SpscEnd const&) = delete("an end of an owner is the one holder of its side, so a copy would give a second "
                                     "writer of that side. Move the end");
    SpscEnd& operator=(SpscEnd const&) = delete("an end names one side of one owner for its life");
    SpscEnd(SpscEnd&&) noexcept = default;
    SpscEnd& operator=(SpscEnd&&) = delete("an end names one side of one owner for its life");
    ~SpscEnd() = default;

private:
    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

    constexpr explicit SpscEnd(owner_ref owner) noexcept : owner_{owner} {}

    friend Owner;
    template <class O, class M, class... A>
        requires CanMintSpscSplit<O, M, A...>
    friend SpscSplit<O> mint_spsc_split(M allocation, A&&... args) noexcept;

    owner_ref owner_;
    state_type state_{};
};

// Builds the owner in a Box of the allocation capability, and its two ends.
// The ends start with the state that each type gives with no argument, which
// is the state of a new owner.
template <class Owner, class Marker, class... Args>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplit<Owner> mint_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::detail::RefDoor::lend<SpscSplitBrand<Owner>>(*owner);
    return SpscSplit<Owner>{
        .owner = std::move(owner), .producer = ProducerEnd<Owner>{ref}, .consumer = ConsumerEnd<Owner>{ref}};
}

namespace spsc_ends_witness {

// An owner with two empty states, which the witness row of SpscEnd in
// utils/rule-table.txt names.  No code builds one.  It is outside a detail
// namespace, so the witness fixture of the row uses no detail name.
struct WitnessOwner final {
    struct producer_state {};
    struct consumer_state {};
    WitnessOwner() noexcept = default;
    WitnessOwner(WitnessOwner const&) = delete("the witness owner is pinned, as each owner of a split is");
    WitnessOwner(WitnessOwner&&) = delete("the witness owner is pinned, as each owner of a split is");
};

}  // namespace spsc_ends_witness

}  // namespace fixy::concurrent
