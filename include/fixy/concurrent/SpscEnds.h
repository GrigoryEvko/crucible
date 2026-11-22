#pragma once

// THE ENDS OF A SINGLE-PRODUCER, SINGLE-CONSUMER OWNER
//   An owner of one producer side and one consumer side, such as a ring or a
//   log, keeps the state of each side in an end.  The producer end holds the
//   state of the producer, such as its copy of the head and its cached tail.
//   The consumer end holds the state of the consumer, such as its copy of the
//   tail.  Each end holds the Ref of its owner, so an end opens no other
//   owner.
//
// THE SPLIT
//   mint_spsc_split builds the owner in a Box, and a split that holds the Box
//   and the two ends.  It gives the split and one loan key for each side
//   (THE LOAN KEY of foundation/core/Frame.h):
//     auto [split, producer_key, consumer_key] = mint_spsc_split<Log>(alloc, ...);
//   - The split holds the Box first and the two ends after it, as private
//     members.  So the ends end before the owner, and no end outlives the
//     owner that its Ref names.
//   - The split does not copy and does not move.  An end does not copy and
//     does not move.  So no end leaves its split, no code moves the Box out of
//     it, and an owner has exactly one end of each side for its life.
//   - Only the split builds an end, with a key that only the split builds.
//     So the owner, a friend of its ends, builds no second end.
//   - No door splits an owner that exists, and the mint refuses an owner that
//     moves, so the Ref of an end stays valid while the split lives.
//
// THE DOORS
//   A body borrows an end through a frame door of the split, for the time of
//   the call.  Each door takes the loan key of its side, by move or by a
//   borrow, and it refuses a body that reaches that key.  So no code inside a
//   body opens a second frame of the same side: not a nested door, not a
//   helper, not a closure that the body calls, and not a Ref of the split.
//     with_producer(key, body)       body gets the producer end
//     with_consumer(key, body)       body gets the consumer end
//     with_ends(producer_key, consumer_key, body)
//                                    body gets the two ends
//     with_owner(body)               body gets the owner as const, with no key
//   The result of a body holds no borrow, so no reference to an end leaves its
//   frame.  Two threads can each hold the key of one side, and each opens only
//   its own side.
//
//   The owner is a friend of its two ends.  The side operations are static
//   members of the owner that take the end, and they read the Ref and the
//   state of the end.  No other code reads them, so no code reaches a side of
//   the owner without its end.
//
//   An end proves one holder of a side, not one thread.  The holder of a key
//   that more than one thread can reach keeps its own proof of the thread.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Record.h>
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

// The brand of the loan key of one side of the splits of a brand.  The two
// sides have two brands, so the key of one side opens no frame of the other.
template <class Brand, SpscSide Side>
struct SpscSideBrand final {};

// The gate of mint_spsc_split: an owner that splits, an allocation capability,
// and arguments that build the owner.
template <class Owner, class Marker, class... Args>
concept CanMintSpscSplit = SpscSplitOwner<Owner> && ::foundation::core::CanMintBox<Owner, Marker, Args...>;

template <class Owner, SpscSide Side>
class SpscEnd;

template <class Owner, class Brand>
class SpscSplit;

namespace detail {

template <class Owner, class Brand>
struct SpscSplitDoor;

}  // namespace detail

template <class Owner>
using ProducerEnd = SpscEnd<Owner, SpscSide::producer>;
template <class Owner>
using ConsumerEnd = SpscEnd<Owner, SpscSide::consumer>;

// The loan keys of the two sides of the splits of a brand.
template <class Brand>
using ProducerKey = ::foundation::core::LoanKey<SpscSideBrand<Brand, SpscSide::producer>>;
template <class Brand>
using ConsumerKey = ::foundation::core::LoanKey<SpscSideBrand<Brand, SpscSide::consumer>>;

// The key of the constructor of an end.  Only a split builds one, so only a
// split builds an end.  The owner is a friend of its ends, and it cannot
// build this key, so it builds no second end of a side.  The seal closes the
// byte routes to a key.
template <class Owner>
class SpscEndKey final {
    template <class, class>
    friend class SpscSplit;

    constexpr SpscEndKey() noexcept = default;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    SpscEndKey(SpscEndKey const&) = delete("a key of an end lives for one construction of an end");
    SpscEndKey& operator=(SpscEndKey const&) = delete("a key of an end lives for one construction of an end");
};

// One end of an owner.  Its two parts are private, and the owner reads them.
// The Ref is const, so no code points an end at a second owner.
template <class Owner, SpscSide Side>
class [[nodiscard]] SpscEnd final {
public:
    using owner_type = Owner;
    using state_type =
        std::conditional_t<Side == SpscSide::producer, typename Owner::producer_state, typename Owner::consumer_state>;
    static constexpr SpscSide side = Side;

    SpscEnd(SpscEnd const&) = delete("an end of an owner is the one holder of its side, so a copy would give a second "
                                     "writer of that side. Borrow the end through a frame door of its split");
    SpscEnd& operator=(SpscEnd const&) = delete("an end names one side of one owner for its life");
    SpscEnd(SpscEnd&&) = delete("an end lives in its split beside its owner, so a move would let the end outlive the "
                                "owner or give a second holder of its side. Borrow the end through a frame door of "
                                "its split");
    SpscEnd& operator=(SpscEnd&&) = delete("an end names one side of one owner for its life");
    ~SpscEnd() = default;

private:
    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

    constexpr SpscEnd(SpscEndKey<Owner> const& /*key*/, owner_ref owner) noexcept : owner_{owner} {}

    friend Owner;
    template <class, class>
    friend class SpscSplit;

    owner_ref const owner_;
    state_type state_{};
};

// The body of a frame door of one end: it takes the end with no throw, its
// result holds no borrow and fits a Record, and it reaches no key of the side
// (THE GATE of foundation/core/Frame.h).
template <class Body, class End, class KeyBrand>
concept SpscEndBody = std::is_nothrow_invocable_v<Body&&, End&>
                   && (std::is_void_v<std::invoke_result_t<Body&&, End&>>
                       || (::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, End&>>
                           && ::foundation::core::RecordElement<std::remove_cv_t<std::invoke_result_t<Body&&, End&>>>))
                   && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>;

// The body of the frame door of the two ends of a split: it takes the two
// ends with no throw, its result holds no borrow, and it reaches no key of
// either side.
template <class Body, class Producer, class Consumer, class ProducerBrand, class ConsumerBrand>
concept SpscEndsBody = std::is_nothrow_invocable_v<Body&&, Producer&, Consumer&>
                    && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Producer&, Consumer&>>
                    && ::foundation::core::ReachesNoLoanKey<Body, ProducerBrand>
                    && ::foundation::core::ReachesNoLoanKey<Body, ConsumerBrand>;

// A body of a frame door of one end that reaches the key of the side: the
// refused twin of the door binds to it.
template <class Body, class End, class KeyBrand>
concept SpscKeyReachingBody =
    std::is_nothrow_invocable_v<Body&&, End&> && !::foundation::core::ReachesNoLoanKey<Body, KeyBrand>;

// A body of a frame door of one end whose result holds a borrow: the refused
// twin of the door binds to it.
template <class Body, class End>
concept SpscBorrowGivingBody =
    std::is_nothrow_invocable_v<Body&&, End&> && !std::is_void_v<std::invoke_result_t<Body&&, End&>>
    && !::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, End&>>;

// The owner in a Box and its two ends (THE SPLIT).  The Box is the first
// member, so the two ends end before it.  The split does not move, so the
// Box stays in it.
template <class Owner, class Brand>
class [[nodiscard]][[= ::foundation::core::loan_key_mark{}]] SpscSplit final {
    using producer_brand = SpscSideBrand<Brand, SpscSide::producer>;
    using consumer_brand = SpscSideBrand<Brand, SpscSide::consumer>;
    using producer_end = ProducerEnd<Owner>;
    using consumer_end = ConsumerEnd<Owner>;

public:
    using owner_type = Owner;
    using brand_type = Brand;

    SpscSplit(SpscSplit const&) = delete("a split holds the one end of each side of its owner: a copy would give a "
                                         "second end of each side");
    SpscSplit& operator=(SpscSplit const&) = delete("a split holds one owner and its two ends for their life");
    SpscSplit(SpscSplit&&) = delete("the ends of a split live beside the Box of their owner, so a split does not "
                                    "move, and no code moves its Box out while an end lives");
    SpscSplit& operator=(SpscSplit&&) = delete("a split holds one owner and its two ends for their life");
    ~SpscSplit() = default;

    // THE PRODUCER DOOR: body gets the producer end in place.  The key moves
    // into the door, and the door gives it back: alone for a body that gives
    // void, and after the result in a Record for each other body.  The borrow
    // form keeps the key with the caller and gives the result of body.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires SpscEndBody<Body, producer_end, producer_brand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, producer_end&>,
                                                                       ProducerKey<Brand>>
    with_producer(ProducerKey<Brand>&& key, Body&& body) & noexcept {
        return give_key_back_(key, producer_, static_cast<Body&&>(body));
    }
    template <class Body>
        requires SpscEndBody<Body, producer_end, producer_brand>
    constexpr auto with_producer(ProducerKey<Brand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(producer_);
    }

    // THE CONSUMER DOOR, in the two forms of the producer door.
    template <class Body>
        requires SpscEndBody<Body, consumer_end, consumer_brand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, consumer_end&>,
                                                                       ConsumerKey<Brand>>
    with_consumer(ConsumerKey<Brand>&& key, Body&& body) & noexcept {
        return give_key_back_(key, consumer_, static_cast<Body&&>(body));
    }
    template <class Body>
        requires SpscEndBody<Body, consumer_end, consumer_brand>
    constexpr auto with_consumer(ConsumerKey<Brand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(consumer_);
    }

    // THE DOOR OF THE TWO ENDS: body gets the producer end and the consumer
    // end, and the door borrows the two keys for the call.  The two ends are
    // two members, so the two borrows do not overlap.  The body reaches no key
    // of either side, and its result holds no borrow.  Complexity: O(1), and
    // the cost of body.
    template <class Body>
        requires SpscEndsBody<Body, producer_end, consumer_end, producer_brand, consumer_brand>
    constexpr auto with_ends(ProducerKey<Brand>& /*producer_key*/, ConsumerKey<Brand>& /*consumer_key*/,
                             Body&& body) & noexcept {
        return static_cast<Body&&>(body)(producer_, consumer_);
    }

    // THE READ OF THE OWNER: body gets the owner as const, through the Ref of
    // the producer end, and the door gives the result of body.  A read opens
    // no side, so the door takes no key.  Complexity: O(1), and the cost of
    // body.
    template <class Body>
        requires std::is_nothrow_invocable_v<Body&&, Owner const&>
              && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Owner const&>>
    constexpr auto with_owner(Body&& body) const& noexcept {
        Owner const& owner = *producer_.owner_;
        return static_cast<Body&&>(body)(owner);
    }

    // A door with no key binds to these overloads, so the refusal gives its
    // reason.
    template <class Body>
        requires std::is_invocable_v<Body&&, producer_end&>
    void with_producer(Body&& body) & = delete("a split lends its producer end only with the producer key: call "
                                               "with_producer(std::move(key), body) or with_producer(key, body)");
    template <class Body>
        requires std::is_invocable_v<Body&&, consumer_end&>
    void with_consumer(Body&& body) & = delete("a split lends its consumer end only with the consumer key: call "
                                               "with_consumer(std::move(key), body) or with_consumer(key, body)");

    // A body that reaches the key of its side binds to these overloads, so
    // the refusal gives its reason.
    template <class Body>
        requires SpscKeyReachingBody<Body, producer_end, producer_brand>
    void with_producer(ProducerKey<Brand>&& key, Body&& body) & = delete(
        "a body of a frame door of a split reaches the key of its side, so code in the body could open a second frame "
        "of that side: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires SpscKeyReachingBody<Body, producer_end, producer_brand>
    void with_producer(ProducerKey<Brand>& key, Body&& body) & = delete(
        "a body of a frame door of a split reaches the key of its side, so code in the body could open a second frame "
        "of that side: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires SpscKeyReachingBody<Body, consumer_end, consumer_brand>
    void with_consumer(ConsumerKey<Brand>&& key, Body&& body) & = delete(
        "a body of a frame door of a split reaches the key of its side, so code in the body could open a second frame "
        "of that side: capture only what the body reads, and keep the key with the caller");
    template <class Body>
        requires SpscKeyReachingBody<Body, consumer_end, consumer_brand>
    void with_consumer(ConsumerKey<Brand>& key, Body&& body) & = delete(
        "a body of a frame door of a split reaches the key of its side, so code in the body could open a second frame "
        "of that side: capture only what the body reads, and keep the key with the caller");

    // A body whose result holds a borrow binds to these overloads, so the
    // refusal gives its reason.
    template <class Body>
        requires SpscBorrowGivingBody<Body, producer_end>
    void with_producer(ProducerKey<Brand>&& key, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");
    template <class Body>
        requires SpscBorrowGivingBody<Body, producer_end>
    void with_producer(ProducerKey<Brand>& key, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");
    template <class Body>
        requires SpscBorrowGivingBody<Body, consumer_end>
    void with_consumer(ConsumerKey<Brand>&& key, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");
    template <class Body>
        requires SpscBorrowGivingBody<Body, consumer_end>
    void with_consumer(ConsumerKey<Brand>& key, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");

private:
    friend struct detail::SpscSplitDoor<Owner, Brand>;

    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

    // The split of the owner in the Box.  The Ref names the object in the Box,
    // and the mint makes it.
    SpscSplit(::foundation::core::Box<Owner>&& owner, owner_ref ref) noexcept
        : owner_{std::move(owner)}, producer_{SpscEndKey<Owner>{}, ref}, consumer_{SpscEndKey<Owner>{}, ref} {}

    // Runs body on the end, and gives the key back: alone for a body that
    // gives void, and after the result in a Record for each other body.
    template <class End, class Key, class Body>
    [[nodiscard]] static constexpr auto give_key_back_(Key& key, End& end, Body&& body) noexcept
        -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, End&>, Key> {
        using Given = std::invoke_result_t<Body&&, End&>;
        if constexpr (std::is_void_v<Given>) {
            static_cast<Body&&>(body)(end);
            return std::move(key);
        } else {
            return ::foundation::core::detail::frame_return_t<Given, Key>{static_cast<Body&&>(body)(end),
                                                                          std::move(key)};
        }
    }

    ::foundation::core::Box<Owner> owner_;
    producer_end producer_;
    consumer_end consumer_;
};

// The result of mint_spsc_split: the split, and the key of each side.  A
// binding takes the three parts by name, in place, so the split does not
// move.  The split cannot move, so neither can this holder, and the end of
// its scope is the door of each part: the split ends its owner and its two
// ends, and a key holds no resource.
template <class Owner, class Brand>
struct [[nodiscard]][[= ::foundation::linear::scope_end_door{}]] SpscSplitParts final {
    SpscSplit<Owner, Brand> split;
    ProducerKey<Brand> producer_key;
    ConsumerKey<Brand> consumer_key;
};

namespace detail {

// The one door that builds a split, for mint_spsc_split.  The split names it
// as a friend, and no other code builds a split.  The door is in namespace
// detail, and a use of that namespace outside the base is a finding.
template <class Owner, class Brand>
struct SpscSplitDoor final {
    [[nodiscard]] static SpscSplitParts<Owner, Brand>
    build(::foundation::core::Box<Owner>&& owner, ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>> ref) noexcept {
        return {SpscSplit<Owner, Brand>{std::move(owner), ref},
                ProducerKey<Brand>{::foundation::core::detail::FrameDoor::key()},
                ConsumerKey<Brand>{::foundation::core::detail::FrameDoor::key()}};
    }
};

}  // namespace detail

// Builds the owner in a Box of the allocation capability, the split that
// holds it with its two ends, and the key of each side.  The ends start with
// the state that each type gives with no argument, which is the state of a
// new owner.  The brand is the fresh brand of the call site, in
// undeducible_brand_t, so no caller names it.  The split comes back as a
// prvalue part of the Record, so it does not move.
template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>
mint_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::detail::RefDoor::lend<SpscSplitBrand<Owner>>(*owner);
    return detail::SpscSplitDoor<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>::build(std::move(owner), ref);
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
