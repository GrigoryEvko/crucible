// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A consumer calls by_name_spec, which gives the RecipeSpec of the two
// axes, but it types the result as the NumericalTier of by_name_pinned.
// The two types are different.  RecipeSpec<T> stores its two-byte grade
// per value, and NumericalTier<T_static, T> pins its tier in the type.
// The conversion of the std::expected must fail.
//
// The sibling fixture pinned_assigned_to_spec_expected catches the other
// direction.
//
// Expected diagnostic: no conversion from the expected of the RecipeSpec
// to the expected of the NumericalTier.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>

#include <expected>

using namespace crucible;

int main() {
    Arena arena{};
    auto test_ctx = ::foundation::effects::testing::test();
    auto init_ctx = ::foundation::effects::testing::init();
    RecipePool pool{::fixy::mint_borrowed_ref(arena), init_ctx};
    RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), test_ctx.alloc};

    std::expected<::fixy::NumericalTier<::fixy::Tolerance::BITEXACT, const NumericalRecipe*>, RecipeError>
        wrong_slot = reg.by_name_spec(recipe_names::kF32Strict);
    (void)wrong_slot;
    return 0;
}
