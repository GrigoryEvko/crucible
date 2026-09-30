#pragma once

// Delegation: a message that carries a live endpoint of a different
// session.
//
// A sender hands an endpoint of a session to a peer as the payload of a
// message.  This is the throw and catch of higher-order session types
// (Honda, Vasconcelos and Kubo, ESOP 1998).  mint_delegated_session moves
// the live handle of the endpoint into a DelegatedSession, together with
// the PermHold that backs the permission set of the handle.  The sender
// then holds a moved-from handle and no token of the endpoint.  The
// session stays live, and its record in fixy/session/Watch.h moves with
// the handle.  The recipient calls accept() and gets the handle at the
// same protocol position, and the hold with it.
//
// The rules:
//
//   1. Only mint_delegated_session builds a DelegatedSession.  Its
//      constructor takes a DelegationKey, which only the delegation door
//      makes, and the type is not trivially copyable and not an
//      implicit-lifetime type.  So no code builds one from bytes or from a
//      Resource that no handle owned.
//   2. A DelegatedSession is linear.  It moves and never copies.  A
//      DelegatedSession that is destroyed or assigned over before
//      accept() drops the handle it holds, so the abandonment policy of
//      the handle acts: it aborts, it sends the cancellation, or under
//      check::Off it does nothing.
//   3. Only a handle outside every Loop delegates.  Inside a Loop the
//      position does not state the rest of the protocol, because a
//      Continue refers to the loop context of the handle.  A handle that
//      a callback entry or a forked channel owns carries the brand of its
//      body in that context, and the body must return it, so it cannot
//      delegate either.
//   4. The tags of the permission set of the endpoint move from the set of
//      the sender to the set of the recipient, as the payload walk of
//      fixy/session/Payload.h states for a hand-off.  The tokens of those
//      tags travel in the DelegatedSession, so the recipient holds the
//      tokens that its handle claims, and the sender holds none of them.
//   5. The payload order is contravariant in the protocol of the endpoint
//      (the axiom delegation_contravariant of fixy/session/Subtype.h).
//   6. A protocol that sends an endpoint of a session to a peer of that
//      session is refused at every mint (DelegatesToNoOwnPeer in
//      fixy/session/Payload.h).
//
// Crash sessions and checkpoint sessions refuse a payload that delegates
// (payload_conveys_delegation_v), because their theories have no
// delegation.
//
// ── The delegation heads ─────────────────────────────────────────────
//
// fixy/session/Protocol.h defines Delegate<T, K> and Accept<T, K>, two
// heads that state a hand-off in the protocol type.  They stand beside
// the other combinators, because the payload walk of
// fixy/session/Handle.h reads the registry when that header is read.  A
// head that this header registered would be a head that the walk cannot
// classify.
//
// No mint admits a protocol that holds one of the two heads.  The
// permission-flow walk of fixy/session/Handle.h refuses a head that it
// does not know, and no handle has a step for them.  A hand-off that
// runs is a Send of a DelegatedSession.  The queries below read the
// hand-off that a protocol type states.

#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>
#include <fixy/session/Subtype.h>
#include <foundation/NoObject.h>
#include <foundation/contracts/Armed.h>

#include <cstdio>
#include <cstdlib>
#include <meta>
#include <optional>
#include <type_traits>
#include <utility>

namespace fixy::session {

namespace detail {

// A DelegatedSession that already gave its endpoint away holds none.  An
// accept() on it would build a second handle over a Resource that moved.
[[noreturn, gnu::cold, gnu::noinline]] inline void report_delegation_taken() noexcept {
    std::fputs("fixy::session: ACCEPT OF AN EMPTY DELEGATION\n"
               "The DelegatedSession was moved from or accepted, and it holds no endpoint.  Accept the value\n"
               "that the move or the receive returned.\n",
               stderr);
    std::abort();
}

}  // namespace detail

class DelegationDoor;

// The key of the DelegatedSession constructor.  Only the delegation door
// makes one, and the door states the gate of the mint first.  The key has
// the shape that the section on passkeys of fixy/session/Handle.h names.
class DelegationKey final {
    constexpr DelegationKey() noexcept {}
    friend class DelegationDoor;

public:
    DelegationKey(const DelegationKey&) = delete("a delegation key exists only for the one call that its maker "
                                                 "passes it to");
    DelegationKey& operator=(const DelegationKey&) = delete("a delegation key is never stored");
    constexpr ~DelegationKey() noexcept {}
};

static_assert(detail::is_sealed_passkey<DelegationKey>(),
              "a passkey is final, has a private user-provided constructor, no copy, no move and a user-provided "
              "destructor, so no route but its one friend makes it");

template <typename InnerProto, typename Resource, typename Policy, typename InnerPS>
class [[nodiscard]] DelegatedSession {
    using handle_type = SessionHandle<InnerProto, Resource, void, Policy, InnerPS>;
    using hold_type = PermHold<InnerPS>;
    static constexpr bool is_nothrow_move = std::is_nothrow_move_constructible_v<handle_type>;

    // Empty after a move, an assignment from it, or accept().  The move of
    // a std::optional keeps the source engaged, so each move resets it.
    std::optional<handle_type> handle_;
    std::optional<hold_type> hold_;

    // Moves the endpoint and its hold out of `other`, and leaves it empty.
    constexpr void take_from_(DelegatedSession& other) noexcept(is_nothrow_move) {
        if (other.handle_.has_value()) {
            handle_.emplace(std::move(*other.handle_));
            other.handle_.reset();
        }
        if (other.hold_.has_value()) {
            hold_.emplace(std::move(*other.hold_));
            other.hold_.reset();
        }
    }

public:
    using inner_proto = InnerProto;
    using inner_perm_set = InnerPS;
    using resource_type = Resource;
    using abandonment_policy = Policy;

    // The constructor takes the delegation key, which only the delegation
    // door makes.
    constexpr DelegatedSession(DelegationKey const&, handle_type&& handle, hold_type&& hold) noexcept(is_nothrow_move)
        : handle_{std::in_place, std::move(handle)}, hold_{std::in_place, std::move(hold)} {}

    constexpr DelegatedSession(DelegatedSession&& other) noexcept(is_nothrow_move) { take_from_(other); }

    // An assignment over a DelegatedSession that still holds an endpoint
    // drops that endpoint first, as an assignment over a live handle does.
    constexpr DelegatedSession& operator=(DelegatedSession&& other) noexcept(is_nothrow_move) {
        if (this == &other) [[unlikely]]
            return *this;
        handle_.reset();
        hold_.reset();
        take_from_(other);
        return *this;
    }

    DelegatedSession(const DelegatedSession&) =
        delete("a DelegatedSession holds one endpoint.  A copy is a second holder of the endpoint");
    DelegatedSession& operator=(const DelegatedSession&) =
        delete("a DelegatedSession holds one endpoint.  A copy is a second holder of the endpoint");

    // A DelegatedSession that no recipient accepted drops the handle it
    // holds, so the abandonment policy of the handle acts.
    ~DelegatedSession() = default;

    [[nodiscard]] constexpr bool holds_endpoint() const noexcept { return handle_.has_value(); }

    // Gives the recipient the handle at the delegated position.  A handle
    // with an empty permission set comes alone.  A handle with tags comes
    // in a pair with the hold of their tokens.  An accept on a
    // DelegatedSession that holds no endpoint aborts.
    [[nodiscard]] constexpr auto accept() && noexcept(is_nothrow_move) {
        if (!handle_.has_value()) [[unlikely]]
            detail::report_delegation_taken();
        handle_type handle{std::move(*handle_)};
        hold_type hold{std::move(*hold_)};
        handle_.reset();
        hold_.reset();
        if constexpr (detail::perm_set_is_empty(^^InnerPS)) {
            return handle;
        } else {
            return std::pair{std::move(handle), std::move(hold)};
        }
    }
};

namespace detail {

// True when the type is a handle that can delegate: a handle outside every
// Loop, with no brand.  The test is a function at namespace scope that is
// not a template, so no translation unit can specialize it to delegate a
// handle inside a Loop.
[[nodiscard]] consteval bool is_delegatable_handle(std::meta::info handle) {
    return std::meta::has_template_arguments(handle) && std::meta::template_of(handle) == (^^SessionHandle)
        && std::meta::dealias(std::meta::template_arguments_of(handle)[2]) == (^^void);
}

}  // namespace detail

// True when H is a session handle that can travel as a DelegatedSession.
template <typename H>
concept DelegatableHandle = detail::is_delegatable_handle(^^H);

// True when Hold backs the permission set of the handle H: it holds the
// token of each tag of that set, and nothing else.
template <typename Hold, typename H>
concept HoldsTokensOf = std::is_same_v<Hold, PermHold<typename H::perm_set>>;

// The door that builds a DelegatedSession, and the one friend of
// DelegationKey.  Each public member states the whole gate of a form of
// mint_delegated_session, so a direct call is no weaker than the mint.
// The class is final, and no object of it exists.
class DelegationDoor final : ::foundation::NoObject<DelegationDoor> {
public:
    // Moves the live handle and the hold of its tokens into the payload.  A
    // consumed handle aborts here, as at every operation.
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS>
        requires DelegatableHandle<SessionHandle<Proto, Resource, void, Policy, PS>>
    [[nodiscard]] static constexpr DelegatedSession<Proto, Resource, Policy, PS>
    give(SessionHandle<Proto, Resource, void, Policy, PS>&& handle,
         PermHold<PS>&& hold) noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        using Handle = SessionHandle<Proto, Resource, void, Policy, PS>;
        if (!handle.is_live()) [[unlikely]]
            detail::report_use_after_consume(Handle::wrapper_name(), Handle::protocol_name(), std::source_location{});
        return DelegatedSession<Proto, Resource, Policy, PS>{DelegationKey{}, std::move(handle), std::move(hold)};
    }
};

// Moves the live handle, with the empty permission set, into the payload
// that carries it.  The payload travels as the value of a Send.
template <typename H>
    requires DelegatableHandle<H> && (detail::perm_set_is_empty(^^typename H::perm_set))
[[nodiscard]] constexpr auto
mint_delegated_session(H handle) noexcept(std::is_nothrow_move_constructible_v<typename H::resource_type>) {
    return DelegationDoor::give(std::move(handle), mint_permission_hold());
}

// Moves the live handle and the hold of the tokens that back its
// permission set into the payload.  The sender keeps no token of the
// endpoint, and the recipient gets them with the handle.
template <typename H, typename Hold>
    requires DelegatableHandle<H> && HoldsTokensOf<Hold, H>
[[nodiscard]] constexpr auto
mint_delegated_session(H handle, Hold hold) noexcept(std::is_nothrow_move_constructible_v<typename H::resource_type>) {
    return DelegationDoor::give(std::move(handle), std::move(hold));
}

// ── Queries over the delegation heads ────────────────────────────────

// True when the head of P, under its VendorPinned wrappers, is a
// Delegate or an Accept.  Each trait is an alias and each _v form is a
// concept, so no program can specialize one to change its answer.
template <typename P>
using is_delegate = std::bool_constant<detail::head_is(^^P, ^^Delegate)>;

template <typename P>
using is_accept = std::bool_constant<detail::head_is(^^P, ^^Accept)>;

template <typename P>
concept is_delegate_v = detail::head_is(^^P, ^^Delegate);

template <typename P>
concept is_accept_v = detail::head_is(^^P, ^^Accept);

// True when CarrierProto is a Delegate that hands off an endpoint of
// DelegatedProto.
template <typename CarrierProto, typename DelegatedProto>
concept DelegatesTo =
    is_delegate_v<CarrierProto> && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

// True when CarrierProto is an Accept that receives an endpoint of
// DelegatedProto.
template <typename CarrierProto, typename DelegatedProto>
concept AcceptsFrom =
    is_accept_v<CarrierProto> && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

}  // namespace fixy::session

// ── Armed cell ───────────────────────────────────────────────────────

namespace fixy::session::detail::delegatable_armed_witness {
struct Wire {};
using Loose = SessionHandle<End, Wire, void, DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>;
using Branded = SessionHandle<End, Wire, session_brand<Wire, void>, DefaultAbandonmentPolicy,
                              ::foundation::permissions::EmptyPermSet>;
using InLoop = SessionHandle<End, Wire, Loop<Send<int, Continue>>, DefaultAbandonmentPolicy,
                             ::foundation::permissions::EmptyPermSet>;
}  // namespace fixy::session::detail::delegatable_armed_witness

static_assert(::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::Loose>);
static_assert(!::fixy::session::DelegatableHandle<int>
              && !::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::Branded>
              && !::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::InLoop>);

namespace fixy::session::detail::delegation_head_armed_witness {
using Carried = Send<int, End>;
using Hands = Delegate<Carried, End>;
using Takes = Accept<Carried, End>;
using PinnedHands = VendorPinned<VendorBackend::NV, Hands>;
}  // namespace fixy::session::detail::delegation_head_armed_witness

// A Delegate under a VendorPinned is still a Delegate.  A Send of a
// protocol is no hand-off.
template <>
struct foundation::contracts::armed_cell<::fixy::session::is_delegate> {
    using accepts = witnesses<::fixy::session::detail::delegation_head_armed_witness::Hands,
                              ::fixy::session::detail::delegation_head_armed_witness::PinnedHands>;
    using refuses = witnesses<
        int, ::fixy::session::detail::delegation_head_armed_witness::Takes,
        ::fixy::session::Send<::fixy::session::detail::delegation_head_armed_witness::Carried, ::fixy::session::End>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_accept> {
    using accepts = witnesses<::fixy::session::detail::delegation_head_armed_witness::Takes>;
    using refuses = witnesses<
        int, ::fixy::session::detail::delegation_head_armed_witness::Hands,
        ::fixy::session::Recv<::fixy::session::detail::delegation_head_armed_witness::Carried, ::fixy::session::End>>;
};

// The two traits are aliases, and the roster walk of
// foundation/contracts/Armed.h finds class templates only.  These two
// assertions read the two cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_delegate>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_accept>);
