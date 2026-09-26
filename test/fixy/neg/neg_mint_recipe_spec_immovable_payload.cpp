// mint_recipe_spec moves the payload into the carrier.  A payload that
// cannot move cannot get there, and the door refuses it at its constraint
// rather than in the carrier's body.

#include <fixy/Bands.h>

namespace {

struct Pinned {
    int v = 0;
    Pinned() = default;
    Pinned(Pinned&&) = delete;
};

}  // namespace

int main() {
    auto const spec = fixy::mint_recipe_spec<Pinned>(Pinned{}, fixy::Tolerance::ULP_FP16, fixy::RecipeFamily::Kahan);
    return spec.peek().v;
}
