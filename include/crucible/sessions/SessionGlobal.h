#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — L4 global types G + projection (SEPLOG-
//                            H2g, task #339)
//
// A GLOBAL TYPE G is a bird's-eye-view protocol spec that names every
// participant and every communication event in one syntactic object.
// Projection G↾p extracts participant p's local view — the ordinary
// L1 session type they execute.
//
// The value proposition of top-down multiparty session types (MPST):
// write the protocol once as a global type, prove its φ-properties
// at the global level (safety, deadlock-freedom, liveness), project
// to every role, and EVERY well-typed implementation of the projected
// local types is correct by construction.  No per-pair reasoning.
//
// This header ships the STRUCTURE and PROJECTION of global types
// (plain merging for third-party branches; full merging deferred).
//
// ─── Combinators ──────────────────────────────────────────────────
//
//     End_G                                  terminal
//     Transmission<From, To, P, G>           p sends P to q, continue G
//     BranchG<Payload, G>                    a single labeled branch
//     Choice<From, To, Branches...>          p sends one of N payloads
//                                             to q; positional index =
//                                             the label
//     Rec_G<Body>                            μ-recursion (unnamed var)
//     Var_G                                  recursion-back marker
//                                             (binds to nearest Rec_G)
//     StopG<Peer>                            peer has crashed
//
// ─── Operations ────────────────────────────────────────────────────
//
//     RoleList<Rs...>                        type-list of roles
//     insert_unique_t<R, RL>                 RL ∪ {R}
//     union_roles_t<RL1, RL2>                RL1 ∪ RL2
//     RolesOf<G> / roles_of_t<G>             all roles in G
//     is_global_well_formed_v<G>             every Var_G has enclosing Rec_G
//     Project<G, Role> / project_t<G, Role>  projection G↾Role
//     plain_merge_t<T...>                    plain merge (all-equal)
//
// ─── Projection rules ─────────────────────────────────────────────
//
//   End_G                    ↾ R = End
//   Transmission<F, T, P, G> ↾ F = Send<P, G↾F>           (sender)
//   Transmission<F, T, P, G> ↾ T = Recv<P, G↾T>           (receiver)
//   Transmission<F, T, P, G> ↾ R = G↾R                    (third-party,
//                                                          skip event)
//   Choice<F, T, B...>       ↾ F = Select<Send<B.p, B.g↾F>...>   (sender)
//   Choice<F, T, B...>       ↾ T = Offer<Recv<B.p, B.g↾T>...>    (receiver)
//   Choice<F, T, B...>       ↾ R = plain_merge_t<B.g↾R...>       (third)
//   Rec_G<Body>              ↾ R = Loop<Body↾R>
//   Var_G                    ↾ R = Continue
//   StopG<Peer>              ↾ Peer = Stop
//   StopG<Peer>              ↾ R    = End                  (R ≠ Peer)
//
// ─── Positional branches instead of labels ────────────────────────
//
// Classical MPST (Honda-Yoshida-Carbone 2008) uses LABELED branches:
// `p → q : {m_1(P_1).G_1, m_2(P_2).G_2}`.  Our L1 Select/Offer use
// POSITIONAL branches — the index is the implicit label.  Choice
// here matches that encoding: branches are ordered; index = label;
// projection preserves the positional order.
//
// Consequence: "reorder branches" refactors that classical MPST
// permits aren't automatic subtype relations here.  They're
// expressible as explicit refactors of both the Choice and its
// projected Select/Offer together.
//
// ─── Merge: plain (shipped) vs full (deferred) ────────────────────
//
// When projecting Choice onto a THIRD-PARTY role R (R ≠ From, R ≠ To),
// each branch produces a local type; these must be merged into a
// single local type for R.
//
//   Plain merge (this ship):  all branch projections must be equal;
//                             otherwise compile error with a clear
//                             diagnostic.
//
//   Full merge (PMY25 §4.3):  coinductive full merging admits branches
//                             that structurally diverge if R can
//                             distinguish them later in the protocol.
//                             Deferred — requires coinductive fixed-
//                             point machinery.
//
// Plain merge is SUFFICIENT for binary protocols (only two roles, so
// no third-party projection) and for N-party protocols where every
// role is involved in every Choice (like symmetric collectives).
// Raft, 2PC with multiple followers, and similar DIVERGING protocols
// need full merge — flagged as deferred in the self-test block.
//
// ─── Eager-evaluation trap, solved ────────────────────────────────
//
// The obvious way to dispatch projection — `std::conditional_t` over
// the three cases (sender/receiver/third-party) — INSTANTIATES ALL
// THREE ARMS because std::conditional_t is a template alias evaluated
// eagerly.  On a sender-projection of a Choice, the third-party arm
// instantiates plain_merge_t over differing projections and fails to
// compile even though only the sender arm is semantically needed.
//
// Fix: dispatch via a compile-time enum + partial-specialisation of a
// helper metafunction.  Only the matching specialisation instantiates.
// See detail::global::project_case_v + project_choice / project_transmission
// below.
//
// ─── References ───────────────────────────────────────────────────
//
//   Honda-Yoshida-Carbone 2008 (POPL) / JACM 2016 — classical MPST;
//     global type G, projection G↾p, merging, coherence, consistency.
//   Hou-Yoshida-Kuhn 2024 — the association fix (Δ ⊑_s G replaces
//     consistency as the correct invariant under reductions); this
//     layer is the G side of Δ ⊑_s G (L5 arrives in #345).
//   Pischke-Masters-Yoshida 2025 — async top-down MPST; en-route
//     global-type primitives.  Async extensions deferred to L7.
//   Scalas-Yoshida 2019 — alternative bottom-up formulation; Γ-only.
//     Both approaches are supported: Γ from L2 for bottom-up, G from
//     this header for top-down, with L5 Association bridging them.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionContext.h>   // detail::ctx::type_id_hash_v
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

// "dependent_false" — defers static_assert to instantiation time so
// error diagnostics fire only on actual use.
template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::global

// ═════════════════════════════════════════════════════════════════════
// ── Global-type combinators ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Terminal global type.
struct End_G {};

// Single communication event: From sends To a value of type Payload,
// then the protocol continues as G.
template <typename From, typename To, typename Payload, typename G>
struct Transmission {
    using from    = From;
    using to      = To;
    using payload = Payload;
    using next    = G;
};

// A single labeled branch in a Choice.  Positional ordering —
// branch_index = label.
template <typename Payload, typename G>
struct BranchG {
    using payload = Payload;
    using next    = G;
};

// n-way choice: From sends To exactly one of the Branches' payloads;
// the protocol continues as that branch's continuation.
template <typename From, typename To, typename... Branches>
struct Choice {
    using from    = From;
    using to      = To;
    static constexpr std::size_t branch_count = sizeof...(Branches);
};

// μ-recursion with unnamed variable.  Var_G inside Body binds to the
// NEAREST enclosing Rec_G (like de Bruijn index 0).  Nested Rec_G
// shadow outer ones.
template <typename Body>
struct Rec_G {
    using body = Body;
};

// Recursion variable.  Only valid syntactically inside a Rec_G.
struct Var_G {};

// StopG<Peer> — participant Peer has crashed at this point in the
// protocol.  Peer's projection is Stop; other roles' projections at
// this point are End (their protocol has finished by this point in
// the global type).
template <typename Peer>
struct StopG {
    using peer = Peer;
};

// ═════════════════════════════════════════════════════════════════════
// ── Shape traits ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename G> struct is_end_g : std::false_type {};
template <> struct is_end_g<End_G> : std::true_type {};

template <typename G> struct is_transmission : std::false_type {};
template <typename F, typename T, typename P, typename N>
struct is_transmission<Transmission<F, T, P, N>> : std::true_type {};

template <typename G> struct is_choice : std::false_type {};
template <typename F, typename T, typename... Bs>
struct is_choice<Choice<F, T, Bs...>> : std::true_type {};

template <typename G> struct is_rec_g : std::false_type {};
template <typename B> struct is_rec_g<Rec_G<B>> : std::true_type {};

template <typename G> struct is_var_g : std::false_type {};
template <> struct is_var_g<Var_G> : std::true_type {};

template <typename G> struct is_stop_g : std::false_type {};
template <typename P> struct is_stop_g<StopG<P>> : std::true_type {};

template <typename G> inline constexpr bool is_end_g_v        = is_end_g<G>::value;
template <typename G> inline constexpr bool is_transmission_v = is_transmission<G>::value;
template <typename G> inline constexpr bool is_choice_v       = is_choice<G>::value;
template <typename G> inline constexpr bool is_rec_g_v        = is_rec_g<G>::value;
template <typename G> inline constexpr bool is_var_g_v        = is_var_g<G>::value;
template <typename G> inline constexpr bool is_stop_g_v       = is_stop_g<G>::value;

// ═════════════════════════════════════════════════════════════════════
// ── RoleList + set operations ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

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
    : std::bool_constant<
          std::is_same_v<R, Head> ||
          contains_role<R, RoleList<Rest...>>::value
      > {};

template <typename R, typename RL>
inline constexpr bool contains_role_v = contains_role<R, RL>::value;

template <typename R, typename RL>
struct insert_unique;

template <typename R, typename... Rs>
struct insert_unique<R, RoleList<Rs...>> {
    using type = std::conditional_t<
        contains_role_v<R, RoleList<Rs...>>,
        RoleList<Rs...>,
        RoleList<R, Rs...>>;
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
    : union_roles<
          insert_unique_t<Head, RoleList<Rs1...>>,
          RoleList<Rest...>> {};

}  // namespace detail::global

template <typename RL1, typename RL2>
using union_roles_t = typename detail::global::union_roles<RL1, RL2>::type;

// ═════════════════════════════════════════════════════════════════════
// ── RolesOf<G>  (#374 SEPLOG-PERF-4: sort-based dedup, O(N log N)) ─
// ═════════════════════════════════════════════════════════════════════
//
// Implementation strategy: walk G recursively to COLLECT all role
// instances into a flat parameter pack (with duplicates).  Then
// deduplicate via a single consteval sort over hash projections,
// rebuilding the deduped RoleList via C++26 pack indexing.
//
// Total cost: O(N) collection + O(N log N) consteval sort, replacing
// the previous chain of O(N) `insert_unique` template instantiations
// each doing an O(d) `contains_role` linear scan for total
// O(N²) template-instantiation work.  At 100-role globals (rare but
// possible in MPST-heavy adapter codegen) the saving is ~10×.
//
// Output ordering: the deduped RoleList is sorted by first-occurrence
// index in the input pack — independent of hash-based intermediate
// sort.  All current consumers (`role_lists_equal_as_sets_v`,
// `contains_role_v`, `::size`, lookup via per-role iteration) are
// order-insensitive, so the change is invisible.

namespace detail::global {

// ── flat_roles_t<G> — collect all role instances with duplicates ──
//
// Walks G with the same recursive structure as the old RolesOf,
// but APPENDS rather than UNIQ-INSERTS.  Output is RoleList<R1, R2,
// ..., Rn> where Ri can repeat.

template <typename G>
struct flat_roles;

template <>
struct flat_roles<End_G> { using type = RoleList<>; };

template <>
struct flat_roles<Var_G> { using type = RoleList<>; };

template <typename Peer>
struct flat_roles<StopG<Peer>> { using type = RoleList<Peer>; };

template <typename Body>
struct flat_roles<Rec_G<Body>> {
    using type = typename flat_roles<Body>::type;
};

// Concatenation of role lists.
template <typename... RLs>
struct concat_role_lists;

template <>
struct concat_role_lists<> { using type = RoleList<>; };

template <typename... Rs>
struct concat_role_lists<RoleList<Rs...>> { using type = RoleList<Rs...>; };

template <typename... A, typename... B, typename... Rest>
struct concat_role_lists<RoleList<A...>, RoleList<B...>, Rest...>
    : concat_role_lists<RoleList<A..., B...>, Rest...> {};

template <typename... RLs>
using concat_role_lists_t = typename concat_role_lists<RLs...>::type;

template <typename From, typename To, typename P, typename G>
struct flat_roles<Transmission<From, To, P, G>> {
    using type = concat_role_lists_t<
        RoleList<From, To>,
        typename flat_roles<G>::type>;
};

template <typename From, typename To, typename... Bs>
struct flat_roles<Choice<From, To, Bs...>> {
    using type = concat_role_lists_t<
        RoleList<From, To>,
        typename flat_roles<typename Bs::next>::type...>;
};

template <typename G>
using flat_roles_t = typename flat_roles<G>::type;

// ── compute_dedup_count<Rs...>() — number of distinct role types ──
//
// Hashes each role, sorts hashes, counts unique adjacent.

template <typename... Rs>
[[nodiscard]] inline consteval std::size_t compute_dedup_count() noexcept {
    constexpr std::size_t N = sizeof...(Rs);
    if constexpr (N == 0) return 0;
    else {
        std::array<std::uint64_t, N> hashes{
            detail::ctx::type_id_hash_v<Rs>...
        };
        std::ranges::sort(hashes);
        std::size_t unique = 1;
        for (std::size_t j = 1; j < N; ++j) {
            if (hashes[j] != hashes[j - 1]) ++unique;
        }
        return unique;
    }
}

// ── compute_kept_indices<Rs...>() — first-occurrence indices of each
//    distinct role, sorted by original-input position ───────────────

template <typename... Rs>
[[nodiscard]] inline consteval auto compute_kept_indices() noexcept {
    constexpr std::size_t N = sizeof...(Rs);
    std::array<std::size_t, N == 0 ? 1 : N> kept{};
    if constexpr (N == 0) return kept;  // unused
    else {
        std::array<std::pair<std::uint64_t, std::size_t>, N> tagged{};
        for (std::size_t i = 0; i < N; ++i) {
            tagged[i] = std::pair{std::uint64_t{0}, i};
        }
        // Fill hashes via fold-expression.
        const std::array<std::uint64_t, N> hashes{
            detail::ctx::type_id_hash_v<Rs>...
        };
        for (std::size_t i = 0; i < N; ++i) tagged[i].first = hashes[i];

        // Sort by (hash, original_index) so equal-hash entries land
        // adjacent with the lowest-index first (gives first-occurrence
        // semantics on dedup).
        std::ranges::sort(tagged, [](const auto& a, const auto& b){
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        // First-occurrence dedup: for each unique hash, keep the
        // smallest original index.
        std::size_t k = 0;
        for (std::size_t j = 0; j < N; ++j) {
            if (j == 0 || tagged[j].first != tagged[j - 1].first) {
                kept[k++] = tagged[j].second;
            }
        }

        // Re-sort kept indices by their original-input position so the
        // output preserves first-occurrence ORDER, not hash order.
        std::sort(kept.begin(), kept.begin() + k);
        return kept;
    }
}

// ── build_dedup_role_list — pack-indexes via the kept array ──────
//
// Only invoked for non-empty packs; the empty case is short-circuited
// at the dedup_role_list trait below.

template <typename First, typename... Rest, std::size_t... Is>
auto build_dedup_role_list_helper(std::index_sequence<Is...>) {
    static constexpr auto kept = compute_kept_indices<First, Rest...>();
    using Combined = std::tuple<First, Rest...>;
    return RoleList<std::tuple_element_t<kept[Is], Combined>...>{};
}

template <typename RL>
struct dedup_role_list;

// Empty-pack short-circuit: avoid pack-indexing on an empty pack.
template <>
struct dedup_role_list<RoleList<>> { using type = RoleList<>; };

template <typename First, typename... Rest>
struct dedup_role_list<RoleList<First, Rest...>> {
    static constexpr std::size_t kept_n = compute_dedup_count<First, Rest...>();
    using type = decltype(build_dedup_role_list_helper<First, Rest...>(
        std::make_index_sequence<kept_n>{}));
};

template <typename RL>
using dedup_role_list_t = typename dedup_role_list<RL>::type;

}  // namespace detail::global

// Public surface unchanged: RolesOf<G> + roles_of_t<G> remain the
// API, now backed by collect-then-dedup.

template <typename G>
struct RolesOf {
    using type = detail::global::dedup_role_list_t<
        detail::global::flat_roles_t<G>>;
};

template <typename G>
using roles_of_t = typename RolesOf<G>::type;

// ═════════════════════════════════════════════════════════════════════
// ── is_global_well_formed_v<G> ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every Var_G must have an enclosing Rec_G.  Nested Rec_G shadow
// outer ones — Var_G binds to nearest enclosing.
//
// Additional check (#363): every Transmission<From, To, P, G> and
// Choice<From, To, Bs...> must have From ≠ To — a participant cannot
// send to itself in MPST.  Self-transmissions project to nonsense
// local types (the same role would appear at both Send and Recv
// positions) and would deadlock at runtime if reachable.

template <typename G, typename RecCtx = void>
struct is_global_well_formed;

template <typename RecCtx>
struct is_global_well_formed<End_G, RecCtx> : std::true_type {};

template <typename RecCtx>
struct is_global_well_formed<Var_G, RecCtx>
    : std::bool_constant<!std::is_void_v<RecCtx>> {};

template <typename From, typename To, typename P, typename G, typename RecCtx>
struct is_global_well_formed<Transmission<From, To, P, G>, RecCtx>
    : std::bool_constant<
          !std::is_same_v<From, To>
          && is_global_well_formed<G, RecCtx>::value
      > {};

template <typename From, typename To, typename... Bs, typename RecCtx>
struct is_global_well_formed<Choice<From, To, Bs...>, RecCtx>
    : std::bool_constant<
          !std::is_same_v<From, To>
          && (is_global_well_formed<typename Bs::next, RecCtx>::value && ...)
      > {};

template <typename Body, typename RecCtx>
struct is_global_well_formed<Rec_G<Body>, RecCtx>
    // Rec_G introduces itself as the new RecCtx for checking Body.
    : is_global_well_formed<Body, Rec_G<Body>> {};

template <typename Peer, typename RecCtx>
struct is_global_well_formed<StopG<Peer>, RecCtx> : std::true_type {};

template <typename G>
inline constexpr bool is_global_well_formed_v =
    is_global_well_formed<G>::value;

// ─── Self-loop detection (#363) ────────────────────────────────────
//
// has_self_loop_v<G> is true iff G CONTAINS a Transmission<X, X, ...>
// or Choice<X, X, ...> at any position.  Distinct from
// is_global_well_formed_v's negation: is_global_well_formed_v can
// also return false for a free Var_G; this trait pinpoints the
// self-transmission case specifically, so the diagnostic helper
// below can give a routed message instead of the generic
// "ill-formed" failure.

template <typename G>
struct has_self_loop : std::false_type {};

template <typename From, typename To, typename P, typename G>
struct has_self_loop<Transmission<From, To, P, G>>
    : std::bool_constant<
          std::is_same_v<From, To> || has_self_loop<G>::value
      > {};

template <typename From, typename To, typename... Bs>
struct has_self_loop<Choice<From, To, Bs...>>
    : std::bool_constant<
          std::is_same_v<From, To>
          || (has_self_loop<typename Bs::next>::value || ...)
      > {};

template <typename Body>
struct has_self_loop<Rec_G<Body>> : has_self_loop<Body> {};

template <typename G>
inline constexpr bool has_self_loop_v = has_self_loop<G>::value;

// assert_no_self_loop<G>() — consteval helper that fires a routed
// [ProtocolViolation_Self_Loop] static_assert when G contains a
// self-transmission anywhere in its tree.  Use at protocol
// declaration sites for the cleanest diagnostic; the framework-
// controlled tag prefix is stable across GCC versions (#371).

template <typename G>
consteval void assert_no_self_loop() noexcept {
    static_assert(!has_self_loop_v<G>,
        "crucible::session::diagnostic [ProtocolViolation_Self_Loop]: "
        "global type contains a Transmission<X, X, ...> or "
        "Choice<X, X, ...> — a participant cannot send to itself in "
        "MPST.  Check that From and To in your Transmission / Choice "
        "are different role tags.  Common cause: copy-paste error "
        "where both sides reference the same role tag.  If you "
        "genuinely want a participant's local-only state transition, "
        "model it as a Machine<State> transition outside the global "
        "protocol rather than as a self-Transmission.");
}

// ═════════════════════════════════════════════════════════════════════
// ── plain_merge_t<Ts...> ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All branches must project to the SAME local type; otherwise the
// merge is undefined (would need full merging, deferred).  Fires a
// named static_assert with remediation guidance if branches diverge.

namespace detail::global {

template <typename... Ts>
struct plain_merge_impl;

template <>
struct plain_merge_impl<> {
    // Empty merge — no branches.  Doesn't arise from a well-formed
    // Choice (which requires at least one branch); yield End as a
    // placeholder.
    using type = End;
};

template <typename T>
struct plain_merge_impl<T> {
    using type = T;
};

template <typename T, typename... Rest>
struct plain_merge_impl<T, Rest...> {
    static_assert((std::is_same_v<T, Rest> && ...),
        "crucible::session::diagnostic [Merge_Branches_Diverge]: "
        "plain_merge_t: branch projections differ.  The third-party "
        "role sees structurally-different local types across Choice "
        "branches, which plain merging cannot unify.  Full merging "
        "(PMY25 §4.3 coinductive) is required — not yet implemented "
        "in this shipment of SessionGlobal.h.  Workaround: project "
        "to a role that IS involved in every Choice (From or To), "
        "or restructure the global type so third-party projections "
        "match across all branches.");
    using type = T;
};

}  // namespace detail::global

template <typename... Ts>
using plain_merge_t = typename detail::global::plain_merge_impl<Ts...>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Project<G, Role> — projection metafunction ─────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename G, typename Role>
struct Project;

// End_G projects to End for every role.
template <typename Role>
struct Project<End_G, Role> { using type = End; };

// Var_G projects to Continue (framework enforces Var_G inside Rec_G).
template <typename Role>
struct Project<Var_G, Role> { using type = Continue; };

// Rec_G<Body> projects to Loop<Body↾R>.
template <typename Body, typename Role>
struct Project<Rec_G<Body>, Role> {
    using type = Loop<typename Project<Body, Role>::type>;
};

// StopG<Peer> — peer sees Stop; others see End.
template <typename Peer>
struct Project<StopG<Peer>, Peer> {
    using type = Stop;
};

// Non-Peer role: the Peer crashed, others' protocol ends here.
// The specialisation above is more specialised (pins Role=Peer), so
// it wins for Role=Peer; this fallback catches Role≠Peer.
template <typename Peer, typename Role>
struct Project<StopG<Peer>, Role> {
    using type = End;
};

// ─── Transmission projection: three-case dispatch ────────────────

namespace detail::global {

enum class proj_case { sender, receiver, third_party };

template <typename Role, typename From, typename To>
inline constexpr proj_case project_case_v =
      std::is_same_v<Role, From> ? proj_case::sender
    : std::is_same_v<Role, To>   ? proj_case::receiver
    :                              proj_case::third_party;

template <proj_case C, typename From, typename To, typename Role,
          typename P, typename G>
struct project_transmission_helper;

template <typename From, typename To, typename Role, typename P, typename G>
struct project_transmission_helper<proj_case::sender,
                                    From, To, Role, P, G> {
    using type = Send<P, typename Project<G, Role>::type>;
};

template <typename From, typename To, typename Role, typename P, typename G>
struct project_transmission_helper<proj_case::receiver,
                                    From, To, Role, P, G> {
    using type = Recv<P, typename Project<G, Role>::type>;
};

template <typename From, typename To, typename Role, typename P, typename G>
struct project_transmission_helper<proj_case::third_party,
                                    From, To, Role, P, G> {
    // Role is not involved — skip the event, project the continuation.
    using type = typename Project<G, Role>::type;
};

}  // namespace detail::global

template <typename From, typename To, typename P, typename G, typename Role>
struct Project<Transmission<From, To, P, G>, Role> {
    using type = typename detail::global::project_transmission_helper<
        detail::global::project_case_v<Role, From, To>,
        From, To, Role, P, G
    >::type;
};

// ─── Choice projection: three-case dispatch ──────────────────────

namespace detail::global {

template <proj_case C, typename From, typename To, typename Role,
          typename... Bs>
struct project_choice_helper;

template <typename From, typename To, typename Role, typename... Bs>
struct project_choice_helper<proj_case::sender, From, To, Role, Bs...> {
    using type = Select<
        Send<typename Bs::payload,
             typename Project<typename Bs::next, Role>::type>...>;
};

template <typename From, typename To, typename Role, typename... Bs>
struct project_choice_helper<proj_case::receiver, From, To, Role, Bs...> {
    using type = Offer<
        Recv<typename Bs::payload,
             typename Project<typename Bs::next, Role>::type>...>;
};

template <typename From, typename To, typename Role, typename... Bs>
struct project_choice_helper<proj_case::third_party,
                              From, To, Role, Bs...> {
    // Third-party: merge branch projections (plain merge; full merge
    // deferred).
    using type = plain_merge_t<
        typename Project<typename Bs::next, Role>::type...>;
};

}  // namespace detail::global

template <typename From, typename To, typename... Bs, typename Role>
struct Project<Choice<From, To, Bs...>, Role> {
    using type = typename detail::global::project_choice_helper<
        detail::global::project_case_v<Role, From, To>,
        From, To, Role, Bs...
    >::type;
};

// Public projection alias.
template <typename G, typename Role>
using project_t = typename Project<G, Role>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::global::global_self_test {

// Fixture role tags.
struct Alice {};
struct Bob   {};
struct Carol {};

// Fixture payloads.
struct Ping {};
struct Pong {};
struct Ack  {};
struct Query {};
struct Reply {};

// ─── Shape traits ─────────────────────────────────────────────────

static_assert( is_end_g_v<End_G>);
static_assert(!is_end_g_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert( is_transmission_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(!is_transmission_v<End_G>);
static_assert( is_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);
static_assert( is_rec_g_v<Rec_G<End_G>>);
static_assert( is_var_g_v<Var_G>);
static_assert( is_stop_g_v<StopG<Alice>>);

// ─── RoleList operations ──────────────────────────────────────────

using RL_AB  = RoleList<Alice, Bob>;
using RL_BC  = RoleList<Bob, Carol>;
using RL_ABC = RoleList<Alice, Bob, Carol>;

static_assert(EmptyRoleList::size == 0);
static_assert(RL_AB::size  == 2);
static_assert(RL_ABC::size == 3);

static_assert(contains_role_v<Alice, RL_AB>);
static_assert(contains_role_v<Bob,   RL_AB>);
static_assert(!contains_role_v<Carol, RL_AB>);

// insert_unique: new element prepends.
static_assert(std::is_same_v<
    insert_unique_t<Carol, RL_AB>,
    RoleList<Carol, Alice, Bob>>);

// insert_unique: existing element is idempotent.
static_assert(std::is_same_v<
    insert_unique_t<Alice, RL_AB>,
    RL_AB>);

// union_roles: merges without duplicates.
using Merged = union_roles_t<RL_AB, RL_BC>;
static_assert(Merged::size == 3);
static_assert(contains_role_v<Alice, Merged>);
static_assert(contains_role_v<Bob,   Merged>);
static_assert(contains_role_v<Carol, Merged>);

// union with empty is identity.
static_assert(std::is_same_v<
    union_roles_t<RL_AB, EmptyRoleList>, RL_AB>);

// ─── RolesOf ──────────────────────────────────────────────────────

using G_binary = Transmission<Alice, Bob, Ping, End_G>;
using G_ternary = Transmission<Alice, Bob, Ping,
                   Transmission<Bob, Carol, Pong, End_G>>;

static_assert(roles_of_t<End_G>::size == 0);

// A binary transmission has two roles.
static_assert(contains_role_v<Alice, roles_of_t<G_binary>>);
static_assert(contains_role_v<Bob,   roles_of_t<G_binary>>);
static_assert(!contains_role_v<Carol, roles_of_t<G_binary>>);
static_assert(roles_of_t<G_binary>::size == 2);

// A ternary chain has three roles.
static_assert(contains_role_v<Alice, roles_of_t<G_ternary>>);
static_assert(contains_role_v<Bob,   roles_of_t<G_ternary>>);
static_assert(contains_role_v<Carol, roles_of_t<G_ternary>>);
static_assert(roles_of_t<G_ternary>::size == 3);

// StopG<Peer> contributes Peer.
static_assert(contains_role_v<Alice, roles_of_t<StopG<Alice>>>);
static_assert(!contains_role_v<Bob, roles_of_t<StopG<Alice>>>);

// ─── Well-formedness ──────────────────────────────────────────────

static_assert(is_global_well_formed_v<End_G>);
static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(is_global_well_formed_v<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>);
static_assert(is_global_well_formed_v<Rec_G<Choice<Alice, Bob,
    BranchG<Ping, Var_G>,
    BranchG<Ack,  End_G>>>>);

// Var_G outside Rec_G — ill-formed.
static_assert(!is_global_well_formed_v<Var_G>);
static_assert(!is_global_well_formed_v<Transmission<Alice, Bob, Ping, Var_G>>);
static_assert(!is_global_well_formed_v<Choice<Alice, Bob,
    BranchG<Ping, Var_G>>>);

// Nested Rec_G: inner Var_G binds to innermost (well-formed).
static_assert(is_global_well_formed_v<Rec_G<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>>);

// StopG is well-formed in any context.
static_assert(is_global_well_formed_v<StopG<Alice>>);
static_assert(is_global_well_formed_v<Transmission<Alice, Bob, Ping, StopG<Alice>>>);

// ─── Self-transmission rejection (#363) ────────────────────────────
//
// Transmission<X, X, ...> and Choice<X, X, ...> are nonsensical —
// a participant cannot send to itself in MPST.  is_global_well_formed_v
// rejects; has_self_loop_v identifies the case explicitly.

// Self-Transmission rejected.
static_assert(!is_global_well_formed_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert(!is_global_well_formed_v<Transmission<Bob,   Bob,   Ack,  End_G>>);

// Self-Choice rejected.
static_assert(!is_global_well_formed_v<Choice<Alice, Alice,
    BranchG<Ping, End_G>>>);
static_assert(!is_global_well_formed_v<Choice<Bob, Bob,
    BranchG<Ping, End_G>,
    BranchG<Ack,  End_G>>>);

// Cross-role transmissions still well-formed (positive control).
static_assert( is_global_well_formed_v<Transmission<Alice, Bob,   Ping, End_G>>);
static_assert( is_global_well_formed_v<Transmission<Bob,   Alice, Ack,  End_G>>);

// Self-loop NESTED inside a well-formed prefix is also rejected
// (the non-self-loop prefix doesn't redeem the inner self-loop).
static_assert(!is_global_well_formed_v<Transmission<Alice, Bob, Ping,
    Transmission<Alice, Alice, Ack, End_G>>>);

// Same nested check for Choice.
static_assert(!is_global_well_formed_v<Choice<Alice, Bob,
    BranchG<Ping, End_G>,
    BranchG<Ack,  Transmission<Bob, Bob, Ping, End_G>>>>);

// Inside Rec_G: the recursion variable is fine; the self-loop is
// what makes it ill-formed.
static_assert( is_global_well_formed_v<
    Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>);
static_assert(!is_global_well_formed_v<
    Rec_G<Transmission<Alice, Alice, Ping, Var_G>>>);

// has_self_loop_v identifies self-loops independently of WF status.
static_assert(!has_self_loop_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert( has_self_loop_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert( has_self_loop_v<Choice<Alice, Alice, BranchG<Ping, End_G>>>);

// Self-loop nested anywhere in the tree.
static_assert( has_self_loop_v<Transmission<Alice, Bob, Ping,
    Transmission<Alice, Alice, Ack, End_G>>>);
static_assert( has_self_loop_v<Choice<Alice, Bob,
    BranchG<Ping, End_G>,
    BranchG<Ack,  Transmission<Bob, Bob, Ping, End_G>>>>);

// has_self_loop_v on End_G / Var_G / StopG is false.
static_assert(!has_self_loop_v<End_G>);
static_assert(!has_self_loop_v<Var_G>);
static_assert(!has_self_loop_v<StopG<Alice>>);

// assert_no_self_loop<G>() is consteval and compiles for clean Gs.
consteval bool check_assert_no_self_loop_compiles() {
    assert_no_self_loop<End_G>();
    assert_no_self_loop<Transmission<Alice, Bob, Ping, End_G>>();
    assert_no_self_loop<Rec_G<Transmission<Alice, Bob, Ping, Var_G>>>();
    return true;
}
static_assert(check_assert_no_self_loop_compiles());

// ─── plain_merge_t ─────────────────────────────────────────────────

static_assert(std::is_same_v<plain_merge_t<>, End>);  // vacuous
static_assert(std::is_same_v<plain_merge_t<End>, End>);
static_assert(std::is_same_v<plain_merge_t<End, End, End>, End>);
static_assert(std::is_same_v<
    plain_merge_t<Send<int, End>, Send<int, End>>,
    Send<int, End>>);

// ─── Projection: End_G ─────────────────────────────────────────────

static_assert(std::is_same_v<project_t<End_G, Alice>, End>);
static_assert(std::is_same_v<project_t<End_G, Bob>,   End>);

// ─── Projection: Transmission (binary) ────────────────────────────

using G_AB = Transmission<Alice, Bob, Ping, End_G>;

// Sender: Send<P, End>.
static_assert(std::is_same_v<
    project_t<G_AB, Alice>,
    Send<Ping, End>>);

// Receiver: Recv<P, End>.
static_assert(std::is_same_v<
    project_t<G_AB, Bob>,
    Recv<Ping, End>>);

// ─── Projection: three-party Transmission chain ──────────────────

// Alice → Bob, then Bob → Carol.  Each role sees only events they
// participate in; events between other roles are skipped.

using G_chain = Transmission<Alice, Bob, Ping,
                Transmission<Bob, Carol, Pong, End_G>>;

// Alice: sends Ping, then skips Bob→Carol.
static_assert(std::is_same_v<
    project_t<G_chain, Alice>,
    Send<Ping, End>>);

// Bob: recvs Ping (from Alice), then sends Pong (to Carol).
static_assert(std::is_same_v<
    project_t<G_chain, Bob>,
    Recv<Ping, Send<Pong, End>>>);

// Carol: skips Alice→Bob, then recvs Pong.
static_assert(std::is_same_v<
    project_t<G_chain, Carol>,
    Recv<Pong, End>>);

// ─── Projection: Choice ────────────────────────────────────────────

// Binary request-response with loop: Alice sends either Query
// or close, Bob offers both.
using G_reqresp = Rec_G<Choice<Alice, Bob,
    BranchG<Query, Transmission<Bob, Alice, Reply, Var_G>>,
    BranchG<Ack,   End_G>>>;

// Alice's projection: Loop<Select<Send<Query, Recv<Reply, Continue>>,
//                                  Send<Ack, End>>>.
static_assert(std::is_same_v<
    project_t<G_reqresp, Alice>,
    Loop<Select<
        Send<Query, Recv<Reply, Continue>>,
        Send<Ack,   End>>>>);

// Bob's projection: the dual shape — Offer over Recv.
static_assert(std::is_same_v<
    project_t<G_reqresp, Bob>,
    Loop<Offer<
        Recv<Query, Send<Reply, Continue>>,
        Recv<Ack,   End>>>>);

// And the duals match.
static_assert(std::is_same_v<
    dual_of_t<project_t<G_reqresp, Alice>>,
    project_t<G_reqresp, Bob>>);

// ─── Projection: StopG ─────────────────────────────────────────────

using G_alice_stops = Transmission<Alice, Bob, Ping, StopG<Alice>>;

// Alice: Send<Ping, Stop>.
static_assert(std::is_same_v<
    project_t<G_alice_stops, Alice>,
    Send<Ping, Stop>>);

// Bob: Recv<Ping, End> (Alice crashed after sending; Bob's protocol
// ends).
static_assert(std::is_same_v<
    project_t<G_alice_stops, Bob>,
    Recv<Ping, End>>);

// Stop and End are distinct (Stop is bottom of subtype order).
static_assert(!std::is_same_v<End, Stop>);

// ─── Projection: Var_G / Rec_G ─────────────────────────────────────

// A simple loop: Alice → Bob forever.
using G_forever = Rec_G<Transmission<Alice, Bob, Ping, Var_G>>;

static_assert(std::is_same_v<
    project_t<G_forever, Alice>,
    Loop<Send<Ping, Continue>>>);

static_assert(std::is_same_v<
    project_t<G_forever, Bob>,
    Loop<Recv<Ping, Continue>>>);

// ─── Projection: third-party with plain merge ─────────────────────
//
// A Choice where the third party's projection matches across branches:
// both branches lead to the same continuation for Carol, so plain
// merge succeeds.
//
// Alice chooses between Ping-to-Bob or Ack-to-Bob, but AFTER the
// choice, both branches have Bob→Carol send Reply.
//
// Carol sees the same Recv<Reply, End> in both branches — plain
// merge yields Recv<Reply, End>.

using G_merged = Choice<Alice, Bob,
    BranchG<Ping, Transmission<Bob, Carol, Reply, End_G>>,
    BranchG<Ack,  Transmission<Bob, Carol, Reply, End_G>>>;

// Carol's projection is the merge of both branches (both the same).
static_assert(std::is_same_v<
    project_t<G_merged, Carol>,
    Recv<Reply, End>>);

// Alice (sender) and Bob (receiver) still see Select/Offer.
static_assert(std::is_same_v<
    project_t<G_merged, Alice>,
    Select<
        Send<Ping, End>,
        Send<Ack,  End>>>);

static_assert(std::is_same_v<
    project_t<G_merged, Bob>,
    Offer<
        Recv<Ping, Send<Reply, End>>,
        Recv<Ack,  Send<Reply, End>>>>);

// ─── Dual preservation: peer projection is dual of my projection ──
//
// For every binary global type G involving Alice and Bob:
//     dual(project_t<G, Alice>) == project_t<G, Bob>
//
// This is the "duality preservation under projection" theorem for
// binary MPST (degenerate — classical binary Honda 1998 subsumed).

static_assert(std::is_same_v<
    dual_of_t<project_t<G_AB, Alice>>,
    project_t<G_AB, Bob>>);

static_assert(std::is_same_v<
    dual_of_t<project_t<G_forever, Alice>>,
    project_t<G_forever, Bob>>);

// ─── Patterns built via projection match hand-written patterns ────
//
// A request-response global type's projection onto Alice produces the
// same session type as the hand-written RequestResponse_Client pattern
// from SessionPatterns.h (modulo the in-band close branch).  This
// verifies the framework is consistent.

using G_rrloop = Rec_G<Transmission<Alice, Bob, Query,
                        Transmission<Bob, Alice, Reply, Var_G>>>;

static_assert(std::is_same_v<
    project_t<G_rrloop, Alice>,
    Loop<Send<Query, Recv<Reply, Continue>>>>);  // = RequestResponse_Client<Query, Reply>

static_assert(std::is_same_v<
    project_t<G_rrloop, Bob>,
    Loop<Recv<Query, Send<Reply, Continue>>>>);  // = RequestResponse_Server<Query, Reply>

}  // namespace detail::global::global_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
