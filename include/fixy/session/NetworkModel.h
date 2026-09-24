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

#include <foundation/contracts/Armed.h>

#include <cstdint>
#include <type_traits>

namespace fixy::session {

// The semantics of the message buffers of a carrier (Definition 4.1 of
// Li and Wies).
enum class Network : std::uint8_t {
    PerPairFifo,
    Mailbox,
    Bag,
};

// ── The declaration of a carrier ─────────────────────────────────────

namespace detail::network {

template <typename Resource>
consteval bool states_a_network_member() noexcept {
    using Bare = std::remove_cvref_t<Resource>;
    return requires { Bare::session_network; };
}

template <typename Resource>
consteval bool states_a_typed_network() noexcept {
    using Bare = std::remove_cvref_t<Resource>;
    if constexpr (states_a_network_member<Resource>()) {
        return std::is_same_v<std::remove_cv_t<decltype(Bare::session_network)>, Network>;
    } else {
        return false;
    }
}

}  // namespace detail::network

// True when the carrier states its network model with a static member
// session_network of the type Network.  A member of that name with
// another type states nothing: a plain integer could be a count.
template <typename Resource>
struct has_session_network : std::bool_constant<detail::network::states_a_typed_network<Resource>()> {};

template <typename Resource>
inline constexpr bool has_session_network_v = has_session_network<Resource>::value;

template <typename Resource>
    requires has_session_network_v<Resource>
inline constexpr Network session_network_v = std::remove_cvref_t<Resource>::session_network;

namespace detail::network::witness {

struct FifoCarrier {
    static constexpr Network session_network = Network::PerPairFifo;
};
struct MailboxCarrier {
    static constexpr Network session_network = Network::Mailbox;
};
struct BagCarrier {
    static constexpr Network session_network = Network::Bag;
};
struct SilentCarrier {};
struct IntegerCarrier {
    static constexpr int session_network = 0;
};

}  // namespace detail::network::witness

}  // namespace fixy::session

template <>
struct foundation::contracts::armed_cell<::fixy::session::has_session_network> {
    using accepts = witnesses<::fixy::session::detail::network::witness::FifoCarrier,
                              ::fixy::session::detail::network::witness::MailboxCarrier,
                              ::fixy::session::detail::network::witness::BagCarrier,
                              ::fixy::session::detail::network::witness::FifoCarrier&>;
    using refuses = witnesses<int, ::fixy::session::detail::network::witness::SilentCarrier,
                              ::fixy::session::detail::network::witness::IntegerCarrier>;
};
