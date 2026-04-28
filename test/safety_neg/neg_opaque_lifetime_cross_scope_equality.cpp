// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing OpaqueLifetime<SCOPE_A, T> with
// OpaqueLifetime<SCOPE_B, T> via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Scope, T) instantiation has its OWN
// friend taking two OpaqueLifetime<Scope, T>&.  Cross-scope
// comparison fails to find a viable operator==.
//
// Concrete bug-class this catches: a refactor that introduced a
// template friend operator==(OpaqueLifetime<...,A>, OpaqueLifetime
// <...,B>) would silently let lifetime-scope mismatches at the
// comparison surface escape detection — every site doing
// `if (fleet_state == request_replica) ...` would compile and
// silently compare bytes across scopes, hiding cases where the
// two values came from incompatible scope contexts.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/OpaqueLifetime.h>

using namespace crucible::safety;

int main() {
    OpaqueLifetime<Lifetime_v::PER_FLEET,   int> fleet_value{42};
    OpaqueLifetime<Lifetime_v::PER_REQUEST, int> request_value{42};

    // Should FAIL: operator== for OpaqueLifetime<PER_FLEET, int>
    // takes two OpaqueLifetime<PER_FLEET, int>&; request_value is
    // OpaqueLifetime<PER_REQUEST, int>, no implicit conversion.
    return static_cast<int>(fleet_value == request_value);
}
