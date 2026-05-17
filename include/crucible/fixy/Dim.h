#pragma once

// ── crucible::fixy::dim — alias over safety::DimensionAxis ──────────
//
// Phase A of the clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// THIS HEADER MUST NOT REDEFINE THE DIMENSION TAXONOMY.  The 20-axis
// enum + 5-Tier classification is owned by `safety/DimensionTraits.h`
// (P0-3, shipped via GAPS-091).  This header re-exports the enum into
// the `fixy::dim` namespace + a reflection-driven self-test verifying
// the substrate enum hasn't drifted under us.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::DimensionAxis      — the 20 enumerators (§24.1 of fixy.md)
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
// references the 20 dimensions cites `fixy::dim::DimensionAxis`, not
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
// The static_assert below verifies the substrate's enumerator count
// matches our expectation (20).  If a future P-row adds a 21st axis,
// this assertion fires and the maintainer is forced to update fixy/
// Default.h and fixy/Grant.h alongside.

#include <crucible/safety/DimensionTraits.h>

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
// ── Self-test — substrate drift detector ──────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// If the substrate's enumerator count changes, every fixy header that
// switches on per-dim engagement must be updated alongside.  Pinning
// the count here forces the build to fail at fixy/Dim.h with a clear
// message, rather than at some downstream consumer with a cryptic
// pack-expansion error.

static_assert(DIMENSION_AXIS_COUNT == 20,
    "fixy::dim — substrate DimensionAxis enumerator count has drifted "
    "from the 20-axis taxonomy assumed by Phase A.  Update fixy/Default.h "
    "(per-dim strict defaults) and fixy/Grant.h (engagement markers) "
    "alongside safety/DimensionTraits.h's enumerator addition.");

static_assert(TIER_KIND_COUNT == 5,
    "fixy::dim — substrate TierKind enumerator count has drifted from "
    "the 5-family classification.  fixy/Reject.h's per-Tier dispatch "
    "must be updated alongside.");

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
