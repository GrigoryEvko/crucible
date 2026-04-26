// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a Stale<U> to a method expecting Stale<T> for
// T ≠ U (cross-payload-type mixing).
//
// Stale<T> is a distinct class template per T; the underlying Graded
// substrate's value field has type T.  Passing a Stale<double> where
// Stale<int> is expected is a type mismatch — no implicit conversion
// is provided (and none should be: the §8 ASGD admission gate reasons
// about staleness ALONGSIDE the typed payload, so cross-T mixing
// would let an int-shaped admission decision flow with a double-
// shaped value's staleness measurement).
//
// Pins the per-T type identity at the wrapper surface.  Without this,
// a refactor that added an implicit converting constructor to Stale
// (e.g. for "interoperability") would silently allow cross-payload
// staleness composition — defeating the type discipline that
// distinguishes "the staleness OF an int gradient" from "the
// staleness OF a double gradient".
//
// Symmetric to the cross-N/cross-Tag wrapper-level tests for
// TimeOrdered, but on the only template-parameter axis Stale has
// (T itself; there's no N or Tag).
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection at the method
// signature.

#include <crucible/safety/Stale.h>

using namespace crucible::safety;

int main() {
    Stale<int>    s_int    = Stale<int>::fresh(10);
    Stale<double> s_double = Stale<double>::fresh(3.14);

    // Should FAIL: Stale<int>::compose_add takes Stale<int> const&;
    // s_double is Stale<double> — different class template
    // instantiation, no implicit conversion.
    auto composed = s_int.compose_add(s_double);
    return composed.peek();
}
