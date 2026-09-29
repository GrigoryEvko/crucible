// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name_spec<CallerRow>() is the two-axis RecipeSpec projection.  It
// only reads registry state, so a caller row that can block must fail at
// substitution.
//
// Expected diagnostic: the Subrow<Row<Block>, Row<>> constraint of
// by_name_spec is not satisfied.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    crucible::Arena arena{};
    auto init = eff::testing::init();
    auto test = eff::testing::test();
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init};
    crucible::RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), test.alloc};
    auto wrong = reg.by_name_spec<eff::Row<eff::Effect::Block>>(crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
