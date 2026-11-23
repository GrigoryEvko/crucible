// A binding whose pack states refined_with<IsPositive> takes a proof of that
// predicate.  A Refined of IsNonZero proves a different fact: a negative
// value is not zero, and it is not positive.  So the mint refuses it.

#include <fixy/Fn.h>
#include <fixy/Predicates.h>
#include <fixy/Refined.h>

int main() {
    auto const proof = ::fixy::mint_refined<::fixy::non_zero>(-3);
    auto const binding = ::fixy::mint_fn<int, ::fixy::atom::refined_with<::fixy::IsPositive>>(proof);
    return binding.value() == -3 ? 0 : 1;
}
