#pragma once

// ── crucible::fixy::sess::declassify — DeclassifyOnSend wire surface ─
//
// FIXY-U-052a (first slice of U-052 umbrella).  Re-exports the
// `safety::DeclassifyOnSend<T, Policy>` payload wrapper + traits +
// concept from sessions/SessionDeclassify.h into
// `crucible::fixy::sess::declassify::` so production callers can
// spell wire-classification payloads through the fixy umbrella
// without descending into the substrate.
//
// One axis surfaced (the wire-policy payload-marker layer):
//
//   1. DeclassifyOnSend<T, Policy>          — wire-policy payload wrapper
//   2. is_declassify_on_send<T>             — primary trait
//      is_declassify_on_send_v<T>           — value alias
//   3. DeclassifyOnSendable<T>              — concept gate
//   4. wire_payload_type<T>                 — primary metafn (extract T)
//      wire_payload_type_t<T>               — alias
//   5. wire_policy<T>                       — primary metafn (extract Policy)
//      wire_policy_t<T>                     — alias
//
// ── Why a dedicated declassify:: sub-namespace ────────────────────
//
// fixy::sess:: holds the binary session-type surface (Send / Recv /
// Loop / Select + the various Handle wrappers); fixy::sess::mpst::
// holds the MPST global-types layer.  DeclassifyOnSend is a PAYLOAD
// MARKER — it lives on the per-message level and composes with Send /
// Recv via the existing payload covariance rules (no new
// SessionHandle specialisation).  Keeping it in its own sub-namespace
// mirrors safety::DeclassifyOnSend's organisation
// (sessions/SessionDeclassify.h is one self-contained substrate
// header per misc/24_04_2026_safety_integration.md §13) and lets
// the audit-grep `fixy::sess::declassify::` find every fixy-routed
// wire-classification site distinct from the substrate's grep
// `safety::DeclassifyOnSend<`.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::DeclassifyOnSend<T, Policy>      — payload wrapper
//   safety::is_declassify_on_send<T>         — primary trait
//   safety::is_declassify_on_send_v<T>       — value alias
//   safety::DeclassifyOnSendable<T>          — concept
//   safety::wire_payload_type<T>             — metafn (with passthrough fallback)
//   safety::wire_payload_type_t<T>           — alias
//   safety::wire_policy<T>                   — metafn (no fallback)
//   safety::wire_policy_t<T>                 — alias
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Eight using-decls + a sentinel battery + smoke routine.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl.  `sizeof(DeclassifyOnSend<T, P>)
// == sizeof(T)` per the substrate's regime-1 EBO collapse claim
// (asserted at detail::declassify_size_test in SessionDeclassify.h).

#include <crucible/sessions/SessionDeclassify.h>

#include <type_traits>

namespace crucible::fixy::sess::declassify {

// ═════════════════════════════════════════════════════════════════════
// ── 1. DeclassifyOnSend<T, Policy> ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::DeclassifyOnSend;

// ═════════════════════════════════════════════════════════════════════
// ── 2. Shape predicates (primary + value alias) ────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::is_declassify_on_send;
using ::crucible::safety::is_declassify_on_send_v;

// ═════════════════════════════════════════════════════════════════════
// ── 3. Concept gate ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::DeclassifyOnSendable;

// ═════════════════════════════════════════════════════════════════════
// ── 4. Wire-payload metafunctions (with passthrough fallback) ──────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::wire_payload_type;
using ::crucible::safety::wire_payload_type_t;

// ═════════════════════════════════════════════════════════════════════
// ── 5. Wire-policy metafunctions (no fallback — DeclassifyOnSend only)
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::wire_policy;
using ::crucible::safety::wire_policy_t;

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Same dual-export discipline as fixy/Mpst.h::u013_self_test +
// fixy/Rules.h::u062_self_test.  Drift between the substrate's
// DeclassifyOnSend surface and the fixy projection here trips at
// every consumer's include time — NOT only inside a downstream test
// TU.

namespace u052a_self_test {

// ── A. Payload + policy placeholders ───────────────────────────────
struct Token { int v = 0; };
struct Other {};

using TokenWire  = DeclassifyOnSend<Token,
    ::crucible::safety::secret_policy::WireSerialize>;
using TokenAudit = DeclassifyOnSend<Token,
    ::crucible::safety::secret_policy::AuditedLogging>;

// ── B. Re-export type-identity witness (dual-export sentinel) ──────
//
// Every using-decl preserves the substrate type identity bit-for-bit.
// If a substrate symbol is moved out from under us (renamed, removed,
// or relocated to a different namespace), the next line fails to
// compile with a recognisable substrate-rename diagnostic.

static_assert(std::is_same_v<
    DeclassifyOnSend<Token,
        ::crucible::safety::secret_policy::WireSerialize>,
    ::crucible::safety::DeclassifyOnSend<Token,
        ::crucible::safety::secret_policy::WireSerialize>>,
    "fixy::sess::declassify::DeclassifyOnSend must alias "
    "safety::DeclassifyOnSend.");

// ── C. Shape predicates discriminate wrapper vs non-wrapper ────────
static_assert( is_declassify_on_send_v<TokenWire>);
static_assert( is_declassify_on_send_v<TokenAudit>);
static_assert(!is_declassify_on_send_v<Token>);
static_assert(!is_declassify_on_send_v<int>);

// ── D. Concept gate routes through the umbrella ────────────────────
static_assert( DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<Token>);

// ── E. wire_payload_type_t extracts the inner T (with fallback) ────
//
// The metafunction has a fallback that passes non-DeclassifyOnSend
// types unchanged (so generic transport code can use it uniformly).
// Both branches must reach through the umbrella.
static_assert(std::is_same_v<wire_payload_type_t<TokenWire>,  Token>);
static_assert(std::is_same_v<wire_payload_type_t<TokenAudit>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<Token>,      Token>);
static_assert(std::is_same_v<wire_payload_type_t<int>,        int>);

// ── F. wire_policy_t extracts the policy tag ──────────────────────
//
// Defined ONLY for DeclassifyOnSend specialisations (no fallback —
// asking for the wire-policy of a bare payload is a category error).
static_assert(std::is_same_v<wire_policy_t<TokenWire>,
    ::crucible::safety::secret_policy::WireSerialize>);
static_assert(std::is_same_v<wire_policy_t<TokenAudit>,
    ::crucible::safety::secret_policy::AuditedLogging>);

// ── G. Distinct-policy wrapper distinctness ────────────────────────
//
// Different policies on the SAME T are unrelated wrapper types — the
// policy tag is part of the wrapper identity, so silently flowing
// TokenWire to a TokenAudit-position would defeat the audit-
// discoverability of `grep DeclassifyOnSend<T, P>`.  Pinned at the
// fixy layer so a future substrate refactor that accidentally
// unified them breaks this header BEFORE the diagnostic propagates
// into Send/Recv call sites.
static_assert(!std::is_same_v<TokenWire, TokenAudit>);

// ── H. Move-only discipline preserved across re-export ─────────────
//
// DeclassifyOnSend wraps Secret<T>; both are move-only by design.
// If a future refactor accidentally promoted DeclassifyOnSend to
// copyable, classified values would silently duplicate at the wire-
// payload boundary — defeating the security promise of Secret<T>.
static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert(!std::is_copy_assignable_v<TokenWire>);
static_assert( std::is_move_constructible_v<TokenWire>);
static_assert( std::is_move_assignable_v<TokenWire>);

// ── I. Cardinality witness — count of items U-052a surfaces.
//
//   Wrapper (1):
//     DeclassifyOnSend
//   Shape predicates (2):
//     is_declassify_on_send, is_declassify_on_send_v
//   Concept (1):
//     DeclassifyOnSendable
//   Wire-payload metafns (2):
//     wire_payload_type, wire_payload_type_t
//   Wire-policy metafns (2):
//     wire_policy, wire_policy_t
//                                                       ───
//                                                        8
constexpr int u052a_surface_cardinality = 8;
static_assert(u052a_surface_cardinality == 8,
    "fixy::sess::declassify:: U-052a surface cardinality drifted — "
    "update SessDecl.h using-decls AND this sentinel in lockstep.");

}  // namespace u052a_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The runtime smoke routine instantiates every public template
// against non-constant args so any latent template-evaluation issue
// surfaces under `-fsyntax-only` of any TU that includes SessDecl.h.
//
// Cost: instantiations only.  No runtime code path is executed.

inline void runtime_smoke_test() noexcept {
    struct Payload { int v = 0; };
    using P = DeclassifyOnSend<Payload,
        ::crucible::safety::secret_policy::WireSerialize>;

    [[maybe_unused]] constexpr bool isW       = is_declassify_on_send_v<P>;
    [[maybe_unused]] constexpr bool isNotWrap = is_declassify_on_send_v<int>;
    [[maybe_unused]] constexpr bool cap       = DeclassifyOnSendable<P>;

    using PayloadT = wire_payload_type_t<P>;
    using PolicyT  = wire_policy_t<P>;
    using PassT    = wire_payload_type_t<int>;

    (void)isW; (void)isNotWrap; (void)cap;
    (void)static_cast<PayloadT*>(nullptr);
    (void)static_cast<PolicyT*>(nullptr);
    (void)static_cast<PassT*>(nullptr);
}

}  // namespace crucible::fixy::sess::declassify
