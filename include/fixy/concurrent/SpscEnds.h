#pragma once

// Ends of a single-producer, single-consumer owner
//   An owner of one producer side and one consumer side, such as a ring or a
//   log, keeps the state of each side in an end.  The producer end holds the
//   state of the producer, such as its copy of the head and its cached tail.
//   The consumer end holds the state of the consumer, such as its copy of the
//   tail.  Each end holds the Ref of its owner, so an end opens no other
//   owner.  An end starts with the first state that its owner gives
//   (SpscGivesFirstStates), or with the state that its type gives with no
//   argument.
//
//   The owner is a friend of its two ends.  The side operations are static
//   members of the owner that take the end, and they read the Ref and the
//   state of the end.  No other code reads them, so no code reaches a side of
//   the owner without its end.
//
// Split
//   mint_spsc_split builds the owner in a Box, and a split that holds the Box
//   and the two ends.  It gives the split and one side token for each side:
//     auto [split, producer_token, consumer_token] = mint_spsc_split<Log>(alloc, ...);
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
// Side tokens
//   A side token (SpscSplit::producer_token and SpscSplit::consumer_token) is
//   the authority over one side of one split.  It is a nested class of the
//   split, so it opens only the splits of its type, and the mint of a split
//   gives the one token of each side of the fresh brand of its call.  Only
//   that mint builds a token: its constructor is private, and the door of the
//   mint is its one friend.  A token is linear and does not copy, so one side has
//   one holder.  It moves, so its holder can give it to the body of another
//   thread.  It holds no resource, so the end of its scope is its door.  Two
//   threads never read one token through a reference to const.
//
// Doors
//   A body borrows an end through a frame door of the split, for the time of
//   the call.  The doors are free functions.  Each door takes the token of its
//   side, by move or by a borrow, and it refuses a body that reaches that
//   token.  So no code inside a body opens a second frame of the same side:
//   not a nested door, not a helper, not a closure that the body calls.
//     with_producer(split, token, body)      body gets the producer end
//     with_consumer(split, token, body)      body gets the consumer end
//     with_ends(split, producer_token, consumer_token, body)
//                                            body gets the two ends
//     with_owner(split, body)                body gets the owner as const
//   The move form of a door gives the token back: alone for a body that gives
//   void, and after the result in a Record for each other body.  The result of
//   a body holds no borrow, so no reference to an end leaves its frame.
//
// Two threads
//   A split states synchronized{} of foundation/Transfer.h when two threads
//   can write its owner (Synchronized of the owner).  Then the holder of each
//   token opens its side on its own thread, at the same time as the holder of
//   the other token.  Each end has one writer, the holder of its token, and
//   the owner orders the state that the two sides share.  Each member door of
//   the split takes the token of a side, or is const, and only the frame doors
//   call the member doors.  The split has no friend.  A split of an owner that
//   two threads cannot write states nothing, so it stays on one thread: a door
//   that starts threads refuses two bodies that borrow it.
//
// Held split
//   A class that holds a split as a member, such as the ring of a Vigil, names
//   its brand, and a token of a named brand would open each split of that
//   brand.  So a held brand names a proof for each side in place of a token:
//   its member types producer_proof and consumer_proof (SpscHeldBrand).  A
//   proof is an empty class that no code builds with no argument and that
//   does not copy, such as the proof of the one writer of
//   foundation/SoleWriterProof.h.  The holder makes each proof only on the
//   thread of its side.
//     SpscHeldSplit<Log, HeldBrand> split_ = mint_held_spsc_split<Log, HeldBrand>(alloc, ...);
//     split_.producer_side().with_end(producer_proof, body)
//     split_.consumer_side().with_end(consumer_proof, body)
//   - The mint gives the held split alone, with no token, as a prvalue, so
//     the holder builds its member in place.
//   - A held split keeps each end in a side (SpscHeldSide), and it lends a
//     reference to each side.  A side opens its end only with its proof, and
//     its type names its owner, its side and its proof, and no brand.  So a
//     consumer that drains the splits of many holders, such as a stage of a
//     pipeline, holds a reference to the consumer side of each, and the
//     brands of the holders stay apart.
//   - The door of a side refuses a body that reaches a side of the same
//     proof through a borrow, also through the split that holds it, so no
//     code inside the body opens a second frame of that side.
//   - with_held_ends(first, first_proof, second, second_proof, body) opens
//     two sides of two types in one frame, for a producer that writes two
//     owners in one step, or for the two sides of one split on one thread.
//   - The proofs prove a thread for each side, not one holder of a split.  A
//     proof of type P opens each side of proof P that its code reaches.  So a
//     thread that holds the proof of one holder opens the side of a second
//     holder of one class when it reaches that side.  Each holder keeps its
//     split private, so only its own code reaches the split.
//   - A held split states no transfer fact, because its proofs belong to the
//     holder and not to the split.  The holder states the fact of the class
//     that the threads share, and it names the proofs of its sides there.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Transfer.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The side of an end.
enum class SpscSide : std::uint8_t {
    producer = 0,
    consumer = 1,
};

// The state of one side: it starts with no argument, it moves with no
// failure, and it holds no borrow (the store mark of an end).
template <class State>
concept SpscSideState = std::is_nothrow_default_constructible_v<State> && std::is_nothrow_move_constructible_v<State>
                     && ::foundation::core::HoldsNoBorrow<State>;

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

// An owner that gives the first state of each end (the first states): a
// static member of the owner reads the owner that the mint built, such as a
// log that starts at a position, and gives the state of the end.
template <class Owner>
concept SpscGivesFirstStates = requires(Owner const& owner) {
    { Owner::first_producer_state(owner) } noexcept -> std::same_as<typename Owner::producer_state>;
    { Owner::first_consumer_state(owner) } noexcept -> std::same_as<typename Owner::consumer_state>;
};

// The brand of the Ref that an end of Owner holds.  The Ref of an end then has
// a type of its own, apart from each other Ref of the owner type.
template <class Owner>
struct SpscSplitBrand final {};

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

// The statement of a split (two threads): synchronized{} for a split whose
// owner two threads can write, and no statement for each other split.
struct spsc_no_statement final {};

template <bool IsShared>
inline constexpr auto spsc_split_statement = spsc_no_statement{};

template <>
inline constexpr auto spsc_split_statement<true> = ::foundation::transfer::synchronized{};

template <class Owner, SpscSide Side>
class SpscEnd;

template <class Owner, class Brand>
class SpscSplit;

template <class Owner, class Brand>
struct SpscSplitParts;

template <class Owner, SpscSide Side, class Proof>
class SpscHeldSide;

template <class Owner, class Brand>
class SpscHeldSplit;

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
[[nodiscard]] SpscHeldSplit<Owner, Brand> mint_held_spsc_split(Marker allocation, Args&&... args) noexcept;

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
        -> ::fixy::concurrent::SpscHeldSplit<Owner, Brand>;

    // The owner in the new Box of a mint, as a Ref of the split brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

}  // namespace foundation::core

namespace fixy::concurrent {

template <class Owner>
using ProducerEnd = SpscEnd<Owner, SpscSide::producer>;
template <class Owner>
using ConsumerEnd = SpscEnd<Owner, SpscSide::consumer>;

// The key of the constructor of an end.  Only a split and a held split build
// one, so only they build an end.  The owner is a friend of its ends, and it
// cannot build this key, so it builds no second end of a side.  The seal
// closes the byte routes to a key.
template <class Owner>
class SpscEndKey final {
    template <class, class>
    friend class SpscSplit;
    template <class, class>
    friend class SpscHeldSplit;

    constexpr SpscEndKey() noexcept = default;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    SpscEndKey(SpscEndKey const&) = delete("a key of an end lives for one construction of an end");
    SpscEndKey& operator=(SpscEndKey const&) = delete("a key of an end lives for one construction of an end");
};

// One end of an owner.  Its two parts are private, and the owner reads them.
// The Ref is const, so no code points an end at a second owner.
//
// The store mark of an end
//   The end states owns_its_storage{} of foundation/core/Owner.h.  The mark
//   holds because the end has two parts only: the one Ref, which is const
//   and which only the mint sets, and the state, which holds no borrow
//   (SpscSideState).  So no door and no body can store a borrow of a caller
//   in an end.  The store check of a frame body (StoresNoBorrowOutside of
//   foundation/core/CaptureStore.h) still reads the owner type, the type
//   argument of the end, in the region of the end, so a body that reaches an
//   owner that holds a borrow stays refused.  A body of an inner frame, such
//   as a drain into the run of an array, can thus capture an end, or a split
//   that holds one, by a reference that is not const.
template <class Owner, SpscSide Side>
class [[nodiscard]][[= ::foundation::core::owns_its_storage{}]] SpscEnd final {
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

    // The end of the owner, with its first state (the first states).  The
    // state comes from a prvalue, so it builds in place and does not move.
    constexpr SpscEnd(SpscEndKey<Owner> const& /*key*/, owner_ref owner) noexcept
        : owner_{owner}, state_{first_state_(*owner)} {}

    // The first state of the end: the state that the owner gives, or the
    // state that the type gives with no argument.
    [[nodiscard]] static constexpr state_type first_state_(Owner const& owner) noexcept {
        if constexpr (!SpscGivesFirstStates<Owner>) {
            static_cast<void>(owner);
            return state_type{};
        } else if constexpr (Side == SpscSide::producer) {
            return Owner::first_producer_state(owner);
        } else {
            return Owner::first_consumer_state(owner);
        }
    }

    friend Owner;
    template <class, class>
    friend class SpscSplit;
    template <class, SpscSide, class>
    friend class SpscHeldSide;

    owner_ref const owner_;
    state_type state_{};
};

// True when no value of Body reaches the token of the type Token, by value,
// through a reference, a pointer or a template argument, at each depth (the
// walk of the loan keys of foundation/core/Frame.h).
template <class Body, class Token>
concept ReachesNoSideToken =
    !::foundation::core::detail::reaches_loan_key_(^^std::remove_cvref_t<Body>, std::meta::dealias(^^Token));

// The result of a body of a frame door of a split: void, or a value that holds
// no borrow and fits a Record.
template <class Given>
concept SpscFrameValue =
    std::is_void_v<Given>
    || (::foundation::core::HoldsNoBorrow<Given> && ::foundation::core::RecordElement<std::remove_cv_t<Given>>);

// The body of a frame door of one end: it takes the end with no throw, its
// result holds no borrow and fits a Record, it reaches no token of the side,
// it holds no linear value by value (HoldsNoLinearValue of
// foundation/Linear.h), and it stores no borrow outside the frame
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).  A Ref of the end
// in a capture would let the caller write the side after the frame, beside
// the next holder of the side.
template <class Body, class End, class Token>
concept SpscEndBody = std::is_nothrow_invocable_v<Body&&, End&> && SpscFrameValue<std::invoke_result_t<Body&&, End&>>
                   && ReachesNoSideToken<Body, Token> && ::foundation::linear::HoldsNoLinearValue<Body>
                   && ::foundation::core::StoresNoBorrowOutside<Body>;

// The body of the frame door of the two ends of a split: it takes the two
// ends with no throw, its result holds no borrow, it reaches no token of
// either side, it holds no linear value by value, and it stores no borrow
// outside the frame.
template <class Body, class Producer, class Consumer, class ProducerToken, class ConsumerToken>
concept SpscEndsBody =
    std::is_nothrow_invocable_v<Body&&, Producer&, Consumer&>
    && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Producer&, Consumer&>>
    && ReachesNoSideToken<Body, ProducerToken> && ReachesNoSideToken<Body, ConsumerToken>
    && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>;

// The body of a read of the owner: it takes the owner as const with no throw,
// its result holds no borrow, it holds no linear value by value, and it stores
// no borrow outside the frame.
template <class Body, class Owner>
concept SpscOwnerBody =
    std::is_nothrow_invocable_v<Body&&, Owner const&>
    && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, Owner const&>>
    && ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of a frame door of one end that reaches no token of the side and
// holds a linear value by value: the linear twin of the door binds to it.
template <class Body, class End, class Token>
concept SpscLinearEndBody = (!::foundation::linear::HoldsNoLinearValue<Body>)
                         && std::is_invocable_v<Body&&, End&> && ReachesNoSideToken<Body, Token>;

// A body of a frame door of one end that reaches the token of the side: the
// refused twin of the door binds to it.
template <class Body, class End, class Token>
concept SpscTokenReachingBody = std::is_nothrow_invocable_v<Body&&, End&> && !ReachesNoSideToken<Body, Token>;

// A body of a frame door of one end whose result holds a borrow: the refused
// twin of the door binds to it.
template <class Body, class End>
concept SpscBorrowGivingBody =
    std::is_nothrow_invocable_v<Body&&, End&> && !std::is_void_v<std::invoke_result_t<Body&&, End&>>
    && !::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, End&>>;

// True when the body reaches a side of the proof through a borrow, also through
// the held split that holds the side, or a class whose members the walk cannot
// read: the walk of the borrow of a root door (foundation/core/Frame.h), which
// counts each class with the mark of a keyed owner whose arguments name the
// proof.
template <class Body, class Proof>
concept SpscReachesHeldSide =
    ::foundation::core::detail::reaches_borrowed_key_owner_(^^std::remove_cvref_t<Body>, std::meta::dealias(^^Proof));

// The body of the door of a side of a held split: it takes the end with no
// throw, its result holds no borrow and fits a Record, it reaches no side of
// the proof through a borrow, it holds no linear value by value, and it stores
// no borrow outside the frame.
template <class Body, class End, class Proof>
concept SpscHeldEndBody =
    std::is_nothrow_invocable_v<Body&&, End&> && SpscFrameValue<std::invoke_result_t<Body&&, End&>>
    && !SpscReachesHeldSide<Body, Proof> && ::foundation::linear::HoldsNoLinearValue<Body>
    && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body of the door of a side that reaches a side of the proof through a
// borrow: the refused twin of the door binds to it.
template <class Body, class End, class Proof>
concept SpscHeldSideReachingBody = std::is_nothrow_invocable_v<Body&&, End&> && SpscReachesHeldSide<Body, Proof>;

// The body of the door of two held sides: it takes the two ends with no
// throw, its result holds no borrow and fits a Record, it reaches no side of
// either proof through a borrow, it holds no linear value by value, and it
// stores no borrow outside the frame.
template <class Body, class FirstEnd, class SecondEnd, class FirstProof, class SecondProof>
concept SpscHeldEndsBody =
    std::is_nothrow_invocable_v<Body&&, FirstEnd&, SecondEnd&>
    && SpscFrameValue<std::invoke_result_t<Body&&, FirstEnd&, SecondEnd&>> && !SpscReachesHeldSide<Body, FirstProof>
    && !SpscReachesHeldSide<Body, SecondProof> && ::foundation::linear::HoldsNoLinearValue<Body>
    && ::foundation::core::StoresNoBorrowOutside<Body>;

// Two held sides of two types.  One type names one owner type, one side and
// one proof, so two sides of two types are two objects.
template <class FirstSide, class SecondSide>
concept SpscTwoSideTypes = !std::is_same_v<FirstSide, SecondSide>;

// The door of two held sides (the definition follows the side).
template <class FirstOwner, SpscSide FirstSide, class FirstProof, class SecondOwner, SpscSide SecondSide,
          class SecondProof, class Body>
    requires SpscTwoSideTypes<SpscHeldSide<FirstOwner, FirstSide, FirstProof>,
                              SpscHeldSide<SecondOwner, SecondSide, SecondProof>>
          && SpscHeldEndsBody<Body, SpscEnd<FirstOwner, FirstSide>, SpscEnd<SecondOwner, SecondSide>, FirstProof,
                              SecondProof>
constexpr auto with_held_ends(SpscHeldSide<FirstOwner, FirstSide, FirstProof>& first, FirstProof const& first_proof,
                              SpscHeldSide<SecondOwner, SecondSide, SecondProof>& second,
                              SecondProof const& second_proof, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, SpscEnd<FirstOwner, FirstSide>&, SpscEnd<SecondOwner, SecondSide>&>;

// The key of the build of a split.  Only the door of the mint builds one, so
// only mint_spsc_split builds a split.  The door is no friend of the split:
// the split states synchronized{} for an owner that two threads write, and
// such a class has no friend (the doors of a synchronized class of
// foundation/Transfer.h).
template <class Owner, class Brand>
class SpscBuildKey final {
    friend struct detail::SpscSplitDoor<Owner, Brand>;

    constexpr SpscBuildKey() noexcept = default;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    SpscBuildKey(SpscBuildKey const&) = delete("a key of a split lives for one build of a split");
    SpscBuildKey& operator=(SpscBuildKey const&) = delete("a key of a split lives for one build of a split");
};

// The key of the member doors of a split, which give a reference to an end or
// to the owner.  Only the frame doors build one (the definitions below), so
// only a frame door lends an end, and only to its body.
class SpscFrameKey;

// The split of an owner: the owner in a Box and its two ends.  The Box is the
// first member, so the two ends end before it.  The split does not move, so
// the Box stays in it.  A mint of a fresh brand gives the split and the two
// tokens of its sides (side tokens).
template <class Owner, class Brand>
class [[nodiscard]][[= ::foundation::core::loan_key_mark{}]]
                   [[= spsc_split_statement<::foundation::transfer::Synchronized<Owner>>]] SpscSplit final {
    using producer_end = ProducerEnd<Owner>;
    using consumer_end = ConsumerEnd<Owner>;
    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

public:
    using owner_type = Owner;
    using brand_type = Brand;

    // The token of the producer side of the splits of this type (side
    // tokens).  The constructor is private, and the door of the mint is its
    // one friend.  The token states no shared write, so a friend of it is no
    // friend of the split.
    class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                       [[= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::not_shareable{}]]
                       [[= ::foundation::core::loan_key_mark{}]] producer_token final {
        friend struct detail::SpscSplitDoor<Owner, Brand>;

        constexpr producer_token() noexcept {}

        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        producer_token(producer_token const&) = delete("a side token is the authority over one side: a copy would "
                                                       "give a second holder of the side");
        producer_token& operator=(producer_token const&) = delete("a side token is the authority over one side");
        constexpr producer_token(producer_token&&) noexcept = default;
        constexpr producer_token& operator=(producer_token&&) noexcept = default;
        ~producer_token() = default;
    };

    // The token of the consumer side, in the form of the producer token.
    class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                       [[= ::foundation::linear::scope_end_door{}]][[= ::foundation::transfer::not_shareable{}]]
                       [[= ::foundation::core::loan_key_mark{}]] consumer_token final {
        friend struct detail::SpscSplitDoor<Owner, Brand>;

        constexpr consumer_token() noexcept {}

        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        consumer_token(consumer_token const&) = delete("a side token is the authority over one side: a copy would "
                                                       "give a second holder of the side");
        consumer_token& operator=(consumer_token const&) = delete("a side token is the authority over one side");
        constexpr consumer_token(consumer_token&&) noexcept = default;
        constexpr consumer_token& operator=(consumer_token&&) noexcept = default;
        ~consumer_token() = default;
    };

    SpscSplit(SpscSplit const&) = delete("a split holds the one end of each side of its owner: a copy would give a "
                                         "second end of each side");
    SpscSplit& operator=(SpscSplit const&) = delete("a split holds one owner and its two ends for their life");
    SpscSplit(SpscSplit&&) = delete("the ends of a split live beside the Box of their owner, so a split does not "
                                    "move, and no code moves its Box out while an end lives");
    SpscSplit& operator=(SpscSplit&&) = delete("a split holds one owner and its two ends for their life");
    ~SpscSplit() = default;

    // The split of the owner in the Box, for the door of the mint, which
    // alone holds the key.  The Ref names the object in the Box, and the mint
    // makes it.  The split comes back as a prvalue, so it does not move.
    [[nodiscard]] static SpscSplit build_(SpscBuildKey<Owner, Brand> const& /*key*/,
                                          ::foundation::core::Box<Owner>&& owner, owner_ref ref) noexcept {
        return SpscSplit{std::move(owner), ref};
    }

    // The producer end, for a frame door, which holds the key of the frame
    // doors and the producer token.  Complexity: O(1).
    [[nodiscard]] constexpr producer_end& producer_end_(SpscFrameKey const& /*key*/,
                                                        producer_token& /*token*/) & noexcept {
        return producer_;
    }

    // The consumer end, for a frame door, in the form of the producer end.
    [[nodiscard]] constexpr consumer_end& consumer_end_(SpscFrameKey const& /*key*/,
                                                        consumer_token& /*token*/) & noexcept {
        return consumer_;
    }

    // The owner as const, for the read door, through the Ref of the producer
    // end.  A read opens no side, so it takes no token.  Complexity: O(1).
    [[nodiscard]] constexpr Owner const& owner_of_(SpscFrameKey const& /*key*/) const& noexcept {
        return *producer_.owner_;
    }

private:
    SpscSplit(::foundation::core::Box<Owner>&& owner, owner_ref ref) noexcept
        : owner_{std::move(owner)}, producer_{SpscEndKey<Owner>{}, ref}, consumer_{SpscEndKey<Owner>{}, ref} {}

    ::foundation::core::Box<Owner> owner_;
    producer_end producer_;
    consumer_end consumer_;
};

// The token of each side of the splits of a type.
template <class Owner, class Brand>
using ProducerToken = typename SpscSplit<Owner, Brand>::producer_token;
template <class Owner, class Brand>
using ConsumerToken = typename SpscSplit<Owner, Brand>::consumer_token;

// The result of mint_spsc_split: the split, and the token of each side.  A
// binding takes the three parts by name, in place, so the split does not
// move.  The split cannot move, so neither can this holder, and the end of
// its scope is the door of each part: the split ends its owner and its two
// ends, and a token holds no resource.
template <class Owner, class Brand>
struct [[nodiscard]][[= ::foundation::linear::scope_end_door{}]] SpscSplitParts final {
    SpscSplit<Owner, Brand> split;
    ProducerToken<Owner, Brand> producer_token;
    ConsumerToken<Owner, Brand> consumer_token;
};

// The frame doors of a split (the definitions follow the key).

// Producer door: body gets the producer end in place.  The token moves into
// the door, and the door gives it back: alone for a body that gives void, and
// after the result in a Record for each other body.  Complexity: O(1), and
// the cost of body.
template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
[[nodiscard]] constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token,
                                           Body&& body) noexcept
    -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ProducerEnd<Owner>&>,
                                                  ProducerToken<Owner, Brand>>;

// Producer door, the borrow form: the token stays with the caller, and the
// door gives the result of body.
template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&>;

// Consumer door, in the two forms of the producer door.
template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
[[nodiscard]] constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token,
                                           Body&& body) noexcept
    -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>,
                                                  ConsumerToken<Owner, Brand>>;

template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>;

// Door of the two ends: body gets the producer end and the consumer end, and
// the door borrows the two tokens for the call.  The two ends are two
// members, so the two borrows do not overlap.  Complexity: O(1), and the cost
// of body.
template <class Owner, class Brand, class Body>
    requires SpscEndsBody<Body, ProducerEnd<Owner>, ConsumerEnd<Owner>, ProducerToken<Owner, Brand>,
                          ConsumerToken<Owner, Brand>>
constexpr auto with_ends(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& producer_token,
                         ConsumerToken<Owner, Brand>& consumer_token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&, ConsumerEnd<Owner>&>;

// Read of the owner: body gets the owner as const, and the door gives the
// result of body.  A read opens no side, so the door takes no token.
// Complexity: O(1), and the cost of body.
template <class Owner, class Brand, class Body>
    requires SpscOwnerBody<Body, Owner>
constexpr auto with_owner(SpscSplit<Owner, Brand> const& split, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, Owner const&>;

// The key of the member doors of a split (the declaration above).  Its
// constructor is private, and its friends are the frame doors, so no other
// code builds one.  The seal closes the byte routes to a key.
class SpscFrameKey final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr SpscFrameKey() noexcept {}

    template <class Owner, class Brand, class Body>
        requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
    friend constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token,
                                        Body&& body) noexcept
        -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ProducerEnd<Owner>&>,
                                                      ProducerToken<Owner, Brand>>;
    template <class Owner, class Brand, class Body>
        requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
    friend constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token,
                                        Body&& body) noexcept -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&>;
    template <class Owner, class Brand, class Body>
        requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
    friend constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token,
                                        Body&& body) noexcept
        -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>,
                                                      ConsumerToken<Owner, Brand>>;
    template <class Owner, class Brand, class Body>
        requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
    friend constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token,
                                        Body&& body) noexcept -> std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>;
    template <class Owner, class Brand, class Body>
        requires SpscEndsBody<Body, ProducerEnd<Owner>, ConsumerEnd<Owner>, ProducerToken<Owner, Brand>,
                              ConsumerToken<Owner, Brand>>
    friend constexpr auto with_ends(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& producer_token,
                                    ConsumerToken<Owner, Brand>& consumer_token, Body&& body) noexcept
        -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&, ConsumerEnd<Owner>&>;
    template <class Owner, class Brand, class Body>
        requires SpscOwnerBody<Body, Owner>
    friend constexpr auto with_owner(SpscSplit<Owner, Brand> const& split, Body&& body) noexcept
        -> std::invoke_result_t<Body&&, Owner const&>;

public:
    SpscFrameKey(SpscFrameKey const&) = delete("a key of the frame doors lives in the frame of one door call");
    SpscFrameKey& operator=(SpscFrameKey const&) = delete("a key of the frame doors does not change");
    ~SpscFrameKey() = default;
};

template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token, Body&& body) noexcept
    -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ProducerEnd<Owner>&>,
                                                  ProducerToken<Owner, Brand>> {
    using Given = std::invoke_result_t<Body&&, ProducerEnd<Owner>&>;
    SpscFrameKey const key{};
    ProducerEnd<Owner>& end = split.producer_end_(key, token);
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(end);
        return std::move(token);
    } else {
        return ::foundation::core::detail::frame_return_t<Given, ProducerToken<Owner, Brand>>{
            static_cast<Body&&>(body)(end), std::move(token)};
    }
}

template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
constexpr auto with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&> {
    SpscFrameKey const key{};
    return static_cast<Body&&>(body)(split.producer_end_(key, token));
}

template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token, Body&& body) noexcept
    -> ::foundation::core::detail::frame_return_t<std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>,
                                                  ConsumerToken<Owner, Brand>> {
    using Given = std::invoke_result_t<Body&&, ConsumerEnd<Owner>&>;
    SpscFrameKey const key{};
    ConsumerEnd<Owner>& end = split.consumer_end_(key, token);
    if constexpr (std::is_void_v<Given>) {
        static_cast<Body&&>(body)(end);
        return std::move(token);
    } else {
        return ::foundation::core::detail::frame_return_t<Given, ConsumerToken<Owner, Brand>>{
            static_cast<Body&&>(body)(end), std::move(token)};
    }
}

template <class Owner, class Brand, class Body>
    requires SpscEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
constexpr auto with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ConsumerEnd<Owner>&> {
    SpscFrameKey const key{};
    return static_cast<Body&&>(body)(split.consumer_end_(key, token));
}

template <class Owner, class Brand, class Body>
    requires SpscEndsBody<Body, ProducerEnd<Owner>, ConsumerEnd<Owner>, ProducerToken<Owner, Brand>,
                          ConsumerToken<Owner, Brand>>
constexpr auto with_ends(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& producer_token,
                         ConsumerToken<Owner, Brand>& consumer_token, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, ProducerEnd<Owner>&, ConsumerEnd<Owner>&> {
    SpscFrameKey const key{};
    return static_cast<Body&&>(body)(split.producer_end_(key, producer_token),
                                     split.consumer_end_(key, consumer_token));
}

template <class Owner, class Brand, class Body>
    requires SpscOwnerBody<Body, Owner>
constexpr auto with_owner(SpscSplit<Owner, Brand> const& split, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, Owner const&> {
    SpscFrameKey const key{};
    return static_cast<Body&&>(body)(split.owner_of_(key));
}

// A body that holds a linear value by value binds to these overloads, so the
// refusal gives its reason.
template <class Owner, class Brand, class Body>
    requires SpscLinearEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token,
                   Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class Owner, class Brand, class Body>
    requires SpscLinearEndBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token,
                   Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class Owner, class Brand, class Body>
    requires SpscLinearEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token,
                   Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class Owner, class Brand, class Body>
    requires SpscLinearEndBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token,
                   Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class Owner, class Brand, class Body>
    requires std::is_invocable_v<Body&&, ProducerEnd<Owner>&, ConsumerEnd<Owner>&>
              && (!::foundation::linear::HoldsNoLinearValue<Body>)
void with_ends(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& producer_token,
               ConsumerToken<Owner, Brand>& consumer_token, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
template <class Owner, class Brand, class Body>
    requires std::is_invocable_v<Body&&, Owner const&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
void with_owner(SpscSplit<Owner, Brand> const& split, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A door with no token binds to these overloads, so the refusal gives its
// reason.
template <class Owner, class Brand, class Body>
    requires std::is_invocable_v<Body&&, ProducerEnd<Owner>&>
void with_producer(SpscSplit<Owner, Brand>& split, Body&& body) =
    delete("a split lends its producer end only with the producer token of its mint: call "
           "with_producer(split, token, body)");
template <class Owner, class Brand, class Body>
    requires std::is_invocable_v<Body&&, ConsumerEnd<Owner>&>
void with_consumer(SpscSplit<Owner, Brand>& split, Body&& body) =
    delete("a split lends its consumer end only with the consumer token of its mint: call "
           "with_consumer(split, token, body)");

// A body that reaches the token of its side binds to these overloads, so the
// refusal gives its reason.
template <class Owner, class Brand, class Body>
    requires SpscTokenReachingBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token, Body&& body) = delete(
    "a body of a frame door of a split reaches the token of its side, so code in the body could open a second frame "
    "of that side: capture only what the body reads, and keep the token with the caller");
template <class Owner, class Brand, class Body>
    requires SpscTokenReachingBody<Body, ProducerEnd<Owner>, ProducerToken<Owner, Brand>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token, Body&& body) = delete(
    "a body of a frame door of a split reaches the token of its side, so code in the body could open a second frame "
    "of that side: capture only what the body reads, and keep the token with the caller");
template <class Owner, class Brand, class Body>
    requires SpscTokenReachingBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token, Body&& body) = delete(
    "a body of a frame door of a split reaches the token of its side, so code in the body could open a second frame "
    "of that side: capture only what the body reads, and keep the token with the caller");
template <class Owner, class Brand, class Body>
    requires SpscTokenReachingBody<Body, ConsumerEnd<Owner>, ConsumerToken<Owner, Brand>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token, Body&& body) = delete(
    "a body of a frame door of a split reaches the token of its side, so code in the body could open a second frame "
    "of that side: capture only what the body reads, and keep the token with the caller");

// A body whose result holds a borrow binds to these overloads, so the refusal
// gives its reason.
template <class Owner, class Brand, class Body>
    requires SpscBorrowGivingBody<Body, ProducerEnd<Owner>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>&& token, Body&& body) = delete(
    "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a plain "
    "value");
template <class Owner, class Brand, class Body>
    requires SpscBorrowGivingBody<Body, ProducerEnd<Owner>>
void with_producer(SpscSplit<Owner, Brand>& split, ProducerToken<Owner, Brand>& token, Body&& body) = delete(
    "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a plain "
    "value");
template <class Owner, class Brand, class Body>
    requires SpscBorrowGivingBody<Body, ConsumerEnd<Owner>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>&& token, Body&& body) = delete(
    "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a plain "
    "value");
template <class Owner, class Brand, class Body>
    requires SpscBorrowGivingBody<Body, ConsumerEnd<Owner>>
void with_consumer(SpscSplit<Owner, Brand>& split, ConsumerToken<Owner, Brand>& token, Body&& body) = delete(
    "the result of a frame door of a split leaves the frame, so it holds no reference and no address: give a plain "
    "value");

// One side of a held split (the held split): the end of the side, which opens
// only with the proof of the side.  The held split builds each side in place,
// and it owns the side for its life.  A holder that lends a reference to a
// side to another object, such as the consumer side of a ring to the stage of
// a pipeline that drains it, makes that object end first: the holder declares
// the object after the split, so the object ends before the split, and the
// object joins its thread before its own end.  The type names the owner, the
// side and the proof, and no brand, so one consumer takes the sides of many
// holders.
//
// The proof is not tied to one holder.  A proof of type Proof opens each side
// of that proof that its code reaches.  So a thread that holds the proof of
// one holder opens the side of a second holder of one class when its code
// reaches that side, and the second side then has two writers.  The check
// file of this header holds that route as a case that compiles (the residual
// of a held side).  Each holder keeps its split private, so only its own code
// reaches its sides.
template <class Owner, SpscSide Side, class Proof>
class [[nodiscard]][[= ::foundation::core::loan_key_mark{}]] SpscHeldSide final {
    using end_type = SpscEnd<Owner, Side>;
    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

public:
    using owner_type = Owner;
    using proof_type = Proof;
    static constexpr SpscSide side = Side;

    SpscHeldSide(SpscHeldSide const&) = delete("a side holds the one end of its side: a copy would give a second "
                                               "writer of that side");
    SpscHeldSide& operator=(SpscHeldSide const&) = delete("a side names one end of one owner for its life");
    SpscHeldSide(SpscHeldSide&&) = delete("a side lives in its held split beside its owner, so a move would let the "
                                          "end outlive the owner");
    SpscHeldSide& operator=(SpscHeldSide&&) = delete("a side names one end of one owner for its life");
    ~SpscHeldSide() = default;

    // Door of the side: body gets the end in place, for the caller that gives
    // the proof of the side.  The door reads no byte of the proof.  The body
    // reaches no side of the proof through a borrow, so no code inside it
    // opens a second frame of the side.  Complexity: O(1), and the cost of
    // body.
    template <class Body>
        requires SpscHeldEndBody<Body, end_type, Proof>
    constexpr auto with_end(Proof const& /*proof*/, Body&& body) & noexcept {
        return static_cast<Body&&>(body)(end_);
    }

    // Read of the owner: body gets the owner as const, through the Ref of the
    // end.  A read opens no side, so the door takes no proof.  Complexity:
    // O(1), and the cost of body.
    template <class Body>
        requires SpscOwnerBody<Body, Owner>
    constexpr auto with_owner(Body&& body) const& noexcept {
        Owner const& owner = *end_.owner_;
        return static_cast<Body&&>(body)(owner);
    }

    // View of the owner for the consumer: a Ref to the owner as const, which
    // lives as long as the held split.  The holder declares the split before
    // the object that keeps the view, and that object joins its thread
    // before its own end, as for a reference to a side.  Only the consumer
    // side has the door, and the producer side has none.
    //
    // The view writes nothing, because each door of the owner that writes
    // takes an end.  Its risk is a read of a part that the producer still
    // writes.  So each read through the view stays at or below the position
    // that the consumer acquired, and the consumer gives the range of each
    // read from that position.  The users of the view are these:
    //   - The build stage of the background thread (crucible/
    //     BackgroundThread.h).  It calls one const door of the metadata log
    //     through the view, MetaLog::copy_run, for the records that the meta
    //     starts of its drained ops name.  The record path appends the records
    //     of an op and publishes the head of the log before it publishes the
    //     op in the trace ring, and the drain stage acquired the head of the
    //     ring, so each such record is published.  The publish stage releases
    //     a record only after the build of its work.
    //   - The consumer handle of crucible/PermissionedMetaLog.h, and the
    //     consumer of a test rig.  A record holds the data pointer of its
    //     tensor, and no frame gives such a value out, so each reads a record
    //     through the view.  Each reads only records between the tail that
    //     only it moves and the head that it acquired.
    // The constructor, the first start and the flush of the background
    // thread read the counters of the ring and of the log through with_owner
    // frames, and not through a view.
    //
    // The door ends for the build stage when a built region holds its arena
    // in a form that the store check of with_owner admits: the build stage
    // then reads the log inside a with_owner frame again.  Complexity: O(1).
    [[nodiscard]] constexpr ::foundation::core::Ref<Owner const, SpscSplitBrand<Owner>> owner_view() const& noexcept
        requires(Side == SpscSide::consumer)
    {
        return end_.owner_;
    }
    ::foundation::core::Ref<Owner const, SpscSplitBrand<Owner>> owner_view() const&&
        requires(Side == SpscSide::consumer)
    = delete("a temporary side gives no view of its owner: take the view from the side that the held split holds");
    ::foundation::core::Ref<Owner const, SpscSplitBrand<Owner>> owner_view() const&
        requires(Side == SpscSide::producer)
    = delete("the producer side gives no view of its owner: the producer writes the owner, so a read outside a frame "
             "could read a part that the producer still writes");

    // A door with no proof binds to this overload, so the refusal gives its
    // reason.
    template <class Body>
        requires std::is_invocable_v<Body&&, end_type&>
    void with_end(Body&& body) & = delete("a side of a held split lends its end only with the proof of the side: "
                                          "call with_end(proof, body)");

    // A body that reaches a side of the proof, whose result holds a borrow, or
    // that holds a linear value by value binds to these overloads, so the
    // refusal gives its reason.
    template <class Body>
        requires SpscHeldSideReachingBody<Body, end_type, Proof>
    void with_end(Proof const& proof, Body&& body) & = delete(
        "a body of the door of a side of a held split reaches a side of its proof through a borrow, so code in the "
        "body could open a second frame of that side: capture only what the body reads");
    template <class Body>
        requires SpscBorrowGivingBody<Body, end_type>
    void with_end(Proof const& proof, Body&& body) & = delete(
        "the result of the door of a side leaves the frame, so it holds no reference and no address: give a plain "
        "value");
    template <class Body>
        requires std::is_invocable_v<Body&&, end_type&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_end(Proof const& proof, Body&& body) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);
    template <class Body>
        requires std::is_invocable_v<Body&&, Owner const&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_owner(Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

private:
    template <class, class>
    friend class SpscHeldSplit;

    template <class FirstOwner, SpscSide FirstSide, class FirstProof, class SecondOwner, SpscSide SecondSide,
              class SecondProof, class Body>
        requires SpscTwoSideTypes<SpscHeldSide<FirstOwner, FirstSide, FirstProof>,
                                  SpscHeldSide<SecondOwner, SecondSide, SecondProof>>
              && SpscHeldEndsBody<Body, SpscEnd<FirstOwner, FirstSide>, SpscEnd<SecondOwner, SecondSide>, FirstProof,
                                  SecondProof>
    friend constexpr auto with_held_ends(SpscHeldSide<FirstOwner, FirstSide, FirstProof>& first,
                                         FirstProof const& first_proof,
                                         SpscHeldSide<SecondOwner, SecondSide, SecondProof>& second,
                                         SecondProof const& second_proof, Body&& body) noexcept
        -> std::invoke_result_t<Body&&, SpscEnd<FirstOwner, FirstSide>&, SpscEnd<SecondOwner, SecondSide>&>;

    constexpr SpscHeldSide(SpscEndKey<Owner> const& key, owner_ref ref) noexcept : end_{key, ref} {}

    end_type end_;
};

// Door of two held sides: body gets the end of each side in place, for the
// caller that gives the proof of each side.  The door reads no byte of a
// proof.  A producer that writes two owners in one step, such as the trace
// ring and its metadata log, opens the two sides in one frame, because a
// body of the door of one side cannot reach the other side: the store check
// refuses a capture of a side by a reference that is not const.  The two
// sides of one held split open together too, as the two ends of one owner
// on one thread.
//
// The two sides have two types.  One side type given two times could name one
// side two times, and the body would then hold two writers of one end, so
// the deleted twin below refuses it.  The body reaches no side of either
// proof through a borrow, so no code inside it opens a second frame of a
// side.  A proof is not tied to one holder, as for the door of one side: the
// proof of one holder opens the sides of a second holder of one class that
// its code reaches.  Complexity: O(1), and the cost of body.
template <class FirstOwner, SpscSide FirstSide, class FirstProof, class SecondOwner, SpscSide SecondSide,
          class SecondProof, class Body>
    requires SpscTwoSideTypes<SpscHeldSide<FirstOwner, FirstSide, FirstProof>,
                              SpscHeldSide<SecondOwner, SecondSide, SecondProof>>
          && SpscHeldEndsBody<Body, SpscEnd<FirstOwner, FirstSide>, SpscEnd<SecondOwner, SecondSide>, FirstProof,
                              SecondProof>
constexpr auto with_held_ends(SpscHeldSide<FirstOwner, FirstSide, FirstProof>& first, FirstProof const& /*first_proof*/,
                              SpscHeldSide<SecondOwner, SecondSide, SecondProof>& second,
                              SecondProof const& /*second_proof*/, Body&& body) noexcept
    -> std::invoke_result_t<Body&&, SpscEnd<FirstOwner, FirstSide>&, SpscEnd<SecondOwner, SecondSide>&> {
    return static_cast<Body&&>(body)(first.end_, second.end_);
}

// One side type given two times binds to this overload, so the refusal gives
// its reason.
template <class Owner, SpscSide Side, class Proof, class Body>
void with_held_ends(SpscHeldSide<Owner, Side, Proof>& first, Proof const& first_proof,
                    SpscHeldSide<Owner, Side, Proof>& second, Proof const& second_proof, Body&& body) =
    delete("the door of two held sides takes two sides of one type, so the two can be one side, and the body would "
           "hold two writers of one end: give two sides of two types");

// The held split of an owner (the held split): the owner in a Box and its two
// sides.  The Box is the first member, so the two sides end before it.  The
// held split does not move, so the Box stays in it.  The class that holds it
// owns it, and it lends a reference to each side.
template <class Owner, class Brand>
class [[nodiscard]] SpscHeldSplit final {
    using owner_ref = ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>>;

public:
    using owner_type = Owner;
    using brand_type = Brand;
    using producer_side_type = SpscHeldSide<Owner, SpscSide::producer, typename Brand::producer_proof>;
    using consumer_side_type = SpscHeldSide<Owner, SpscSide::consumer, typename Brand::consumer_proof>;

    SpscHeldSplit(SpscHeldSplit const&) = delete("a held split holds the one end of each side of its owner: a copy "
                                                 "would give a second end of each side");
    SpscHeldSplit& operator=(SpscHeldSplit const&) = delete("a held split holds one owner and its two sides for their "
                                                            "life");
    SpscHeldSplit(SpscHeldSplit&&) = delete("the sides of a held split live beside the Box of their owner, so a held "
                                            "split does not move");
    SpscHeldSplit& operator=(SpscHeldSplit&&) = delete("a held split holds one owner and its two sides for their "
                                                       "life");
    ~SpscHeldSplit() = default;

    // The producer side and the consumer side.  A side opens its end only with
    // its proof, so a reference to a side opens nothing.  A temporary held
    // split ends at the end of its statement, so it lends no side.
    [[nodiscard]] constexpr producer_side_type& producer_side() & noexcept { return producer_; }
    producer_side_type& producer_side() && = delete("a temporary held split ends at the end of the statement, and "
                                                    "its side with it: use a held split that has a name");
    [[nodiscard]] constexpr consumer_side_type& consumer_side() & noexcept { return consumer_; }
    consumer_side_type& consumer_side() && = delete("a temporary held split ends at the end of the statement, and "
                                                    "its side with it: use a held split that has a name");

    // Read of the owner, through the producer side.  Complexity: O(1), and the
    // cost of body.
    template <class Body>
        requires SpscOwnerBody<Body, Owner>
    constexpr auto with_owner(Body&& body) const& noexcept {
        return producer_.with_owner(static_cast<Body&&>(body));
    }
    template <class Body>
        requires std::is_invocable_v<Body&&, Owner const&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_owner(Body&& body) const& = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

private:
    friend struct detail::SpscSplitDoor<Owner, Brand>;

    // The held split of the owner in the Box.  The Ref names the object in the
    // Box, and the mint makes it.
    SpscHeldSplit(::foundation::core::Box<Owner>&& owner, owner_ref ref) noexcept
        : owner_{std::move(owner)}, producer_{SpscEndKey<Owner>{}, ref}, consumer_{SpscEndKey<Owner>{}, ref} {}

    ::foundation::core::Box<Owner> owner_;
    producer_side_type producer_;
    consumer_side_type consumer_;
};

namespace detail {

// The one door that builds a split and its tokens, for mint_spsc_split, and a
// held split, for mint_held_spsc_split.  It alone builds the key of a build.
// The door is in namespace detail, and a use of that namespace outside the
// base is a finding.
template <class Owner, class Brand>
struct SpscSplitDoor final {
    [[nodiscard]] static SpscSplitParts<Owner, Brand>
    build(::foundation::core::Box<Owner>&& owner, ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>> ref) noexcept {
        SpscBuildKey<Owner, Brand> const key{};
        return {SpscSplit<Owner, Brand>::build_(key, std::move(owner), ref), ProducerToken<Owner, Brand>{},
                ConsumerToken<Owner, Brand>{}};
    }

    // The held split of a held brand, with no token (the held split).
    [[nodiscard]] static SpscHeldSplit<Owner, Brand>
    build_held(::foundation::core::Box<Owner>&& owner,
               ::foundation::core::Ref<Owner, SpscSplitBrand<Owner>> ref) noexcept
        requires SpscHeldBrand<Brand>
    {
        return SpscHeldSplit<Owner, Brand>{std::move(owner), ref};
    }
};

}  // namespace detail

// Builds the owner in a Box of the allocation capability, the split that
// holds it with its two ends, and the token of each side.  Each end starts
// with its first state (the first states).  The brand is the fresh brand of
// the call site, in undeducible_brand_t, so no caller names it.  The split
// comes back as a prvalue part of the Record, so it does not move.
template <class Owner, class Marker, class... Args, ::foundation::brand::IsBrand Fresh>
    requires CanMintSpscSplit<Owner, Marker, Args...>
[[nodiscard]] SpscSplitParts<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>
mint_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::SpscSplitRefDoor::lend_<SpscSplitBrand<Owner>>(*owner);
    return detail::SpscSplitDoor<Owner, ::foundation::brand::undeducible_brand_t<Fresh>>::build(std::move(owner), ref);
}

// Builds the owner in a Box of the allocation capability and the held split of
// the held brand that holds it with its two sides (the held split).  It gives
// no token.  Each end starts with its first state.  The held split comes back
// as a prvalue, so a holder builds its member in place:
//   SpscHeldSplit<Log, HeldBrand> split_ = mint_held_spsc_split<Log, HeldBrand>(alloc);
template <class Owner, class Brand, class Marker, class... Args>
    requires CanMintHeldSpscSplit<Owner, Brand, Marker, Args...>
[[nodiscard]] SpscHeldSplit<Owner, Brand> mint_held_spsc_split(Marker allocation, Args&&... args) noexcept {
    ::foundation::core::Box<Owner> owner =
        ::foundation::core::mint_box<Owner>(allocation, static_cast<Args&&>(args)...);
    auto const ref = ::foundation::core::SpscSplitRefDoor::lend_<SpscSplitBrand<Owner>>(*owner);
    return detail::SpscSplitDoor<Owner, Brand>::build_held(std::move(owner), ref);
}

namespace spsc_ends_witness {

// An owner with two empty states, a brand, and a held brand with its two
// proofs, which the witness rows of the classes of this header in
// utils/rule-table.txt name.  No code builds one.  They are outside a detail
// namespace, so the witness fixtures of the rows use no detail name.
struct WitnessOwner final {
    struct producer_state {};
    struct consumer_state {};
    WitnessOwner() noexcept = default;
    WitnessOwner(WitnessOwner const&) = delete("the witness owner is pinned, as each owner of a split is");
    WitnessOwner(WitnessOwner&&) = delete("the witness owner is pinned, as each owner of a split is");
};
struct WitnessBrand final {};
// The destructor of each witness proof is user-provided, so the proof is not
// trivially copyable and not an implicit-lifetime type, and no byte route
// builds one.
struct WitnessProducerProof final {
    WitnessProducerProof() = delete("no code builds a witness proof");
    WitnessProducerProof(WitnessProducerProof const&) = delete("no code builds a witness proof");
    ~WitnessProducerProof() {}
};
struct WitnessConsumerProof final {
    WitnessConsumerProof() = delete("no code builds a witness proof");
    WitnessConsumerProof(WitnessConsumerProof const&) = delete("no code builds a witness proof");
    ~WitnessConsumerProof() {}
};
struct WitnessHeldBrand final {
    using producer_proof = WitnessProducerProof;
    using consumer_proof = WitnessConsumerProof;
};

}  // namespace spsc_ends_witness

}  // namespace fixy::concurrent
