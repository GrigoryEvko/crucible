#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — payload-wrapper subsort axioms
//
// Bridges the per-value safety wrappers (Tagged, Refined, Linear) to
// the session-type subsort lattice from SessionSubtype.h.  Without
// these axioms, `Send<Refined<positive, int>, K>` and `Send<int, K>`
// are unrelated types — every protocol that wants payload refinement
// has to specialise is_subsort by hand at every site.
//
// The integration is deliberately one-way and provenance-aware:
//
//   *  Refined<P, T>            ⩽  T          ALWAYS
//   *  Refined<P, T>            ⩽  Refined<Q, T>   when implies_v<P,Q>
//                                              (#227 strengthening
//                                              lattice — positive ⇒
//                                              non_negative, aligned<64>
//                                              ⇒ aligned<32>, etc.)
//   *  Tagged<T, Sanitized>     ⩽  T          provenance ERASURE on the
//   *  Tagged<T, Validated>     ⩽  T          way IN to bare-T positions
//   *  Tagged<T, FromInternal>  ⩽  T          (information narrowing —
//   *  Tagged<T, FromConfig>    ⩽  T          the Sanitized/Validated
//   *  Tagged<T, FromDb>        ⩽  T          carries MORE information
//   *  Tagged<T, Durable>       ⩽  T          than bare T; flowing it
//   *  Tagged<T, Computed>      ⩽  T          to a bare T position
//                                              loses information but
//                                              never violates a
//                                              precondition).
//
//   *  Tagged<T, External>      ⩽  T          DELIBERATELY ABSENT —
//   *  Tagged<T, FromPytorch>   ⩽  T          flowing untrusted input
//                                              to a bare T position
//                                              defeats the validator
//                                              the FFI boundary
//                                              installs.  This is the
//                                              load-bearing asymmetry
//                                              described in
//                                              misc/24_04_2026_safety_integration.md
//                                              §6.
//
//   *  Tagged<T, FromUser>      ⩽  T          ALSO ABSENT — user input
//                                              is untrusted by default;
//                                              must be explicitly
//                                              retagged to Sanitized
//                                              after validation.
//
//   *  Linear<T>                ⩽  T          DELIBERATELY ABSENT —
//                                              Linear's whole point is
//                                              that consume() is
//                                              explicit; auto-flowing
//                                              would silently bypass
//                                              the linearity contract.
//
//   *  Secret<T>                ⩽  T          DELIBERATELY ABSENT —
//                                              auto-flowing classified
//                                              data to unclassified
//                                              positions would leak
//                                              information.  Forces
//                                              explicit declassify<P>
//                                              at the trust boundary.
//
//   *  T                        ⩽  Tagged<T, V>  DELIBERATELY ABSENT
//                                              for every V — the
//                                              opposite direction
//                                              would let a bare T
//                                              silently gain stronger
//                                              provenance than it has.
//                                              Stronger provenance
//                                              must be earned via
//                                              explicit construction.
//
// Trust-tier tags (trust::Verified / trust::Tested / trust::Assumed /
// etc.) are also intentionally NOT auto-flowed.  Trust information is
// strictly stronger than no-information; silently dropping it on the
// way to a bare-T position would lose the verifier's work.  Callers
// that genuinely want to surrender the trust evidence must call
// .into() explicitly.
//
// ─── Why this lives in a sibling header, not in SessionSubtype.h ──
//
// SessionSubtype.h is the subtype RELATION; this header is a set of
// AXIOMS over the relation that mention concrete safety wrappers.
// Putting the axioms here keeps SessionSubtype.h free of dependencies
// on Tagged.h / Refined.h / etc., which preserves the framework's
// single-responsibility principle for headers.  TUs that want the
// payload-wrapper axioms include this header explicitly; TUs that
// only need the bare relation include just SessionSubtype.h.
//
// ─── Integration with PermissionedSessionHandle (FOUND-C v1) ──────
//
// `sessions/PermissionedSession.h` (shipped FOUND-C v1, see
// `misc/27_04_csl_permission_session_wiring.md`) carries protocol
// payloads through Send/Recv whose types may include Tagged /
// Refined / Transferable / Borrowed / Returned wrappers.  The
// permission-flow rules dispatch on payload SHAPE (Transferable /
// Borrowed / Returned / plain) via `sessions/SessionPermPayloads.h`
// markers; the subsumption rules in this header determine which
// payload TYPES are interchangeable through Send's covariance /
// Recv's contravariance.  Both layers compose cleanly because they
// operate on disjoint information.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §6, §7, §38, §39 — the
//     design rationale and the full catalog of intentional
//     non-axioms.
//   safety/SessionSubtype.h — the primary is_subsort template and
//     the Send/Recv covariance rules that consume our axioms.
//   safety/Tagged.h — the wrapper types whose subsort relations we
//     install here.
//   safety/Refined.h — same for refinement-typed payloads.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Refined.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Refined<P, T> ⩽ T — narrowing always flows ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A Refined value carries a runtime-checked predicate; the bare T does
// not.  Refined ⩽ T because Refined IS a T whose construction proved
// the predicate.  The reverse direction (T ⩽ Refined<P, T>) is
// deliberately false — a bare T has no proof.

template <auto Pred, typename T>
struct is_subsort<Refined<Pred, T>, T> : std::true_type {};

// LinearRefined<P, T> = Linear<Refined<P, T>>.  Same one-way flow:
// the inner Refined is a subtype of the bare T; the Linear wrapper
// does not change that.  However, LinearRefined ⩽ T is NOT shipped
// because the Linear consume() must be explicit (see file header).
//
// (No specialisation here.)

// ═════════════════════════════════════════════════════════════════════
// ── Refined<P, T> ⩽ Refined<Q, T> when P ⇒ Q  (#227 + §22 wiring) ──
// ═════════════════════════════════════════════════════════════════════
//
// Strengthening axiom: if predicate P implies predicate Q (P's truth-
// set ⊆ Q's), then a value carrying the stronger refinement P stands
// where the weaker refinement Q is expected.  Direction matches §22:
//
//   Send<Refined<positive, int>, K>  ⩽  Send<Refined<non_negative, int>, K>
//     because the producer guarantees more (positive); the recipient
//     at the position only requires non_negative; the stronger
//     guarantee subsumes via Send's payload covariance.
//
//   Recv<Refined<non_negative, int>, K> ⩽ Recv<Refined<positive, int>, K>
//     because the recipient is willing to accept any non_negative
//     value (the looser type); a position expecting a strict-positive
//     recipient is satisfied since positive ⊂ non_negative.  Recv's
//     payload contravariance reverses the implication direction.
//
// The implies_v trait lives in safety/Refined.h.  Ship implications
// for canonical predicate pairs there; user code adds more via
// predicate_implies type-level specialisation.

template <auto P, auto Q, typename T>
    requires (implies_v<P, Q> && !std::is_same_v<decltype(P), decltype(Q)>)
struct is_subsort<Refined<P, T>, Refined<Q, T>> : std::true_type {};

// ═════════════════════════════════════════════════════════════════════
// ── Tagged<T, V> ⩽ T — provenance erasure for trusted tags ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Each specialisation below flows the corresponding provenance to a
// bare T position.  These are the safe-to-erase tags: each represents
// a value that has been validated, sanitized, internally constructed,
// or loaded from a trusted store.  The asymmetry with the deliberately
// absent specialisations (External, FromPytorch, FromUser) is the
// load-bearing FFI / trust-boundary discipline.

// --- source::* (provenance) ----------------------------------------

template <typename T>
struct is_subsort<Tagged<T, source::Sanitized>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromInternal>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromConfig>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromDb>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::Durable>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::Computed>, T> : std::true_type {};

// --- vessel_trust::* (FFI validation) -------------------------------

template <typename T>
struct is_subsort<Tagged<T, vessel_trust::Validated>, T> : std::true_type {};

// --- DELIBERATELY ABSENT axioms (documented for grep-ability) ------
//
// The following specialisations are intentionally NOT shipped.  Each
// would defeat a load-bearing safety property; the absence is the
// discipline.
//
//   is_subsort<Tagged<T, source::External>,           T>  -- ABSENT
//   is_subsort<Tagged<T, source::FromUser>,           T>  -- ABSENT
//   is_subsort<Tagged<T, vessel_trust::FromPytorch>,  T>  -- ABSENT
//   is_subsort<Linear<T>,                             T>  -- ABSENT
//   is_subsort<Secret<T>,                             T>  -- ABSENT
//   is_subsort<T, Tagged<T, V>>  for any V                -- ABSENT
//   is_subsort<T, Refined<P, T>>                          -- ABSENT
//
// trust::* tags are deliberately omitted both directions: Verified vs
// Unverified vs Assumed all carry distinct epistemic content; silently
// dropping or upgrading is a discipline violation in either direction.
//
// access::* tags (RW / RO / WO / WriteOnce / AppendOnly / etc.) are
// also deliberately omitted: the access-mode information is what
// distinguishes safe-to-call APIs at the type level; flowing them
// silently would bypass exactly the discipline they exist to enforce.
//
// version::V<N> is omitted: cross-version compatibility requires a
// per-pair migration policy, not silent flow.
//
// If a downstream caller WANTS to drop one of the absent provenances,
// the explicit path is the wrapper's own .into() / .value() accessor
// at the call site — discoverable via grep, named at the discipline
// boundary.

// ═════════════════════════════════════════════════════════════════════
// ── Stacked composition: Refined<P, Tagged<T, V>> ⩽ Tagged<T, V> ──
// ═════════════════════════════════════════════════════════════════════
//
// Composability check: if a payload is BOTH refined AND tagged, the
// Refined-around-Tagged shape should be a subtype of the inner Tagged
// (refinement narrows, tag stays).  By transitivity (which is the
// USER's contract per SessionSubtype.h's note, not something the
// framework auto-closes), the stack flows further to bare T iff
// Tagged ⩽ T is one of the shipped axioms above.
//
// This single specialisation handles the canonical refined-tagged
// stack uniformly; the corresponding Tagged-around-Refined shape is
// handled by the Tagged ⩽ T axiom propagating through the inner
// Refined position via Send/Recv covariance.

template <auto Pred, typename T, typename V>
struct is_subsort<Refined<Pred, Tagged<T, V>>, Tagged<T, V>>
    : std::true_type {};

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the axiom set's positive cases (specialisations fire), the
// asymmetry (reverse direction is false), and the protocol-level
// composition (Send/Recv covariance picks up the axioms).  Runs at
// header-inclusion time; regressions to either this header or
// SessionSubtype.h's payload-flow rules fail at the first TU that
// includes us.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::payload_subsort_self_test {

// ── Fixture payloads ───────────────────────────────────────────────

struct DispatchRequest { int op_id; };
struct MemoryPlanByte  { unsigned char value; };
struct ConfigEntry     { int field; };

// ── Refined<P, T> ⩽ T positive cases ──────────────────────────────

static_assert(is_subsort_v<Refined<positive, int>,            int>);
static_assert(is_subsort_v<Refined<non_negative, int>,        int>);
static_assert(is_subsort_v<Refined<non_zero, int>,            int>);
static_assert(is_subsort_v<Refined<bounded_above<1024>, int>, int>);

// Reverse direction is false (default primary template) — a bare T
// has no proof of the predicate.
static_assert(!is_subsort_v<int, Refined<positive, int>>);
static_assert(!is_subsort_v<int, Refined<non_negative, int>>);

// ── Tagged<T, V> ⩽ T for safe-to-erase tags ────────────────────────

static_assert(is_subsort_v<Tagged<DispatchRequest, source::Sanitized>,
                            DispatchRequest>);
static_assert(is_subsort_v<Tagged<DispatchRequest, source::FromInternal>,
                            DispatchRequest>);
static_assert(is_subsort_v<Tagged<ConfigEntry, source::FromConfig>,
                            ConfigEntry>);
static_assert(is_subsort_v<Tagged<ConfigEntry, source::FromDb>,
                            ConfigEntry>);
static_assert(is_subsort_v<Tagged<MemoryPlanByte, source::Durable>,
                            MemoryPlanByte>);
static_assert(is_subsort_v<Tagged<MemoryPlanByte, source::Computed>,
                            MemoryPlanByte>);
static_assert(is_subsort_v<Tagged<DispatchRequest, vessel_trust::Validated>,
                            DispatchRequest>);

// ── Tagged<T, V> ⩽ T for UNSAFE tags is FALSE ──────────────────────
//
// External / FromPytorch / FromUser provenance must be validated /
// retagged before flowing to a bare-T position; the type system
// must not silently flow them.

static_assert(!is_subsort_v<Tagged<DispatchRequest, source::External>,
                             DispatchRequest>);
static_assert(!is_subsort_v<Tagged<DispatchRequest, source::FromUser>,
                             DispatchRequest>);
static_assert(!is_subsort_v<Tagged<DispatchRequest, vessel_trust::FromPytorch>,
                             DispatchRequest>);

// ── Reverse direction is FALSE for every tag ───────────────────────

static_assert(!is_subsort_v<DispatchRequest,
                             Tagged<DispatchRequest, source::Sanitized>>);
static_assert(!is_subsort_v<DispatchRequest,
                             Tagged<DispatchRequest, source::External>>);
static_assert(!is_subsort_v<DispatchRequest,
                             Tagged<DispatchRequest, vessel_trust::Validated>>);

// ── trust::* and access::* are NOT auto-flowed (epistemic content) ─

static_assert(!is_subsort_v<Tagged<int, trust::Verified>,   int>);
static_assert(!is_subsort_v<Tagged<int, trust::Tested>,     int>);
static_assert(!is_subsort_v<Tagged<int, trust::Unverified>, int>);
static_assert(!is_subsort_v<Tagged<int, trust::Assumed>,    int>);
static_assert(!is_subsort_v<Tagged<int, access::RO>,        int>);
static_assert(!is_subsort_v<Tagged<int, access::WriteOnce>, int>);
static_assert(!is_subsort_v<Tagged<int, access::AppendOnly>,int>);

// ── version::V<N> is NOT auto-flowed ───────────────────────────────

static_assert(!is_subsort_v<Tagged<int, version::V<1>>, int>);
static_assert(!is_subsort_v<Tagged<int, version::V<2>>, int>);

// ── Stacked Refined<P, Tagged<T, V>> ⩽ Tagged<T, V> ────────────────

static_assert(is_subsort_v<
    Refined<positive, Tagged<int, source::Sanitized>>,
    Tagged<int, source::Sanitized>>);

static_assert(is_subsort_v<
    Refined<bounded_above<1024>, Tagged<int, vessel_trust::Validated>>,
    Tagged<int, vessel_trust::Validated>>);

// And the reverse remains false.
static_assert(!is_subsort_v<
    Tagged<int, source::Sanitized>,
    Refined<positive, Tagged<int, source::Sanitized>>>);

// ── Reflexivity / identity (via primary is_same path) ──────────────

static_assert(is_subsort_v<Tagged<int, source::Sanitized>,
                            Tagged<int, source::Sanitized>>);
static_assert(is_subsort_v<Refined<positive, int>,
                            Refined<positive, int>>);
static_assert(is_subsort_v<int, int>);

// ── Different tags are unrelated ───────────────────────────────────

static_assert(!is_subsort_v<Tagged<int, source::Sanitized>,
                             Tagged<int, source::FromInternal>>);
static_assert(!is_subsort_v<Tagged<int, source::FromConfig>,
                             Tagged<int, source::FromDb>>);

// ── Cross-predicate refinement via implies_v (#227 + §22) ──────────
//
// Where the implication lattice in Refined.h ships an axiom, distinct
// refinements on the same T compose: stronger ⩽ weaker.
// Where no implication exists, they remain unrelated siblings.

// positive ⇒ non_negative  → Refined<positive, T> ⩽ Refined<non_neg, T>
static_assert( is_subsort_v<Refined<positive, int>,
                             Refined<non_negative, int>>);
// reverse rejected
static_assert(!is_subsort_v<Refined<non_negative, int>,
                             Refined<positive, int>>);

// positive ⇒ non_zero
static_assert( is_subsort_v<Refined<positive, int>,
                             Refined<non_zero, int>>);

// power_of_two ⇒ non_zero
static_assert( is_subsort_v<Refined<power_of_two, std::size_t>,
                             Refined<non_zero, std::size_t>>);

// non_zero does NOT imply non_negative (non_zero admits negative
// values like -3; non_negative does not).
static_assert(!is_subsort_v<Refined<non_zero, int>,
                             Refined<non_negative, int>>);

// Parameterised: BoundedAbove<8> ⇒ BoundedAbove<16>
static_assert( is_subsort_v<Refined<bounded_above<8u>, unsigned>,
                             Refined<bounded_above<16u>, unsigned>>);
static_assert(!is_subsort_v<Refined<bounded_above<16u>, unsigned>,
                             Refined<bounded_above<8u>, unsigned>>);

// Parameterised: InRange<10, 20> ⇒ InRange<0, 100>
static_assert( is_subsort_v<Refined<in_range<10, 20>, int>,
                             Refined<in_range<0, 100>, int>>);
// Strictly tighter range strictly inside looser one.
static_assert(!is_subsort_v<Refined<in_range<0, 100>, int>,
                             Refined<in_range<10, 20>, int>>);
// Disjoint ranges: neither subtype.
static_assert(!is_subsort_v<Refined<in_range<0, 10>, int>,
                             Refined<in_range<20, 30>, int>>);

// Parameterised: InRange<L, H> ⇒ BoundedAbove<H>
static_assert( is_subsort_v<Refined<in_range<0, 100>, int>,
                             Refined<bounded_above<100>, int>>);

// Parameterised: Aligned<64> ⇒ Aligned<32> ⇒ Aligned<8>
//   (transitivity is the USER's contract per SessionSubtype.h note,
//    not auto-closed; each direct step is checked here)
static_assert( is_subsort_v<Refined<aligned<64>, void*>,
                             Refined<aligned<32>, void*>>);
static_assert( is_subsort_v<Refined<aligned<32>, void*>,
                             Refined<aligned<8>, void*>>);
static_assert(!is_subsort_v<Refined<aligned<8>, void*>,
                             Refined<aligned<32>, void*>>);

// Reflexivity remains intact via the std::is_same fall-through (the
// strengthening spec's `!std::is_same_v` guard prevents shadowing).
static_assert( is_subsort_v<Refined<positive, int>,
                             Refined<positive, int>>);

// ═════════════════════════════════════════════════════════════════════
// ── Protocol-level composition: Send / Recv covariance picks up ────
// ── the axioms via SessionSubtype's existing payload rules ─────────
// ═════════════════════════════════════════════════════════════════════
//
// These tests verify that the wrapper subsorts compose cleanly with
// the session-type combinators.  The session subtyping rules (from
// SessionSubtype.h) are:
//
//   Send<P1, R1> ⩽ Send<P2, R2>   iff   P1 ⩽ P2  ∧  R1 ⩽ R2   (cov)
//   Recv<P1, R1> ⩽ Recv<P2, R2>   iff   P2 ⩽ P1  ∧  R1 ⩽ R2   (contra)
//
// So Refined<P, T> on a Send position flows to bare T (covariance);
// on a Recv position, bare T flows to Refined<P, T> (contravariance,
// which the type system DELIBERATELY rejects — the recipient cannot
// produce the predicate from nothing).

// Send: refined payload flows to bare-payload position (covariance).
static_assert(is_subtype_sync_v<
    Send<Refined<positive, int>, End>,
    Send<int, End>>);

// The reverse direction is rejected: a sender promising bare int
// cannot stand where Send<Refined<positive, int>, End> is expected,
// because the recipient at that position is entitled to the
// predicate.  Covariance + (T ⩽ Refined<P, T> is false) = the whole
// Send relation is false in this direction.
static_assert(!is_subtype_sync_v<
    Send<int, End>,
    Send<Refined<positive, int>, End>>);

// Recv: payload contravariance.  A receiver expecting a Refined value
// can stand where the supertype expects a bare-payload Recv (because
// the recipient's downstream code is fine with the looser type).
// Wait — Gay-Hole is contravariant in payload, which means the
// SUBTYPE's payload must be a SUPER of the supertype's payload.  So:
//
//   Recv<int, End>  ⩽  Recv<Refined<positive, int>, End>
//
// reads as "this subtype receives bare ints; the supertype's recipient
// expects Refined<positive, int>; we can stand because we accept
// MORE values than the supertype's recipient expected to send."
//
// This is the dual asymmetry to Send: the "validation flows from
// producer to consumer" direction works on Send; the "looser is
// substitutable for stricter" direction works on Recv.

static_assert(is_subtype_sync_v<
    Recv<int, End>,
    Recv<Refined<positive, int>, End>>);

static_assert(!is_subtype_sync_v<
    Recv<Refined<positive, int>, End>,
    Recv<int, End>>);

// Tagged provenance through Send: Sanitized flows to bare.
static_assert(is_subtype_sync_v<
    Send<Tagged<int, source::Sanitized>, End>,
    Send<int, End>>);

// External does NOT flow to bare — the load-bearing FFI gap-closure.
static_assert(!is_subtype_sync_v<
    Send<Tagged<int, source::External>, End>,
    Send<int, End>>);

// vessel_trust::Validated flows to bare; FromPytorch does not.
static_assert(is_subtype_sync_v<
    Send<Tagged<int, vessel_trust::Validated>, End>,
    Send<int, End>>);

static_assert(!is_subtype_sync_v<
    Send<Tagged<int, vessel_trust::FromPytorch>, End>,
    Send<int, End>>);

// Recv contravariance for Tagged: a recipient willing to accept bare
// int can substitute for one expecting Sanitized, because they're
// happy with the looser type.
static_assert(is_subtype_sync_v<
    Recv<int, End>,
    Recv<Tagged<int, source::Sanitized>, End>>);

// Loop / Continue propagation (the axioms compose through Loop bodies).
static_assert(is_subtype_sync_v<
    Loop<Send<Refined<positive, int>, Continue>>,
    Loop<Send<int, Continue>>>);

// ── Cross-predicate strengthening through Send / Recv (#227 + §22) ─

// Send covariance: stronger refinement subsumes weaker.
static_assert(is_subtype_sync_v<
    Send<Refined<positive, int>, End>,
    Send<Refined<non_negative, int>, End>>);

// Reverse rejected: the recipient at the supertype position is
// entitled to the stronger predicate; weaker doesn't satisfy.
static_assert(!is_subtype_sync_v<
    Send<Refined<non_negative, int>, End>,
    Send<Refined<positive, int>, End>>);

// Recv contravariance: the looser-payload recipient stands where the
// stricter-payload position is expected.
static_assert(is_subtype_sync_v<
    Recv<Refined<non_negative, int>, End>,
    Recv<Refined<positive, int>, End>>);

static_assert(!is_subtype_sync_v<
    Recv<Refined<positive, int>, End>,
    Recv<Refined<non_negative, int>, End>>);

// Parameterised: tighter BoundedAbove subtypes looser via Send.
static_assert(is_subtype_sync_v<
    Send<Refined<bounded_above<1024u>, unsigned>, End>,
    Send<Refined<bounded_above<4096u>, unsigned>, End>>);

// Loop body strengthening composes through Continue.
static_assert(is_subtype_sync_v<
    Loop<Send<Refined<positive, int>, Continue>>,
    Loop<Send<Refined<non_negative, int>, Continue>>>);

// Select branch covariance: each branch's Send-payload subsumption
// flows independently.
static_assert(is_subtype_sync_v<
    Select<Send<Refined<positive, int>, End>,
           Send<Tagged<MemoryPlanByte, source::Sanitized>, End>>,
    Select<Send<int, End>,
           Send<MemoryPlanByte, End>>>);

// Offer branch contravariance: a wider Offer with refined-payload
// Recvs is a subtype of a narrower Offer with bare-payload Recvs (the
// subtype handles MORE branches AND each branch's payload is
// contravariant).

// Wait — this is more subtle.  Offer<...> has BRANCH-contravariance
// (more branches = subtype) AND each branch's payload-position
// follows whatever rule that combinator imposes.  An Offer branch is
// `Recv<P, K>` (typically), so the branch's PAYLOAD position is
// contravariant by the Recv rule.
//
// So Offer<Recv<int, End>>  ⩽  Offer<Recv<Refined<positive, int>, End>>
// because the subtype's recipient (bare int) is fine for the
// supertype's expected payload (Refined positive int) — recipient
// asymmetry, the receiver of the looser type can stand for the
// receiver of the stricter type.

static_assert(is_subtype_sync_v<
    Offer<Recv<int, End>>,
    Offer<Recv<Refined<positive, int>, End>>>);

}  // namespace detail::payload_subsort_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
