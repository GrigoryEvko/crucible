// Definition 4.19 of fixy/session/CrashAssociation.h, checked clause by
// clause, and operational correspondence under crash-stop failures.
//
// The second half walks each global type of a corpus from its projected
// configuration, label by label, with some roles unreliable.  At each
// state the configuration and the global type must allow the same
// labels, crashes and detections included, and after each label the
// configuration must stay associated with the state (Theorems 4.20 and
// 4.21 of Barwell, Hou, Yoshida and Zhou, LMCS 2025).  The corpus holds
// a sender that acts before its first message arrives, the case where
// only the chosen branch of an en-route node is live
// (fixy/session/Global.h).

#include <fixy/session/CrashAssociation.h>
#include <fixy/session/Semantics.h>

#include <cstdio>
#include <type_traits>

namespace test_session_crash_association {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct R {};
struct M {};
struct M1 {};
struct M2 {};
struct Add {};
struct Sub {};

using Crash = g::CrashLabel;
using F = c::CrashAssociationFault;

template <typename G>
using Start = g::State<g::Roles<>, G>;

// ── Clause by clause ─────────────────────────────────────────────────

// P is not reliable, so Q has a crash branch.
using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using SendsM = s::Send<s::PeerMsg<Q, M, int>, s::End>;
using TakesM =
    s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M, int>, s::End>, s::Recv<s::PeerMsg<P, Crash, void>, s::End>>;
using Ctx0 = c::crash_projected_context_t<Guarded, OnlyQ>;
static_assert(
    std::is_same_v<Ctx0, s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>, s::RoleState<Q, s::OutQueue<>, TakesM>>>);
static_assert(c::crash_association_holds_v<Ctx0, Start<Guarded>, OnlyQ>);

// Each label keeps the association.
using SentState = g::state_step_t<Start<Guarded>, g::SendAction<P, Q, M, int>, OnlyQ>;
using SentCtx = c::step_t<Ctx0, g::SendAction<P, Q, M, int>, OnlyQ>;
static_assert(c::crash_association_holds_v<SentCtx, SentState, OnlyQ>);
using PCrashedState = g::state_step_t<Start<Guarded>, g::CrashAction<P>, OnlyQ>;
using PCrashedCtx = c::step_t<Ctx0, g::CrashAction<P>, OnlyQ>;
static_assert(c::crash_association_holds_v<PCrashedCtx, PCrashedState, OnlyQ>);
using DetectedState = g::state_step_t<PCrashedState, g::DetectAction<Q, P>, OnlyQ>;
using DetectedCtx = c::step_t<PCrashedCtx, g::DetectAction<Q, P>, OnlyQ>;
static_assert(c::crash_association_holds_v<DetectedCtx, DetectedState, OnlyQ>);

// A2: the entries at Stop are the crashed roles, no more and no fewer.
static_assert(c::crash_association_fault_v<Ctx0, PCrashedState, OnlyQ> == F::CrashedRoleMismatch);
static_assert(c::crash_association_fault_v<PCrashedCtx, Start<Guarded>, OnlyQ> == F::CrashedRoleMismatch);

// A1: a live role refines its crash-stop projection.  Q cannot drop the
// crash branch, and a live role must have an entry.
using DropsCrash = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>,
                                    s::RoleState<Q, s::OutQueue<>, s::Recv<s::PeerMsg<P, M, int>, s::End>>>;
static_assert(c::crash_association_fault_v<DropsCrash, Start<Guarded>, OnlyQ> == F::LiveRoleMismatch);
using NoQ = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>>;
static_assert(c::crash_association_fault_v<NoQ, Start<Guarded>, OnlyQ> == F::LiveRoleMismatch);

// A3: a role that is neither live nor crashed is at End.
using StrayR = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>, s::RoleState<Q, s::OutQueue<>, TakesM>,
                                s::RoleState<R, s::OutQueue<>, s::Send<s::PeerMsg<P, M, int>, s::End>>>;
static_assert(c::crash_association_fault_v<StrayR, Start<Guarded>, OnlyQ> == F::UnfinishedRole);
using DoneR = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>, s::RoleState<Q, s::OutQueue<>, TakesM>,
                               s::RoleState<R, s::OutQueue<>, s::End>>;
static_assert(c::crash_association_holds_v<DoneR, Start<Guarded>, OnlyQ>);

// A4: a message that G has not sent, a message that G has en route and
// the queue lacks, and a message to a crashed role.
using Early = s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, SendsM>,
                               s::RoleState<Q, s::OutQueue<>, TakesM>>;
static_assert(c::crash_association_fault_v<Early, Start<Guarded>, OnlyQ> == F::QueueMismatch);
using Lost = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::End>, s::RoleState<Q, s::OutQueue<>, TakesM>>;
static_assert(c::crash_association_fault_v<Lost, SentState, OnlyQ> == F::QueueMismatch);
using QCrashedState = g::state_step_t<Start<Guarded>, g::CrashAction<Q>, s::NoReliableRoles>;
static_assert(std::is_same_v<QCrashedState, g::State<g::Roles<Q>, g::Comm<P, g::Crashed<Q>, g::Branch<M, int, g::End>,
                                                                          g::Branch<Crash, void, g::End>>>>);
using ToCrashed = s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, SendsM>,
                                   s::RoleState<Q, s::OutQueue<>, s::Stop>>;
static_assert(c::crash_association_fault_v<ToCrashed, QCrashedState, s::NoReliableRoles> == F::QueueMismatch);
static_assert(c::crash_association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, SendsM>,
                                                            s::RoleState<Q, s::OutQueue<>, s::Stop>>,
                                           QCrashedState, s::NoReliableRoles>);

// Definition 4.15: a reliable role never crashes, and each role with
// the crash annotation is in the crashed set.
static_assert(c::crash_association_fault_v<PCrashedCtx, PCrashedState, s::ReliableSet<P, Q>> == F::NotWellAnnotated);
static_assert(c::crash_association_fault_v<PCrashedCtx, g::State<g::Roles<>, typename PCrashedState::type>, OnlyQ>
              == F::NotWellAnnotated);

// Balanced+: equation (49) of Pischke, Masters and Yoshida.
using Ex49 = g::Msg<P, Q, M, int, g::EnRoute<P, Q, M1, int, g::End>>;
static_assert(c::crash_association_fault_v<s::projected_context_t<Ex49>, Start<Ex49>, s::EveryRoleReliable>
              == F::NotBalancedPlus);

// With every role reliable the relation agrees with association_holds_v.
using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using RingCtx = s::projected_context_t<Ring>;
static_assert(std::is_same_v<c::crash_projected_context_t<Ring, s::EveryRoleReliable>, RingCtx>);
static_assert(c::crash_association_holds_v<RingCtx, Start<Ring>, s::EveryRoleReliable>
              && s::association_holds_v<RingCtx, Ring>);

// ── Operational correspondence under crashes ─────────────────────────

template <typename List, typename A>
inline constexpr bool holds_v = false;
template <typename... As, typename A>
inline constexpr bool holds_v<g::Actions<As...>, A> = (std::is_same_v<As, A> || ...);

template <typename A, typename B>
inline constexpr bool includes_v = false;
template <typename A, typename... Bs>
inline constexpr bool includes_v<A, g::Actions<Bs...>> = (holds_v<A, Bs> && ...);

template <typename A, typename B>
inline constexpr bool same_labels_v = includes_v<A, B> && includes_v<B, A>;

struct Tally {
    int states = 0;
    int labels = 0;
    int crashes = 0;
    int enabled_mismatches = 0;
    int unassociated = 0;

    constexpr Tally operator+(Tally other) const noexcept {
        return {states + other.states, labels + other.labels, crashes + other.crashes,
                enabled_mismatches + other.enabled_mismatches, unassociated + other.unassociated};
    }
};

template <typename A>
inline constexpr bool is_crash_v = false;
template <typename Role>
inline constexpr bool is_crash_v<g::CrashAction<Role>> = true;

template <typename Ctx, typename S, typename Rel, int Depth>
constexpr Tally explore() noexcept;

// Complexity: one projection per live role, per label.
template <typename Ctx, typename S, typename Rel, int Depth, typename A>
constexpr Tally explore_label() noexcept {
    using next_state = g::state_step_t<S, A, Rel>;
    using next_ctx = c::step_t<Ctx, A, Rel>;
    Tally tally{.labels = 1, .crashes = is_crash_v<A> ? 1 : 0};
    if constexpr (std::is_same_v<next_state, g::NoTransition> || std::is_same_v<next_ctx, g::NoTransition>) {
        tally.unassociated = 1;
    } else {
        if (!c::crash_association_holds_v<next_ctx, next_state, Rel>) tally.unassociated = 1;
        if constexpr (Depth > 0) tally = tally + explore<next_ctx, next_state, Rel, Depth - 1>();
    }
    return tally;
}

template <typename Ctx, typename S, typename Rel, int Depth, typename... As>
constexpr Tally explore_each(g::Actions<As...>*) noexcept {
    return (Tally{} + ... + explore_label<Ctx, S, Rel, Depth, As>());
}

// Complexity: the labels of each state, to the power of Depth.
template <typename Ctx, typename S, typename Rel, int Depth>
constexpr Tally explore() noexcept {
    using global_labels = g::state_enabled_t<S, Rel>;
    using context_labels = c::enabled_t<Ctx, Rel>;
    Tally tally{.states = 1};
    if (!same_labels_v<global_labels, context_labels>) tally.enabled_mismatches = 1;
    return tally + explore_each<Ctx, S, Rel, Depth>(static_cast<global_labels*>(nullptr));
}

template <typename G, typename Rel, int Depth>
constexpr Tally explore_projected() noexcept {
    return explore<c::crash_projected_context_t<G, Rel>, Start<G>, Rel, Depth>();
}

constexpr bool is_clean(Tally tally) noexcept {
    return tally.states > 1 && tally.enabled_mismatches == 0 && tally.unassociated == 0;
}

// Remark 4.13 of the paper, with no reliable role.
using Remark413 = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<Crash, void, g::End>>;

// A relay: P tells Q, and Q tells R, each with a crash branch.
using Relay =
    g::Comm<P, Q, g::Branch<M, int, g::Comm<Q, R, g::Branch<M1, int, g::End>, g::Branch<Crash, void, g::End>>>,
            g::Branch<Crash, void, g::Comm<Q, R, g::Branch<M2, int, g::End>, g::Branch<Crash, void, g::End>>>>;

// The simpler logging protocol, equation (2.1) of the paper.
struct L {};
struct I {};
struct Cl {};
struct Trigger {};
struct Read {};
struct Report {};
struct Fatal {};
struct Log {};
using Logging = g::Msg<L, I, Trigger, void,
                       g::Comm<Cl, I,
                               g::Branch<Read, void,
                                         g::Msg<I, L, Read, void, g::Msg<L, I, Report, Log, g::Msg<I, Cl, Report, Log, g::End>>>>,
                               g::Branch<Crash, void, g::Msg<I, L, Fatal, void, g::End>>>>;

// P chooses a label for Q, and then sends R the matching label, with no
// role reliable.  After its first send P acts again at once, so the walk
// passes through en-route nodes whose other branches can never run.
using SenderGoesOn =
    g::Comm<P, Q,
            g::Branch<M1, int, g::Comm<P, R, g::Branch<Add, int, g::End>, g::Branch<Crash, void, g::End>>>,
            g::Branch<M2, int, g::Comm<P, R, g::Branch<Sub, int, g::End>, g::Branch<Crash, void, g::End>>>,
            g::Branch<Crash, void, g::Comm<P, R, g::Branch<Add, int, g::End>, g::Branch<Crash, void, g::End>>>>;

inline constexpr Tally kGuarded = explore_projected<Guarded, OnlyQ, 4>();
inline constexpr Tally kSenderGoesOn = explore_projected<SenderGoesOn, s::NoReliableRoles, 5>();
inline constexpr Tally kRemark413 = explore_projected<Remark413, s::NoReliableRoles, 4>();
inline constexpr Tally kRelay = explore_projected<Relay, s::NoReliableRoles, 6>();
inline constexpr Tally kLogging = explore_projected<Logging, s::ReliableSet<L, I>, 8>();
inline constexpr Tally kRing = explore_projected<Ring, s::EveryRoleReliable, 6>();

static_assert(is_clean(kGuarded) && kGuarded.crashes > 0);
static_assert(is_clean(kSenderGoesOn) && kSenderGoesOn.crashes > 0);
static_assert(is_clean(kRemark413) && kRemark413.crashes > 0);
static_assert(is_clean(kRelay) && kRelay.crashes > 0);
static_assert(is_clean(kLogging) && kLogging.crashes > 0);
static_assert(is_clean(kRing) && kRing.crashes == 0);

// ── Self-attack: the walk must see a configuration that is not associated

inline constexpr Tally kEarly = explore<Early, Start<Guarded>, OnlyQ, 2>();
static_assert(kEarly.enabled_mismatches > 0 && kEarly.unassociated > 0);

inline constexpr Tally kDropsCrash = explore<DropsCrash, Start<Guarded>, OnlyQ, 3>();
static_assert(kDropsCrash.enabled_mismatches > 0);

}  // namespace test_session_crash_association

namespace {

namespace t = test_session_crash_association;

// The runtime half evaluates the same walks through a function pointer,
// so the compiler cannot fold the call into the constant above.
using Walk = t::Tally (*)() noexcept;

int check(char const* name, Walk walk, t::Tally expected) {
    const t::Tally got = walk();
    const bool agrees = got.states == expected.states && got.labels == expected.labels
                        && got.crashes == expected.crashes && got.enabled_mismatches == expected.enabled_mismatches
                        && got.unassociated == expected.unassociated;
    if (!agrees) std::fprintf(stderr, "test_session_crash_association: %s: the runtime walk disagrees\n", name);
    return agrees ? 0 : 1;
}

}  // namespace

int main() {
    volatile Walk relay = &t::explore_projected<t::Relay, ::fixy::session::NoReliableRoles, 6>;
    volatile Walk logging = &t::explore_projected<t::Logging, ::fixy::session::ReliableSet<t::L, t::I>, 8>;
    volatile Walk early = &t::explore<t::Early, t::Start<t::Guarded>, t::OnlyQ, 2>;
    int failures = 0;
    failures += check("relay", relay, t::kRelay);
    failures += check("logging", logging, t::kLogging);
    failures += check("early message", early, t::kEarly);
    std::printf("test_session_crash_association: relay %d states %d labels %d crashes, logging %d states %d labels\n",
                t::kRelay.states, t::kRelay.labels, t::kRelay.crashes, t::kLogging.states, t::kLogging.labels);
    return failures;
}
