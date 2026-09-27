#pragma once

// Global types.  A global type describes a multiparty protocol as one
// object that names every role and every message.  Projection
// (fixy/session/Projection.h) extracts the local type of each role from
// it.  The combinators and the checks in this header follow Pischke,
// Masters and Yoshida, "Asynchronous Global Protocols, Precisely"
// (arXiv 2505.17676, version 4), section 2.1 and section 4.1.
//
// A transmission Comm<From, To, Branch<Label, Payload, Cont>...> is the
// global type p->q:{m_i(B_i).G_i}.  Each branch has a label, a payload
// type and a continuation.  The labels of one transmission must be
// pairwise distinct, because a receiver identifies the branch by its
// label.  A transmission with one branch is a plain message, and Msg is
// the short form of it.
//
// EnRouteChoice<From, To, Chosen, Branch<Label, Payload, Cont>...> is a
// transmission that From sent and To has not received: the global type
// p ⇝ q : j {m_i(B_i).G_i} of Barwell, Hou, Yoshida and Zhou (LMCS 21:2,
// 2025, section 4.1).  Chosen is the label of the branch that From sent.
// Each branch stays, because the receiver still holds the whole choice:
// its projection is the external choice over every branch, the crash
// branch too.  EnRoute<From, To, Label, Payload, Cont> is the form with
// one branch, the en-route message of Pischke, Masters and Yoshida.  An
// en-route node occurs only in the runtime types that a protocol
// reaches.  A static specification has none.
//
// Only the chosen branch of an en-route node is live.  After From sends
// the chosen label, no other branch can run, and those branches stay
// only to give the receiver its choice.  So each walk here that reads
// the roles or the en-route count reads the chosen branch, as the
// en-route node of Pischke, Masters and Yoshida has that branch alone.
// Barwell et al. read every branch, and their rule [GR-Ctx-ii] then asks
// every branch to act.  In our reading that leaves a gap in their
// Theorem 4.20 (misc/session_types_literature.md, section 5, item 12).
//
// Rec<Body> binds Var.  A Var always refers to the nearest Rec around
// it, and a nested Rec hides the outer one.  This matches Loop and
// Continue in fixy/session/Protocol.h, so a projected local type can
// express each loop of the global type.  One consequence: a Var never
// refers past the nearest Rec, so no path can leave a loop and come
// back to an outer loop.
//
// Well-formedness here is syntactic.  Every transmission has at least
// one branch, distinct labels and two different roles, every Var has a
// binder, and every Rec body is guarded by a communication.
//
// Balancedness is Definition 14 of the paper: each role has a bounded
// depth (Definition 13) in each global type that the protocol can
// reach.  With nearest-binder recursion the condition is structural.
// For each Rec and each role in its body, every path from the body to a
// Var of that Rec must meet an action of that role.  Otherwise the path
// can repeat for ever without the role, the depth of the role does not
// exist, and the role can starve.  A bounded buffer does not give this
// property.  It is a property of the shape of the protocol.
//
// Balanced+ is Definition 17: balanced, and for each pair of roles the
// count of en-route messages (Definition 16) exists.  The count exists
// when it agrees across the branches of each transmission and no
// en-route message sits below a recursion binder.  Below an en-route
// node the count is the count of its chosen branch.  A static
// specification has no en-route message, so it is balanced+ when it is
// balanced.  Theorem 3 of the paper shows that transitions keep the
// count.
//
// The checks are evaluated at each syntactic position of the global
// type.  A reachable global type (Definition 7) is built from those
// positions by removal of a prefix and by addition of en-route nodes,
// and Lemma 4 shows that the depth of a role does not increase along a
// transition that is not an action of that role.  So a depth that
// exists at each syntactic position exists in each reachable type.
//
// Each walk has a primary template that is declared and not defined.
// A node that no walk knows about is a hard error, not a silent
// default.  A header that adds a global combinator, for example a crash
// annotation, adds a specialization of each walk beside that
// combinator.  If it forgets one, the build stops.
//
// Crash-stop failures follow Barwell, Hou, Yoshida and Zhou, "Crash-Stop
// Failures in Asynchronous Multiparty Session Types" (LMCS 21:2, 2025),
// section 4.1.  The paper adds no syntax to a design-time global type
// except a special label.  Here that label is CrashLabel: the branch
// Branch<CrashLabel, void, G> of a transmission from p to q is what q
// does when it detects that p crashed.  No role sends the label.  A
// transmission cannot have the crash branch as its only branch, and the
// branch carries no payload.
//
// The runtime annotation Crashed<Role> marks a role that crashed.  It
// takes the two positions of Figure 6 of that paper: the receiver of a
// Comm, and the sender of an EnRoute.  role removal (Definition 4.10,
// remove_role_t below) writes it.  Each walk of this header has a
// specialization for the two annotated nodes.  roles_t names each
// participant, crashed or not.  crashed_roles_t names each role that
// carries the annotation somewhere.  The paper's set of crashed roles
// (Definition 4.1) leaves out a crashed sender of an en-route message
// (its Remark 4.2), so crashed_roles_t can be larger, and each check
// that reads it is stricter than the paper.

#include <foundation/algebra/Transition.h>
#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <type_traits>
#include <vector>

namespace fixy::session::global {

// ── Combinators ──────────────────────────────────────────────────────

struct End {};

struct Var {};

template <typename Body>
struct Rec {
    using body = Body;
};

template <typename Label, typename Payload, typename Cont>
struct Branch {
    using label = Label;
    using payload = Payload;
    using next = Cont;
};

template <typename From, typename To, typename... Branches>
struct Comm {
    using from = From;
    using to = To;
    static constexpr std::size_t branch_count = sizeof...(Branches);
};

template <typename From, typename To, typename Label, typename Payload, typename Cont>
using Msg = Comm<From, To, Branch<Label, Payload, Cont>>;

template <typename From, typename To, typename Chosen, typename... Branches>
struct EnRouteChoice {
    using from = From;
    using to = To;
    using chosen = Chosen;
    static constexpr std::size_t branch_count = sizeof...(Branches);
};

template <typename From, typename To, typename Label, typename Payload, typename Cont>
using EnRoute = EnRouteChoice<From, To, Label, Branch<Label, Payload, Cont>>;

// The label of a crash handling branch.  It is a pseudo-message: the
// receiver takes the branch when it detects that the sender crashed.
struct CrashLabel {};

// The runtime annotation of a crashed role.  Comm<From, Crashed<To>,
// ...> is a transmission to a crashed receiver: From still sends, and
// the message is lost.  EnRouteChoice<Crashed<From>, To, ...> is a
// message that From sent before it crashed.  Its chosen label is
// CrashLabel when it is the pseudo-message that lets To detect the crash.
template <typename Role>
struct Crashed {
    using role = Role;
};

// ── The members of a global node ─────────────────────────────────────
//
// The walks of this header match the template arguments of a node, but a
// projection reads nested members too, such as `next` of a Branch.  An
// explicit specialization could give a member a value that the arguments
// do not give, and the projection would then be wrong with no error.  The
// node reader below claims each member from the arguments, as the reader
// of fixy/session/Protocol.h does for a local node, and the projection
// refuses a node whose members disagree.  The reader is a function that
// is not a template, so no program can specialize a claim away.
// test/fixy/test_session_node_members.cpp walks the members of each
// combinator and fails when one has a member that no claim names.
namespace detail {

[[nodiscard]] consteval ::foundation::algebra::transition::node_members global_node_members(std::meta::info type) {
    using ::foundation::algebra::transition::member_claim;
    ::foundation::algebra::transition::node_members result{};
    const std::meta::info shape = ::foundation::algebra::transition::shape_of(type);
    result.is_node = type == ^^End || type == ^^Var;
    if (result.is_node || shape == type) return result;
    const auto arguments = std::meta::template_arguments_of(type);
    result.is_node = true;
    if (shape == ^^Rec) {
        result.claims = {member_claim{"body", arguments[0]}};
        result.children = {arguments[0]};
    } else if (shape == ^^Branch) {
        result.claims = {member_claim{"label", arguments[0]}, member_claim{"payload", arguments[1]},
                         member_claim{"next", arguments[2]}};
        result.children = {arguments[2]};
    } else if (shape == ^^Comm || shape == ^^EnRouteChoice) {
        // A Comm has two leading arguments and an en-route choice three,
        // the chosen label the third.  The branches follow, and a role can
        // be a Crashed node.
        const std::ptrdiff_t first_branch = shape == ^^Comm ? 2 : 3;
        result.children = {arguments.begin() + first_branch, arguments.end()};
        result.claims = {member_claim{"from", arguments[0]}, member_claim{"to", arguments[1]},
                         member_claim{"branch_count", std::meta::reflect_constant(result.children.size())}};
        if (shape == ^^EnRouteChoice) result.claims.push_back(member_claim{"chosen", arguments[2]});
        result.children.push_back(arguments[0]);
        result.children.push_back(arguments[1]);
    } else if (shape == ^^Crashed) {
        result.claims = {member_claim{"role", arguments[0]}};
    } else {
        result.is_node = false;
    }
    return result;
}

}  // namespace detail

// ── Role lists ───────────────────────────────────────────────────────
//
// A role list holds each role once, in the order of first occurrence.
// Membership compares types exactly.  There is no hash, so two distinct
// roles can never merge into one.

template <typename... Rs>
struct Roles {
    static constexpr std::size_t size = sizeof...(Rs);
};

namespace detail {

template <typename...>
inline constexpr bool dependent_false_v = false;

template <typename R, typename RL>
struct is_role_in;
template <typename R, typename... Rs>
struct is_role_in<R, Roles<Rs...>> : std::bool_constant<(std::is_same_v<R, Rs> || ...)> {};

template <typename RL, typename R>
struct role_insert;
template <typename... Rs, typename R>
struct role_insert<Roles<Rs...>, R> {
    using type = std::conditional_t<(std::is_same_v<R, Rs> || ...), Roles<Rs...>, Roles<Rs..., R>>;
};

template <typename RL, typename RL2>
struct role_union;
template <typename RL>
struct role_union<RL, Roles<>> {
    using type = RL;
};
template <typename RL, typename R, typename... Rest>
struct role_union<RL, Roles<R, Rest...>> : role_union<typename role_insert<RL, R>::type, Roles<Rest...>> {};

template <typename... RLs>
struct role_union_all;
template <>
struct role_union_all<> {
    using type = Roles<>;
};
template <typename RL, typename... Rest>
struct role_union_all<RL, Rest...> : role_union<RL, typename role_union_all<Rest...>::type> {};

// The roles of RL that are not in Remove, in the order of RL.
template <typename RL, typename Remove>
struct role_difference;
template <typename... Rs, typename Remove>
struct role_difference<Roles<Rs...>, Remove> {
    using type = typename role_union_all<std::conditional_t<is_role_in<Rs, Remove>::value, Roles<>, Roles<Rs>>...>::type;
};

template <typename R>
inline constexpr bool is_crashed_v = false;
template <typename R>
inline constexpr bool is_crashed_v<Crashed<R>> = true;

template <typename L>
inline constexpr bool is_crash_label_v = std::is_same_v<L, CrashLabel>;

// The role that a possibly crashed role names.
template <typename R>
struct bare_role {
    using type = R;
};
template <typename R>
struct bare_role<Crashed<R>> {
    using type = R;
};
template <typename R>
using bare_role_t = typename bare_role<R>::type;

// The continuation of the branch labelled L, or void.
template <typename L, typename... Bs>
struct continuation_of {
    using type = void;
};
template <typename L, typename First, typename... Rest>
struct continuation_of<L, First, Rest...> {
    using type = std::conditional_t<std::is_same_v<L, typename First::label>, typename First::next,
                                    typename continuation_of<L, Rest...>::type>;
};

// The payload of the branch labelled L, or void.
template <typename L, typename... Bs>
struct payload_of {
    using type = void;
};
template <typename L, typename First, typename... Rest>
struct payload_of<L, First, Rest...> {
    using type = std::conditional_t<std::is_same_v<L, typename First::label>, typename First::payload,
                                    typename payload_of<L, Rest...>::type>;
};

}  // namespace detail

template <typename R, typename RL>
inline constexpr bool role_in_v = detail::is_role_in<R, RL>::value;

template <typename RL1, typename RL2>
inline constexpr bool roles_equal_as_sets_v =
    RL1::size == RL2::size && []<typename... Rs>(Roles<Rs...>*) { return (role_in_v<Rs, RL2> && ...); }(
                                  static_cast<RL1*>(nullptr));

// ── Recognition ──────────────────────────────────────────────────────
//
// is_global_type accepts a type built only from the combinators above.
// Its "no" is the correct answer for any other type, so the gates below
// ask it first and never walk a type it refuses.

template <typename G>
struct is_global_type : std::false_type {};
template <>
struct is_global_type<End> : std::true_type {};
template <>
struct is_global_type<Var> : std::true_type {};
template <typename Body>
struct is_global_type<Rec<Body>> : is_global_type<Body> {};
template <typename From, typename To, typename... Bs>
    requires(!detail::is_crashed_v<To>)
struct is_global_type<Comm<From, To, Bs...>> : std::false_type {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct is_global_type<Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_global_type<Cs>::value && ...)> {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct is_global_type<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_global_type<Cs>::value && ...)> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct is_global_type<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_global_type<Cs>::value && ...)> {};

template <typename G>
inline constexpr bool is_global_type_v = is_global_type<G>::value;

// ── Role functions (Definition 3) ────────────────────────────────────
//
// roles_t: the participants.  active_roles_t: the roles that can still
// act, that is a receiver, or the sender of a transmission that is not
// en route.  sending_roles_t: the roles that sent an en-route message.

namespace detail {

template <typename G>
struct role_walk;
template <>
struct role_walk<End> {
    using type = Roles<>;
};
template <>
struct role_walk<Var> {
    using type = Roles<>;
};
template <typename Body>
struct role_walk<Rec<Body>> : role_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct role_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<
        Roles<bare_role_t<From>, To>,
        typename role_walk<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type>::type>::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct role_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename role_walk<Cs>::type...>::type;
};

template <typename G>
struct active_role_walk;
template <>
struct active_role_walk<End> {
    using type = Roles<>;
};
template <>
struct active_role_walk<Var> {
    using type = Roles<>;
};
template <typename Body>
struct active_role_walk<Rec<Body>> : active_role_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct active_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename active_role_walk<Cs>::type...>::type;
};
// The receiver of an en-route message can act, and so can each role of
// the chosen branch (Definition 4.1 of the crash-stop paper reads every
// branch).
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct active_role_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<
        Roles<To>,
        typename active_role_walk<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type>::type>::type;
};
// A crashed receiver cannot act.  The sender still sends.
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct active_role_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From>, typename active_role_walk<Cs>::type...>::type;
};

template <typename G>
struct sending_role_walk;
template <>
struct sending_role_walk<End> {
    using type = Roles<>;
};
template <>
struct sending_role_walk<Var> {
    using type = Roles<>;
};
template <typename Body>
struct sending_role_walk<Rec<Body>> : sending_role_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct sending_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<typename sending_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct sending_role_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<typename sending_role_walk<Cs>::type...>::type;
};
// A message that a role sent before it crashed stays in the queue.  The
// crash pseudo-message is not in a queue.
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct sending_role_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<
        std::conditional_t<is_crash_label_v<Chosen>, Roles<>, Roles<bare_role_t<From>>>,
        typename sending_role_walk<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type>::type>::type;
};

// The roles that carry the crash annotation somewhere in G.
template <typename G>
struct crashed_role_walk;
template <>
struct crashed_role_walk<End> {
    using type = Roles<>;
};
template <>
struct crashed_role_walk<Var> {
    using type = Roles<>;
};
template <typename Body>
struct crashed_role_walk<Rec<Body>> : crashed_role_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct crashed_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<typename crashed_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<From>)
struct crashed_role_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<typename crashed_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct crashed_role_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<To>, typename crashed_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct crashed_role_walk<EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From>, typename crashed_role_walk<Cs>::type...>::type;
};

// The roles that occur somewhere without the crash annotation.
template <typename G>
struct plain_role_walk;
template <>
struct plain_role_walk<End> {
    using type = Roles<>;
};
template <>
struct plain_role_walk<Var> {
    using type = Roles<>;
};
template <typename Body>
struct plain_role_walk<Rec<Body>> : plain_role_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct plain_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename plain_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<From>)
struct plain_role_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename plain_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct plain_role_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From>, typename plain_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct plain_role_walk<EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<To>, typename plain_role_walk<Cs>::type...>::type;
};

// True when G holds a Var that no Rec inside G binds.
template <typename G>
struct has_free_var;
template <>
struct has_free_var<End> : std::false_type {};
template <>
struct has_free_var<Var> : std::true_type {};
template <typename Body>
struct has_free_var<Rec<Body>> : std::false_type {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct has_free_var<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(has_free_var<Cs>::value || ...)> {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct has_free_var<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_free_var<Cs>::value || ...)> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct has_free_var<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_free_var<Cs>::value || ...)> {};

// True when G holds an en-route node anywhere.
template <typename G>
struct has_en_route;
template <>
struct has_en_route<End> : std::false_type {};
template <>
struct has_en_route<Var> : std::false_type {};
template <typename Body>
struct has_en_route<Rec<Body>> : has_en_route<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct has_en_route<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(has_en_route<Cs>::value || ...)> {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct has_en_route<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> : std::true_type {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct has_en_route<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_en_route<Cs>::value || ...)> {};

// True when G holds an en-route node whose sender is live.  The crash
// pseudo-message of a crashed sender, EnRouteChoice<Crashed<P>, ...>,
// does not count, but the walk reads its branches.
template <typename G>
struct has_live_en_route;
template <>
struct has_live_en_route<End> : std::false_type {};
template <>
struct has_live_en_route<Var> : std::false_type {};
template <typename Body>
struct has_live_en_route<Rec<Body>> : has_live_en_route<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct has_live_en_route<Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_live_en_route<Cs>::value || ...)> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct has_live_en_route<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_live_en_route<Cs>::value || ...)> {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<From>)
struct has_live_en_route<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>> : std::true_type {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct has_live_en_route<EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_live_en_route<Cs>::value || ...)> {};

}  // namespace detail

template <typename G>
using roles_t = typename detail::role_walk<G>::type;

template <typename G>
using active_roles_t = typename detail::active_role_walk<G>::type;

template <typename G>
using sending_roles_t = typename detail::sending_role_walk<G>::type;

template <typename G>
inline constexpr bool holds_free_var_v = detail::has_free_var<G>::value;

template <typename G>
inline constexpr bool holds_en_route_v = detail::has_en_route<G>::value;

template <typename G>
inline constexpr bool holds_live_en_route_v = detail::has_live_en_route<G>::value;

template <typename G>
using crashed_roles_t = typename detail::crashed_role_walk<G>::type;

// ── Well-formedness ──────────────────────────────────────────────────
//
// The walk returns the first fault it finds, in depth-first order, so a
// diagnostic can name the fault and not only report that one exists.

enum class GlobalFault : std::uint8_t {
    None,
    EmptyChoice,
    SelfCommunication,
    DuplicateLabel,
    UnboundVariable,
    UnguardedRecursion,
    CrashOnlyChoice,
    CrashLabelPayload,
    MisplacedCrashAnnotation,
    UnknownChosenLabel,
};

namespace detail {

// Complexity: quadratic in the number of branches of one transmission.
template <typename L, typename... Ls>
inline constexpr std::size_t label_occurrences_v = ((std::is_same_v<L, Ls> ? std::size_t{1} : std::size_t{0}) + ...
                                                    + std::size_t{0});

template <typename... Ls>
inline constexpr bool label_set_distinct_v = ((label_occurrences_v<Ls, Ls...> == 1) && ...);

// A Rec body is guarded when its first node is a communication.  A
// nested Rec passes the question to its own body.
template <typename Body>
struct is_rec_guarded : std::true_type {};
template <>
struct is_rec_guarded<Var> : std::false_type {};
template <typename Inner>
struct is_rec_guarded<Rec<Inner>> : is_rec_guarded<Inner> {};

consteval GlobalFault first_fault(std::initializer_list<GlobalFault> faults) noexcept {
    for (const GlobalFault fault : faults) {
        if (fault != GlobalFault::None) return fault;
    }
    return GlobalFault::None;
}

template <typename L, typename P>
inline constexpr bool crash_payload_ok_v = !is_crash_label_v<L> || std::is_void_v<P>;

template <typename G, bool Bound>
struct fault_walk;

// The faults of one transmission, before its continuations.  From and
// To are the roles as written, so a Crashed that wraps the sender, or a
// Crashed inside a Crashed, is a misplaced annotation.
template <bool Bound, typename From, typename To, typename... Bs>
struct comm_fault;
template <bool Bound, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct comm_fault<Bound, From, To, Branch<Ls, Ps, Cs>...>
    : std::integral_constant<GlobalFault, std::is_same_v<From, To>   ? GlobalFault::SelfCommunication
                                          : is_crashed_v<From> || is_crashed_v<To> ? GlobalFault::MisplacedCrashAnnotation
                                          : !label_set_distinct_v<Ls...>            ? GlobalFault::DuplicateLabel
                                          : (is_crash_label_v<Ls> && ...)           ? GlobalFault::CrashOnlyChoice
                                          : !(crash_payload_ok_v<Ls, Ps> && ...)    ? GlobalFault::CrashLabelPayload
                                                                                    : first_fault({fault_walk<Cs, Bound>::value...})> {};

// The faults of one en-route node.  A live role never sends the crash
// label, and a crashed receiver has no en-route message (Definition
// 4.10 removes it).  The chosen label names exactly one branch.
template <bool Bound, bool FromCrashed, typename From, typename To, typename Chosen, typename... Bs>
struct en_route_fault;
template <bool Bound, bool FromCrashed, typename From, typename To, typename Chosen, typename... Ls, typename... Ps,
          typename... Cs>
struct en_route_fault<Bound, FromCrashed, From, To, Chosen, Branch<Ls, Ps, Cs>...>
    : std::integral_constant<GlobalFault,
                             std::is_same_v<From, To> ? GlobalFault::SelfCommunication
                             : is_crashed_v<From> || is_crashed_v<To> || (!FromCrashed && is_crash_label_v<Chosen>)
                                 ? GlobalFault::MisplacedCrashAnnotation
                             : !label_set_distinct_v<Ls...>          ? GlobalFault::DuplicateLabel
                             : label_occurrences_v<Chosen, Ls...> != 1 ? GlobalFault::UnknownChosenLabel
                             : !(crash_payload_ok_v<Ls, Ps> && ...)  ? GlobalFault::CrashLabelPayload
                                                                     : first_fault({fault_walk<Cs, Bound>::value...})> {};
template <bool Bound>
struct fault_walk<End, Bound> : std::integral_constant<GlobalFault, GlobalFault::None> {};
template <bool Bound>
struct fault_walk<Var, Bound>
    : std::integral_constant<GlobalFault, Bound ? GlobalFault::None : GlobalFault::UnboundVariable> {};
template <typename Body, bool Bound>
struct fault_walk<Rec<Body>, Bound>
    : std::integral_constant<GlobalFault, is_rec_guarded<Body>::value ? fault_walk<Body, true>::value
                                                                 : GlobalFault::UnguardedRecursion> {};
template <typename From, typename To, bool Bound>
struct fault_walk<Comm<From, To>, Bound> : std::integral_constant<GlobalFault, GlobalFault::EmptyChoice> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, bool Bound>
    requires(!detail::is_crashed_v<To>)
struct fault_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>, Bound> : comm_fault<Bound, From, To, Branch<Ls, Ps, Cs>...> {};
template <typename From, typename To, typename Chosen, typename... Bs, bool Bound>
    requires(!detail::is_crashed_v<From>)
struct fault_walk<EnRouteChoice<From, To, Chosen, Bs...>, Bound> : en_route_fault<Bound, false, From, To, Chosen, Bs...> {};
template <typename From, typename To, bool Bound>
struct fault_walk<Comm<From, Crashed<To>>, Bound> : std::integral_constant<GlobalFault, GlobalFault::EmptyChoice> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, bool Bound>
struct fault_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>, Bound>
    : comm_fault<Bound, From, To, Branch<Ls, Ps, Cs>...> {};
template <typename From, typename To, typename Chosen, typename... Bs, bool Bound>
struct fault_walk<EnRouteChoice<Crashed<From>, To, Chosen, Bs...>, Bound>
    : en_route_fault<Bound, true, From, To, Chosen, Bs...> {};

}  // namespace detail

// The first well-formedness fault of G.  G must be a global type.
template <typename G>
    requires is_global_type_v<G>
inline constexpr GlobalFault global_fault_v = detail::fault_walk<G, false>::value;

template <typename G>
struct is_global_well_formed
    : std::bool_constant<[] {
          if constexpr (is_global_type_v<G>) {
              return detail::fault_walk<G, false>::value == GlobalFault::None;
          } else {
              return false;
          }
      }()> {};

template <typename G>
inline constexpr bool is_global_well_formed_v = is_global_well_formed<G>::value;

// ── Balancedness (Definitions 13 and 14) ─────────────────────────────

namespace detail {

// True when every path from G to a Var of the Rec around G meets an
// action of R.  An action of R is a transmission that R sends or
// receives, or an en-route message that R receives.  A path that ends
// at End meets no Var, so it passes.  A path that enters a nested Rec
// can never come back to the outer Var, because Var binds to the
// nearest Rec, so it passes too.  The nested Rec is examined on its own.
template <typename R, typename G>
struct is_met_on_every_path;
template <typename R>
struct is_met_on_every_path<R, End> : std::true_type {};
template <typename R>
struct is_met_on_every_path<R, Var> : std::false_type {};
template <typename R, typename Body>
struct is_met_on_every_path<R, Rec<Body>> : std::true_type {};
template <typename R, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct is_met_on_every_path<R, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<std::is_same_v<R, From> || std::is_same_v<R, To> || (is_met_on_every_path<R, Cs>::value && ...)> {};
// The receipt of an en-route message is an action of the receiver, and
// so is the detection of a crash.
template <typename R, typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct is_met_on_every_path<R, EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<std::is_same_v<R, To> || (is_met_on_every_path<R, Cs>::value && ...)> {};
// The crashed receiver does not act.
template <typename R, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct is_met_on_every_path<R, Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<std::is_same_v<R, From> || (is_met_on_every_path<R, Cs>::value && ...)> {};

template <typename Body, typename RL>
struct is_loop_met_by_each;
template <typename Body, typename... Rs>
struct is_loop_met_by_each<Body, Roles<Rs...>> : std::bool_constant<(is_met_on_every_path<Rs, Body>::value && ...)> {};

// True when each Rec in G passes is_loop_met_by_each for each role in its
// body that has not crashed.  A crashed role cannot starve.
// Complexity: O(|G| * |roles|) walks of O(|G|) each.
template <typename G>
struct is_each_loop_balanced;
template <>
struct is_each_loop_balanced<End> : std::true_type {};
template <>
struct is_each_loop_balanced<Var> : std::true_type {};
template <typename Body>
struct is_each_loop_balanced<Rec<Body>>
    : std::bool_constant<
          is_loop_met_by_each<Body, typename role_difference<typename role_walk<Body>::type,
                                                         typename crashed_role_walk<Body>::type>::type>::value
          && is_each_loop_balanced<Body>::value> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct is_each_loop_balanced<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(is_each_loop_balanced<Cs>::value && ...)> {};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs>
struct is_each_loop_balanced<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_each_loop_balanced<Cs>::value && ...)> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct is_each_loop_balanced<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_each_loop_balanced<Cs>::value && ...)> {};

}  // namespace detail

template <typename G>
struct is_balanced : std::bool_constant<[] {
    if constexpr (is_global_well_formed_v<G>) {
        return detail::is_each_loop_balanced<G>::value;
    } else {
        return false;
    }
}()> {};

template <typename G>
inline constexpr bool is_balanced_v = is_balanced<G>::value;

// ── The en-route count (Definitions 15 and 16) ───────────────────────

namespace detail {

// The count of en-route messages from P to Q along the paths of G, or
// -1 where Definition 16 leaves it undefined.
inline constexpr std::int64_t count_undefined = -1;

// True when G holds an en-route message from P to Q (the pair is in
// mRoles(G), Definition 15).
template <typename P, typename Q, typename G>
struct has_en_route_pair;
template <typename P, typename Q>
struct has_en_route_pair<P, Q, End> : std::false_type {};
template <typename P, typename Q>
struct has_en_route_pair<P, Q, Var> : std::false_type {};
template <typename P, typename Q, typename Body>
struct has_en_route_pair<P, Q, Rec<Body>> : has_en_route_pair<P, Q, Body> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct has_en_route_pair<P, Q, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_en_route_pair<P, Q, Cs>::value || ...)> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct has_en_route_pair<P, Q, Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(has_en_route_pair<P, Q, Cs>::value || ...)> {};
// The crash pseudo-message is not in a queue.  Only the chosen branch
// is live.
template <typename P, typename Q, typename From, typename To, typename Chosen, typename... Ls, typename... Pls,
          typename... Cs>
struct has_en_route_pair<P, Q, EnRouteChoice<From, To, Chosen, Branch<Ls, Pls, Cs>...>>
    : std::bool_constant<(std::is_same_v<P, bare_role_t<From>> && std::is_same_v<Q, To> && !is_crash_label_v<Chosen>)
                         || has_en_route_pair<P, Q, typename continuation_of<Chosen, Branch<Ls, Pls, Cs>...>::type>::value> {};

consteval std::int64_t agreed_count(std::initializer_list<std::int64_t> counts) noexcept {
    std::int64_t agreed = count_undefined;
    bool first = true;
    for (const std::int64_t count : counts) {
        if (count == count_undefined) return count_undefined;
        if (first) {
            agreed = count;
            first = false;
        } else if (count != agreed) {
            return count_undefined;
        }
    }
    return agreed;
}

template <typename P, typename Q, typename G>
struct en_route_count;
template <typename P, typename Q>
struct en_route_count<P, Q, End> : std::integral_constant<std::int64_t, 0> {};
template <typename P, typename Q>
struct en_route_count<P, Q, Var> : std::integral_constant<std::int64_t, 0> {};
template <typename P, typename Q, typename Body>
struct en_route_count<P, Q, Rec<Body>>
    : std::integral_constant<std::int64_t,
                             (!has_free_var<Body>::value || en_route_count<P, Q, Body>::value == 0)
                                 ? en_route_count<P, Q, Body>::value
                                 : count_undefined> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
    requires(!detail::is_crashed_v<To>)
struct en_route_count<P, Q, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::integral_constant<std::int64_t, (std::is_same_v<P, From> && std::is_same_v<Q, To>)
                                               ? ((has_en_route_pair<P, Q, Cs>::value || ...) ? count_undefined : 0)
                                               : agreed_count({en_route_count<P, Q, Cs>::value...})> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct en_route_count<P, Q, Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>>
    : std::integral_constant<std::int64_t, (std::is_same_v<P, From> && std::is_same_v<Q, To>)
                                               ? ((has_en_route_pair<P, Q, Cs>::value || ...) ? count_undefined : 0)
                                               : agreed_count({en_route_count<P, Q, Cs>::value...})> {};
// The count below an en-route node is the count of its chosen branch,
// the only live one, and the node adds its own message unless it is the
// crash pseudo-message.
template <typename P, typename Q, typename From, typename To, typename Chosen, typename... Ls, typename... Pls,
          typename... Cs>
struct en_route_count<P, Q, EnRouteChoice<From, To, Chosen, Branch<Ls, Pls, Cs>...>> {
    static constexpr std::int64_t below =
        en_route_count<P, Q, typename continuation_of<Chosen, Branch<Ls, Pls, Cs>...>::type>::value;
    static constexpr std::int64_t value =
        below == count_undefined ? count_undefined
        : (std::is_same_v<P, bare_role_t<From>> && std::is_same_v<Q, To> && !is_crash_label_v<Chosen>) ? below + 1
                                                                                                       : below;
};

template <typename G, typename Senders, typename Receivers>
struct counts_defined;
template <typename G, typename... Ps, typename Receivers>
struct counts_defined<G, Roles<Ps...>, Receivers> {
    template <typename P, typename... Qs>
    static consteval bool row(Roles<Qs...>*) noexcept {
        return ((en_route_count<P, Qs, G>::value != count_undefined) && ...);
    }
    static constexpr bool value = (row<Ps>(static_cast<Receivers*>(nullptr)) && ...);
};

}  // namespace detail

// The en-route count from P to Q in G (Definition 16), or -1 where it
// is undefined.
template <typename P, typename Q, typename G>
inline constexpr std::int64_t en_route_count_v = detail::en_route_count<P, Q, G>::value;

template <typename G>
struct is_balanced_plus : std::bool_constant<[] {
    if constexpr (is_balanced_v<G>) {
        return detail::counts_defined<G, roles_t<G>, roles_t<G>>::value;
    } else {
        return false;
    }
}()> {};

template <typename G>
inline constexpr bool is_balanced_plus_v = is_balanced_plus<G>::value;

// ── Crash annotations (Definitions 4.10 and 4.15) ────────────────────
//
// A global type with crash annotations is well-annotated for a set of
// reliable roles when no reliable role carries the annotation (WA1) and
// no role carries it at one position and occurs without it at another
// (WA3).  Clause WA2 of the paper relates the annotations to the set of
// crashed roles of the transition system, which this header does not
// model.  crashed_roles_t can hold more roles than the set of crashed
// roles of Definition 4.1, so both clauses here are stricter.
//
// The question is a type, so that the predicate takes one argument and
// can hold an armed cell.

template <typename G, typename ReliableRoles>
struct CrashAnnotation {};

template <typename Q>
struct is_well_annotated : std::false_type {};
template <typename G, typename... Reliable>
struct is_well_annotated<CrashAnnotation<G, Roles<Reliable...>>> : std::bool_constant<[] {
    if constexpr (is_global_well_formed_v<G>) {
        using crashed = crashed_roles_t<G>;
        using plain = typename detail::plain_role_walk<G>::type;
        return !(role_in_v<Reliable, crashed> || ...)
               && std::is_same_v<typename detail::role_difference<crashed, plain>::type, crashed>;
    } else {
        return false;
    }
}()> {};

template <typename G, typename ReliableRoles>
inline constexpr bool is_well_annotated_v = is_well_annotated<CrashAnnotation<G, ReliableRoles>>::value;

// The result of a role removal that Definition 4.10 does not define: the
// removed role sends a transmission that has no crash branch, or the
// role has already crashed.
struct RemovalUndefined {};

namespace detail {

template <typename... Bs>
struct crash_continuation {
    using type = void;
};
template <typename L, typename P, typename C, typename... Rest>
struct crash_continuation<Branch<L, P, C>, Rest...> {
    using type = std::conditional_t<is_crash_label_v<L>, C, typename crash_continuation<Rest...>::type>;
};

template <typename G, typename R>
struct removal_walk;

template <typename R, typename... Cs>
inline constexpr bool removal_defined_v = (!std::is_same_v<typename removal_walk<Cs, R>::type, RemovalUndefined> && ...);

// The crash branch of a transmission whose sender R crashed.  The
// continuations of the other branches must have a removal too, as in the
// paper, although the result keeps only the crash branch.
template <typename R, typename... Ls, typename... Ps, typename... Cs>
consteval auto removal_crash_branch(Branch<Ls, Ps, Cs>*...) {
    using crash = typename crash_continuation<Branch<Ls, Ps, Cs>...>::type;
    if constexpr (std::is_void_v<crash> || !removal_defined_v<R, Cs...>) {
        return std::type_identity<RemovalUndefined>{};
    } else {
        return std::type_identity<typename removal_walk<crash, R>::type>{};
    }
}

template <typename R>
struct removal_walk<End, R> {
    using type = End;
};
template <typename R>
struct removal_walk<Var, R> {
    using type = Var;
};
template <typename Body, typename R>
struct removal_walk<Rec<Body>, R> {
    static consteval auto select() {
        using inner = typename removal_walk<Body, R>::type;
        if constexpr (std::is_same_v<inner, RemovalUndefined>) {
            return std::type_identity<RemovalUndefined>{};
        } else if constexpr (active_role_walk<inner>::type::size == 0) {
            return std::type_identity<End>{};
        } else {
            return std::type_identity<Rec<inner>>{};
        }
    }
    using type = typename decltype(select())::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename R>
    requires(!detail::is_crashed_v<To>)
struct removal_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>, R> {
    static consteval auto select() {
        if constexpr (std::is_same_v<From, R>) {
            // The pseudo-message keeps each branch, because the receiver
            // still holds the whole choice until it detects the crash.
            using crash = typename crash_continuation<Branch<Ls, Ps, Cs>...>::type;
            if constexpr (std::is_void_v<crash> || !removal_defined_v<R, Cs...>) {
                return std::type_identity<RemovalUndefined>{};
            } else {
                return std::type_identity<
                    EnRouteChoice<Crashed<From>, To, CrashLabel, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
            }
        } else if constexpr (!removal_defined_v<R, Cs...>) {
            return std::type_identity<RemovalUndefined>{};
        } else if constexpr (std::is_same_v<To, R>) {
            return std::type_identity<Comm<From, Crashed<To>, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        } else {
            return std::type_identity<Comm<From, To, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        }
    }
    using type = typename decltype(select())::type;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename R>
struct removal_walk<Comm<From, Crashed<To>, Branch<Ls, Ps, Cs>...>, R> {
    static consteval auto select() {
        if constexpr (std::is_same_v<From, R>) {
            // Both roles crashed: the prefix goes, and the crash branch stays.
            return removal_crash_branch<R>(static_cast<Branch<Ls, Ps, Cs>*>(nullptr)...);
        } else if constexpr (std::is_same_v<To, R> || !removal_defined_v<R, Cs...>) {
            return std::type_identity<RemovalUndefined>{};
        } else {
            return std::type_identity<Comm<From, Crashed<To>, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        }
    }
    using type = typename decltype(select())::type;
};
// A removed receiver drops the message and keeps the chosen branch.  A
// removed sender leaves the message available: the receiver still gets
// what the sender sent before it crashed.
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs, typename R>
    requires(!detail::is_crashed_v<From>)
struct removal_walk<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, Cs>...>, R> {
    static consteval auto select() {
        if constexpr (std::is_same_v<To, R>) {
            return std::type_identity<typename removal_walk<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type,
                                                            R>::type>{};
        } else if constexpr (!removal_defined_v<R, Cs...>) {
            return std::type_identity<RemovalUndefined>{};
        } else if constexpr (std::is_same_v<From, R>) {
            return std::type_identity<
                EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        } else {
            return std::type_identity<EnRouteChoice<From, To, Chosen, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        }
    }
    using type = typename decltype(select())::type;
};
template <typename From, typename To, typename Chosen, typename... Ls, typename... Ps, typename... Cs, typename R>
struct removal_walk<EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, Cs>...>, R> {
    static consteval auto select() {
        if constexpr (std::is_same_v<From, R>) {
            return std::type_identity<RemovalUndefined>{};
        } else if constexpr (std::is_same_v<To, R>) {
            return std::type_identity<typename removal_walk<typename continuation_of<Chosen, Branch<Ls, Ps, Cs>...>::type,
                                                            R>::type>{};
        } else if constexpr (!removal_defined_v<R, Cs...>) {
            return std::type_identity<RemovalUndefined>{};
        } else {
            return std::type_identity<
                EnRouteChoice<Crashed<From>, To, Chosen, Branch<Ls, Ps, typename removal_walk<Cs, R>::type>...>>{};
        }
    }
    using type = typename decltype(select())::type;
};

}  // namespace detail

// G with the live role R removed (Definition 4.10): each occurrence of R
// carries the crash annotation, a transmission that R sends becomes the
// crash pseudo-message, and a transmission between two crashed roles
// goes.  RemovalUndefined where the paper leaves the removal undefined.
// The transition that crashes R also asks that R is not reliable (rule
// [GR-crash] of Figure 7).  That check belongs to the caller.
template <typename G, typename R>
    requires is_global_type_v<G>
using remove_role_t = typename detail::removal_walk<G, R>::type;

// ── Diagnostics ──────────────────────────────────────────────────────
//
// Each gate stops at the first clause that fails, so one fault gives one
// error.  The bracketed tag is stable, and tests match on it.

template <typename G>
consteval void ensure_global_well_formed() noexcept {
    if constexpr (!is_global_type_v<G>) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Not_A_Global_Type]: the type is not built from "
                      "fixy::session::global combinators (End, Var, Rec, Comm and EnRouteChoice with Branch).  A local "
                      "type such as fixy::session::Send is not a global type.");
    } else if constexpr (global_fault_v<G> == GlobalFault::EmptyChoice) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Empty_Choice]: a Comm has no branch.  A transmission "
                      "must carry at least one Branch<Label, Payload, Cont>.");
    } else if constexpr (global_fault_v<G> == GlobalFault::SelfCommunication) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Self_Communication]: a Comm or an EnRoute has the same "
                      "role as sender and receiver.  A role cannot send a message to itself.  Make From and To "
                      "two different role types.");
    } else if constexpr (global_fault_v<G> == GlobalFault::DuplicateLabel) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Duplicate_Label]: two branches of one Comm have the same "
                      "label.  The receiver identifies the branch by its label, so the labels of one transmission "
                      "must be pairwise distinct.");
    } else if constexpr (global_fault_v<G> == GlobalFault::UnboundVariable) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Unbound_Variable]: a Var has no enclosing Rec.  Put the "
                      "loop body inside Rec<...>, or replace the Var with End.");
    } else if constexpr (global_fault_v<G> == GlobalFault::UnguardedRecursion) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Unguarded_Recursion]: a Rec body starts with Var.  A loop "
                      "must communicate before it loops back.");
    } else if constexpr (global_fault_v<G> == GlobalFault::CrashOnlyChoice) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Crash_Only_Choice]: a Comm has the crash branch as its "
                      "only branch.  No role sends the crash label, so the receiver could only wait for a crash.  "
                      "Add a message branch.");
    } else if constexpr (global_fault_v<G> == GlobalFault::CrashLabelPayload) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Crash_Label_Payload]: a branch with the label CrashLabel "
                      "carries a payload.  No role sends that branch, so no value can arrive.  Write "
                      "Branch<CrashLabel, void, Cont>.");
    } else if constexpr (global_fault_v<G> == GlobalFault::MisplacedCrashAnnotation) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Misplaced_Crash_Annotation]: Crashed<Role> occurs at a "
                      "position the crash-stop semantics does not reach, or a live role sends CrashLabel.  "
                      "Crashed marks only the receiver of a Comm or the sender of an EnRouteChoice, it does not nest, "
                      "and only a crashed sender has the crash label en route.  remove_role_t writes these nodes.");
    } else if constexpr (global_fault_v<G> == GlobalFault::UnknownChosenLabel) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Unknown_Chosen_Label]: the chosen label of an "
                      "EnRouteChoice names no branch of it.  The chosen label is the branch that the sender sent, "
                      "so it must be the label of exactly one Branch of the node.");
    }
}

template <typename G>
consteval void ensure_balanced_plus() noexcept {
    ensure_global_well_formed<G>();
    if constexpr (is_global_well_formed_v<G>) {
        if constexpr (!is_balanced_v<G>) {
            static_assert(detail::dependent_false_v<G>,
                          "fixy::session::diagnostic [Global_Not_Balanced]: a loop has a path back to its Var that "
                          "avoids a role of the loop.  That path can repeat for ever while the role waits, so the "
                          "role has no bounded depth (Pischke, Masters, Yoshida, Definitions 13 and 14).  Make each "
                          "role of the loop act on each path of each iteration, or move the role out of the loop.");
        } else if constexpr (!is_balanced_plus_v<G>) {
            static_assert(detail::dependent_false_v<G>,
                          "fixy::session::diagnostic [Global_EnRoute_Count_Undefined]: the count of en-route "
                          "messages for a pair of roles differs across the branches of a Comm, or an EnRoute sits "
                          "below a Rec that loops (Definitions 16 and 17).  The queue for that pair then has no "
                          "fixed length.");
        }
    }
}

// ── Armed cells ──────────────────────────────────────────────────────

namespace detail::witness {

struct RoleA {};
struct RoleB {};
struct RoleC {};
struct LabelX {};
struct LabelY {};
struct LabelZ {};

using Once = Msg<RoleA, RoleB, LabelX, int, End>;
using Forever = Rec<Msg<RoleA, RoleB, LabelX, int, Var>>;
// Pischke, Masters, Yoshida, equation (46), G1: RoleC can wait for ever.
using Starves = Rec<Comm<RoleA, RoleB, Branch<LabelX, int, Var>, Branch<LabelY, int, Msg<RoleA, RoleC, LabelZ, int, End>>>>;
using SentOnce = EnRoute<RoleA, RoleB, LabelX, int, End>;
using SentEachLoop = Rec<EnRoute<RoleA, RoleB, LabelX, int, Msg<RoleA, RoleB, LabelY, int, Var>>>;
using WithCrashBranch = Comm<RoleA, RoleB, Branch<LabelX, int, End>, Branch<CrashLabel, void, End>>;
using SenderCrashed = remove_role_t<WithCrashBranch, RoleA>;
using ReceiverCrashed = remove_role_t<WithCrashBranch, RoleB>;
// RoleB crashed at one position and acts at another.
using CrashedAndLive = Comm<RoleA, Crashed<RoleB>, Branch<LabelX, int, Msg<RoleB, RoleA, LabelY, int, End>>>;

}  // namespace detail::witness

}  // namespace fixy::session::global

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_global_type> {
    using accepts = witnesses<::fixy::session::global::End, ::fixy::session::global::detail::witness::Once,
                              ::fixy::session::global::detail::witness::Forever,
                              ::fixy::session::global::detail::witness::SentOnce>;
    using refuses = witnesses<int, ::fixy::session::global::Comm<int, int, int>,
                              ::fixy::session::global::Rec<int>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_global_well_formed> {
    using accepts = witnesses<::fixy::session::global::End, ::fixy::session::global::detail::witness::Once,
                              ::fixy::session::global::detail::witness::Forever,
                              ::fixy::session::global::detail::witness::WithCrashBranch,
                              ::fixy::session::global::detail::witness::SenderCrashed,
                              ::fixy::session::global::detail::witness::ReceiverCrashed>;
    using refuses = witnesses<
        int, ::fixy::session::global::Var, ::fixy::session::global::Comm<int, char>,
        ::fixy::session::global::Msg<int, int, char, char, ::fixy::session::global::End>,
        ::fixy::session::global::Rec<::fixy::session::global::Var>,
        ::fixy::session::global::Msg<int, char, ::fixy::session::global::CrashLabel, void, ::fixy::session::global::End>,
        ::fixy::session::global::Msg<::fixy::session::global::Crashed<int>, char, char, int, ::fixy::session::global::End>,
        ::fixy::session::global::EnRoute<int, char, ::fixy::session::global::CrashLabel, void, ::fixy::session::global::End>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_balanced> {
    using accepts = witnesses<::fixy::session::global::End, ::fixy::session::global::detail::witness::Forever,
                              ::fixy::session::global::detail::witness::SentEachLoop>;
    using refuses = witnesses<int, ::fixy::session::global::Var, ::fixy::session::global::detail::witness::Starves>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_balanced_plus> {
    using accepts = witnesses<::fixy::session::global::End, ::fixy::session::global::detail::witness::Forever,
                              ::fixy::session::global::detail::witness::SentOnce>;
    using refuses = witnesses<int, ::fixy::session::global::detail::witness::Starves,
                              ::fixy::session::global::detail::witness::SentEachLoop>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::global::is_well_annotated> {
    using accepts = witnesses<
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::End, ::fixy::session::global::Roles<>>,
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::detail::witness::SenderCrashed,
                                                 ::fixy::session::global::Roles<::fixy::session::global::detail::witness::RoleB>>,
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::detail::witness::ReceiverCrashed,
                                                 ::fixy::session::global::Roles<>>>;
    using refuses = witnesses<
        int,
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::detail::witness::SenderCrashed,
                                                 ::fixy::session::global::Roles<::fixy::session::global::detail::witness::RoleA>>,
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::detail::witness::CrashedAndLive,
                                                 ::fixy::session::global::Roles<>>,
        ::fixy::session::global::CrashAnnotation<::fixy::session::global::Var, ::fixy::session::global::Roles<>>>;
};
