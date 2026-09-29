// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_hash<CallerRow>() is a pure projection.  Init is the row of the
// initialization phase, not of a pure lookup, so the call must fail
// before the body of the lookup is considered.
//
// Expected diagnostic: the Subrow<Row<Init>, Row<>> constraint of
// by_hash is not satisfied.

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
    auto wrong = reg.by_hash<eff::Row<eff::Effect::Init>>(crucible::RecipeHash{0x1234});
    return wrong.has_value() ? 0 : 1;
}
