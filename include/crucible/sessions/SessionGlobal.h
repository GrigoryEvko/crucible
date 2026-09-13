#pragma once

// A global type describes a multiparty protocol as one object naming every
// participant and every communication event.  Projection extracts one
// participant's local view, which is the ordinary session type that
// participant executes.  A property proved of the global type holds for every
// well-typed implementation of the projected local types, so no pair of
// participants has to be reasoned about on its own.
//
// Branches are positional, not labeled.  Classical multiparty session types
// name each branch with a message label.  Here the branch's index in the pack
// is its label, which matches the positional Select and Offer that a choice
// projects onto.  Reordering branches is not a subtype relation here.  It is a
// refactor of the global choice and of every projected local type together.
//
// Merging a choice onto a third-party role is plain: every branch projection
// must be equal.  Full merging, which admits branches that diverge as long as
// the third party can tell them apart later, needs coinductive fixed-point
// machinery and is not implemented.  Plain merging suffices for binary
// protocols, which have no third party, and for protocols where every role
// takes part in every choice.  A protocol where a role sits out some choice
// and then sees different continuations needs full merging.
//
// Projection dispatches on a compile-time case enum with one partial
// specialization per case, rather than on std::conditional_t.  A conditional
// alias is evaluated eagerly and instantiates all three arms, so projecting a
// choice onto its own sender would still instantiate the third-party arm and
// fail the plain merge over branches that only the sender distinguishes.

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionCrash.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

namespace detail::global {

template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::global

struct End_G {};

template <typename From, typename To, typename Payload, typename G>
struct Transmission {
    using from = From;
    using to = To;
    using payload = Payload;
    using next = G;
};

template <typename Payload, typename G>
struct BranchG {
    using payload = Payload;
    using next = G;
};

template <typename From, typename To, typename... Branches>
struct Choice {
    using from = From;
    using to = To;
    static constexpr std::size_t branch_count = sizeof...(Branches);
};

// Var_G binds to the nearest enclosing Rec_G.  A nested Rec_G shadows the
// outer one.
template <typename Body>
struct Rec_G {
    using body = Body;
};

struct Var_G {};

// StopG marks the point where Peer has crashed.  The crash class rides along
// so that a protocol author can tell a best-effort failure apart from a strict
// one, and every projection receives the class the author declared instead of
// one collapsed terminal.
template <typename Peer, CrashClass C = CrashClass::Abort>
struct StopG {
    using peer = Peer;
    static constexpr CrashClass crash_class = C;
};

template <typename G>
struct is_end_g : std::false_type {};
template <>
struct is_end_g<End_G> : std::true_type {};

template <typename G>
struct is_transmission : std::false_type {};
template <typename F, typename T, typename P, typename N>
struct is_transmission<Transmission<F, T, P, N>> : std::true_type {};

template <typename G>
struct is_choice : std::false_type {};
template <typename F, typename T, typename... Bs>
struct is_choice<Choice<F, T, Bs...>> : std::true_type {};

template <typename G>
struct is_rec_g : std::false_type {};
template <typename B>
struct is_rec_g<Rec_G<B>> : std::true_type {};

template <typename G>
struct is_var_g : std::false_type {};
template <>
struct is_var_g<Var_G> : std::true_type {};

template <typename G>
struct is_stop_g : std::false_type {};
template <typename P, CrashClass C>
struct is_stop_g<StopG<P, C>> : std::true_type {};

template <typename G>
inline constexpr bool is_end_g_v = is_end_g<G>::value;
template <typename G>
inline constexpr bool is_transmission_v = is_transmission<G>::value;
template <typename G>
inline constexpr bool is_choice_v = is_choice<G>::value;
template <typename G>
inline constexpr bool is_rec_g_v = is_rec_g<G>::value;
template <typename G>
inline constexpr bool is_var_g_v = is_var_g<G>::value;
template <typename G>
inline constexpr bool is_stop_g_v = is_stop_g<G>::value;

template <typename... Rs>
struct RoleList {
    static constexpr std::size_t size = sizeof...(Rs);
};

using EmptyRoleList = RoleList<>;

namespace detail::global {

template <typename R, typename RL>
struct contains_role;

template <typename R>
struct contains_role<R, RoleList<>> : std::false_type {};

template <typename R, typename Head, typename... Rest>
struct contains_role<R, RoleList<Head, Rest...>>
    : std::bool_constant<std::is_same_v<R, Head> || contains_role<R, RoleList<Rest...>>::value> {};

template <typename R, typename RL>
inline constexpr bool contains_role_v = contains_role<R, RL>::value;

template <typename R, typename RL>
struct insert_unique;

template <typename R, typename... Rs>
struct insert_unique<R, RoleList<Rs...>> {
    using type = std::conditional_t<contains_role_v<R, RoleList<Rs...>>, RoleList<Rs...>, RoleList<R, Rs...>>;
};

}  // namespace detail::global

template <typename R, typename RL>
using insert_unique_t = typename detail::global::insert_unique<R, RL>::type;

namespace detail::global {

template <typename RL1, typename RL2>
struct union_roles;

template <typename... Rs1>
struct union_roles<RoleList<Rs1...>, RoleList<>> {
    using type = RoleList<Rs1...>;
};

template <typename... Rs1, typename Head, typename... Rest>
struct union_roles<RoleList<Rs1...>, RoleList<Head, Rest...>>
    : union_roles<insert_unique_t<Head, RoleList<Rs1...>>, RoleList<Rest...>> {};

}  // namespace detail::global

template <typename RL1, typename RL2>
using union_roles_t = typename detail::global::union_roles<RL1, RL2>::type;

// RolesOf collects every role instance, duplicates included, in one walk of
// G, then deduplicates with a single consteval sort over type hashes.  A chain
// of unique-inserts instead would pay an O(d) linear scan per insert, for
// O(N^2) template instantiations overall.
//
// The deduped list is ordered by first occurrence in the input, not by hash.

namespace detail::global {

template <typename G>
struct flat_roles;

template <>
struct flat_roles<End_G> {
    using type = RoleList<>;
};

template <>
struct flat_roles<Var_G> {
    using type = RoleList<>;
};

template <typename Peer, CrashClass C>
struct flat_roles<StopG<Peer, C>> {
    using type = RoleList<Peer>;
};

template <typename Body>
struct flat_roles<Rec_G<Body>> {
    using type = typename flat_roles<Body>::type;
};

template <typename... RLs>
struct concat_role_lists;

template <>
struct concat_role_lists<> {
    using type = RoleList<>;
};

template <typename... Rs>
struct concat_role_lists<RoleList<Rs...>> {
    using type = RoleList<Rs...>;
};

template <typename... A, typename... B, typename... Rest>
struct concat_role_lists<RoleList<A...>, RoleList<B...>, Rest...> : concat_role_lists<RoleList<A..., B...>, Rest...> {};

template <typename... RLs>
using concat_role_lists_t = typename concat_role_lists<RLs...>::type;

template <typename From, typename To, typename P, typename G>
struct flat_roles<Transmission<From, To, P, G>> {
    using type = concat_role_lists_t<RoleList<From, To>, typename flat_roles<G>::type>;
};

template <typename From, typename To, typename... Bs>
struct flat_roles<Choice<From, To, Bs...>> {
    using type = concat_role_lists_t<RoleList<From, To>, typename flat_roles<typename Bs::next>::type...>;
};

template <typename G>
using flat_roles_t = typename flat_roles<G>::type;

template <typename... Rs>
[[nodiscard]] inline consteval std::size_t compute_dedup_count() noexcept {
    constexpr std::size_t N = sizeof...(Rs);
    if constexpr (N == 0)
        return 0;
    else {
        std::array<std::uint64_t, N> hashes{detail::ctx::type_id_hash_v<Rs>...};
        std::ranges::sort(hashes);
        std::size_t unique = 1;
        for (std::size_t j = 1; j < N; ++j) {
            if (hashes[j] != hashes[j - 1]) ++unique;
        }
        return unique;
    }
}

template <typename... Rs>
[[nodiscard]] inline consteval auto compute_kept_indices() noexcept {
    constexpr std::size_t N = sizeof...(Rs);
    std::array<std::size_t, N == 0 ? 1 : N> kept{};
    if constexpr (N == 0)
        return kept;  // unused
    else {
        std::array<std::pair<std::uint64_t, std::size_t>, N> tagged{};
        for (std::size_t i = 0; i < N; ++i) {
            tagged[i] = std::pair{std::uint64_t{0}, i};
        }
        const std::array<std::uint64_t, N> hashes{detail::ctx::type_id_hash_v<Rs>...};
        for (std::size_t i = 0; i < N; ++i)
            tagged[i].first = hashes[i];

        // The secondary key puts the earliest occurrence first within each
        // group of equal hashes, which is what makes the dedup below keep
        // first occurrences rather than an arbitrary member of the group.
        std::ranges::sort(tagged, [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        std::size_t k = 0;
        for (std::size_t j = 0; j < N; ++j) {
            if (j == 0 || tagged[j].first != tagged[j - 1].first) {
                kept[k++] = tagged[j].second;
            }
        }

        // Re-sort by original position so the output preserves
        // first-occurrence order rather than hash order.
        std::sort(kept.begin(), kept.begin() + k);
        return kept;
    }
}

template <typename First, typename... Rest, std::size_t... Is>
auto build_dedup_role_list_helper(std::index_sequence<Is...>) {
    static constexpr auto kept = compute_kept_indices<First, Rest...>();
    using Combined = std::tuple<First, Rest...>;
    return RoleList<std::tuple_element_t<kept[Is], Combined>...>{};
}

template <typename RL>
struct dedup_role_list;

// Short-circuits the empty case so the helper above never pack-indexes an
// empty pack.
template <>
struct dedup_role_list<RoleList<>> {
    using type = RoleList<>;
};

template <typename First, typename... Rest>
struct dedup_role_list<RoleList<First, Rest...>> {
    static constexpr std::size_t kept_n = compute_dedup_count<First, Rest...>();
    using type = decltype(build_dedup_role_list_helper<First, Rest...>(std::make_index_sequence<kept_n>{}));
};

template <typename RL>
using dedup_role_list_t = typename dedup_role_list<RL>::type;

}  // namespace detail::global

template <typename G>
struct RolesOf {
    using type = detail::global::dedup_role_list_t<detail::global::flat_roles_t<G>>;
};

template <typename G>
using roles_of_t = typename RolesOf<G>::type;

// Well-formedness carries two clauses.  Every Var_G needs an enclosing Rec_G,
// and no Transmission or Choice may have From equal to To.  A self-directed
// event projects to a local type that puts the same role at both the Send and
// the Recv position, which deadlocks if it is ever reached.

template <typename G, typename RecCtx = void>
struct is_global_well_formed;

template <typename RecCtx>
struct is_global_well_formed<End_G, RecCtx> : std::true_type {};

template <typename RecCtx>
struct is_global_well_formed<Var_G, RecCtx> : std::bool_constant<!std::is_void_v<RecCtx>> {};

template <typename From, typename To, typename P, typename G, typename RecCtx>
struct is_global_well_formed<Transmission<From, To, P, G>, RecCtx>
    : std::bool_constant<!std::is_same_v<From, To> && is_global_well_formed<G, RecCtx>::value> {};

// The variadic specialization below folds branch well-formedness with
// (... && ...), which is vacuously true on an empty pack.  Without this
// zero-branch specialization a choice with no branches would pass the gate,
// then fail far away inside projection where plain_merge_t<> is ill-formed at
// the leaf.  A choice with no branch to select has no meaning, so reject it
// here where the diagnostic can say so.
template <typename From, typename To, typename RecCtx>
struct is_global_well_formed<Choice<From, To>, RecCtx> : std::false_type {};

template <typename From, typename To, typename... Bs, typename RecCtx>
struct is_global_well_formed<Choice<From, To, Bs...>, RecCtx>
    : std::bool_constant<!std::is_same_v<From, To>
                         && (is_global_well_formed<typename Bs::next, RecCtx>::value && ...)> {};

template <typename Body, typename RecCtx>
struct is_global_well_formed<Rec_G<Body>, RecCtx> : is_global_well_formed<Body, Rec_G<Body>> {};

template <typename Peer, CrashClass C, typename RecCtx>
struct is_global_well_formed<StopG<Peer, C>, RecCtx> : std::true_type {};

template <typename G>
inline constexpr bool is_global_well_formed_v = is_global_well_formed<G>::value;

// Well-formedness also fails for a free Var_G, so its negation cannot say
// which fault occurred.  This trait pinpoints the self-directed event so the
// helper below can route a specific message.

template <typename G>
struct has_self_loop : std::false_type {};

template <typename From, typename To, typename P, typename G>
struct has_self_loop<Transmission<From, To, P, G>>
    : std::bool_constant<std::is_same_v<From, To> || has_self_loop<G>::value> {};

template <typename From, typename To, typename... Bs>
struct has_self_loop<Choice<From, To, Bs...>>
    : std::bool_constant<std::is_same_v<From, To> || (has_self_loop<typename Bs::next>::value || ...)> {};

template <typename Body>
struct has_self_loop<Rec_G<Body>> : has_self_loop<Body> {};

template <typename G>
inline constexpr bool has_self_loop_v = has_self_loop<G>::value;

// Call this at a protocol declaration site.  The bracketed tag in the message
// is stable across compiler versions, so tests can match on it.

template <typename G>
consteval void assert_no_self_loop() noexcept {
    static_assert(!has_self_loop_v<G>, "crucible::session::diagnostic [ProtocolViolation_Self_Loop]: "
                                       "global type contains a Transmission<X, X, ...> or "
                                       "Choice<X, X, ...> — a participant cannot send to itself in "
                                       "MPST.  Check that From and To in your Transmission / Choice "
                                       "are different role tags.  Common cause: copy-paste error "
                                       "where both sides reference the same role tag.  If you "
                                       "genuinely want a participant's local-only state transition, "
                                       "model it as a Machine<State> transition outside the global "
                                       "protocol rather than as a self-Transmission.");
}

// Well-formedness also fails for a free Var_G or a self-directed event, so
// this trait pinpoints the empty choice for a routed message.

template <typename G>
struct has_empty_choice : std::false_type {};

template <typename From, typename To, typename P, typename G>
struct has_empty_choice<Transmission<From, To, P, G>> : has_empty_choice<G> {};

// This matches Choice<From, To> more specifically than the variadic case
// below, because no pack at all beats a pack that happens to be empty.
template <typename From, typename To>
struct has_empty_choice<Choice<From, To>> : std::true_type {};

template <typename From, typename To, typename... Bs>
struct has_empty_choice<Choice<From, To, Bs...>>
    : std::bool_constant<(has_empty_choice<typename Bs::next>::value || ...)> {};

template <typename Body>
struct has_empty_choice<Rec_G<Body>> : has_empty_choice<Body> {};

template <typename G>
inline constexpr bool has_empty_choice_v = has_empty_choice<G>::value;

// Call this at a protocol declaration site.  The bracketed tag in the message
// is stable across compiler versions, so tests can match on it.

template <typename G>
consteval void assert_no_empty_choice() noexcept {
    static_assert(!has_empty_choice_v<G>, "crucible::session::diagnostic [Choice_Empty_Branches]: "
                                          "global type contains a Choice<From, To> with zero branches.  "
                                          "An empty-branch Choice has no semantic meaning in MPST — "
                                          "there's no selectable label to drive the protocol forward, "
                                          "and projection would collapse to plain_merge_t<> which is "
                                          "ill-formed at the leaf.  Add at least one BranchG<Payload, "
                                          "Continuation> to the Choice's branch pack, or replace the "
                                          "Choice with End_G / StopG<Peer> / Transmission<From, To, P, "
                                          "End_G> depending on the intended semantics.  Common cause: "
                                          "scaffolding a Choice before its branches were filled in and "
                                          "leaving the placeholder.");
}

namespace detail::global {

template <typename... Ts>
struct plain_merge_impl;

template <>
struct plain_merge_impl<> {
    // A well-formed Choice always has at least one branch, so this case is
    // unreachable from projection.  End stands in for it rather than being
    // ill-formed, which keeps the diagnostic on the real fault.
    using type = End;
};

template <typename T>
struct plain_merge_impl<T> {
    using type = T;
};

template <typename T, typename... Rest>
struct plain_merge_impl<T, Rest...> {
    static_assert((std::is_same_v<T, Rest> && ...), "crucible::session::diagnostic [Merge_Branches_Diverge]: "
                                                    "plain_merge_t: branch projections differ.  The third-party "
                                                    "role sees structurally-different local types across Choice "
                                                    "branches, which plain merging cannot unify.  Coinductive "
                                                    "full merging is required, and is not implemented.  "
                                                    "Workaround: project to a role that IS involved in every "
                                                    "Choice (From or To), "
                                                    "or restructure the global type so third-party projections "
                                                    "match across all branches.");
    using type = T;
};

}  // namespace detail::global

template <typename... Ts>
using plain_merge_t = typename detail::global::plain_merge_impl<Ts...>::type;

namespace detail::global {

// The symmetric-pair check is factored out because a typo in one expansion of
// it would break crash detection for one direction of a pair without ever
// failing to compile.  The walker would simply return the wrong answer.
template <typename F, typename T, typename A, typename B>
inline constexpr bool same_unordered_pair_v =
    (std::is_same_v<F, A> && std::is_same_v<T, B>) || (std::is_same_v<F, B> && std::is_same_v<T, A>);

template <typename G, typename RoleA, typename RoleB>
struct has_interaction_between : std::false_type {};

template <typename A, typename B>
struct has_interaction_between<End_G, A, B> : std::false_type {};

template <typename A, typename B>
struct has_interaction_between<Var_G, A, B> : std::false_type {};

template <typename Peer, CrashClass C, typename A, typename B>
struct has_interaction_between<StopG<Peer, C>, A, B> : std::false_type {};

template <typename From, typename To, typename P, typename N, typename A, typename B>
struct has_interaction_between<Transmission<From, To, P, N>, A, B>
    : std::bool_constant<same_unordered_pair_v<From, To, A, B> || has_interaction_between<N, A, B>::value> {};

template <typename From, typename To, typename... Bs, typename A, typename B>
struct has_interaction_between<Choice<From, To, Bs...>, A, B>
    : std::bool_constant<same_unordered_pair_v<From, To, A, B>
                         || (has_interaction_between<typename Bs::next, A, B>::value || ...)> {};

// The walk terminates because Var_G is a leaf.  The type tree is finite even
// though the protocol it denotes runs unbounded.
template <typename Body, typename A, typename B>
struct has_interaction_between<Rec_G<Body>, A, B> : has_interaction_between<Body, A, B> {};

template <typename G, typename A, typename B>
inline constexpr bool has_interaction_between_v = has_interaction_between<G, A, B>::value;

}  // namespace detail::global

// The walker lives in detail for namespace hygiene, but the predicate itself
// is public shape: a caller acting on the StopG projection rule needs a path
// to it that does not reach past the detail boundary.

template <typename G, typename A, typename B>
inline constexpr bool has_interaction_between_v = detail::global::has_interaction_between_v<G, A, B>;

// Every rule threads RootG, the outermost global type, unchanged.  Only the
// StopG rule for a non-peer role reads it.  Deciding whether that role is
// affected by the crash means asking whether it interacts with the peer
// anywhere in the whole protocol, which the subtree rooted at the StopG node
// cannot answer.

namespace detail::global {

template <typename G, typename Role, typename RootG>
struct ProjectImpl;

template <typename Role, typename RootG>
struct ProjectImpl<End_G, Role, RootG> {
    using type = End;
};

template <typename Role, typename RootG>
struct ProjectImpl<Var_G, Role, RootG> {
    using type = Continue;
};

template <typename Body, typename Role, typename RootG>
struct ProjectImpl<Rec_G<Body>, Role, RootG> {
    using type = Loop<typename ProjectImpl<Body, Role, RootG>::type>;
};

template <typename Peer, CrashClass C, typename RootG>
struct ProjectImpl<StopG<Peer, C>, Peer, RootG> {
    using type = Stop_g<C>;
};

// A surviving role whose protocol intersects the crashed peer's sees
// crash-induced termination, not clean termination.  The crash class travels
// with it, so an implementation can tell the failure tiers apart instead of
// seeing one collapsed terminal.  A role that never interacts with the peer is
// unaffected and finishes at End, which carries no crash class.
//
// Projecting to Stop_g<C> rather than End is a tightening.  Stop_g<C> sits at
// the bottom of the subtype order, so code that handled End still handles it,
// while code that dispatches crash recovery now gets a typed signal.
template <typename Peer, CrashClass C, typename Role, typename RootG>
struct ProjectImpl<StopG<Peer, C>, Role, RootG> {
    using type = std::conditional_t<has_interaction_between_v<RootG, Role, Peer>, Stop_g<C>, End>;
};

enum class proj_case {
    sender,
    receiver,
    third_party
};

template <typename Role, typename From, typename To>
inline constexpr proj_case project_case_v = std::is_same_v<Role, From> ? proj_case::sender
                                          : std::is_same_v<Role, To>   ? proj_case::receiver
                                                                       : proj_case::third_party;

template <proj_case C, typename From, typename To, typename Role, typename P, typename G, typename RootG>
struct project_transmission_helper;

template <typename From, typename To, typename Role, typename P, typename G, typename RootG>
struct project_transmission_helper<proj_case::sender, From, To, Role, P, G, RootG> {
    using type = Send<P, typename ProjectImpl<G, Role, RootG>::type>;
};

template <typename From, typename To, typename Role, typename P, typename G, typename RootG>
struct project_transmission_helper<proj_case::receiver, From, To, Role, P, G, RootG> {
    using type = Recv<P, typename ProjectImpl<G, Role, RootG>::type>;
};

template <typename From, typename To, typename Role, typename P, typename G, typename RootG>
struct project_transmission_helper<proj_case::third_party, From, To, Role, P, G, RootG> {
    using type = typename ProjectImpl<G, Role, RootG>::type;
};

template <typename From, typename To, typename P, typename G, typename Role, typename RootG>
struct ProjectImpl<Transmission<From, To, P, G>, Role, RootG> {
    using type =
        typename project_transmission_helper<project_case_v<Role, From, To>, From, To, Role, P, G, RootG>::type;
};

template <proj_case C, typename From, typename To, typename Role, typename RootG, typename... Bs>
struct project_choice_helper;

template <typename From, typename To, typename Role, typename RootG, typename... Bs>
struct project_choice_helper<proj_case::sender, From, To, Role, RootG, Bs...> {
    using type = Select<Send<typename Bs::payload, typename ProjectImpl<typename Bs::next, Role, RootG>::type>...>;
};

template <typename From, typename To, typename Role, typename RootG, typename... Bs>
struct project_choice_helper<proj_case::receiver, From, To, Role, RootG, Bs...> {
    using type = Offer<Recv<typename Bs::payload, typename ProjectImpl<typename Bs::next, Role, RootG>::type>...>;
};

template <typename From, typename To, typename Role, typename RootG, typename... Bs>
struct project_choice_helper<proj_case::third_party, From, To, Role, RootG, Bs...> {
    using type = plain_merge_t<typename ProjectImpl<typename Bs::next, Role, RootG>::type...>;
};

template <typename From, typename To, typename... Bs, typename Role, typename RootG>
struct ProjectImpl<Choice<From, To, Bs...>, Role, RootG> {
    using type = typename project_choice_helper<project_case_v<Role, From, To>, From, To, Role, RootG, Bs...>::type;
};

}  // namespace detail::global

// A caller projecting a sub-tree of a larger protocol can specialize Project
// to supply a different RootG.

template <typename G, typename Role>
struct Project {
    using type = typename detail::global::ProjectImpl<G, Role, G>::type;
};

template <typename G, typename Role>
using project_t = typename Project<G, Role>::type;

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::global::global_self_test {

struct Alice {};
struct Bob {};
struct Carol {};

struct Ping {};
struct Pong {};
struct Ack {};
struct Query {};
struct Reply {};

static_assert(is_end_g_v<End_G>);
static_assert(!is_end_g_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(is_transmission_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(!is_transmission_v<End_G>);
static_assert(is_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);
static_assert(is_rec_g_v<Rec_G<End_G>>);
static_assert(is_var_g_v<Var_G>);
static_assert(is_stop_g_v<StopG<Alice>>);

using RL_AB = RoleList<Alice, Bob>;
using RL_BC = RoleList<Bob, Carol>;
using RL_ABC = RoleList<Alice, Bob, Carol>;

static_assert(EmptyRoleList::size == 0);
static_assert(RL_AB::size == 2);
static_assert(RL_ABC::size == 3);

static_assert(contains_role_v<Alice, RL_AB>);
static_assert(contains_role_v<Bob, RL_AB>);
static_assert(!contains_role_v<Carol, RL_AB>);

static_assert(std::is_same_v<insert_unique_t<Carol, RL_AB>, RoleList<Carol, Alice, Bob>>);
static_assert(std::is_same_v<insert_unique_t<Alice, RL_AB>, RL_AB>);

using Merged = union_roles_t<RL_AB, RL_BC>;
static_assert(Merged::size == 3);
static_assert(contains_role_v<Alice, Merged>);
static_assert(contains_role_v<Bob, Merged>);
static_assert(contains_role_v<Carol, Merged>);

static_assert(std::is_same_v<union_roles_t<RL_AB, EmptyRoleList>, RL_AB>);

using G_binary = Transmission<Alice, Bob, Ping, End_G>;
using G_ternary = Transmission<Alice, Bob, Ping, Transmission<Bob, Carol, Pong, End_G>>;

static_assert(roles_of_t<End_G>::size == 0);

static_assert(contains_role_v<Alice, roles_of_t<G_binary>>);
static_assert(contains_role_v<Bob, roles_of_t<G_binary>>);
static_assert(!contains_role_v<Carol, roles_of_t<G_binary>>);
static_assert(roles_of_t<G_binary>::size == 2);

static_assert(contains_role_v<Alice, roles_of_t<G_ternary>>);
static_assert(contains_role_v<Bob, roles_of_t<G_ternary>>);
static_assert(contains_role_v<Carol, roles_of_t<G_ternary>>);
static_assert(roles_of_t<G_ternary>::size == 3);

static_assert(contains_role_v<Alice, roles_of_t<StopG<Alice>>>);
static_assert(!contains_role_v<Bob, roles_of_t<StopG<Alice>>>);

static_assert(is_global_well_formed_v<End_G>);
static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(is_global_well_formed_v<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>);
static_assert(is_global_well_formed_v<Rec_G<Choice<Alice, Bob, BranchG<Ping, Var_G>, BranchG<Ack, End_G>>>>);

static_assert(!is_global_well_formed_v<Var_G>);
static_assert(!is_global_well_formed_v<Transmission<Alice, Bob, Ping, Var_G>>);
static_assert(!is_global_well_formed_v<Choice<Alice, Bob, BranchG<Ping, Var_G>>>);

static_assert(is_global_well_formed_v<Rec_G<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>>);

static_assert(is_global_well_formed_v<StopG<Alice>>);
static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, StopG<Alice>>>);

static_assert(!is_global_well_formed_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert(!is_global_well_formed_v<Transmission<Bob, Bob, Ack, End_G>>);

static_assert(!is_global_well_formed_v<Choice<Alice, Alice, BranchG<Ping, End_G>>>);
static_assert(!is_global_well_formed_v<Choice<Bob, Bob, BranchG<Ping, End_G>, BranchG<Ack, End_G>>>);

static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(is_global_well_formed_v<Transmission<Bob, Alice, Ack, End_G>>);

// A well-formed prefix does not redeem a self-directed event nested below it.
static_assert(!is_global_well_formed_v<Transmission<Alice, Bob, Ping, Transmission<Alice, Alice, Ack, End_G>>>);

static_assert(!is_global_well_formed_v<
              Choice<Alice, Bob, BranchG<Ping, End_G>, BranchG<Ack, Transmission<Bob, Bob, Ping, End_G>>>>);

static_assert(is_global_well_formed_v<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>);
static_assert(!is_global_well_formed_v<Rec_G<Transmission<Alice, Alice, Ping, Var_G>>>);

static_assert(!has_self_loop_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(has_self_loop_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert(has_self_loop_v<Choice<Alice, Alice, BranchG<Ping, End_G>>>);

static_assert(has_self_loop_v<Transmission<Alice, Bob, Ping, Transmission<Alice, Alice, Ack, End_G>>>);
static_assert(
    has_self_loop_v<Choice<Alice, Bob, BranchG<Ping, End_G>, BranchG<Ack, Transmission<Bob, Bob, Ping, End_G>>>>);

static_assert(!has_self_loop_v<End_G>);
static_assert(!has_self_loop_v<Var_G>);
static_assert(!has_self_loop_v<StopG<Alice>>);

consteval bool check_assert_no_self_loop_compiles() {
    assert_no_self_loop<End_G>();
    assert_no_self_loop<Transmission<Alice, Bob, Ping, End_G>>();
    assert_no_self_loop<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>();
    return true;
}
static_assert(check_assert_no_self_loop_compiles());

static_assert(!is_global_well_formed_v<Choice<Alice, Bob>>);
static_assert(!is_global_well_formed_v<Choice<Bob, Carol>>);

static_assert(is_global_well_formed_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);

static_assert(is_global_well_formed_v<Choice<Alice, Bob, BranchG<Ping, End_G>, BranchG<Ack, End_G>>>);

static_assert(!is_global_well_formed_v<Rec_G<Choice<Alice, Bob>>>);

static_assert(!is_global_well_formed_v<Transmission<Alice, Bob, Ping, Choice<Bob, Carol>>>);

static_assert(!is_global_well_formed_v<Choice<Alice, Bob, BranchG<Ping, Choice<Bob, Carol>>>>);

static_assert(!has_empty_choice_v<End_G>);
static_assert(!has_empty_choice_v<Var_G>);
static_assert(!has_empty_choice_v<StopG<Alice>>);
static_assert(!has_empty_choice_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(!has_empty_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);
static_assert(has_empty_choice_v<Choice<Alice, Bob>>);
static_assert(has_empty_choice_v<Rec_G<Choice<Alice, Bob>>>);
static_assert(has_empty_choice_v<Transmission<Alice, Bob, Ping, Choice<Bob, Carol>>>);
static_assert(has_empty_choice_v<Choice<Alice, Bob, BranchG<Ping, Choice<Bob, Carol>>>>);

static_assert(!has_empty_choice_v<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>);

consteval bool check_assert_no_empty_choice_compiles() {
    assert_no_empty_choice<End_G>();
    assert_no_empty_choice<Transmission<Alice, Bob, Ping, End_G>>();
    assert_no_empty_choice<Choice<Alice, Bob, BranchG<Ping, End_G>>>();
    assert_no_empty_choice<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>();
    return true;
}
static_assert(check_assert_no_empty_choice_compiles());

static_assert(std::is_same_v<plain_merge_t<>, End>);
static_assert(std::is_same_v<plain_merge_t<End>, End>);
static_assert(std::is_same_v<plain_merge_t<End, End, End>, End>);
static_assert(std::is_same_v<plain_merge_t<Send<int, End>, Send<int, End>>, Send<int, End>>);

static_assert(std::is_same_v<project_t<End_G, Alice>, End>);
static_assert(std::is_same_v<project_t<End_G, Bob>, End>);

using G_AB = Transmission<Alice, Bob, Ping, End_G>;

static_assert(std::is_same_v<project_t<G_AB, Alice>, Send<Ping, End>>);
static_assert(std::is_same_v<project_t<G_AB, Bob>, Recv<Ping, End>>);

using G_chain = Transmission<Alice, Bob, Ping, Transmission<Bob, Carol, Pong, End_G>>;

static_assert(std::is_same_v<project_t<G_chain, Alice>, Send<Ping, End>>);
static_assert(std::is_same_v<project_t<G_chain, Bob>, Recv<Ping, Send<Pong, End>>>);
static_assert(std::is_same_v<project_t<G_chain, Carol>, Recv<Pong, End>>);

using G_reqresp =
    Rec_G<Choice<Alice, Bob, BranchG<Query, Transmission<Bob, Alice, Reply, Var_G>>, BranchG<Ack, End_G>>>;

static_assert(
    std::is_same_v<project_t<G_reqresp, Alice>, Loop<Select<Send<Query, Recv<Reply, Continue>>, Send<Ack, End>>>>);

static_assert(
    std::is_same_v<project_t<G_reqresp, Bob>, Loop<Offer<Recv<Query, Send<Reply, Continue>>, Recv<Ack, End>>>>);

static_assert(std::is_same_v<dual_of_t<project_t<G_reqresp, Alice>>, project_t<G_reqresp, Bob>>);

using G_alice_stops = Transmission<Alice, Bob, Ping, StopG<Alice>>;

static_assert(std::is_same_v<project_t<G_alice_stops, Alice>, Send<Ping, Stop>>);
static_assert(std::is_same_v<project_t<G_alice_stops, Bob>, Recv<Ping, Stop>>);

static_assert(!std::is_same_v<End, Stop>);

// Bob sends to Alice, who crashes at once.  Bob cannot know whether his send
// arrived, so he must see crash-induced termination.
using G_send_then_crash = Transmission<Bob, Alice, Query, StopG<Alice>>;
static_assert(std::is_same_v<project_t<G_send_then_crash, Bob>, Send<Query, Stop>>);
static_assert(std::is_same_v<project_t<G_send_then_crash, Alice>, Recv<Query, Stop>>);

// Carol never interacts with Alice, so Alice's crash leaves Carol's session
// able to terminate cleanly while Bob's still ends in Stop.
using G_carol_unaffected = Transmission<Bob, Carol, Pong, Transmission<Alice, Bob, Ping, StopG<Alice>>>;
static_assert(std::is_same_v<project_t<G_carol_unaffected, Carol>, Recv<Pong, End>>);
static_assert(std::is_same_v<project_t<G_carol_unaffected, Bob>, Send<Pong, Recv<Ping, Stop>>>);

template <typename Proto>
struct ends_in_stop_recursive : std::false_type {};

template <>
struct ends_in_stop_recursive<Stop> : std::true_type {};

template <typename Payload, typename Continuation>
struct ends_in_stop_recursive<Send<Payload, Continuation>> : ends_in_stop_recursive<Continuation> {};

template <typename Payload, typename Continuation>
struct ends_in_stop_recursive<Recv<Payload, Continuation>> : ends_in_stop_recursive<Continuation> {};

template <typename Body>
struct ends_in_stop_recursive<Loop<Body>> : ends_in_stop_recursive<Body> {};

template <typename Proto>
inline constexpr bool ends_in_stop_v = ends_in_stop_recursive<Proto>::value;

static_assert(ends_in_stop_v<project_t<G_alice_stops, Alice>>);
static_assert(ends_in_stop_v<project_t<G_alice_stops, Bob>>);
static_assert(std::is_same_v<project_t<G_alice_stops, Carol>, End>);

static_assert(has_interaction_between_v<G_alice_stops, Alice, Bob>);
static_assert(has_interaction_between_v<G_alice_stops, Bob, Alice>);
static_assert(!has_interaction_between_v<G_alice_stops, Carol, Alice>);
static_assert(!has_interaction_between_v<G_alice_stops, Carol, Bob>);
static_assert(has_interaction_between_v<G_carol_unaffected, Bob, Carol>);
static_assert(has_interaction_between_v<G_carol_unaffected, Alice, Bob>);
static_assert(!has_interaction_between_v<G_carol_unaffected, Carol, Alice>);

// The crash classes are ordered by strictness.  The fixtures below pin every
// crossing of class against peer-or-survivor against interaction-present, so
// that no case silently collapses the four classes to one.

static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Abort>>, Alice>,
                             Send<Ping, Stop_g<CrashClass::Abort>>>);
static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Throw>>, Alice>,
                             Send<Ping, Stop_g<CrashClass::Throw>>>);
static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::ErrorReturn>>, Alice>,
                             Send<Ping, Stop_g<CrashClass::ErrorReturn>>>);
static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::NoThrow>>, Alice>,
                             Send<Ping, Stop_g<CrashClass::NoThrow>>>);

static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Throw>>, Bob>,
                             Recv<Ping, Stop_g<CrashClass::Throw>>>);
static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::NoThrow>>, Bob>,
                             Recv<Ping, Stop_g<CrashClass::NoThrow>>>);

using G_carol_uninvolved_throw =
    Transmission<Bob, Carol, Pong, Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Throw>>>;
static_assert(std::is_same_v<project_t<G_carol_uninvolved_throw, Carol>, Recv<Pong, End>>);

static_assert(!std::is_same_v<Stop_g<CrashClass::Abort>, Stop_g<CrashClass::Throw>>);
static_assert(!std::is_same_v<Stop_g<CrashClass::ErrorReturn>, Stop_g<CrashClass::NoThrow>>);

static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice>>, Alice>,
                             project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Abort>>, Alice>>);
static_assert(std::is_same_v<project_t<Transmission<Alice, Bob, Ping, StopG<Alice>>, Bob>,
                             project_t<Transmission<Alice, Bob, Ping, StopG<Alice, CrashClass::Abort>>, Bob>>);

static_assert(is_stop_g_v<StopG<Alice, CrashClass::Throw>>);
static_assert(is_stop_g_v<StopG<Alice, CrashClass::NoThrow>>);

static_assert(is_global_well_formed_v<StopG<Alice, CrashClass::ErrorReturn>>);

static_assert(contains_role_v<Alice, roles_of_t<StopG<Alice, CrashClass::NoThrow>>>);
static_assert(!contains_role_v<Bob, roles_of_t<StopG<Alice, CrashClass::NoThrow>>>);

// The fixtures above pin the rule on two and three participants.  Those below
// stress each interaction shape a real protocol produces — broadcast, fan-in,
// chain, diamond, star, and recursion — because each one drives the
// interaction walker down a different path.

struct David {};
struct Eve {};
struct Frank {};
struct Grace {};

struct Cmd {};
struct Beat {};
struct Vote {};
struct Sum {};
struct Close {};

using G_broadcast_crash = Transmission<
    Alice, Bob, Cmd,
    Transmission<Alice, Carol, Cmd, Transmission<Alice, David, Cmd, Transmission<Alice, Eve, Cmd, StopG<Alice>>>>>;

static_assert(is_global_well_formed_v<G_broadcast_crash>);
static_assert(roles_of_t<G_broadcast_crash>::size == 5);

static_assert(std::is_same_v<project_t<G_broadcast_crash, Alice>, Send<Cmd, Send<Cmd, Send<Cmd, Send<Cmd, Stop>>>>>);

static_assert(std::is_same_v<project_t<G_broadcast_crash, Bob>, Recv<Cmd, Stop>>);
static_assert(std::is_same_v<project_t<G_broadcast_crash, Carol>, Recv<Cmd, Stop>>);
static_assert(std::is_same_v<project_t<G_broadcast_crash, David>, Recv<Cmd, Stop>>);
static_assert(std::is_same_v<project_t<G_broadcast_crash, Eve>, Recv<Cmd, Stop>>);

static_assert(std::is_same_v<project_t<G_broadcast_crash, Frank>, End>);

static_assert(has_interaction_between_v<G_broadcast_crash, Alice, Bob>);
static_assert(has_interaction_between_v<G_broadcast_crash, Alice, Eve>);
static_assert(!has_interaction_between_v<G_broadcast_crash, Bob, Carol>);
static_assert(!has_interaction_between_v<G_broadcast_crash, David, Eve>);
static_assert(!has_interaction_between_v<G_broadcast_crash, Frank, Alice>);

using G_fan_in_crash =
    Transmission<Bob, Eve, Vote, Transmission<Carol, Eve, Vote, Transmission<David, Eve, Vote, StopG<Eve>>>>;

static_assert(is_global_well_formed_v<G_fan_in_crash>);
static_assert(roles_of_t<G_fan_in_crash>::size == 4);

static_assert(std::is_same_v<project_t<G_fan_in_crash, Eve>, Recv<Vote, Recv<Vote, Recv<Vote, Stop>>>>);

static_assert(std::is_same_v<project_t<G_fan_in_crash, Bob>, Send<Vote, Stop>>);
static_assert(std::is_same_v<project_t<G_fan_in_crash, Carol>, Send<Vote, Stop>>);
static_assert(std::is_same_v<project_t<G_fan_in_crash, David>, Send<Vote, Stop>>);

static_assert(std::is_same_v<project_t<G_fan_in_crash, Frank>, End>);

// Interaction is direct, not transitive.  A role sees Stop only when it
// talked to the crashed peer itself, which matches how a role detects a crash
// in practice: the peer it was talking to stopped answering, not someone
// else's peer upstream of it.  A transitive rule would mark every node of a
// long chain as crashed on any participant's failure, and would need causality
// analysis to compute.
//
// In the chain below only David talks to Eve directly, so the roles upstream
// of David terminate cleanly even though the pipeline they feed is broken.

using G_long_chain = Transmission<
    Alice, Bob, Beat,
    Transmission<Bob, Carol, Beat, Transmission<Carol, David, Beat, Transmission<David, Eve, Beat, StopG<Eve>>>>>;

static_assert(is_global_well_formed_v<G_long_chain>);
static_assert(roles_of_t<G_long_chain>::size == 5);

static_assert(std::is_same_v<project_t<G_long_chain, Eve>, Recv<Beat, Stop>>);
static_assert(std::is_same_v<project_t<G_long_chain, David>, Recv<Beat, Send<Beat, Stop>>>);
static_assert(std::is_same_v<project_t<G_long_chain, Carol>, Recv<Beat, Send<Beat, End>>>);
static_assert(std::is_same_v<project_t<G_long_chain, Bob>, Recv<Beat, Send<Beat, End>>>);
static_assert(std::is_same_v<project_t<G_long_chain, Alice>, Send<Beat, End>>);

static_assert(has_interaction_between_v<G_long_chain, David, Eve>);
static_assert(has_interaction_between_v<G_long_chain, Eve, David>);
static_assert(!has_interaction_between_v<G_long_chain, Carol, Eve>);
static_assert(!has_interaction_between_v<G_long_chain, Alice, Eve>);
static_assert(!has_interaction_between_v<G_long_chain, Bob, David>);

// Alice initiated the pipeline that killed David but never talked to him, so
// she terminates cleanly while Bob and Carol, who did, see Stop.

using G_diamond_crash = Transmission<
    Alice, Bob, Cmd,
    Transmission<Alice, Carol, Cmd, Transmission<Bob, David, Sum, Transmission<Carol, David, Sum, StopG<David>>>>>;

static_assert(is_global_well_formed_v<G_diamond_crash>);
static_assert(roles_of_t<G_diamond_crash>::size == 4);

static_assert(std::is_same_v<project_t<G_diamond_crash, David>, Recv<Sum, Recv<Sum, Stop>>>);
static_assert(std::is_same_v<project_t<G_diamond_crash, Bob>, Recv<Cmd, Send<Sum, Stop>>>);
static_assert(std::is_same_v<project_t<G_diamond_crash, Carol>, Recv<Cmd, Send<Sum, Stop>>>);
static_assert(std::is_same_v<project_t<G_diamond_crash, Alice>, Send<Cmd, Send<Cmd, End>>>);

static_assert(has_interaction_between_v<G_diamond_crash, Alice, Bob>);
static_assert(has_interaction_between_v<G_diamond_crash, Alice, Carol>);
static_assert(has_interaction_between_v<G_diamond_crash, Bob, David>);
static_assert(has_interaction_between_v<G_diamond_crash, Carol, David>);
static_assert(!has_interaction_between_v<G_diamond_crash, Alice, David>);
static_assert(!has_interaction_between_v<G_diamond_crash, Bob, Carol>);

// Bob's interaction with Alice sits in the beat arm, while the crash sits in
// the close arm.  Bob still sees Stop on the close arm, so the walker must
// descend through the recursion into the other branch to find that
// interaction.

using G_pump_crash = Rec_G<Choice<Alice, Bob, BranchG<Beat, Var_G>, BranchG<Close, StopG<Alice>>>>;

static_assert(is_global_well_formed_v<G_pump_crash>);

static_assert(std::is_same_v<project_t<G_pump_crash, Alice>, Loop<Select<Send<Beat, Continue>, Send<Close, Stop>>>>);
static_assert(std::is_same_v<project_t<G_pump_crash, Bob>, Loop<Offer<Recv<Beat, Continue>, Recv<Close, Stop>>>>);

static_assert(std::is_same_v<dual_of_t<project_t<G_pump_crash, Alice>>, project_t<G_pump_crash, Bob>>);

static_assert(has_interaction_between_v<G_pump_crash, Alice, Bob>);
static_assert(!has_interaction_between_v<G_pump_crash, Alice, Carol>);

using G_fan_out_in_crash = Transmission<Carol, David, Beat, Transmission<David, Frank, Beat, StopG<Frank>>>;

static_assert(is_global_well_formed_v<G_fan_out_in_crash>);
static_assert(roles_of_t<G_fan_out_in_crash>::size == 3);

static_assert(std::is_same_v<project_t<G_fan_out_in_crash, Frank>, Recv<Beat, Stop>>);
static_assert(std::is_same_v<project_t<G_fan_out_in_crash, David>, Recv<Beat, Send<Beat, Stop>>>);
static_assert(std::is_same_v<project_t<G_fan_out_in_crash, Carol>, Send<Beat, End>>);
static_assert(std::is_same_v<project_t<G_fan_out_in_crash, Grace>, End>);

using G_star_hub_crash = Transmission<
    Alice, Eve, Vote,
    Transmission<Bob, Eve, Vote, Transmission<Carol, Eve, Vote, Transmission<David, Eve, Vote, StopG<Eve>>>>>;

static_assert(is_global_well_formed_v<G_star_hub_crash>);
static_assert(roles_of_t<G_star_hub_crash>::size == 5);

static_assert(std::is_same_v<project_t<G_star_hub_crash, Eve>, Recv<Vote, Recv<Vote, Recv<Vote, Recv<Vote, Stop>>>>>);

static_assert(std::is_same_v<project_t<G_star_hub_crash, Alice>, Send<Vote, Stop>>);
static_assert(std::is_same_v<project_t<G_star_hub_crash, Bob>, Send<Vote, Stop>>);
static_assert(std::is_same_v<project_t<G_star_hub_crash, Carol>, Send<Vote, Stop>>);
static_assert(std::is_same_v<project_t<G_star_hub_crash, David>, Send<Vote, Stop>>);

static_assert(std::is_same_v<project_t<G_star_hub_crash, Frank>, End>);

// This is the only fixture whose interacting pair sits below a choice's own
// From and To.  A walker that inspected the choice's pair but forgot to fold
// over the branches would call Bob and Carol non-interacting here and still
// pass every other fixture in this file.

using G_nested_choice = Choice<Alice, Bob, BranchG<Cmd, Transmission<Bob, Carol, Reply, End_G>>, BranchG<Ack, End_G>>;

static_assert(is_global_well_formed_v<G_nested_choice>);

static_assert(has_interaction_between_v<G_nested_choice, Alice, Bob>);
static_assert(has_interaction_between_v<G_nested_choice, Bob, Alice>);

static_assert(has_interaction_between_v<G_nested_choice, Bob, Carol>);
static_assert(has_interaction_between_v<G_nested_choice, Carol, Bob>);

static_assert(!has_interaction_between_v<G_nested_choice, Alice, Carol>);

using G_nested_choice_with_crash =
    Choice<Alice, Bob, BranchG<Cmd, Transmission<Bob, Carol, Reply, StopG<Bob>>>, BranchG<Ack, End_G>>;

static_assert(is_global_well_formed_v<G_nested_choice_with_crash>);

// RootG threading is tree-level, not path-level.  Carol interacts with Bob on
// one branch only, yet the StopG rule consults the whole tree, so Carol's
// projection ends in Stop on every branch.  Carol's branch projections diverge
// and cannot be plain-merged, which is the limit this fixture marks.  The
// query itself still answers correctly.
static_assert(has_interaction_between_v<G_nested_choice_with_crash, Carol, Bob>);

// This fixture wires the walker's three recursion arms at once: it must
// descend through the recursion, then the choice, then a branch continuation
// to reach the nested pair.

using G_loop_with_nested_pair =
    Rec_G<Choice<Alice, Bob, BranchG<Beat, Transmission<Bob, Carol, Reply, Var_G>>, BranchG<Close, End_G>>>;

static_assert(is_global_well_formed_v<G_loop_with_nested_pair>);

static_assert(has_interaction_between_v<G_loop_with_nested_pair, Alice, Bob>);
static_assert(has_interaction_between_v<G_loop_with_nested_pair, Bob, Alice>);

static_assert(has_interaction_between_v<G_loop_with_nested_pair, Bob, Carol>);
static_assert(has_interaction_between_v<G_loop_with_nested_pair, Carol, Bob>);

static_assert(!has_interaction_between_v<G_loop_with_nested_pair, Alice, Carol>);
static_assert(!has_interaction_between_v<G_loop_with_nested_pair, David, Bob>);

static_assert(same_unordered_pair_v<Alice, Bob, Alice, Bob>);
static_assert(same_unordered_pair_v<Alice, Bob, Bob, Alice>);
static_assert(!same_unordered_pair_v<Alice, Bob, Alice, Carol>);
static_assert(!same_unordered_pair_v<Alice, Bob, Carol, David>);
// A self-pair is trivially equal to itself at this level.  Well-formedness
// rejects the self-directed event before the walker can ever ask.
static_assert(same_unordered_pair_v<Alice, Alice, Alice, Alice>);

// A StopG standing alone.  The peer's own rule must answer Stop without
// consulting the tree, and every other role must answer End because the tree
// holds no interaction at all.
using G_only_stop = StopG<Alice>;
static_assert(is_global_well_formed_v<G_only_stop>);
static_assert(roles_of_t<G_only_stop>::size == 1);
static_assert(contains_role_v<Alice, roles_of_t<G_only_stop>>);
static_assert(std::is_same_v<project_t<G_only_stop, Alice>, Stop>);
static_assert(std::is_same_v<project_t<G_only_stop, Bob>, End>);
static_assert(std::is_same_v<project_t<G_only_stop, Carol>, End>);
static_assert(std::is_same_v<project_t<G_only_stop, Frank>, End>);
static_assert(!has_interaction_between_v<G_only_stop, Alice, Bob>);
static_assert(!has_interaction_between_v<G_only_stop, Bob, Alice>);

// Alice both sends earlier and is the crashed peer, and two separate edges
// resolve on her.  The walker must aggregate both, so Bob and Carol each end
// in Stop.
using G_peer_is_from_earlier = Transmission<Alice, Bob, Ping, Transmission<Carol, Alice, Query, StopG<Alice>>>;

static_assert(is_global_well_formed_v<G_peer_is_from_earlier>);
static_assert(roles_of_t<G_peer_is_from_earlier>::size == 3);

static_assert(std::is_same_v<project_t<G_peer_is_from_earlier, Bob>, Recv<Ping, Stop>>);
static_assert(std::is_same_v<project_t<G_peer_is_from_earlier, Carol>, Send<Query, Stop>>);
static_assert(std::is_same_v<project_t<G_peer_is_from_earlier, Alice>, Send<Ping, Recv<Query, Stop>>>);
static_assert(std::is_same_v<project_t<G_peer_is_from_earlier, Frank>, End>);

static_assert(has_interaction_between_v<G_peer_is_from_earlier, Alice, Bob>);
static_assert(has_interaction_between_v<G_peer_is_from_earlier, Carol, Alice>);
static_assert(!has_interaction_between_v<G_peer_is_from_earlier, Bob, Carol>);

// A walker that descended only one level of recursion would answer false here.
using G_nested_rec_3deep = Rec_G<Rec_G<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>>;

static_assert(is_global_well_formed_v<G_nested_rec_3deep>);
static_assert(has_interaction_between_v<G_nested_rec_3deep, Alice, Bob>);
static_assert(has_interaction_between_v<G_nested_rec_3deep, Bob, Alice>);
static_assert(!has_interaction_between_v<G_nested_rec_3deep, Alice, Carol>);

static_assert(std::is_same_v<project_t<G_nested_rec_3deep, Alice>, Loop<Loop<Loop<Send<Ping, Continue>>>>>);
static_assert(std::is_same_v<project_t<G_nested_rec_3deep, Bob>, Loop<Loop<Loop<Recv<Ping, Continue>>>>>);

// One pair repeats: it is the choice's own pair and appears again inside a
// branch.  The check is set membership, so the repeat resolves on first hit.
// What this fixture pins is that the repeat does not short-circuit the walk
// before it reaches the other pair nested deeper in the same branch.
using G_repeated_pair =
    Rec_G<Choice<Alice, Bob, BranchG<Ping, Transmission<Alice, Bob, Query, Transmission<Bob, Carol, Reply, Var_G>>>,
                 BranchG<Ack, End_G>>>;

static_assert(is_global_well_formed_v<G_repeated_pair>);

static_assert(has_interaction_between_v<G_repeated_pair, Alice, Bob>);

static_assert(has_interaction_between_v<G_repeated_pair, Bob, Carol>);
static_assert(has_interaction_between_v<G_repeated_pair, Carol, Bob>);

static_assert(!has_interaction_between_v<G_repeated_pair, Alice, Carol>);
static_assert(!has_interaction_between_v<G_repeated_pair, David, Eve>);

// These three guard against a StopG rule that answered End for every surviving
// role.  Such a rule would drop the crash signal without failing anywhere, so
// each assertion below names the wrong shape it must not produce.
static_assert(!std::is_same_v<project_t<G_alice_stops, Bob>, Recv<Ping, End>>);

static_assert(!std::is_same_v<project_t<G_send_then_crash, Bob>, Send<Query, End>>);

static_assert(!std::is_same_v<project_t<G_diamond_crash, Bob>, Recv<Cmd, Send<Sum, End>>>);

using G_forever = Rec_G<Transmission<Alice, Bob, Ping, Var_G>>;

static_assert(std::is_same_v<project_t<G_forever, Alice>, Loop<Send<Ping, Continue>>>);

static_assert(std::is_same_v<project_t<G_forever, Bob>, Loop<Recv<Ping, Continue>>>);

// Both branches leave Carol the same continuation, so the plain merge on the
// third-party role succeeds.

using G_merged = Choice<Alice, Bob, BranchG<Ping, Transmission<Bob, Carol, Reply, End_G>>,
                        BranchG<Ack, Transmission<Bob, Carol, Reply, End_G>>>;

static_assert(std::is_same_v<project_t<G_merged, Carol>, Recv<Reply, End>>);

static_assert(std::is_same_v<project_t<G_merged, Alice>, Select<Send<Ping, End>, Send<Ack, End>>>);

static_assert(
    std::is_same_v<project_t<G_merged, Bob>, Offer<Recv<Ping, Send<Reply, End>>, Recv<Ack, Send<Reply, End>>>>);

// Projection preserves duality on a two-party global type: one party's
// projection is the dual of the other's.

static_assert(std::is_same_v<dual_of_t<project_t<G_AB, Alice>>, project_t<G_AB, Bob>>);

static_assert(std::is_same_v<dual_of_t<project_t<G_forever, Alice>>, project_t<G_forever, Bob>>);

using G_rrloop = Rec_G<Transmission<Alice, Bob, Query, Transmission<Bob, Alice, Reply, Var_G>>>;

static_assert(std::is_same_v<project_t<G_rrloop, Alice>, Loop<Send<Query, Recv<Reply, Continue>>>>);

static_assert(std::is_same_v<project_t<G_rrloop, Bob>, Loop<Recv<Query, Send<Reply, Continue>>>>);

}  // namespace detail::global::global_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
