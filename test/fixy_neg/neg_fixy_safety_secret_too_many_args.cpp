// fixy_neg: mint_secret<T>(args...) rejects when arity does not match
// any T constructor.
//
// HS14 floor for fixy::safety::mint_secret (re-export of
// safety::mint_secret via fixy/Safety.h).  Distinct from the
// not-constructible-from-type sibling: this fixture exercises the
// arity-mismatch path.  `int` constructors take 0 or 1 args; passing
// 3 ints fails is_constructible_v<int, int, int, int>.
//
// Both fixtures fire the same `is_constructible_v` constraint chain
// from different call shapes — together they pin the constraint as
// the load-bearing gate, not an accident of one particular argument
// type.
//
// Expected diagnostic: "is_constructible_v" — concept-satisfaction
// failure in the requires clause.

#include <crucible/fixy/Safety.h>

namespace fsafety = crucible::fixy::safety;

int main() {
    // int(1, 2, 3) — no matching ctor.  Requires-clause rejects.
    auto bad = fsafety::mint_secret<int>(1, 2, 3);
    (void)bad;
    return 0;
}
