// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A switch combines the contributions in the order that they arrive, so a
// recipe that does not commute cannot reduce there.  sharp_recipe_laws
// refuses the recipe.

#include <crucible/cntp/_wip/Sharp.h>

namespace shp = crucible::cntp::_wip::sharp;

struct NonCommutativeRecipe {
    static constexpr bool associative = true;
    static constexpr bool commutative = false;
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED;
};

int main() {
    constexpr shp::SharpRecipeLaws laws = shp::sharp_recipe_laws<NonCommutativeRecipe>();
    return laws.associative ? 0 : 1;
}
