#pragma once

// Delegation: a message that carries a live endpoint of a different
// session.
//
// A sender hands an endpoint of a session to a peer as the payload of a
// message.  This is the throw and catch of higher-order session types
// (Honda, Vasconcelos and Kubo, ESOP 1998).  mint_delegated_session takes
// the endpoint out of its handle through endpoint_transfer, the one door
// of fixy/session/Handle.h that keeps the session live.  The handle is
// then consumed, so the sender cannot step the endpoint again.  The
// result is a DelegatedSession: a payload that holds the Resource and the
// watch record of the endpoint.  The recipient calls accept() on it and
// gets a handle at the same protocol position.
//
// The rules:
//
//   1. Only mint_delegated_session builds a DelegatedSession.  Its value
//      constructor is private, and the type is not trivially copyable and
//      not an implicit-lifetime type.  So no code builds one from bytes or
//      from a Resource that no handle owned.
//   2. A DelegatedSession is linear.  It moves and never copies.  A
//      DelegatedSession that is destroyed or assigned over before
//      accept() rebuilds its handle and drops it, so the abandonment
//      policy of the handle acts: it aborts, it sends the cancellation, or
//      under check::Off it does nothing.
//   3. Only a handle outside every Loop delegates.  Inside a Loop the
//      position does not state the rest of the protocol, because a
//      Continue refers to the loop context of the handle.  A handle that
//      a callback entry or a forked channel owns carries the brand of its
//      body in that context, and the body must return it, so it cannot
//      delegate either.
//   4. The tags of the permission set of the endpoint move from the set of
//      the sender to the set of the recipient, as the payload walk of
//      fixy/session/Payload.h states for a hand-off.
//   5. The payload order is contravariant in the protocol of the endpoint
//      (the axiom delegation_contravariant of fixy/session/Subtype.h).
//   6. A protocol that sends an endpoint of a session to a peer of that
//      session is refused at every mint (DelegatesToNoOwnPeer in
//      fixy/session/Payload.h).
//
// Crash sessions and checkpoint sessions refuse a payload that delegates
// (payload_conveys_delegation_v), because their theories have no
// delegation.

#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>
#include <fixy/session/Subtype.h>
#include <foundation/contracts/Armed.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace fixy::session {

namespace detail {

struct delegation_door;

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

template <typename InnerProto, typename Resource, typename Policy, typename InnerPS>
class [[nodiscard]] DelegatedSession {
    friend struct detail::delegation_door;

    using endpoint_type = detail::transferred_endpoint<Resource>;
    static constexpr bool is_nothrow_move = std::is_nothrow_move_constructible_v<endpoint_type>;

    constexpr explicit DelegatedSession(endpoint_type&& endpoint) noexcept(is_nothrow_move)
        : endpoint_{std::in_place, std::move(endpoint)} {}

    // Empty after a move, an assignment from it, or accept().  The move of
    // a std::optional keeps the source engaged, so each move resets it.
    std::optional<endpoint_type> endpoint_;

    // Builds the handle at the delegated position.  The session record
    // stays the record of the endpoint, so the watch sees one session.
    [[nodiscard]] constexpr auto rebuild_() && noexcept(is_nothrow_move) {
        endpoint_type taken{std::move(*endpoint_)};
        endpoint_.reset();
        return detail::make_session_handle<InnerProto, Resource, void, Policy, InnerPS>(
            std::forward<Resource>(taken.resource), taken.session);
    }

    // Drops the endpoint that this object holds through its handle, so the
    // abandonment policy of the handle acts on it.
    constexpr void drop_held_() noexcept(is_nothrow_move) {
        if (endpoint_.has_value()) static_cast<void>(std::move(*this).rebuild_());
    }

public:
    using inner_proto = InnerProto;
    using inner_perm_set = InnerPS;
    using resource_type = Resource;
    using abandonment_policy = Policy;

    constexpr DelegatedSession(DelegatedSession&& other) noexcept(is_nothrow_move) : endpoint_{} {
        if (other.endpoint_.has_value()) {
            endpoint_.emplace(std::move(*other.endpoint_));
            other.endpoint_.reset();
        }
    }

    // An assignment over a DelegatedSession that still holds an endpoint
    // drops that endpoint first, as an assignment over a live handle does.
    constexpr DelegatedSession& operator=(DelegatedSession&& other) noexcept(is_nothrow_move) {
        if (this == &other) [[unlikely]]
            return *this;
        drop_held_();
        if (other.endpoint_.has_value()) {
            endpoint_.emplace(std::move(*other.endpoint_));
            other.endpoint_.reset();
        }
        return *this;
    }

    DelegatedSession(const DelegatedSession&) =
        delete("a DelegatedSession holds one endpoint.  A copy is a second holder of the endpoint");
    DelegatedSession& operator=(const DelegatedSession&) =
        delete("a DelegatedSession holds one endpoint.  A copy is a second holder of the endpoint");

    // A DelegatedSession that no recipient accepted drops the endpoint
    // through its handle, so the abandonment policy of the handle acts.
    ~DelegatedSession() { drop_held_(); }

    [[nodiscard]] constexpr bool holds_endpoint() const noexcept { return endpoint_.has_value(); }

    // Gives the recipient the handle at the delegated position.  An accept
    // on a DelegatedSession that holds no endpoint aborts.
    [[nodiscard]] constexpr auto accept() && noexcept(is_nothrow_move) {
        if (!endpoint_.has_value()) [[unlikely]]
            detail::report_delegation_taken();
        return std::move(*this).rebuild_();
    }
};

namespace detail {

// A handle that can delegate: a handle outside every Loop, with no brand.
template <typename H>
struct is_delegatable_handle : std::false_type {};
template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS>
struct is_delegatable_handle<SessionHandle<Proto, Resource, void, Policy, PS>> : std::true_type {};

// The one door that builds a DelegatedSession.  It takes the endpoint out
// of the handle through endpoint_transfer, which keeps the session record
// live.
struct delegation_door {
    template <typename Proto, typename Resource, AbandonmentPolicy Policy, typename PS>
    [[nodiscard]] static constexpr DelegatedSession<Proto, Resource, Policy, PS>
    give(SessionHandle<Proto, Resource, void, Policy, PS>&& handle) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        return DelegatedSession<Proto, Resource, Policy, PS>{endpoint_transfer::take(std::move(handle))};
    }
};

}  // namespace detail

// True when H is a session handle that can travel as a DelegatedSession.
template <typename H>
concept DelegatableHandle = detail::is_delegatable_handle<H>::value;

// Takes the endpoint out of the handle, which is consumed, and gives the
// payload that carries it.  The payload travels as the value of a Send.
template <typename H>
    requires DelegatableHandle<H>
[[nodiscard]] constexpr auto mint_delegated_session(H handle) noexcept(
    std::is_nothrow_move_constructible_v<typename H::resource_type>) {
    return detail::delegation_door::give(std::move(handle));
}

}  // namespace fixy::session

// ── Armed cell ───────────────────────────────────────────────────────

namespace fixy::session::detail::delegatable_armed_witness {
struct Wire {};
using Loose = SessionHandle<End, Wire, void, DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>;
using Branded =
    SessionHandle<End, Wire, session_brand<Wire, void>, DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>;
using InLoop = SessionHandle<End, Wire, Loop<Send<int, Continue>>, DefaultAbandonmentPolicy,
                             ::foundation::permissions::EmptyPermSet>;
}  // namespace fixy::session::detail::delegatable_armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::detail::is_delegatable_handle> {
    using accepts = witnesses<::fixy::session::detail::delegatable_armed_witness::Loose>;
    using refuses = witnesses<int, ::fixy::session::detail::delegatable_armed_witness::Branded,
                              ::fixy::session::detail::delegatable_armed_witness::InLoop>;
};
