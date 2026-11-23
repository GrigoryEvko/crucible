// A role whose pack states refined_with<IsPositive> takes a proof of that
// predicate.  A Refined of IsNonZero proves a different fact: a negative
// value is not zero, and it is not positive.  So the role mint refuses it.

#include <fixy/Fn.h>
#include <fixy/Predicates.h>
#include <fixy/Refined.h>

namespace {
template <class T>
using positive_count = ::fixy::fn<T, ::fixy::atom::refined_with<::fixy::IsPositive>>;
}  // namespace

int main() {
    auto const proof = ::fixy::mint_refined<::fixy::non_zero>(-3);
    auto const binding = ::fixy::mint_fn_for<positive_count>(proof);
    return binding.value() == -3 ? 0 : 1;
}
