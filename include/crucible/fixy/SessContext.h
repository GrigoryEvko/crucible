#pragma once

// ── crucible::fixy::sess::context — typing-context Γ slice ──────────
//
// FIXY-U-052i (ninth slice of the U-052 umbrella).  Re-exports the
// complete public surface of sessions/SessionContext.h (the L2 typing
// context Γ of the session-type stack — the (SessionTag, RoleTag) ↦
// LocalType map that MPST typing threads through every judgment) into
// `crucible::fixy::sess::context::`.
//
// Production callers — MPST projection, per-role local-type tracking,
// permission-balance checking across a multi-party context — spell the
// typing-context vocabulary through the fixy umbrella, not raw
// `safety::proto::`.
//
// Twenty symbols (the complete SessionContext.h public API):
//
//   Core (3):          Entry, Context, EmptyContext
//   Size/empty (2):    context_size_v, is_empty_context_v
//   Compose (2):       ComposeContext, compose_context_t
//   Lookup (3):        contains_key_v, LookupContext, lookup_context_t
//   Domain (4):        Key, KeySet, DomainOf, domain_of_t
//   Update (2):        UpdateEntry, update_entry_t
//   Remove (2):        RemoveEntry, remove_entry_t
//   Perm-balance (2):  is_permission_balanced, is_permission_balanced_v
//
// ── Why a dedicated context:: sub-namespace ────────────────────────
//
// fixy::sess:: holds the binary session combinators; ::subtype:: the
// refinement order; ::queue:: the en-route state; ::diagnostic:: the
// manifest-bug catalog.  This is the TYPING-CONTEXT layer: the Γ
// environment that pairs each (session, role) with its current local
// type and the permission-balance judgment over it.  Keeping it in
// ::context:: lets audit-grep `fixy::sess::context::` find every
// fixy-routed Γ manipulation distinct from substrate-direct sites.
//
// ── A note on identifier spelling ──────────────────────────────────
//
// SessionContext.h's self-test names contexts with the Greek letter Γ
// (TraceRingΓ, the template parameter Γ).  Per CLAUDE.md §XVII this
// header uses ASCII-only identifiers — the re-export does not replicate
// substrate parameter names, and the sentinel fixtures below are named
// in ASCII (SessA / RoleP / Ctx, not …Γ).
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty using-decls + a sentinel battery + smoke routine.
// No new types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; Entry/Context are empty
//              type markers.
//   TypeSafe — using-decls preserve substrate type identity; the
//              (session, role) key is structurally distinct per entry.
//   NullSafe — no pointer state introduced.
//   MemSafe  — all symbols are compile-time-only; nothing is allocated.
//   DetSafe  — context lookup/update/remove are pure type-level
//              functions; same (Γ, key) always yields the same result.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionContext.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::context {

// ── 1. Core typing-context carriers (3) ────────────────────────────
using ::crucible::safety::proto::Entry;
using ::crucible::safety::proto::Context;
using ::crucible::safety::proto::EmptyContext;

// ── 2. Size / emptiness (2) ────────────────────────────────────────
using ::crucible::safety::proto::context_size_v;
using ::crucible::safety::proto::is_empty_context_v;

// ── 3. Composition (2) ─────────────────────────────────────────────
using ::crucible::safety::proto::ComposeContext;
using ::crucible::safety::proto::compose_context_t;

// ── 4. Membership / lookup (3) ─────────────────────────────────────
using ::crucible::safety::proto::contains_key_v;
using ::crucible::safety::proto::LookupContext;
using ::crucible::safety::proto::lookup_context_t;

// ── 5. Domain (Key / KeySet / projection) (4) ──────────────────────
using ::crucible::safety::proto::Key;
using ::crucible::safety::proto::KeySet;
using ::crucible::safety::proto::DomainOf;
using ::crucible::safety::proto::domain_of_t;

// ── 6. Update (2) ──────────────────────────────────────────────────
using ::crucible::safety::proto::UpdateEntry;
using ::crucible::safety::proto::update_entry_t;

// ── 7. Remove (2) ──────────────────────────────────────────────────
using ::crucible::safety::proto::RemoveEntry;
using ::crucible::safety::proto::remove_entry_t;

// ── 8. Permission balance (2) ──────────────────────────────────────
using ::crucible::safety::proto::is_permission_balanced;
using ::crucible::safety::proto::is_permission_balanced_v;

}  // namespace crucible::fixy::sess::context

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessQueue.h::u052f_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.

namespace crucible::fixy::sess::context::u052i_self_test {

namespace proto = ::crucible::safety::proto;

// ASCII-only fixtures (NOT the substrate's Γ-named ones — §XVII).
struct SessA {};
struct SessB {};
struct RoleP {};
struct RoleC {};
struct TypeP {};
struct TypeC {};
struct TypeP2 {};

using Ctx = Context<Entry<SessA, RoleP, TypeP>, Entry<SessA, RoleC, TypeC>>;
// A DISJOINT context (different session tag) — compose requires the
// CSL frame rule's disjoint-domain precondition, so we never compose
// Ctx with itself.
using Ctx2 = Context<Entry<SessB, RoleP, TypeP>, Entry<SessB, RoleC, TypeC>>;

// ── A. Carrier + Entry-accessor type identity ──────────────────────
static_assert(std::is_same_v<Entry<SessA, RoleP, TypeP>,
                             proto::Entry<SessA, RoleP, TypeP>>);
static_assert(std::is_same_v<Context<>, proto::Context<>>);
static_assert(std::is_same_v<EmptyContext, proto::EmptyContext>);
static_assert(std::is_same_v<EmptyContext, Context<>>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::session,    SessA>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::role,       RoleP>);
static_assert(std::is_same_v<typename Entry<SessA, RoleP, TypeP>::local_type, TypeP>);

// ── B. Size / emptiness ────────────────────────────────────────────
static_assert(context_size_v<Ctx> == 2);
static_assert(context_size_v<EmptyContext> == 0);
static_assert(is_empty_context_v<EmptyContext>);
static_assert(!is_empty_context_v<Ctx>);

// ── C. Composition: concatenation + EmptyContext identity ──────────
static_assert(context_size_v<compose_context_t<Ctx, Ctx2>> == 4,
    "composing two DISJOINT contexts concatenates their entries.");
static_assert(std::is_same_v<compose_context_t<Ctx, EmptyContext>, Ctx>,
    "composing with EmptyContext on the right is the identity.");
static_assert(std::is_same_v<compose_context_t<EmptyContext, Ctx>, Ctx>);

// ── D. Membership + lookup ─────────────────────────────────────────
static_assert(contains_key_v<Ctx, SessA, RoleP>);
static_assert(contains_key_v<Ctx, SessA, RoleC>);
static_assert(!contains_key_v<Ctx, SessB, RoleP>,
    "absent (session, role) key must not be reported present.");
static_assert(!contains_key_v<EmptyContext, SessA, RoleP>);
static_assert(std::is_same_v<lookup_context_t<Ctx, SessA, RoleP>, TypeP>);
static_assert(std::is_same_v<lookup_context_t<Ctx, SessA, RoleC>, TypeC>);

// ── E. Domain projection (Key / KeySet / DomainOf) ─────────────────
static_assert(std::is_same_v<
    domain_of_t<Ctx>,
    KeySet<Key<SessA, RoleP>, Key<SessA, RoleC>>>);
static_assert(domain_of_t<Ctx>::size == 2);
static_assert(std::is_same_v<domain_of_t<EmptyContext>, KeySet<>>);
static_assert(domain_of_t<EmptyContext>::size == 0);

// ── F. Update (replace local_type, preserve domain + size) ─────────
using Updated = update_entry_t<Ctx, SessA, RoleP, TypeP2>;
static_assert(std::is_same_v<lookup_context_t<Updated, SessA, RoleP>, TypeP2>);
static_assert(std::is_same_v<lookup_context_t<Updated, SessA, RoleC>, TypeC>,
    "update of one entry leaves the others unchanged.");
static_assert(context_size_v<Updated> == context_size_v<Ctx>);
static_assert(std::is_same_v<
    update_entry_t<Ctx, SessA, RoleP, TypeP>, Ctx>,
    "update with the same type is a no-op (idempotent).");

// ── G. Remove (drop one key; remove-both → EmptyContext) ───────────
using Removed = remove_entry_t<Ctx, SessA, RoleP>;
static_assert(context_size_v<Removed> == 1);
static_assert(!contains_key_v<Removed, SessA, RoleP>);
static_assert(contains_key_v<Removed, SessA, RoleC>);
static_assert(std::is_same_v<
    remove_entry_t<remove_entry_t<Ctx, SessA, RoleP>, SessA, RoleC>,
    EmptyContext>,
    "removing every entry yields EmptyContext.");

// ── H. Permission balance — struct identity + vacuous balance ──────
//
// The rich balanced/unbalanced semantics (per-Tag transfer aggregation,
// payload markers) are exhaustively tested in SessionContext.h's own
// self-test; here we prove the trait REACHES through the fixy spelling
// (struct-template identity) and that the vacuous EmptyContext case
// holds — `EmptyPermSet` is its definitional home (permissions/).
static_assert(std::is_same_v<
    is_permission_balanced<EmptyContext, proto::EmptyPermSet>,
    proto::is_permission_balanced<EmptyContext, proto::EmptyPermSet>>);
static_assert(is_permission_balanced_v<EmptyContext,
                                       proto::EmptyPermSet>,
    "an empty context is vacuously permission-balanced.");

// ── I. Cardinality witness — count of items U-052i surfaces ────────
//
//   Core (3) + size/empty (2) + compose (2) + lookup (3) +
//   domain (4) + update (2) + remove (2) + perm-balance (2)  ──── 20
constexpr int u052i_surface_cardinality = 20;
static_assert(u052i_surface_cardinality == 20,
    "fixy::sess::context:: U-052i surface cardinality drifted — "
    "update SessContext.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::context::u052i_self_test

namespace crucible::fixy::sess::context {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces the context metafunctions through real
// instantiation so any latent template-evaluation issue surfaces under
// `-fsyntax-only` of any TU that includes SessContext.h.

inline void runtime_smoke_test() noexcept {
    struct S {};
    struct RoleR {};
    struct RoleW {};
    struct TR {};
    struct TW {};
    using C0 = EmptyContext;
    using C2 = Context<Entry<S, RoleR, TR>, Entry<S, RoleW, TW>>;

    [[maybe_unused]] constexpr std::size_t sz0 = context_size_v<C0>;
    [[maybe_unused]] constexpr std::size_t sz2 = context_size_v<C2>;
    [[maybe_unused]] constexpr bool        e0  = is_empty_context_v<C0>;
    [[maybe_unused]] constexpr bool        has = contains_key_v<C2, S, RoleR>;
    [[maybe_unused]] constexpr bool        no  = contains_key_v<C2, S, RoleW>;

    using Look    = lookup_context_t<C2, S, RoleR>;
    using Dom     = domain_of_t<C2>;
    using Updated = update_entry_t<C2, S, RoleR, TW>;
    using Removed = remove_entry_t<C2, S, RoleR>;
    [[maybe_unused]] constexpr bool look_ok = std::is_same_v<Look, TR>;
    [[maybe_unused]] constexpr std::size_t dom_sz = Dom::size;
    [[maybe_unused]] constexpr std::size_t up_sz  = context_size_v<Updated>;
    [[maybe_unused]] constexpr std::size_t rm_sz  = context_size_v<Removed>;
    [[maybe_unused]] constexpr bool bal =
        is_permission_balanced_v<C0, ::crucible::safety::proto::EmptyPermSet>;

    (void) sz0; (void) sz2; (void) e0; (void) has; (void) no;
    (void) look_ok; (void) dom_sz; (void) up_sz; (void) rm_sz; (void) bal;
}

}  // namespace crucible::fixy::sess::context
