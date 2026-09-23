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
// EnRoute<From, To, Label, Payload, Cont> is a message that From sent
// and To has not received.  It occurs only in the runtime types that a
// protocol reaches.  A static specification has no EnRoute node.
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
// en-route message sits below a recursion binder.  A static
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

#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

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

template <typename From, typename To, typename Label, typename Payload, typename Cont>
struct EnRoute {
    using from = From;
    using to = To;
    using label = Label;
    using payload = Payload;
    using next = Cont;
};

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
struct role_in;
template <typename R, typename... Rs>
struct role_in<R, Roles<Rs...>> : std::bool_constant<(std::is_same_v<R, Rs> || ...)> {};

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

}  // namespace detail

template <typename R, typename RL>
inline constexpr bool role_in_v = detail::role_in<R, RL>::value;

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
struct is_global_type<Comm<From, To, Bs...>> : std::false_type {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct is_global_type<Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(is_global_type<Cs>::value && ...)> {};
template <typename From, typename To, typename L, typename P, typename C>
struct is_global_type<EnRoute<From, To, L, P, C>> : is_global_type<C> {};

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
struct role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename L, typename P, typename C>
struct role_walk<EnRoute<From, To, L, P, C>> {
    using type = typename role_union<Roles<From, To>, typename role_walk<C>::type>::type;
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
struct active_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<Roles<From, To>, typename active_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename L, typename P, typename C>
struct active_role_walk<EnRoute<From, To, L, P, C>> {
    using type = typename role_union<Roles<To>, typename active_role_walk<C>::type>::type;
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
struct sending_role_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> {
    using type = typename role_union_all<typename sending_role_walk<Cs>::type...>::type;
};
template <typename From, typename To, typename L, typename P, typename C>
struct sending_role_walk<EnRoute<From, To, L, P, C>> {
    using type = typename role_union<Roles<From>, typename sending_role_walk<C>::type>::type;
};

// True when G holds a Var that no Rec inside G binds.
template <typename G>
struct free_var_walk;
template <>
struct free_var_walk<End> : std::false_type {};
template <>
struct free_var_walk<Var> : std::true_type {};
template <typename Body>
struct free_var_walk<Rec<Body>> : std::false_type {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct free_var_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(free_var_walk<Cs>::value || ...)> {};
template <typename From, typename To, typename L, typename P, typename C>
struct free_var_walk<EnRoute<From, To, L, P, C>> : free_var_walk<C> {};

// True when G holds an EnRoute node anywhere.
template <typename G>
struct en_route_walk;
template <>
struct en_route_walk<End> : std::false_type {};
template <>
struct en_route_walk<Var> : std::false_type {};
template <typename Body>
struct en_route_walk<Rec<Body>> : en_route_walk<Body> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct en_route_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(en_route_walk<Cs>::value || ...)> {};
template <typename From, typename To, typename L, typename P, typename C>
struct en_route_walk<EnRoute<From, To, L, P, C>> : std::true_type {};

}  // namespace detail

template <typename G>
using roles_t = typename detail::role_walk<G>::type;

template <typename G>
using active_roles_t = typename detail::active_role_walk<G>::type;

template <typename G>
using sending_roles_t = typename detail::sending_role_walk<G>::type;

template <typename G>
inline constexpr bool holds_free_var_v = detail::free_var_walk<G>::value;

template <typename G>
inline constexpr bool holds_en_route_v = detail::en_route_walk<G>::value;

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
struct rec_guard : std::true_type {};
template <>
struct rec_guard<Var> : std::false_type {};
template <typename Inner>
struct rec_guard<Rec<Inner>> : rec_guard<Inner> {};

consteval GlobalFault first_fault(std::initializer_list<GlobalFault> faults) noexcept {
    for (const GlobalFault fault : faults) {
        if (fault != GlobalFault::None) return fault;
    }
    return GlobalFault::None;
}

template <typename G, bool Bound>
struct fault_walk;
template <bool Bound>
struct fault_walk<End, Bound> : std::integral_constant<GlobalFault, GlobalFault::None> {};
template <bool Bound>
struct fault_walk<Var, Bound>
    : std::integral_constant<GlobalFault, Bound ? GlobalFault::None : GlobalFault::UnboundVariable> {};
template <typename Body, bool Bound>
struct fault_walk<Rec<Body>, Bound>
    : std::integral_constant<GlobalFault, rec_guard<Body>::value ? fault_walk<Body, true>::value
                                                                 : GlobalFault::UnguardedRecursion> {};
template <typename From, typename To, bool Bound>
struct fault_walk<Comm<From, To>, Bound> : std::integral_constant<GlobalFault, GlobalFault::EmptyChoice> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, bool Bound>
struct fault_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>, Bound>
    : std::integral_constant<GlobalFault, std::is_same_v<From, To> ? GlobalFault::SelfCommunication
                                          : !label_set_distinct_v<Ls...>
                                              ? GlobalFault::DuplicateLabel
                                              : first_fault({fault_walk<Cs, Bound>::value...})> {};
template <typename From, typename To, typename L, typename P, typename C, bool Bound>
struct fault_walk<EnRoute<From, To, L, P, C>, Bound>
    : std::integral_constant<GlobalFault,
                             std::is_same_v<From, To> ? GlobalFault::SelfCommunication : fault_walk<C, Bound>::value> {};

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
struct path_meets;
template <typename R>
struct path_meets<R, End> : std::true_type {};
template <typename R>
struct path_meets<R, Var> : std::false_type {};
template <typename R, typename Body>
struct path_meets<R, Rec<Body>> : std::true_type {};
template <typename R, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct path_meets<R, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<std::is_same_v<R, From> || std::is_same_v<R, To> || (path_meets<R, Cs>::value && ...)> {};
template <typename R, typename From, typename To, typename L, typename P, typename C>
struct path_meets<R, EnRoute<From, To, L, P, C>> : std::bool_constant<std::is_same_v<R, To> || path_meets<R, C>::value> {};

template <typename Body, typename RL>
struct loop_meets_each;
template <typename Body, typename... Rs>
struct loop_meets_each<Body, Roles<Rs...>> : std::bool_constant<(path_meets<Rs, Body>::value && ...)> {};

// True when each Rec in G passes loop_meets_each for each role in its
// body.  Complexity: O(|G| * |roles|) walks of O(|G|) each.
template <typename G>
struct balance_walk;
template <>
struct balance_walk<End> : std::true_type {};
template <>
struct balance_walk<Var> : std::true_type {};
template <typename Body>
struct balance_walk<Rec<Body>>
    : std::bool_constant<loop_meets_each<Body, typename role_walk<Body>::type>::value && balance_walk<Body>::value> {};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct balance_walk<Comm<From, To, Branch<Ls, Ps, Cs>...>> : std::bool_constant<(balance_walk<Cs>::value && ...)> {};
template <typename From, typename To, typename L, typename P, typename C>
struct balance_walk<EnRoute<From, To, L, P, C>> : balance_walk<C> {};

}  // namespace detail

template <typename G>
struct is_balanced : std::bool_constant<[] {
    if constexpr (is_global_well_formed_v<G>) {
        return detail::balance_walk<G>::value;
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
struct en_route_pair_walk;
template <typename P, typename Q>
struct en_route_pair_walk<P, Q, End> : std::false_type {};
template <typename P, typename Q>
struct en_route_pair_walk<P, Q, Var> : std::false_type {};
template <typename P, typename Q, typename Body>
struct en_route_pair_walk<P, Q, Rec<Body>> : en_route_pair_walk<P, Q, Body> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct en_route_pair_walk<P, Q, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::bool_constant<(en_route_pair_walk<P, Q, Cs>::value || ...)> {};
template <typename P, typename Q, typename From, typename To, typename L, typename Pl, typename C>
struct en_route_pair_walk<P, Q, EnRoute<From, To, L, Pl, C>>
    : std::bool_constant<(std::is_same_v<P, From> && std::is_same_v<Q, To>) || en_route_pair_walk<P, Q, C>::value> {};

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
                             (!free_var_walk<Body>::value || en_route_count<P, Q, Body>::value == 0)
                                 ? en_route_count<P, Q, Body>::value
                                 : count_undefined> {};
template <typename P, typename Q, typename From, typename To, typename... Ls, typename... Ps, typename... Cs>
struct en_route_count<P, Q, Comm<From, To, Branch<Ls, Ps, Cs>...>>
    : std::integral_constant<std::int64_t, (std::is_same_v<P, From> && std::is_same_v<Q, To>)
                                               ? ((en_route_pair_walk<P, Q, Cs>::value || ...) ? count_undefined : 0)
                                               : agreed_count({en_route_count<P, Q, Cs>::value...})> {};
template <typename P, typename Q, typename From, typename To, typename L, typename Pl, typename C>
struct en_route_count<P, Q, EnRoute<From, To, L, Pl, C>>
    : std::integral_constant<std::int64_t,
                             en_route_count<P, Q, C>::value == count_undefined ? count_undefined
                             : (std::is_same_v<P, From> && std::is_same_v<Q, To>)
                                 ? en_route_count<P, Q, C>::value + 1
                                 : en_route_count<P, Q, C>::value> {};

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

// ── Diagnostics ──────────────────────────────────────────────────────
//
// Each gate stops at the first clause that fails, so one fault gives one
// error.  The bracketed tag is stable, and tests match on it.

template <typename G>
consteval void ensure_global_well_formed() noexcept {
    if constexpr (!is_global_type_v<G>) {
        static_assert(detail::dependent_false_v<G>,
                      "fixy::session::diagnostic [Global_Not_A_Global_Type]: the type is not built from "
                      "fixy::session::global combinators (End, Var, Rec, Comm with Branch, EnRoute).  A local "
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
                              ::fixy::session::global::detail::witness::Forever>;
    using refuses = witnesses<int, ::fixy::session::global::Var, ::fixy::session::global::Comm<int, char>,
                              ::fixy::session::global::Msg<int, int, char, char, ::fixy::session::global::End>,
                              ::fixy::session::global::Rec<::fixy::session::global::Var>>;
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
