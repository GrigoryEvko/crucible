// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (RecipeSpec, fourth and final product wrapper) —
// pins constrained-extractor.  recipe_spec_value_t is constrained on
// `requires is_recipe_spec_v<T>`.
//
// Single fixture (vs two for NTTP wrappers) — same product-wrapper
// rationale: no compile-time tag exists.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsRecipeSpec.h>

int main() {
    using V = crucible::safety::extract::recipe_spec_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
