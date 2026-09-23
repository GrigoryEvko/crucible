// Global types, well-formedness, balancedness, projection, association
// and liveness by construction.
//
// Each claim here is a compile-time fact, because a global type has no
// runtime representation.  The examples come from Pischke, Masters and
// Yoshida, "Asynchronous Global Protocols, Precisely" (arXiv 2505.17676,
// version 4), and from Tirore, Bengtson and Carbone (ITP 2023 and
// ECOOP 2025).  The comment at each block names the source.

#include <fixy/session/Liveness.h>

#include <type_traits>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct P {};
struct Q {};
struct R {};
struct S {};

struct Add {};
struct Sub {};
struct M {};
struct M0 {};
struct M1 {};
struct M2 {};
struct K {};
struct L1 {};
struct L2 {};
struct U {};

// ── Roles (Definition 3) ─────────────────────────────────────────────

// The ring protocol with choice (section 1.2 of the paper).
using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;

static_assert(std::is_same_v<g::roles_t<Ring>, g::Roles<P, Q, R>>);
static_assert(std::is_same_v<g::active_roles_t<Ring>, g::Roles<P, Q, R>>);
static_assert(std::is_same_v<g::sending_roles_t<Ring>, g::Roles<>>);

// A role that only sent an en-route message is a role, but not an
// active role.
using SentOnly = g::EnRoute<P, Q, M, int, g::End>;
static_assert(std::is_same_v<g::roles_t<SentOnly>, g::Roles<P, Q>>);
static_assert(std::is_same_v<g::active_roles_t<SentOnly>, g::Roles<Q>>);
static_assert(std::is_same_v<g::sending_roles_t<SentOnly>, g::Roles<P>>);

// Two distinct role types never merge.  The old walk deduplicated by a
// hash of the type, and a collision would have merged two roles.
struct TwinA {};
struct TwinB {};
static_assert(g::roles_t<g::Msg<TwinA, TwinB, M, int, g::End>>::size == 2);

static_assert(g::roles_equal_as_sets_v<g::Roles<P, Q>, g::Roles<Q, P>>);
static_assert(!g::roles_equal_as_sets_v<g::Roles<P, Q>, g::Roles<P, R>>);

// ── Well-formedness ──────────────────────────────────────────────────

static_assert(g::is_global_well_formed_v<Ring>);
static_assert(g::is_global_well_formed_v<g::End>);
static_assert(g::global_fault_v<g::Comm<P, Q>> == g::GlobalFault::EmptyChoice);
static_assert(g::global_fault_v<g::Msg<P, P, M, int, g::End>> == g::GlobalFault::SelfCommunication);
static_assert(g::global_fault_v<g::EnRoute<P, P, M, int, g::End>> == g::GlobalFault::SelfCommunication);
static_assert(g::global_fault_v<g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<M, char, g::End>>>
              == g::GlobalFault::DuplicateLabel);
static_assert(g::global_fault_v<g::Msg<P, Q, M, int, g::Var>> == g::GlobalFault::UnboundVariable);
static_assert(g::global_fault_v<g::Rec<g::Var>> == g::GlobalFault::UnguardedRecursion);
static_assert(g::global_fault_v<g::Rec<g::Rec<g::Var>>> == g::GlobalFault::UnguardedRecursion);
// The same label in two different transmissions is legal.
static_assert(g::is_global_well_formed_v<g::Msg<P, Q, M, int, g::Msg<Q, P, M, int, g::End>>>);
// A type that is not built from the combinators is not a global type.
static_assert(!g::is_global_type_v<s::Send<int, s::End>>);
static_assert(!g::is_global_well_formed_v<s::Send<int, s::End>>);

// ── Balancedness (Definitions 13, 14; Examples 11, 12, 14) ───────────

static_assert(g::is_balanced_v<Ring>);

// Example 11: three balanced types.  G'' does not project onto R.
using Ex11G = g::Comm<P, Q, g::Branch<M0, int, g::Msg<Q, P, M, int, g::End>>,
                      g::Branch<M1, int, g::Msg<Q, P, M, int, g::End>>>;
using Ex11G2 = g::Comm<P, Q, g::Branch<M0, int, g::Msg<R, S, M, int, g::End>>,
                       g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>>;
using Ex11G3 = g::Comm<P, Q, g::Branch<M0, int, g::Msg<R, S, M, int, g::End>>,
                       g::Branch<M1, int, g::Msg<R, S, M2, int, g::End>>>;
static_assert(g::is_balanced_v<Ex11G>);
static_assert(g::is_balanced_v<Ex11G2>);
static_assert(g::is_balanced_v<Ex11G3>);
static_assert(!s::projects_v<Ex11G3, R>);

// Example 12, equation (46): G1 starves R, G2 starves R and S.
using Ex12G1 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<P, R, M, int, g::End>>>>;
using Ex12G2 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<S, R, M, int, g::End>>>>;
static_assert(g::is_global_well_formed_v<Ex12G1>);
static_assert(!g::is_balanced_v<Ex12G1>);
static_assert(!g::is_balanced_v<Ex12G2>);
// Equation (47): a prefix does not repair the loop.
using Ex12G1Prefixed = g::Msg<P, Q, M, int, g::Msg<S, R, M, int, Ex12G1>>;
static_assert(!g::is_balanced_v<Ex12G1Prefixed>);
// The paper projects both coinductively.  The inductive merge refuses
// both, because a loop-back meets a message in the other branch.
static_assert(!s::projects_v<Ex12G1, R>);
static_assert(!s::projects_v<Ex12G2, S>);

// Example 14, equation (50).
using Ex14G = g::Rec<g::Comm<P, Q, g::Branch<M1, int, g::Var>, g::Branch<M2, int, g::Msg<P, R, M, int, g::End>>>>;
static_assert(!g::is_balanced_v<Ex14G>);
static_assert(!g::is_balanced_plus_v<Ex14G>);

// A role that acts before the loop, and never inside it, is not a role
// of the loop body.
using PrefixThenLoop = g::Msg<P, R, M, int, g::Rec<g::Msg<P, Q, M, int, g::Var>>>;
static_assert(g::is_balanced_v<PrefixThenLoop>);

// A nested loop that never returns to the outer loop.
using Nested = g::Rec<g::Msg<P, Q, M, int, g::Rec<g::Msg<Q, R, M, int, g::Var>>>>;
static_assert(g::is_balanced_v<Nested>);

// ── En-route counts (Definitions 15 to 17; Example 14, eq. (51)) ─────

// G1 = mu t. p ~> q : m . t
using Ex14E1 = g::Rec<g::EnRoute<P, Q, M, int, g::Msg<P, Q, M1, int, g::Var>>>;
// G2 = mu t. p ~> q : m, with no loop-back: the only balanced+ one.
using Ex14E2 = g::Rec<g::EnRoute<P, Q, M, int, g::End>>;
// G3 = p -> q : m' . p ~> q : m
using Ex14E3 = g::Msg<P, Q, M1, int, g::EnRoute<P, Q, M, int, g::End>>;
// G4 = p' -> q' : { m1 . p -> q : m , m2 . p ~> q : m }
using Ex14E4 = g::Comm<R, S, g::Branch<M1, int, g::Msg<P, Q, M, int, g::End>>,
                       g::Branch<M2, int, g::EnRoute<P, Q, M, int, g::End>>>;
static_assert(g::en_route_count_v<P, Q, Ex14E1> == -1);
static_assert(g::en_route_count_v<P, Q, Ex14E2> == 1);
static_assert(g::en_route_count_v<P, Q, Ex14E3> == -1);
static_assert(g::en_route_count_v<P, Q, Ex14E4> == -1);
static_assert(!g::is_balanced_plus_v<Ex14E1>);
static_assert(g::is_balanced_plus_v<Ex14E2>);
static_assert(!g::is_balanced_plus_v<Ex14E3>);
static_assert(!g::is_balanced_plus_v<Ex14E4>);
// Equation (49): the same shape as G3, which the paper shows unsafe.
using Ex49 = g::Msg<P, Q, M, int, g::EnRoute<P, Q, M1, int, g::End>>;
static_assert(!g::is_balanced_plus_v<Ex49>);

// Example 13: the ring after one send of P is balanced+.
using RingAfterAdd = g::EnRoute<P, Q, Add, int,
                                g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, Ring>>,
                                        g::Branch<Sub, int, g::Msg<R, P, Sub, int, Ring>>>>;
static_assert(g::is_balanced_plus_v<RingAfterAdd>);
static_assert(g::en_route_count_v<P, Q, RingAfterAdd> == 1);

// ── Projection: the ring (Example 3) ─────────────────────────────────

using Tp = s::Loop<s::Send<s::PeerMsg<Q, Add, int>, s::Offer<s::Sender<R>, s::Recv<s::PeerMsg<R, Add, int>, s::Continue>,
                                                                s::Recv<s::PeerMsg<R, Sub, int>, s::Continue>>>>;
using Tq = s::Loop<s::Recv<s::PeerMsg<P, Add, int>, s::Select<s::Send<s::PeerMsg<R, Add, int>, s::Continue>,
                                                              s::Send<s::PeerMsg<R, Sub, int>, s::Continue>>>>;
using Tr = s::Loop<s::Offer<s::Sender<Q>, s::Recv<s::PeerMsg<Q, Add, int>, s::Send<s::PeerMsg<P, Add, int>, s::Continue>>,
                            s::Recv<s::PeerMsg<Q, Sub, int>, s::Send<s::PeerMsg<P, Sub, int>, s::Continue>>>>;

static_assert(std::is_same_v<s::project_t<Ring, P>, s::Projected<s::OutQueue<>, Tp>>);
static_assert(std::is_same_v<s::project_t<Ring, Q>, s::Projected<s::OutQueue<>, Tq>>);
static_assert(std::is_same_v<s::project_t<Ring, R>, s::Projected<s::OutQueue<>, Tr>>);
static_assert(s::is_well_formed_v<Tp> && s::is_well_formed_v<Tq> && s::is_well_formed_v<Tr>);

// A role that the protocol never names projects to End, not to
// Loop<Continue>.
static_assert(std::is_same_v<s::project_t<Ring, S>, s::Projected<s::OutQueue<>, s::End>>);

// The ring after one send: P's queue holds the message, Q receives it.
static_assert(std::is_same_v<typename s::project_t<RingAfterAdd, P>::queue, s::OutQueue<s::Queued<Q, Add, int>>>);
static_assert(std::is_same_v<typename s::project_t<RingAfterAdd, Q>::local,
                             s::Recv<s::PeerMsg<P, Add, int>, s::Select<s::Send<s::PeerMsg<R, Add, int>, Tq>,
                                                                        s::Send<s::PeerMsg<R, Sub, int>, Tq>>>>);

// ── Projection: rule P-END for a loop (Tirore et al., ITP 2023) ──────

// Equation (3): p -> q : k . mu t. r -> s : { l1 : end , l2 : t }.
// P takes no part in the loop, so its projection ends after the send.
using Tirore3 = g::Msg<P, Q, K, U, g::Rec<g::Comm<R, S, g::Branch<L1, int, g::End>, g::Branch<L2, int, g::Var>>>>;
static_assert(g::is_balanced_plus_v<Tirore3>);
static_assert(std::is_same_v<s::project_t<Tirore3, P>, s::Projected<s::OutQueue<>, s::Send<s::PeerMsg<Q, K, U>, s::End>>>);
static_assert(std::is_same_v<s::project_t<Tirore3, Q>, s::Projected<s::OutQueue<>, s::Recv<s::PeerMsg<P, K, U>, s::End>>>);
static_assert(std::is_same_v<typename s::project_t<Tirore3, R>::local,
                             s::Loop<s::Select<s::Send<s::PeerMsg<S, L1, int>, s::End>,
                                               s::Send<s::PeerMsg<S, L2, int>, s::Continue>>>>);

// Equation (5), where the loop-back refers to the outer binder.  With
// nearest-binder recursion the inner binder, which binds nothing, is
// left out.  The projection loops the send.
using Tirore5 = g::Rec<g::Msg<P, Q, K, U, g::Msg<R, S, K, U, g::Var>>>;
static_assert(std::is_same_v<typename s::project_t<Tirore5, P>::local,
                             s::Loop<s::Send<s::PeerMsg<Q, K, U>, s::Continue>>>);

// A nested loop that the role enters projects as the inner loop.
static_assert(std::is_same_v<typename s::project_t<Nested, P>::local, s::Send<s::PeerMsg<Q, M, int>, s::End>>);
static_assert(std::is_same_v<typename s::project_t<Nested, Q>::local,
                             s::Recv<s::PeerMsg<P, M, int>, s::Loop<s::Send<s::PeerMsg<R, M, int>, s::Continue>>>>);

// ── Projection: full merge (Examples 4 and 5) ────────────────────────

using Loop1 = g::Rec<g::Msg<P, Q, M1, U, g::Var>>;
using Ex4 = g::Comm<P, R, g::Branch<M1, U, g::EnRoute<Q, P, M, U, Loop1>>,
                    g::Branch<M2, U, g::EnRoute<Q, P, M, U, g::Msg<P, Q, M2, U, Loop1>>>>;
static_assert(g::is_balanced_plus_v<Ex4>);

using L1q = s::Loop<s::Recv<s::PeerMsg<P, M1, U>, s::Continue>>;
using L1p = s::Loop<s::Send<s::PeerMsg<Q, M1, U>, s::Continue>>;
// Equation (15).
static_assert(std::is_same_v<s::project_t<Ex4, R>,
                             s::Projected<s::OutQueue<>, s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M1, U>, s::End>,
                                                                  s::Recv<s::PeerMsg<P, M2, U>, s::End>>>>);
// Equation (16).
static_assert(std::is_same_v<
              s::project_t<Ex4, P>,
              s::Projected<s::OutQueue<>,
                           s::Select<s::Send<s::PeerMsg<R, M1, U>, s::Recv<s::PeerMsg<Q, M, U>, L1p>>,
                                     s::Send<s::PeerMsg<R, M2, U>,
                                             s::Recv<s::PeerMsg<Q, M, U>, s::Send<s::PeerMsg<Q, M2, U>, L1p>>>>>>);
// Equations (17) to (19): the merge unfolds the loop once and takes the
// union of the two external choices.
static_assert(std::is_same_v<
              s::project_t<Ex4, Q>,
              s::Projected<s::OutQueue<s::Queued<P, M, U>>,
                           s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M1, U>, L1q>, s::Recv<s::PeerMsg<P, M2, U>, L1q>>>>);

// An internal choice merges only when the label sets agree.
using SameSends = g::Comm<P, Q, g::Branch<M1, int, g::Msg<R, S, M, int, g::End>>,
                          g::Branch<M2, int, g::Msg<R, S, M, int, g::End>>>;
static_assert(std::is_same_v<typename s::project_t<SameSends, R>::local, s::Send<s::PeerMsg<S, M, int>, s::End>>);
static_assert(std::is_same_v<s::project_t<Ex11G3, R>,
                             s::NotProjectable<s::projection_failure::MergeLabelSetMismatch>>);

// Different payloads for one label.
using PayloadClash = g::Comm<P, Q, g::Branch<M1, int, g::Msg<P, R, M, int, g::End>>,
                             g::Branch<M2, int, g::Msg<P, R, M, char, g::End>>>;
static_assert(std::is_same_v<s::project_t<PayloadClash, R>,
                             s::NotProjectable<s::projection_failure::MergePayloadMismatch>>);

// ── Projection: a role that appears only after a choice ──────────────

using AfterChoice = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>,
                            g::Branch<M2, int, g::Msg<Q, R, M2, int, g::End>>>;
static_assert(std::is_same_v<typename s::project_t<AfterChoice, R>::local,
                             s::Offer<s::Sender<Q>, s::Recv<s::PeerMsg<Q, M1, int>, s::End>,
                                      s::Recv<s::PeerMsg<Q, M2, int>, s::End>>>);
// In only one branch: R would wait for ever in the other one.
using OneBranchOnly = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>, g::Branch<M2, int, g::End>>;
static_assert(std::is_same_v<s::project_t<OneBranchOnly, R>,
                             s::NotProjectable<s::projection_failure::MergeShapeMismatch>>);

// ── Projection: the shared-queue counterexample (ECOOP 2025) ─────────

// Equation (1) of Tirore, Bengtson and Carbone: R receives from Q in one
// branch and from P in the other.  Over one queue per ordered pair of
// roles R cannot know which queue to read, so the projection refuses.
using TiroreShared = g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, R, K, bool, g::End>>,
                             g::Branch<L2, int, g::Msg<P, R, K, bool, g::End>>>;
static_assert(g::is_balanced_plus_v<TiroreShared>);
static_assert(std::is_same_v<s::project_t<TiroreShared, R>,
                             s::NotProjectable<s::projection_failure::MergeShapeMismatch>>);
static_assert(s::projects_v<TiroreShared, P> && s::projects_v<TiroreShared, Q>);
static_assert(!s::is_live_by_construction_v<TiroreShared>);

// ── The binary view ──────────────────────────────────────────────────

using PingPong = g::Rec<g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, P, M2, int, g::Var>>, g::Branch<M2, int, g::End>>>;
static_assert(std::is_same_v<s::strip_peers_t<typename s::project_t<PingPong, Q>::local>,
                             s::dual_of_t<s::strip_peers_t<typename s::project_t<PingPong, P>::local>>>);
static_assert(std::is_same_v<s::local_peers_t<typename s::project_t<PingPong, P>::local>, g::Roles<Q>>);
static_assert(std::is_same_v<s::local_peers_t<Tp>, g::Roles<Q, R>>);
static_assert(std::is_same_v<s::local_peers_t<s::End>, g::Roles<>>);

// ── Queues ───────────────────────────────────────────────────────────

static_assert(s::queues_equivalent_v<s::OutQueue<s::Queued<P, M, U>, s::Queued<Q, M1, U>>,
                                     s::OutQueue<s::Queued<Q, M1, U>, s::Queued<P, M, U>>>);
static_assert(!s::queues_equivalent_v<s::OutQueue<s::Queued<P, M, U>, s::Queued<P, M1, U>>,
                                      s::OutQueue<s::Queued<P, M1, U>, s::Queued<P, M, U>>>);

// ── Association (Definition 21) ──────────────────────────────────────

using RingCtx = s::projected_context_t<Ring>;
static_assert(std::is_same_v<RingCtx, s::TypingContext<s::RoleState<P, s::OutQueue<>, Tp>, s::RoleState<Q, s::OutQueue<>, Tq>,
                                                       s::RoleState<R, s::OutQueue<>, Tr>>>);
static_assert(s::association_holds_v<RingCtx, Ring>);
// Order of entries does not matter.
static_assert(s::association_holds_v<s::TypingContext<s::RoleState<R, s::OutQueue<>, Tr>, s::RoleState<P, s::OutQueue<>, Tp>,
                                                      s::RoleState<Q, s::OutQueue<>, Tq>>,
                                     Ring>);
// A finished extra role is admitted, an active one is not.
static_assert(s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, Tp>, s::RoleState<Q, s::OutQueue<>, Tq>,
                                                      s::RoleState<R, s::OutQueue<>, Tr>,
                                                      s::RoleState<S, s::OutQueue<>, s::End>>,
                                     Ring>);
static_assert(!s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, Tp>, s::RoleState<Q, s::OutQueue<>, Tq>,
                                                       s::RoleState<R, s::OutQueue<>, Tr>,
                                                       s::RoleState<S, s::OutQueue<>, Tq>>,
                                      Ring>);
static_assert(!s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, Tp>, s::RoleState<Q, s::OutQueue<>, Tq>>,
                                      Ring>);
static_assert(!s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, Tp>, s::RoleState<P, s::OutQueue<>, Tp>,
                                                       s::RoleState<Q, s::OutQueue<>, Tq>,
                                                       s::RoleState<R, s::OutQueue<>, Tr>>,
                                      Ring>);
static_assert(!s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, Tq>, s::RoleState<Q, s::OutQueue<>, Tq>,
                                                       s::RoleState<R, s::OutQueue<>, Tr>>,
                                      Ring>);
static_assert(!s::association_holds_v<int, Ring>);
// A queue that disagrees with the en-route messages of G.
static_assert(s::association_holds_v<s::projected_context_t<Ex4>, Ex4>);
static_assert(!s::association_holds_v<s::TypingContext<s::RoleState<P, s::OutQueue<>, typename s::project_t<Ex4, P>::local>,
                                                       s::RoleState<Q, s::OutQueue<>, typename s::project_t<Ex4, Q>::local>,
                                                       s::RoleState<R, s::OutQueue<>, typename s::project_t<Ex4, R>::local>>,
                                      Ex4>);
static_assert(std::is_same_v<s::projected_context_t<Ex11G3>, s::NotProjectable<s::projection_failure::MergeLabelSetMismatch>>);
// Equation (49) projects, but it is not balanced+, and its projected
// context is unsafe.  Association refuses it.
static_assert(s::projects_v<Ex49, P> && s::projects_v<Ex49, Q>);
static_assert(!s::association_holds_v<s::projected_context_t<Ex49>, Ex49>);

// ── Liveness by construction (Theorem 13) ────────────────────────────

static_assert(s::is_live_by_construction_v<Ring>);
static_assert(s::is_live_by_construction_v<RingAfterAdd>);
static_assert(s::is_live_by_construction_v<Ex4>);
static_assert(s::is_live_by_construction_v<Tirore3>);
static_assert(s::is_live_by_construction_v<g::End>);
static_assert(!s::is_live_by_construction_v<Ex12G1>);
static_assert(!s::is_live_by_construction_v<Ex14E1>);
static_assert(!s::is_live_by_construction_v<Ex11G3>);
static_assert(!s::is_live_by_construction_v<int>);
static_assert(s::context_is_live_v<RingCtx, Ring>);
static_assert(!s::context_is_live_v<RingCtx, Ex12G1>);

consteval bool gates_accept_the_ring() {
    g::ensure_global_well_formed<Ring>();
    g::ensure_balanced_plus<Ring>();
    s::ensure_projectable<Ring, P>();
    s::ensure_associated<RingCtx, Ring>();
    s::ensure_live_by_construction<Ring>();
    s::ensure_context_live<RingCtx, Ring>();
    return true;
}
static_assert(gates_accept_the_ring());

}  // namespace

int main() { return 0; }
