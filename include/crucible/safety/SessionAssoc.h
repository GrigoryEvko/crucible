#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — L5 association Δ ⊑_s G (SEPLOG-I3,
//                            task #345)
//
// ASSOCIATION is the invariant that ties a typing context Δ (L2) to a
// global type G (L4).  Under Hou-Yoshida-Kuhn 2024's corrected
// formulation, Δ ⊑_s G means:
//
//     (1) dom(Δ) = { s[p] : p ∈ roles(G) }     -- domain matches
//     (2) ∀ p ∈ roles(G):  Δ(s[p]) ⩽ G ↾ p     -- each entry is a
//                                                 subtype of its
//                                                 projection
//
// Once association holds, every global-level property of G (safety,
// deadlock-freedom, liveness, crash-safety) transfers to the
// implementation Δ.  This is the PAYOFF of the top-down methodology:
// write G once, prove G's properties once, then every implementation
// Δ that associates with G inherits the proofs for free — no per-
// implementation re-verification.
//
// ─── Why THIS definition, not classical "consistency" ─────────────
//
// Scalas-Yoshida 2019's original parametric MPST used CONSISTENCY
// (Γ-entries must equal the projections exactly) as the invariant.
// HYK24 proved that SY19's subject-reduction proofs were FLAWED under
// full merging: consistency is NOT preserved across reductions when
// the typing allows coinductive full merging.
//
// The fix is ASSOCIATION (Δ ⊑_s G) — a more permissive invariant
// that ADMITS subtyping: Δ's entries may be subtypes (narrowings,
// refinements) of the projections, not requirements of equality.
// Association IS preserved under reductions.  HYK24 Thm 5.8 proves
// the preservation theorem; this layer implements the static check
// at compile time.
//
// ─── What ships, what's deferred ──────────────────────────────────
//
// Shipped:
//   is_associated_v<Δ, G, SessionTag>    the static association check
//   projected_context_t<G, SessionTag>   canonical Δ for G (reflexive)
//   AssociatedWith<...> concept
//   assert_associated<...>() helper
//   Per-piece traits for diagnosis (domain_matches_v, etc.)
//
// Deferred:
//   * Preservation-of-association THEOREM at compile time.  HYK24
//     proves that if Δ ⊑_s G and Δ →_α Δ', there exists G' s.t.
//     Δ' ⊑_s G'.  Compile-time verification of this preservation
//     requires reduction semantics (L7 #346) — we can check static
//     association but not its persistence across runtime steps.
//   * Full merging variant.  SessionGlobal.h currently ships PLAIN
//     merge for third-party projections; full merge (PMY25 §4.3)
//     is deferred.  Association built on plain merge is SOUND but
//     more restrictive than classical HYK24.
//
// ─── Worked example ────────────────────────────────────────────────
//
//   struct My2PC {};  // session tag
//   struct Coord {}; struct Follower {};
//
//   using G_2PC = Transmission<Coord, Follower, Prepare,
//                 Transmission<Follower, Coord, Vote,
//                 Choice<Coord, Follower,
//                     BranchG<Commit, End_G>,
//                     BranchG<Abort,  End_G>>>>;
//
//   // Reflexive association: Δ = projected_context_t<G_2PC, My2PC>.
//   using Δ_reflexive = projected_context_t<G_2PC, My2PC>;
//   static_assert(is_associated_v<Δ_reflexive, G_2PC, My2PC>);
//
//   // Refined association: Coord narrows Select to commit-only.
//   using RefinedCoord = Send<Prepare,
//                         Recv<Vote,
//                         Select<Send<Commit, End>>>>;  // fewer branches
//   using Δ_refined = Context<
//       Entry<My2PC, Coord,    RefinedCoord>,
//       Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;
//   static_assert(is_associated_v<Δ_refined, G_2PC, My2PC>);
//
//   // Non-association: wrong domain.
//   using Δ_missing = Context<Entry<My2PC, Coord, project_t<G_2PC, Coord>>>;
//   static_assert(!is_associated_v<Δ_missing, G_2PC, My2PC>);
//
// ─── Gated evaluation discipline ──────────────────────────────────
//
// lookup_context_t<Γ, S, R> fires a static_assert when (S, R) is not
// in Γ — not recoverable via SFINAE.  Our is_associated_v MUST check
// domain-match FIRST and gate the per-role subtype check on that
// result.  The subtype check requires lookups that are undefined
// when domain doesn't match; if we evaluated both checks eagerly,
// domain-mismatched Γ would fire the lookup's static_assert instead
// of cleanly returning false from is_associated_v.
//
// We use the two-tier gated-check pattern (same as
// SessionSubtype.h's gated_prefix_check): a struct parameterised on
// a boolean gate parameter; the false-gate partial spec returns
// false without touching the dependent machinery; the true-gate
// spec evaluates the expensive check.
//
// ─── References ───────────────────────────────────────────────────
//
//   Scalas-Yoshida 2019 (POPL) — parametric MPST; the consistency
//     invariant (which HYK24 later corrected to association).
//   Hou-Yoshida-Kuhn 2024, "Less is More Revisited" — proves SY19's
//     subject-reduction broke under full merging; introduces
//     Δ ⊑_s G as the correct invariant; proves preservation under
//     reductions.
//   Gay-Hole 2005 — the ⩽ subtyping relation used for entry-by-
//     entry refinement check.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionContext.h>
#include <crucible/safety/SessionGlobal.h>
#include <crucible/safety/SessionSubtype.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── RoleList subset + equality ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// RoleList is defined in SessionGlobal.h; here we add equality and
// subset operations (since RoleLists represent SETS per the
// insert_unique invariant).  Both are fold-expression based;
// O(|RL1| × |RL2|) compile-time instantiations.

namespace detail::assoc {

template <typename RL1, typename RL2>
struct role_list_subset;

template <typename... Rs1, typename RL2>
struct role_list_subset<RoleList<Rs1...>, RL2>
    : std::bool_constant<
          (detail::global::contains_role_v<Rs1, RL2> && ...)
      > {};

}  // namespace detail::assoc

// RL1 ⊆ RL2?  true when every role in RL1 appears in RL2.  Empty RL1
// is vacuously a subset of any RL2 (fold over empty pack is true).
template <typename RL1, typename RL2>
inline constexpr bool role_list_subset_v =
    detail::assoc::role_list_subset<RL1, RL2>::value;

// RL1 == RL2 as sets?  Bidirectional subset.
template <typename RL1, typename RL2>
inline constexpr bool role_lists_equal_as_sets_v =
    role_list_subset_v<RL1, RL2> && role_list_subset_v<RL2, RL1>;

// ═════════════════════════════════════════════════════════════════════
// ── domain_roles_for_session_t<Γ, SessionTag> ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Filter Γ's entries, keeping only those whose session_tag matches
// the requested SessionTag, and collect their role tags into a
// RoleList.  Recursive filter; O(|Γ|) compile-time cost.

namespace detail::assoc {

template <typename ΓEntries, typename SessionTag>
struct collect_roles_for_session;

template <typename SessionTag>
struct collect_roles_for_session<Context<>, SessionTag> {
    using type = EmptyRoleList;
};

// Entry whose session matches — prepend its role to the recursion's result.
template <typename SessionTag, typename R, typename T, typename... Rest>
struct collect_roles_for_session<
    Context<Entry<SessionTag, R, T>, Rest...>, SessionTag>
{
    using rest_type = typename collect_roles_for_session<
        Context<Rest...>, SessionTag>::type;
    using type = insert_unique_t<R, rest_type>;
};

// Entry whose session does NOT match — skip.  This partial spec is
// LESS specialised than the matching one above, so the compiler
// prefers the match when applicable.
template <typename S, typename R, typename T, typename... Rest,
          typename SessionTag>
struct collect_roles_for_session<
    Context<Entry<S, R, T>, Rest...>, SessionTag>
{
    using type = typename collect_roles_for_session<
        Context<Rest...>, SessionTag>::type;
};

}  // namespace detail::assoc

template <typename Γ, typename SessionTag>
using domain_roles_for_session_t =
    typename detail::assoc::collect_roles_for_session<Γ, SessionTag>::type;

// ═════════════════════════════════════════════════════════════════════
// ── domain_matches_v<Γ, G, SessionTag> ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Condition (1) of association:
//     dom_S(Δ) = { (S, p) : p ∈ roles(G) }
// equivalently:
//     domain_roles_for_session_t<Δ, S> == roles_of_t<G>  (as sets)

template <typename Γ, typename G, typename SessionTag>
inline constexpr bool domain_matches_v =
    role_lists_equal_as_sets_v<
        domain_roles_for_session_t<Γ, SessionTag>,
        roles_of_t<G>>;

// ═════════════════════════════════════════════════════════════════════
// ── all_entries_refine_projection_v (gated subtype check) ──────────
// ═════════════════════════════════════════════════════════════════════
//
// Condition (2) of association:
//     ∀ p ∈ roles(G):  Δ(S, p) ⩽ G ↾ p   (sync subtype)
//
// MUST be gated on domain match — when (S, p) is not in Δ,
// lookup_context_t fires its own static_assert, breaking SFINAE.
//
// Pattern: parameterise the struct on a boolean gate; the false gate
// returns false without touching lookup_context_t; the true gate
// evaluates the per-role subtype fold.

namespace detail::assoc {

template <bool DomainOK, typename Γ, typename G, typename SessionTag,
          typename RolesPack>
struct gated_refine_check : std::false_type {};

// Gate true: actually do the per-role subtype check.
template <typename Γ, typename G, typename SessionTag, typename... Rs>
struct gated_refine_check<true, Γ, G, SessionTag, RoleList<Rs...>>
    : std::bool_constant<
          (is_subtype_sync_v<
               lookup_context_t<Γ, SessionTag, Rs>,
               project_t<G, Rs>> && ...)
      > {};

}  // namespace detail::assoc

// The gated per-role subtype check.  False when domain-mismatched
// (no lookup attempted); otherwise iterates G's roles and tests
// each (S, p) entry against its projection.
template <typename Γ, typename G, typename SessionTag>
inline constexpr bool all_entries_refine_projection_v =
    detail::assoc::gated_refine_check<
        domain_matches_v<Γ, G, SessionTag>,
        Γ, G, SessionTag,
        roles_of_t<G>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── is_associated_v<Γ, G, SessionTag> — the full check ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Δ ⊑_s G  iff  domain_matches  ∧  every entry refines its projection.
// The second conjunct is already gated, so the top-level expression
// is simply the AND.

template <typename Γ, typename G, typename SessionTag>
inline constexpr bool is_associated_v =
    domain_matches_v<Γ, G, SessionTag>
    && all_entries_refine_projection_v<Γ, G, SessionTag>;

// ═════════════════════════════════════════════════════════════════════
// ── projected_context_t<G, SessionTag> ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The CANONICAL associated context for G: one Entry per role in G,
// with local_type = exact projection.  By reflexivity of subtyping
// (T ⩽ T), projected_context_t IS always associated with G.  Useful
// starting point for users: "give me the simplest Δ that associates
// with G".

namespace detail::assoc {

template <typename RolesPack, typename G, typename SessionTag>
struct project_to_context;

template <typename... Rs, typename G, typename SessionTag>
struct project_to_context<RoleList<Rs...>, G, SessionTag> {
    using type = Context<Entry<SessionTag, Rs, project_t<G, Rs>>...>;
};

}  // namespace detail::assoc

template <typename G, typename SessionTag>
using projected_context_t =
    typename detail::assoc::project_to_context<
        roles_of_t<G>, G, SessionTag>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Ergonomic surface ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Concept form for require-clauses.
template <typename Γ, typename G, typename SessionTag>
concept AssociatedWith = is_associated_v<Γ, G, SessionTag>;

// Consteval assertion with a call-site diagnostic.  Named intent-
// revealing wrapper for protocol-declaration-time use.
template <typename Γ, typename G, typename SessionTag>
consteval void assert_associated() noexcept {
    static_assert(domain_matches_v<Γ, G, SessionTag>,
        "crucible::session::diagnostic [Association_Domain_Mismatch]: "
        "assert_associated: condition (1) fails — Γ's domain (for the "
        "given SessionTag) does not match roles_of_t<G>.  Every role "
        "of G must have a corresponding Entry<SessionTag, role, "
        "local_type> in Γ, and Γ must not have EXTRA entries for this "
        "session beyond G's roles.  Common causes: missing an entry "
        "for a participating role; added an entry for a role not in "
        "G; wrong SessionTag.");

    static_assert(all_entries_refine_projection_v<Γ, G, SessionTag>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_associated: condition (2) fails — at least one Γ "
        "entry's local_type is NOT a synchronous subtype of its "
        "projection G ↾ role.  Subtype follows Gay-Hole 2005 rules "
        "(see SessionSubtype.h): Send payload covariant + continuation "
        "covariant; Recv payload contravariant + continuation "
        "covariant; Select narrows (fewer branches is a subtype); "
        "Offer widens (more branches is a subtype); Loop bodies "
        "related coinductively; Stop is bottom.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the association machinery at compile time across a realistic
// scenario (2PC) plus positive and negative refinement cases.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::assoc::assoc_self_test {

// ─── Fixture tags ──────────────────────────────────────────────────

struct My2PC    {};  // session tag
struct OtherSession {};
struct Coord    {};
struct Follower {};
struct Stranger {};  // a role NOT in G_2PC — used for negative tests

struct Prepare {};
struct Vote    {};
struct Commit  {};
struct Abort   {};

// ─── Fixture global type ──────────────────────────────────────────

using G_2PC = Transmission<Coord, Follower, Prepare,
              Transmission<Follower, Coord, Vote,
              Choice<Coord, Follower,
                  BranchG<Commit, End_G>,
                  BranchG<Abort,  End_G>>>>;

// Sanity: G_2PC is well-formed and its role set is {Coord, Follower}.
static_assert(is_global_well_formed_v<G_2PC>);
static_assert(roles_of_t<G_2PC>::size == 2);

// ─── role_list_subset + role_lists_equal_as_sets ──────────────────

using RL_CF  = RoleList<Coord, Follower>;
using RL_FC  = RoleList<Follower, Coord>;
using RL_C   = RoleList<Coord>;
using RL_CFS = RoleList<Coord, Follower, Stranger>;
using RL_e   = EmptyRoleList;

static_assert(role_list_subset_v<RL_e,  RL_CF>);
static_assert(role_list_subset_v<RL_C,  RL_CF>);
static_assert(role_list_subset_v<RL_CF, RL_CF>);
static_assert(role_list_subset_v<RL_CF, RL_FC>);  // set equality despite order
static_assert(!role_list_subset_v<RL_CFS, RL_CF>); // superset is not subset
static_assert(!role_list_subset_v<RL_CF,  RL_C>);  // Follower not in RL_C

// role_lists_equal_as_sets: order-insensitive.
static_assert( role_lists_equal_as_sets_v<RL_CF, RL_FC>);
static_assert( role_lists_equal_as_sets_v<RL_e,  RL_e>);
static_assert(!role_lists_equal_as_sets_v<RL_CF, RL_C>);
static_assert(!role_lists_equal_as_sets_v<RL_CF, RL_CFS>);

// ─── domain_roles_for_session_t ───────────────────────────────────

using ReflexiveΔ = projected_context_t<G_2PC, My2PC>;

// Δ_reflexive's domain (for My2PC) is {Coord, Follower}.
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<ReflexiveΔ, My2PC>,
    RL_CF>);

// Δ_reflexive's domain for an OTHER session is empty.
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<ReflexiveΔ, OtherSession>,
    RL_e>);

// Multi-session Γ: filter picks only the matching-session entries.
using ΔMultiSession = Context<
    Entry<My2PC,        Coord,    project_t<G_2PC, Coord>>,
    Entry<My2PC,        Follower, project_t<G_2PC, Follower>>,
    Entry<OtherSession, Coord,    End>>;
// For My2PC: {Coord, Follower}.
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<ΔMultiSession, My2PC>, RL_CF>);
// For OtherSession: {Coord}.
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<ΔMultiSession, OtherSession>, RL_C>);

// ─── Reflexive association: projected_context_t ⊑_s G always ─────

static_assert( domain_matches_v<ReflexiveΔ, G_2PC, My2PC>);
static_assert( all_entries_refine_projection_v<ReflexiveΔ, G_2PC, My2PC>);
static_assert( is_associated_v<ReflexiveΔ, G_2PC, My2PC>);

// ─── Refined association: Coord narrows Select to commit-only ────

// The projection of G_2PC onto Coord is:
//   Send<Prepare, Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>>
//
// A narrower implementation: Coord commits unconditionally.
using RefinedCoord = Send<Prepare,
                     Recv<Vote,
                     Select<Send<Commit, End>>>>;

// Verify the refinement is a sync subtype of the projection.
static_assert(is_subtype_sync_v<RefinedCoord, project_t<G_2PC, Coord>>);

using ΔRefined = Context<
    Entry<My2PC, Coord,    RefinedCoord>,
    Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

static_assert(is_associated_v<ΔRefined, G_2PC, My2PC>);

// ─── Non-association: wrong direction of subtype ────────────────
//
// If Δ's entry is a SUPERtype of the projection (more branches, wider
// choice), association fails.

using WidenedCoord = Send<Prepare,
                     Recv<Vote,
                     Select<
                         Send<Commit, End>,
                         Send<Abort,  End>,
                         Send<Commit, End>>>>;  // duplicate branch = wider

using ΔWidened = Context<
    Entry<My2PC, Coord,    WidenedCoord>,
    Entry<My2PC, Follower, project_t<G_2PC, Follower>>>;

// WidenedCoord is NOT a subtype of the projection (more Select
// branches than the super can accept).
static_assert(!is_subtype_sync_v<WidenedCoord, project_t<G_2PC, Coord>>);
static_assert(!all_entries_refine_projection_v<ΔWidened, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔWidened, G_2PC, My2PC>);

// ─── Non-association: missing role ──────────────────────────────

using ΔMissing = Context<
    Entry<My2PC, Coord, project_t<G_2PC, Coord>>>;
// Missing Follower entry.

static_assert(!domain_matches_v<ΔMissing, G_2PC, My2PC>);
// all_entries_refine_projection_v is gated on domain_matches, so it
// returns false without instantiating lookup_context_t for Follower.
static_assert(!all_entries_refine_projection_v<ΔMissing, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔMissing, G_2PC, My2PC>);

// ─── Non-association: extra role ────────────────────────────────

using ΔExtra = Context<
    Entry<My2PC, Coord,    project_t<G_2PC, Coord>>,
    Entry<My2PC, Follower, project_t<G_2PC, Follower>>,
    Entry<My2PC, Stranger, End>>;  // role not in G_2PC

static_assert(!domain_matches_v<ΔExtra, G_2PC, My2PC>);
static_assert(!is_associated_v<ΔExtra, G_2PC, My2PC>);

// ─── Non-association: wrong session tag ─────────────────────────

// An entirely different session's Γ has nothing to do with G_2PC@My2PC.
using ΔWrongSession = Context<
    Entry<OtherSession, Coord,    project_t<G_2PC, Coord>>,
    Entry<OtherSession, Follower, project_t<G_2PC, Follower>>>;

// Checking against My2PC: domain is EMPTY (no My2PC entries).
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<ΔWrongSession, My2PC>,
    RL_e>);
static_assert(!is_associated_v<ΔWrongSession, G_2PC, My2PC>);
// But against OtherSession: reflexive, associates fine.
static_assert( is_associated_v<ΔWrongSession, G_2PC, OtherSession>);

// ─── Multi-session Γ: sessions are independently associated ─────

// ΔMultiSession has BOTH My2PC@{Coord, Follower} AND
// OtherSession@{Coord}.  Only the My2PC part matches G_2PC.  The
// OtherSession entry is irrelevant — does not disturb association
// for My2PC.
static_assert(is_associated_v<ΔMultiSession, G_2PC, My2PC>);

// ─── AssociatedWith concept compiles ────────────────────────────

template <typename Γ, typename G, typename SessionTag>
    requires AssociatedWith<Γ, G, SessionTag>
consteval bool requires_associated() { return true; }

static_assert(requires_associated<ReflexiveΔ, G_2PC, My2PC>());
static_assert(requires_associated<ΔRefined,  G_2PC, My2PC>());

// ─── assert_associated helper compiles ──────────────────────────

consteval bool check_assert_associated() {
    assert_associated<ReflexiveΔ, G_2PC, My2PC>();
    assert_associated<ΔRefined,  G_2PC, My2PC>();
    return true;
}
static_assert(check_assert_associated());

// ─── Reflexivity theorem witness ────────────────────────────────
//
// For every well-formed global type G and session tag S:
//     is_associated_v<projected_context_t<G, S>, G, S>  == true
//
// (Uses reflexivity of sync subtyping: T ⩽ T.)

struct Alice {}; struct Bob {};
struct Ping {};

// Exercise reflexivity on a few more shapes:
using G_bin = Transmission<Alice, Bob, Ping, End_G>;
static_assert(is_associated_v<projected_context_t<G_bin, My2PC>, G_bin, My2PC>);

using G_loop = Rec_G<Transmission<Alice, Bob, Ping, Var_G>>;
static_assert(is_associated_v<projected_context_t<G_loop, My2PC>, G_loop, My2PC>);

using G_empty = End_G;
static_assert(is_associated_v<projected_context_t<G_empty, My2PC>, G_empty, My2PC>);
// Empty-G has empty roles, so Δ = EmptyContext.
static_assert(std::is_same_v<projected_context_t<G_empty, My2PC>, EmptyContext>);

}  // namespace detail::assoc::assoc_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
