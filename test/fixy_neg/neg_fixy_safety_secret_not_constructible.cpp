// fixy_neg: mint_secret rejects when args fail T's constructibility.
//
// HS14 floor for fixy::safety::mint_secret (re-export of
// safety::mint_secret via fixy/Safety.h).  The mint factory's
// requires clause is `std::is_constructible_v<T, Args...>`; passing
// arguments that cannot construct T fires constraint-satisfaction
// failure at overload resolution.
//
// Here we attempt `mint_secret<int, const char*>("hello")` — int is
// not constructible from `const char*`.  No fallback, no narrowing
// path; the constraint fires.
//
// Expected diagnostic: "is_constructible_v" — concept-satisfaction
// failure in the requires clause.

#include <crucible/fixy/Safety.h>

namespace fsafety = crucible::fixy::safety;

int main() {
    // int is not constructible from const char*.  mint_secret's
    // requires-clause rejects this call.
    auto bad = fsafety::mint_secret<int>("hello");
    (void)bad;
    return 0;
}
