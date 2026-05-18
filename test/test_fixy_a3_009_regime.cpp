// fixy-A3-009 sentinel TU: HotPath dimensional reclassification.
//
// Pre-fix:  wrapper_dimension<HotPath<Tier, T>> == DimensionAxis::Complexity
//           wrapper_dimension<Progress<Cls, T>> == DimensionAxis::Complexity
// Post-fix: HotPath → DimensionAxis::Regime (new axis 22)
//           Progress stays on Complexity (legitimate Termination/cost-class
//                  fit — Bounded / Terminating / Diverges is asymptotic cost).
//
// Complexity (fixy.md §24.1) classifies asymptotic cost / termination
// posture.  HotPath classifies foreground-vs-background OPERATING REGIME
// (Hot / Warm / Cold tier residency).  Folding both onto Complexity
// erased the orthogonality between "what work this does" (Progress) and
// "where this work is allowed to run" (HotPath).  Regime is the new
// per-axis identity: a Tier-S (Semiring) axis with par=join (hottest-
// wins) and seq=join semantics, peer to Synchronization.
//
// Tier preservation: HotPath stays at TierKind::Semiring under the new
// Regime axis (same Tier S surface).  GAPS-091's verify_quadruple<W>()
// continues to hold because lattice/modality/tier remain the same.

#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Progress.h>

namespace cs = ::crucible::safety;

namespace {

using HotInt    = cs::HotPath<cs::HotPathTier_v::Hot,  int>;
using WarmInt   = cs::HotPath<cs::HotPathTier_v::Warm, int>;
using ColdInt   = cs::HotPath<cs::HotPathTier_v::Cold, int>;

using TermInt   = cs::Progress<cs::ProgressClass_v::Terminating, int>;
using BoundInt  = cs::Progress<cs::ProgressClass_v::Bounded,     int>;
using DivInt    = cs::Progress<cs::ProgressClass_v::MayDiverge, int>;

// ── Post-fix dimensional identity — HotPath on Regime, not Complexity.
static_assert(cs::wrapper_dimension_v<HotInt>
              == cs::DimensionAxis::Regime,
    "fixy-A3-009: HotPath<Hot> must classify on Regime, not Complexity.  "
    "HotPath wraps foreground/background operating-regime residency; "
    "Complexity tracks asymptotic cost / termination class.");
static_assert(cs::wrapper_dimension_v<WarmInt>
              == cs::DimensionAxis::Regime,
    "fixy-A3-009: HotPath<Warm> must classify on Regime.");
static_assert(cs::wrapper_dimension_v<ColdInt>
              == cs::DimensionAxis::Regime,
    "fixy-A3-009: HotPath<Cold> must classify on Regime.");

// ── Progress stays on Complexity (legitimate cost-class fit).
static_assert(cs::wrapper_dimension_v<TermInt>
              == cs::DimensionAxis::Complexity,
    "fixy-A3-009: Progress<Terminating> stays on Complexity — Bounded / "
    "Terminating / Diverges is asymptotic cost class, not an operating "
    "regime.  Only HotPath migrated off Complexity.");
static_assert(cs::wrapper_dimension_v<BoundInt>
              == cs::DimensionAxis::Complexity,
    "fixy-A3-009: Progress<Bounded> stays on Complexity.");
static_assert(cs::wrapper_dimension_v<DivInt>
              == cs::DimensionAxis::Complexity,
    "fixy-A3-009: Progress<MayDiverge> stays on Complexity.");

// ── Negative-direction guards — HotPath is NOT on Complexity.
static_assert(cs::wrapper_dimension_v<HotInt>
              != cs::DimensionAxis::Complexity,
    "fixy-A3-009 regression: HotPath leaked back onto Complexity.");
static_assert(cs::wrapper_dimension_v<ColdInt>
              != cs::DimensionAxis::Complexity,
    "fixy-A3-009 regression: HotPath<Cold> leaked back onto Complexity.");

// ── HotPath and Progress are dimensionally distinct under the fix.
static_assert(cs::wrapper_dimension_v<HotInt>
              != cs::wrapper_dimension_v<TermInt>,
    "fixy-A3-009: HotPath and Progress must live on distinct axes — they "
    "answer different questions (regime vs. cost-class).");

// ── Tier preservation — Regime is Tier S (Semiring).
static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime)
              == cs::TierKind::Semiring,
    "fixy-A3-009: Regime classifies on Tier S (par=join, seq=join — "
    "hottest-wins semantics).");
static_assert(cs::wrapper_tier_v<HotInt>  == cs::TierKind::Semiring,
    "fixy-A3-009: HotPath wrappers preserve Tier-S surface post-reclassify.");
static_assert(cs::wrapper_tier_v<ColdInt> == cs::TierKind::Semiring,
    "fixy-A3-009: HotPath<Cold> preserves Tier-S surface post-reclassify.");

// ── GAPS-091 cross-product verifier — still passes after reclassify.
static_assert(cs::verify_quadruple<HotInt>(),
    "fixy-A3-009: GAPS-091 verify_quadruple<HotPath<Hot>> must hold under "
    "the new Regime classification.");
static_assert(cs::verify_quadruple<WarmInt>(),
    "fixy-A3-009: GAPS-091 verify_quadruple<HotPath<Warm>> must hold.");
static_assert(cs::verify_quadruple<ColdInt>(),
    "fixy-A3-009: GAPS-091 verify_quadruple<HotPath<Cold>> must hold.");
static_assert(cs::verify_quadruple<TermInt>(),
    "fixy-A3-009: GAPS-091 verify_quadruple<Progress<Terminating>> must "
    "hold — Progress stays on Complexity.");

// ── Catalog cardinality — Regime grew the axis count from 21 to 22.
static_assert(cs::DIMENSION_AXIS_COUNT == 22,
    "fixy-A3-009: DimensionAxis catalog must equal 22 (21 + Regime "
    "extension added 2026-05-18; was 21 after fixy-A3-008).");

// ── Regime carries a non-empty, non-sentinel name.
static_assert(cs::dimension_axis_name(cs::DimensionAxis::Regime)
              == std::string_view{"Regime"},
    "fixy-A3-009: dimension_axis_name must return \"Regime\" for the new "
    "axis; sentinel leak indicates a missing switch arm.");

// ── Tier S (Semiring) growth — covered by DimensionTraits.h's internal
//     count_dims_in_tier(Semiring) == 17 static_assert at file scope.
//     Surfacing the count here would require exposing detail:: helpers;
//     the substrate's own self-test is the authoritative witness.

}  // namespace

int main() { return 0; }
