// Tier 4 refuses one atom stated twice on Observability.  Observability is
// a Row axis in the table of fixy/Axis.h, so it takes the union of its
// atoms.  A copy of a surface adds no effect, and the pack does not say
// what the second surface was meant to state.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<
        int, ::fixy::atom::with_io, ::fixy::atom::observe::surface<::foundation::effects::Effect::IO>,
        ::fixy::atom::observe::surface<::foundation::effects::Effect::IO>, ::fixy::atom::as_public> refused{};
    return 0;
}
