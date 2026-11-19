// Tier 4 refuses one atom stated twice on Effect.  Effect is a Row axis in
// the table of fixy/Axis.h, so it takes the union of its atoms.  A copy of
// an atom adds no effect, and the pack does not say what the second atom
// was meant to state.  The alias with_io names the same atom as with<IO>.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with_io, ::fixy::atom::with<::foundation::effects::Effect::IO>,
                                ::fixy::atom::as_public> refused{};
    return 0;
}
