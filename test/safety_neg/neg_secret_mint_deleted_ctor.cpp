// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-L-04 #1520 negative fixture #2/2:
// `safety::mint_secret<T>(args...)` requires
// `std::is_constructible_v<T, Args...>`.  A type T whose
// matching ctor is `= delete`'d is NOT constructible from
// any args matching that ctor signature — the requires-
// clause fails because the constructibility predicate
// answers structurally (does a callable ctor exist at all?),
// not just "any ctor name is in scope".
//
// Distinct from fixture #1/2 (wrong arg type): #2 exercises
// the case where the matching ctor existed in source form
// but was explicitly disabled.  Pairs with
// neg_secret_mint_unconstructible_args.cpp to discharge
// HS14's ≥2-distinct-mismatch floor on the §XXI
// mint_secret gate (closes fixy-L-04 chokepoint claim by
// structurally witnessing the gate fires).
//
// Expected diagnostic: constraints not satisfied /
// no matching function / use of deleted function /
// is_constructible_v / mint_secret.

#include <crucible/safety/Secret.h>

namespace neg_secret_deleted {
// A class whose default ctor is `= delete`'d.  Constructibility
// predicate `std::is_constructible_v<Deleted>` is false even
// though the type itself is otherwise well-formed.  This is the
// canonical copy-paste-from-runtime-object footgun the
// constructibility gate is designed to catch.
struct Deleted {
    Deleted() = delete;
};
}

int main() {
    // Should FAIL: Deleted has no callable default ctor →
    // std::is_constructible_v<Deleted> is false →
    // mint_secret<Deleted>() requires-clause rejects.
    auto s = ::crucible::safety::mint_secret<
        neg_secret_deleted::Deleted>();
    (void)s;
    return 0;
}
