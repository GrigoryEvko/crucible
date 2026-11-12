// A combinator is one predicate class, and its call operator calls each
// predicate that it names.  This file specializes the call operator of
// IsPositive for int so that it admits each value, and then mints a value
// under a conjunction that names positive.  The combinator gates its call
// operator on PredicateInvocableOn, so the seal reaches positive inside
// the conjunction and stops the build.

#include <fixy/Refined.h>

template <>
constexpr bool fixy::IsPositive::operator()<int>(int) const noexcept {
    return true;
}

constexpr auto forged = fixy::mint_refined<fixy::all_of<fixy::positive, fixy::bounded_above<9>>>(0);

int main() { return forged.value(); }
