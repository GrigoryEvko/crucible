// The hand-written attacks of test_session_global_attack: the controls of
// the explorer, the paper's accepted types, the contexts that each refusal
// prevents, two live sessions that deadlock when interleaved, two gaps
// that the campaign found, and the ledger of known limitations.

#include "session_global_attack.h"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace test_session_global_attack {

namespace {

using test_session_global_attack_roles::P;
using test_session_global_attack_roles::Q;
using test_session_global_attack_roles::R;
using test_session_global_attack_roles::S;
using test_session_global_attack_roles::M;
using test_session_global_attack_roles::M0;
using test_session_global_attack_roles::M1;
using test_session_global_attack_roles::M2;
using test_session_global_attack_roles::X;
using test_session_global_attack_roles::Y;
using test_session_global_attack_roles::Z;
using test_session_global_attack_roles::Kk;
using test_session_global_attack_roles::L1;
using test_session_global_attack_roles::L2;
using test_session_global_attack_roles::Add;
using test_session_global_attack_roles::Sub;

template <typename Peer, typename Label, typename Cont>
using Out = s::Send<s::PeerMsg<Peer, Label, int>, Cont>;
template <typename Peer, typename Label, typename Cont>
using In = s::Recv<s::PeerMsg<Peer, Label, int>, Cont>;
template <typename Role, typename Local, typename Queue = s::OutQueue<>>
using At = s::RoleState<Role, Queue, Local>;

// ── 1. Controls: the explorer finds the faults of Example 10 ─────────

using Ex10Unsafe = s::TypingContext<At<P, s::End, s::OutQueue<s::Queued<Q, M, int>>>, At<Q, In<P, M1, s::End>>>;
using Ex10Deadlocked = s::TypingContext<At<P, In<Q, M, s::End>>, At<Q, s::End>>;
using Ex10Livelocked = s::TypingContext<At<P, s::Loop<Out<Q, M, s::Continue>>>, At<Q, s::Loop<In<P, M, s::Continue>>>,
                                        At<R, s::End, s::OutQueue<s::Queued<P, M1, int>>>>;

// ── 2. The paper's accepted types are live ───────────────────────────

using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using RingAfterAdd = g::EnRoute<P, Q, Add, int,
                                g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, Ring>>,
                                        g::Branch<Sub, int, g::Msg<R, P, Sub, int, Ring>>>>;
using Loop1 = g::Rec<g::Msg<P, Q, M1, int, g::Var>>;
using Ex4 = g::Comm<P, R, g::Branch<M1, int, g::EnRoute<Q, P, M, int, Loop1>>,
                    g::Branch<M2, int, g::EnRoute<Q, P, M, int, g::Msg<P, Q, M2, int, Loop1>>>>;
using Tirore3 = g::Msg<P, Q, Kk, int, g::Rec<g::Comm<R, S, g::Branch<L1, int, g::End>, g::Branch<L2, int, g::Var>>>>;
// Equation (5) loops back to an outer binder past an inner one that binds
// nothing, so its nearest-binder form leaves the inner binder out.
// Equation (7), whose inner loop continues both loops, has no form here:
// a Var reaches only the nearest Rec.  That limit costs expressiveness,
// not safety.
using Tirore5 = g::Rec<g::Msg<P, Q, Kk, int, g::Msg<R, S, Kk, int, g::Var>>>;
using Nested =
    g::Rec<g::Msg<P, Q, M, int, g::Rec<g::Comm<Q, R, g::Branch<L1, int, g::Var>, g::Branch<L2, int, g::End>>>>>;
using FanIn = g::Rec<g::Msg<P, S, X, int, g::Msg<Q, S, Y, int, g::Msg<R, S, Z, int, g::Msg<S, P, M, int, g::Var>>>>>;
using RunAhead = g::Rec<g::Msg<P, Q, X, int, g::Msg<P, R, Y, int, g::Var>>>;
using SentOnly = g::EnRoute<P, Q, M, int, g::End>;
using AfterChoice =
    g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>, g::Branch<M2, int, g::Msg<Q, R, M2, int, g::End>>>;
// A merge whose union hides nothing: R learns the branch from P's label.
using MergedUnion = g::Comm<P, Q, g::Branch<L1, int, g::Msg<P, R, M1, int, g::Msg<R, Q, X, int, g::End>>>,
                            g::Branch<L2, int, g::Msg<P, R, M2, int, g::Msg<Q, R, Y, int, g::End>>>>;
// A branch that leaves the outer loop for an inner loop of the same
// roles.
using InnerOnly = g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, P, X, int, g::Var>>,
                                 g::Branch<L2, int, g::Msg<Q, P, Y, int, g::Rec<g::Msg<P, Q, Z, int, g::Var>>>>>>;
// The same shape with new roles in the inner loop: R and S are roles of
// the outer loop body, and the L1 path loops back without them.
using InnerNewRoles = g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, P, X, int, g::Var>>,
                                     g::Branch<L2, int, g::Msg<Q, R, Y, int, g::Rec<g::Msg<R, S, Z, int, g::Var>>>>>>;
static_assert(!g::is_balanced_v<InnerNewRoles>);
static_assert(!s::is_live_by_construction_v<InnerNewRoles>);

// ── 3. What each refusal prevents ────────────────────────────────────
//
// For each refused type, a context that a weaker checker would give is
// run in the explorer.  Each must show a fault, so each refusal is
// needed.

// PMY25 Example 12, G1: the coinductive projection types R, but P can
// choose M0 for ever and R starves.
using Ex12G1 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<P, R, M, int, g::End>>>>;
static_assert(!g::is_balanced_v<Ex12G1>);
static_assert(!s::projects_v<Ex12G1, R>);
using Ex12G1Coinductive =
    s::TypingContext<At<P, s::Loop<s::Select<Out<Q, M0, s::Continue>, Out<Q, M1, Out<R, M, s::End>>>>>,
                     At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, M0, s::Continue>, In<P, M1, s::End>>>>,
                     At<R, In<P, M, s::End>>>;

// PMY25 equation (49): an en-route message behind a transmission of the
// same pair.  The projection that ignores the count is unsafe.
using Ex49 = g::Msg<P, Q, M, int, g::EnRoute<P, Q, M1, int, g::End>>;
static_assert(!g::is_balanced_plus_v<Ex49>);
using Ex49Naive =
    s::TypingContext<At<P, Out<Q, M, s::End>, s::OutQueue<s::Queued<Q, M1, int>>>, At<Q, In<P, M, In<P, M1, s::End>>>>;

// A role in one branch only: plain merge would need End = Recv.
using OneBranchOnly = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>, g::Branch<M2, int, g::End>>;
static_assert(!s::projects_v<OneBranchOnly, R>);
using OneBranchGuess = s::TypingContext<At<P, s::Select<Out<Q, M1, s::End>, Out<Q, M2, s::End>>>,
                                        At<Q, s::Offer<s::Sender<P>, In<P, M1, Out<R, M1, s::End>>, In<P, M2, s::End>>>,
                                        At<R, In<Q, M1, s::End>>>;

// Tirore et al., ECOOP 2025, equation (1).  With a shared queue R reads
// "channel 2" whoever wrote it.  With one queue per pair R must name the
// peer, and a projection that drops the peer guesses one.
using TiroreShared = g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, R, Kk, bool, g::End>>,
                             g::Branch<L2, int, g::Msg<P, R, Kk, bool, g::End>>>;
static_assert(!s::projects_v<TiroreShared, R>);
using TiroreGuess = s::TypingContext<
    At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::Send<s::PeerMsg<R, Kk, bool>, s::End>>>>,
    At<Q, s::Offer<s::Sender<P>, In<P, L1, s::Send<s::PeerMsg<R, Kk, bool>, s::End>>, In<P, L2, s::End>>>,
    At<R, s::Recv<s::PeerMsg<Q, Kk, bool>, s::End>>>;

// An internal choice that a role must make without seeing the choice.
using BlindSender =
    g::Comm<P, Q, g::Branch<L1, int, g::Msg<R, S, X, int, g::End>>, g::Branch<L2, int, g::Msg<R, S, Y, int, g::End>>>;
static_assert(!s::projects_v<BlindSender, R>);
using BlindGuess = s::TypingContext<At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::End>>>,
                                    At<Q, s::Offer<s::Sender<P>, In<P, L1, s::End>, In<P, L2, s::End>>>,
                                    At<R, Out<S, X, s::End>>, At<S, s::Offer<s::Sender<R>, In<R, Y, s::End>>>>;

// A merge that would hide a deadlock: R sends in one branch and receives
// in the other, from the same peer.
using CrossedMerge =
    g::Comm<P, Q, g::Branch<L1, int, g::Msg<R, S, X, int, g::End>>, g::Branch<L2, int, g::Msg<S, R, Y, int, g::End>>>;
static_assert(
    std::is_same_v<s::project_t<CrossedMerge, R>, s::NotProjectable<s::projection_failure::MergeShapeMismatch>>);
using CrossedGuess = s::TypingContext<At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::End>>>,
                                      At<Q, s::Offer<s::Sender<P>, In<P, L1, s::End>, In<P, L2, s::End>>>,
                                      At<R, In<S, Y, s::End>>, At<S, In<R, X, s::End>>>;

// A role in one branch of an inner loop: unbalanced.
using InnerBranchOnly =
    g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, R, X, int, g::Var>>, g::Branch<L2, int, g::Var>>>;
static_assert(!g::is_balanced_v<InnerBranchOnly>);
static_assert(!s::is_live_by_construction_v<InnerBranchOnly>);
using InnerBranchCoinductive =
    s::TypingContext<At<P, s::Loop<s::Select<Out<Q, L1, s::Continue>, Out<Q, L2, s::Continue>>>>,
                     At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, L1, Out<R, X, s::Continue>>, In<P, L2, s::Continue>>>>,
                     At<R, s::Loop<In<Q, X, s::Continue>>>>;

// A role absent from a loop projects to End, not to Loop<Continue>.
// Loop<Continue> spins without an action, and the explorer refuses it.
static_assert(
    std::is_same_v<typename s::project_t<g::Msg<P, R, M, int, g::Rec<g::Msg<P, Q, M, int, g::Var>>>, R>::local,
                   In<P, M, s::End>>);

// PMY25 Example 12, G2: refused as unbalanced, although its coinductive
// context is live.  The paper refuses it because the global type does not
// follow the context: S can send before P chooses M1.  The refusal costs
// completeness, not safety.
using Ex12G2 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<S, R, M, int, g::End>>>>;
static_assert(!s::is_live_by_construction_v<Ex12G2>);
using Ex12G2Coinductive =
    s::TypingContext<At<P, s::Loop<s::Select<Out<Q, M0, s::Continue>, Out<Q, M1, s::End>>>>,
                     At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, M0, s::Continue>, In<P, M1, s::End>>>>,
                     At<R, In<S, M, s::End>>, At<S, Out<R, M, s::End>>>;

// ── 4. Two sessions, each live, deadlock when interleaved ────────────
//
// Liveness by construction holds for one session.  Two processes that
// each play one role in two sessions, and wait on the sessions in
// opposite orders, deadlock.  Each session alone is live.

using SessionOne = g::Msg<P, Q, X, int, g::End>;
using SessionTwo = g::Msg<Q, P, Y, int, g::End>;
static_assert(s::is_live_by_construction_v<SessionOne> && s::is_live_by_construction_v<SessionTwo>);

// Opposite orders: P waits on session two first, Q on session one.
using ProcessP = s::compose_t<typename s::project_t<SessionTwo, P>::local, typename s::project_t<SessionOne, P>::local>;
using ProcessQ = s::compose_t<typename s::project_t<SessionOne, Q>::local, typename s::project_t<SessionTwo, Q>::local>;
using CrossedSessions = s::TypingContext<At<P, ProcessP>, At<Q, ProcessQ>>;

// One priority order: both processes use session one before session two.
using OrderedP = s::compose_t<typename s::project_t<SessionOne, P>::local, typename s::project_t<SessionTwo, P>::local>;
using OrderedSessions = s::TypingContext<At<P, OrderedP>, At<Q, ProcessQ>>;

// Three processes in a ring of three sessions, each waiting on its left
// neighbour first.
using SessionPQ = g::Msg<P, Q, X, int, g::End>;
using SessionQR = g::Msg<Q, R, X, int, g::End>;
using SessionRP = g::Msg<R, P, X, int, g::End>;
using RingP = s::compose_t<typename s::project_t<SessionRP, P>::local, typename s::project_t<SessionPQ, P>::local>;
using RingQ = s::compose_t<typename s::project_t<SessionPQ, Q>::local, typename s::project_t<SessionQR, Q>::local>;
using RingR = s::compose_t<typename s::project_t<SessionQR, R>::local, typename s::project_t<SessionRP, R>::local>;
using CrossedRing = s::TypingContext<At<P, RingP>, At<Q, RingQ>, At<R, RingR>>;

[[nodiscard]] bool crossed_sessions_deadlock() {
    const System two = load_context<CrossedSessions>();
    const System three = load_context<CrossedRing>();
    return analyse(two, 1).deadlocks > 0 && analyse(three, 1).deadlocks > 0;
}

// ── 5. Two gaps that this campaign found, and their repairs ──────────

// Association asks for balanced+, not only for well-formedness.  The
// projected context of equation (49) matches each projection exactly,
// and the explorer shows it unsafe, so association refuses it.
static_assert(g::is_global_well_formed_v<Ex49>);
static_assert(std::is_same_v<s::projected_context_t<Ex49>, Ex49Naive>);
static_assert(!s::association_holds_v<Ex49Naive, Ex49>);

// The binary view requires one peer.  The ring's P sends to Q and
// receives from R, and one binary channel would carry both.
template <typename Local>
concept admits_binary_view = requires { typename s::strip_peers_t<Local>; };
using RingProjectionP = typename s::project_t<Ring, P>::local;
static_assert(std::is_same_v<s::local_peers_t<RingProjectionP>, g::Roles<Q, R>>);
static_assert(!admits_binary_view<RingProjectionP>);
static_assert(admits_binary_view<typename s::project_t<SessionOne, P>::local>);

// ── Known limitations ────────────────────────────────────────────────

struct KnownLimitation {
    std::string_view attack;
    std::string_view broken_condition;
    bool (*still_succeeds)();
};

constexpr KnownLimitation known_limitations[] = {
    {"Two processes use two live sessions in opposite orders, and three processes use three sessions in a ring; "
     "each deadlocks.",
     "Liveness by construction covers one session (Pischke, Masters, Yoshida, Theorem 13; Pischke, Yoshida, "
     "Top-down = Bottom-up, p. 25).  Freedom from deadlock across sessions needs an acyclic ownership of channels, "
     "where a channel is made with the peer that holds its other end (LinearActris, POPL 2024), or a priority order "
     "on sessions (Dardha and Gay, Prioritised GV).  The types here order no sessions, so the composed context of "
     "this attack still deadlocks in the explorer.  fixy/session/Watch.h keeps the priority order at run time: a "
     "thread that waits while it holds a session of a priority that is not lower is refused before it waits, and "
     "test_session_handle_attacks refuses the crossed and the cyclic waits so.  That is prevention on the run that "
     "waits, and not a type that refuses the program.  A transport that blocks inside its own call publishes no "
     "wait, so the watch does not see a wait through it.",
     &crossed_sessions_deadlock},
};

// The ledger only shrinks.  Raise this bound only with a new attack that
// no gate can refuse, and name it above.
static_assert(std::size(known_limitations) <= 1);

}  // namespace

void run_controls() {
    std::printf("controls (Example 10: each must show a fault)\n");
    const System unsafe = load_context<Ex10Unsafe>();
    const Verdict unsafe_verdict = analyse(unsafe, 1);
    print_verdict("Example 10, unsafe", unsafe_verdict);
    expect(unsafe_verdict.unsafe > 0, "the explorer did not find the unsafe state of Example 10");
    const System deadlocked = load_context<Ex10Deadlocked>();
    const Verdict deadlocked_verdict = analyse(deadlocked, 1);
    print_verdict("Example 10, deadlocked", deadlocked_verdict);
    expect(deadlocked_verdict.deadlocks > 0, "the explorer did not find the deadlock of Example 10");
    const System livelocked = load_context<Ex10Livelocked>();
    const Verdict livelocked_verdict = analyse(livelocked, 2);
    print_verdict("Example 10, livelocked", livelocked_verdict);
    expect(livelocked_verdict.deadlocks == 0 && livelocked_verdict.starved > 0,
           "the explorer did not find the starved message of Example 10");
}

void run_accepted() {
    std::printf("accepted types (each must be live)\n");
    expect_live_global<Ring>("ring (section 1.2)");
    expect_live_global<RingAfterAdd>("ring after one send (Example 13)");
    expect_live_global<Ex4>("Example 4 with full merge");
    expect_live_global<Tirore3>("Tirore et al. ITP 2023, equation (3)");
    expect_live_global<Tirore5>("Tirore et al. ITP 2023, equation (5)");
    expect_live_global<Nested>("nested loop, outer loop never returns");
    expect_live_global<FanIn>("fan-in of three senders");
    expect_live_global<RunAhead>("one sender runs ahead of two receivers");
    expect_live_global<SentOnly>("a role that only has a queued message");
    expect_live_global<AfterChoice>("a role that appears after a choice");
    expect_live_global<MergedUnion>("a merge that takes the union of labels");
    expect_live_global<InnerOnly>("a branch that enters an inner loop");
}

void run_refusals() {
    std::printf("refused types (each weaker context must show a fault)\n");
    expect_context<Ex12G2Coinductive>("Example 12 G2 (refused, but its context is live)", true);
    expect_context<Ex12G1Coinductive>("Example 12 G1, coinductive projection", false);
    expect_context<Ex49Naive>("equation (49), count ignored", false);
    expect_context<OneBranchGuess>("a role in one branch only", false);
    expect_context<TiroreGuess>("ECOOP 2025 equation (1), peer guessed", false);
    expect_context<BlindGuess>("a blind internal choice", false);
    expect_context<CrossedGuess>("a crossed merge", false);
    expect_context<InnerBranchCoinductive>("a role in one branch of a loop", false);
}

void run_interleaving() {
    std::printf("two sessions, each live alone\n");
    expect_live_global<SessionOne>("session one alone");
    expect_live_global<SessionTwo>("session two alone");
    const System crossed = load_context<CrossedSessions>();
    print_verdict("both processes, opposite orders", analyse(crossed, 1));
    const System ordered = load_context<OrderedSessions>();
    const Verdict ordered_verdict = analyse(ordered, 1);
    print_verdict("both processes, one priority order", ordered_verdict);
    expect(ordered_verdict.is_clean(), "two sessions used in one priority order must not deadlock");
    const System ring = load_context<CrossedRing>();
    print_verdict("three processes, three sessions in a ring", analyse(ring, 1));
}

void run_ledger() {
    std::printf("known limitations (each attack must still succeed)\n");
    for (const KnownLimitation& entry : known_limitations) {
        const bool succeeds = entry.still_succeeds();
        std::printf("  %s %.*s\n", succeeds ? "PINNED" : "STALE ", static_cast<int>(entry.attack.size()),
                    entry.attack.data());
        expect(succeeds, std::string{"stale ledger entry, remove it: "} + std::string{entry.attack});
    }
}

}  // namespace test_session_global_attack
