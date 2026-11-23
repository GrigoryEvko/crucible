// A binding whose pack states refined_with<IsPositive> claims that its value
// is positive.  Its mint takes a Refined of that predicate, whose own mint
// ran the predicate.  A plain int carries no such proof, so the mint refuses
// it, and no binding claims a fact that no check made.

#include <fixy/Fn.h>
#include <fixy/Predicates.h>

int main() {
    int const count = 3;
    auto const binding = ::fixy::mint_fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>>(count);
    return binding.value() == 3 ? 0 : 1;
}
