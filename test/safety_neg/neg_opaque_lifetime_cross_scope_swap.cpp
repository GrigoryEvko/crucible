// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing OpaqueLifetime<SCOPE_A, T> with
// OpaqueLifetime<SCOPE_B, T> when SCOPE_A != SCOPE_B.
//
// swap() takes a reference to the SAME class — a member taking
// `OpaqueLifetime<Scope, T>&`.  Cross-scope swap is rejected at
// overload resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor adding cross-scope
// swap (perhaps for SoA gather where multiple lifetime scopes
// cohabit one buffer) would let scope labels swap independently
// of value bytes — a tier-label vs value-bytes disjointness that
// breaks every downstream Cipher tier-promotion consumer.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/OpaqueLifetime.h>
#include <utility>

using namespace crucible::safety;

int main() {
    OpaqueLifetime<Lifetime_v::PER_FLEET,   int> fleet_value{42};
    OpaqueLifetime<Lifetime_v::PER_REQUEST, int> request_value{7};

    // Should FAIL: OpaqueLifetime<PER_FLEET, int>::swap takes
    // OpaqueLifetime<PER_FLEET, int>&; request_value is a
    // different type.
    fleet_value.swap(request_value);

    // Free-function (ADL) swap reaches the same rejection.
    using std::swap;
    swap(fleet_value, request_value);

    return fleet_value.peek();
}
