// every_enumerator_has_exactly_one_atom_ counts the claims of a ladder
// roster on each enumerator of its enum.  The assertions in the check
// files of the atom headers all pass, so none of them shows the walk
// answering no for a real family.
//
// This roster holds three regime atoms for the three HotPathTier
// enumerators, but it names warm two times and cold not at all.  A count
// of atoms passes, and the walk must answer no.
//
// The condition is written as a comparison so the compiler reports the
// value it reduced to.  A bare call would give only the message below,
// which is the fixture's own text and proves nothing on its own.

#include <fixy/atoms/Regime.h>

#include <tuple>

int main() {
    using warm_twice_roster =
        std::tuple<fixy::atom::regime::hot, fixy::atom::regime::warm, fixy::atom::regime::warm>;
    constexpr bool claims_each_tier_once =
        fixy::atom::detail::every_enumerator_has_exactly_one_atom_<warm_twice_roster,
                                                                   foundation::algebra::lattices::HotPathTier>();
    static_assert(claims_each_tier_once == true, "a regime roster that names warm two times claims each tier once");
    return 0;
}
