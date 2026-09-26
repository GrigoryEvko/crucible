// A RecipeSpec states the tolerance and the family a value was produced
// under.  The claim is made at mint_recipe_spec, so braces that pair a
// value with both axes have no constructor to reach.

#include <fixy/Bands.h>

int main() {
    fixy::RecipeSpec<int> const spec{7, {fixy::Tolerance::BITEXACT, fixy::RecipeFamily::Any}};
    return spec.peek();
}
