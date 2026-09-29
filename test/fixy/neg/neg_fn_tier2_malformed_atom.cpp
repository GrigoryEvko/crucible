// Tier 2: every entry in the pack has to be an atom — a final class
// deriving fixy::atom::atom_of<Axis>, which is what carries the axis the
// entry engages.
//
// This fixture holds tier 2 to ONE diagnostic, and that is the reason it
// exists beside the rule fixtures.  The uniqueness walk of tier 4, every
// collision rule and every corpus entry read each atom's `axis` member.
// If those walks ran on a pack that holds a non-atom, they would add
// many errors after the tier-2 message, and a fixture over that pack
// could reject for the wrong reason.  fn asks the tiers in order and
// stops at the first failure, so the later walks are never instantiated.

#include <fixy/Fn.h>

namespace {
struct not_an_atom {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, not_an_atom> refused{};
    return 0;
}
