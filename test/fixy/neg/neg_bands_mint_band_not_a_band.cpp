// mint_band builds a band: the Absolute carrier over one pinned tier.  A
// RecipeSpec stores its grade beside the value, so it is not a band, and
// its door is mint_recipe_spec.

#include <fixy/Bands.h>

int main() {
    auto const spec = fixy::mint_band<fixy::RecipeSpec<int>>(7);
    return spec.peek();
}
