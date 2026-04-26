// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing Refined<Pred, T> when Pred is not
// invocable on T.
//
// `Refined::Refined(T)` carries `requires PredicateInvocableOn
// <Pred, T>` per MIGRATE-2 (#462), turning a SFINAE-cascade inside
// the contract `pre(Pred(v))` clause into a clean concept-violation
// diagnostic at the call site.
//
// `positive` is `[](auto x) { return x > decltype(x){0}; }`.  For
// std::string, neither `s > std::string{0}` nor `std::string{0}` is
// well-formed — Pred(s) is not a valid expression, so the construction
// constructor is removed from the overload set.
//
// This test pins the contract: future Refined refactors must NOT
// drop the construction-site invocability gate (catches
// Refined<positive, std::string> kinds of bugs at the use site
// rather than via cryptic <contracts> errors).

#include <crucible/safety/Refined.h>

using namespace crucible::safety;

// A type with NO operator> defined and NO constructor from int.
// `positive` is `[](auto x) { return x > decltype(x){0}; }`, so
// `positive(NoComparable{})` fails to compile inside the lambda
// body (NoComparable lacks operator> and `NoComparable{0}` is
// ill-formed).
struct NoComparable {};

int main() {
    // `positive` predicate is not invocable on NoComparable.
    // Should FAIL with PredicateInvocableOn constraint violation.
    Refined<positive, NoComparable> bad{NoComparable{}};
    (void)bad;
    return 0;
}
