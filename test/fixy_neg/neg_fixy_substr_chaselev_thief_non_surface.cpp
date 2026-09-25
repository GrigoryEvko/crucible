// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 3 of 8:
// `safety::proto::chaselev_session::mint_chaselev_thief<Deque>(deque)`
// (single-arg overload, no fractional proof) rejects when Deque
// is NOT a ChaseLevSessionSurface.
//
// Mirrors fixture #1 (owner_non_surface) on the thief side:
// proves that the ChaseLevSessionSurface concept gate fires
// INDEPENDENTLY of the owner-side instantiation.
//
// Distinct from fixture #4 (thief_wrong_proof): #3 exercises the
// concept gate on the Deque parameter (single-arg overload); #4
// exercises the proof parameter binding on the two-arg overload
// AFTER the concept is satisfied.
//
// Expected diagnostic: "ChaseLevSessionSurface" / "constraints
// not satisfied" / "no matching function" / "mint_chaselev_thief".

#include <crucible/sessions/_ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;

int main() {
    int not_a_deque = 0;

    auto bad = fchase::mint_chaselev_thief(not_a_deque);
    (void)bad;
    return 0;
}
