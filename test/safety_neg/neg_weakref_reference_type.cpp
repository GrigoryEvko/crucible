// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::WeakRef<T> (#1084 - WeakRef piece).
//
// Premise: WeakRef<T> constrains T to an OBJECT type via
// `requires (std::is_object_v<T>)`.  A reference type (here int&) is NOT
// an object type, so the class has no valid specialization.  This is the
// CLASS-TEMPLATE rejection: the constraint fires before any member is
// instantiated, so `WeakRef<int&>` simply does not exist.
//
// Why it must reject: WeakRef stores a `T* ptr_`.  For T = int&, `T*`
// would be a "pointer to reference", which is ill-formed; the
// is_object_v gate turns that into a clean, early "constraints not
// satisfied" diagnostic instead of a confusing deep-instantiation error,
// and simultaneously rejects void and function types.
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "no matching template" pointing at
// the WeakRef<int&> instantiation.

#include <crucible/safety/WeakRef.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: int& is not an object type → is_object_v<int&> is
    // false → the class-template requires-clause rejects.
    saf::WeakRef<int&> bad{};
    (void)bad;
    return 0;
}
