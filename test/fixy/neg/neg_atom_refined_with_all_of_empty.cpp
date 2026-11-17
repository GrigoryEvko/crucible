// The empty conjunction written as a refinement witness.
//
// H002 asks a hot binding for a refinement witness, because a hot body
// assumes an invariant that something upstream proved.  AllOf<> is the
// conjunction of no predicates, so it holds for each value and proves
// nothing, as the strict pole pole::pred::True does.  refined_with refuses
// it as its predicate, so no hot binding can name it to silence H002.

#include <fixy/Atom.h>
#include <fixy/Predicates.h>

int main() {
    [[maybe_unused]] ::fixy::atom::refined_with<::fixy::AllOf<>> vacuous_witness{};
    return 0;
}
