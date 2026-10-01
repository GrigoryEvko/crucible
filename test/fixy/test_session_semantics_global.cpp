// The rules of Figure 7 of the crash-stop paper, the transition system of
// the global types, one small state at a time, and Remark 4.13 as the paper
// writes it.

#include "session_semantics.h"

#include <type_traits>

namespace test_session_semantics {

// ── [GR-⊕], [GR-&] and [GR-µ] ────────────────────────────────────────

using OnceSent = g::EnRoute<P, Q, M, int, g::End>;
static_assert(std::is_same_v<g::state_enabled_t<Start<Once>, Reliable>, g::Actions<g::SendAction<P, Q, M, int>>>);
static_assert(std::is_same_v<g::state_step_t<Start<Once>, g::SendAction<P, Q, M, int>, Reliable>, Start<OnceSent>>);
static_assert(std::is_same_v<g::state_enabled_t<Start<OnceSent>, Reliable>, g::Actions<g::RecvAction<Q, P, M, int>>>);
static_assert(std::is_same_v<g::state_step_t<Start<OnceSent>, g::RecvAction<Q, P, M, int>, Reliable>, Start<g::End>>);
// A label with another payload, another label or another peer is no step.
static_assert(std::is_same_v<g::state_step_t<Start<Once>, g::SendAction<P, Q, M, char>, Reliable>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<Once>, g::SendAction<P, Q, M1, int>, Reliable>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<Once>, g::SendAction<P, R, M, int>, Reliable>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<g::End>, g::SendAction<P, Q, M, int>, Reliable>, g::NoTransition>);

// A choice sends one of its labels.
using Choice = g::Comm<P, Q, g::Branch<M1, int, g::End>, g::Branch<M2, char, g::End>>;
static_assert(same_labels_v<g::state_enabled_t<Start<Choice>, Reliable>,
                            g::Actions<g::SendAction<P, Q, M1, int>, g::SendAction<P, Q, M2, char>>>);
static_assert(
    std::is_same_v<g::state_step_t<Start<Choice>, g::SendAction<P, Q, M2, char>, Reliable>,
                   Start<g::EnRouteChoice<P, Q, M2, g::Branch<M1, int, g::End>, g::Branch<M2, char, g::End>>>>);

// [GR-µ]: a Rec reduces as its unfolding, and the Var becomes the Rec.
using Forever = g::Rec<g::Msg<P, Q, M, int, g::Var>>;
static_assert(std::is_same_v<g::state_step_t<Start<Forever>, g::SendAction<P, Q, M, int>, Reliable>,
                             Start<g::EnRoute<P, Q, M, int, Forever>>>);

// ── [GR-Ctx-i] and [GR-Ctx-ii] ───────────────────────────────────────

// Under p → q a role that is neither p nor q acts in every branch.
static_assert(same_labels_v<g::state_enabled_t<Start<Independent>, Reliable>,
                            g::Actions<g::SendAction<P, Q, M, int>, g::SendAction<R, S, M1, int>>>);
static_assert(std::is_same_v<g::state_step_t<Start<Independent>, g::SendAction<R, S, M1, int>, Reliable>,
                             Start<g::Msg<P, Q, M, int, g::EnRoute<R, S, M1, int, g::End>>>>);
// The receiver of p → q waits for that message.
using Chained = g::Msg<P, Q, M, int, g::Msg<Q, R, M1, int, g::End>>;
static_assert(std::is_same_v<g::state_enabled_t<Start<Chained>, Reliable>, g::Actions<g::SendAction<P, Q, M, int>>>);
// Under p ⇝ q the sender goes on, and the receiver first receives.
using SendsTwice = g::EnRoute<P, Q, M, int, g::Msg<P, R, M1, int, g::End>>;
static_assert(same_labels_v<g::state_enabled_t<Start<SendsTwice>, Reliable>,
                            g::Actions<g::RecvAction<Q, P, M, int>, g::SendAction<P, R, M1, int>>>);
// Under p ⇝ q : j only branch j is live.  The sender goes on in branch j,
// though the other branch would send another label, and the other branch
// stays for the choice of the receiver.
using SentM1 = g::EnRouteChoice<P, Q, M1, g::Branch<M1, int, g::Msg<P, R, L1, int, g::End>>,
                                g::Branch<M2, int, g::Msg<P, R, L2, int, g::End>>>;
static_assert(same_labels_v<g::state_enabled_t<Start<SentM1>, Reliable>,
                            g::Actions<g::RecvAction<Q, P, M1, int>, g::SendAction<P, R, L1, int>>>);
static_assert(std::is_same_v<g::state_step_t<Start<SentM1>, g::SendAction<P, R, L1, int>, Reliable>,
                             Start<g::EnRouteChoice<P, Q, M1, g::Branch<M1, int, g::EnRoute<P, R, L1, int, g::End>>,
                                                    g::Branch<M2, int, g::Msg<P, R, L2, int, g::End>>>>>);
static_assert(std::is_same_v<g::state_step_t<Start<SentM1>, g::SendAction<P, R, L2, int>, Reliable>, g::NoTransition>);
// A role that acts in one branch only cannot act before the choice.
using OneSided = g::Comm<P, Q, g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>, g::Branch<M2, int, g::End>>;
static_assert(!holds_v<g::state_enabled_t<Start<OneSided>, Reliable>, g::SendAction<R, S, M, int>>);

// ── [GR-↯], [GR-⊙] and [GR-&] after a crash ─────────────────────────

// P is not reliable, so Q has a crash branch.
using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
static_assert(same_labels_v<g::state_enabled_t<Start<Guarded>, OnlyQ>,
                            g::Actions<g::SendAction<P, Q, M, int>, g::CrashAction<P>>>);
using PCrashed =
    g::State<g::Roles<P>,
             g::EnRouteChoice<g::Crashed<P>, Q, Crash, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::CrashAction<P>, OnlyQ>, PCrashed>);
static_assert(std::is_same_v<g::state_enabled_t<PCrashed, OnlyQ>, g::Actions<g::DetectAction<Q, P>>>);
static_assert(std::is_same_v<g::state_step_t<PCrashed, g::DetectAction<Q, P>, OnlyQ>, g::State<g::Roles<P>, g::End>>);
// No role sends the crash label, and a reliable role never crashes.
static_assert(
    std::is_same_v<g::state_step_t<Start<Guarded>, g::SendAction<P, Q, Crash, void>, OnlyQ>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::CrashAction<Q>, OnlyQ>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::CrashAction<P>, Reliable>, g::NoTransition>);
// A role that is no live role of G does not crash.
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::CrashAction<R>, s::ReliableSet<>>, g::NoTransition>);
static_assert(std::is_same_v<g::state_step_t<Start<OnceSent>, g::CrashAction<P>, s::ReliableSet<>>, g::NoTransition>);

// A message that P sent before it crashed still arrives, and Q detects
// the crash at the next reception.
using Twice = g::Comm<P, Q, g::Branch<M1, int, g::End>, g::Branch<Crash, void, g::End>>;
using FirstSent = g::EnRouteChoice<P, Q, M, g::Branch<M, int, Twice>, g::Branch<Crash, void, g::End>>;
using FirstSentPCrashed =
    g::State<g::Roles<P>,
             g::EnRouteChoice<g::Crashed<P>, Q, M,
                              g::Branch<M, int,
                                        g::EnRouteChoice<g::Crashed<P>, Q, Crash, g::Branch<M1, int, g::End>,
                                                         g::Branch<Crash, void, g::End>>>,
                              g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<FirstSent>, g::CrashAction<P>, OnlyQ>, FirstSentPCrashed>);
static_assert(std::is_same_v<g::state_enabled_t<FirstSentPCrashed, OnlyQ>, g::Actions<g::RecvAction<Q, P, M, int>>>);
static_assert(
    std::is_same_v<g::state_enabled_t<g::state_step_t<FirstSentPCrashed, g::RecvAction<Q, P, M, int>, OnlyQ>, OnlyQ>,
                   g::Actions<g::DetectAction<Q, P>>>);

// [GR-↯m] and Remark 4.13: the crash of q leaves an annotation, and the
// message of p to the crashed q is lost.  The final state is end, so only
// the set C still says that q crashed.
using Remark413 = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;
using QCrashed =
    g::State<g::Roles<Q>, g::Comm<P, g::Crashed<Q>, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<Remark413>, g::CrashAction<Q>, s::ReliableSet<>>, QCrashed>);
static_assert(std::is_same_v<g::state_step_t<QCrashed, g::SendAction<P, Q, M, int>, s::ReliableSet<>>,
                             g::State<g::Roles<Q>, g::End>>);

}  // namespace test_session_semantics
