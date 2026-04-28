// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling OpaqueLifetime<NarrowerScope, T>::relax<WiderScope>()
// when WiderScope > NarrowerScope in the LifetimeLattice.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `LifetimeLattice::leq(NarrowerScope, Scope)` to a permissive form
// — would silently let a request-scoped state widen to fleet scope,
// LEAKING REQUEST-SCOPED DATA ACROSS REQUESTS.  This is the
// load-bearing bug class FOUND-G09 fences at compile time:
// inferlet user state declared OpaqueLifetime<PER_REQUEST, PdaState>
// for grammar-constrained decoding cannot accidentally be persisted
// to PER_FLEET cold storage.
//
// The lattice direction: PER_FLEET is HIGHER than PER_REQUEST in
// the chain (top of lattice; longest-lived).  Going DOWN (PER_FLEET
// → PER_PROGRAM → PER_REQUEST) is allowed — wider availability
// trivially serves narrower requirement.  Going UP (PER_REQUEST →
// PER_FLEET) is FORBIDDEN — the request-scoped value dies when the
// request ends; widening its scope would cross-request leak.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/OpaqueLifetime.h>

using namespace crucible::safety;

int main() {
    OpaqueLifetime<Lifetime_v::PER_REQUEST, int> request_value{42};

    // Should FAIL: relax<PER_FLEET> on a PER_REQUEST-pinned wrapper.
    // The requires-clause `LifetimeLattice::leq(PER_FLEET, PER_REQUEST)`
    // is FALSE — PER_FLEET is above PER_REQUEST — so the relax<>
    // overload is excluded.  Without this fence a PER_REQUEST state
    // would silently widen to PER_FLEET cold-tier persistence, leaking
    // request-scoped data across requests.
    auto fleet = std::move(request_value).relax<Lifetime_v::PER_FLEET>();
    return fleet.peek();
}
