// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A consumer calls by_name_pinned<BITEXACT>, which gives the
// NumericalTier of one axis, but it types the result as the RecipeSpec of
// by_name_spec.  The NumericalTier has no family axis, so the result
// cannot go to fixy::admits.  The conversion of the fixy::Result must
// fail.
//
// The sibling fixture spec_assigned_to_pinned_expected catches the other
// direction.
//
// Expected diagnostic: no conversion from the Result of the
// NumericalTier to the Result of the RecipeSpec.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

using namespace crucible;

int main() {
    ::fixy::BumpArena arena{};
    auto test_ctx = ::foundation::effects::testing::test();
    auto init_ctx = ::foundation::effects::testing::init();
    RecipePool pool{::fixy::mint_borrowed_ref(arena), init_ctx};
    RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), test_ctx.alloc};

    ::fixy::Result<::fixy::RecipeSpec<RecipeRegistry::RecipeRef>, RecipeError> wrong_slot =
        reg.by_name_pinned<::fixy::Tolerance::BITEXACT>(recipe_names::kF32Strict);
    (void)wrong_slot;
    return 0;
}
