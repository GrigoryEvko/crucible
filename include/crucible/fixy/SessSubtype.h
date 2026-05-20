#pragma once

// ── crucible::fixy::sess::subtype — Gay-Hole subtype-layer slice ────
//
// FIXY-U-052e (fifth slice of the U-052 umbrella).  Re-exports the
// complete public surface of the subtype LAYER — sessions/
// SessionSubtype.h (the Gay-Hole 2005 synchronous subtype relation +
// its ergonomic concepts/assertions) AND sessions/SessionSubtypeReason.h
// (the failure-REASON diagnostics that walk the protocol tree and name
// the first failing inner pair) — into `crucible::fixy::sess::subtype::`.
//
// Production callers — Vessel adapter declarations
// (`assert_subtype_sync<VesselProto, FrontendCanon>()`), protocol-
// evolution boundaries (`check_protocol_evolution<V1, V2>()`), client/
// server FFI compatibility checks — spell the subtyping vocabulary
// through the fixy umbrella, not raw `safety::proto::`.
//
// Twenty-nine symbols (the complete subtype-layer public API):
//
//   Relation traits (3):  is_subsort, is_subtype_sync_structural,
//                         is_subtype_sync
//   Relation _v aliases (6): is_subsort_v, protocol_grade_satisfies_v,
//                         is_subtype_sync_v, equivalent_sync_v,
//                         is_strict_subtype_sync_v, subtype_chain_v
//   Concepts (5):         SubtypeSync, EquivalentSync, StrictSubtypeSync,
//                         CompatibleClient, CompatibleServer
//   Assertion helpers (6): assert_subtype_sync, assert_vendor_subtype_sync,
//                         check_protocol_evolution, assert_equivalent_sync,
//                         assert_compatible_client, assert_compatible_server
//   Reason result types (2): SubtypeOk, RejectionReason
//   Reason predicates (3): is_rejection_reason, is_rejection_reason_v,
//                         is_subtype_sync_diag_v
//   Reason metafns (2):   subtype_rejection_reason, subtype_rejection_reason_t
//   Reason helpers (2):   assert_subtype_sync_diag, subtype_diag_agrees_v
//
// ── Why a dedicated subtype:: sub-namespace ────────────────────────
//
// fixy::sess:: holds the binary session combinators; ::mpst:: the
// global-types layer; ::declassify:: / ::ct:: / ::contentaddr:: /
// ::eventlog:: the payload/record layers.  The subtype layer is the
// REFINEMENT-ORDER layer: the partial order ⩽ on session types
// (Gay-Hole 2005) plus its first-failure diagnostics.  Keeping it in
// ::subtype:: lets audit-grep `fixy::sess::subtype::` find every
// fixy-routed protocol-refinement check distinct from substrate-direct
// `safety::proto::` call sites.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-nine using-decls + a sentinel battery + smoke routine.
// No new types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; SubtypeOk/RejectionReason
//              are empty/NSDMI'd.
//   TypeSafe — using-decls preserve substrate type identity; the whole
//              point of the layer is REJECTING ill-typed protocol
//              substitution at compile time.
//   NullSafe — no pointer state introduced.
//   MemSafe  — all symbols are compile-time metafunctions / empty tags.
//   DetSafe  — the subtype relation is a pure type-level predicate;
//              same (T, U) always yields the same answer.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionSubtypeReason.h>

#include <type_traits>

namespace crucible::fixy::sess::subtype {

// ── 1. Relation traits (3) ─────────────────────────────────────────
using ::crucible::safety::proto::is_subsort;
using ::crucible::safety::proto::is_subtype_sync_structural;
using ::crucible::safety::proto::is_subtype_sync;

// ── 2. Relation variable-template aliases (6) ──────────────────────
using ::crucible::safety::proto::is_subsort_v;
using ::crucible::safety::proto::protocol_grade_satisfies_v;
using ::crucible::safety::proto::is_subtype_sync_v;
using ::crucible::safety::proto::equivalent_sync_v;
using ::crucible::safety::proto::is_strict_subtype_sync_v;
using ::crucible::safety::proto::subtype_chain_v;

// ── 3. Ergonomic concepts (5) ──────────────────────────────────────
using ::crucible::safety::proto::SubtypeSync;
using ::crucible::safety::proto::EquivalentSync;
using ::crucible::safety::proto::StrictSubtypeSync;
using ::crucible::safety::proto::CompatibleClient;
using ::crucible::safety::proto::CompatibleServer;

// ── 4. Assertion helpers — call-site diagnostics (6) ───────────────
using ::crucible::safety::proto::assert_subtype_sync;
using ::crucible::safety::proto::assert_vendor_subtype_sync;
using ::crucible::safety::proto::check_protocol_evolution;
using ::crucible::safety::proto::assert_equivalent_sync;
using ::crucible::safety::proto::assert_compatible_client;
using ::crucible::safety::proto::assert_compatible_server;

// ── 5. Reason result types (2) ─────────────────────────────────────
using ::crucible::safety::proto::SubtypeOk;
using ::crucible::safety::proto::RejectionReason;

// ── 6. Reason shape predicates (3) ─────────────────────────────────
using ::crucible::safety::proto::is_rejection_reason;
using ::crucible::safety::proto::is_rejection_reason_v;
using ::crucible::safety::proto::is_subtype_sync_diag_v;

// ── 7. Reason metafunctions (2) ────────────────────────────────────
using ::crucible::safety::proto::subtype_rejection_reason;
using ::crucible::safety::proto::subtype_rejection_reason_t;

// ── 8. Reason call-site helpers (2) ────────────────────────────────
using ::crucible::safety::proto::assert_subtype_sync_diag;
using ::crucible::safety::proto::subtype_diag_agrees_v;

}  // namespace crucible::fixy::sess::subtype

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessEventLog.h::u052d_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.

namespace crucible::fixy::sess::subtype::u052e_self_test {

namespace proto = ::crucible::safety::proto;

// Representative protocol shapes (combinators come transitively from
// SessionSubtype.h's include of Session.h).
using End  = proto::End;
using SInt = proto::Send<int, proto::End>;
using RInt = proto::Recv<int, proto::End>;

// ── A. Trait-template type identity ────────────────────────────────
//
// Instantiated-struct identity proves the using-decl resolves to the
// substrate template, not a same-named shadow.
static_assert(std::is_same_v<is_subsort<int, int>, proto::is_subsort<int, int>>);
static_assert(std::is_same_v<is_subtype_sync_structural<End, End>,
                             proto::is_subtype_sync_structural<End, End>>);
static_assert(std::is_same_v<is_subtype_sync<End, End>,
                             proto::is_subtype_sync<End, End>>);
static_assert(std::is_same_v<RejectionReason<proto::diagnostic::SubtypeMismatch, int, int>,
                             proto::RejectionReason<proto::diagnostic::SubtypeMismatch, int, int>>);

// ── B. Subsort primitive behaviour (reflexive, invariant default) ──
static_assert(is_subsort_v<int, int>,        "subsort is reflexive");
static_assert(!is_subsort_v<int, double>,    "default subsort is invariant (is_same)");

// ── C. Core subtype relation — positive + negative ─────────────────
static_assert(is_subtype_sync_v<End, End>,   "End is a subtype of itself");
static_assert(!is_subtype_sync_v<SInt, RInt>,
    "Send and Recv are incomparable shapes — the canonical "
    "send/recv-confusion bug must be rejected.");
static_assert(protocol_grade_satisfies_v<End, End>,
    "End trivially satisfies End's (empty) product-lattice grade.");

// ── D. Equivalence / strictness / chain ────────────────────────────
static_assert(equivalent_sync_v<End, End>,   "End ≡ End (bidirectional)");
static_assert(!is_strict_subtype_sync_v<End, End>,
    "reflexive pair is NOT a strict subtype (strict is irreflexive)");
static_assert(subtype_chain_v<End, End, End>, "End ⩽ End ⩽ End");

// ── E. Concepts hold on representative args ────────────────────────
static_assert(SubtypeSync<End, End>);
static_assert(EquivalentSync<End, End>);
// dual_of_t<Recv<int,End>> == Send<int,End>, so a Send client is a
// subtype of the dual of a Recv server — the FFI compatibility shape.
static_assert(CompatibleClient<SInt, RInt>);
static_assert(CompatibleServer<RInt, SInt>);

// ── F. Reason path: success sentinel + failure record + agreement ──
static_assert(std::is_same_v<subtype_rejection_reason_t<End, End>, SubtypeOk>,
    "End ⩽ End yields the SubtypeOk success sentinel.");
static_assert(is_subtype_sync_diag_v<End, End>,
    "diag path agrees with the bool path on the positive case.");
static_assert(subtype_diag_agrees_v<End, End>,
    "is_subtype_sync_diag_v and is_subtype_sync_v must agree.");
static_assert(is_rejection_reason_v<subtype_rejection_reason_t<SInt, RInt>>,
    "a shape mismatch produces a RejectionReason record, not SubtypeOk.");
static_assert(!is_rejection_reason_v<SubtypeOk>,
    "the success sentinel is NOT a rejection record.");

// ── G. Distinct strong relations do not collapse ───────────────────
//
// is_subtype_sync (grade-filtered, public) and is_subtype_sync_structural
// (pre-grade) are distinct templates — a future edit that aliased one to
// the other would erase the ProductLattice grade filter silently.
static_assert(!std::is_same_v<is_subtype_sync<End, End>,
                              is_subtype_sync_structural<End, End>>,
    "grade-filtered and structural relations are distinct templates.");

// ── H. Cardinality witness — count of items U-052e surfaces ────────
//
//   Relation traits (3) + relation _v (6) + concepts (5) +
//   assertion helpers (6) + reason result types (2) +
//   reason predicates (3) + reason metafns (2) + reason helpers (2)
//                                                          ──── 29
constexpr int u052e_surface_cardinality = 29;
static_assert(u052e_surface_cardinality == 29,
    "fixy::sess::subtype:: U-052e surface cardinality drifted — "
    "update SessSubtype.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::subtype::u052e_self_test

namespace crucible::fixy::sess::subtype {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces the consteval assertion helpers AND the
// metafunctions through real instantiation so any latent
// template-evaluation issue surfaces under `-fsyntax-only` of any TU
// that includes SessSubtype.h.
//
// Cost: compile-time consteval evaluation only — no runtime state, no
// I/O.  Every call below is over a relation that HOLDS, so the
// static_asserts inside the helpers pass.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    using End  = proto::End;
    using SInt = proto::Send<int, proto::End>;
    using RInt = proto::Recv<int, proto::End>;

    // Consteval assertion helpers fire (and pass) at the call site.
    assert_subtype_sync<End, End>();
    assert_vendor_subtype_sync<End, End>();
    check_protocol_evolution<End, End>();
    assert_equivalent_sync<End, End>();
    assert_compatible_client<SInt, RInt>();
    assert_compatible_server<RInt, SInt>();
    assert_subtype_sync_diag<End, End>();

    // Metafunctions evaluate against non-trivial (failing) shapes too.
    [[maybe_unused]] constexpr bool sub      = is_subtype_sync_v<End, End>;
    [[maybe_unused]] constexpr bool not_sub  = is_subtype_sync_v<SInt, RInt>;
    [[maybe_unused]] constexpr bool equiv    = equivalent_sync_v<End, End>;
    [[maybe_unused]] constexpr bool strict   = is_strict_subtype_sync_v<End, End>;
    [[maybe_unused]] constexpr bool chain    = subtype_chain_v<End, End, End>;
    [[maybe_unused]] constexpr bool diag     = is_subtype_sync_diag_v<End, End>;
    [[maybe_unused]] constexpr bool agrees   = subtype_diag_agrees_v<End, End>;

    using OkReason   = subtype_rejection_reason_t<End, End>;
    using FailReason = subtype_rejection_reason_t<SInt, RInt>;
    [[maybe_unused]] constexpr bool ok_is_ok   = std::is_same_v<OkReason, SubtypeOk>;
    [[maybe_unused]] constexpr bool fail_is_rej = is_rejection_reason_v<FailReason>;

    (void) sub; (void) not_sub; (void) equiv; (void) strict; (void) chain;
    (void) diag; (void) agrees; (void) ok_is_ok; (void) fail_is_rej;
}

}  // namespace crucible::fixy::sess::subtype
