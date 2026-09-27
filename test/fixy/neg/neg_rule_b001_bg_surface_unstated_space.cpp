// B001: a background observable surface that states no space.
//
// A space is a fact about the binding, and the strict pole of the Space
// axis states nothing, so a binding that states no space grade can hold
// unbounded data.  With a linear cost the surface is still unbounded in what
// it holds, and a producer that cannot see its consumer fall behind fills
// it.  The rule reads an unstated space as it reads an unstated cost.
//
// The Security grade is public, so the corpus entry for a classified Bg
// crossing does not apply, and only B001 refuses the pack.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with<::foundation::effects::Effect::Bg>,
                                ::fixy::atom::observe::surface<::foundation::effects::Effect::Bg>,
                                ::fixy::atom::cost_linear<8>, ::fixy::atom::as_public>
        refused{};
    return 0;
}
