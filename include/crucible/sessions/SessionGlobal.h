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
// ── has_interaction_between_v<G, RoleA, RoleB>   (GAPS-001) ────────
// ═════════════════════════════════════════════════════════════════════
//
// Does G contain ANY Transmission<F, T, P, K> or Choice<F, T, ...> at
// any depth where {F, T} == {RoleA, RoleB} (unordered)?  Walks the
// global type tree recursively.
//
// Used by the StopG<Peer> projection rule for Role ≠ Peer to decide
// whether Role is affected by Peer's crash:
//
//   * Role HAS interactions with Peer in G → projection ends in Stop
//                                            (BHYZ23: surviving roles
//                                            see crash-induced
//                                            termination)
//   * Role has NO interactions with Peer in G → projection ends in End
//                                               (Role is a non-
//                                               participant w.r.t.
//                                               this peer; clean
//                                               termination)
//
// The walker is symmetric over (RoleA, RoleB): a Transmission<A, B,...>
// matches whether the query is (A, B) or (B, A).
//
// Cost: O(N) instantiations on the size of G's tree, identical to the
// existing flat_roles_t walker.

namespace detail::global {

// `same_unordered_pair_v<F, T, A, B>` — true iff the multiset {F, T}
// equals the multiset {A, B}.  Pulled out as a single point of truth
// for the symmetric-pair check used in every Transmission/Choice
// specialisation of `has_interaction_between`.  A typo in the
// expansion of this predicate would silently break crash-safety
// detection for one direction of the pair (e.g. (Bob, Alice) but not
// (Alice, Bob)), and the bug would never reach a static_assert
// because `has_interaction_between` returns its WRONG value rather
// than failing to compile.  Single-point-of-truth makes that class of
// bug review-detectable instead of silent.
template <typename F, typename T, typename A, typename B>
inline constexpr bool same_unordered_pair_v =
       (std::is_same_v<F, A> && std::is_same_v<T, B>)
    || (std::is_same_v<F, B> && std::is_same_v<T, A>);

template <typename G, typename RoleA, typename RoleB>
struct has_interaction_between : std::false_type {};

// Terminals contribute no interactions.
template <typename A, typename B>
struct has_interaction_between<End_G, A, B> : std::false_type {};

template <typename A, typename B>
struct has_interaction_between<Var_G, A, B> : std::false_type {};

template <typename Peer, typename A, typename B>
struct has_interaction_between<StopG<Peer>, A, B> : std::false_type {};

// Transmission: matches if {From, To} == {A, B}; recurse into
// continuation otherwise.
template <typename From, typename To, typename P, typename N,
          typename A, typename B>
struct has_interaction_between<Transmission<From, To, P, N>, A, B>
    : std::bool_constant<
             same_unordered_pair_v<From, To, A, B>
          || has_interaction_between<N, A, B>::value
      > {};

// Choice: matches if {From, To} == {A, B}; recurse into all branches'
// continuations otherwise.
template <typename From, typename To, typename... Bs,
          typename A, typename B>
struct has_interaction_between<Choice<From, To, Bs...>, A, B>
    : std::bool_constant<
             same_unordered_pair_v<From, To, A, B>
          || (has_interaction_between<typename Bs::next, A, B>::value || ...)
      > {};

// Rec_G recurses into Body.  Var_G inside Body terminates the walk
// (handled by Var_G specialisation above) — the loop body is finite
// type-tree even if its runtime semantics is unbounded.
template <typename Body, typename A, typename B>
struct has_interaction_between<Rec_G<Body>, A, B>
    : has_interaction_between<Body, A, B> {};

template <typename G, typename A, typename B>
inline constexpr bool has_interaction_between_v =
    has_interaction_between<G, A, B>::value;

}  // namespace detail::global

// ═════════════════════════════════════════════════════════════════════
// ── Project<G, Role> — projection metafunction ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Projection is implemented as the public alias `project_t<G, Role>`
// (and `Project<G, Role>::type`) over the internal RootG-aware
// `ProjectImpl<G, Role, RootG>`.
//
// RootG is the OUTERMOST global type at the start of the walk —
// available everywhere through the recursion via parameter threading.
// Most projection rules ignore RootG (they recurse with the same
// RootG, and that's a no-op in their body).  The single rule that
// CONSULTS RootG is the StopG<Peer> non-Peer case, which queries
// `has_interaction_between_v<RootG, Role, Peer>` to decide between
// Stop (Role had pending business with the crashed Peer) and End
// (Role is a non-participant and gets a clean termination).
//
// Per-rule projection (BHYZ23 / BSYZ22 semantics):
//
//   End_G                    ↾ R = End
//   Var_G                    ↾ R = Continue
//   Rec_G<Body>              ↾ R = Loop<Body↾R>
//   StopG<Peer>              ↾ Peer = Stop
//   StopG<Peer>              ↾ R    = Stop  (when R interacts with
//                                            Peer in RootG, BHYZ23
//                                            crash propagation)
//                                  = End   (when R has no interactions
//                                           with Peer in RootG)
//   Transmission<F, T, P, G> ↾ F = Send<P, G↾F>
//   Transmission<F, T, P, G> ↾ T = Recv<P, G↾T>
//   Transmission<F, T, P, G> ↾ R = G↾R                  (third-party)
//   Choice<F, T, B...>       ↾ F = Select<Send<B.p, B.g↾F>...>
//   Choice<F, T, B...>       ↾ T = Offer<Recv<B.p, B.g↾T>...>
//   Choice<F, T, B...>       ↾ R = plain_merge_t<B.g↾R...>  (third)

namespace detail::global {

template <typename G, typename Role, typename RootG>
struct ProjectImpl;

// End_G projects to End for every role.
template <typename Role, typename RootG>
struct ProjectImpl<End_G, Role, RootG> { using type = End; };

// Var_G projects to Continue (framework enforces Var_G inside Rec_G).
template <typename Role, typename RootG>
struct ProjectImpl<Var_G, Role, RootG> { using type = Continue; };

// Rec_G<Body> projects to Loop<Body↾R>.  RootG threads unchanged.
template <typename Body, typename Role, typename RootG>
struct ProjectImpl<Rec_G<Body>, Role, RootG> {
    using type = Loop<typename ProjectImpl<Body, Role, RootG>::type>;
};

// StopG<Peer> — peer (the crashed role) sees Stop.  Per BSYZ22 §3:
// the crashed participant's local protocol transitions to Stop.
template <typename Peer, typename RootG>
struct ProjectImpl<StopG<Peer>, Peer, RootG> { using type = Stop; };

// StopG<Peer> — non-Peer role.  Per BHYZ23 / BSYZ22 [GR-✂] crash
// propagation: surviving roles whose protocol intersects with Peer's
// see crash-induced termination (Stop), not clean termination (End).
//
// Decision rule (GAPS-001 fix): walk RootG; if Role has any
// interaction with Peer anywhere in the protocol, project to Stop.
// Otherwise, Role is a non-participant w.r.t. Peer and projects to End
// (the role is unaffected by Peer's crash).
//
// Why this matters: previously the rule unconditionally returned End,
// which encoded "Role's session terminated cleanly" regardless of the
// crash event — losing crash-safety information silently.  Stop ⩽ T
// (subtype lattice bottom, per SessionCrash.h:205) means changing End
// to Stop is a *tightening* of the protocol, not a loosening: any
// implementation that handled End correctly handles Stop correctly,
// but an implementation that needs to dispatch crash recovery now
// gets the type-level signal to do so.
template <typename Peer, typename Role, typename RootG>
struct ProjectImpl<StopG<Peer>, Role, RootG> {
    using type = std::conditional_t<
        has_interaction_between_v<RootG, Role, Peer>,
        Stop,
        End>;
};

// ─── Transmission projection: three-case dispatch ────────────────

enum class proj_case { sender, receiver, third_party };

template <typename Role, typename From, typename To>
inline constexpr proj_case project_case_v =
      std::is_same_v<Role, From> ? proj_case::sender
    : std::is_same_v<Role, To>   ? proj_case::receiver
    :                              proj_case::third_party;

template <proj_case C, typename From, typename To, typename Role,
          typename P, typename G, typename RootG>
struct project_transmission_helper;

template <typename From, typename To, typename Role, typename P,
          typename G, typename RootG>
struct project_transmission_helper<proj_case::sender,
                                    From, To, Role, P, G, RootG> {
    using type = Send<P, typename ProjectImpl<G, Role, RootG>::type>;
};

template <typename From, typename To, typename Role, typename P,
          typename G, typename RootG>
struct project_transmission_helper<proj_case::receiver,
                                    From, To, Role, P, G, RootG> {
    using type = Recv<P, typename ProjectImpl<G, Role, RootG>::type>;
};

template <typename From, typename To, typename Role, typename P,
          typename G, typename RootG>
struct project_transmission_helper<proj_case::third_party,
                                    From, To, Role, P, G, RootG> {
    // Role is not involved — skip the event, project the continuation.
    using type = typename ProjectImpl<G, Role, RootG>::type;
};

template <typename From, typename To, typename P, typename G,
          typename Role, typename RootG>
struct ProjectImpl<Transmission<From, To, P, G>, Role, RootG> {
    using type = typename project_transmission_helper<
        project_case_v<Role, From, To>,
        From, To, Role, P, G, RootG
    >::type;
};

// ─── Choice projection: three-case dispatch ──────────────────────

template <proj_case C, typename From, typename To, typename Role,
          typename RootG, typename... Bs>
struct project_choice_helper;

template <typename From, typename To, typename Role, typename RootG,
          typename... Bs>
struct project_choice_helper<proj_case::sender, From, To, Role,
                              RootG, Bs...> {
    using type = Select<
        Send<typename Bs::payload,
             typename ProjectImpl<typename Bs::next, Role, RootG>::type>...>;
};

template <typename From, typename To, typename Role, typename RootG,
          typename... Bs>
struct project_choice_helper<proj_case::receiver, From, To, Role,
                              RootG, Bs...> {
    using type = Offer<
        Recv<typename Bs::payload,
             typename ProjectImpl<typename Bs::next, Role, RootG>::type>...>;
};

template <typename From, typename To, typename Role, typename RootG,
          typename... Bs>
struct project_choice_helper<proj_case::third_party, From, To, Role,
                              RootG, Bs...> {
    // Third-party: merge branch projections (plain merge; full merge
    // deferred — see plain_merge_t docstring).
    using type = plain_merge_t<
        typename ProjectImpl<typename Bs::next, Role, RootG>::type...>;
};

template <typename From, typename To, typename... Bs,
          typename Role, typename RootG>
struct ProjectImpl<Choice<From, To, Bs...>, Role, RootG> {
    using type = typename project_choice_helper<
        project_case_v<Role, From, To>,
        From, To, Role, RootG, Bs...
    >::type;
};

}  // namespace detail::global

// Public Project<G, Role>: forwards to ProjectImpl<G, Role, RootG=G>
// so the OUTERMOST G is the RootG threaded through the entire walk.
// Specialisations of Project may be added by callers that want a
// different RootG (rare; typically only useful when projecting a
// sub-tree of a larger protocol).

template <typename G, typename Role>
struct Project {
    using type = typename detail::global::ProjectImpl<G, Role, G>::type;
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

// ─── Projection: StopG (GAPS-001 — crash-safety preservation) ─────
//
// BHYZ23 / BSYZ22 [GR-✂] crash propagation: at a StopG<Peer> node,
// every role whose protocol intersects with Peer's sees crash-induced
// termination (Stop), not clean termination (End).  The previous rule
// projected non-Peer roles to End unconditionally, losing crash-safety
// information silently.

using G_alice_stops = Transmission<Alice, Bob, Ping, StopG<Alice>>;

// Alice: Send<Ping, Stop> — she IS the crashed peer.
static_assert(std::is_same_v<
    project_t<G_alice_stops, Alice>,
    Send<Ping, Stop>>);

// Bob: Recv<Ping, Stop> — Bob INTERACTED with Alice (received Ping),
// so Bob sees crash-induced Stop, not clean End.  Tightening from
// the previous (buggy) Recv<Ping, End>: Stop ⩽ End in subtype order
// (Stop is the bottom — SessionCrash.h:205), so any code that
// correctly handled End handles Stop, but crash-recovery dispatch
// now gets the type-level signal.
static_assert(std::is_same_v<
    project_t<G_alice_stops, Bob>,
    Recv<Ping, Stop>>);

// Stop and End are distinct (Stop is bottom of subtype order).
static_assert(!std::is_same_v<End, Stop>);

// (a) Non-Peer role with PENDING SENDS to Peer — projects to Stop.
//
// Bob sends Query to Alice; Alice immediately crashes.  Bob doesn't
// know if his send arrived — he must see crash-induced termination.
using G_send_then_crash = Transmission<Bob, Alice, Query, StopG<Alice>>;
static_assert(std::is_same_v<
    project_t<G_send_then_crash, Bob>,
    Send<Query, Stop>>);
static_assert(std::is_same_v<
    project_t<G_send_then_crash, Alice>,
    Recv<Query, Stop>>);

// (b) Non-Peer role with NO INTERACTIONS with Peer — projects to End.
//
// Carol is a third party who never interacts with Alice in the
// protocol; Alice's crash doesn't affect Carol's session, which can
// terminate cleanly.
using G_carol_unaffected = Transmission<Bob, Carol, Pong,
                            Transmission<Alice, Bob, Ping,
                              StopG<Alice>>>;
// Carol projects to: Recv<Pong, End> — Carol's only interaction is
// with Bob; Alice's crash propagates only to Bob (who interacts with
// Alice), not to Carol (who doesn't).
static_assert(std::is_same_v<
    project_t<G_carol_unaffected, Carol>,
    Recv<Pong, End>>);
// Bob still sees Stop (he interacts with Alice afterwards).
static_assert(std::is_same_v<
    project_t<G_carol_unaffected, Bob>,
    Send<Pong, Recv<Ping, Stop>>>);

// (c) Round-trip Γ → G → projection → Γ' preserves crash-safety:
// every projection that ends at a StopG<Peer> for a role R that
// interacts with Peer contains Stop somewhere in its tree.  Verified
// via the structural witness: project Bob from G_alice_stops and
// confirm the leaf is Stop, not End.  Symmetric round-trip test:
// Alice's projection contains Stop too.
//
// The aggregate: there exists no role R interacting with Alice in
// G_alice_stops whose projection terminates in End.

// Helper: structural test that a projected protocol contains Stop at
// its terminal position.
template <typename Proto>
struct ends_in_stop_recursive : std::false_type {};

template <>
struct ends_in_stop_recursive<Stop> : std::true_type {};

template <typename Payload, typename Continuation>
struct ends_in_stop_recursive<Send<Payload, Continuation>>
    : ends_in_stop_recursive<Continuation> {};

template <typename Payload, typename Continuation>
struct ends_in_stop_recursive<Recv<Payload, Continuation>>
    : ends_in_stop_recursive<Continuation> {};

template <typename Body>
struct ends_in_stop_recursive<Loop<Body>>
    : ends_in_stop_recursive<Body> {};

template <typename Proto>
inline constexpr bool ends_in_stop_v = ends_in_stop_recursive<Proto>::value;

// Round-trip witness: every role that interacts with Alice in
// G_alice_stops has a Stop terminal in its projection.
static_assert(ends_in_stop_v<project_t<G_alice_stops, Alice>>);
static_assert(ends_in_stop_v<project_t<G_alice_stops, Bob>>);
// Carol (uninvolved in G_alice_stops) projects to End.
static_assert(std::is_same_v<project_t<G_alice_stops, Carol>, End>);

// has_interaction_between_v witness — the helper the fix is built on.
static_assert( has_interaction_between_v<G_alice_stops, Alice, Bob>);
static_assert( has_interaction_between_v<G_alice_stops, Bob,   Alice>);  // symmetric
static_assert(!has_interaction_between_v<G_alice_stops, Carol, Alice>);
static_assert(!has_interaction_between_v<G_alice_stops, Carol, Bob>);
static_assert( has_interaction_between_v<G_carol_unaffected, Bob, Carol>);
static_assert( has_interaction_between_v<G_carol_unaffected, Alice, Bob>);
static_assert(!has_interaction_between_v<G_carol_unaffected, Carol, Alice>);

// ═════════════════════════════════════════════════════════════════════
// ── Multiparty (MPST) crash-propagation fixtures (GAPS-001) ────────
// ═════════════════════════════════════════════════════════════════════
//
// The 2-party Alice/Bob/Carol fixtures above pin the basic rule.  Real
// MPST protocols have N participants, broadcast, fan-in, fan-out,
// diamonds, chains, and recursion — each interaction pattern produces
// a different has_interaction_between answer.  The fixtures below
// stress every shape with 5+ roles.

struct David {};
struct Eve   {};
struct Frank {};
struct Grace {};

struct Cmd   {};   // broadcast/command payload
struct Beat  {};   // heartbeat / link-step payload
struct Vote  {};   // voting payload
struct Sum   {};   // aggregation payload
struct Close {};   // shut-down marker

// ─── (1) FIVE-PARTY BROADCAST + COORDINATOR CRASH ────────────────
//
// Coordinator Alice broadcasts Cmd to {Bob, Carol, David, Eve}
// sequentially, then crashes (StopG<Alice>).  Every Di interacted
// with Alice (received Cmd) → all see Stop.  Bystander Frank never
// appears → End.

using G_broadcast_crash =
    Transmission<Alice, Bob,   Cmd,
    Transmission<Alice, Carol, Cmd,
    Transmission<Alice, David, Cmd,
    Transmission<Alice, Eve,   Cmd,
      StopG<Alice>>>>>;

static_assert(is_global_well_formed_v<G_broadcast_crash>);
static_assert(roles_of_t<G_broadcast_crash>::size == 5);

// Alice (the crashed coordinator) emits 4 Sends then Stop.
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, Alice>,
    Send<Cmd, Send<Cmd, Send<Cmd, Send<Cmd, Stop>>>>>);

// Each downstream Di sees Recv<Cmd, Stop>.  Their projection skips
// the broadcasts to OTHER recipients (third-party events) and lands
// at StopG<Alice>, which projects to Stop (each Di interacted with
// Alice).
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, Bob>,    Recv<Cmd, Stop>>);
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, Carol>,  Recv<Cmd, Stop>>);
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, David>,  Recv<Cmd, Stop>>);
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, Eve>,    Recv<Cmd, Stop>>);

// Bystander Frank — never appears — projects to End.
static_assert(std::is_same_v<
    project_t<G_broadcast_crash, Frank>, End>);

// Pairwise interactions: every Di interacts with Alice; no Di
// interacts with another Di.
static_assert( has_interaction_between_v<G_broadcast_crash, Alice, Bob>);
static_assert( has_interaction_between_v<G_broadcast_crash, Alice, Eve>);
static_assert(!has_interaction_between_v<G_broadcast_crash, Bob,   Carol>);
static_assert(!has_interaction_between_v<G_broadcast_crash, David, Eve>);
static_assert(!has_interaction_between_v<G_broadcast_crash, Frank, Alice>);

// ─── (2) FAN-IN AGGREGATION + AGGREGATOR CRASH ───────────────────
//
// Three voters (Bob, Carol, David) each send Vote to Aggregator Eve,
// then Eve crashes.  All three voters interacted with Eve → all see
// Stop in their tail.  Eve sees three Recvs then Stop.  Bystander
// Frank never participates → End.

using G_fan_in_crash =
    Transmission<Bob,   Eve, Vote,
    Transmission<Carol, Eve, Vote,
    Transmission<David, Eve, Vote,
      StopG<Eve>>>>;

static_assert(is_global_well_formed_v<G_fan_in_crash>);
static_assert(roles_of_t<G_fan_in_crash>::size == 4);

// Eve (the crashed aggregator) receives 3 Votes then Stop.
static_assert(std::is_same_v<
    project_t<G_fan_in_crash, Eve>,
    Recv<Vote, Recv<Vote, Recv<Vote, Stop>>>>);

// Each voter sends its Vote, then sees Stop because voter interacts
// with Eve directly.  The intermediate Vote events (between OTHER
// voters and Eve) are third-party from this voter's perspective and
// collapse to the StopG continuation's projection.
static_assert(std::is_same_v<
    project_t<G_fan_in_crash, Bob>,   Send<Vote, Stop>>);
static_assert(std::is_same_v<
    project_t<G_fan_in_crash, Carol>, Send<Vote, Stop>>);
static_assert(std::is_same_v<
    project_t<G_fan_in_crash, David>, Send<Vote, Stop>>);

// Frank never appears.
static_assert(std::is_same_v<
    project_t<G_fan_in_crash, Frank>, End>);

// ─── (3) LONG CHAIN — TRANSITIVE INFLUENCE BOUNDARY ──────────────
//
// Pipeline: Alice → Bob → Carol → David → Eve, then Eve crashes.
// Only David has a DIRECT interaction with Eve in the protocol tree
// (David sends Beat to Eve right before the crash).  Alice/Bob/Carol
// are upstream of Eve through transitive links but have no direct
// pairwise interaction in the global type.
//
// has_interaction_between checks DIRECT pair-interactions, NOT
// transitive closure — surviving roles see Stop only when they
// directly interacted with the crashed peer, which matches BHYZ23's
// local crash-detection semantics: each role detects "the peer I
// was talking to crashed", not "someone else's peer crashed
// somewhere upstream of me."
//
// A transitive variant (any role causally upstream of Peer) would
// mark Alice/Bob/Carol as Stop too — but then the type system would
// over-report crashes (every node in a long chain would see Stop on
// any participant's failure, even tangential ones), and would
// require costly causality analysis.  The DIRECT-only rule keeps
// the machinery compositional.

using G_long_chain =
    Transmission<Alice, Bob,   Beat,
    Transmission<Bob,   Carol, Beat,
    Transmission<Carol, David, Beat,
    Transmission<David, Eve,   Beat,
      StopG<Eve>>>>>;

static_assert(is_global_well_formed_v<G_long_chain>);
static_assert(roles_of_t<G_long_chain>::size == 5);

// Eve (peer) — receives the last Beat then Stop.
static_assert(std::is_same_v<
    project_t<G_long_chain, Eve>,
    Recv<Beat, Stop>>);

// David — recv from Carol, send to Eve, then Stop (direct interaction
// with Eve).
static_assert(std::is_same_v<
    project_t<G_long_chain, David>,
    Recv<Beat, Send<Beat, Stop>>>);

// Carol — recv from Bob, send to David, then End (no DIRECT
// interaction with Eve in the tree).
static_assert(std::is_same_v<
    project_t<G_long_chain, Carol>,
    Recv<Beat, Send<Beat, End>>>);

// Bob — recv from Alice, send to Carol, then End.
static_assert(std::is_same_v<
    project_t<G_long_chain, Bob>,
    Recv<Beat, Send<Beat, End>>>);

// Alice — initiates with one Send, then End (no chain-tail Stop).
static_assert(std::is_same_v<
    project_t<G_long_chain, Alice>,
    Send<Beat, End>>);

// Interaction matrix: only adjacent pairs.
static_assert( has_interaction_between_v<G_long_chain, David, Eve>);
static_assert( has_interaction_between_v<G_long_chain, Eve,   David>);    // symmetric
static_assert(!has_interaction_between_v<G_long_chain, Carol, Eve>);      // not direct
static_assert(!has_interaction_between_v<G_long_chain, Alice, Eve>);      // not direct
static_assert(!has_interaction_between_v<G_long_chain, Bob,   David>);    // not adjacent

// ─── (4) DIAMOND PATTERN — A→{B,C}, {B,C}→D, then D crashes ──────
//
// Alice fans out Cmd to Bob and Carol; both Bob and Carol fan in Sum
// to David; David crashes.  Bob and Carol each interact directly
// with David → both see Stop.  Alice never directly touches David
// even though her Cmds initiated the pipeline that killed him —
// Alice projects to clean End at the StopG point.

using G_diamond_crash =
    Transmission<Alice, Bob,   Cmd,
    Transmission<Alice, Carol, Cmd,
    Transmission<Bob,   David, Sum,
    Transmission<Carol, David, Sum,
      StopG<David>>>>>;

static_assert(is_global_well_formed_v<G_diamond_crash>);
static_assert(roles_of_t<G_diamond_crash>::size == 4);

// David (peer) — receives both Sums then Stop.
static_assert(std::is_same_v<
    project_t<G_diamond_crash, David>,
    Recv<Sum, Recv<Sum, Stop>>>);

// Bob — recv Cmd from Alice, send Sum to David, then Stop.
static_assert(std::is_same_v<
    project_t<G_diamond_crash, Bob>,
    Recv<Cmd, Send<Sum, Stop>>>);

// Carol — recv Cmd from Alice, send Sum to David, then Stop.
static_assert(std::is_same_v<
    project_t<G_diamond_crash, Carol>,
    Recv<Cmd, Send<Sum, Stop>>>);

// Alice — sends Cmd to Bob, then Cmd to Carol; never directly
// interacts with David, so projects to End at the StopG.
static_assert(std::is_same_v<
    project_t<G_diamond_crash, Alice>,
    Send<Cmd, Send<Cmd, End>>>);

// Interaction matrix: A↔B, A↔C, B↔D, C↔D — but NOT A↔D, NOT B↔C.
static_assert( has_interaction_between_v<G_diamond_crash, Alice, Bob>);
static_assert( has_interaction_between_v<G_diamond_crash, Alice, Carol>);
static_assert( has_interaction_between_v<G_diamond_crash, Bob,   David>);
static_assert( has_interaction_between_v<G_diamond_crash, Carol, David>);
static_assert(!has_interaction_between_v<G_diamond_crash, Alice, David>);
static_assert(!has_interaction_between_v<G_diamond_crash, Bob,   Carol>);

// ─── (5) RECURSIVE PUMP WITH CRASH BRANCH (Rec_G + Choice + Stop) ─
//
// Coordinator Alice loops; in each iteration Alice picks Choice
// between { keep beating (Var_G), close down with crash (StopG<Alice>) }.
// Both Alice and Bob see the crash branch reflected in their
// projection's Select / Offer tree as Send/Recv<Close, Stop>.
//
// The recursion case stresses has_interaction_between's recursion
// through Rec_G into the Choice body (so the StopG<Alice> arm's
// non-Peer rule for Bob detects that Bob does interact with Alice
// in the Beat arm of the Choice).

using G_pump_crash = Rec_G<Choice<Alice, Bob,
    BranchG<Beat,  Var_G>,
    BranchG<Close, StopG<Alice>>>>;

static_assert(is_global_well_formed_v<G_pump_crash>);

// Alice projects to Loop<Select<beat-arm, close-arm>>; the close-arm
// terminates in Stop (Alice IS the crashed peer in that arm).
static_assert(std::is_same_v<
    project_t<G_pump_crash, Alice>,
    Loop<Select<
        Send<Beat,  Continue>,
        Send<Close, Stop>>>>);

// Bob projects to the dual: Loop<Offer<Recv<Beat, Continue>,
//                                       Recv<Close, Stop>>>.  Bob
// interacts with Alice in the Beat arm, so the Close arm's StopG<Alice>
// projects to Stop for Bob (not End) — the surviving role sees the
// crash signal, ready to dispatch crash recovery on the close branch.
static_assert(std::is_same_v<
    project_t<G_pump_crash, Bob>,
    Loop<Offer<
        Recv<Beat,  Continue>,
        Recv<Close, Stop>>>>);

// Duals match: dual_of(Alice's projection) == Bob's projection.
static_assert(std::is_same_v<
    dual_of_t<project_t<G_pump_crash, Alice>>,
    project_t<G_pump_crash, Bob>>);

// has_interaction_between recurses through Rec_G → Choice.
static_assert( has_interaction_between_v<G_pump_crash, Alice, Bob>);
static_assert(!has_interaction_between_v<G_pump_crash, Alice, Carol>);

// ─── (6) MID-PROTOCOL CRASH — fan-out path interrupted by crash ──
//
// Carol→David, David→Frank, [Frank crashes].  David interacts with
// Frank directly → Stop in David's tail.  Carol's only interaction
// is with David — no direct touch of Frank — so Carol sees End.
// Grace doesn't participate at all → End.

using G_fan_out_in_crash =
    Transmission<Carol, David, Beat,
    Transmission<David, Frank, Beat,
      StopG<Frank>>>;

static_assert(is_global_well_formed_v<G_fan_out_in_crash>);
static_assert(roles_of_t<G_fan_out_in_crash>::size == 3);

// Frank (peer) — recv Beat from David then Stop.
static_assert(std::is_same_v<
    project_t<G_fan_out_in_crash, Frank>,
    Recv<Beat, Stop>>);

// David — recv from Carol, send to Frank, then Stop (direct).
static_assert(std::is_same_v<
    project_t<G_fan_out_in_crash, David>,
    Recv<Beat, Send<Beat, Stop>>>);

// Carol — send to David, then End (no direct interaction with Frank).
static_assert(std::is_same_v<
    project_t<G_fan_out_in_crash, Carol>,
    Send<Beat, End>>);

// Grace doesn't participate → End.
static_assert(std::is_same_v<
    project_t<G_fan_out_in_crash, Grace>, End>);

// ─── (7) STAR PATTERN — central hub crashes, every spoke sees Stop ─
//
// Hub Eve receives from each of {Alice, Bob, Carol, David} in turn,
// then crashes.  All four spokes interact directly with Eve →
// every spoke's projection ends in Stop.  This is the inverse shape
// of (1): everyone-talks-to-one rather than one-talks-to-everyone.

using G_star_hub_crash =
    Transmission<Alice, Eve, Vote,
    Transmission<Bob,   Eve, Vote,
    Transmission<Carol, Eve, Vote,
    Transmission<David, Eve, Vote,
      StopG<Eve>>>>>;

static_assert(is_global_well_formed_v<G_star_hub_crash>);
static_assert(roles_of_t<G_star_hub_crash>::size == 5);

// Eve (peer) — 4 recvs then Stop.
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, Eve>,
    Recv<Vote, Recv<Vote, Recv<Vote, Recv<Vote, Stop>>>>>);

// Each spoke's first event is its own Send<Vote, ...>; intermediate
// events (other spokes' Sends to Eve) are third-party and skip; the
// tail at StopG<Eve> projects to Stop (each spoke directly interacts
// with Eve).  So every spoke projects to Send<Vote, Stop>.
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, Alice>, Send<Vote, Stop>>);
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, Bob>,   Send<Vote, Stop>>);
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, Carol>, Send<Vote, Stop>>);
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, David>, Send<Vote, Stop>>);

// Frank again uninvolved.
static_assert(std::is_same_v<
    project_t<G_star_hub_crash, Frank>, End>);

// ─── (8) NESTED-CHOICE INTERACTION DETECTION (audit-round-2) ─────
//
// has_interaction_between<G, A, B> must recurse INTO Choice branches
// to find pairs nested in continuations.  A buggy walker that only
// inspects the top-level Choice's (From, To) and forgets to fold
// over branches' :: next would silently mis-classify (Bob, Carol)
// as non-interacting in this protocol — passing every other fixture
// in this file because no other fixture has the interacting pair
// nested below the top-level Choice's (F, T).

using G_nested_choice =
    Choice<Alice, Bob,
        BranchG<Cmd, Transmission<Bob, Carol, Reply, End_G>>,
        BranchG<Ack, End_G>>;

static_assert(is_global_well_formed_v<G_nested_choice>);

// Top-level pair: yes (Choice<Alice, Bob, ...>).
static_assert( has_interaction_between_v<G_nested_choice, Alice, Bob>);
static_assert( has_interaction_between_v<G_nested_choice, Bob,   Alice>);

// Nested in branch 0's continuation: Transmission<Bob, Carol, ...>.
// This is the audit-critical case — the walker must descend into
// branches' next-fields, not just inspect the Choice's (From, To).
static_assert( has_interaction_between_v<G_nested_choice, Bob,   Carol>);
static_assert( has_interaction_between_v<G_nested_choice, Carol, Bob>);    // symmetric

// (Alice, Carol) does NOT appear anywhere → false.
static_assert(!has_interaction_between_v<G_nested_choice, Alice, Carol>);

// Now exercise the StopG-projection consequence: imagine adding a
// StopG<Bob> in branch 0's tail.  Carol interacts with Bob there
// (we just confirmed it), so Carol's projection ends in Stop on the
// branch where Bob crashes.

using G_nested_choice_with_crash =
    Choice<Alice, Bob,
        BranchG<Cmd, Transmission<Bob, Carol, Reply, StopG<Bob>>>,
        BranchG<Ack, End_G>>;

static_assert(is_global_well_formed_v<G_nested_choice_with_crash>);

// Carol on branch 0: Recv<Reply, Stop>  (Carol interacts with Bob in
//   RootG, and Bob is the crashed peer).
// Carol on branch 1: End  (no interaction with Bob in this branch's
//   tail; but RootG-walk DOES find a Carol↔Bob interaction in the
//   OTHER branch, so the StopG specialisation actually projects to
//   Stop here too — RootG threading is tree-level, not path-level).
//
// Plain merge of (Recv<Reply, Stop>, ?) — we cannot get past the
// merge differing-tail conflict without committing to one of the two
// shapes.  This fixture deliberately exercises the limit and shows
// that the audit-critical query is the has_interaction_between query
// itself, not the (deferred) full-merge question.  Concrete check:
static_assert( has_interaction_between_v<G_nested_choice_with_crash,
                                          Carol, Bob>);
// Bob's projection on branch 0: Send<Reply, Stop>; on branch 1: End.
// Bob is the Choice's To role — sees Offer<...>.  We don't try to
// compute Bob's full projection here (Choice projection on the To
// role across branches with a Stop tail vs End tail diverges in
// payload — would need a separate Offer to express).  Limit
// documented; future full-merge work resolves it (see #381).

// ─── (9) Rec_G + NESTED Choice has_interaction recursion ─────────
//
// has_interaction_between must descend through Rec_G AND Choice
// AND recurse through branches' continuations to find the nested
// pair.  This wires up all three recursion arms in a single fixture.

using G_loop_with_nested_pair =
    Rec_G<Choice<Alice, Bob,
        BranchG<Beat,  Transmission<Bob, Carol, Reply, Var_G>>,
        BranchG<Close, End_G>>>;

static_assert(is_global_well_formed_v<G_loop_with_nested_pair>);

// Top-level Rec_G + Choice's (From, To): (Alice, Bob).
static_assert( has_interaction_between_v<G_loop_with_nested_pair, Alice, Bob>);
static_assert( has_interaction_between_v<G_loop_with_nested_pair, Bob,   Alice>);

// Buried in branch 0's continuation, inside Rec_G: (Bob, Carol).
// Only reached if walker recurses Rec_G → Choice → branch.next →
// Transmission.  All three layers must be wired — single bug in any
// would make this assertion fire.
static_assert( has_interaction_between_v<G_loop_with_nested_pair, Bob,   Carol>);
static_assert( has_interaction_between_v<G_loop_with_nested_pair, Carol, Bob>);

// Pairs that DON'T appear anywhere: (Alice, Carol), (David, Bob).
static_assert(!has_interaction_between_v<G_loop_with_nested_pair, Alice, Carol>);
static_assert(!has_interaction_between_v<G_loop_with_nested_pair, David, Bob>);

// ─── (10) same_unordered_pair_v helper unit-tests ────────────────

static_assert( same_unordered_pair_v<Alice, Bob, Alice, Bob>);  // identical
static_assert( same_unordered_pair_v<Alice, Bob, Bob,   Alice>); // swapped
static_assert(!same_unordered_pair_v<Alice, Bob, Alice, Carol>); // mismatch
static_assert(!same_unordered_pair_v<Alice, Bob, Carol, David>); // disjoint
// Self-pair degeneracy is rejected upstream by has_self_loop_v / WF
// gating — at the helper level, (Alice, Alice) vs (Alice, Alice) is
// trivially true.  Documented for completeness.
static_assert( same_unordered_pair_v<Alice, Alice, Alice, Alice>);

// ─── (11) Quirky fixtures (GAPS-001 audit-round-3) ───────────────

// (11a) Minimal isolated StopG<Peer> projection:  StopG<Alice> alone
// (no other transmissions at all).  The Peer-on-Peer specialisation
// must return Stop directly without consulting RootG; the non-Peer
// rule for ANY other role must return End (no interactions exist).
using G_only_stop = StopG<Alice>;
static_assert(is_global_well_formed_v<G_only_stop>);
// roles_of_t<StopG<Alice>>::size == 1 (only Alice; StopG contributes
// the Peer to the role set per `roles_of_t<StopG<P>> = {P}`).
static_assert(roles_of_t<G_only_stop>::size == 1);
static_assert( contains_role_v<Alice, roles_of_t<G_only_stop>>);
// Alice (the Peer) projects to Stop directly.
static_assert(std::is_same_v<project_t<G_only_stop, Alice>, Stop>);
// Any other role: no interaction in RootG → End.
static_assert(std::is_same_v<project_t<G_only_stop, Bob>,   End>);
static_assert(std::is_same_v<project_t<G_only_stop, Carol>, End>);
static_assert(std::is_same_v<project_t<G_only_stop, Frank>, End>);
// has_interaction_between_v on this fixture: false for any pair
// (StopG itself contributes no interactions).
static_assert(!has_interaction_between_v<G_only_stop, Alice, Bob>);
static_assert(!has_interaction_between_v<G_only_stop, Bob,   Alice>);

// (11b) Multi-hop crash with Peer-as-From earlier in the protocol.
// Alice both SENDS earlier (Alice→Bob: P) AND IS the crashed peer.
// Carol→Alice happens immediately before the crash.  Both Bob and
// Carol have direct interactions with Alice — both must project to
// Stop.  Verifies the walker correctly aggregates two independent
// interaction edges resolving on the same Peer.
using G_peer_is_from_earlier =
    Transmission<Alice, Bob,   Ping,
    Transmission<Carol, Alice, Query,
      StopG<Alice>>>;

static_assert(is_global_well_formed_v<G_peer_is_from_earlier>);
static_assert(roles_of_t<G_peer_is_from_earlier>::size == 3);

// Bob's projection: Recv<Ping, Stop>.  Bob received Ping from Alice;
// then Carol→Alice is third-party from Bob's view (skipped); then
// StopG<Alice>'s non-Peer rule queries has_interaction_between_v
// <RootG, Bob, Alice>, which finds the (Alice, Bob) Transmission and
// returns true → Stop.
static_assert(std::is_same_v<
    project_t<G_peer_is_from_earlier, Bob>,
    Recv<Ping, Stop>>);

// Carol's projection: Send<Query, Stop>.  Alice→Bob is third-party
// from Carol's view (skipped); Carol sends Query to Alice; then
// StopG<Alice> with Carol-Alice interaction → Stop.
static_assert(std::is_same_v<
    project_t<G_peer_is_from_earlier, Carol>,
    Send<Query, Stop>>);

// Alice's projection: Send<Ping, Recv<Query, Stop>>.  Alice both
// sends and receives, then crashes.
static_assert(std::is_same_v<
    project_t<G_peer_is_from_earlier, Alice>,
    Send<Ping, Recv<Query, Stop>>>);

// Frank uninvolved → End.
static_assert(std::is_same_v<
    project_t<G_peer_is_from_earlier, Frank>, End>);

// Both edges are detected.
static_assert( has_interaction_between_v<G_peer_is_from_earlier, Alice, Bob>);
static_assert( has_interaction_between_v<G_peer_is_from_earlier, Carol, Alice>);
static_assert(!has_interaction_between_v<G_peer_is_from_earlier, Bob,   Carol>);

// (11c) Nested Rec_G has_interaction_between recursion.  The walker
// must descend through stacked Rec_G layers.  Each layer's body is
// itself a Rec_G; the deepest layer holds the actual Transmission.
// A buggy walker that recurses Rec_G only one level deep would miss
// this and return false — the assertion fires.
using G_nested_rec_3deep = Rec_G<Rec_G<Rec_G<
    Transmission<Alice, Bob, Ping, Var_G>>>>;

static_assert(is_global_well_formed_v<G_nested_rec_3deep>);
// Walker must descend three Rec_G levels to find the (Alice, Bob)
// Transmission inside the innermost body.
static_assert( has_interaction_between_v<G_nested_rec_3deep, Alice, Bob>);
static_assert( has_interaction_between_v<G_nested_rec_3deep, Bob,   Alice>); // sym
static_assert(!has_interaction_between_v<G_nested_rec_3deep, Alice, Carol>);

// Projection through nested Rec_G.  Each Rec_G wraps a Loop in the
// projected protocol.  The framework's `Rec_G<Body>↾R = Loop<Body↾R>`
// rule gives Loop<Loop<Loop<Send<Ping, Continue>>>> for Alice — three
// nested Loops mirroring the three nested Rec_Gs.
static_assert(std::is_same_v<
    project_t<G_nested_rec_3deep, Alice>,
    Loop<Loop<Loop<Send<Ping, Continue>>>>>);
static_assert(std::is_same_v<
    project_t<G_nested_rec_3deep, Bob>,
    Loop<Loop<Loop<Recv<Ping, Continue>>>>>);

// (11d) Repeated interaction in Rec_G + Choice.  The same (A, B)
// pair appears in MULTIPLE places: top-level Choice's (From, To)
// AND inside one branch's continuation.  Walker must NOT double-
// count or short-circuit incorrectly.  has_interaction_between is
// boolean (set membership), so multiple occurrences of the same pair
// resolve to true on the first hit.  We verify that the SECOND
// occurrence (inside branch 0's continuation) doesn't break the
// recursion of OTHER pairs nested in the same branch.
//
// G shape:
//   Rec_G<Choice<Alice, Bob,
//       BranchG<P, Transmission<Alice, Bob, Q,        // (A,B) again
//                   Transmission<Bob, Carol, R,        // (B,C) nested
//                     Var_G>>>,
//       BranchG<Q, End_G>>>
using G_repeated_pair = Rec_G<Choice<Alice, Bob,
    BranchG<Ping,  Transmission<Alice, Bob,   Query,
                    Transmission<Bob,   Carol, Reply, Var_G>>>,
    BranchG<Ack,   End_G>>>;

static_assert(is_global_well_formed_v<G_repeated_pair>);

// Top-level Choice + nested Transmission<Alice, Bob>.
static_assert( has_interaction_between_v<G_repeated_pair, Alice, Bob>);

// Nested-deeper Transmission<Bob, Carol> — must be reached via
// Rec_G → Choice → branch.next → Transmission → continuation →
// Transmission.  Five-level descent.
static_assert( has_interaction_between_v<G_repeated_pair, Bob,   Carol>);
static_assert( has_interaction_between_v<G_repeated_pair, Carol, Bob>);    // sym

// Pairs that don't appear at any depth.
static_assert(!has_interaction_between_v<G_repeated_pair, Alice, Carol>);
static_assert(!has_interaction_between_v<G_repeated_pair, David, Eve>);

// (11e) Negative-witness: regression to the original GAPS-001 bug
// would make these assertions FAIL.  Documents the WRONG behaviour
// the previous rule produced, so a future reviewer who sees this
// fixture knows what specifically is being defended against.
//
// Original (buggy) rule: StopG<Peer> ↾ R = End  (unconditionally)
// Fixed (current) rule:  StopG<Peer> ↾ R = Stop when R interacts
//                                          with Peer in RootG; End
//                                          otherwise.
//
// If the bug regressed:
//   project_t<G_alice_stops, Bob> would become Recv<Ping, End>
//   instead of Recv<Ping, Stop>.  The first negative assertion below
//   would FAIL TO COMPILE (the !std::is_same_v negation would fire).
static_assert(!std::is_same_v<
    project_t<G_alice_stops, Bob>,
    Recv<Ping, End>>);  // would fail iff GAPS-001 regressed

static_assert(!std::is_same_v<
    project_t<G_send_then_crash, Bob>,
    Send<Query, End>>);  // would fail iff GAPS-001 regressed

static_assert(!std::is_same_v<
    project_t<G_diamond_crash, Bob>,
    Recv<Cmd, Send<Sum, End>>>);  // would fail iff GAPS-001 regressed

// ─── End multiparty fixtures (GAPS-001) ──────────────────────────

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
