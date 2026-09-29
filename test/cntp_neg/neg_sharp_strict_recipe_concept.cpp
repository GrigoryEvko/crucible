// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A BITEXACT_STRICT recipe fixes its reduction order, and a switch reduces
// in its own order.  sharp_recipe_laws refuses the recipe.

#include <crucible/cntp/_wip/Sharp.h>

namespace shp = crucible::cntp::_wip::sharp;

struct StrictRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
};

int main() {
    constexpr shp::SharpRecipeLaws laws = shp::sharp_recipe_laws<StrictRecipe>();
    return laws.associative ? 0 : 1;
}
