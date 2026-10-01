#pragma once

// The network model of a carrier.
//
// A projection of fixy/session/Projection.h is safe on one FIFO queue for
// each ordered pair of roles.  A carrier can give other semantics.  Li
// and Wies, "Implementability of Global Distributed Protocols modulo
// Network Architectures" (PLDI 2026, arXiv 2602.10320), Definition 4.1,
// names three that this tree uses:
//
//   - per-pair FIFO: one FIFO queue for each ordered pair of roles;
//   - mailbox: one FIFO queue for each receiver, which all its senders
//     share;
//   - bag: an unordered multiset of messages for each receiver.
//
// The network model is a property of the carrier, the Resource of a
// session handle.  A carrier states it with a static constexpr member
// session_network of the type Network.  A carrier that states none has
// no model, and a multiparty binding refuses it.  A carrier that gives
// selective receive for each sender has per-pair FIFO semantics, and it
// states PerPairFifo.
//
// This header holds only the model and the trait that reads it.  It
// includes no global type and no projection.  A gate that reads only the
// model of a carrier includes this header, and not fixy/session/Network.h.
// fixy/session/Network.h holds the check that a global type is
// implementable on a model.

#include <foundation/reflect/Instance.h>

#include <cstdint>
#include <meta>

namespace fixy::session {

// The semantics of the message buffers of a carrier (Definition 4.1 of
// Li and Wies), and one value for a carrier with no peer.
enum class Network : std::uint8_t {
    PerPairFifo,
    Mailbox,
    Bag,
    // A carrier with no peer, such as a log or an atomic cell.  A session
    // over it makes each choice alone and gives each message to the
    // carrier, not to a role, so a choice puts no label on a wire.  It
    // implements no global type with two roles (fixy/session/Network.h).
    Local,
};

// ── The declaration of a carrier ─────────────────────────────────────
//
// Each answer below is a function at namespace scope that is not a
// template, so no translation unit can specialize it.  A carrier states
// its network in its own declarations, and nothing else states it.

namespace detail::network {

// Declared and not defined, and not constexpr.  A constant evaluation
// that calls it fails, and the diagnostic gives its name as the reason.
void carrier_states_no_network() noexcept;

}  // namespace detail::network

// True when the carrier, or a base of it, has a member named
// session_network that code outside the class can name.
[[nodiscard]] consteval bool states_a_network_member(std::meta::info resource) {
    return ::foundation::reflect::member_named(resource, "session_network") != std::meta::info{};
}

// True when the carrier states its network model with a static member
// session_network of the type Network.  A member of that name with
// another type states nothing: a plain integer could be a count.
[[nodiscard]] consteval bool has_session_network(std::meta::info resource) {
    const std::meta::info member = ::foundation::reflect::member_named(resource, "session_network");
    return member != std::meta::info{} && std::meta::is_variable(member) && std::meta::is_static_member(member)
        && std::meta::remove_cv(std::meta::type_of(member)) == ^^Network;
}

// The network that the carrier states.  A carrier that states none has no
// network, and a call for it is not a constant expression.
[[nodiscard]] consteval Network session_network(std::meta::info resource) {
    if (!has_session_network(resource)) detail::network::carrier_states_no_network();
    return std::meta::extract<Network>(::foundation::reflect::member_named(resource, "session_network"));
}

// True when the carrier states the Local network, so it has no peer.  A
// carrier that states no network, or another one, answers false.
template <typename Resource>
concept LocalCarrier = has_session_network(^^Resource) && session_network(^^Resource) == Network::Local;

}  // namespace fixy::session
