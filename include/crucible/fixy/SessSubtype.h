#pragma once

// ── crucible::fixy::sess::subtype — Gay-Hole subtype-layer slice ────
//
// FIXY-U-052e (fifth slice of the U-052 umbrella) + FIXY-V-067 merge.
// Re-exports the complete public surface of the subtype LAYER —
// sessions/SessionSubtype.h (the Gay-Hole 2005 synchronous subtype
// relation + its ergonomic concepts/assertions) AND
// sessions/SessionSubtypeReason.h (the failure-REASON diagnostics
// that walk the protocol tree and name the first failing inner pair)
// — into `crucible::fixy::sess::subtype::`.
//
// **V-067 consolidation**: also OWNS the payload-subsort axiom
// witness battery formerly split into fixy/SessPayloadSubsort.h
// (deleted 2026-05-22).  SessionPayloadSubsort.h ships ONLY
// specialisations of `is_subsort<...>` (the payload-level subtype
// relation), no new names — its only effect through the umbrella
// is VISIBILITY.  Pulling SessionPayloadSubsort.h alongside
// SessionSubtype.h guarantees that umbrella consumers reaching
// `fixy::sess::subtype::is_subsort_v<Refined<P,T>, T>` resolve the
// NARROWING specialisation (true), not the bare primary template
// (false) which would silently lie.
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

#include <crucible/safety/NumericalTier.h>       // V-067: payload axiom NumericalTier
#include <crucible/sessions/SessionPayloadSubsort.h>  // V-067: is_subsort<...> specialisations
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionSubtypeReason.h>

#include <cstddef>
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

// ═════════════════════════════════════════════════════════════════════
// ── V-067 payload-subsort axiom witness battery ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Merged from the now-deleted fixy/SessPayloadSubsort.h on 2026-05-22.
// SessionPayloadSubsort.h ships only `is_subsort<...>` partial
// specialisations — NO new names.  The witnesses below re-prove the
// COMPLETE shipped axiom set AND the deliberately-absent (asymmetry-
// discipline) axioms THROUGH the fixy spelling
// (`fixy::sess::subtype::is_subsort_v` / `is_subtype_sync_v`), so a
// substrate regression that drops a specialisation — or silently adds
// an unsafe one (External/FromUser/FromPytorch flowing to bare T) —
// trips at every consumer's include time.
//
// ── The shipped payload-subsort axioms (witnessed below) ───────────
//
//   Positive (flow holds):
//     Refined<P, T> ⩽ T                              (narrowing)
//     Refined<P, T> ⩽ Refined<Q, T>  when P ⇒ Q      (strengthening)
//     Tagged<T, source::{Sanitized,FromInternal,FromConfig,
//                        FromDb,Durable,Computed}> ⩽ T
//     Tagged<T, vessel_trust::Validated> ⩽ T
//     Refined<P, Tagged<T, V>> ⩽ Tagged<T, V>        (stacked)
//     NumericalTier<tight, P> ⩽ NumericalTier<loose, P>   (tolerance)
//
//   Deliberately ABSENT (must stay false — the discipline):
//     Tagged<T, source::External / source::FromUser /
//             vessel_trust::FromPytorch> ⩽ T          -- trust boundary
//     Tagged<T, trust::* / access::* / version::*> ⩽ T -- epistemic
//     T ⩽ Refined<P, T>, T ⩽ Tagged<T, V>             -- no proof reverse

namespace crucible::fixy::sess::subtype::v067_payload_axiom_test {

namespace saf  = ::crucible::safety;
namespace prot = ::crucible::safety::proto;
namespace fsub = ::crucible::fixy::sess::subtype;

struct Payload { int v; };

// ── A. Refined<P, T> ⩽ T (narrowing) + reverse rejected ────────────
static_assert(fsub::is_subsort_v<saf::Refined<saf::positive, int>, int>);
static_assert(fsub::is_subsort_v<saf::Refined<saf::non_negative, int>, int>);
static_assert(fsub::is_subsort_v<saf::Refined<saf::non_zero, int>, int>);
static_assert(!fsub::is_subsort_v<int, saf::Refined<saf::positive, int>>,
    "a bare T has no proof — reverse narrowing must be rejected.");

// ── B. Refined strengthening: P ⇒ Q  ⟹  Refined<P> ⩽ Refined<Q> ───
static_assert(fsub::is_subsort_v<saf::Refined<saf::positive, int>,
                                 saf::Refined<saf::non_negative, int>>,
    "positive ⇒ non_negative, so the stronger refinement flows.");
static_assert(!fsub::is_subsort_v<saf::Refined<saf::non_negative, int>,
                                  saf::Refined<saf::positive, int>>,
    "the weaker refinement does NOT flow to the stronger position.");

// ── C. Safe-to-erase provenance tags ⩽ bare T ──────────────────────
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Sanitized>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromInternal>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromConfig>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromDb>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Durable>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::source::Computed>, Payload>);
static_assert(fsub::is_subsort_v<saf::Tagged<Payload, saf::vessel_trust::Validated>, Payload>);

// ── D. Trust-boundary discipline: unsafe tags must NOT flow ────────
static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::source::External>, Payload>,
    "External provenance must be validated before flowing to bare T.");
static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::source::FromUser>, Payload>);
static_assert(!fsub::is_subsort_v<saf::Tagged<Payload, saf::vessel_trust::FromPytorch>, Payload>);

// ── E. Epistemic/access/version tags carry content — not auto-flowed
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::trust::Verified>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::trust::Unverified>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::access::RO>, int>);
static_assert(!fsub::is_subsort_v<saf::Tagged<int, saf::version::V<1>>, int>);

// Reverse direction is false for every tag.
static_assert(!fsub::is_subsort_v<int, saf::Tagged<int, saf::source::Sanitized>>);

// ── F. Stacked Refined<P, Tagged<T, V>> ⩽ Tagged<T, V> ─────────────
static_assert(fsub::is_subsort_v<
    saf::Refined<saf::positive, saf::Tagged<int, saf::source::Sanitized>>,
    saf::Tagged<int, saf::source::Sanitized>>);

// ── G. NumericalTier tolerance: tighter producer ⩽ looser consumer ─
using BitexactT = saf::NumericalTier<saf::Tolerance::BITEXACT, Payload>;
using RelaxedT  = saf::NumericalTier<saf::Tolerance::RELAXED, Payload>;
static_assert(fsub::is_subsort_v<BitexactT, RelaxedT>,
    "a bit-exact producer guarantee satisfies a relaxed consumer.");
static_assert(!fsub::is_subsort_v<RelaxedT, BitexactT>);

// ── H. Protocol composition: payload axiom flows through Send/Recv ─
//
// The payload subsort is consumed by SessionSubtype's Send/Recv
// covariance/contravariance — witness it reaches there through fixy too.
static_assert(fsub::is_subtype_sync_v<
    prot::Send<saf::Refined<saf::positive, int>, prot::End>,
    prot::Send<int, prot::End>>,
    "Send is payload-covariant: refined payload flows to bare-T position.");
static_assert(!fsub::is_subtype_sync_v<
    prot::Send<int, prot::End>,
    prot::Send<saf::Refined<saf::positive, int>, prot::End>>,
    "the reverse (bare T into a refined Send position) is rejected.");
static_assert(fsub::is_subtype_sync_v<
    prot::Recv<int, prot::End>,
    prot::Recv<saf::Refined<saf::positive, int>, prot::End>>,
    "Recv is payload-contravariant — the directions flip.");

// ── I. Axiom-family-count witness ──────────────────────────────────
//
// This block surfaces NO symbols (the source ships specialisations,
// not names).  We pin the count of shipped POSITIVE axiom FAMILIES
// witnessed through fixy — a substrate edit that adds/removes a
// payload-subsort axiom family updates this in lockstep.
//
//   1 Refined⩽T · 2 Refined-strengthening · 3 source::* (6 tags) ·
//   4 vessel_trust::Validated · 5 stacked Refined-of-Tagged ·
//   6 NumericalTier-tolerance                              ──── 6 families
constexpr int v067_positive_axiom_families = 6;
static_assert(v067_positive_axiom_families == 6,
    "fixy::sess::subtype:: V-067 positive-axiom-family count drifted — "
    "update SessionPayloadSubsort.h witnesses AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::subtype::v067_payload_axiom_test

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

    // ── V-067: payload-subsort axiom instantiations ────────────────
    //
    // Force the substrate's `is_subsort` specialisations through real
    // template instantiation under `-fsyntax-only`.  Without these the
    // visibility include of SessionPayloadSubsort.h could compile while
    // never proving the specialisations EVALUATE — these calls make a
    // latent SFINAE/inline-body regression in the specialisations fire
    // immediately at every consumer's include time.
    namespace saf = ::crucible::safety;
    [[maybe_unused]] constexpr bool refined_narrows =
        is_subsort_v<saf::Refined<saf::positive, int>, int>;
    [[maybe_unused]] constexpr bool refined_strengthens =
        is_subsort_v<saf::Refined<saf::positive, int>,
                     saf::Refined<saf::non_negative, int>>;
    [[maybe_unused]] constexpr bool tag_safe_erases =
        is_subsort_v<saf::Tagged<int, saf::source::Sanitized>, int>;
    [[maybe_unused]] constexpr bool tag_unsafe_blocked =
        !is_subsort_v<saf::Tagged<int, saf::source::External>, int>;
    [[maybe_unused]] constexpr bool send_payload_covariant =
        is_subtype_sync_v<
            proto::Send<saf::Refined<saf::positive, int>, proto::End>,
            proto::Send<int, proto::End>>;

    (void) refined_narrows;       (void) refined_strengthens;
    (void) tag_safe_erases;       (void) tag_unsafe_blocked;
    (void) send_payload_covariant;
}

}  // namespace crucible::fixy::sess::subtype
