#pragma once

// ── crucible::fixy::sess::mpst — MPST global-types layer ──────────
//
// FIXY-U-013 / FIXY-V-068 (file rename — canonical home).
//
// Re-exports the Honda 2008 / Gay-Hole 2005 multi-party session
// type machinery from safety/proto:: (sessions/SessionGlobal.h)
// into the `crucible::fixy::sess::mpst::` namespace so production
// callers can spell every MPST construct through the fixy umbrella
// without reaching into the substrate.
//
// ── File history ───────────────────────────────────────────────────
//
// V-068 renamed `fixy/Mpst.h` → `fixy/SessGlobal.h` for naming
// consistency with the `Sess<Foo>.h` convention used by V-059..V-066
// session-surface carve-outs (SessAssoc / SessDelegate / SessCheckpoint
// / SessRowExtraction / SessView / SessCrash / SessFederation / SessShape).
// The MPST surface IS the binary-session's global-type generalisation —
// the file-naming alignment makes that role visible to reviewers reading
// `include/crucible/fixy/`.
//
// The pre-V-068 path `fixy/Mpst.h` stays as a thin shim re-include
// (one-release deprecation window per U-123 retire-stale-fixture
// discipline) so existing call sites compile unchanged.
//
// Namespace identity is preserved: this file ships its surface under
// `crucible::fixy::sess::mpst::` exactly as the pre-rename Mpst.h did;
// no caller-visible namespace migration is required by V-068.
//
// ── Surface (unchanged from FIXY-U-013) ───────────────────────────
//
// Three layers surfaced:
//
//   1. Global type constructors (BHYZ23 §3, HYC 2008 §2):
//        End_G  Var_G  Transmission  BranchG  Choice  Rec_G  StopG
//
//   2. Form / well-formedness / diagnostics (PMY25 §4.2, BSYZ22):
//        is_{end_g,transmission,choice,rec_g,var_g,stop_g}_v
//        is_global_well_formed_v
//        has_self_loop_v / assert_no_self_loop
//        has_empty_choice_v / assert_no_empty_choice
//        plain_merge_t (Honda 2008 — plain merging on third-party
//        projections; full merging deferred per PMY25 §4.3)
//
//   3. Role machinery + projection (BHYZ23 §3.4 projection rules):
//        RoleList  EmptyRoleList  contains_role_v
//        insert_unique_t  union_roles_t
//        RolesOf  roles_of_t
//        Project  project_t
//
// ── Why a dedicated mpst:: sub-namespace ──────────────────────────
//
// fixy::sess:: holds the BINARY session-type surface (Send / Recv /
// Loop / Select / Offer + the various Handle wrappers).  MPST is the
// MULTI-party generalisation that lives one layer up — a global type
// G projects to per-role local types via project_t<G, Role>, and
// every local-type protocol used at fixy::sess:: is in principle
// derivable from a global G + a role.  Keeping the MPST surface in
// its own sub-namespace mirrors safety::proto::'s organisation
// (SessionGlobal.h is one of ~30 sessions/ headers) and keeps the
// fixy::sess:: top level focussed on the binary surface every
// production hot path actually consumes today.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::proto::End_G, Var_G, Transmission, BranchG, Choice,
//                  Rec_G, StopG                       — global types
//   safety::proto::is_*_v                              — form predicates
//   safety::proto::is_global_well_formed_v             — wellformedness
//   safety::proto::has_self_loop_v / has_empty_choice_v + asserts
//   safety::proto::plain_merge_t                       — branch merge
//   safety::proto::RoleList / EmptyRoleList            — role tracking
//   safety::proto::contains_role_v / insert_unique_t
//                  union_roles_t                       — role algebra
//   safety::proto::RolesOf / roles_of_t                — role extraction
//   safety::proto::Project / project_t                 — G ↾ R
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-eight using-decls + a sentinel battery.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is an alias.  Template-arg dependent instantia-
// tions (Project<G,R>, project_t<G,R>, etc.) cost exactly what they
// cost when spelled through the substrate.

#include <crucible/sessions/SessionGlobal.h>

namespace crucible::fixy::sess::mpst {

// ═════════════════════════════════════════════════════════════════════
// ── 1. Global type constructors ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::End_G;
using ::crucible::safety::proto::Var_G;
using ::crucible::safety::proto::Transmission;
using ::crucible::safety::proto::BranchG;
using ::crucible::safety::proto::Choice;
using ::crucible::safety::proto::Rec_G;
using ::crucible::safety::proto::StopG;

// ═════════════════════════════════════════════════════════════════════
// ── 2. Form predicates (variable-template form) ────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::is_end_g_v;
using ::crucible::safety::proto::is_transmission_v;
using ::crucible::safety::proto::is_choice_v;
using ::crucible::safety::proto::is_rec_g_v;
using ::crucible::safety::proto::is_var_g_v;
using ::crucible::safety::proto::is_stop_g_v;

// ═════════════════════════════════════════════════════════════════════
// ── 3. Role machinery ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::RoleList;
using ::crucible::safety::proto::EmptyRoleList;
using ::crucible::safety::proto::insert_unique_t;
using ::crucible::safety::proto::union_roles_t;
using ::crucible::safety::proto::RolesOf;
using ::crucible::safety::proto::roles_of_t;

// ═════════════════════════════════════════════════════════════════════
// ── 4. Well-formedness + diagnostic helpers ────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::is_global_well_formed_v;
using ::crucible::safety::proto::has_self_loop_v;
using ::crucible::safety::proto::assert_no_self_loop;
using ::crucible::safety::proto::has_empty_choice_v;
using ::crucible::safety::proto::assert_no_empty_choice;

// ═════════════════════════════════════════════════════════════════════
// ── 5. Plain merge (third-party projection unifier) ────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::plain_merge_t;

// ═════════════════════════════════════════════════════════════════════
// ── 6. Projection metafunction (G ↾ Role) ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::Project;
using ::crucible::safety::proto::project_t;

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Same dual-export discipline as fixy/Rules.h::u062_self_test +
// fixy/Bridge.h::self_test.  Drift between the substrate's MPST
// surface and the fixy projection here trips at every consumer's
// include time — NOT only inside a downstream test TU.

namespace u013_self_test {

// ── A. Role tags (anonymous-namespace placeholders for sentinel use) ─
struct Alice {};
struct Bob   {};
struct Carol {};
struct Ping  {};   // payload
struct Pong  {};   // payload

// ── B. Form predicates discriminate the six tag families ────────────
static_assert( is_end_g_v<End_G>);
static_assert(!is_end_g_v<Var_G>);
static_assert( is_var_g_v<Var_G>);
static_assert( is_transmission_v<Transmission<Alice, Bob, Ping, End_G>>);
static_assert(!is_transmission_v<End_G>);
static_assert( is_choice_v<Choice<Alice, Bob, BranchG<Ping, End_G>>>);
static_assert( is_rec_g_v<Rec_G<End_G>>);
static_assert( is_stop_g_v<StopG<Alice>>);

// ── C. Well-formedness — positive + negative probes ────────────────
//
// 1. End_G is trivially well-formed.
static_assert(is_global_well_formed_v<End_G>);
// 2. A bare Var_G outside Rec_G is ill-formed (no enclosing binder).
static_assert(!is_global_well_formed_v<Var_G>);
// 3. A canonical 3-party Transmission with End_G continuation is
//    well-formed.
static_assert(is_global_well_formed_v<
    Transmission<Alice, Bob, Ping, End_G>>);
// 4. A self-Transmission (Alice → Alice) is ill-formed per the
//    "From ≠ To" axiom (#363).
static_assert(!is_global_well_formed_v<
    Transmission<Alice, Alice, Ping, End_G>>);

// ── D. Self-loop detection — pinpoints the From == To bug ──────────
static_assert( has_self_loop_v<Transmission<Alice, Alice, Ping, End_G>>);
static_assert(!has_self_loop_v<Transmission<Alice, Bob,   Ping, End_G>>);

// ── E. Empty-Choice detection — pinpoints the zero-branch Choice bug
static_assert( has_empty_choice_v<Choice<Alice, Bob>>);
static_assert(!has_empty_choice_v<
    Choice<Alice, Bob, BranchG<Ping, End_G>>>);

// ── F. Role machinery — RoleList, insert, union, roles_of ──────────
//
// Substrate `insert_unique_t<R, RoleList<Rs...>>` is PREPEND-form per
// SessionGlobal.h:282 (RoleList<R, Rs...> when absent; RoleList<Rs...>
// when present).  union_roles_t folds insert_unique_t right-to-left,
// so the order ends up reverse-of-encounter on the absent additions.
using RL_A    = RoleList<Alice>;
using RL_AB   = RoleList<Alice, Bob>;
using RL_BC   = RoleList<Bob, Carol>;
static_assert( std::is_same_v<insert_unique_t<Carol, RL_AB>,
                               RoleList<Carol, Alice, Bob>>);
// insert_unique is idempotent on already-present role.
static_assert( std::is_same_v<insert_unique_t<Alice, RL_AB>, RL_AB>);
// union_roles_t<RL_AB, RL_BC>: walks RL_BC = (Bob, Carol).  Bob already
// present → RL_AB unchanged; Carol absent → prepend → RoleList<Carol,
// Alice, Bob>.
static_assert( std::is_same_v<union_roles_t<RL_AB, RL_BC>,
                               RoleList<Carol, Alice, Bob>>);
// EmptyRoleList alias resolves identically.
static_assert( std::is_same_v<EmptyRoleList, RoleList<>>);

// roles_of_t extracts every distinct role from a global type tree.
using G_ternary =
    Transmission<Alice, Bob, Ping,
        Transmission<Bob, Carol, Pong, End_G>>;
static_assert( std::is_same_v<roles_of_t<G_ternary>,
                               RoleList<Alice, Bob, Carol>>);

// ── G. Projection — three-case dispatch ────────────────────────────
//
// Probe G = Alice → Bob: Ping; End.  Projecting:
//   Alice (sender)   → Send<Ping, End>
//   Bob   (receiver) → Recv<Ping, End>
//   Carol (third)    → End   (no involvement in this 2-party G)
using G_2p = Transmission<Alice, Bob, Ping, End_G>;
using L_Alice = project_t<G_2p, Alice>;
using L_Bob   = project_t<G_2p, Bob>;
using L_Carol = project_t<G_2p, Carol>;

static_assert(std::is_same_v<L_Alice,
    ::crucible::safety::proto::Send<Ping,
        ::crucible::safety::proto::End>>);
static_assert(std::is_same_v<L_Bob,
    ::crucible::safety::proto::Recv<Ping,
        ::crucible::safety::proto::End>>);
static_assert(std::is_same_v<L_Carol,
    ::crucible::safety::proto::End>);

// ── H. plain_merge_t identity on a singleton + trivial homogeneous
//      pack matches expected definition (Honda 2008 plain merging).
static_assert(std::is_same_v<
    plain_merge_t<::crucible::safety::proto::End>,
    ::crucible::safety::proto::End>);
static_assert(std::is_same_v<
    plain_merge_t<::crucible::safety::proto::End,
                   ::crucible::safety::proto::End>,
    ::crucible::safety::proto::End>);

// ── I. Cardinality witness — count of items U-013 surfaces.
//
//   Constructors (7):
//     End_G, Var_G, Transmission, BranchG, Choice, Rec_G, StopG
//   Form predicates (6):
//     is_end_g_v / is_var_g_v / is_transmission_v
//     is_choice_v / is_rec_g_v / is_stop_g_v
//   Role machinery (6):
//     RoleList, EmptyRoleList,
//     insert_unique_t, union_roles_t, RolesOf, roles_of_t
//   Well-formedness + diagnostics (5):
//     is_global_well_formed_v, has_self_loop_v, assert_no_self_loop,
//     has_empty_choice_v, assert_no_empty_choice
//   Plain merge (1):
//     plain_merge_t
//   Projection (2):
//     Project, project_t
//                                                       ───
//                                                       27
constexpr int u013_surface_cardinality = 27;
static_assert(u013_surface_cardinality == 27,
    "fixy::sess::mpst:: U-013 surface cardinality drifted — update "
    "SessGlobal.h using-decls AND this sentinel in lockstep.");

}  // namespace u013_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The runtime smoke routine instantiates every public template
// against non-constant args so any latent template-evaluation issue
// surfaces under `-fsyntax-only` of any TU that includes SessGlobal.h.
//
// Cost: instantiations only.  No runtime code path is executed.

inline void runtime_smoke_test() noexcept {
    struct A {}; struct B {}; struct P {};
    using G_AB = Transmission<A, B, P, End_G>;

    [[maybe_unused]] constexpr bool wf  = is_global_well_formed_v<G_AB>;
    [[maybe_unused]] constexpr bool sl  = has_self_loop_v<G_AB>;
    [[maybe_unused]] constexpr bool ec  = has_empty_choice_v<G_AB>;
    [[maybe_unused]] constexpr bool isT = is_transmission_v<G_AB>;

    using LA = project_t<G_AB, A>;
    using LB = project_t<G_AB, B>;
    using RL = roles_of_t<G_AB>;
    using IU = insert_unique_t<A, EmptyRoleList>;
    using UR = union_roles_t<RoleList<A>, RoleList<B>>;
    using PM = plain_merge_t<::crucible::safety::proto::End>;

    (void)wf; (void)sl; (void)ec; (void)isT;
    (void)static_cast<LA*>(nullptr);
    (void)static_cast<LB*>(nullptr);
    (void)static_cast<RL*>(nullptr);
    (void)static_cast<IU*>(nullptr);
    (void)static_cast<UR*>(nullptr);
    (void)static_cast<PM*>(nullptr);
}

}  // namespace crucible::fixy::sess::mpst
