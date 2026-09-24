#pragma once

// This header adds nothing to the dimension taxonomy. A new axis, tier,
// trait, or projection belongs in the substrate that owns the taxonomy
// and is aliased here afterwards, so the catalog stays in one place.

#include <crucible/safety/DimensionTraits.h>

#include <meta>
#include <string_view>

namespace crucible::fixy::dim {

using safety::DimensionAxis;
using safety::TierKind;

using safety::dimension_axis_name;
using safety::tier_of_axis;
using safety::tier_of_axis_v;
using safety::tier_kind_name;

using safety::DIMENSION_AXIS_COUNT;
using safety::TIER_KIND_COUNT;

using safety::WrapperKind;
using safety::WRAPPER_KIND_COUNT;
using safety::wrapper_kind_to_axis;
using safety::wrapper_kind_name;
using safety::count_wrappers_on_axis;
using safety::wrapper_for;
using safety::wrapper_for_v;

// Both sides of each comparison read the same enum through reflection,
// so neither assertion fires when the taxonomy grows on purpose. What
// they catch is a substrate count that is maintained by hand and falls
// behind the enum it counts.
//
// The reflection operator does not see through a using-declaration, so
// the operand names the substrate enum rather than the alias above.

static_assert(DIMENSION_AXIS_COUNT == std::meta::enumerators_of(^^safety::DimensionAxis).size(),
              "The published dimension-axis count disagrees with the enumerator "
              "count reflection reads off the enum itself.");

static_assert(TIER_KIND_COUNT == std::meta::enumerators_of(^^safety::TierKind).size(),
              "The published tier-kind count disagrees with the enumerator count "
              "reflection reads off the enum itself.");

// There is no per-axis tier assertion here. The substrate ships one and
// it fires in every translation unit that reaches this header, so a
// mirror would be dead code.

}  // namespace crucible::fixy::dim
