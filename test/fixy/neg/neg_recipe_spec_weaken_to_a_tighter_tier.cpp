// A RecipeSpec stores its two numerical axes through their order duals,
// so the substrate's weaken() can only relax a claim.  A move from RELAXED
// to BITEXACT would claim a bit-exact result for a value produced under no
// error bound.  The guard in weaken() stops the constant evaluation.

#include <fixy/Bands.h>

int main() {
    constexpr fixy::RecipeSpec<int> relaxed =
        fixy::mint_recipe_spec(7, fixy::Tolerance::RELAXED, fixy::RecipeFamily::Linear);
    constexpr fixy::RecipeSpec<int> tightened = relaxed.weaken({fixy::Tolerance::BITEXACT, fixy::RecipeFamily::Linear});
    return tightened.peek();
}
