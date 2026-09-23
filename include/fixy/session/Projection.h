#pragma once

// Projection of a global type onto one role, and association of a
// typing context with a global type.  The rules follow Pischke, Masters
// and Yoshida, "Asynchronous Global Protocols, Precisely" (arXiv
// 2505.17676, version 4): the projection of Definition 4 and the
// association of Definition 21.
//
// The projection of G onto a role p is a pair: the queue of messages
// that p sent and nobody received yet, and the local type of p.  The
// queue comes from the EnRoute nodes of G whose sender is p.
//
// A local type names the peer of each action.  The message type of a
// Send or a Recv is PeerMsg<Peer, Label, Payload>: in a Send the peer
// is the receiver, in a Recv the peer is the sender.  A multiparty local
// type that drops the peer cannot say to whom a message goes, so two
// roles that send the same label to different peers would share one
// local type.  An internal choice with several branches is a Select of
// Sends.  An external choice with several branches is an Offer that
// names its sender with Sender<Peer>.  A choice with one branch is a
// plain Send or Recv.
//
// The rule for a role that no path of G activates (P-END) is what
// makes a loop that the role never enters project to End.  The check
// asks for the active roles of the loop body, which with nearest-binder
// recursion are the active roles of every unfolding of the loop.
//
// A role that does not take part in a choice cannot see which branch
// was taken, so the projections of the branches must merge (rule
// P-merge).  This header implements the full merge inductively: two
// internal choices merge when they have the same labels, and two
// external choices merge into the union of their labels.  When the two
// operands differ in shape and one of them is a Loop, the merge unfolds
// the Loop and tries again, a bounded number of times.  Page 9 of the
// paper notes that the inductive full projection is a subrelation of the
// coinductive one.  The coinductive merge reads each type up to
// unfolding, so a merge of an unfolded Loop is one of its results too.
// So each projection that this header gives is a projection of
// Definition 4.  Some global types that the coinductive projection
// accepts are refused here, with a reason that names the limit.
//
// One queue per ordered pair of roles.  Queue elements with different
// receivers commute (Definition 1), so two queues are equivalent when,
// for each receiver, they hold the same messages in the same order.
// A model where roles share one queue is the case that makes the
// subject reduction of Honda, Yoshida and Carbone (2008) fail (Tirore,
// Bengtson and Carbone, ECOOP 2025).
//
// Association (Definition 21): a typing context associates with G when
// it holds an entry for each role of G, and for each such role its local
// type and its queue refine the projection.  The refinement relation is
// precise asynchronous subtyping.  Until fixy/session/Subtype.h provides
// that relation, this header uses equality for local types and queue
// equivalence for queues.  Equality is a subset of subtyping, so each
// association this header accepts is an association of the paper.  A
// role of the context that G does not name must have finished, which is
// the rule of Pischke and Yoshida, "Top-down = Bottom-up" (OOPSLA 2026),
// Definition 6.9.  Definition 21 does not constrain such a role, but a
// role that has not finished cannot be live with no peer.

#include <fixy/session/Global.h>
#include <fixy/session/Protocol.h>
#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::session {

// ── Local messages and queues ────────────────────────────────────────

template <typename Peer, typename Label, typename Payload>
struct PeerMsg {
    using peer = Peer;
    using label = Label;
    using payload = Payload;
};

// One element of an outgoing queue: a message to To.
template <typename To, typename Label, typename Payload>
struct Queued {
    using to = To;
    using label = Label;
    using payload = Payload;
};

template <typename... Ms>
struct OutQueue {
    static constexpr std::size_t size = sizeof...(Ms);
};

// The projection of a global type onto one role.
template <typename Queue, typename Local>
struct Projected {
    using queue = Queue;
    using local = Local;
};

// A projection that does not exist.  Reason names the rule that refused.
template <typename Reason>
struct NotProjectable {
    using reason = Reason;
};

namespace projection_failure {
struct QueueDiffersAcrossBranches {};
struct MergeShapeMismatch {};
struct MergeLabelSetMismatch {};
struct MergePayloadMismatch {};
struct MergeUnfoldLimit {};
struct EnRouteUnderRecursion {};
struct LoopWithoutAction {};
}  // namespace projection_failure

template <typename T>
struct is_projection_failure : std::false_type {};
template <typename Reason>
struct is_projection_failure<NotProjectable<Reason>> : std::true_type {};

template <typename T>
inline constexpr bool is_projection_failure_v = is_projection_failure<T>::value;

// How many times one merge path can unfold a Loop before it gives up.
inline constexpr int projection_unfold_limit = 4;

namespace detail::proj {

namespace g = ::fixy::session::global;

template <typename...>
inline constexpr bool dependent_false_v = false;

// ── Queues ───────────────────────────────────────────────────────────

template <typename... Qs>
struct queue_concat;
template <>
struct queue_concat<> {
    using type = OutQueue<>;
};
template <typename... A>
struct queue_concat<OutQueue<A...>> {
    using type = OutQueue<A...>;
};
template <typename... A, typename... B, typename... Rest>
struct queue_concat<OutQueue<A...>, OutQueue<B...>, Rest...> : queue_concat<OutQueue<A..., B...>, Rest...> {};

template <typename Q, typename M>
struct queue_prepend;
template <typename... Ms, typename M>
struct queue_prepend<OutQueue<Ms...>, M> {
    using type = OutQueue<M, Ms...>;
};

// The messages of Q whose receiver is D, in order.
template <typename Q, typename D>
struct queue_for;
template <typename... Ms, typename D>
struct queue_for<OutQueue<Ms...>, D> {
    using type =
        typename queue_concat<std::conditional_t<std::is_same_v<typename Ms::to, D>, OutQueue<Ms>, OutQueue<>>...>::type;
};

template <typename Q>
struct queue_receivers;
template <typename... Ms>
struct queue_receivers<OutQueue<Ms...>> {
    using type = typename g::detail::role_union_all<g::Roles<typename Ms::to>...>::type;
};

template <typename Q1, typename Q2, typename Receivers>
struct queue_match;
template <typename Q1, typename Q2, typename... Ds>
struct queue_match<Q1, Q2, g::Roles<Ds...>>
    : std::bool_constant<(std::is_same_v<typename queue_for<Q1, Ds>::type, typename queue_for<Q2, Ds>::type> && ...)> {};

}  // namespace detail::proj

// True when Q1 and Q2 hold, for each receiver, the same messages in the
// same order (Definition 1).
template <typename Q1, typename Q2>
inline constexpr bool queues_equivalent_v =
    detail::proj::queue_match<Q1, Q2,
                              typename detail::proj::g::detail::role_union<
                                  typename detail::proj::queue_receivers<Q1>::type,
                                  typename detail::proj::queue_receivers<Q2>::type>::type>::value;

namespace detail::proj {

// ── Choice view ──────────────────────────────────────────────────────
//
// The merge reads a local type as a side (internal or external), a
// peer, and a list of branches, and it writes the result back in the
// canonical form: one branch is a Send or a Recv, several branches are
// a Select or an Offer.

template <typename L, typename P, typename K>
struct Br {
    using label = L;
    using payload = P;
    using next = K;
};

template <typename... Brs>
struct BL {
    static constexpr std::size_t size = sizeof...(Brs);
};

enum class Side : std::uint8_t {
    Neither,
    Internal,
    External,
};

template <typename T>
struct view {
    static constexpr Side side = Side::Neither;
    using peer = void;
    using branches = BL<>;
};
template <typename Q, typename L, typename P, typename K>
struct view<Send<PeerMsg<Q, L, P>, K>> {
    static constexpr Side side = Side::Internal;
    using peer = Q;
    using branches = BL<Br<L, P, K>>;
};
template <typename Q, typename L, typename P, typename K>
struct view<Recv<PeerMsg<Q, L, P>, K>> {
    static constexpr Side side = Side::External;
    using peer = Q;
    using branches = BL<Br<L, P, K>>;
};
template <typename... Qs, typename... Ls, typename... Ps, typename... Ks>
struct view<Select<Send<PeerMsg<Qs, Ls, Ps>, Ks>...>> {
    static constexpr bool one_peer = sizeof...(Qs) > 0 && (std::is_same_v<Qs, Qs...[0]> && ...);
    static constexpr Side side = one_peer ? Side::Internal : Side::Neither;
    using peer = std::conditional_t<one_peer, Qs...[0], void>;
    using branches = BL<Br<Ls, Ps, Ks>...>;
};
template <typename Q, typename... Qs, typename... Ls, typename... Ps, typename... Ks>
struct view<Offer<Sender<Q>, Recv<PeerMsg<Qs, Ls, Ps>, Ks>...>> {
    static constexpr bool one_peer = sizeof...(Qs) > 0 && (std::is_same_v<Qs, Q> && ...);
    static constexpr Side side = one_peer ? Side::External : Side::Neither;
    using peer = Q;
    using branches = BL<Br<Ls, Ps, Ks>...>;
};

template <Side S, typename Peer, typename Branches>
struct rebuild;
template <typename Peer, typename L, typename P, typename K>
struct rebuild<Side::Internal, Peer, BL<Br<L, P, K>>> {
    using type = Send<PeerMsg<Peer, L, P>, K>;
};
template <typename Peer, typename... Brs>
struct rebuild<Side::Internal, Peer, BL<Brs...>> {
    using type = Select<Send<PeerMsg<Peer, typename Brs::label, typename Brs::payload>, typename Brs::next>...>;
};
template <typename Peer, typename L, typename P, typename K>
struct rebuild<Side::External, Peer, BL<Br<L, P, K>>> {
    using type = Recv<PeerMsg<Peer, L, P>, K>;
};
template <typename Peer, typename... Brs>
struct rebuild<Side::External, Peer, BL<Brs...>> {
    using type = Offer<Sender<Peer>, Recv<PeerMsg<Peer, typename Brs::label, typename Brs::payload>, typename Brs::next>...>;
};

template <typename... Lists>
struct bl_concat;
template <>
struct bl_concat<> {
    using type = BL<>;
};
template <typename... A>
struct bl_concat<BL<A...>> {
    using type = BL<A...>;
};
template <typename... A, typename... B, typename... Rest>
struct bl_concat<BL<A...>, BL<B...>, Rest...> : bl_concat<BL<A..., B...>, Rest...> {};

// The branch of List whose label is L, or void.
template <typename L, typename List>
struct find_label {
    using type = void;
};
template <typename L, typename First, typename... Rest>
struct find_label<L, BL<First, Rest...>> {
    using type = std::conditional_t<std::is_same_v<L, typename First::label>, First,
                                    typename find_label<L, BL<Rest...>>::type>;
};

template <typename... Ts>
struct first_failure {
    using type = void;
};
template <typename T, typename... Rest>
struct first_failure<T, Rest...> {
    using type = std::conditional_t<is_projection_failure_v<T>, T, typename first_failure<Rest...>::type>;
};

// ── Loop unfolding ───────────────────────────────────────────────────
//
// unfold replaces each Continue that the Loop binds with the Loop
// itself.  A nested Loop binds its own Continue, so the walk stops
// there.  The replacement is closed, because a Continue always binds to
// the nearest Loop, so no capture can occur.

template <typename T, typename Rep>
struct subst;
template <typename Rep>
struct subst<End, Rep> {
    using type = End;
};
template <typename Rep>
struct subst<Continue, Rep> {
    using type = Rep;
};
template <typename M, typename K, typename Rep>
struct subst<Send<M, K>, Rep> {
    using type = Send<M, typename subst<K, Rep>::type>;
};
template <typename M, typename K, typename Rep>
struct subst<Recv<M, K>, Rep> {
    using type = Recv<M, typename subst<K, Rep>::type>;
};
template <typename... Bs, typename Rep>
struct subst<Select<Bs...>, Rep> {
    using type = Select<typename subst<Bs, Rep>::type...>;
};
template <typename... Bs, typename Rep>
struct subst<Offer<Bs...>, Rep> {
    using type = Offer<typename subst<Bs, Rep>::type...>;
};
template <typename Role, typename... Bs, typename Rep>
struct subst<Offer<Sender<Role>, Bs...>, Rep> {
    using type = Offer<Sender<Role>, typename subst<Bs, Rep>::type...>;
};
template <typename B, typename Rep>
struct subst<Loop<B>, Rep> {
    using type = Loop<B>;
};

// A Loop node that the projection built.  is_loop_v also admits a
// VendorPinned wrapper, which a projection never produces.
template <typename T>
inline constexpr bool is_loop_node_v = false;
template <typename B>
inline constexpr bool is_loop_node_v<Loop<B>> = true;

template <typename T>
struct unfold {
    using type = T;
};
template <typename B>
struct unfold<Loop<B>> {
    using type = typename subst<B, Loop<B>>::type;
};

// ── Merge ────────────────────────────────────────────────────────────

template <typename A, typename B, int Fuel>
struct merge2;

template <typename A, typename List, int Fuel>
consteval auto merge_external_branch() {
    using partner = typename find_label<typename A::label, List>::type;
    if constexpr (std::is_void_v<partner>) {
        return std::type_identity<A>{};
    } else if constexpr (!std::is_same_v<typename A::payload, typename partner::payload>) {
        return std::type_identity<NotProjectable<projection_failure::MergePayloadMismatch>>{};
    } else {
        using next = typename merge2<typename A::next, typename partner::next, Fuel>::type;
        if constexpr (is_projection_failure_v<next>) {
            return std::type_identity<next>{};
        } else {
            return std::type_identity<Br<typename A::label, typename A::payload, next>>{};
        }
    }
}

template <typename A, typename List, int Fuel>
consteval auto merge_internal_branch() {
    using partner = typename find_label<typename A::label, List>::type;
    if constexpr (std::is_void_v<partner>) {
        return std::type_identity<NotProjectable<projection_failure::MergeLabelSetMismatch>>{};
    } else if constexpr (!std::is_same_v<typename A::payload, typename partner::payload>) {
        return std::type_identity<NotProjectable<projection_failure::MergePayloadMismatch>>{};
    } else {
        using next = typename merge2<typename A::next, typename partner::next, Fuel>::type;
        if constexpr (is_projection_failure_v<next>) {
            return std::type_identity<next>{};
        } else {
            return std::type_identity<Br<typename A::label, typename A::payload, next>>{};
        }
    }
}

// Two internal choices to the same peer merge when they offer the same
// labels (rule merge-internal).
template <typename Peer, int Fuel, typename... As, typename... Bs>
consteval auto merge_internal(BL<As...>, BL<Bs...>) {
    if constexpr (sizeof...(As) != sizeof...(Bs)) {
        return std::type_identity<NotProjectable<projection_failure::MergeLabelSetMismatch>>{};
    } else {
        using failure =
            typename first_failure<typename decltype(merge_internal_branch<As, BL<Bs...>, Fuel>())::type...>::type;
        if constexpr (!std::is_void_v<failure>) {
            return std::type_identity<failure>{};
        } else {
            return std::type_identity<typename rebuild<
                Side::Internal, Peer, BL<typename decltype(merge_internal_branch<As, BL<Bs...>, Fuel>())::type...>>::type>{};
        }
    }
}

// Two external choices from the same peer merge into the union of their
// labels (rule merge-external).
template <typename Peer, int Fuel, typename... As, typename... Bs>
consteval auto merge_external(BL<As...>, BL<Bs...>) {
    using failure =
        typename first_failure<typename decltype(merge_external_branch<As, BL<Bs...>, Fuel>())::type...>::type;
    if constexpr (!std::is_void_v<failure>) {
        return std::type_identity<failure>{};
    } else {
        using merged = BL<typename decltype(merge_external_branch<As, BL<Bs...>, Fuel>())::type...>;
        using extra = typename bl_concat<std::conditional_t<std::is_void_v<typename find_label<typename Bs::label, BL<As...>>::type>,
                                                            BL<Bs>, BL<>>...>::type;
        return std::type_identity<typename rebuild<Side::External, Peer, typename bl_concat<merged, extra>::type>::type>{};
    }
}

template <typename A, typename B, int Fuel>
consteval auto merge_select() {
    if constexpr (is_projection_failure_v<A>) {
        return std::type_identity<A>{};
    } else if constexpr (is_projection_failure_v<B>) {
        return std::type_identity<B>{};
    } else if constexpr (std::is_same_v<A, B>) {
        return std::type_identity<A>{};
    } else if constexpr (is_loop_node_v<A> || is_loop_node_v<B>) {
        if constexpr (Fuel <= 0) {
            return std::type_identity<NotProjectable<projection_failure::MergeUnfoldLimit>>{};
        } else {
            return std::type_identity<typename merge2<typename unfold<A>::type, typename unfold<B>::type, Fuel - 1>::type>{};
        }
    } else if constexpr (view<A>::side == Side::Internal && view<B>::side == Side::Internal
                         && std::is_same_v<typename view<A>::peer, typename view<B>::peer>) {
        return merge_internal<typename view<A>::peer, Fuel>(typename view<A>::branches{}, typename view<B>::branches{});
    } else if constexpr (view<A>::side == Side::External && view<B>::side == Side::External
                         && std::is_same_v<typename view<A>::peer, typename view<B>::peer>) {
        return merge_external<typename view<A>::peer, Fuel>(typename view<A>::branches{}, typename view<B>::branches{});
    } else {
        return std::type_identity<NotProjectable<projection_failure::MergeShapeMismatch>>{};
    }
}

template <typename A, typename B, int Fuel>
struct merge2 {
    using type = typename decltype(merge_select<A, B, Fuel>())::type;
};

template <typename... Ts>
struct merge_all;
template <typename T>
struct merge_all<T> {
    using type = T;
};
template <typename T1, typename T2, typename... Rest>
struct merge_all<T1, T2, Rest...> : merge_all<typename merge2<T1, T2, projection_unfold_limit>::type, Rest...> {};

// ── Projection walk ──────────────────────────────────────────────────

template <typename G, typename R>
struct proj_walk;

template <typename B, typename R>
consteval auto proj_rec() {
    if constexpr (!g::holds_free_var_v<B>) {
        // A Rec whose body never loops back is its body.
        return std::type_identity<typename proj_walk<B, R>::type>{};
    } else if constexpr (g::holds_en_route_v<B>) {
        return std::type_identity<NotProjectable<projection_failure::EnRouteUnderRecursion>>{};
    } else if constexpr (!g::role_in_v<R, g::active_roles_t<B>>) {
        // P-END: no unfolding of the loop activates R.
        return std::type_identity<Projected<OutQueue<>, End>>{};
    } else {
        using inner = typename proj_walk<B, R>::type;
        if constexpr (is_projection_failure_v<inner>) {
            return std::type_identity<inner>{};
        } else if constexpr (std::is_same_v<typename inner::local, Continue>) {
            return std::type_identity<NotProjectable<projection_failure::LoopWithoutAction>>{};
        } else {
            return std::type_identity<Projected<typename inner::queue, Loop<typename inner::local>>>{};
        }
    }
}

template <typename Result, typename First>
inline constexpr bool queue_agrees_v = queues_equivalent_v<typename Result::queue, typename First::queue>;

template <typename From, typename To, typename R, typename... Ls, typename... Ps, typename... Cs>
consteval auto proj_comm(BL<Br<Ls, Ps, Cs>...>) {
    using failure = typename first_failure<typename proj_walk<Cs, R>::type...>::type;
    if constexpr (!std::is_void_v<failure>) {
        return std::type_identity<failure>{};
    } else {
        using first = typename proj_walk<Cs...[0], R>::type;
        if constexpr (!(queue_agrees_v<typename proj_walk<Cs, R>::type, first> && ...)) {
            return std::type_identity<NotProjectable<projection_failure::QueueDiffersAcrossBranches>>{};
        } else if constexpr (std::is_same_v<R, From>) {
            return std::type_identity<Projected<
                typename first::queue,
                typename rebuild<Side::Internal, To, BL<Br<Ls, Ps, typename proj_walk<Cs, R>::type::local>...>>::type>>{};
        } else if constexpr (std::is_same_v<R, To>) {
            return std::type_identity<Projected<
                typename first::queue,
                typename rebuild<Side::External, From, BL<Br<Ls, Ps, typename proj_walk<Cs, R>::type::local>...>>::type>>{};
        } else {
            using merged = typename merge_all<typename proj_walk<Cs, R>::type::local...>::type;
            if constexpr (is_projection_failure_v<merged>) {
                return std::type_identity<merged>{};
            } else {
                return std::type_identity<Projected<typename first::queue, merged>>{};
            }
        }
    }
}

template <typename From, typename To, typename L, typename P, typename C, typename R>
consteval auto proj_en_route() {
    using inner = typename proj_walk<C, R>::type;
    if constexpr (is_projection_failure_v<inner>) {
        return std::type_identity<inner>{};
    } else if constexpr (std::is_same_v<R, From>) {
        return std::type_identity<
            Projected<typename queue_prepend<typename inner::queue, Queued<To, L, P>>::type, typename inner::local>>{};
    } else if constexpr (std::is_same_v<R, To>) {
        return std::type_identity<Projected<typename inner::queue, Recv<PeerMsg<From, L, P>, typename inner::local>>>{};
    } else {
        return std::type_identity<inner>{};
    }
}

template <typename R>
struct proj_walk<g::End, R> {
    using type = Projected<OutQueue<>, End>;
};
template <typename R>
struct proj_walk<g::Var, R> {
    using type = Projected<OutQueue<>, Continue>;
};
template <typename B, typename R>
struct proj_walk<g::Rec<B>, R> {
    using type = typename decltype(proj_rec<B, R>())::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename R>
struct proj_walk<g::Comm<From, To, g::Branch<Ls, Ps, Cs>...>, R> {
    using type = typename decltype(proj_comm<From, To, R>(BL<Br<Ls, Ps, Cs>...>{}))::type;
};
template <typename From, typename To, typename L, typename P, typename C, typename R>
struct proj_walk<g::EnRoute<From, To, L, P, C>, R> {
    using type = typename decltype(proj_en_route<From, To, L, P, C, R>())::type;
};

}  // namespace detail::proj

// The projection of G onto R: a Projected<Queue, Local>, or a
// NotProjectable<Reason>.  G must be well-formed.
template <typename G, typename R>
    requires global::is_global_well_formed_v<G>
using project_t = typename detail::proj::proj_walk<G, R>::type;

template <typename G, typename R>
    requires global::is_global_well_formed_v<G>
inline constexpr bool projects_v = !is_projection_failure_v<project_t<G, R>>;

template <typename G, typename R>
consteval void ensure_projectable() noexcept {
    global::ensure_global_well_formed<G>();
    if constexpr (global::is_global_well_formed_v<G>) {
        using P = project_t<G, R>;
        if constexpr (is_projection_failure_v<P>) {
            using reason = typename P::reason;
            if constexpr (std::is_same_v<reason, projection_failure::MergeShapeMismatch>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Merge_Shape_Mismatch]: a role that does not "
                              "take part in a choice sees continuations of different shape in the branches (for "
                              "example a send in one branch and End in another, or two different peers).  The role "
                              "cannot tell the branches apart, so no single local type fits.  Tell the role which "
                              "branch was taken with a message in each branch.");
            } else if constexpr (std::is_same_v<reason, projection_failure::MergeLabelSetMismatch>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Merge_Label_Set_Mismatch]: a role that does "
                              "not take part in a choice must send different labels in different branches.  A "
                              "sender cannot select on a choice it did not see.  Tell the role which branch was "
                              "taken before it sends.");
            } else if constexpr (std::is_same_v<reason, projection_failure::MergePayloadMismatch>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Merge_Payload_Mismatch]: two branches carry "
                              "the same label with different payload types to a role that does not see the choice.  "
                              "Use one payload type for one label, or use two labels.");
            } else if constexpr (std::is_same_v<reason, projection_failure::MergeUnfoldLimit>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Merge_Unfold_Limit]: the merge unfolded a loop "
                              "projection_unfold_limit times without a match.  The coinductive projection can "
                              "accept such a protocol, but this inductive merge does not.  Restructure the loop so "
                              "that the branches meet sooner.");
            } else if constexpr (std::is_same_v<reason, projection_failure::QueueDiffersAcrossBranches>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Queue_Differs_Across_Branches]: the role has "
                              "different messages en route in different branches of one choice.  Its queue must be "
                              "the same whichever branch was taken.");
            } else if constexpr (std::is_same_v<reason, projection_failure::EnRouteUnderRecursion>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_EnRoute_Under_Recursion]: an EnRoute node sits "
                              "inside a Rec that loops.  Each iteration would add a message, so the queue has no "
                              "fixed contents.");
            } else if constexpr (std::is_same_v<reason, projection_failure::LoopWithoutAction>) {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Loop_Without_Action]: the projection of a loop "
                              "body is only a loop-back, so the role would loop for ever without an action.");
            } else {
                static_assert(detail::proj::dependent_false_v<G, R>,
                              "fixy::session::diagnostic [Projection_Refused]: the projection refused for a reason "
                              "this gate does not name.");
            }
        }
    }
}

// ── Typing contexts and association ──────────────────────────────────

template <typename Role, typename Queue, typename Local>
struct RoleState {
    using role = Role;
    using queue = Queue;
    using local = Local;
};

template <typename... States>
struct TypingContext {
    static constexpr std::size_t size = sizeof...(States);
};

namespace detail::proj {

template <typename G, typename... Rs>
consteval auto context_select(g::Roles<Rs...>) {
    using failure = typename first_failure<project_t<G, Rs>...>::type;
    if constexpr (!std::is_void_v<failure>) {
        return std::type_identity<failure>{};
    } else {
        return std::type_identity<
            TypingContext<RoleState<Rs, typename project_t<G, Rs>::queue, typename project_t<G, Rs>::local>...>>{};
    }
}

template <typename Q>
inline constexpr bool is_queue_shape_v = false;
template <typename... Tos, typename... Ls, typename... Ps>
inline constexpr bool is_queue_shape_v<OutQueue<Queued<Tos, Ls, Ps>...>> = true;

template <typename Domain, typename RL>
struct domain_covers;
template <typename Domain, typename... Rs>
struct domain_covers<Domain, g::Roles<Rs...>> : std::bool_constant<(g::role_in_v<Rs, Domain> && ...)> {};

enum class AssociationFault : std::uint8_t {
    None,
    NotBalancedPlus,
    NotAContext,
    DuplicateRole,
    MissingRole,
    ExtraRoleNotTerminated,
    RoleNotProjectable,
    QueueMismatch,
    LocalMismatch,
};

template <typename State, typename G>
consteval AssociationFault state_fault() {
    if constexpr (!g::role_in_v<typename State::role, g::roles_t<G>>) {
        // A role outside G has finished (Pischke and Yoshida, "Top-down
        // = Bottom-up", Definition 6.9): an empty queue and End.
        if constexpr (std::is_same_v<typename State::queue, OutQueue<>> && std::is_same_v<typename State::local, End>) {
            return AssociationFault::None;
        } else {
            return AssociationFault::ExtraRoleNotTerminated;
        }
    } else {
        using P = project_t<G, typename State::role>;
        if constexpr (is_projection_failure_v<P>) {
            return AssociationFault::RoleNotProjectable;
        } else if constexpr (!queues_equivalent_v<typename State::queue, typename P::queue>) {
            return AssociationFault::QueueMismatch;
        } else if constexpr (!std::is_same_v<typename State::local, typename P::local>) {
            // TODO(fixy/session/Subtype.h): replace equality with precise
            // asynchronous subtyping, the refinement of Definition 21.
            return AssociationFault::LocalMismatch;
        } else {
            return AssociationFault::None;
        }
    }
}

template <typename Ctx, typename G>
struct association_walk {
    static constexpr AssociationFault value =
        g::is_balanced_plus_v<G> ? AssociationFault::NotAContext : AssociationFault::NotBalancedPlus;
};
template <typename... Rs, typename... Qs, typename... Ts, typename G>
struct association_walk<TypingContext<RoleState<Rs, Qs, Ts>...>, G> {
    static consteval AssociationFault compute() {
        using domain = typename g::detail::role_union_all<g::Roles<Rs>...>::type;
        if constexpr (!g::is_balanced_plus_v<G>) {
            // Definition 21 is stated for well-formed global types, and
            // the paper's well-formed types are balanced+ (Definition
            // 17).  The projected context of equation (49) matches its
            // projection exactly and is still unsafe.
            return AssociationFault::NotBalancedPlus;
        } else if constexpr (!(is_queue_shape_v<Qs> && ...)) {
            return AssociationFault::NotAContext;
        } else if constexpr (domain::size != sizeof...(Rs)) {
            return AssociationFault::DuplicateRole;
        } else if constexpr (!domain_covers<domain, g::roles_t<G>>::value) {
            return AssociationFault::MissingRole;
        } else {
            AssociationFault fault = AssociationFault::None;
            ((fault = fault == AssociationFault::None ? state_fault<RoleState<Rs, Qs, Ts>, G>() : fault), ...);
            return fault;
        }
    }
    static constexpr AssociationFault value = compute();
};

}  // namespace detail::proj

// The typing context that projects G onto each of its roles, or the
// first projection failure.
template <typename G>
    requires global::is_global_well_formed_v<G>
using projected_context_t = typename decltype(detail::proj::context_select<G>(global::roles_t<G>{}))::type;

// Definition 21 of the paper, with equality in place of subtyping until
// fixy/session/Subtype.h lands.  The context holds each role of G once.
// A role of the context that G does not name must have finished.  G must
// be well-formed, and the association is false when G is not balanced+.
// A projection exists for some global types that are not balanced+, so
// a projection alone proves nothing about the processes.
template <typename Ctx, typename G>
    requires global::is_global_well_formed_v<G>
inline constexpr bool association_holds_v =
    detail::proj::association_walk<Ctx, G>::value == detail::proj::AssociationFault::None;

template <typename Ctx, typename G>
consteval void ensure_associated() noexcept {
    global::ensure_global_well_formed<G>();
    if constexpr (global::is_global_well_formed_v<G>) {
        using F = detail::proj::AssociationFault;
        constexpr F fault = detail::proj::association_walk<Ctx, G>::value;
        if constexpr (fault == F::NotBalancedPlus) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Global_Not_Balanced_Plus]: the global type is not "
                          "balanced+ (Pischke, Masters, Yoshida, Definition 17).  A context can match each projection "
                          "of such a type and still be unsafe.  Call ensure_balanced_plus<G>() to see why.");
        } else if constexpr (fault == F::NotAContext) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Not_A_Context]: the context is not a "
                          "TypingContext<RoleState<Role, Queue, Local>...>.");
        } else if constexpr (fault == F::DuplicateRole) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Duplicate_Role]: a role has two RoleState entries "
                          "in the context.  Give each role exactly one entry.");
        } else if constexpr (fault == F::MissingRole) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Missing_Role]: a role of the global type has no "
                          "RoleState in the context.  Give each role of G an entry.");
        } else if constexpr (fault == F::ExtraRoleNotTerminated) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Extra_Role_Not_Terminated]: the context holds a "
                          "role that the global type does not name, and that role has not finished.  Its queue must "
                          "be OutQueue<> and its local type End.");
        } else if constexpr (fault == F::RoleNotProjectable) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Role_Not_Projectable]: the global type has no "
                          "projection onto a role of the context.  Call ensure_projectable<G, Role>() to see why.");
        } else if constexpr (fault == F::QueueMismatch) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Queue_Mismatch]: the queue of a role does not "
                          "hold, for some receiver, the messages that the global type has en route from that role.");
        } else if constexpr (fault == F::LocalMismatch) {
            static_assert(detail::proj::dependent_false_v<Ctx, G>,
                          "fixy::session::diagnostic [Association_Local_Mismatch]: the local type of a role is not "
                          "its projection.  Until fixy/session/Subtype.h lands, association asks for the projected "
                          "type itself.");
        }
    }
}

// ── The binary view ──────────────────────────────────────────────────
//
// strip_peers_t removes the peer from each message and each Offer.  For
// a global type with two roles each local type has one peer, so the
// stripped types are binary session types, and the projections onto
// the two roles are duals of each other.  This is the bridge to the
// binary handle of fixy/session/Handle.h.
//
// A local type with two peers has no binary view.  Its sends to one
// peer and its receives from the other would share one channel, and the
// label on the wire would reach the wrong process.  So strip_peers_t
// requires a local type that names at most one peer.

template <typename Label, typename Payload>
struct Labelled {
    using label = Label;
    using payload = Payload;
};

namespace detail::proj {

// The peers that a local type names, each once.  The primary has no
// definition, so a combinator the walk does not know stops the build.
template <typename T>
struct peer_walk;
template <>
struct peer_walk<End> {
    using type = g::Roles<>;
};
template <>
struct peer_walk<Continue> {
    using type = g::Roles<>;
};
template <typename Q, typename L, typename P, typename K>
struct peer_walk<Send<PeerMsg<Q, L, P>, K>> {
    using type = typename g::detail::role_union<g::Roles<Q>, typename peer_walk<K>::type>::type;
};
template <typename Q, typename L, typename P, typename K>
struct peer_walk<Recv<PeerMsg<Q, L, P>, K>> {
    using type = typename g::detail::role_union<g::Roles<Q>, typename peer_walk<K>::type>::type;
};
template <typename... Bs>
struct peer_walk<Select<Bs...>> {
    using type = typename g::detail::role_union_all<typename peer_walk<Bs>::type...>::type;
};
template <typename Role, typename... Bs>
struct peer_walk<Offer<Sender<Role>, Bs...>> {
    using type = typename g::detail::role_union_all<g::Roles<Role>, typename peer_walk<Bs>::type...>::type;
};
template <typename B>
struct peer_walk<Loop<B>> {
    using type = typename peer_walk<B>::type;
};

}  // namespace detail::proj

template <typename Local>
using local_peers_t = typename detail::proj::peer_walk<Local>::type;

namespace detail::proj {

template <typename T>
struct strip;
template <>
struct strip<End> {
    using type = End;
};
template <>
struct strip<Continue> {
    using type = Continue;
};
template <typename Q, typename L, typename P, typename K>
struct strip<Send<PeerMsg<Q, L, P>, K>> {
    using type = Send<Labelled<L, P>, typename strip<K>::type>;
};
template <typename Q, typename L, typename P, typename K>
struct strip<Recv<PeerMsg<Q, L, P>, K>> {
    using type = Recv<Labelled<L, P>, typename strip<K>::type>;
};
template <typename... Bs>
struct strip<Select<Bs...>> {
    using type = Select<typename strip<Bs>::type...>;
};
template <typename Role, typename... Bs>
struct strip<Offer<Sender<Role>, Bs...>> {
    using type = Offer<typename strip<Bs>::type...>;
};
template <typename B>
struct strip<Loop<B>> {
    using type = Loop<typename strip<B>::type>;
};

}  // namespace detail::proj

template <typename Local>
    requires(local_peers_t<Local>::size <= 1)
using strip_peers_t = typename detail::proj::strip<Local>::type;

}  // namespace fixy::session

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_projection_failure> {
    using accepts = witnesses<::fixy::session::NotProjectable<::fixy::session::projection_failure::MergeShapeMismatch>,
                              ::fixy::session::NotProjectable<int>>;
    using refuses = witnesses<int, ::fixy::session::Projected<::fixy::session::OutQueue<>, ::fixy::session::End>,
                              ::fixy::session::End>;
};
