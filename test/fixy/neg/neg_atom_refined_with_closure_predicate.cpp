// A closure type as the predicate of refined_with.
//
// A capture-less closure is an empty class that the type alone can
// construct, so it has the shape of a refinement predicate.  The name of
// a closure type is not a function of the type: GCC prints it from its
// call signature, so two closures of one signature print one name.  The
// name of an atom is its key in the row hash of each binding, so IsAtom
// refuses the atom at tier 2, and the message names the identity read.

#include <fixy/Fn.h>

using positive_closure = decltype([](int value) noexcept { return value > 0; });

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::refined_with<positive_closure>> refused{};
    return 0;
}
