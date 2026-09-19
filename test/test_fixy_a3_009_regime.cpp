#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/_HotPath.h>
#include <crucible/safety/Progress.h>

namespace cs = ::crucible::safety;

namespace {

using HotInt = cs::HotPath<cs::HotPathTier_v::Hot, int>;
using WarmInt = cs::HotPath<cs::HotPathTier_v::Warm, int>;
using ColdInt = cs::HotPath<cs::HotPathTier_v::Cold, int>;

using TermInt = cs::Progress<cs::ProgressClass_v::Terminating, int>;
using BoundInt = cs::Progress<cs::ProgressClass_v::Bounded, int>;
using DivInt = cs::Progress<cs::ProgressClass_v::MayDiverge, int>;

static_assert(cs::wrapper_dimension_v<HotInt> == cs::DimensionAxis::Regime,
              "HotPath<Hot> must classify on Regime, not Complexity. HotPath wraps "
              "foreground and background residency. Complexity tracks asymptotic "
              "cost and termination class.");
static_assert(cs::wrapper_dimension_v<WarmInt> == cs::DimensionAxis::Regime, "HotPath<Warm> must classify on Regime.");
static_assert(cs::wrapper_dimension_v<ColdInt> == cs::DimensionAxis::Regime, "HotPath<Cold> must classify on Regime.");

static_assert(cs::wrapper_dimension_v<TermInt> == cs::DimensionAxis::Complexity,
              "Progress<Terminating> stays on Complexity. Bounded, Terminating and "
              "Diverges name an asymptotic cost class, not an operating regime.");
static_assert(cs::wrapper_dimension_v<BoundInt> == cs::DimensionAxis::Complexity,
              "Progress<Bounded> stays on Complexity.");
static_assert(cs::wrapper_dimension_v<DivInt> == cs::DimensionAxis::Complexity,
              "Progress<MayDiverge> stays on Complexity.");

static_assert(cs::wrapper_dimension_v<HotInt> != cs::DimensionAxis::Complexity, "HotPath leaked back onto Complexity.");
static_assert(cs::wrapper_dimension_v<ColdInt> != cs::DimensionAxis::Complexity,
              "HotPath<Cold> leaked back onto Complexity.");

static_assert(cs::wrapper_dimension_v<HotInt> != cs::wrapper_dimension_v<TermInt>,
              "HotPath and Progress must live on distinct axes. They answer "
              "different questions: where work runs, and what it costs.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime) == cs::TierKind::Semiring,
              "Regime classifies on Tier S, with par=join and seq=join, so the "
              "hottest tier wins.");
static_assert(cs::wrapper_tier_v<HotInt> == cs::TierKind::Semiring, "HotPath wrappers carry a Tier-S surface.");
static_assert(cs::wrapper_tier_v<ColdInt> == cs::TierKind::Semiring, "HotPath<Cold> carries a Tier-S surface.");

static_assert(cs::verify_quadruple<HotInt>(), "verify_quadruple<HotPath<Hot>> must hold under the Regime "
                                              "classification.");
static_assert(cs::verify_quadruple<WarmInt>(), "verify_quadruple<HotPath<Warm>> must hold.");
static_assert(cs::verify_quadruple<ColdInt>(), "verify_quadruple<HotPath<Cold>> must hold.");
static_assert(cs::verify_quadruple<TermInt>(), "verify_quadruple<Progress<Terminating>> must hold. Progress stays "
                                               "on Complexity.");

// A floor, not an exact count. The exact count is pinned beside the
// enumerator definition, so this witness catches only the removal of an
// arm, which that pin cannot see.
static_assert(cs::DIMENSION_AXIS_COUNT >= 22, "floor: DimensionAxis cardinality regressed below 22. An enumerator "
                                              "was removed without updating the exact pin beside the enumerator "
                                              "definition and this floor witness.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::Regime) == std::string_view{"Regime"},
              "dimension_axis_name must return \"Regime\" for this axis. A sentinel "
              "leak means a missing switch arm.");

}  // namespace

int main() { return 0; }
