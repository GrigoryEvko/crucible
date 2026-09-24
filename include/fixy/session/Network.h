#pragma once

// The check that a global type is implementable on the network model of
// a carrier.
//
// fixy/session/NetworkModel.h names the three network models of this
// tree, and it holds the declaration of a carrier.
//
// A projected type that is implementable on per-pair FIFO can fail on a
// mailbox or a bag.  On a mailbox, the head of the queue of a receiver
// can come from one sender while the receiver waits for another sender.
// Nothing else takes that head, so the receiver waits for ever.  On a
// bag, a later label of a choice can overtake an earlier one.  The
// receiver then takes the later branch, and the earlier message stays
// in the bag with no receiver.
//
// ── The conditions ──────────────────────────────────────────────────
//
// is_implementable_on reads a global type G, a network N and a set of
// reliable roles.  It admits G only when G is well-formed, holds no
// runtime construct, is balanced+ (Pischke, Masters and Yoshida,
// Definition 17), and projects onto each of its roles.  These are the
// conditions of the association theorem of fixy/session/Projection.h,
// which holds on per-pair FIFO.  On per-pair FIFO the check stops here.
//
// Li and Wies characterize implementability on each network exactly
// with their Generalized Coherence Conditions (Definition 5.4, Theorem
// 5.5).  This header does not implement those conditions.  It admits G
// on a mailbox or a bag only through a reduction to per-pair FIFO that
// holds for the executions of G.  The two reductions are sufficient, not
// necessary.  So each admission is sound, and a type that the paper
// admits can still be refused here.
//
//   - Mailbox.  Each receiver of G has at most one sender.  Then the
//     channel of a receiver q on a mailbox, ch(p, q) = q, holds messages
//     from one sender p only.  It is the FIFO queue of the pair (p, q), with
//     the same insert and remove.  The two networks then have the same
//     channel-compliant words over the messages of G (Definition 4.3).  So
//     an implementation of G on per-pair FIFO also implements G on the
//     mailbox (Definition 4.5).
//
//   - Bag.  No transmission of G has more than one branch.  Then G has one
//     run, and on each pair (p, q) the receives of q follow the sends of p
//     in the order of G.  A receive of a message m from a bag is possible
//     exactly when the FIFO queue of the pair has m at its head: the bag
//     holds m only when p sent the message that the order of G puts next.
//     So the two networks enable the same transitions from each reachable
//     configuration, and an implementation on one is an implementation on
//     the other.
//
// A crash branch makes a transmission a choice, and Li and Wies model no
// crash.  So a set of reliable roles other than EveryRoleReliable is
// admitted on per-pair FIFO only.  There the crash-stop paper of
// Barwell, Hou, Yoshida and Zhou (LMCS 21:2, 2025) gives the theorem.
//
// A refusal on a mailbox or a bag is either a real fault or a gap of the
// reduction.  The session oracle compares each verdict with the naive
// query generator of Sprout(A), the tool of Li and Wies.  A type that
// this header admits and Sprout(A) refuses fails the oracle.  A type that
// this header refuses and Sprout(A) admits is an incompleteness gap on a
// ledger that can only shrink.
//
// Complexity: the mailbox check is quadratic in the transmissions of G,
// and the bag check is linear in them.  Both run at compile time.

#include <fixy/session/Crash.h>
#include <fixy/session/Global.h>
#include <fixy/session/NetworkModel.h>
#include <fixy/session/Projection.h>
#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::session {

namespace detail::network {

template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::network

// ── The reasons of a refusal ─────────────────────────────────────────

enum class NetworkRefusal : std::uint8_t {
    None,
    NotAReliableSet,
    NotWellFormed,
    RuntimeConstruct,
    NotBalancedPlus,
    RoleNotProjectable,
    CrashStopOffPerPairFifo,
    ReceiverHasTwoSenders,
    ChoiceOnBag,
};

namespace detail::network {

namespace g = ::fixy::session::global;

template <typename From, typename To>
struct SenderReceiver {
    using from = From;
    using to = To;
};

template <typename... Pairs>
struct PairList {};

template <typename... Lists>
struct pair_concat;
template <>
struct pair_concat<> {
    using type = PairList<>;
};
template <typename... A>
struct pair_concat<PairList<A...>> {
    using type = PairList<A...>;
};
template <typename... A, typename... B, typename... Rest>
struct pair_concat<PairList<A...>, PairList<B...>, Rest...> : pair_concat<PairList<A..., B...>, Rest...> {};

// The sender and the receiver of each transmission of G.  The primary is
// declared only, so a node that the walk does not know stops the build.
// The gate refuses a runtime construct before it runs the walk, so the
// walk never meets an en-route node or a crash annotation.
template <typename G>
struct transmission_walk;
template <>
struct transmission_walk<g::End> {
    using type = PairList<>;
};
template <>
struct transmission_walk<g::Var> {
    using type = PairList<>;
};
template <typename Body>
struct transmission_walk<g::Rec<Body>> : transmission_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct transmission_walk<g::Comm<From, To, g::Branch<Ls, Ps, Cs>...>> {
    using type =
        typename pair_concat<PairList<SenderReceiver<From, To>>, typename transmission_walk<Cs>::type...>::type;
};

// The most branches that one transmission of G has.
template <typename G>
struct widest_choice_walk;
template <>
struct widest_choice_walk<g::End> : std::integral_constant<std::size_t, 0> {};
template <>
struct widest_choice_walk<g::Var> : std::integral_constant<std::size_t, 0> {};
template <typename Body>
struct widest_choice_walk<g::Rec<Body>> : widest_choice_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct widest_choice_walk<g::Comm<From, To, g::Branch<Ls, Ps, Cs>...>>
    : std::integral_constant<std::size_t, [] {
          std::size_t widest = sizeof...(Ls);
          ((widest = widest_choice_walk<Cs>::value > widest ? widest_choice_walk<Cs>::value : widest), ...);
          return widest;
      }()> {};

// True when no receiver of the list has two different senders.  The
// primary answers false, so a list of another shape is refused.
template <typename Pair, typename... Others>
inline constexpr bool sender_agrees_v =
    ((!std::is_same_v<typename Pair::to, typename Others::to> || std::is_same_v<typename Pair::from, typename Others::from>)
     && ...);

template <typename List>
inline constexpr bool each_receiver_one_sender_v = false;
template <typename... Pairs>
inline constexpr bool each_receiver_one_sender_v<PairList<Pairs...>> = (sender_agrees_v<Pairs, Pairs...> && ...);

template <typename Reliable>
inline constexpr bool reliable_set_is_known_v =
    std::is_same_v<Reliable, EveryRoleReliable> || is_reliable_set<Reliable>::value;

template <typename G, Network N, typename Reliable>
consteval NetworkRefusal refusal_of() noexcept {
    if constexpr (!reliable_set_is_known_v<Reliable>) {
        return NetworkRefusal::NotAReliableSet;
    } else if constexpr (!g::is_global_well_formed_v<G>) {
        return NetworkRefusal::NotWellFormed;
    } else if constexpr (proj::holds_runtime_construct_v<G>) {
        return NetworkRefusal::RuntimeConstruct;
    } else if constexpr (!g::is_balanced_plus_v<G>) {
        return NetworkRefusal::NotBalancedPlus;
    } else if constexpr (!proj::each_role_crash_projects<G, Reliable, g::roles_t<G>>::value) {
        return NetworkRefusal::RoleNotProjectable;
    } else if constexpr (N == Network::PerPairFifo) {
        return NetworkRefusal::None;
    } else if constexpr (!std::is_same_v<Reliable, EveryRoleReliable>) {
        return NetworkRefusal::CrashStopOffPerPairFifo;
    } else if constexpr (N == Network::Mailbox) {
        return each_receiver_one_sender_v<typename transmission_walk<G>::type> ? NetworkRefusal::None
                                                                               : NetworkRefusal::ReceiverHasTwoSenders;
    } else {
        return widest_choice_walk<G>::value <= 1 ? NetworkRefusal::None : NetworkRefusal::ChoiceOnBag;
    }
}

}  // namespace detail::network

// The question "is G implementable on N, with these reliable roles".  It
// is one type, so the predicate below takes one type argument.
template <typename G, Network N, typename Reliable = EveryRoleReliable>
struct Implementability {};

template <typename Q>
struct is_implementable_on : std::false_type {};
template <typename G, Network N, typename Reliable>
struct is_implementable_on<Implementability<G, N, Reliable>>
    : std::bool_constant<detail::network::refusal_of<G, N, Reliable>() == NetworkRefusal::None> {};

template <typename G, Network N, typename Reliable = EveryRoleReliable>
inline constexpr bool implementable_on_v = is_implementable_on<Implementability<G, N, Reliable>>::value;

template <typename G, Network N, typename Reliable = EveryRoleReliable>
inline constexpr NetworkRefusal network_refusal_v = detail::network::refusal_of<G, N, Reliable>();

// The gate of a multiparty binding: the carrier states its network, and
// G is implementable on it.  A binding of a projected type to a carrier
// asks this concept, and ensure_carrier_implements names the reason of a
// refusal.
template <typename G, typename Resource, typename Reliable = EveryRoleReliable>
concept CarrierImplements =
    has_session_network_v<Resource> && is_implementable_on<Implementability<G, session_network_v<Resource>, Reliable>>::value;

template <typename G, Network N, typename Reliable = EveryRoleReliable>
consteval void ensure_implementable_on() noexcept {
    constexpr NetworkRefusal refusal = detail::network::refusal_of<G, N, Reliable>();
    if constexpr (refusal == NetworkRefusal::NotAReliableSet) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Reliable_Set_Malformed]: the set of reliable roles must be "
                      "EveryRoleReliable or a ReliableSet<Roles...>.");
    } else if constexpr (refusal == NetworkRefusal::NotWellFormed) {
        global::ensure_global_well_formed<G>();
    } else if constexpr (refusal == NetworkRefusal::RuntimeConstruct) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Runtime_Construct]: the global type holds an EnRoute node or "
                      "a crash annotation.  A carrier binds a protocol at its start.  Check the protocol, not a "
                      "state that it reaches.");
    } else if constexpr (refusal == NetworkRefusal::NotBalancedPlus) {
        global::ensure_balanced_plus<G>();
    } else if constexpr (refusal == NetworkRefusal::RoleNotProjectable) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Role_Not_Projectable]: the global type has no projection "
                      "onto one of its roles.  Call ensure_crash_projectable<G, Role, Reliable>() or "
                      "ensure_projectable<G, Role>() to see why.");
    } else if constexpr (refusal == NetworkRefusal::CrashStopOffPerPairFifo) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Crash_Stop_Needs_Per_Pair_Fifo]: a crash-stop protocol binds "
                      "only to a per-pair FIFO carrier.  The theorem of Barwell, Hou, Yoshida and Zhou holds there, "
                      "and Li and Wies model no crash on a mailbox or a bag.");
    } else if constexpr (refusal == NetworkRefusal::ReceiverHasTwoSenders) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Mailbox_Receiver_Has_Two_Senders]: on a mailbox the "
                      "senders of one receiver share one FIFO queue.  A message from one sender can reach the head "
                      "while the receiver waits for the other sender, and the receiver then waits for ever.  Give "
                      "each receiver one sender, or bind to a per-pair FIFO carrier.");
    } else if constexpr (refusal == NetworkRefusal::ChoiceOnBag) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      "fixy::session::diagnostic [Network_Bag_Choice]: on a bag a later label of a choice can overtake "
                      "an earlier one, so the receiver can take the wrong branch.  Bind a protocol with choices to a "
                      "per-pair FIFO carrier.");
    }
}

template <typename G, typename Resource, typename Reliable = EveryRoleReliable>
consteval void ensure_carrier_implements() noexcept {
    if constexpr (detail::network::states_a_network_member<Resource>()
                  && !detail::network::states_a_typed_network<Resource>()) {
        static_assert(detail::network::dependent_false_v<G, Resource>,
                      "fixy::session::diagnostic [Session_Network_Type]: the carrier declares session_network with "
                      "a type other than fixy::session::Network.  Declare it as static constexpr "
                      "fixy::session::Network session_network = fixy::session::Network::PerPairFifo; or with the "
                      "value that states its semantics.");
    } else if constexpr (!has_session_network_v<Resource>) {
        static_assert(detail::network::dependent_false_v<G, Resource>,
                      "fixy::session::diagnostic [Carrier_Network_Undeclared]: the carrier states no network model, "
                      "so no check can say that the protocol is implementable on it.  Give the carrier a static "
                      "constexpr fixy::session::Network session_network member.");
    } else {
        ensure_implementable_on<G, session_network_v<Resource>, Reliable>();
    }
}

namespace detail::network::witness {

using global::detail::witness::LabelX;
using global::detail::witness::RoleA;
using global::detail::witness::RoleB;

struct RoleC {};
struct LabelY {};

// RoleA receives from RoleB and then from RoleC.
using TwoSenders = global::Msg<RoleB, RoleA, LabelX, int, global::Msg<RoleC, RoleA, LabelX, int, global::End>>;
// RoleA chooses, again and again, to go on or to stop.
using LoopChoice = global::Rec<global::Comm<RoleA, RoleB, global::Branch<LabelX, int, global::Var>,
                                            global::Branch<LabelY, int, global::End>>>;
// One message from RoleA to RoleB.
using OneMessage = global::Msg<RoleA, RoleB, LabelX, int, global::End>;

}  // namespace detail::network::witness

}  // namespace fixy::session

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_implementable_on> {
    using accepts = witnesses<
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoSenders,
                                          ::fixy::session::Network::PerPairFifo>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::OneMessage,
                                          ::fixy::session::Network::Mailbox>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoSenders,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::Mailbox>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::PerPairFifo>>;
    using refuses = witnesses<
        int,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoSenders,
                                          ::fixy::session::Network::Mailbox>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::OneMessage,
                                          ::fixy::session::Network::Mailbox, ::fixy::session::NoReliableRoles>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::OneMessage,
                                          ::fixy::session::Network::PerPairFifo, int>>;
};
