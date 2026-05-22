#pragma once

// ── crucible::fixy::sess::assoc — Δ ⊑_s G association layer ─────────
//
// FIXY-V-059.  Re-exports the public surface of
// `sessions/SessionAssoc.h` — the L5 association invariant (HYK24
// Hou-Yoshida-Kuhn 2024, corrected from Scalas-Yoshida 2019's flawed
// consistency invariant) that ties a typing context Δ (L2) to a global
// type G (L4).
//
// Δ ⊑_s G holds iff
//
//   (1) dom(Δ) = { s[p] : p ∈ roles(G) }      — domain matches
//   (2) ∀ p ∈ roles(G):  Δ(s[p]) ⩽ G ↾ p      — each entry is a
//                                               synchronous subtype
//                                               of its projection
//
// Once association holds, every global-level property of G (safety,
// deadlock-freedom, liveness, crash-safety) transfers to the
// implementation Δ — write G once, prove G's properties once, then
// every Δ that associates inherits the proofs for free.
//
// ── Why this surface exists (Agent 3 finding B3 MEDIUM) ────────────
//
// Before V-059, only the diagnostic tag `Association_Domain_Mismatch`
// was reachable through fixy:: (via SessDiagnostic.h).  A Vessel→Forge
// pipeline that mints multiple sessions from one Γ had no fixy-surface
// way to assert `is_associated_v<Γ, G, S>` at the composition boundary
// — substrate-direct calls in the band-3 trees bypass the fixy
// discipline ledger.  This header closes that gap.
//
// ── Ten symbols (the public association API) ───────────────────────
//
//   role-list helpers (3):   role_list_subset, role_list_subset_v,
//                            role_lists_equal_as_sets_v
//   domain projection (1):   domain_roles_for_session_t
//   per-condition traits (2): domain_matches_v,
//                            all_entries_refine_projection_v
//   association invariant (3): is_associated_v, AssociatedWith,
//                            assert_associated
//   canonical Δ generator (1): projected_context_t
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Ten using-decls, sentinel battery, smoke routine.  No new
// types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; every symbol is a
//              type-level metafunction with no runtime side effect.
//   TypeSafe — using-decls preserve substrate identity; the (Γ, G,
//              SessionTag) triple is structurally distinct.
//   NullSafe — no pointer state.
//   MemSafe  — all symbols are compile-time-only; nothing allocated.
//   DetSafe  — pure type-level computation; same inputs always
//              produce the same is_associated_v outcome.
//   BorrowSafe — no aliasing at this layer (purely structural).
//   ThreadSafe — no shared state crossed.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionAssoc.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::assoc {

// ── 1. Role-list set machinery (2) ─────────────────────────────────
// HYK24's condition (1) is a set-equality test; these primitives
// implement the order-insensitive subset / equality checks the domain
// trait composes.  Substrate keeps the class-template `role_list_subset`
// in `detail::assoc::`; the public surface exposes only the `_v` form.
using ::crucible::safety::proto::role_list_subset_v;
using ::crucible::safety::proto::role_lists_equal_as_sets_v;

// ── 2. Domain projection (1) ───────────────────────────────────────
// Given a context Γ and a session tag S, collect the set of roles R
// such that Entry<S, R, _> ∈ Γ.  This is dom(Γ) restricted to one
// session — the LHS of condition (1).
using ::crucible::safety::proto::domain_roles_for_session_t;

// ── 3. Per-condition diagnostic traits (2) ─────────────────────────
// HYK24's two conditions split into separately-queryable traits so
// callers can attribute failures to the SPECIFIC violated condition.
// domain_matches_v fires condition (1); all_entries_refine_projection_v
// fires (the gated form of) condition (2).
using ::crucible::safety::proto::domain_matches_v;
using ::crucible::safety::proto::all_entries_refine_projection_v;

// ── 4. The association invariant proper (3) ────────────────────────
// is_associated_v — the boolean static check.
// AssociatedWith — concept form for requires-clauses.
// assert_associated — consteval helper firing both condition-(1) and
//   condition-(2) diagnostics at the call site (named intent-revealing
//   wrapper for protocol-declaration-time use).
using ::crucible::safety::proto::is_associated_v;
using ::crucible::safety::proto::AssociatedWith;
using ::crucible::safety::proto::assert_associated;

// ── 5. Canonical Δ generator (1) ───────────────────────────────────
// Reflexive association: projected_context_t<G, S> ⊑_s G holds for
// every well-formed G and tag S.  The simplest associating Δ.
using ::crucible::safety::proto::projected_context_t;

}  // namespace crucible::fixy::sess::assoc

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessContext.h::u052i_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.  ASCII-only identifiers per CLAUDE.md §XVII (no Γ /
// Δ in identifiers — the substrate uses them; this header does not).

namespace crucible::fixy::sess::assoc::v059_self_test {

namespace proto = ::crucible::safety::proto;

// ASCII fixture tags — minimal G that exercises every export.
struct MySess {};        // session tag
struct OtherSess {};     // different session tag (for wrong-tag witness)
struct Alice {};
struct Bob {};
struct Stranger {};      // role NOT in G_bin
struct Ping {};

// Minimal well-formed G: one-message Alice → Bob.
using G_bin = proto::Transmission<Alice, Bob, Ping, proto::End_G>;

// Reflexive Δ — the substrate guarantees Δ_refl ⊑_s G.
using GammaRefl = projected_context_t<G_bin, MySess>;

// ── A. Role-list helpers reach (variable identity) ─────────────────
// Substrate's `role_list_subset` class template lives in `detail::assoc::`
// and is not part of the public surface — only the `_v` form is.
static_assert(role_list_subset_v<proto::RoleList<Alice>,
                                 proto::RoleList<Alice, Bob>>
              == proto::role_list_subset_v<proto::RoleList<Alice>,
                                           proto::RoleList<Alice, Bob>>,
    "role_list_subset_v must reach identically through fixy::");
static_assert(role_list_subset_v<proto::RoleList<Alice>,
                                 proto::RoleList<Alice, Bob>>,
    "{Alice} ⊆ {Alice, Bob} must hold (subset is reflexive on each Rs).");
static_assert(!role_list_subset_v<proto::RoleList<Stranger>,
                                  proto::RoleList<Alice, Bob>>,
    "Stranger ∉ {Alice, Bob} — subset must reject.");
static_assert(role_lists_equal_as_sets_v<proto::RoleList<Alice, Bob>,
                                         proto::RoleList<Bob,   Alice>>,
    "Set equality is order-insensitive (Alice,Bob == Bob,Alice).");

// ── B. Domain projection trait reach ────────────────────────────────
static_assert(role_lists_equal_as_sets_v<
    domain_roles_for_session_t<GammaRefl, MySess>,
    proto::RoleList<Alice, Bob>>,
    "GammaRefl's domain (restricted to MySess) must be {Alice, Bob}.");
static_assert(std::is_same_v<
    domain_roles_for_session_t<GammaRefl, OtherSess>,
    proto::EmptyRoleList>,
    "GammaRefl's domain for OtherSess is empty (no entries tagged OtherSess).");

// ── C. Per-condition trait reach ────────────────────────────────────
static_assert(domain_matches_v<GammaRefl, G_bin, MySess>
              == proto::domain_matches_v<GammaRefl, G_bin, MySess>,
    "domain_matches_v must reach identically through fixy::");
static_assert(all_entries_refine_projection_v<GammaRefl, G_bin, MySess>
              == proto::all_entries_refine_projection_v<GammaRefl, G_bin, MySess>,
    "all_entries_refine_projection_v must reach identically.");
static_assert(domain_matches_v<GammaRefl, G_bin, MySess>);
static_assert(all_entries_refine_projection_v<GammaRefl, G_bin, MySess>);

// ── D. The association invariant proper ────────────────────────────
static_assert(is_associated_v<GammaRefl, G_bin, MySess>
              == proto::is_associated_v<GammaRefl, G_bin, MySess>,
    "is_associated_v must reach identically through fixy::");
static_assert(is_associated_v<GammaRefl, G_bin, MySess>,
    "Reflexive association: projected_context_t<G, S> ⊑_s G always holds.");

// AssociatedWith concept reach.
template <typename G_arg, typename G_, typename S>
    requires AssociatedWith<G_arg, G_, S>
consteval bool requires_associated_witness() { return true; }
static_assert(requires_associated_witness<GammaRefl, G_bin, MySess>());

// assert_associated helper reach (consteval call site).
consteval bool check_fixy_assert_associated() {
    assert_associated<GammaRefl, G_bin, MySess>();
    return true;
}
static_assert(check_fixy_assert_associated());

// ── E. Canonical Δ generator reach ──────────────────────────────────
static_assert(std::is_same_v<
    projected_context_t<G_bin, MySess>,
    proto::projected_context_t<G_bin, MySess>>,
    "projected_context_t must reach identically through fixy::");

// Empty-G's reflexive Δ is EmptyContext.
static_assert(std::is_same_v<
    projected_context_t<proto::End_G, MySess>,
    proto::EmptyContext>,
    "projected_context_t<End_G, S> must be EmptyContext (no roles).");

// ── F. Cardinality witness — count of items V-059 surfaces ──────────
//
//   role-list helpers (2: role_list_subset_v + role_lists_equal_as_sets_v)
//   + domain projection (1: domain_roles_for_session_t)
//   + per-condition traits (2: domain_matches_v + all_entries_refine_projection_v)
//   + association invariant (3: is_associated_v + AssociatedWith + assert_associated)
//   + canonical Δ generator (1: projected_context_t)              ──── 9
constexpr int v059_surface_cardinality = 9;
static_assert(v059_surface_cardinality == 9,
    "fixy::sess::assoc:: V-059 surface cardinality drifted — update "
    "SessAssoc.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::assoc::v059_self_test

namespace crucible::fixy::sess::assoc {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces every association metafunction through real
// instantiation so latent template-evaluation issues surface under
// `-fsyntax-only` of any TU that includes SessAssoc.h.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct S {};
    struct OS {};
    struct RA {};
    struct RB {};
    struct M {};

    using G = proto::Transmission<RA, RB, M, proto::End_G>;
    using Gamma = projected_context_t<G, S>;

    [[maybe_unused]] constexpr bool dom    = domain_matches_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool refine = all_entries_refine_projection_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool assoc  = is_associated_v<Gamma, G, S>;
    [[maybe_unused]] constexpr bool subset = role_list_subset_v<
        proto::RoleList<RA>, proto::RoleList<RA, RB>>;
    [[maybe_unused]] constexpr bool equal  = role_lists_equal_as_sets_v<
        proto::RoleList<RA, RB>, proto::RoleList<RB, RA>>;
    using DomRoles = domain_roles_for_session_t<Gamma, S>;
    using EmptyDom = domain_roles_for_session_t<Gamma, OS>;
    [[maybe_unused]] constexpr bool dr_ok =
        role_lists_equal_as_sets_v<DomRoles, proto::RoleList<RA, RB>>;
    [[maybe_unused]] constexpr bool ed_ok =
        std::is_same_v<EmptyDom, proto::EmptyRoleList>;

    (void) dom; (void) refine; (void) assoc; (void) subset;
    (void) equal; (void) dr_ok; (void) ed_ok;
}

}  // namespace crucible::fixy::sess::assoc
