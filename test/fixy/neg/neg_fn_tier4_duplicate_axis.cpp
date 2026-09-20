// Tier 4: an axis carries one grade, so a pack must not name an axis
// twice.  atom::copy and atom::affine both engage Usage, and the pack
// does not say which grade the binding means.
//
// There is no tier 3.  An axis the pack says nothing about resolves to
// its strict pole silently, so a missing atom is not a rejection and
// needs no check — which is the whole difference between this gate and
// the old tree's, where the shortest honest binding named thirty-two
// axes it had nothing to say about.
//
// Like its two siblings, this fixture holds its tier to one diagnostic:
// the duplicate walk answers with an AXIS rather than with an atom, so
// the message names Usage and a reader learns which axis to fix.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::copy, ::fixy::atom::affine> refused{};
    return 0;
}
