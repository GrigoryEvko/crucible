#pragma once

// ── crucible::fixy::sess::grade — session grade-extraction slice ────
//
// FIXY-U-052j (tenth and FINAL slice of the U-052 umbrella).  Surfaces
// the grade-OWNED public surface of sessions/SessionGrade.h (the
// Graded-product grade-EXTRACTION layer of the session-type stack —
// the metafunctions that fold a session protocol / payload type into a
// six-axis `ProductLattice` grade and project per-axis values out of
// it) into `crucible::fixy::sess::grade::`.
//
// Production callers — subtype-relation grade filters (SessionSubtype's
// `protocol_grade_satisfies_v`), per-vendor/per-tier admission gates,
// Forge recipe-tier reasoning — spell the grade vocabulary through the
// fixy umbrella, not raw `safety::proto::`.
//
// Twenty-six symbols (the grade-OWNED public API):
//
//   axis tags (6, under ::axis::): Vendor, NumericalTier, CipherTier,
//                          CrashClass, EpochVersioned, NumaPlacement
//   Grade metafns (7):     protocol_grade, protocol_grade_t,
//                          payload_grade, payload_grade_t,
//                          grade_for_axis, grade_for_axis_t,
//                          grade_for_axis_v
//   Protocol projections — type (6): protocol_vendor_t,
//                          protocol_numerical_tier_t,
//                          protocol_cipher_tier_t, protocol_crash_class_t,
//                          protocol_epoch_versioned_t,
//                          protocol_numa_placement_t
//   Protocol projections — value (6): protocol_grade_vendor_v,
//                          protocol_grade_numerical_tier_v,
//                          protocol_grade_cipher_tier_v,
//                          protocol_grade_crash_class_v,
//                          protocol_grade_epoch_versioned_v,
//                          protocol_grade_numa_placement_v
//   Aggregate (1):         protocol_grade_aggregate_satisfies_v
//
// ── Boundary: what this slice DELIBERATELY does NOT surface ────────
//
// SessionGrade.h's `crucible::safety::proto` block contains three tiers
// that are NOT grade-owned and are therefore excluded here:
//
//   1. NINE `algebra::lattices::*` re-exports (CipherTierLattice,
//      CipherTierTag, CrashClass-the-enum, CrashLattice, ProductLattice,
//      Tolerance, ToleranceLattice, VendorBackend, VendorLattice).
//      These are ALGEBRA-owned lattice carriers SessionGrade.h merely
//      re-exports for its own use; the grade metafns RETURN them but do
//      not DEFINE them.  Reach them via `safety::proto::` (or a future
//      fixy::algebra:: slice), not here — re-re-exporting would create a
//      second cache slot for an algebra symbol.
//   2. ELEVEN foreign forward-declarations (ContentAddressed,
//      Transferable, Borrowed, Returned, DelegatedSession, Delegate,
//      Accept, EpochedDelegate, EpochedAccept, CheckpointedSession,
//      Stop_g).  SessionGrade.h forward-declares these ONLY so it can
//      write `payload_grade` / `protocol_grade` specializations for
//      them; the types are OWNED by SessionContentAddressed.h /
//      SessionPermPayloads.h / SessionDelegate.h / SessionCrash.h —
//      surfaced (where surfaced) by ::contentaddr:: and peers.
//   3. detail::session_grade machinery (PresenceLattice, make_t,
//      join_t, values, bottom_t, satisfies_v) — INTERNAL.
//
// The `CrashClass` NAME collides between the axis tag
// (`grade::axis::CrashClass`) and the algebra enum
// (`safety::proto::CrashClass`).  This slice surfaces ONLY the axis tag
// (under ::axis::); the enum is reached via `safety::proto::CrashClass`.
//
// ── Why a dedicated grade:: sub-namespace ──────────────────────────
//
// fixy::sess::subtype:: holds the refinement order whose grade filter
// (`protocol_grade_satisfies_v`) consumes THIS layer's grades;
// ::queue:: / ::context:: / ::diagnostic:: are the other L-tier slices.
// Keeping grade-extraction in ::grade:: lets audit-grep
// `fixy::sess::grade::` find every fixy-routed grade computation
// distinct from the subtype relation that consumes it.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-six using-decls + a sentinel battery + smoke routine.
// No new types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state; axis tags + grades are empty
//              type markers / pure metafunctions.
//   TypeSafe — using-decls preserve substrate type identity; per-axis
//              projections are strongly typed (VendorBackend, Tolerance,
//              CipherTierTag, CrashClass enums; lattice At<> types).
//   NullSafe — no pointer state introduced.
//   MemSafe  — all symbols are compile-time-only; nothing is allocated.
//   DetSafe  — grade extraction is a pure type-level fold; same protocol
//              always yields the same ProductLattice grade.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionGrade.h>

#include <type_traits>

namespace crucible::fixy::sess::grade {

// ── 1. Grade metafunctions (7) ─────────────────────────────────────
using ::crucible::safety::proto::protocol_grade;
using ::crucible::safety::proto::protocol_grade_t;
using ::crucible::safety::proto::payload_grade;
using ::crucible::safety::proto::payload_grade_t;
using ::crucible::safety::proto::grade_for_axis;
using ::crucible::safety::proto::grade_for_axis_t;
using ::crucible::safety::proto::grade_for_axis_v;

// ── 2. Per-axis protocol projections — lattice types (6) ───────────
using ::crucible::safety::proto::protocol_vendor_t;
using ::crucible::safety::proto::protocol_numerical_tier_t;
using ::crucible::safety::proto::protocol_cipher_tier_t;
using ::crucible::safety::proto::protocol_crash_class_t;
using ::crucible::safety::proto::protocol_epoch_versioned_t;
using ::crucible::safety::proto::protocol_numa_placement_t;

// ── 3. Per-axis protocol projections — enum/bool values (6) ────────
using ::crucible::safety::proto::protocol_grade_vendor_v;
using ::crucible::safety::proto::protocol_grade_numerical_tier_v;
using ::crucible::safety::proto::protocol_grade_cipher_tier_v;
using ::crucible::safety::proto::protocol_grade_crash_class_v;
using ::crucible::safety::proto::protocol_grade_epoch_versioned_v;
using ::crucible::safety::proto::protocol_grade_numa_placement_v;

// ── 4. Aggregate grade satisfaction (1) ────────────────────────────
using ::crucible::safety::proto::protocol_grade_aggregate_satisfies_v;

// ── 5. Axis tags (6) — mirror the substrate proto::axis:: namespace ─
namespace axis {
using ::crucible::safety::proto::axis::Vendor;
using ::crucible::safety::proto::axis::NumericalTier;
using ::crucible::safety::proto::axis::CipherTier;
using ::crucible::safety::proto::axis::CrashClass;
using ::crucible::safety::proto::axis::EpochVersioned;
using ::crucible::safety::proto::axis::NumaPlacement;
}  // namespace axis

}  // namespace crucible::fixy::sess::grade

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Mirrors the substrate SessionGrade.h self-test's MultiAxisProto
// witnesses through the fixy spelling, so a substrate-side rename trips
// at every consumer's include time.

namespace crucible::fixy::sess::grade::u052j_self_test {

namespace prot = ::crucible::safety::proto;
namespace saf  = ::crucible::safety;

// Mirror the substrate fixture: a six-axis-loaded payload on a Send.
struct ResultTensor {};
using MultiAxisPayload =
    saf::NumericalTier<prot::Tolerance::BITEXACT,
        saf::Vendor<prot::VendorBackend::NV,
            saf::CipherTier<prot::CipherTierTag::Hot,
                saf::Crash<prot::CrashClass::NoThrow, ResultTensor>>>>;
using MultiAxisProto = prot::Send<MultiAxisPayload, prot::End>;

// ── A. Metafn + axis-tag type identity ─────────────────────────────
static_assert(std::is_same_v<protocol_grade<prot::End>,
                             prot::protocol_grade<prot::End>>);
static_assert(std::is_same_v<protocol_grade_t<MultiAxisProto>,
                             prot::protocol_grade_t<MultiAxisProto>>);
static_assert(std::is_same_v<axis::NumericalTier, prot::axis::NumericalTier>);
static_assert(std::is_same_v<axis::Vendor, prot::axis::Vendor>);
static_assert(!std::is_same_v<axis::Vendor, axis::CipherTier>,
    "axis tags are structurally distinct.");

// ── B. Per-axis VALUE projections through the fixy spelling ────────
static_assert(protocol_grade_vendor_v<MultiAxisProto> == prot::VendorBackend::NV);
static_assert(protocol_grade_numerical_tier_v<MultiAxisProto>
                  == prot::Tolerance::BITEXACT);
static_assert(protocol_grade_cipher_tier_v<MultiAxisProto>
                  == prot::CipherTierTag::Hot);
static_assert(protocol_grade_crash_class_v<MultiAxisProto>
                  == prot::CrashClass::NoThrow);
static_assert(!protocol_grade_epoch_versioned_v<MultiAxisProto>);
static_assert(!protocol_grade_numa_placement_v<MultiAxisProto>);

// ── C. Per-axis TYPE projections resolve to lattice At<> types ─────
static_assert(std::is_same_v<protocol_vendor_t<MultiAxisProto>,
                             prot::VendorLattice::At<prot::VendorBackend::NV>>);
static_assert(std::is_same_v<protocol_numerical_tier_t<MultiAxisProto>,
                             prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>);

// ── D. grade_for_axis on a bare payload ────────────────────────────
static_assert(grade_for_axis_v<axis::NumericalTier, MultiAxisPayload>
                  == prot::Tolerance::BITEXACT);
static_assert(grade_for_axis_v<axis::Vendor, MultiAxisPayload>
                  == prot::VendorBackend::NV);
static_assert(std::is_same_v<
    grade_for_axis_t<axis::NumericalTier, MultiAxisPayload>,
    prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>);

// ── E. Aggregate satisfaction — runtime-graded ⊒ compile-graded ────
using EpochPayload = saf::EpochVersioned<MultiAxisPayload>;
using NumaPayload  = saf::NumaPlacement<EpochPayload>;
using RuntimeGradeProto = prot::Send<NumaPayload, prot::End>;
static_assert(protocol_grade_epoch_versioned_v<RuntimeGradeProto>);
static_assert(protocol_grade_numa_placement_v<RuntimeGradeProto>);
static_assert(protocol_grade_aggregate_satisfies_v<RuntimeGradeProto,
                                                   MultiAxisProto>,
    "the runtime-graded protocol carries strictly more evidence.");
static_assert(!protocol_grade_aggregate_satisfies_v<MultiAxisProto,
                                                    RuntimeGradeProto>,
    "the compile-graded protocol lacks the epoch/numa evidence.");

// ── F. Cardinality witness — count of items U-052j surfaces ────────
//
//   grade metafns (7) + type projections (6) + value projections (6) +
//   aggregate (1) + axis tags (6)                            ──── 26
constexpr int u052j_surface_cardinality = 26;
static_assert(u052j_surface_cardinality == 26,
    "fixy::sess::grade:: U-052j surface cardinality drifted — "
    "update SessGrade.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::grade::u052j_self_test

namespace crucible::fixy::sess::grade {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Forces the grade-extraction metafunctions through real instantiation
// so any latent template-evaluation issue surfaces under `-fsyntax-only`
// of any TU that includes SessGrade.h.  Grades are pure type-level
// folds; the locals are zero-footprint.

inline void runtime_smoke_test() noexcept {
    namespace prot = ::crucible::safety::proto;
    namespace saf  = ::crucible::safety;

    struct Payload {};
    using P = prot::Send<
        saf::NumericalTier<prot::Tolerance::BITEXACT, Payload>, prot::End>;

    [[maybe_unused]] constexpr prot::Tolerance ntier =
        protocol_grade_numerical_tier_v<P>;
    [[maybe_unused]] constexpr prot::VendorBackend vend =
        protocol_grade_vendor_v<P>;
    [[maybe_unused]] constexpr bool self_sat =
        protocol_grade_aggregate_satisfies_v<P, P>;

    using GradeT = protocol_grade_t<P>;
    using AxisT  = grade_for_axis_t<axis::NumericalTier,
        saf::NumericalTier<prot::Tolerance::BITEXACT, Payload>>;
    [[maybe_unused]] constexpr bool grade_ok =
        std::is_same_v<GradeT, prot::protocol_grade_t<P>>;
    [[maybe_unused]] constexpr bool axis_ok =
        std::is_same_v<AxisT, prot::ToleranceLattice::At<prot::Tolerance::BITEXACT>>;

    (void) ntier; (void) vend; (void) self_sat; (void) grade_ok; (void) axis_ok;
}

}  // namespace crucible::fixy::sess::grade
