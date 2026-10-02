// The network model of a carrier, and implementable_on.
//
// The corpus types below are the rows of the session oracle
// (test/session_oracle/golden.csv) on which Sprout(A), the tool of Li and
// Wies (PLDI 2026), refuses a network that fixy's projection accepted.
// Each projects onto every role, so each is admitted on per-pair FIFO.
// Twelve are refused on a mailbox, because a receiver has two senders.
// Three are refused on a bag, because they hold a choice.  Each is also
// checked on another network, so a change that refuses everything fails
// here too.
//
// Four of the twelve are refused on a bag too, where Sprout(A) admits
// them: two messages go to one receiver with one label, and no receive
// orders them.  Our wire word is the label alone, and the message of
// Sprout(A) also carries the sender and the payload type.
//
// The file also checks the declaration of a carrier, and that a session
// mint refuses a local type that names two peers.

#include <fixy/session/Handle.h>
#include <fixy/session/Network.h>
#include <fixy/session/Projection.h>

#include <cstdio>
#include <type_traits>
#include "../test_assert.h"

namespace session_network_test {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct R0 {};
struct R1 {};
struct R2 {};
struct R3 {};
struct Nat {};
struct Bool {};
struct Unit {};
struct Val {};
template <unsigned I>
struct Label {};

// The encoding of the oracle: msg(p,q,T,K) is one message with the label
// Val, and branch(p,q,[K0,K1,...]) labels its arms 0, 1, and so on.
template <typename From, typename To, typename Payload, typename Cont>
using Msg = g::Comm<From, To, g::Branch<Val, Payload, Cont>>;
template <unsigned I, typename Cont>
using Arm = g::Branch<Label<I>, Unit, Cont>;
template <typename From, typename To, typename... Arms>
using Choice = g::Comm<From, To, Arms...>;
template <typename Body>
using Rec = g::Rec<Body>;
using End = g::End;
using Var = g::Var;

// ── The twelve types that a mailbox refuses ──────────────────────────

using R9 = Msg<R1, R0, Nat, Msg<R2, R0, Bool, Msg<R0, R2, Nat, Msg<R1, R2, Nat, Msg<R2, R1, Nat, End>>>>>;
using R14 = Msg<R1, R2, Bool, Msg<R0, R2, Nat, Msg<R0, R2, Bool, End>>>;
using R17 = Msg<R0, R2, Bool, Msg<R1, R2, Bool, Msg<R0, R2, Bool, End>>>;
using R31 = Rec<Msg<R1, R2, Nat, Msg<R1, R0, Nat, Msg<R2, R0, Nat, Var>>>>;
using R41 = Msg<R1, R0, Bool, Msg<R2, R0, Bool, End>>;
using R59 = Msg<R2, R0, Nat, Msg<R1, R0, Bool, Msg<R2, R0, Bool, End>>>;
using R83 = Rec<Msg<R2, R0, Nat, Msg<R1, R0, Bool, Msg<R1, R0, Nat, Rec<Msg<R0, R2, Bool, End>>>>>>;
using R86 = Msg<R0, R2, Bool, Msg<R1, R2, Nat, End>>;
using R90 = Msg<R2, R1, Nat, Rec<Msg<R0, R2, Bool, Msg<R0, R1, Nat, Var>>>>;
using R92 = Msg<R0, R1, Nat, Msg<R0, R2, Nat, Msg<R1, R2, Nat, Msg<R0, R1, Bool, Msg<R1, R2, Bool, End>>>>>;
using A36 = Choice<R2, R1,
                   Arm<0, Msg<R0, R1, Nat,
                              Msg<R2, R0, Bool,
                                  Msg<R0, R2, Nat,
                                      Choice<R0, R2, Arm<0, Msg<R2, R1, Nat, Rec<Choice<R2, R1, Arm<0, End>>>>>,
                                             Arm<1, Rec<Choice<R2, R1, Arm<0, Var>, Arm<1, Var>>>>>>>>>>;
using A112 = Rec<Rec<Msg<R1, R0, Bool, Msg<R2, R0, Bool, End>>>>;

// ── The three types that a bag refuses ───────────────────────────────

using R65 = Rec<Choice<R0, R1, Arm<0, Var>, Arm<1, Msg<R1, R0, Nat, End>>>>;
using TiroreEq3 = Msg<R0, R1, Nat, Rec<Choice<R2, R3, Arm<0, End>, Arm<1, Var>>>>;
using M7 = Rec<Choice<R0, R1, Arm<0, Var>, Arm<1, End>>>;

template <typename G>
inline constexpr bool refused_on_mailbox_only_v =
    s::implementable_on_v<G, s::Network::PerPairFifo> && !s::implementable_on_v<G, s::Network::Mailbox>
    && s::network_refusal_v<G, s::Network::Mailbox> == s::NetworkRefusal::ReceiverHasTwoSenders;

static_assert(refused_on_mailbox_only_v<R9>);
static_assert(refused_on_mailbox_only_v<R14>);
static_assert(refused_on_mailbox_only_v<R17>);
static_assert(refused_on_mailbox_only_v<R31>);
static_assert(refused_on_mailbox_only_v<R41>);
static_assert(refused_on_mailbox_only_v<R59>);
static_assert(refused_on_mailbox_only_v<R83>);
static_assert(refused_on_mailbox_only_v<R86>);
static_assert(refused_on_mailbox_only_v<R90>);
static_assert(refused_on_mailbox_only_v<R92>);
static_assert(refused_on_mailbox_only_v<A36>);
static_assert(refused_on_mailbox_only_v<A112>);

template <typename G>
inline constexpr bool refused_on_bag_only_v =
    s::implementable_on_v<G, s::Network::PerPairFifo> && !s::implementable_on_v<G, s::Network::Bag>
    && s::network_refusal_v<G, s::Network::Bag> == s::NetworkRefusal::ChoiceOnBag;

static_assert(refused_on_bag_only_v<R65>);
static_assert(refused_on_bag_only_v<TiroreEq3>);
static_assert(refused_on_bag_only_v<M7>);

// A bag refuses two messages to one receiver with one label that no
// receive orders.  R86 sends a Bool from R0 and then a Nat from R1 to R2,
// so the first receive of R2 can take the Nat.  R41 and A112 send one
// label from two senders to R0.  In R31, R1 and R2 each send to R0 in
// each iteration, and R0 answers neither.
template <typename G>
inline constexpr bool refused_for_a_repeated_word_v =
    s::implementable_on_v<G, s::Network::PerPairFifo>
    && s::network_refusal_v<G, s::Network::Bag> == s::NetworkRefusal::RepeatedWordOnBag;

static_assert(refused_for_a_repeated_word_v<R41>);
static_assert(refused_for_a_repeated_word_v<R86>);
static_assert(refused_for_a_repeated_word_v<R31>);
static_assert(refused_for_a_repeated_word_v<A112>);

// A bag admits one label twice when a receive orders the two messages,
// and two labels with no order.  The branch of a choice with one arm has
// the label Label<0>, not Val.
using Answered = Msg<R0, R1, Nat, Msg<R1, R0, Bool, Msg<R0, R1, Bool, End>>>;
using TwoLabels = Msg<R0, R1, Nat, Choice<R0, R1, Arm<0, End>>>;
static_assert(s::implementable_on_v<Answered, s::Network::Bag>);
static_assert(s::implementable_on_v<TwoLabels, s::Network::Bag>);

// Where each receiver has one sender, a mailbox admits the bag types.
static_assert(s::implementable_on_v<M7, s::Network::Mailbox>);
static_assert(s::implementable_on_v<R65, s::Network::Mailbox>);
static_assert(s::implementable_on_v<TiroreEq3, s::Network::Mailbox>);

// A choice refuses a bag even when every receiver has one sender, and a
// second sender refuses a mailbox even without a choice.
static_assert(!s::implementable_on_v<A36, s::Network::Bag>);

// ── The earlier refusals ─────────────────────────────────────────────

// A type that one role cannot project is refused on every network.
using NoMerge = Choice<R0, R1, Arm<0, Msg<R2, R0, Nat, End>>, Arm<1, End>>;
static_assert(s::network_refusal_v<NoMerge, s::Network::PerPairFifo> == s::NetworkRefusal::RoleNotProjectable);
static_assert(!s::implementable_on_v<NoMerge, s::Network::Mailbox>);
static_assert(!s::implementable_on_v<NoMerge, s::Network::Bag>);

// A runtime state is not a protocol, so no carrier binds it.
using InFlight = g::EnRoute<R0, R1, Val, Nat, End>;
static_assert(s::network_refusal_v<InFlight, s::Network::PerPairFifo> == s::NetworkRefusal::RuntimeConstruct);

// A crash-stop protocol binds to per-pair FIFO only.
using Detects = g::Comm<R0, R1, g::Branch<Val, Nat, End>, g::Branch<g::CrashLabel, void, End>>;
static_assert(s::implementable_on_v<Detects, s::Network::PerPairFifo, s::NoReliableRoles>);
static_assert(s::network_refusal_v<Detects, s::Network::Mailbox, s::NoReliableRoles>
              == s::NetworkRefusal::CrashStopOffPerPairFifo);
static_assert(s::network_refusal_v<Detects, s::Network::Bag, s::NoReliableRoles>
              == s::NetworkRefusal::CrashStopOffPerPairFifo);

// A set of reliable roles that is not a ReliableSet is refused.
static_assert(s::network_refusal_v<R41, s::Network::PerPairFifo, int> == s::NetworkRefusal::NotAReliableSet);

// ── The declaration of a carrier ─────────────────────────────────────

struct PairQueues {
    static constexpr s::Network session_network = s::Network::PerPairFifo;
};
struct SharedMailbox {
    static constexpr s::Network session_network = s::Network::Mailbox;
};
struct SharedBag {
    static constexpr s::Network session_network = s::Network::Bag;
};
struct Silent {};
struct CountedNotTyped {
    static constexpr unsigned session_network = 1;
};

static_assert(s::has_session_network(^^PairQueues));
static_assert(s::has_session_network(^^PairQueues&));
static_assert(s::has_session_network(^^const SharedBag));
static_assert(!s::has_session_network(^^Silent));
static_assert(!s::has_session_network(^^CountedNotTyped));
static_assert(s::session_network(^^SharedMailbox) == s::Network::Mailbox);

static_assert(s::CarrierImplements<R41, PairQueues>);
static_assert(!s::CarrierImplements<R41, SharedBag>);
static_assert(s::CarrierImplements<Answered, SharedBag>);
static_assert(!s::CarrierImplements<R41, SharedMailbox>);
static_assert(s::CarrierImplements<M7, SharedMailbox>);
static_assert(!s::CarrierImplements<M7, SharedBag>);
static_assert(!s::CarrierImplements<R41, Silent>);
static_assert(!s::CarrierImplements<R41, CountedNotTyped>);
static_assert(s::CarrierImplements<Detects, PairQueues, s::NoReliableRoles>);
static_assert(!s::CarrierImplements<Detects, SharedBag, s::NoReliableRoles>);

// ── A mint takes a local type with one peer at most ──────────────────

// The projection of R41 onto R0 receives from R1 and then from R2.
using ReceivesFromTwo = typename s::project_t<R41, R0>::local;
// The projection of R41 onto R1 sends to R0 only.
using SendsToOne = typename s::project_t<R41, R1>::local;

static_assert(!s::NamesAtMostOnePeer<ReceivesFromTwo>);
static_assert(s::NamesAtMostOnePeer<SendsToOne>);
static_assert(!s::WellFormedRunnableProtocol<ReceivesFromTwo>);
static_assert(s::WellFormedRunnableProtocol<SendsToOne>);
// The binary view names no peer.
static_assert(s::NamesAtMostOnePeer<s::strip_peers_t<SendsToOne>>);
static_assert(s::NamesAtMostOnePeer<s::End>);

}  // namespace session_network_test

int main() {
    namespace s = ::fixy::session;
    namespace t = ::session_network_test;
    // The verdicts are values as well as types.  A read at run time
    // checks that the constants this file asserts are the ones a caller
    // sees.
    const bool mailbox_refuses_r41 = !s::implementable_on_v<t::R41, s::Network::Mailbox>;
    const bool bag_refuses_m7 = !s::implementable_on_v<t::M7, s::Network::Bag>;
    const bool bag_refuses_r86 = !s::implementable_on_v<t::R86, s::Network::Bag>;
    const bool pair_queues_admit_r41 = s::CarrierImplements<t::R41, t::PairQueues>;
    if (!mailbox_refuses_r41 || !bag_refuses_m7 || !bag_refuses_r86 || !pair_queues_admit_r41) {
        std::fputs("test_session_network: a network verdict differs at run time\n", stderr);
        return 1;
    }
    crucible::test::pass("test_session_network: ok\n");
    return 0;
}
