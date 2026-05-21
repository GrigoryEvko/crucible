#pragma once

// ── crucible::fixy::dim — alias over safety::DimensionAxis ──────────
//
// Clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// THIS HEADER MUST NOT REDEFINE THE DIMENSION TAXONOMY.  The 20-axis
// enum + 5-Tier classification is owned by `safety/DimensionTraits.h`
// (P0-3, shipped via GAPS-091).  This header re-exports the enum into
// the `fixy::dim` namespace + a reflection-driven self-test verifying
// the substrate enum hasn't drifted under us.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::DimensionAxis      — the 22 enumerators (§24.1 of fixy.md;
//                                 +Synchronization via fixy-A3-008,
//                                 +Regime via fixy-A3-009)
//   safety::dimension_axis_name — string projection
//   safety::tier_of_axis        — Tier S/L/T/F/V classification
//   safety::TierKind            — the 5 composition-law families
//   safety::DIMENSION_AXIS_COUNT — reflection-driven cardinality
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  The fixy::dim namespace is a thin namespace alias.  If a
// reviewer is tempted to add a new enumerator, struct, or trait here,
// the rule is: add it to safety/DimensionTraits.h first, then alias
// here.  This keeps the dimension catalog in ONE place; downstream
// tooling (Diagnostic Category, RowHashFold, DimensionTraits cross-
// product table) sees the canonical source of truth.
//
// ── Why a separate namespace alias rather than `using` at call site
//
// The fixy:: discipline surface is greppable.  Every fixy header that
// references the 22 dimensions cites `fixy::dim::DimensionAxis`, not
// the raw `safety::DimensionAxis`.  A reviewer auditing "what does
// fixy depend on?" can grep `fixy::` and see every substrate touch.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — strong enum class : uint8_t inherited from substrate.
//   InitSafe — no fields, no state; the alias has zero footprint.
//   DetSafe  — every operation is constexpr; downstream `Engaged<F, D>`
//              checks resolve at template instantiation time.
//
// ── Runtime cost ───────────────────────────────────────────────────
//
// Zero.  The fixy::dim namespace contains type aliases + a constexpr
// reflection-driven coverage assertion that fires only at TU compile.
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Substrate-contract sanity check, NOT a count-pinning drift detector.
// Per `feedback_gcc16_c26_reflection_gotchas.md §3`, hardcoded enum-
// count literals (`== 20`, `== 5`) invariably drift when an enumerator
// is added; the assertion fires every time the substrate intentionally
// grows, mistaking "substrate intentionally extended" for "fixy mirror
// needs auditing."  Both sides of the comparison below are now
// reflection-derived from the same enum, making the assertion
// tautological under normal operation but catching the structural drift
// where the substrate's exposed count constant diverges from
// reflection's view of the same enum.
//
// Adding a new DimensionAxis is a deliberately-coordinated change.
// Each downstream owner ships its OWN per-axis coverage assertion (the
// load-bearing drift detectors) — this header's assertion is only a
// substrate-contract sanity check:
//
//   safety/DimensionTraits.h:594  — every_dimension_axis_has_name()
//   safety/DimensionTraits.h:616  — every_dimension_axis_has_tier()
//   fixy/Default.h:289-298        — per-axis strict_default_for sweep
//   fixy/Grant.h                  — which_dim<G> + acceptance markers
//   fixy/Reject.h:432             — fixy_catalog_size == kDimAxisCount
//                                   (FixyCatalog ↔ DimensionAxis card.)
//
// Adding a new TierKind currently has NO fixy-side per-tier consumer
// (Reject.h dispatches per-axis, not per-tier — verified via grep).
// The substrate's tier_of_axis switch (DimensionTraits.h:616) is the
// only consumer that needs updating; substrate's own static_assert
// fires first in every TU pulling fixy/Dim.h.

#include <crucible/safety/DimensionTraits.h>

#include <meta>
#include <string_view>

namespace crucible::fixy::dim {

// ─── DimensionAxis — alias of safety::DimensionAxis ────────────────
//
// The single source of truth.  Every fixy header that names a
// dimension cites this alias.  Downstream:
//
//   fixy::dim::DimensionAxis::Usage
//   fixy::dim::TierKind::Semiring
//
// reads through to the substrate without redefinition.

using safety::DimensionAxis;
using safety::TierKind;

// ─── String projection + tier classification ───────────────────────

using safety::dimension_axis_name;
using safety::tier_of_axis;
using safety::tier_of_axis_v;
using safety::tier_kind_name;

// ─── Reflection-driven cardinality ─────────────────────────────────
//
// Used by fixy/Reject.h's engagement check.  Re-exposing it here
// makes the dim header self-contained for downstream consumers.

using safety::DIMENSION_AXIS_COUNT;
using safety::TIER_KIND_COUNT;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — substrate-contract sanity check ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Both sides of each comparison derive from the same reflection call
// on the same enum.  Under normal operation the assertion is
// tautological — it does NOT fire when the substrate intentionally
// grows.  The load-bearing drift detectors live in the per-consumer
// coverage assertions documented in the header preamble above; the
// canonical one for axis cardinality is `fixy_catalog_size ==
// kDimAxisCount` at fixy/Reject.h:432, which fires if a new axis is
// added to safety/DimensionTraits.h without a matching FixyCatalog
// entry.
//
// This assertion catches the structural drift case where the
// substrate constant is ever defined non-reflectively (manual count,
// hardcoded literal — feedback_gcc16_c26_reflection_gotchas.md §3) and
// falls out of sync with reflection.

// NOTE: `^^DimensionAxis` here would resolve to the using-declaration
// above (line 72 `using safety::DimensionAxis;`) which GCC 16's
// reflection operator does NOT follow — emits "`^^` cannot be applied
// to a using-declaration."  Reach through to the substrate enum
// directly via the fully-qualified `safety::DimensionAxis` path.

static_assert(DIMENSION_AXIS_COUNT
              == std::meta::enumerators_of(^^safety::DimensionAxis).size(),
    "fixy::dim — substrate DIMENSION_AXIS_COUNT has drifted from the "
    "reflection-derived enumerator count of safety::DimensionAxis.  "
    "Either the substrate constant was manually maintained (and forgot "
    "to bump on enumerator addition) or reflection is reporting a "
    "different enum than the substrate exposes.  Investigate "
    "safety/DimensionTraits.h's DIMENSION_AXIS_COUNT definition.");

static_assert(TIER_KIND_COUNT
              == std::meta::enumerators_of(^^safety::TierKind).size(),
    "fixy::dim — substrate TIER_KIND_COUNT has drifted from the "
    "reflection-derived enumerator count of safety::TierKind.  Same "
    "diagnosis as DIMENSION_AXIS_COUNT above.  Note: there is no "
    "fixy-side per-tier dispatch (Reject.h dispatches per-axis, NOT "
    "per-tier — verified via grep); this assertion exists for "
    "substrate-contract consistency only.  Adding a new TierKind "
    "requires updating safety/DimensionTraits.h's tier_of_axis switch "
    "arms (substrate's own static_assert at DimensionTraits.h:616 "
    "fires first).");

// ─── Per-dim Tier sanity — covered by substrate guard ──────────────
//
// safety/DimensionTraits.h ships `every_dimension_axis_has_tier()` +
// its own static_assert (line 616 of that header) — both reflect
// `enumerators_of(^^DimensionAxis)` and reject the
// `TierKind{0xFF}` fallthrough.  The substrate header is included
// at line 56 above, so the substrate static_assert fires inside
// every TU that pulls fixy/Dim.h.  A fixy-side mirror would be dead
// code (per fixy-H-11): the substrate guard always fires first and
// catches the same failure mode (new axis without matching arm in
// tier_of_axis).  Add new axes by editing safety/DimensionTraits.h
// — the substrate's static_assert then forces the maintainer to
// extend tier_of_axis's switch arms before the build re-succeeds.

}  // namespace crucible::fixy::dim
