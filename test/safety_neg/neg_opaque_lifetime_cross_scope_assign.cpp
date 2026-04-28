// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning an OpaqueLifetime<SCOPE_A, T> to an
// OpaqueLifetime<SCOPE_B, T> when SCOPE_A != SCOPE_B.
//
// Different Scope template arguments produce DIFFERENT class
// instantiations.  No converting assignment operator and no
// implicit conversion between them — the type system enforces
// per-scope disjointness at the assignment surface.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on OpaqueLifetime would
// let a fleet-scoped state silently flow into a request-scoped
// slot, potentially leaking PER_FLEET data across PER_REQUEST
// boundaries (or, in the reverse direction, persisting PER_REQUEST
// data into a PER_FLEET slot).  Both directions are wrong — the
// assignment surface MUST stay per-scope-disjoint.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/OpaqueLifetime.h>

using namespace crucible::safety;

int main() {
    OpaqueLifetime<Lifetime_v::PER_FLEET,   int> fleet_value{42};
    OpaqueLifetime<Lifetime_v::PER_REQUEST, int> request_value{7};

    // Should FAIL: different template instantiations of
    // OpaqueLifetime — no converting assignment exists.
    fleet_value = request_value;
    return fleet_value.peek();
}
