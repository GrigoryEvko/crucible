// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// IsAtom refuses refined_with over a closure type, because the name of a
// closure type is not a function of the type, and the name of an atom is
// its key in the row hash.  Here a translation unit tries to put a
// verdict of no refusal in front of the reads of IsAtom, through a
// variable template of the detail namespace.  No such template exists.
// The gate calls the refusal function itself, and a concept has no
// specialization, so tier 2 of fixy::fn refuses the atom.
//
// Expected diagnostic: the tier-2 assertion of fixy::fn names the atom
// and the identity read that refuses it.

#include <fixy/Fn.h>

using positive_closure = decltype([](int value) noexcept { return value > 0; });

template <>
inline constexpr fixy::atom::detail::atom_refusal
    fixy::atom::detail::atom_refusal_v<fixy::atom::refined_with<positive_closure>> =
        fixy::atom::detail::atom_refusal::none;

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::refined_with<positive_closure>> refused{};
    return 0;
}
