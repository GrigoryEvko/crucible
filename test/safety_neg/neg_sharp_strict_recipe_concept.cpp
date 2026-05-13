// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-133. BITEXACT_STRICT recipes require a fixed
// reduction order and cannot enter the SHARP compile-time eligibility
// lane.

#include <crucible/cntp/_wip/Sharp.h>

namespace shp = crucible::cntp::_wip::sharp;

struct StrictRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::BITEXACT_STRICT;
};

template <class R>
    requires shp::SharpEligibleRecipe<R>
constexpr bool accepts_sharp_recipe() {
    return true;
}

static_assert(accepts_sharp_recipe<StrictRecipe>());
