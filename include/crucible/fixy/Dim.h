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

#include <cstdint>
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

// ─── Per-dim Tier sanity — every dim classifies to a known Tier ────
//
// Uses P1306R5 `template for` over reflection-discovered enumerators.
// If a new enumerator lands and `tier_of_axis` doesn't grow an arm,
// the inner switch falls through to TierKind{0xFF} and this assertion
// fires.

namespace detail {

[[nodiscard]] consteval bool every_axis_has_known_tier() noexcept {
    // Reflect on the substrate-qualified name; GCC 16 rejects ^^ on a
    // using-declaration (fixy::dim::DimensionAxis is a using-alias),
    // so we go through ::crucible::safety::DimensionAxis directly.
    //
    // `template for` re-declares the induction variable on every
    // expansion; -Werror=shadow fires on the second iteration.
    // Suppression follows the substrate pattern from
    // safety/DimensionTraits.h's every_dimension_axis_has_tier.
    static constexpr auto dim_known_tier_axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : dim_known_tier_axes) {
        constexpr TierKind t = tier_of_axis([:en:]);
        if (static_cast<std::uint8_t>(t) >= TIER_KIND_COUNT) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

static_assert(detail::every_axis_has_known_tier(),
    "fixy::dim — at least one DimensionAxis enumerator does not "
    "classify to a known TierKind via safety::tier_of_axis.  Likely "
    "cause: a new enumerator was appended to DimensionAxis without "
    "adding the matching case arm to tier_of_axis's switch.  See "
    "safety/DimensionTraits.h's extension policy block.");

}  // namespace crucible::fixy::dim
