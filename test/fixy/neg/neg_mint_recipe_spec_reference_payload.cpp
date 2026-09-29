// mint_recipe_spec grades an object.  A reference payload would grade a
// value that lives somewhere else, and the door refuses it.

#include <fixy/Bands.h>

int main() {
    int value = 7;
    auto const spec = fixy::mint_recipe_spec<int&>(value, fixy::Tolerance::ULP_FP16, fixy::RecipeFamily::Kahan);
    return spec.peek();
}
