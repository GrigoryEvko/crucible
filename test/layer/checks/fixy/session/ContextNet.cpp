// The compile-time checks of fixy/session/ContextNet.h.

#include <fixy/session/ContextNet.h>

#include <cstddef>
#include <cstdint>

namespace fixy::session::detail::context_net_self_test {

namespace g = ::fixy::session::global;
namespace net = ::foundation::algebra::net;

// The roles and the labels have external linkage, because a label word is a
// stable id, and a stable id refuses a type with internal linkage.
struct P {};
struct Q {};
struct R {};
struct X {};
struct Y {};

// Channel types that state a capacity.
struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};
struct TwoSlots {
    static constexpr std::size_t channel_capacity = 2;
};

template <typename Role, typename Local>
using At = RoleState<Role, OutQueue<>, Local>;
template <typename Peer, typename Label, typename Next>
using Out = Send<PeerMsg<Peer, Label, int>, Next>;
template <typename Peer, typename Label, typename Next>
using In = Recv<PeerMsg<Peer, Label, int>, Next>;

// ── The contexts of the attack test ─────────────────────────────────────
//
// test/fixy/test_session_global_attack_hand.cpp makes these contexts from two
// live sessions, and its explorer finds the deadlock of each crossed one.

using SessionOne = g::Msg<P, Q, X, int, g::End>;
using SessionTwo = g::Msg<Q, P, Y, int, g::End>;
using ProcessP = compose_t<typename project_t<SessionTwo, P>::local, typename project_t<SessionOne, P>::local>;
using ProcessQ = compose_t<typename project_t<SessionOne, Q>::local, typename project_t<SessionTwo, Q>::local>;
using CrossedSessions = TypingContext<At<P, ProcessP>, At<Q, ProcessQ>>;
using OrderedP = compose_t<typename project_t<SessionOne, P>::local, typename project_t<SessionTwo, P>::local>;
using OrderedSessions = TypingContext<At<P, OrderedP>, At<Q, ProcessQ>>;

using SessionPQ = g::Msg<P, Q, X, int, g::End>;
using SessionQR = g::Msg<Q, R, X, int, g::End>;
using SessionRP = g::Msg<R, P, X, int, g::End>;
using RingP = compose_t<typename project_t<SessionRP, P>::local, typename project_t<SessionPQ, P>::local>;
using RingQ = compose_t<typename project_t<SessionPQ, Q>::local, typename project_t<SessionQR, Q>::local>;
using RingR = compose_t<typename project_t<SessionQR, R>::local, typename project_t<SessionRP, R>::local>;
using CrossedRing = TypingContext<At<P, RingP>, At<Q, RingQ>, At<R, RingR>>;

// ── The expected systems ────────────────────────────────────────────────
//
// The hand-built systems of test/layer/checks/foundation/algebra/net/
// Priority.cpp for the same contexts, with each channel named by its two
// roles and with the capacity of OneSlot.

[[nodiscard]] consteval net::Action send_on(net::ProcessId process, net::ChannelId channel) noexcept {
    return net::Action{.process = process, .channel = channel, .move = net::Move::Send};
}
[[nodiscard]] consteval net::Action receive_on(net::ProcessId process, net::ChannelId channel) noexcept {
    return net::Action{.process = process, .channel = channel, .move = net::Move::Receive};
}

inline constexpr net::ProcessId process_p{0};
inline constexpr net::ProcessId process_q{1};
inline constexpr net::ProcessId process_r{2};
inline constexpr net::ChannelId first_channel{0};
inline constexpr net::ChannelId second_channel{1};
inline constexpr net::ChannelId third_channel{2};

// The channels of two roles: P to Q first, because P stands first.
inline constexpr net::ChannelId s1 = first_channel;
inline constexpr net::ChannelId s2 = second_channel;

inline constexpr net::ProcessSystem<2, 2, 4> crossed_sessions_system{
    .process_names = {"P", "Q"},
    .channels = {{.name = "P to Q", .capacity = 1}, {.name = "Q to P", .capacity = 1}},
    .actions = {receive_on(process_p, s2), send_on(process_p, s1), receive_on(process_q, s1), send_on(process_q, s2)}};

inline constexpr net::ProcessSystem<2, 2, 4> ordered_sessions_system{
    .process_names = {"P", "Q"},
    .channels = {{.name = "P to Q", .capacity = 1}, {.name = "Q to P", .capacity = 1}},
    .actions = {send_on(process_p, s1), receive_on(process_p, s2), receive_on(process_q, s1), send_on(process_q, s2)}};

inline constexpr net::ChannelId pq = first_channel;
inline constexpr net::ChannelId qr = second_channel;
inline constexpr net::ChannelId rp = third_channel;

inline constexpr net::ProcessSystem<3, 3, 6> crossed_ring_system{
    .process_names = {"P", "Q", "R"},
    .channels = {{.name = "P to Q", .capacity = 1},
                 {.name = "Q to R", .capacity = 1},
                 {.name = "R to P", .capacity = 1}},
    .actions = {receive_on(process_p, rp), send_on(process_p, pq), receive_on(process_q, pq), send_on(process_q, qr),
                receive_on(process_r, qr), send_on(process_r, rp)}};

// Two systems are the same system when each name, each capacity and each
// action is the same.  The two arguments can swap with no change of meaning.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval bool is_same_system(net::ProcessSystem<Processes, Channels, Actions> const& made,
                                            net::ProcessSystem<Processes, Channels, Actions> const& expected) noexcept {
    for (std::size_t place = 0; place < Processes; ++place) {
        if (!(made.process_names[place] == expected.process_names[place])) return false;
    }
    for (std::size_t place = 0; place < Channels; ++place) {
        if (!(made.channels[place].name == expected.channels[place].name)) return false;
        if (made.channels[place].capacity != expected.channels[place].capacity) return false;
    }
    for (std::size_t place = 0; place < Actions; ++place) {
        net::Action const& left = made.actions[place];
        net::Action const& right = expected.actions[place];
        if (left.process != right.process || left.channel != right.channel || left.move != right.move) return false;
    }
    return true;
}

static_assert(is_same_system(marked_system_of_context<CrossedSessions, OneSlot>(), crossed_sessions_system));
static_assert(is_same_system(marked_system_of_context<OrderedSessions, OneSlot>(), ordered_sessions_system));
static_assert(is_same_system(marked_system_of_context<CrossedRing, OneSlot>(), crossed_ring_system));

// The verdicts of the attack contexts.  No capacity helps the crossed ones,
// because each role receives first.
static_assert(check_context_deadlock_freedom<CrossedSessions, OneSlot>().says(
    "liveness: a cycle of waits holds no token: P receives 'Q to P' step 0 <- Q sends 'Q to P' step 0 <- Q "
    "receives 'P to Q' step 0 <- P sends 'P to Q' step 0.  No action of the cycle can start."));
static_assert(check_context_deadlock_freedom<CrossedSessions, TwoSlots>().says(
    "liveness: a cycle of waits holds no token: P receives 'Q to P' step 0 <- Q sends 'Q to P' step 0 <- Q "
    "receives 'P to Q' step 0 <- P sends 'P to Q' step 0.  No action of the cycle can start."));
static_assert(check_context_deadlock_freedom<CrossedRing, OneSlot>().says(
    "liveness: a cycle of waits holds no token: P receives 'R to P' step 0 <- R sends 'R to P' step 0 <- R "
    "receives 'Q to R' step 0 <- Q sends 'Q to R' step 0 <- ....  No action of the cycle can start."));
static_assert(check_context_deadlock_freedom<OrderedSessions, OneSlot>().is_admitted());
static_assert(check_context_deadlock_freedom<OrderedSessions, TwoSlots>().is_admitted());

// The door reads the system with the plain names, and the system with the
// names of the roles gives the same verdict.
static_assert(!detail::context_net::is_context_live_v<CrossedSessions, OneSlot>);
static_assert(!detail::context_net::is_context_live_v<CrossedRing, OneSlot>);
static_assert(detail::context_net::is_context_live_v<OrderedSessions, OneSlot>);
static_assert(
    detail::context_net::marked_system_of_context_<OrderedSessions, OneSlot, detail::context_net::SystemNames::Plain>()
        .channels[0]
        .name
    == ::foundation::algebra::net::NetName{"channel"});

// ── A verdict that the capacity decides ─────────────────────────────────
//
// P sends two messages to Q and then one to R.  R forwards one to Q, and Q
// waits on R first.  A queue of two messages admits it.  A queue of one
// makes the second send of P wait for a receive of Q that waits on R.

using WideP = Out<Q, X, Out<Q, X, Out<R, Y, End>>>;
using WideR = In<P, Y, Out<Q, Y, End>>;
using WideQ = In<R, Y, In<P, X, In<P, X, End>>>;
using WideQueue = TypingContext<At<P, WideP>, At<Q, WideQ>, At<R, WideR>>;
static_assert(check_context_deadlock_freedom<WideQueue, TwoSlots>().is_admitted());
static_assert(check_context_deadlock_freedom<WideQueue, OneSlot>().says(
    "liveness: a cycle of waits holds no token: P sends 'P to Q' step 1 <- Q receives 'P to Q' step 0 <- Q "
    "receives 'R to Q' step 0 <- R sends 'R to Q' step 0 <- ....  No action of the cycle can start."));

// ── The other verdicts ──────────────────────────────────────────────────

// A receive whose peer never sends.
using NoSend = TypingContext<At<P, In<Q, X, End>>, At<Q, End>>;
static_assert(check_context_deadlock_freedom<NoSend, OneSlot>().says(
    "liveness: channel 'Q to P' has 0 sends and 1 receives.  Each send has its receive."));

// A context with no action has no wait.
static_assert(check_context_deadlock_freedom<TypingContext<At<P, End>, At<Q, End>>, OneSlot>().is_admitted());
static_assert(check_context_deadlock_freedom<TypingContext<>, OneSlot>().is_admitted());

// A role can send to itself, as a channel of the marked system can.
using SelfQueue = TypingContext<At<P, Out<P, X, In<P, X, End>>>>;
static_assert(check_context_deadlock_freedom<SelfQueue, OneSlot>().is_admitted());

// ── Roles that loop ─────────────────────────────────────────────────────
//
// The body of a Loop around the whole local type is one period of its role.

// A stream: P sends in each period, and Q receives in each period.
using Stream = TypingContext<At<P, Loop<Out<Q, X, Continue>>>, At<Q, Loop<In<P, X, Continue>>>>;
inline constexpr net::ProcessSystem<2, 1, 2> stream_system{
    .process_names = {"P", "Q"},
    .channels = {{.name = "P to Q", .capacity = 1}},
    .actions = {send_on(process_p, first_channel), receive_on(process_q, first_channel)}};
static_assert(is_same_system(marked_system_of_context<Stream, OneSlot>(), stream_system));
static_assert(check_context_deadlock_freedom<Stream, OneSlot>().is_admitted());

// A request and its answer in each period.
using PingPong = TypingContext<At<P, Loop<Out<Q, X, In<Q, Y, Continue>>>>, At<Q, Loop<In<P, X, Out<P, Y, Continue>>>>>;
static_assert(check_context_deadlock_freedom<PingPong, OneSlot>().is_admitted());

// Each role waits on the other first in each period.
using CrossedLoops =
    TypingContext<At<P, Loop<In<Q, Y, Out<Q, X, Continue>>>>, At<Q, Loop<In<P, X, Out<P, Y, Continue>>>>>;
static_assert(check_context_deadlock_freedom<CrossedLoops, TwoSlots>().says(
    "liveness: a cycle of waits holds no token: P receives 'Q to P' step 0 <- Q sends 'Q to P' step 0 <- Q "
    "receives 'P to Q' step 0 <- P sends 'P to Q' step 0.  No action of the cycle can start."));

// The ring of Priority.cpp in a loop: each role sends to its right and then
// receives from its left.
using LoopRing = TypingContext<At<P, Loop<Out<Q, X, In<R, X, Continue>>>>, At<Q, Loop<In<P, X, Out<R, X, Continue>>>>,
                               At<R, Loop<In<Q, X, Out<P, X, Continue>>>>>;
static_assert(check_context_deadlock_freedom<LoopRing, OneSlot>().is_admitted());

// One period sends one message, and the other period receives two.
using UnevenPeriods = TypingContext<At<P, Loop<Out<Q, X, Continue>>>, At<Q, Loop<In<P, X, In<P, X, Continue>>>>>;
static_assert(check_context_deadlock_freedom<UnevenPeriods, OneSlot>().says(
    "liveness: channel 'P to Q' has 1 sends and 2 receives.  Each send has its receive."));

// A Loop whose body reaches End and no Continue runs one time.
using LoopOnce = TypingContext<At<P, Loop<Out<Q, X, End>>>, At<Q, In<P, X, End>>>;
static_assert(check_context_deadlock_freedom<LoopOnce, OneSlot>().is_admitted());

// ── Choices ─────────────────────────────────────────────────────────────
//
// The arms of a choice that wait the same way are one sequence of actions.

// The Ring global type of section 1.2 of the attack test: P sends Add to Q,
// Q chooses Add or Sub for R, and R answers P with the same label.  Each
// choice has two arms with the same waits.
struct Add {};
struct Sub {};
using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using RingContext = projected_context_t<Ring>;
inline constexpr net::ProcessSystem<3, 3, 6> ring_system{
    .process_names = {"P", "Q", "R"},
    .channels = {{.name = "P to Q", .capacity = 1},
                 {.name = "Q to R", .capacity = 1},
                 {.name = "R to P", .capacity = 1}},
    .actions = {send_on(process_p, pq), receive_on(process_p, rp), receive_on(process_q, pq), send_on(process_q, qr),
                receive_on(process_r, qr), send_on(process_r, rp)}};
static_assert(is_same_system(marked_system_of_context<RingContext, OneSlot>(), ring_system));
static_assert(check_context_deadlock_freedom<RingContext, OneSlot>().is_admitted());

// A choice inside one arm: each arm sends two messages to Q.
using NestedP = Select<Out<Q, X, Select<Out<Q, X, End>, Out<Q, Y, End>>>, Out<Q, Y, Out<Q, X, End>>>;
using NestedQ = Offer<Sender<P>, In<P, X, Offer<Sender<P>, In<P, X, End>, In<P, Y, End>>>, In<P, Y, In<P, X, End>>>;
using NestedChoices = TypingContext<At<P, NestedP>, At<Q, NestedQ>>;
static_assert(marked_system_of_context<NestedChoices, OneSlot>().actions[1].move == net::Move::Send);
static_assert(check_context_deadlock_freedom<NestedChoices, OneSlot>().is_admitted());

// The arms of one role wait on two channels.  Two branches of one choice
// carry two labels, because a choice with one label two times is not well
// formed.
using TwoChannels =
    TypingContext<At<P, Select<Out<Q, X, End>, Out<R, Y, End>>>, At<Q, In<P, X, End>>, At<R, In<P, Y, End>>>;
static_assert(find_context_fault<TwoChannels>().code == ContextFaultCode::ArmsWaitDifferently);
static_assert(find_context_fault<TwoChannels>().type == ^^Select<Out<Q, X, End>, Out<R, Y, End>>);

// A loop with an exit: one arm continues, and the other ends.
using ExitP = Loop<Select<Out<Q, X, Continue>, Out<Q, Y, End>>>;
using ExitQ = Loop<Offer<Sender<P>, In<P, X, Continue>, In<P, Y, End>>>;
using LoopWithExit = TypingContext<At<P, ExitP>, At<Q, ExitQ>>;
static_assert(find_context_fault<LoopWithExit>().code == ContextFaultCode::ArmsWaitDifferently);
static_assert(check_context_deadlock_freedom<LoopWithExit, OneSlot>().starts_with(
    "context: a choice of the role 'P' has arms that wait on other channels, or that end in other ways: "
    "fixy::session::Select<"));

// ── The faults ──────────────────────────────────────────────────────────

static_assert(find_context_fault<int>().code == ContextFaultCode::NotAContext);
static_assert(check_context_deadlock_freedom<int, OneSlot>().says(
    "context: the type is no TypingContext of RoleState entries: int."));
static_assert(find_context_fault<TypingContext<int>>().code == ContextFaultCode::NotAContext);

using TwoEntries = TypingContext<At<P, End>, At<P, End>>;
static_assert(find_context_fault<TwoEntries>().code == ContextFaultCode::RoleTwice);
static_assert(check_context_deadlock_freedom<TwoEntries, OneSlot>().says(
    "context: the role 'P' stands two times in the context.  Each role has one entry."));

using Queued = TypingContext<RoleState<P, OutQueue<::fixy::session::Queued<Q, X, int>>, End>, At<Q, In<P, X, End>>>;
static_assert(find_context_fault<Queued>().code == ContextFaultCode::QueuedMessage);
static_assert(check_context_deadlock_freedom<Queued, OneSlot>().says(
    "context: the role 'P' holds a message in its queue.  The check reads a context whose queues are empty."));

using BinaryStep = TypingContext<At<P, Send<int, End>>, At<Q, End>>;
static_assert(find_context_fault<BinaryStep>().code == ContextFaultCode::NodeOutsideModel);
static_assert(check_context_deadlock_freedom<BinaryStep, OneSlot>().says(
    "context: the local type of the role 'P' holds a node that the check does not read: "
    "fixy::session::Send<int, fixy::session::End>.  The check reads End, one Loop around the whole type with its "
    "Continue, the Send and Recv steps of a PeerMsg, and a Select or an Offer whose arms wait the same way."));

using Stranger = TypingContext<At<P, Out<R, X, End>>, At<Q, End>>;
static_assert(find_context_fault<Stranger>().code == ContextFaultCode::PeerOutsideContext);
static_assert(check_context_deadlock_freedom<Stranger, OneSlot>().says(
    "context: the local type of the role 'P' names the peer 'R', which is no role of the context."));

// A step before the loop: the loop is not the whole local type.
using StepBeforeLoop = TypingContext<At<P, Out<Q, X, Loop<Out<Q, X, Continue>>>>, At<Q, Loop<In<P, X, Continue>>>>;
static_assert(find_context_fault<StepBeforeLoop>().code == ContextFaultCode::LoopNotWhole);
static_assert(find_context_fault<StepBeforeLoop>().type == ^^Loop<Out<Q, X, Continue>>);
static_assert(check_context_deadlock_freedom<StepBeforeLoop, OneSlot>().starts_with(
    "context: the local type of the role 'P' holds a loop that is not one Loop around the whole type with an "
    "action in its period: fixy::session::Loop<"));

// A loop inside the loop.
using LoopInLoop = TypingContext<At<P, Loop<Out<Q, X, Loop<Out<Q, Y, Continue>>>>>, At<Q, Loop<In<P, X, Continue>>>>;
static_assert(find_context_fault<LoopInLoop>().code == ContextFaultCode::LoopNotWhole);
static_assert(find_context_fault<LoopInLoop>().type == ^^Loop<Out<Q, Y, Continue>>);

// P sends for ever, and Q receives one message and ends.  The counts of one
// period agree, and the second send of P waits for ever.
using StreamToRun = TypingContext<At<P, Loop<Out<Q, X, Continue>>>, At<Q, In<P, X, End>>>;
static_assert(find_context_fault<StreamToRun>().code == ContextFaultCode::LoopMeetsRun);
static_assert(check_context_deadlock_freedom<StreamToRun, OneSlot>().says(
    "context: the role 'P' loops, the role 'Q' runs one time, and a channel joins them.  Along each channel the two "
    "roles loop, or the two run one time."));

}  // namespace fixy::session::detail::context_net_self_test
