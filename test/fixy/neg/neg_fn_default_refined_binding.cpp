// A default-built binding that states a refinement.
//
// The Refinement grade is a fact about the value: fixy::IsPositive holds
// for the payload.  A default constructor would make the binding hold
// int{}, which is 0 and fails the predicate, and no mint would check it.
// So a pack that names an atom has no default constructor.

#include <fixy/Fn.h>
#include <fixy/Predicates.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>> unchecked{};
    return 0;
}
