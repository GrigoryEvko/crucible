#pragma once

// Ends of a single-producer, single-consumer owner
//   An owner of one producer side and one consumer side, such as a ring or a
//   log, keeps the state of each side in an end.  The producer end holds the
//   state of the producer, such as its copy of the head and its cached tail.
//   The consumer end holds the state of the consumer, such as its copy of the
//   tail.  Each end holds the Ref of its owner, so an end opens no other
//   owner.
//
// Split
//   mint_spsc_split builds the owner in a Box, and a split that holds the Box
//   and the two ends.  It gives the split and one loan key for each side
//   (the loan key of foundation/core/Frame.h):
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
// Doors
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
//   frame.  The key of one side opens only that side.
//
// Thread
//   A split stays on one thread.  Its doors take loan keys, and a loan key
//   proves no thread (foundation/Transfer.h), so the split states no transfer
//   fact, and two threads cannot write it at the same time.  A door that
//   starts threads thus refuses two bodies that borrow one split, also when
//   each body holds the key of a different side.
//
//   The owner is a friend of its two ends.  The side operations are static
//   members of the owner that take the end, and they read the Ref and the
//   state of the end.  No other code reads them, so no code reaches a side of
//   the owner without its end.
//
//   An end proves one holder of a side, not one thread.  The holder of a key
//   that more than one thread can reach keeps its own proof of the thread.
//
// Held split
//   A class that holds a split as a member, such as the ring of a Vigil, names
//   its brand, and a key of a named brand has no mint (the borrow of a root
//   door of foundation/core/Frame.h).  So a held brand names a proof for each
//   side in place of a key: its member types producer_proof and
//   consumer_proof (SpscHeldBrand).  A proof is an empty class that no code
//   builds with no argument and that does not copy, such as the proof of the
//   one writer of foundation/SoleWriterProof.h.  The owner of each proof makes
//   it only on the thread of its side.
//     SpscSplit<Log, HeldBrand> split = mint_held_spsc_split<Log, HeldBrand>(alloc, ...);
//     split.with_producer(producer_proof, body)
//     split.with_consumer(consumer_proof, body)
//   - The mint gives the split alone, with no key, as a prvalue, so the
//     holder builds its member in place.
//   - The keyed doors refuse a held split, so a key of a side of a held brand
//     opens nothing.
//   - A proof door refuses a body that reaches a split of the brand through a
//     borrow, so no code inside the body opens a second frame of a side.
//   - The proofs prove a thread for each side, not one holder of a split.  Two
//     holders of one class hold two splits of one brand, and the type does
//     not refuse a proof of one holder at the split of the other.  Each holder
//     keeps its split private, so only its own code reaches the split.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/core/CaptureStore.h>
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
// The brand states the brand of its splits (states_owner_brand of
// foundation/core/Frame.h).  The walk of the borrow of a root door reads it,
// so it counts a split of the brand as an owner of each side brand.  A root
// door thus mints no key of a side for a body that borrows the split.
template <class Brand, SpscSide Side>
struct SpscSideBrand final : ::foundation::core::states_owner_brand<Brand> {};

// The gate of mint_spsc_split: an owner that splits, an allocation capability,
// and arguments that build the owner.
template <class Owner, class Marker, class... Args>
concept CanMintSpscSplit = SpscSplitOwner<Owner> && ::foundation::core::CanMintBox<Owner, Marker, Args...>;

// A proof that opens one side of a held split: an empty class that no code
// builds with no argument and that does not copy.
template <class Proof>
concept SpscSideProof = std::is_class_v<Proof> && std::is_empty_v<Proof> && !std::is_default_constructible_v<Proof>
                     && !std::is_copy_constructible_v<Proof>;

// A held brand (the held split): a brand that names a proof for each side, and
// the two proofs are two types.
template <class Brand>
concept SpscNamesSideProofs = requires {
    typename Brand::producer_proof;
    typename Brand::consumer_proof;
};
template <class Brand>
concept SpscHeldBrand = ::foundation::brand::IsFreshBrand<Brand> && SpscNamesSideProofs<Brand>
                     && SpscSideProof<typename Brand::producer_proof> && SpscSideProof<typename Brand::consumer_proof>
                     && !std::is_same_v<typename Brand::producer_proof, typename Brand::consumer_proof>;

// The gate of mint_held_spsc_split: an owner that splits, a held brand, an
// allocation capability, and arguments that build the owner.
template <class Owner, class Brand, class Marker, class... Args>
concept CanMintHeldSpscSplit =
    SpscSplitOwner<Owner> && SpscHeldBrand<Brand> && ::foundation::core::CanMintBox<Owner, Marker, Args...>;

// The type that no code builds: the proof of a side of a brand that is not
// held.  A proof door of such a split then takes no argument.  The
// destructor is user-provided, so the class is not trivially copyable and not
// an implicit-lifetime type, and no byte route builds one.
class SpscNoSideProof final {
public:
    SpscNoSideProof() = delete("a split of a brand that is not held opens with a key, not with a proof");
    SpscNoSideProof(SpscNoSideProof const&) = delete("a split of a brand that is not held opens with a key");
    SpscNoSideProof& operator=(SpscNoSideProof const&) = delete("a split of a brand that is not held opens with a key");
    ~SpscNoSideProof() {}
};

// The proof of one side of the splits of a brand: the type that a held brand
// names, and SpscNoSideProof for each other brand.
template <class Brand, SpscSide Side>
struct spsc_side_proof {
    using type = SpscNoSideProof;
};
template <class Brand>
    requires SpscHeldBrand<Brand>
struct spsc_side_proof<Brand, SpscSide::producer> {
    using type = typename Brand::producer_proof;
};
template <class Brand>
    requires SpscHeldBrand<Brand>
struct spsc_side_proof<Brand, SpscSide::consumer> {
    using type = typename Brand::consumer_proof;
};
template <class Brand, SpscSide Side>
using spsc_side_proof_t = typename spsc_side_proof<Brand, Side>::type;

template <class Owner, SpscSide Side>
class SpscEnd;

template <class Owner, class Brand>
class SpscSplit;

template <class Owner, class Brand>
struct SpscSplitParts;

namespace detail {

template <class Owner, class Brand>
struct SpscSplitDoor;

}  // namespace detail

// The two mints of a split (the definitions below).  SpscSplitRefDoor names
// them as friends, so these declarations stand before the door, and the
// default argument of the brand belongs to the first one.
template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>
mint_spsc_split(Marker allocation, Args&&... args) noexcept;

template <class Owner, class Brand, class Marker, class... Args>
    requires CanMintHeldSpscSplit<Owner, Brand, Marker, Args...>
[[nodiscard]] SpscSplit<Owner, Brand> mint_held_spsc_split(Marker allocation, Args&&... args) noexcept;

}  // namespace fixy::concurrent

namespace foundation::core {

// The door of the two mints of a split to the door of a Ref (the doors of the
// owners above foundation/core/Ref.h).  Its one member is private, and the
// mints of fixy/concurrent/SpscEnds.h are its two friends.  The invariant: a
// mint lends the owner that it just built in a new Box, with the split brand
// of the type of the owner, and the split that holds the Box takes the Ref.
// So each Ref of the brand names the owner in the Box of its split.  A home
// owner row of utils/rule-table.txt keeps this definition in this header.
class SpscSplitRefDoor final : ::foundation::NoObject<SpscSplitRefDoor> {
    template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh>
        requires ::fixy::concurrent::CanMintSpscSplit<Owner, Marker, Args...>
    friend auto ::fixy::concurrent::mint_spsc_split(Marker allocation, Args&&... args) noexcept
        -> ::fixy::concurrent::SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>;
    template <class Owner, class Brand, class Marker, class... Args>
        requires ::fixy::concurrent::CanMintHeldSpscSplit<Owner, Brand, Marker, Args...>
    friend auto ::fixy::concurrent::mint_held_spsc_split(Marker allocation, Args&&... args) noexcept
        -> ::fixy::concurrent::SpscSplit<Owner, Brand>;

    // The owner in the new Box of a mint, as a Ref of the split brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

// The door of the mint of a split to the key of the frame doors (the doors
// of the owners above foundation/core/Frame.h).  Its one member is private,
// and mint_spsc_split of fixy/concurrent/SpscEnds.h is its one friend.  The
// invariant: the mint gives the one key of each side of the fresh brand of
// its call, with the new split of that brand.  The door of the split takes
// the key from the mint and builds the two keys.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class SpscSplitFrameKeyDoor final : ::foundation::NoObject<SpscSplitFrameKeyDoor> {
    template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh>
        requires ::fixy::concurrent::CanMintSpscSplit<Owner, Marker, Args...>
    friend auto ::fixy::concurrent::mint_spsc_split(Marker allocation, Args&&... args) noexcept
        -> ::fixy::concurrent::SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>;

    // The key of the frame doors, for the keys of the two sides of a split.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy::concurrent {

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
// result holds no borrow and fits a Record, it reaches no key of the side
// (the gate of foundation/core/Frame.h), it holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h), and it stores no borrow
// outside the frame (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).  A Ref of the end in a capture would let
// the caller write the side after the frame, beside the next holder of the
// side.
template <class Body, class End, class KeyBrand>
concept SpscEndBody = std::is_nothrow_invocable_v<Body&&, End&>
                   && (std::is_void_v<std::invoke_result_t<Body&&, End&>>
                       || (::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, End&>>
                           && ::foundation::core::RecordElement<std::remove_cv_t<std::invoke_result_t<Body&&, End&>>>))
                   && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>
                   && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>;

// The body of the frame door of the two ends of a split: it takes the two
// ends with no throw, its result holds no borrow, it reaches no key of either
// side, it holds no linear value by value, and it stores no borrow outside
// the frame.
template <class Body, class Producer, class Consumer, class ProducerBrand, class ConsumerBrand>
concept SpscEndsBody =
    std::is_nothrow_invocable_v<Body&&, Producer&, Consumer&>
    && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Producer&, Consumer&>>
    && ::foundation::core::ReachesNoLoanKey<Body, ProducerBrand>
    && ::foundation::core::ReachesNoLoanKey<Body, ConsumerBrand> && ::foundation::linear::HoldsNoLinearValue<Body>
    && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of a frame door of one end that reaches no key of the side and
// holds a linear value by value: the linear twin of the door binds to it.
template <class Body, class End, class KeyBrand>
concept SpscLinearEndBody = (!::foundation::linear::HoldsNoLinearValue<Body>)
                         && std::is_invocable_v<Body&&, End&> && ::foundation::core::ReachesNoLoanKey<Body, KeyBrand>;

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

// True when the body reaches a split of the brand through a borrow, or a
// class whose members the walk cannot read: the walk of the borrow of a root
// door (foundation/core/Frame.h), which counts each class with the mark of a
// keyed owner whose arguments name the brand.
template <class Body, class Brand>
concept SpscReachesHeldSplit =
    ::foundation::core::detail::reaches_borrowed_key_owner_(^^std::remove_cvref_t<Body>, std::meta::dealias(^^Brand));

// The body of a proof door of one end of a held split: it takes the end with
// no throw, its result holds no borrow and fits a Record, it reaches no split
// of the brand through a borrow, it holds no linear value by value, and it
// stores no borrow outside the frame.
template <class Body, class End, class Brand>
concept SpscHeldEndBody =
    std::is_nothrow_invocable_v<Body&&, End&>
    && (std::is_void_v<std::invoke_result_t<Body&&, End&>>
        || (::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, End&>>
            && ::foundation::core::RecordElement<std::remove_cv_t<std::invoke_result_t<Body&&, End&>>>))
    && !SpscReachesHeldSplit<Body, Brand> && ::foundation::linear::HoldsNoLinearValue<Body>
    && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of a proof door that reaches a split of the brand through a borrow:
// the refused twin of the door binds to it.
template <class Body, class End, class Brand>
concept SpscHeldSplitReachingBody = std::is_nothrow_invocable_v<Body&&, End&> && SpscReachesHeldSplit<Body, Brand>;

// The owner in a Box and its two ends (the split).  The Box is the first
// member, so the two ends end before it.  The split does not move, so the
// Box stays in it.
template <class Owner, class Brand>
class [[nodiscard]][[= ::foundation::core::loan_key_mark{}]] SpscSplit final {
    using producer_brand = SpscSideBrand<Brand, SpscSide::producer>;
    using consumer_brand = SpscSideBrand<Brand, SpscSide::consumer>;
    using producer_end = ProducerEnd<Owner>;
    using consumer_end = ConsumerEnd<Owner>;
    using producer_proof = spsc_side_proof_t<Brand, SpscSide::producer>;
    using consumer_proof = spsc_side_proof_t<Brand, SpscSide::consumer>;

    // True for a split of a held brand (the held split), which opens its sides
    // with the proofs of the brand and with no key.
    static constexpr bool is_held_ = SpscHeldBrand<Brand>;

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

    // Producer door: body gets the producer end in place.  The key moves
    // into the door, and the door gives it back: alone for a body that gives
    // void, and after the result in a Record for each other body.  The borrow
    // form keeps the key with the caller and gives the result of body.
    // Complexity: O(1), and the cost of body.
    template <class Body>
        requires(!is_held_) && SpscEndBody<Body, producer_end, producer_brand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, producer_end&>,
                                                                       ProducerKey<Brand>>
    with_producer(ProducerKey<Brand>&& key, Body&& body) & noexcept {
        return give_key_back_(std::move(key), producer_, static_cast<Body&&>(body));
    }
    template <class Body>
        requires(!is_held_) && SpscEndBody<Body, producer_end, producer_brand>
    constexpr auto with_producer(ProducerKey<Brand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(producer_);
    }

    // Consumer door, in the two forms of the producer door.
    template <class Body>
        requires(!is_held_) && SpscEndBody<Body, consumer_end, consumer_brand>
    [[nodiscard]] constexpr ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, consumer_end&>,
                                                                       ConsumerKey<Brand>>
    with_consumer(ConsumerKey<Brand>&& key, Body&& body) & noexcept {
        return give_key_back_(std::move(key), consumer_, static_cast<Body&&>(body));
    }
    template <class Body>
        requires(!is_held_) && SpscEndBody<Body, consumer_end, consumer_brand>
    constexpr auto with_consumer(ConsumerKey<Brand>& /*key*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(consumer_);
    }

    // Door of the two ends: body gets the producer end and the consumer
    // end, and the door borrows the two keys for the call.  The two ends are
    // two members, so the two borrows do not overlap.  The body reaches no key
    // of either side, and its result holds no borrow.  Complexity: O(1), and
    // the cost of body.
    template <class Body>
        requires(!is_held_) && SpscEndsBody<Body, producer_end, consumer_end, producer_brand, consumer_brand>
    constexpr auto with_ends(ProducerKey<Brand>& /*producer_key*/, ConsumerKey<Brand>& /*consumer_key*/,
                             Body&& body) & noexcept {
        return static_cast<Body&&>(body)(producer_, consumer_);
    }

    // Producer door of a held split (the held split): body gets the producer
    // end in place, for the caller that gives the producer proof of the brand.
    // The door reads no byte of the proof.  The body reaches no split of the
    // brand through a borrow, so no code inside it opens a second frame of a
    // side.  Complexity: O(1), and the cost of body.
    template <class Body>
        requires is_held_ && SpscHeldEndBody<Body, producer_end, Brand>
    constexpr auto with_producer(producer_proof const& /*proof*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(producer_);
    }

    // Consumer door of a held split, in the form of its producer door.
    template <class Body>
        requires is_held_ && SpscHeldEndBody<Body, consumer_end, Brand>
    constexpr auto with_consumer(consumer_proof const& /*proof*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(consumer_);
    }

    // Read of the owner: body gets the owner as const, through the Ref of
    // the producer end, and the door gives the result of body.  A read opens
    // no side, so the door takes no key.  Complexity: O(1), and the cost of
    // body.
    template <class Body>
        requires std::is_nothrow_invocable_v<Body&&, Owner const&>
              && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Owner const&>>
              && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>
    constexpr auto with_owner(Body&& body) const& noexcept {
        Owner const& owner = *producer_.owner_;
        return static_cast<Body&&>(body)(owner);
    }

    // A body that holds a linear value by value binds to these overloads, so
    // the refusal gives its reason.
    template <class Body>
        requires SpscLinearEndBody<Body, producer_end, producer_brand>
    void with_producer(ProducerKey<Brand>&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires SpscLinearEndBody<Body, producer_end, producer_brand>
    void with_producer(ProducerKey<Brand>& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires SpscLinearEndBody<Body, consumer_end, consumer_brand>
    void with_consumer(ConsumerKey<Brand>&& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires SpscLinearEndBody<Body, consumer_end, consumer_brand>
    void with_consumer(ConsumerKey<Brand>& key, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires std::is_invocable_v<Body&&, producer_end&, consumer_end&>
                  && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_ends(ProducerKey<Brand>& producer_key, ConsumerKey<Brand>& consumer_key,
                   Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires std::is_invocable_v<Body&&, Owner const&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_owner(Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // A door with no key binds to these overloads, so the refusal gives its
    // reason.
    template <class Body>
        requires std::is_invocable_v<Body&&, producer_end&>
    void with_producer(Body&& body) & = delete("a split lends its producer end only with the producer key, or with "
                                               "the producer proof of a held brand: call with_producer(key, body) or "
                                               "with_producer(proof, body)");
    template <class Body>
        requires std::is_invocable_v<Body&&, consumer_end&>
    void with_consumer(Body&& body) & = delete("a split lends its consumer end only with the consumer key, or with "
                                               "the consumer proof of a held brand: call with_consumer(key, body) or "
                                               "with_consumer(proof, body)");

    // A key at a door of a held split binds to these overloads, so the
    // refusal gives its reason.  A root door can mint a key of a side brand of
    // a held brand, and that key opens nothing.
    template <class Body>
        requires is_held_
    void with_producer(ProducerKey<Brand>&& key, Body&& body) & =
        delete("a held split opens its producer side only with the producer proof of its brand, and no key opens it");
    template <class Body>
        requires is_held_
    void with_producer(ProducerKey<Brand>& key, Body&& body) & =
        delete("a held split opens its producer side only with the producer proof of its brand, and no key opens it");
    template <class Body>
        requires is_held_
    void with_consumer(ConsumerKey<Brand>&& key, Body&& body) & =
        delete("a held split opens its consumer side only with the consumer proof of its brand, and no key opens it");
    template <class Body>
        requires is_held_
    void with_consumer(ConsumerKey<Brand>& key, Body&& body) & =
        delete("a held split opens its consumer side only with the consumer proof of its brand, and no key opens it");
    template <class Body>
        requires is_held_
    void with_ends(ProducerKey<Brand>& producer_key, ConsumerKey<Brand>& consumer_key, Body&& body) & =
        delete("a held split opens each side only with the proof of that side, and no key opens it");

    // A body at a proof door that reaches a split of the brand, or that holds
    // a linear value by value, binds to these overloads, so the refusal gives
    // its reason.
    template <class Body>
        requires is_held_ && SpscHeldSplitReachingBody<Body, producer_end, Brand>
    void with_producer(producer_proof const& proof, Body&& body) & = delete(
        "a body of a frame door of a held split reaches a split of its brand through a borrow, so code in the body "
        "could open a second frame of that side: capture only what the body reads");
    template <class Body>
        requires is_held_ && SpscHeldSplitReachingBody<Body, consumer_end, Brand>
    void with_consumer(consumer_proof const& proof, Body&& body) & = delete(
        "a body of a frame door of a held split reaches a split of its brand through a borrow, so code in the body "
        "could open a second frame of that side: capture only what the body reads");
    template <class Body>
        requires is_held_ && SpscBorrowGivingBody<Body, producer_end>
    void with_producer(producer_proof const& proof, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");
    template <class Body>
        requires is_held_ && SpscBorrowGivingBody<Body, consumer_end>
    void with_consumer(consumer_proof const& proof, Body&& body) & = delete(
        "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a "
        "plain value");
    template <class Body>
        requires is_held_
                  && std::is_invocable_v<Body&&, producer_end&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_producer(producer_proof const& proof, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires is_held_
                  && std::is_invocable_v<Body&&, consumer_end&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_consumer(consumer_proof const& proof, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

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
    // gives void, and after the result in a Record for each other body.  The
    // door gives up its key here, so the helper takes it as an rvalue.
    template <class End, class Key, class Body>
    [[nodiscard]] static constexpr auto give_key_back_(Key&& key, End& end, Body&& body) noexcept
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
// detail, and a use of that namespace outside the base is a finding.  build
// takes the key of the frame doors from the mint, which alone holds it, and
// builds the key of each side with it.
template <class Owner, class Brand>
struct SpscSplitDoor final {
    [[nodiscard]] static SpscSplitParts<Owner, Brand>
    build(::foundation::core::detail::FrameKey const& key, ::foundation::core::Box<Owner>&& owner,
          ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>> ref) noexcept {
        return {SpscSplit<Owner, Brand>{std::move(owner), ref}, ProducerKey<Brand>{key}, ConsumerKey<Brand>{key}};
    }

    // The split of a held brand, with no key (the held split).
    [[nodiscard]] static SpscSplit<Owner, Brand>
    build_held(::foundation::core::Box<Owner>&& owner,
               ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>> ref) noexcept
        requires SpscHeldBrand<Brand>
    {
        return SpscSplit<Owner, Brand>{std::move(owner), ref};
    }
};

}  // namespace detail

// Builds the owner in a Box of the allocation capability, the split that
// holds it with its two ends, and the key of each side.  The ends start with
// the state that each type gives with no argument, which is the state of a
// new owner.  The brand is the fresh brand of the call site, in
// undeducible_brand_t, so no caller names it.  The split comes back as a
// prvalue part of the Record, so it does not move.
template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>
mint_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::SpscSplitRefDoor::lend_<SpscSplitBrand<Owner>>(*owner);
    return detail::SpscSplitDoor<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>::build(
        ::foundation::core::SpscSplitFrameKeyDoor::key_(), std::move(owner), ref);
}

// Builds the owner in a Box of the allocation capability and the split of the
// held brand that holds it with its two ends (the held split).  It gives no
// key.  The ends start with the state that each type gives with no argument.
// The split comes back as a prvalue, so a holder builds its member in place:
//   SpscSplit<Log, HeldBrand> split_ = mint_held_spsc_split<Log, HeldBrand>(alloc);
template <class Owner, class Brand, class Marker, class... Args>
    requires CanMintHeldSpscSplit<Owner, Brand, Marker, Args...>
[[nodiscard]] SpscSplit<Owner, Brand> mint_held_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::SpscSplitRefDoor::lend_<SpscSplitBrand<Owner>>(*owner);
    return detail::SpscSplitDoor<Owner, Brand>::build_held(std::move(owner), ref);
}

namespace spsc_ends_witness {

// An owner with two empty states and a brand, which the witness rows of
// SpscEnd, SpscEndKey and SpscSplit in utils/rule-table.txt name.  No code
// builds one.  They are outside a detail namespace, so the witness fixtures
// of the rows use no detail name.
struct WitnessOwner final {
    struct producer_state {};
    struct consumer_state {};
    WitnessOwner() noexcept = default;
    WitnessOwner(WitnessOwner const&) = delete("the witness owner is pinned, as each owner of a split is");
    WitnessOwner(WitnessOwner&&) = delete("the witness owner is pinned, as each owner of a split is");
};
struct WitnessBrand final {};

}  // namespace spsc_ends_witness

}  // namespace fixy::concurrent
