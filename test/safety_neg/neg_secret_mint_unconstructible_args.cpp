// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-L-04 #1520 negative fixture #1/2:
// `safety::mint_secret<T>(args...)` requires
// `std::is_constructible_v<T, Args...>`.  Passing args that
// are NOT a valid constructor pack for T fails the
// requires-clause at the substitution boundary — `int` is
// not constructible from `const char*`, so
// `mint_secret<int>("not_a_number")` is rejected as
// substitution failure on the `is_constructible_v` predicate.
//
// Pairs with neg_secret_mint_deleted_ctor.cpp (distinct
// rejection class: T has a `= delete`'d ctor failing the
// same constructibility predicate from the OPPOSITE
// direction — ctor existed in source form but was
// explicitly disabled).  Together the two fixtures
// discharge HS14's ≥2-distinct-mismatch floor on the
// §XXI mint_secret gate (closes fixy-L-04 chokepoint
// claim by structurally witnessing the gate fires).
//
// Expected diagnostic: constraints not satisfied /
// no matching function / is_constructible_v / mint_secret.

#include <crucible/safety/Secret.h>

int main() {
    // Should FAIL: int is not constructible from const char*.
    // std::is_constructible_v<int, const char*> is false →
    // mint_secret<int>("not_a_number") requires-clause rejects.
    auto s = ::crucible::safety::mint_secret<int>("not_a_number");
    (void)s;
    return 0;
}
