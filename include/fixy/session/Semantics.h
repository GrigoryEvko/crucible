#pragma once

// The labelled transition systems of global types and of configurations,
// under crash-stop failures.  The rules are those of Barwell, Hou, Yoshida
// and Zhou, "Crash-Stop Failures in Asynchronous Multiparty Session Types"
// (LMCS 21:2, 2025): the labels of Definition 4.9, the reductions of a
// global type of Definition 4.12 (Figure 7), and the reductions of a
// configuration of Definition 4.18 (Figure 8).  With every role reliable
// no crash label occurs, and the rules are those of the asynchronous
// theory of Pischke, Masters and Yoshida.
//
// ── Global types ────────────────────────────────────────────────────────
//
// A state is ⟨C; G⟩: the set of crashed roles and the global type.  The
// set is kept apart from G, because a role can crash and then leave no
// annotation in G (Remark 4.13 of the paper).
//
//   [GR-⊕]      p → q : {m_i.G_i}  --p⊕q:m_j-->  p ⇝ q : j {m_i.G_i}
//   [GR-&]      p† ⇝ q : j {m_i.G_i} --q&p:m_j--> G_j        (m_j not crash)
//   [GR-⊙]      p↯ ⇝ q : crash      --q⊙p------>  G_crash
//   [GR-↯m]     p → q↯ : {m_i.G_i}  --p⊕q:m_j-->  G_j        (message lost)
//   [GR-↯]      G --p↯--> G with p removed        (p live, p not reliable,
//                                                  G not a Rec)
//   [GR-µ]      a Rec reduces as its unfolding
//   [GR-Ctx-i]  under p → q†, a label whose subject is neither p nor q
//               reduces every branch
//   [GR-Ctx-ii] under p† ⇝ q : j, a label whose subject is not q reduces
//               branch j (Figure 7 asks every branch, see below)
//
// Here p† is p or p↯.  A live role is a role of active_roles_t, which is
// roles(G) of the paper.
//
// The crash label comes only from [GR-↯], at the top of the state.  The
// two context rules can also carry a crash label under a prefix.  The
// result then differs only when the crashed role is the sender of an
// en-route message whose label is not crash: [GR-↯] marks that sender
// crashed, and [GR-Ctx-ii] does not.  No rule reads that mark, and
// association (Definition 4.19) does not read it either.  So one step
// result per label is enough, and the step keeps the one of [GR-↯].
//
// Figure 7 states [GR-Ctx-ii] with every branch of the en-route prefix.
// After p sends m_j only branch j can still run, and the configuration of
// p is branch j alone.  So with every branch, p cannot act before its
// message arrives, while its configuration can, and in our reading
// Theorem 4.20 has a gap (misc/session_types_literature.md, section 5,
// item 12, gives the derivation).  Here the rule reduces branch j alone,
// as for the en-route node of Pischke, Masters and Yoshida, and the other
// branches stay as they are.  They only give the receiver its choice.
// fixy/session/Global.h and fixy/session/Projection.h read the chosen
// branch alone for the same reason.
//
// ── Configurations ──────────────────────────────────────────────────────
//
// A configuration is a TypingContext of fixy/session/Projection.h: for
// each role its queue of outgoing messages and its local type.  A queue
// holds one FIFO per receiver, as the paper's queue environment holds one
// per ordered pair.  A crashed role has the local type Stop.  The queues
// to a crashed role are unavailable: the crash drops each message to it,
// and a later send to it is lost.  The messages that a crashed role sent
// stay in its queue, and their receivers still get them.
//
//   [Γ-⊕]  a role at an internal choice sends one branch
//   [Γ-&]  a role at an external choice receives the message at the head
//          of the queue from its peer, when a branch has its label
//   [Γ-↯]  a role that is not at End or Stop, and not reliable, crashes
//   [Γ-⊙]  a role at an external choice with a crash branch detects the
//          crash of its peer, when the peer is at Stop and its queue to
//          the role is empty
//
// A received payload can be below the payload of its branch in the
// payload order of fixy/session/Subtype.h, because association lets a
// sender refine its payload.  The paper has no subtyping on payloads, so
// there the two payloads are equal.
//
// Each public alias asks for a state or a configuration that its walks
// know: is_global_state_v and is_configuration_v.  So a node that no
// rule knows is refused at the alias, with the predicate in the message,
// and not deep inside a walk.
//
// Complexity: one step visits the global type once, and each candidate
// label is checked by one step, so the enabled set of a state costs the
// size of the type times the number of candidates.

#include <fixy/session/Crash.h>
#include <fixy/session/Global.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Subtype.h>
#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <type_traits>

namespace fixy::session::global {

// ── Labels (Definition 4.9) ──────────────────────────────────────────

// P sends the message with label L and payload B to Q.
template <typename P, typename Q, typename L, typename B>
struct SendAction {};

// P receives the message with label L and payload B from Q.
template <typename P, typename Q, typename L, typename B>
struct RecvAction {};

// P crashes.
template <typename P>
struct CrashAction {};

// P detects that Q crashed.
template <typename P, typename Q>
struct DetectAction {};

template <typename... As>
struct Actions {
    static constexpr std::size_t size = sizeof...(As);
};

// No transition exists for the label.
struct NoTransition {};

// A state ⟨C; G⟩ of the global semantics.
template <typename Crashed, typename G>
struct State {
    using crashed = Crashed;
    using type = G;
};

namespace detail::lts {

using ::fixy::session::global::detail::bare_role_t;
using ::fixy::session::global::detail::continuation_of;
using ::fixy::session::global::detail::is_crash_label_v;
using ::fixy::session::global::detail::is_crashed_v;
using ::fixy::session::global::detail::payload_of;

template <typename A>
struct subject_of;
template <typename P, typename Q, typename L, typename B>
struct subject_of<SendAction<P, Q, L, B>> {
    using type = P;
};
template <typename P, typename Q, typename L, typename B>
struct subject_of<RecvAction<P, Q, L, B>> {
    using type = P;
};
template <typename P>
struct subject_of<CrashAction<P>> {
    using type = P;
};
template <typename P, typename Q>
struct subject_of<DetectAction<P, Q>> {
    using type = P;
};

template <typename A>
using subject_t = typename subject_of<A>::type;

template <typename A>
inline constexpr bool is_crash_action_v = false;
template <typename P>
inline constexpr bool is_crash_action_v<CrashAction<P>> = true;

// ── Lists of labels ──────────────────────────────────────────────────

template <typename... Lists>
struct concat;
template <>
struct concat<> {
    using type = Actions<>;
};
template <typename... As>
struct concat<Actions<As...>> {
    using type = Actions<As...>;
};
template <typename... As, typename... Bs, typename... Rest>
struct concat<Actions<As...>, Actions<Bs...>, Rest...> : concat<Actions<As..., Bs...>, Rest...> {};

template <typename List, typename A>
inline constexpr bool holds_v = false;
template <typename... As, typename A>
inline constexpr bool holds_v<Actions<As...>, A> = (std::is_same_v<As, A> || ...);

// Each label of List once, in the order of first occurrence.
template <typename Seen, typename List>
struct unique;
template <typename Seen>
struct unique<Seen, Actions<>> {
    using type = Seen;
};
template <typename... Seen, typename A, typename... Rest>
struct unique<Actions<Seen...>, Actions<A, Rest...>>
    : unique<std::conditional_t<holds_v<Actions<Seen...>, A>, Actions<Seen...>, Actions<Seen..., A>>, Actions<Rest...>> {};

// True when the subject of A is one of Roles.
template <typename A, typename... Roles>
inline constexpr bool subject_among_v = (std::is_same_v<subject_t<A>, Roles> || ...);

// The labels of List whose subject is none of Excluded, and that are no
// crash label.
template <typename List, typename... Excluded>
struct not_about;
template <typename... As, typename... Excluded>
struct not_about<Actions<As...>, Excluded...> {
    using type = typename concat<std::conditional_t<!is_crash_action_v<As> && !subject_among_v<As, Excluded...>,
                                                    Actions<As>, Actions<>>...>::type;
};

// ── Unfolding (rule GR-µ) ────────────────────────────────────────────
//
// Each Var that the Rec binds becomes the Rec.  A nested Rec binds its
// own Var, so the walk stops there, and the replacement is closed.

template <typename G, typename Rep>
struct subst_var;
template <typename Rep>
struct subst_var<End, Rep> {
    using type = End;
};
template <typename Rep>
struct subst_var<Var, Rep> {
    using type = Rep;
};
template <typename B, typename Rep>
struct subst_var<Rec<B>, Rep> {
    using type = Rec<B>;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename Rep>
struct subst_var<Comm<From, To, Branch<Ls, Ps, Cs>...>, Rep> {
    using type = Comm<From, To, Branch<Ls, Ps, typename subst_var<Cs, Rep>::type>...>;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs, typename Rep>
struct subst_var<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>, Rep> {
    using type = EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, typename subst_var<Cs, Rep>::type>...>;
};

template <typename G>
struct unfold {
    using type = G;
};
template <typename B>
struct unfold<Rec<B>> : unfold<typename subst_var<B, Rec<B>>::type> {};

template <typename G>
using unfold_t = typename unfold<G>::type;

// ── One step of a global type ────────────────────────────────────────
//
// The rules are syntax-directed: for one type and one label, exactly one
// rule can apply.  So a derivation that meets a Rec it is already
// unfolding, with the same label, repeats itself and has no finite end,
// and the label is no transition.  Unfolding holds the Recs on the path
// of the derivation.  A Var binds its nearest Rec, so each Rec that the
// walk meets is a subterm of the type it started from, and the set is
// finite.

template <typename... Recs>
struct Unfolding {};

template <typename U, typename G>
inline constexpr bool is_unfolding_v = false;
template <typename... Recs, typename G>
inline constexpr bool is_unfolding_v<Unfolding<Recs...>, G> = (std::is_same_v<Recs, G> || ...);

template <typename U, typename G>
struct push_unfolding;
template <typename... Recs, typename G>
struct push_unfolding<Unfolding<Recs...>, G> {
    using type = Unfolding<Recs..., G>;
};

template <typename G, typename A, typename U>
struct step;

template <typename G, typename A, typename U = Unfolding<>>
using step_t = typename step<G, A, U>::type;

template <typename... Gs>
inline constexpr bool all_step_v = (!std::is_same_v<Gs, NoTransition> && ...);

template <typename A, typename U>
struct step<End, A, U> {
    using type = NoTransition;
};
template <typename A, typename U>
struct step<Var, A, U> {
    using type = NoTransition;
};
template <typename B, typename A, typename U>
struct step<Rec<B>, A, U> {
    static consteval auto select() {
        if constexpr (is_unfolding_v<U, Rec<B>>) {
            return std::type_identity<NoTransition>{};
        } else {
            return std::type_identity<step_t<unfold_t<Rec<B>>, A, typename push_unfolding<U, Rec<B>>::type>>{};
        }
    }
    using type = typename decltype(select())::type;
};

// The send of branch L from From to To, or NoTransition.  Lost: the
// receiver crashed, so the message goes and the sender continues.
template <bool Lost, typename From, typename To, typename A, typename... Bs>
struct comm_send {
    using type = NoTransition;
};
template <bool Lost, typename From, typename To, typename L, typename B, typename... Ls, typename... Ps, typename... Cs>
struct comm_send<Lost, From, To, SendAction<From, To, L, B>, Branch<Ls, Ps, Cs>...> {
    static constexpr bool is_branch = (std::is_same_v<L, Ls> || ...) && !is_crash_label_v<L>
                                   && std::is_same_v<B, typename payload_of<L, Branch<Ls, Ps, Cs>...>::type>;
    using sent = std::conditional_t<Lost, typename continuation_of<L, Branch<Ls, Ps, Cs>...>::type,
                                    EnRouteChoice<From, To, L, Branch<Ls, Ps, Cs>...>>;
    using type = std::conditional_t<is_branch, sent, NoTransition>;
};

template <bool Lost, typename From, typename To, typename A, typename U, typename... Ls, typename... Ps,
          typename... Cs>
consteval auto comm_step(Branch<Ls, Ps, Cs>*...) {
    using sent = typename comm_send<Lost, From, To, A, Branch<Ls, Ps, Cs>...>::type;
    if constexpr (!std::is_same_v<sent, NoTransition>) {
        return std::type_identity<sent>{};
    } else if constexpr (is_crash_action_v<A> || std::is_same_v<subject_t<A>, From>
                         || std::is_same_v<subject_t<A>, To>) {
        return std::type_identity<NoTransition>{};
    } else if constexpr (all_step_v<step_t<Cs, A, U>...>) {
        using target = std::conditional_t<Lost, Crashed<To>, To>;
        return std::type_identity<Comm<From, target, Branch<Ls, Ps, step_t<Cs, A, U>>...>>{};
    } else {
        return std::type_identity<NoTransition>{};
    }
}

template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename A, typename U>
    requires(!is_crashed_v<To>)
struct step<Comm<From, To, Branch<Ls, Ps, Cs>...>, A, U> {
    using type =
        typename decltype(comm_step<false, From, To, A, U>(static_cast<Branch<Ls, Ps, Cs>*>(nullptr)...))::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename A, typename U>
struct step<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>, A, U> {
    using type =
        typename decltype(comm_step<true, From, To, A, U>(static_cast<Branch<Ls, Ps, Cs>*>(nullptr)...))::type;
};

// The receipt ([GR-&]) or the detection ([GR-⊙]) that an en-route node
// allows.  A live sender never has the crash label en route, so that
// node allows neither.
template <typename From, typename To, typename Chosen, typename... Bs>
struct en_route_actions {
    using type = std::conditional_t<is_crash_label_v<Chosen>, Actions<>,
                                    Actions<RecvAction<To, bare_role_t<From>, Chosen,
                                                       typename payload_of<Chosen, Bs...>::type>>>;
};
template <typename From, typename To, typename... Bs>
struct en_route_actions<Crashed<From>, To, CrashLabel, Bs...> {
    using type = Actions<DetectAction<To, From>>;
};

// [GR-Ctx-ii] reduces the chosen branch, the only live one, and keeps the
// other branches for the receiver's choice.
template <typename From, typename To, typename Chosen, typename A, typename U, typename... Ls, typename... Ps,
          typename... Cs>
consteval auto en_route_step(Branch<Ls, Ps, Cs>*...) {
    using chosen = typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type;
    if constexpr (holds_v<typename en_route_actions<From, To, Chosen, Branch<Ls, Ps, Cs>...>::type, A>) {
        return std::type_identity<chosen>{};
    } else if constexpr (is_crash_action_v<A> || std::is_same_v<subject_t<A>, To>) {
        return std::type_identity<NoTransition>{};
    } else if constexpr (std::is_same_v<step_t<chosen, A, U>, NoTransition>) {
        return std::type_identity<NoTransition>{};
    } else {
        using stepped = step_t<chosen, A, U>;
        return std::type_identity<
            EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, std::conditional_t<std::is_same_v<Ls, Chosen>, stepped, Cs>>...>>{};
    }
}

template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs, typename A,
          typename U>
struct step<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>, A, U> {
    using type =
        typename decltype(en_route_step<From, To, Chosen, A, U>(static_cast<Branch<Ls, Ps, Cs>*>(nullptr)...))::type;
};

// ── The labels a global type offers, before the check ────────────────
//
// Under a transmission a context rule needs the label in every branch,
// so the labels of the first branch are enough candidates.  Under an
// en-route node the chosen branch gives them.  A Rec that the walk is
// already unfolding offers nothing more, for the reason the step gives.

template <typename G, typename U = Unfolding<>>
struct candidates;
template <typename U>
struct candidates<End, U> {
    using type = Actions<>;
};
template <typename U>
struct candidates<Var, U> {
    using type = Actions<>;
};
template <typename B, typename U>
struct candidates<Rec<B>, U> {
    static consteval auto select() {
        if constexpr (is_unfolding_v<U, Rec<B>>) {
            return std::type_identity<Actions<>>{};
        } else {
            return std::type_identity<
                typename candidates<unfold_t<Rec<B>>, typename push_unfolding<U, Rec<B>>::type>::type>{};
        }
    }
    using type = typename decltype(select())::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename U>
struct candidates<Comm<From, To, Branch<Ls, Ps, Cs>...>, U> {
    using type = typename concat<
        std::conditional_t<is_crash_label_v<Ls>, Actions<>, Actions<SendAction<From, bare_role_t<To>, Ls, Ps>>>...,
        typename not_about<typename candidates<Cs...[0], U>::type, From, bare_role_t<To>>::type>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs, typename U>
struct candidates<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>, U> {
    using type = typename concat<
        typename en_route_actions<From, To, Chosen, Branch<Ls, Ps, Cs>...>::type,
        typename not_about<typename candidates<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type, U>::type,
                           To>::type>::type;
};

// ── States ───────────────────────────────────────────────────────────

template <typename Reliable, typename Role>
inline constexpr bool is_reliable_v = ::fixy::session::detail::proj::is_reliable_role_v<Reliable, Role>;

template <typename S, typename A, typename Reliable>
struct state_step {
    using type = NoTransition;
};
template <typename... Cr, typename G, typename A, typename Reliable>
    requires(!is_crash_action_v<A>)
struct state_step<State<Roles<Cr...>, G>, A, Reliable> {
    using next = step_t<G, A>;
    using type = std::conditional_t<std::is_same_v<next, NoTransition>, NoTransition, State<Roles<Cr...>, next>>;
};
// [GR-↯]: a live role that is not reliable crashes, and the state is G
// with the role removed.  A Rec crashes as its unfolding ([GR-µ]).
template <typename... Cr, typename G, typename P, typename Reliable>
struct state_step<State<Roles<Cr...>, G>, CrashAction<P>, Reliable> {
    static consteval auto select() {
        if constexpr (is_reliable_v<Reliable, P> || !role_in_v<P, active_roles_t<G>>) {
            return std::type_identity<NoTransition>{};
        } else {
            using removed = remove_role_t<unfold_t<G>, P>;
            if constexpr (std::is_same_v<removed, RemovalUndefined>) {
                return std::type_identity<NoTransition>{};
            } else {
                return std::type_identity<State<typename role_insert<Roles<Cr...>, P>::type, removed>>{};
            }
        }
    }
    using type = typename decltype(select())::type;
};

template <typename List, typename S, typename Reliable>
struct keep_enabled;
template <typename... As, typename S, typename Reliable>
struct keep_enabled<Actions<As...>, S, Reliable> {
    using type = typename concat<std::conditional_t<
        std::is_same_v<typename state_step<S, As, Reliable>::type, NoTransition>, Actions<>, Actions<As>>...>::type;
};

template <typename RL>
struct crash_candidates;
template <typename... Rs>
struct crash_candidates<Roles<Rs...>> {
    using type = Actions<CrashAction<Rs>...>;
};

}  // namespace detail::lts

// ── The inputs the walks know ────────────────────────────────────────

// A transition label of Definition 4.9.
template <typename A>
struct is_transition_label : std::false_type {};
template <typename P, typename Q, typename L, typename B>
struct is_transition_label<SendAction<P, Q, L, B>> : std::true_type {};
template <typename P, typename Q, typename L, typename B>
struct is_transition_label<RecvAction<P, Q, L, B>> : std::true_type {};
template <typename P>
struct is_transition_label<CrashAction<P>> : std::true_type {};
template <typename P, typename Q>
struct is_transition_label<DetectAction<P, Q>> : std::true_type {};

template <typename A>
inline constexpr bool is_transition_label_v = is_transition_label<A>::value;

// The reliability assumption: a ReliableSet, or EveryRoleReliable for the
// theory without crashes.
template <typename R>
struct is_reliability
    : std::bool_constant<std::is_same_v<R, EveryRoleReliable> || ::fixy::session::is_reliable_set<R>::value> {};

template <typename R>
inline constexpr bool is_reliability_v = is_reliability<R>::value;

// A state ⟨C; G⟩ whose G is well-formed and whose C lists each role once.
template <typename S>
struct is_global_state : std::false_type {};
template <typename... Cr, typename G>
struct is_global_state<State<Roles<Cr...>, G>>
    : std::bool_constant<is_global_well_formed_v<G>
                         && detail::role_union_all<Roles<Cr>...>::type::size == sizeof...(Cr)> {};

template <typename S>
inline constexpr bool is_global_state_v = is_global_state<S>::value;

// The state after the label A, or NoTransition (Definition 4.12).
template <typename S, typename A, typename Reliable>
    requires is_global_state_v<S> && is_transition_label_v<A> && is_reliability_v<Reliable>
using state_step_t = typename detail::lts::state_step<S, A, Reliable>::type;

// Every label the state allows, each once.
template <typename S, typename Reliable>
    requires is_global_state_v<S> && is_reliability_v<Reliable>
using state_enabled_t = typename detail::lts::unique<
    Actions<>,
    typename detail::lts::keep_enabled<
        typename detail::lts::concat<typename detail::lts::candidates<typename S::type>::type,
                                     typename detail::lts::crash_candidates<active_roles_t<typename S::type>>::type>::type,
        S, Reliable>::type>::type;

}  // namespace fixy::session::global

namespace fixy::session::config {

// ── Configurations (Definition 4.18) ─────────────────────────────────

namespace detail {

namespace g = ::fixy::session::global;
namespace proj = ::fixy::session::detail::proj;

using g::Actions;
using g::CrashAction;
using g::DetectAction;
using g::NoTransition;
using g::RecvAction;
using g::SendAction;

// ── Queues ───────────────────────────────────────────────────────────

template <typename Q, typename M>
struct append;
template <typename... Ms, typename M>
struct append<OutQueue<Ms...>, M> {
    using type = OutQueue<Ms..., M>;
};

// The first message of Q to D, or void.
template <typename Q, typename D>
struct head_for;
template <typename D>
struct head_for<OutQueue<>, D> {
    using type = void;
};
template <typename M, typename... Rest, typename D>
struct head_for<OutQueue<M, Rest...>, D> {
    using type = std::conditional_t<std::is_same_v<typename M::to, D>, M, typename head_for<OutQueue<Rest...>, D>::type>;
};

// Q without its first message to D.
template <typename Q, typename D, typename Kept = OutQueue<>>
struct drop_head_for;
template <typename D, typename Kept>
struct drop_head_for<OutQueue<>, D, Kept> {
    using type = Kept;
};
template <typename M, typename... Rest, typename D, typename... Kept>
struct drop_head_for<OutQueue<M, Rest...>, D, OutQueue<Kept...>> {
    using type = std::conditional_t<std::is_same_v<typename M::to, D>, OutQueue<Kept..., Rest...>,
                                    typename drop_head_for<OutQueue<Rest...>, D, OutQueue<Kept..., M>>::type>;
};

// Q without any message to D: the queue to a crashed role is unavailable.
template <typename Q, typename D>
struct drop_all_for;
template <typename... Ms, typename D>
struct drop_all_for<OutQueue<Ms...>, D> {
    using type = typename ::fixy::session::detail::proj::queue_concat<
        std::conditional_t<std::is_same_v<typename Ms::to, D>, OutQueue<>, OutQueue<Ms>>...>::type;
};

// ── Local types ──────────────────────────────────────────────────────

// A local type with each head Loop unfolded ([Γ-µ]).
template <typename T>
struct unfold_local {
    using type = T;
};
template <typename B>
struct unfold_local<Loop<B>> : unfold_local<typename ::fixy::session::detail::proj::unfold<Loop<B>>::type> {};

template <typename T>
using unfold_local_t = typename unfold_local<T>::type;

// The entry of role R in Ctx, or void.
template <typename Ctx, typename R>
struct entry_of;
template <typename R>
struct entry_of<TypingContext<>, R> {
    using type = void;
};
template <typename E, typename... Rest, typename R>
struct entry_of<TypingContext<E, Rest...>, R> {
    using type = std::conditional_t<std::is_same_v<typename E::role, R>, E,
                                    typename entry_of<TypingContext<Rest...>, R>::type>;
};

template <typename Ctx, typename R>
inline constexpr bool is_stopped_v = [] {
    using entry = typename entry_of<Ctx, R>::type;
    if constexpr (std::is_void_v<entry>) {
        return false;
    } else {
        return std::is_same_v<typename entry::local, Stop>;
    }
}();

// The first message that R has from Q in Ctx, or void.
template <typename Ctx, typename R, typename Q>
struct message_from {
    using type = void;
};
template <typename Ctx, typename R, typename Q>
    requires(!std::is_void_v<typename entry_of<Ctx, Q>::type>)
struct message_from<Ctx, R, Q> {
    using type = typename head_for<typename entry_of<Ctx, Q>::type::queue, R>::type;
};

// ── The labels one role offers ───────────────────────────────────────

template <typename Ctx, typename R, typename U>
struct role_actions;
template <typename Ctx, typename R>
struct role_actions<Ctx, R, End> {
    using type = Actions<>;
};
template <typename Ctx, typename R>
struct role_actions<Ctx, R, Stop> {
    using type = Actions<>;
};
template <typename Ctx, typename R, typename Q, typename L, typename B, typename K>
struct role_actions<Ctx, R, Send<PeerMsg<Q, L, B>, K>> {
    using type = Actions<SendAction<R, Q, L, B>>;
};
template <typename Ctx, typename R, typename... Qs, typename... Ls, typename... Bs, typename... Ks>
struct role_actions<Ctx, R, Select<Send<PeerMsg<Qs, Ls, Bs>, Ks>...>> {
    using type = Actions<SendAction<R, Qs, Ls, Bs>...>;
};

// The receipt of the message at the head of the queue from Q, when a
// branch has its label and a payload above its payload.
template <typename R, typename Q, typename M, typename... Branches>
struct receipt {
    using type = Actions<>;
};
template <typename R, typename Q, typename M, typename... Ls, typename... Bs, typename... Ks>
    requires(!std::is_void_v<M>)
struct receipt<R, Q, M, Recv<PeerMsg<Q, Ls, Bs>, Ks>...> {
    static constexpr bool is_received =
        ((std::is_same_v<typename M::label, Ls> && is_payload_subsort_v<typename M::payload, Bs>) || ...);
    using type =
        std::conditional_t<is_received, Actions<RecvAction<R, Q, typename M::label, typename M::payload>>, Actions<>>;
};

// [Γ-⊙]: the peer is at Stop, its queue to R is empty, and a branch is
// the crash branch.
template <typename Ctx, typename R, typename Q, typename... Branches>
struct detection {
    using type = Actions<>;
};
template <typename Ctx, typename R, typename Q, typename... Ls, typename... Bs, typename... Ks>
struct detection<Ctx, R, Q, Recv<PeerMsg<Q, Ls, Bs>, Ks>...> {
    static constexpr bool is_detected = (std::is_same_v<Ls, g::CrashLabel> || ...) && is_stopped_v<Ctx, Q>
                                     && std::is_void_v<typename message_from<Ctx, R, Q>::type>;
    using type = std::conditional_t<is_detected, Actions<DetectAction<R, Q>>, Actions<>>;
};

template <typename Ctx, typename R, typename Q, typename L, typename B, typename K>
struct role_actions<Ctx, R, Recv<PeerMsg<Q, L, B>, K>> {
    using type = typename g::detail::lts::concat<
        typename receipt<R, Q, typename message_from<Ctx, R, Q>::type, Recv<PeerMsg<Q, L, B>, K>>::type,
        typename detection<Ctx, R, Q, Recv<PeerMsg<Q, L, B>, K>>::type>::type;
};
template <typename Ctx, typename R, typename Q, typename... Branches>
struct role_actions<Ctx, R, Offer<Sender<Q>, Branches...>> {
    using type = typename g::detail::lts::concat<
        typename receipt<R, Q, typename message_from<Ctx, R, Q>::type, Branches...>::type,
        typename detection<Ctx, R, Q, Branches...>::type>::type;
};

template <typename Ctx, typename Reliable, typename E>
struct entry_actions {
    using local = unfold_local_t<typename E::local>;
    static constexpr bool may_crash = !g::detail::lts::is_reliable_v<Reliable, typename E::role>
                                   && !std::is_same_v<local, End> && !std::is_same_v<local, Stop>;
    using type = typename g::detail::lts::concat<typename role_actions<Ctx, typename E::role, local>::type,
                                                 std::conditional_t<may_crash, Actions<CrashAction<typename E::role>>,
                                                                    Actions<>>>::type;
};

template <typename Ctx, typename Reliable>
struct actions_of;
template <typename... Es, typename Reliable>
struct actions_of<TypingContext<Es...>, Reliable> {
    using type = typename g::detail::lts::concat<typename entry_actions<TypingContext<Es...>, Reliable, Es>::type...>::type;
};

// ── One step of a configuration ──────────────────────────────────────

// The continuation of the branch of U whose message is M, or void.
template <typename U, typename L, typename B>
struct branch_next {
    using type = void;
};
template <typename Q, typename L, typename B, typename K>
struct branch_next<Send<PeerMsg<Q, L, B>, K>, L, B> {
    using type = K;
};
template <typename... Qs, typename... Ls, typename... Bs, typename... Ks, typename L, typename B>
struct branch_next<Select<Send<PeerMsg<Qs, Ls, Bs>, Ks>...>, L, B> {
    using type = typename g::detail::continuation_of<L, g::Branch<Ls, Bs, Ks>...>::type;
};
template <typename Q, typename L, typename B, typename K, typename L2, typename B2>
struct branch_next<Recv<PeerMsg<Q, L, B>, K>, L2, B2> {
    using type = std::conditional_t<std::is_same_v<L, L2>, K, void>;
};
template <typename Q, typename... Qs, typename... Ls, typename... Bs, typename... Ks, typename L, typename B>
struct branch_next<Offer<Sender<Q>, Recv<PeerMsg<Qs, Ls, Bs>, Ks>...>, L, B> {
    using type = typename g::detail::continuation_of<L, g::Branch<Ls, Bs, Ks>...>::type;
};

// Replaces the entry of each role with its image under the step.
template <typename E, typename A>
struct entry_after;

// [Γ-⊕]: the sender continues, and the message joins its queue unless the
// receiver crashed.
template <typename Ctx, typename A>
struct context_after;
template <typename... Es, typename P, typename Q, typename L, typename B>
struct context_after<TypingContext<Es...>, SendAction<P, Q, L, B>> {
    using Ctx = TypingContext<Es...>;
    template <typename E>
    using image = std::conditional_t<
        std::is_same_v<typename E::role, P>,
        RoleState<P,
                  std::conditional_t<is_stopped_v<Ctx, Q>, typename E::queue,
                                     typename append<typename E::queue, Queued<Q, L, B>>::type>,
                  typename branch_next<unfold_local_t<typename E::local>, L, B>::type>,
        E>;
    using type = TypingContext<image<Es>...>;
};
// [Γ-&]: the receiver continues, and the message leaves the queue of the
// sender.
template <typename... Es, typename P, typename Q, typename L, typename B>
struct context_after<TypingContext<Es...>, RecvAction<P, Q, L, B>> {
    template <typename E>
    using image = std::conditional_t<
        std::is_same_v<typename E::role, P>,
        RoleState<P, typename E::queue, typename branch_next<unfold_local_t<typename E::local>, L, B>::type>,
        std::conditional_t<std::is_same_v<typename E::role, Q>,
                           RoleState<Q, typename drop_head_for<typename E::queue, P>::type, typename E::local>, E>>;
    using type = TypingContext<image<Es>...>;
};
// [Γ-⊙]: the detecting role takes its crash branch.
template <typename... Es, typename P, typename Q>
struct context_after<TypingContext<Es...>, DetectAction<P, Q>> {
    template <typename E>
    using image = std::conditional_t<
        std::is_same_v<typename E::role, P>,
        RoleState<P, typename E::queue,
                  typename branch_next<unfold_local_t<typename E::local>, g::CrashLabel, void>::type>,
        E>;
    using type = TypingContext<image<Es>...>;
};
// [Γ-↯]: the role stops, and each message to it is dropped.
template <typename... Es, typename P>
struct context_after<TypingContext<Es...>, CrashAction<P>> {
    template <typename E>
    using image = std::conditional_t<std::is_same_v<typename E::role, P>, RoleState<P, typename E::queue, Stop>,
                                     RoleState<typename E::role, typename drop_all_for<typename E::queue, P>::type,
                                               typename E::local>>;
    using type = TypingContext<image<Es>...>;
};

// ── The local types the rules read ───────────────────────────────────
//
// A local type of a configuration is End, Stop, a Loop whose body starts
// with an action, or a choice whose messages name their peer with a
// PeerMsg: a Send or a Recv, a Select of Sends to one peer, or an Offer
// with the note of its one sender.  A choice has distinct labels.  No
// Send and no Select sends the crash label, and no Recv or Offer has
// the crash label as its only label (the paper's local types, Section
// 4.1).  A Continue stands only under a Loop.

template <typename T, bool InLoop>
struct is_config_local_ : std::false_type {};

template <bool InLoop>
struct is_config_local_<End, InLoop> : std::true_type {};
template <bool InLoop>
struct is_config_local_<Stop, InLoop> : std::true_type {};
template <>
struct is_config_local_<Continue, true> : std::true_type {};

// A Loop body must start with an action, so an unfolding always ends.
template <typename B>
inline constexpr bool is_guarded_body_v = !std::is_same_v<B, Continue>;
template <typename B>
inline constexpr bool is_guarded_body_v<Loop<B>> = is_guarded_body_v<B>;

template <typename B, bool InLoop>
struct is_config_local_<Loop<B>, InLoop> : std::bool_constant<is_guarded_body_v<B> && is_config_local_<B, true>::value> {};

template <typename Q, typename L, typename B, typename K, bool InLoop>
struct is_config_local_<Send<PeerMsg<Q, L, B>, K>, InLoop>
    : std::bool_constant<!g::detail::is_crash_label_v<L> && is_config_local_<K, InLoop>::value> {};
template <typename Q, typename L, typename B, typename K, bool InLoop>
struct is_config_local_<Recv<PeerMsg<Q, L, B>, K>, InLoop>
    : std::bool_constant<!g::detail::is_crash_label_v<L> && is_config_local_<K, InLoop>::value> {};
template <typename... Qs, typename... Ls, typename... Bs, typename... Ks, bool InLoop>
struct is_config_local_<Select<Send<PeerMsg<Qs, Ls, Bs>, Ks>...>, InLoop>
    : std::bool_constant<(sizeof...(Qs) > 0) && (std::is_same_v<Qs, Qs...[0]> && ...)
                         && g::detail::label_set_distinct_v<Ls...> && (!g::detail::is_crash_label_v<Ls> && ...)
                         && (is_config_local_<Ks, InLoop>::value && ...)> {};
template <typename Q, typename... Qs, typename... Ls, typename... Bs, typename... Ks, bool InLoop>
struct is_config_local_<Offer<Sender<Q>, Recv<PeerMsg<Qs, Ls, Bs>, Ks>...>, InLoop>
    : std::bool_constant<(sizeof...(Qs) > 0) && (std::is_same_v<Qs, Q> && ...)
                         && g::detail::label_set_distinct_v<Ls...> && !(g::detail::is_crash_label_v<Ls> && ...)
                         && (is_config_local_<Ks, InLoop>::value && ...)> {};

}  // namespace detail

// A configuration Γ; ∆ (Definition 4.17): one entry for each role, each
// with an outgoing queue and a local type that the rules read.
template <typename Ctx>
struct is_configuration : std::false_type {};
template <typename... Rs, typename... Qs, typename... Ts>
struct is_configuration<TypingContext<RoleState<Rs, Qs, Ts>...>>
    : std::bool_constant<global::detail::role_union_all<global::Roles<Rs>...>::type::size == sizeof...(Rs)
                         && (::fixy::session::detail::proj::is_queue_shape_v<Qs> && ...)
                         && (detail::is_config_local_<Ts, false>::value && ...)> {};

template <typename Ctx>
inline constexpr bool is_configuration_v = is_configuration<Ctx>::value;

// Every label the configuration allows (Definition 4.18).  A reliable role
// never crashes.
template <typename Ctx, typename Reliable>
    requires is_configuration_v<Ctx> && global::is_reliability_v<Reliable>
using enabled_t =
    typename global::detail::lts::unique<global::Actions<>, typename detail::actions_of<Ctx, Reliable>::type>::type;

namespace detail {

template <typename Ctx, typename A, typename Reliable>
consteval auto step_select() {
    if constexpr (global::detail::lts::holds_v<enabled_t<Ctx, Reliable>, A>) {
        return std::type_identity<typename context_after<Ctx, A>::type>{};
    } else {
        return std::type_identity<global::NoTransition>{};
    }
}

}  // namespace detail

// The configuration after the label A, or NoTransition when the
// configuration does not allow A.
template <typename Ctx, typename A, typename Reliable>
    requires is_configuration_v<Ctx> && global::is_transition_label_v<A> && global::is_reliability_v<Reliable>
using step_t = typename decltype(detail::step_select<Ctx, A, Reliable>())::type;

}  // namespace fixy::session::config

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::config::detail::witness {

namespace gw = ::fixy::session::global::detail::witness;

using SendsX = Send<PeerMsg<gw::RoleB, gw::LabelX, int>, End>;
using ReceivesX = Recv<PeerMsg<gw::RoleA, gw::LabelX, int>, End>;
using LoopsX = Loop<Send<PeerMsg<gw::RoleB, gw::LabelX, int>, Continue>>;
using GuardedX = Offer<Sender<gw::RoleA>, Recv<PeerMsg<gw::RoleA, gw::LabelX, int>, End>,
                       Recv<PeerMsg<gw::RoleA, ::fixy::session::global::CrashLabel, void>, End>>;
using Pair = TypingContext<RoleState<gw::RoleA, OutQueue<>, SendsX>, RoleState<gw::RoleB, OutQueue<>, ReceivesX>>;
using SentAndStopped = TypingContext<RoleState<gw::RoleA, OutQueue<Queued<gw::RoleB, gw::LabelX, int>>, Stop>,
                                     RoleState<gw::RoleB, OutQueue<>, GuardedX>>;

}  // namespace fixy::session::config::detail::witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_transition_label> {
    using accepts = witnesses<
        ::fixy::session::global::SendAction<::fixy::session::global::detail::witness::RoleA,
                                            ::fixy::session::global::detail::witness::RoleB,
                                            ::fixy::session::global::detail::witness::LabelX, int>,
        ::fixy::session::global::RecvAction<::fixy::session::global::detail::witness::RoleB,
                                            ::fixy::session::global::detail::witness::RoleA,
                                            ::fixy::session::global::detail::witness::LabelX, int>,
        ::fixy::session::global::CrashAction<::fixy::session::global::detail::witness::RoleA>,
        ::fixy::session::global::DetectAction<::fixy::session::global::detail::witness::RoleB,
                                              ::fixy::session::global::detail::witness::RoleA>>;
    using refuses = witnesses<int, ::fixy::session::global::Actions<>, ::fixy::session::global::NoTransition>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_reliability> {
    using accepts = witnesses<::fixy::session::EveryRoleReliable, ::fixy::session::ReliableSet<>,
                              ::fixy::session::ReliableSet<::fixy::session::global::detail::witness::RoleA>>;
    using refuses = witnesses<int, ::fixy::session::global::Roles<::fixy::session::global::detail::witness::RoleA>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_global_state> {
    using accepts = witnesses<
        ::fixy::session::global::State<::fixy::session::global::Roles<>, ::fixy::session::global::End>,
        ::fixy::session::global::State<::fixy::session::global::Roles<>,
                                       ::fixy::session::global::detail::witness::Forever>,
        ::fixy::session::global::State<
            ::fixy::session::global::Roles<::fixy::session::global::detail::witness::RoleB>,
            ::fixy::session::global::detail::witness::ReceiverCrashed>>;
    using refuses = witnesses<
        int, ::fixy::session::global::End, ::fixy::session::global::State<int, ::fixy::session::global::End>,
        ::fixy::session::global::State<::fixy::session::global::Roles<>, int>,
        ::fixy::session::global::State<::fixy::session::global::Roles<::fixy::session::global::detail::witness::RoleA,
                                                                      ::fixy::session::global::detail::witness::RoleA>,
                                       ::fixy::session::global::End>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::config::is_configuration> {
    using accepts = witnesses<::fixy::session::TypingContext<>, ::fixy::session::config::detail::witness::Pair,
                              ::fixy::session::config::detail::witness::SentAndStopped>;
    using refuses = witnesses<
        int, ::fixy::session::config::detail::witness::SendsX,
        ::fixy::session::TypingContext<::fixy::session::RoleState<::fixy::session::global::detail::witness::RoleA,
                                                                  ::fixy::session::OutQueue<>,
                                                                  ::fixy::session::Recv<int, ::fixy::session::End>>>,
        ::fixy::session::TypingContext<
            ::fixy::session::RoleState<::fixy::session::global::detail::witness::RoleA, ::fixy::session::OutQueue<>,
                                       ::fixy::session::End>,
            ::fixy::session::RoleState<::fixy::session::global::detail::witness::RoleA, ::fixy::session::OutQueue<>,
                                       ::fixy::session::End>>,
        ::fixy::session::TypingContext<::fixy::session::RoleState<::fixy::session::global::detail::witness::RoleA,
                                                                  ::fixy::session::OutQueue<int>,
                                                                  ::fixy::session::End>>>;
};

template <>
struct foundation::contracts::armed_instances<^^::fixy::session::config::detail::is_config_local_> {
    using accepts = witnesses<
        ::fixy::session::config::detail::is_config_local_<::fixy::session::End, false>,
        ::fixy::session::config::detail::is_config_local_<::fixy::session::Stop, false>,
        ::fixy::session::config::detail::is_config_local_<::fixy::session::Continue, true>,
        ::fixy::session::config::detail::is_config_local_<::fixy::session::config::detail::witness::LoopsX, false>,
        ::fixy::session::config::detail::is_config_local_<::fixy::session::config::detail::witness::GuardedX, false>>;
    using refuses = witnesses<
        ::fixy::session::config::detail::is_config_local_<::fixy::session::Continue, false>,
        ::fixy::session::config::detail::is_config_local_<
            ::fixy::session::Loop<::fixy::session::Continue>, false>,
        ::fixy::session::config::detail::is_config_local_<::fixy::session::Recv<int, ::fixy::session::End>, false>,
        ::fixy::session::config::detail::is_config_local_<
            ::fixy::session::Recv<::fixy::session::PeerMsg<::fixy::session::global::detail::witness::RoleA,
                                                           ::fixy::session::global::CrashLabel, void>,
                                  ::fixy::session::End>,
            false>,
        ::fixy::session::config::detail::is_config_local_<
            ::fixy::session::Offer<::fixy::session::Recv<
                ::fixy::session::PeerMsg<::fixy::session::global::detail::witness::RoleA,
                                         ::fixy::session::global::detail::witness::LabelX, int>,
                ::fixy::session::End>>,
            false>>;
};
