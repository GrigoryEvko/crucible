// The transition systems of fixy/session/Semantics.h, checked.
//
// The first half checks each rule of Figure 7 (global types) and of
// Figure 8 (configurations) of Barwell, Hou, Yoshida and Zhou,
// "Crash-Stop Failures in Asynchronous Multiparty Session Types" (LMCS
// 21:2, 2025), one small state at a time.  Remark 4.13 of the paper is
// checked as the paper writes it.
//
// The second half checks operational correspondence on a corpus of
// global types, with every role reliable.  The walk starts from G and its
// projected context, and it takes each label that G allows, to a bounded
// depth.  At each state the two sides must allow the same labels, and
// after each label the context must stay associated with G (Definition
// 21 of Pischke, Masters and Yoshida).  That is Theorems 4.20 and 4.21 of
// the crash-stop paper for the reliable case.  A context that is not
// associated must make the walk report a fault, and the self-attack at
// the foot checks that it does.

#include <fixy/Tagged.h>
#include <fixy/session/Semantics.h>

#include <cstdio>
#include <type_traits>

namespace test_session_semantics {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct R {};
struct S {};

struct Add {};
struct Sub {};
struct M {};
struct M1 {};
struct M2 {};
struct K {};
struct L1 {};
struct L2 {};

using Reliable = s::EveryRoleReliable;
using Crash = g::CrashLabel;

template <typename G>
using Start = g::State<g::Roles<>, G>;

// ── Label sets ───────────────────────────────────────────────────────

template <typename List, typename A>
inline constexpr bool holds_v = false;
template <typename... As, typename A>
inline constexpr bool holds_v<g::Actions<As...>, A> = (std::is_same_v<As, A> || ...);

template <typename A, typename B>
inline constexpr bool includes_v = false;
template <typename A, typename... Bs>
inline constexpr bool includes_v<A, g::Actions<Bs...>> = (holds_v<A, Bs> && ...);

// Two label lists hold the same labels, in any order.
template <typename A, typename B>
inline constexpr bool same_labels_v = includes_v<A, B> && includes_v<B, A>;

// ── [GR-⊕], [GR-&] and [GR-µ] ────────────────────────────────────────

using Once = g::Msg<P, Q, M, int, g::End>;
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
static_assert(std::is_same_v<g::state_step_t<Start<Choice>, g::SendAction<P, Q, M2, char>, Reliable>,
                             Start<g::EnRouteChoice<P, Q, M2, g::Branch<M1, int, g::End>, g::Branch<M2, char, g::End>>>>);

// [GR-µ]: a Rec reduces as its unfolding, and the Var becomes the Rec.
using Forever = g::Rec<g::Msg<P, Q, M, int, g::Var>>;
static_assert(std::is_same_v<g::state_step_t<Start<Forever>, g::SendAction<P, Q, M, int>, Reliable>,
                             Start<g::EnRoute<P, Q, M, int, Forever>>>);

// ── [GR-Ctx-i] and [GR-Ctx-ii] ───────────────────────────────────────

// Under p → q a role that is neither p nor q acts in every branch.
using Independent = g::Msg<P, Q, M, int, g::Msg<R, S, M1, int, g::End>>;
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
// A role that acts in one branch only cannot act before the choice.
using OneSided = g::Comm<P, Q, g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>, g::Branch<M2, int, g::End>>;
static_assert(!holds_v<g::state_enabled_t<Start<OneSided>, Reliable>, g::SendAction<R, S, M, int>>);

// ── [GR-↯], [GR-⊙] and [GR-&] after a crash ─────────────────────────

// P is not reliable, so Q has a crash branch.
using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
static_assert(same_labels_v<g::state_enabled_t<Start<Guarded>, OnlyQ>,
                            g::Actions<g::SendAction<P, Q, M, int>, g::CrashAction<P>>>);
using PCrashed = g::State<g::Roles<P>, g::EnRouteChoice<g::Crashed<P>, Q, Crash, g::Branch<M, int, g::End>,
                                                        g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::CrashAction<P>, OnlyQ>, PCrashed>);
static_assert(std::is_same_v<g::state_enabled_t<PCrashed, OnlyQ>, g::Actions<g::DetectAction<Q, P>>>);
static_assert(std::is_same_v<g::state_step_t<PCrashed, g::DetectAction<Q, P>, OnlyQ>, g::State<g::Roles<P>, g::End>>);
// No role sends the crash label, and a reliable role never crashes.
static_assert(std::is_same_v<g::state_step_t<Start<Guarded>, g::SendAction<P, Q, Crash, void>, OnlyQ>, g::NoTransition>);
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
    g::State<g::Roles<P>, g::EnRouteChoice<g::Crashed<P>, Q, M,
                                           g::Branch<M, int,
                                                     g::EnRouteChoice<g::Crashed<P>, Q, Crash, g::Branch<M1, int, g::End>,
                                                                      g::Branch<Crash, void, g::End>>>,
                                           g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<FirstSent>, g::CrashAction<P>, OnlyQ>, FirstSentPCrashed>);
static_assert(std::is_same_v<g::state_enabled_t<FirstSentPCrashed, OnlyQ>, g::Actions<g::RecvAction<Q, P, M, int>>>);
static_assert(std::is_same_v<
              g::state_enabled_t<g::state_step_t<FirstSentPCrashed, g::RecvAction<Q, P, M, int>, OnlyQ>, OnlyQ>,
              g::Actions<g::DetectAction<Q, P>>>);

// [GR-↯m] and Remark 4.13: the crash of q leaves an annotation, and the
// message of p to the crashed q is lost.  The final state is end, so only
// the set C still says that q crashed.
using Remark413 = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;
using QCrashed = g::State<g::Roles<Q>, g::Comm<P, g::Crashed<Q>, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>>;
static_assert(std::is_same_v<g::state_step_t<Start<Remark413>, g::CrashAction<Q>, s::ReliableSet<>>, QCrashed>);
static_assert(std::is_same_v<g::state_step_t<QCrashed, g::SendAction<P, Q, M, int>, s::ReliableSet<>>,
                             g::State<g::Roles<Q>, g::End>>);

// ── The configuration rules (Figure 8) ───────────────────────────────

using SendM = s::Send<s::PeerMsg<Q, M, int>, s::End>;
using RecvM = s::Recv<s::PeerMsg<P, M, int>, s::End>;
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
static_assert(std::is_same_v<c::step_t<Pending, g::RecvAction<Q, P, M, int>, Reliable>, PQ<s::OutQueue<>, s::End, s::End>>);
static_assert(std::is_same_v<c::step_t<Fresh, g::RecvAction<Q, P, M, int>, Reliable>, g::NoTransition>);
// A message whose label no branch has stays in the queue.
static_assert(std::is_same_v<c::enabled_t<PQ<s::OutQueue<s::Queued<Q, M1, int>>, s::End, RecvM>, Reliable>, g::Actions<>>);

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
static_assert(std::is_same_v<c::enabled_t<PQ<s::OutQueue<s::Queued<Q, M, Narrow>>, s::End,
                                             s::Recv<s::PeerMsg<P, M, int>, s::End>>,
                                          Reliable>,
                             g::Actions<g::RecvAction<Q, P, M, Narrow>>>);

// ── Operational correspondence ───────────────────────────────────────

struct Tally {
    int states = 0;
    int labels = 0;
    int enabled_mismatches = 0;
    int unassociated = 0;

    constexpr Tally operator+(Tally other) const noexcept {
        return {states + other.states, labels + other.labels, enabled_mismatches + other.enabled_mismatches,
                unassociated + other.unassociated};
    }
};

template <typename Ctx, typename G, int Depth>
constexpr Tally explore() noexcept;

// One label of G: the context takes it too, and the result stays
// associated.  Complexity: one projection of G per role, per label.
template <typename Ctx, typename G, int Depth, typename A>
constexpr Tally explore_label() noexcept {
    using next_state = g::state_step_t<Start<G>, A, Reliable>;
    using next_ctx = c::step_t<Ctx, A, Reliable>;
    Tally tally{.labels = 1};
    if constexpr (std::is_same_v<next_state, g::NoTransition> || std::is_same_v<next_ctx, g::NoTransition>) {
        tally.unassociated = 1;
    } else {
        if (!s::association_holds_v<next_ctx, typename next_state::type>) tally.unassociated = 1;
        if constexpr (Depth > 0) tally = tally + explore<next_ctx, typename next_state::type, Depth - 1>();
    }
    return tally;
}

template <typename Ctx, typename G, int Depth, typename... As>
constexpr Tally explore_each(g::Actions<As...>*) noexcept {
    return (Tally{} + ... + explore_label<Ctx, G, Depth, As>());
}

// Complexity: the labels of each state, to the power of Depth.  The
// compiler keeps one instance per (context, type, depth), so a state
// that two paths reach costs once per depth.
template <typename Ctx, typename G, int Depth>
constexpr Tally explore() noexcept {
    using global_labels = g::state_enabled_t<Start<G>, Reliable>;
    using context_labels = c::enabled_t<Ctx, Reliable>;
    Tally tally{.states = 1};
    if (!same_labels_v<global_labels, context_labels>) tally.enabled_mismatches = 1;
    return tally + explore_each<Ctx, G, Depth>(static_cast<global_labels*>(nullptr));
}

template <typename G, int Depth>
constexpr Tally explore_projected() noexcept {
    return explore<s::projected_context_t<G>, G, Depth>();
}

constexpr bool is_clean(Tally tally) noexcept {
    return tally.states > 1 && tally.enabled_mismatches == 0 && tally.unassociated == 0;
}

// The corpus: the examples of fixy/session tests, each balanced+.
using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using Loop1 = g::Rec<g::Msg<P, Q, M1, int, g::Var>>;
using Ex4 = g::Comm<P, R, g::Branch<M1, int, g::EnRoute<Q, P, M, int, Loop1>>,
                    g::Branch<M2, int, g::EnRoute<Q, P, M, int, g::Msg<P, Q, M2, int, Loop1>>>>;
using Tirore3 = g::Msg<P, Q, K, int, g::Rec<g::Comm<R, S, g::Branch<L1, int, g::End>, g::Branch<L2, int, g::Var>>>>;
using PingPong = g::Rec<g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, P, M2, int, g::Var>>, g::Branch<M2, int, g::End>>>;
using AfterChoice = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>,
                            g::Branch<M2, int, g::Msg<Q, R, M2, int, g::End>>>;
using SameSends = g::Comm<P, Q, g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>,
                          g::Branch<M2, int, g::Msg<R, S, M, int, g::End>>>;
using Nested = g::Rec<g::Msg<P, Q, M, int, g::Rec<g::Msg<Q, R, M, int, g::Var>>>>;

inline constexpr Tally kOnce = explore_projected<Once, 4>();
inline constexpr Tally kRing = explore_projected<Ring, 8>();
inline constexpr Tally kPingPong = explore_projected<PingPong, 6>();
inline constexpr Tally kAfterChoice = explore_projected<AfterChoice, 6>();
inline constexpr Tally kSameSends = explore_projected<SameSends, 6>();
inline constexpr Tally kNested = explore_projected<Nested, 6>();
inline constexpr Tally kIndependent = explore_projected<Independent, 6>();

static_assert(is_clean(kOnce));
static_assert(is_clean(kRing));
static_assert(is_clean(kPingPong));
static_assert(is_clean(kAfterChoice));
static_assert(is_clean(kSameSends));
static_assert(is_clean(kNested));
static_assert(is_clean(kIndependent));

// ── Known failures: the sender acts before its message arrives ───────
//
// [GR-Ctx-ii] of Figure 7 lets a label pass an en-route prefix only when
// every branch of the prefix takes it.  After p sends m_j, the branches
// other than j can never run, but the rule still asks them to move.  The
// configuration of p is the projection of branch j alone, so it takes
// the next action of p at once, and G cannot.  In our reading this is a
// gap in Theorem 4.20: p → q : {m1.p → r : a.end, m2.p → r : b.end} sends
// m1, and then the configuration can send a while G cannot.  Lemma A.20
// (1)(b) of the paper names no case for the en-route sender.  Section 5,
// item 12, of misc/session_types_literature.md gives the derivation.
//
// Each entry runs its walk, and the walk must still fail.  An entry
// whose walk stops failing is stale and fails this test, so the ledger
// can only shrink.

using SenderGoesOn = g::Comm<P, Q, g::Branch<M1, int, g::Msg<P, R, L1, int, g::End>>,
                             g::Branch<M2, int, g::Msg<P, R, L2, int, g::End>>>;
inline constexpr Tally kSenderGoesOn = explore_projected<SenderGoesOn, 4>();
inline constexpr Tally kEx4 = explore_projected<Ex4, 6>();
inline constexpr Tally kTirore3 = explore_projected<Tirore3, 6>();

static_assert(kSenderGoesOn.enabled_mismatches > 0);
static_assert(kEx4.enabled_mismatches > 0);
static_assert(kTirore3.enabled_mismatches > 0);

// The first mismatch of the minimal example is the one the comment names.
using SenderGoesOnSent = g::EnRouteChoice<P, Q, M1, g::Branch<M1, int, g::Msg<P, R, L1, int, g::End>>,
                                          g::Branch<M2, int, g::Msg<P, R, L2, int, g::End>>>;
static_assert(std::is_same_v<g::state_enabled_t<Start<SenderGoesOnSent>, Reliable>,
                             g::Actions<g::RecvAction<Q, P, M1, int>>>);
static_assert(holds_v<c::enabled_t<c::step_t<s::projected_context_t<SenderGoesOn>, g::SendAction<P, Q, M1, int>,
                                             Reliable>,
                                   Reliable>,
                      g::SendAction<P, R, L1, int>>);

// ── Self-attack: the walk must see a context that is not associated ──

// Q expects another label, so G sends a message that Q never takes.
using WrongLabel = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendM>,
                                    s::RoleState<Q, s::OutQueue<>, s::Recv<s::PeerMsg<P, M1, int>, s::End>>>;
inline constexpr Tally kWrongLabel = explore<WrongLabel, Once, 2>();
static_assert(kWrongLabel.enabled_mismatches > 0 && kWrongLabel.unassociated > 0);

// A message waits that G never sent, so Q can receive it at once.
using ExtraMessage = s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, SendM>,
                                      s::RoleState<Q, s::OutQueue<>, RecvM>>;
inline constexpr Tally kExtraMessage = explore<ExtraMessage, Once, 2>();
static_assert(kExtraMessage.enabled_mismatches > 0);

}  // namespace test_session_semantics

namespace {

namespace t = test_session_semantics;

// The runtime half evaluates the same walks through a function pointer,
// so the compiler cannot fold the call into the constant above.
using Walk = t::Tally (*)() noexcept;

int check(char const* name, Walk walk, t::Tally expected, bool expect_clean) {
    const t::Tally got = walk();
    const bool agrees = got.states == expected.states && got.labels == expected.labels
                        && got.enabled_mismatches == expected.enabled_mismatches
                        && got.unassociated == expected.unassociated && t::is_clean(got) == expect_clean;
    if (!agrees) std::fprintf(stderr, "test_session_semantics: %s: the runtime walk disagrees\n", name);
    return agrees ? 0 : 1;
}

}  // namespace

int main() {
    volatile Walk ring = &t::explore_projected<t::Ring, 8>;
    volatile Walk ping_pong = &t::explore_projected<t::PingPong, 6>;
    volatile Walk sender_goes_on = &t::explore_projected<t::SenderGoesOn, 4>;
    volatile Walk wrong = &t::explore<t::WrongLabel, t::Once, 2>;
    int failures = 0;
    failures += check("ring", ring, t::kRing, true);
    failures += check("ping pong", ping_pong, t::kPingPong, true);
    failures += check("sender goes on", sender_goes_on, t::kSenderGoesOn, false);
    failures += check("wrong label", wrong, t::kWrongLabel, false);
    std::printf("test_session_semantics: ring %d states %d labels, ping pong %d states %d labels\n", t::kRing.states,
                t::kRing.labels, t::kPingPong.states, t::kPingPong.labels);
    return failures;
}
