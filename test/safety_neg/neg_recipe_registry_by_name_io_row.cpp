// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name<CallerRow>() is a pure projection over registry state that
// does not change.  A caller that declares the IO effect must fail the
// Subrow<CallerRow, Row<>> gate.
//
// Expected diagnostic: the Subrow<Row<IO>, Row<>> constraint of by_name
// is not satisfied.

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
    auto wrong = reg.by_name<eff::Row<eff::Effect::IO>>(crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
