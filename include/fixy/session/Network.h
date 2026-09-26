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
// in the bag with no receiver.  A bag also mixes up two messages with
// one label.  The wire word of a message is its label alone
// (fixy/session/Protocol.h): the sender and the payload type are not on
// the wire.  So a receive can take the second of two such messages
// first, with a payload of another type or from another sender.
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
//   - Bag.  A bag carrier gives a receive a message of its receiver whose
//     wire word is the word that the receive expects.  G is admitted when
//     no transmission of G has more than one branch, and when each two
//     transmissions to one receiver with one wire word are ordered: the
//     receive of the first happens before the send of the second.
//     "Happens before" is the order of the events of the run of G: the
//     events of one role in the order of G, and each send before its
//     receive.
//
//     With no choice, G has one run.  Take a receive of q in that run, of
//     the message m from p with the word w.  Each earlier message to q
//     with the word w was received before, because the receives of q
//     follow the order of G.  Each later message to q with the word w is
//     sent after this receive, by the order condition.  So while q waits
//     for m, the bag of q holds no other message with the word w, and the
//     receive takes m exactly when p has sent it.  On per-pair FIFO the
//     queue of (p, q) has m at its head exactly then, because q received
//     each earlier message of p.  So the two networks enable the same
//     transitions from each reachable configuration, and an
//     implementation on one is an implementation on the other.
//
//     Two messages with one word and no order between them can both be in
//     the bag.  The receive of the first can then take the second: a
//     payload of another type, a message from another sender, or a later
//     value of a stream.  So such a pair is refused, also when the two
//     messages have the same sender and the same payload type: the bag
//     loses the order of their values.
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
// ledger that can only shrink.  The model of Sprout(A) is wider than our
// wire on a bag: its message carries the sender and the payload type, and
// our wire word does not.  So Sprout(A) admits two messages with one
// label that our bag check refuses, and the oracle keeps those rows on a
// ledger of their own.
//
// Complexity: the mailbox check is quadratic in the transmissions of G.
// The bag check is O(n² · r) for n transmissions in the run of G, with
// the loop body counted twice, and r roles.  Both run at compile time.

#include <fixy/session/Crash.h>
#include <fixy/session/Global.h>
#include <fixy/session/NetworkModel.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <foundation/algebra/Transition.h>
#include <foundation/contracts/Armed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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
    RepeatedWordOnBag,
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

// ── The run of a protocol on a bag ───────────────────────────────────
//
// The bag check reads G as one run: the transmissions before the loop
// that the Var at its end binds, then the body of that loop.  A Var binds
// the nearest Rec, and with no choice the nearest Rec is the last Rec on
// the path.  A Rec that no Var binds runs its body once.

// One transmission of the run: its roles, its label and payload, and the
// wire word that its receive expects.
struct hop {
    std::meta::info from{};
    std::meta::info to{};
    std::meta::info label{};
    std::meta::info payload{};
    std::uint64_t word = 0;
};

// The transmissions of the run, with the loop body two times, or the
// first choice on the path.
struct run {
    std::vector<hop> hops{};
    bool has_choice = false;
};

enum class bag_fault : std::uint8_t {
    none,
    choice,
    repeated_word,
};

// For a repeated word, first and second index the two transmissions in
// the hops of the run.
struct bag_verdict {
    bag_fault fault = bag_fault::none;
    std::size_t first = 0;
    std::size_t second = 0;
};

// Declared and never defined.  A constant evaluation that calls it stops
// the build, so a node that the bag walk does not read is a hard error,
// as a node that a template walk does not know is.  The gates before the
// bag check leave End, Var, Rec and a transmission only.
void bag_walk_meets_an_unknown_node();

// The wire word that the receive of a message expects: the word of the
// keyed Recv that projection writes for the message.
consteval std::uint64_t receive_word(std::meta::info from, std::meta::info label, std::meta::info payload) {
    const std::meta::info message = std::meta::substitute(^^::fixy::session::PeerMsg, {from, label, payload});
    const ::foundation::algebra::transition::wire_word word = ::foundation::algebra::transition::wire_word_of_step(
        ::fixy::session::detail::protocol_registry,
        std::meta::substitute(^^::fixy::session::Recv, {message, ^^::fixy::session::End}));
    if (!word.is_wired) bag_walk_meets_an_unknown_node();
    return word.value;
}

// Complexity: linear in the transmissions of the run.
consteval run run_of(std::meta::info protocol) {
    run result{};
    std::size_t loop_start = 0;
    std::meta::info node = std::meta::dealias(protocol);
    for (;;) {
        if (node == ^^g::End) return result;
        if (node == ^^g::Var) {
            // A pair of transmissions from iterations k and k + m, m > 1,
            // is ordered when the pair from k and k + 1 is: the send in
            // k + m comes after the send of the same transmission in
            // k + 1, in the order of its sender.  So two copies of the
            // body hold every pair that the check must read.
            const std::vector<hop> body(result.hops.begin() + static_cast<std::ptrdiff_t>(loop_start),
                                        result.hops.end());
            result.hops.insert(result.hops.end(), body.begin(), body.end());
            return result;
        }
        if (!std::meta::has_template_arguments(node)) bag_walk_meets_an_unknown_node();
        const std::vector<std::meta::info> args = std::meta::template_arguments_of(node);
        if (std::meta::template_of(node) == ^^g::Rec) {
            loop_start = result.hops.size();
            node = std::meta::dealias(args[0]);
            continue;
        }
        if (std::meta::template_of(node) != ^^g::Comm || args.size() < 3) bag_walk_meets_an_unknown_node();
        if (args.size() > 3) {
            result.has_choice = true;
            return result;
        }
        const std::meta::info branch = std::meta::dealias(args[2]);
        if (!std::meta::has_template_arguments(branch) || std::meta::template_of(branch) != ^^g::Branch) {
            bag_walk_meets_an_unknown_node();
        }
        const std::vector<std::meta::info> parts = std::meta::template_arguments_of(branch);
        const std::meta::info from = std::meta::dealias(args[0]);
        const std::meta::info to = std::meta::dealias(args[1]);
        const std::meta::info label = std::meta::dealias(parts[0]);
        const std::meta::info payload = std::meta::dealias(parts[1]);
        result.hops.push_back(hop{from, to, label, payload, receive_word(from, label, payload)});
        node = std::meta::dealias(parts[2]);
    }
}

// The first pair of transmissions to one receiver with one wire word
// whose order the run does not fix, or the first choice.  For each
// transmission, the sweep keeps the roles whose later events come after
// its receive: its receiver, and each role that then receives from such a
// role.  A later transmission is ordered after the receive when its
// sender is one of those roles.  Complexity: O(n² · r) for n hops and r
// roles.
consteval bag_verdict bag_verdict_of(std::meta::info protocol) {
    const run walked = run_of(protocol);
    if (walked.has_choice) return {bag_fault::choice};
    const std::vector<hop>& hops = walked.hops;
    for (std::size_t first = 0; first < hops.size(); ++first) {
        std::vector<std::meta::info> informed{hops[first].to};
        for (std::size_t second = first + 1; second < hops.size(); ++second) {
            const hop& later = hops[second];
            const bool is_after_receive = std::ranges::contains(informed, later.from);
            if (!is_after_receive && later.to == hops[first].to && later.word == hops[first].word) {
                return {bag_fault::repeated_word, first, second};
            }
            if (is_after_receive && !std::ranges::contains(informed, later.to)) informed.push_back(later.to);
        }
    }
    return {};
}

consteval std::string hop_text(const hop& message) {
    std::string text{std::meta::display_string_of(message.label)};
    text += "(";
    text += std::meta::display_string_of(message.payload);
    text += ") from ";
    text += std::meta::display_string_of(message.from);
    text += " to ";
    text += std::meta::display_string_of(message.to);
    return text;
}

template <typename G>
consteval std::string_view repeated_word_message() {
    const bag_verdict verdict = bag_verdict_of(^^G);
    const run walked = run_of(^^G);
    std::string text =
        "fixy::session::diagnostic [Network_Bag_Repeated_Word]: on a bag the wire word of a message is its label "
        "alone, so a receive cannot tell two messages with one label apart.  The message ";
    text += hop_text(walked.hops[verdict.first]);
    text += " and the message ";
    text += hop_text(walked.hops[verdict.second]);
    text +=
        " go to one receiver with one word, and no receive of the first happens before the send of the second.  "
        "The receive of the first can take the second.  Give the two messages different labels, let the receiver "
        "answer the first before the second is sent, or bind to a per-pair FIFO carrier.";
    return std::define_static_string(text);
}

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
        constexpr bag_fault fault = bag_verdict_of(^^G).fault;
        if constexpr (fault == bag_fault::choice) {
            return NetworkRefusal::ChoiceOnBag;
        } else if constexpr (fault == bag_fault::repeated_word) {
            return NetworkRefusal::RepeatedWordOnBag;
        } else {
            return NetworkRefusal::None;
        }
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
    } else if constexpr (refusal == NetworkRefusal::RepeatedWordOnBag) {
        static_assert(detail::network::dependent_false_v<G, Reliable>,
                      detail::network::repeated_word_message<G>());
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

// RoleA receives from RoleB and then from RoleC, with one label and no
// order between the two sends.
using TwoSenders = global::Msg<RoleB, RoleA, LabelX, int, global::Msg<RoleC, RoleA, LabelX, int, global::End>>;
// RoleA receives from RoleB and then from RoleC, with two labels.
using TwoLabels = global::Msg<RoleB, RoleA, LabelX, int, global::Msg<RoleC, RoleA, LabelY, int, global::End>>;
// RoleA tells RoleC to send after it received from RoleB, so the receive
// orders the two messages with one label.
using Answered = global::Msg<RoleB, RoleA, LabelX, int,
                             global::Msg<RoleA, RoleC, LabelY, int, global::Msg<RoleC, RoleA, LabelX, int, global::End>>>;
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
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoLabels,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::Answered,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::Mailbox>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::PerPairFifo>>;
    using refuses = witnesses<
        int,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoSenders,
                                          ::fixy::session::Network::Mailbox>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::TwoSenders,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::LoopChoice,
                                          ::fixy::session::Network::Bag>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::OneMessage,
                                          ::fixy::session::Network::Mailbox, ::fixy::session::NoReliableRoles>,
        ::fixy::session::Implementability<::fixy::session::detail::network::witness::OneMessage,
                                          ::fixy::session::Network::PerPairFifo, int>>;
};
