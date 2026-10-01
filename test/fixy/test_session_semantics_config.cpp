// The rules of Figure 8 of the crash-stop paper, the transition system of
// the configurations, one small state at a time.

#include "session_semantics.h"

#include <type_traits>

namespace test_session_semantics {

// ── The configuration rules (Figure 8) ───────────────────────────────

using RecvMGuarded =
    s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M, int>, s::End>, s::Recv<s::PeerMsg<P, Crash, void>, s::End>>;

template <typename QueueP, typename LocalP, typename LocalQ>
using PQ = s::TypingContext<s::RoleState<P, QueueP, LocalP>, s::RoleState<Q, s::OutQueue<>, LocalQ>>;

// [Γ-⊕] puts the message on the queue of the sender, and [Γ-&] takes it.
using Fresh = PQ<s::OutQueue<>, SendM, RecvM>;
using Pending = PQ<s::OutQueue<s::Queued<Q, M, int>>, s::End, RecvM>;
static_assert(std::is_same_v<c::enabled_t<Fresh, Reliable>, g::Actions<g::SendAction<P, Q, M, int>>>);
static_assert(std::is_same_v<c::step_t<Fresh, g::SendAction<P, Q, M, int>, Reliable>, Pending>);
static_assert(std::is_same_v<c::enabled_t<Pending, Reliable>, g::Actions<g::RecvAction<Q, P, M, int>>>);
static_assert(
    std::is_same_v<c::step_t<Pending, g::RecvAction<Q, P, M, int>, Reliable>, PQ<s::OutQueue<>, s::End, s::End>>);
static_assert(std::is_same_v<c::step_t<Fresh, g::RecvAction<Q, P, M, int>, Reliable>, g::NoTransition>);
// A message whose label no branch has stays in the queue.
static_assert(
    std::is_same_v<c::enabled_t<PQ<s::OutQueue<s::Queued<Q, M1, int>>, s::End, RecvM>, Reliable>, g::Actions<>>);

// [Γ-↯] stops the role and drops each message to it.  A message to a
// stopped role is dropped too.  End and Stop never crash, and a reliable
// role never crashes.
using FreshGuarded = PQ<s::OutQueue<>, SendM, RecvMGuarded>;
static_assert(same_labels_v<c::enabled_t<FreshGuarded, s::ReliableSet<>>,
                            g::Actions<g::SendAction<P, Q, M, int>, g::CrashAction<P>, g::CrashAction<Q>>>);
static_assert(same_labels_v<c::enabled_t<FreshGuarded, s::ReliableSet<Q>>,
                            g::Actions<g::SendAction<P, Q, M, int>, g::CrashAction<P>>>);
using QStopped = PQ<s::OutQueue<>, SendM, s::Stop>;
static_assert(std::is_same_v<c::step_t<FreshGuarded, g::CrashAction<Q>, s::ReliableSet<>>, QStopped>);
static_assert(std::is_same_v<c::step_t<QStopped, g::SendAction<P, Q, M, int>, s::ReliableSet<>>,
                             PQ<s::OutQueue<>, s::End, s::Stop>>);
static_assert(std::is_same_v<c::step_t<PQ<s::OutQueue<s::Queued<Q, M, int>>, s::End, RecvMGuarded>, g::CrashAction<Q>,
                                       s::ReliableSet<>>,
                             PQ<s::OutQueue<>, s::End, s::Stop>>);
static_assert(std::is_same_v<c::enabled_t<PQ<s::OutQueue<>, s::End, s::Stop>, s::ReliableSet<>>, g::Actions<>>);

// [Γ-⊙]: Q detects the crash of P when P is at Stop and no message of P
// to Q waits.  A message that P sent before it crashed comes first.
using PStoppedEmpty = PQ<s::OutQueue<>, s::Stop, RecvMGuarded>;
using PStoppedSent = PQ<s::OutQueue<s::Queued<Q, M, int>>, s::Stop, RecvMGuarded>;
static_assert(std::is_same_v<c::enabled_t<PStoppedEmpty, s::ReliableSet<Q>>, g::Actions<g::DetectAction<Q, P>>>);
static_assert(std::is_same_v<c::step_t<PStoppedEmpty, g::DetectAction<Q, P>, s::ReliableSet<Q>>,
                             PQ<s::OutQueue<>, s::Stop, s::End>>);
static_assert(std::is_same_v<c::enabled_t<PStoppedSent, s::ReliableSet<Q>>, g::Actions<g::RecvAction<Q, P, M, int>>>);
// Without a crash branch there is nothing to detect.
static_assert(std::is_same_v<c::enabled_t<PQ<s::OutQueue<>, s::Stop, RecvM>, s::ReliableSet<Q>>, g::Actions<>>);

// [Γ-µ]: a Loop acts as its unfolding, and the Continue becomes the Loop.
using Pinger = s::Loop<s::Send<s::PeerMsg<Q, M, int>, s::Continue>>;
using Ponger = s::Loop<s::Recv<s::PeerMsg<P, M, int>, s::Continue>>;
static_assert(std::is_same_v<c::step_t<PQ<s::OutQueue<>, Pinger, Ponger>, g::SendAction<P, Q, M, int>, Reliable>,
                             PQ<s::OutQueue<s::Queued<Q, M, int>>, Pinger, Ponger>>);

// A received payload can be below the payload of its branch.
using Narrow = ::fixy::Tagged<int, ::fixy::tags::source::Sanitized>;
static_assert(std::is_same_v<
              c::enabled_t<PQ<s::OutQueue<s::Queued<Q, M, Narrow>>, s::End, s::Recv<s::PeerMsg<P, M, int>, s::End>>,
                           Reliable>,
              g::Actions<g::RecvAction<Q, P, M, Narrow>>>);

}  // namespace test_session_semantics
