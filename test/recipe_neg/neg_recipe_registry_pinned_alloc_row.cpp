// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// by_name_pinned<T, CallerRow>() does a lookup and a tier admission over
// registry state that does not change.  The projection does not
// allocate, so an Alloc row must fail the pure-row gate.
//
// Expected diagnostic: the Subrow<Row<Alloc>, Row<>> constraint of
// by_name_pinned is not satisfied.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
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
    auto wrong = reg.by_name_pinned<::fixy::Tolerance::BITEXACT, eff::Row<eff::Effect::Alloc>>(
        crucible::recipe_names::kF32Strict);
    return wrong.has_value() ? 0 : 1;
}
