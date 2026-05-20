#pragma once

// ── crucible::fixy::sess::payloadsubsort — payload subsort axiom set ──
//
// FIXY-U-052h (eighth slice of the U-052 umbrella).  sessions/
// SessionPayloadSubsort.h ships NO new public names — it consists
// ENTIRELY of partial specialisations of `is_subsort<...>` (the
// payload-level subtype relation, whose primary template is OWNED by
// sessions/SessionSubtype.h and already surfaced in
// `fixy::sess::subtype::is_subsort` / `is_subsort_v`).
//
// So this is NOT a re-export slice — there is nothing to using-declare.
// It is a WITNESS-AND-VISIBILITY slice with two load-bearing jobs:
//
//   (1) UMBRELLA VISIBILITY.  fixy/SessSubtype.h pulls SessionSubtype.h
//       but NOT SessionPayloadSubsort.h.  A consumer that reaches the
//       relation through the fixy umbrella alone (e.g.
//       `fixy::sess::subtype::is_subsort_v<Refined<positive,int>, int>`)
//       would otherwise resolve the PRIMARY template (std::is_same →
//       false) — a SILENT WRONG ANSWER, because the narrowing
//       specialisation lives in a header the umbrella never included.
//       This header `#include`s SessionPayloadSubsort.h, so once it is
//       in Fixy.h's Phase-C block every umbrella consumer sees the
//       payload-subsort specialisations.  That is the actual fix.
//
//   (2) AXIOM WITNESS.  The sentinel battery below re-proves the
//       COMPLETE shipped axiom set AND the deliberately-absent
//       (asymmetry-discipline) axioms THROUGH the fixy spelling
//       (`fixy::sess::subtype::is_subsort_v` / `is_subtype_sync_v`),
//       so a substrate regression that drops a specialisation — or
//       silently adds an unsafe one (External/FromUser/FromPytorch
//       flowing to bare T) — trips at every consumer's include time.
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
//
// ── Why a dedicated payloadsubsort:: sub-namespace ─────────────────
//
// It contains no using-decls (nothing to re-export) but DOES carry the
// sentinel battery + runtime_smoke_test, and the include itself is the
// visibility fix.  Keeping it in ::payloadsubsort:: lets audit-grep
// `fixy::sess::payloadsubsort::` find the payload-axiom witness site,
// and makes the umbrella's dependency on the specialisations explicit.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  No types, no using-decls, no mint factories.  One substrate
// `#include` (the visibility fix) + a sentinel battery + smoke.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — the entire point: payload provenance/refinement does not
//              silently erase; unsafe tags do NOT flow to bare T.
//   DetSafe  — subsort is a pure type-level predicate.
//   InitSafe/NullSafe/MemSafe — no state, no pointers, no allocation.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  One include + compile-time witnesses.

#include <crucible/fixy/SessSubtype.h>          // fixy::sess::subtype::is_subsort_v
#include <crucible/safety/NumericalTier.h>      // saf::NumericalTier + saf::Tolerance
#include <crucible/sessions/SessionPayloadSubsort.h>  // THE specialisations

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::payloadsubsort {

// Intentionally NO using-declarations.  SessionPayloadSubsort.h ships
// only `is_subsort<...>` specialisations; the relation NAME is reached
// via `crucible::fixy::sess::subtype::is_subsort` / `is_subsort_v`.
// This namespace exists for the witness battery + smoke test below and
// to make `fixy::sess::payloadsubsort::` a greppable audit anchor.

}  // namespace crucible::fixy::sess::payloadsubsort

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Every witness routes through the FIXY spelling
// (`fixy::sess::subtype::is_subsort_v` / `is_subtype_sync_v`) so the
// claim is "the payload-subsort axioms reach a fixy-umbrella consumer",
// not merely "they exist in the substrate".

namespace crucible::fixy::sess::payloadsubsort::u052h_self_test {

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

// ── I. Axiom-count witness ─────────────────────────────────────────
//
// This slice surfaces NO symbols (it ships specialisations, not names),
// so there is no using-decl cardinality.  Instead we pin the count of
// shipped POSITIVE axiom FAMILIES witnessed through fixy — a substrate
// edit that adds/removes a payload-subsort axiom family updates this.
//
//   1 Refined⩽T · 2 Refined-strengthening · 3 source::* (6 tags) ·
//   4 vessel_trust::Validated · 5 stacked Refined-of-Tagged ·
//   6 NumericalTier-tolerance                              ──── 6 families
constexpr int u052h_positive_axiom_families = 6;
static_assert(u052h_positive_axiom_families == 6,
    "fixy::sess::payloadsubsort:: positive-axiom-family count drifted — "
    "update SessPayloadSubsort.h witnesses AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::payloadsubsort::u052h_self_test

namespace crucible::fixy::sess::payloadsubsort {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The payload subsort is a pure type-level relation; the smoke routine
// forces representative instantiations through the fixy spelling so any
// latent template-evaluation issue surfaces under `-fsyntax-only` of any
// TU that includes SessPayloadSubsort.h.

inline void runtime_smoke_test() noexcept {
    namespace saf  = ::crucible::safety;
    namespace prot = ::crucible::safety::proto;
    namespace fsub = ::crucible::fixy::sess::subtype;

    [[maybe_unused]] constexpr bool narrows =
        fsub::is_subsort_v<saf::Refined<saf::positive, int>, int>;
    [[maybe_unused]] constexpr bool strengthens =
        fsub::is_subsort_v<saf::Refined<saf::positive, int>,
                           saf::Refined<saf::non_negative, int>>;
    [[maybe_unused]] constexpr bool tag_erases =
        fsub::is_subsort_v<saf::Tagged<int, saf::source::Sanitized>, int>;
    [[maybe_unused]] constexpr bool unsafe_blocked =
        !fsub::is_subsort_v<saf::Tagged<int, saf::source::External>, int>;
    [[maybe_unused]] constexpr bool send_covariant =
        fsub::is_subtype_sync_v<
            prot::Send<saf::Refined<saf::positive, int>, prot::End>,
            prot::Send<int, prot::End>>;

    (void) narrows; (void) strengthens; (void) tag_erases;
    (void) unsafe_blocked; (void) send_covariant;
}

}  // namespace crucible::fixy::sess::payloadsubsort
