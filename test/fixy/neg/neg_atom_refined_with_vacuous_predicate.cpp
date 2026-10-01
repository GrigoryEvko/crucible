// The strict Refinement pole written as a refinement witness.
//
// H002 asks a hot binding for a refinement witness, because a hot body
// assumes an invariant that something upstream proved.  pole::pred::True
// is the claim of a binding that names no refinement, and it proves
// nothing.  refined_with refuses it as its predicate, so no binding can
// name it, and no rule can read it as a witness.

#include <fixy/Atom.h>

int main() {
    [[maybe_unused]] ::fixy::atom::refined_with<::fixy::pole::pred::True>* vacuous_witness = nullptr;
    return 0;
}
