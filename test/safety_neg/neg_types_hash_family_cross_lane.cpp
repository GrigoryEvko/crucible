// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Types-3 #1069, mismatch class #2 of 2:
// CROSS-LANE STATIC_ASSERT FAILS.
//
// Each strong hash strong-id is pinned to exactly one family lane.
// ContentHash is Family-A (persistent, byte-stable for Cipher) —
// asserting IsFamilyB<ContentHash> must fail at compile time.  If a
// future regression silently dropped a Family-A specialization or
// gave a hash BOTH family tags, this fixture catches the lane
// confusion before the production header includes it.
//
// Distinct from the unspecialized-type fixture, which fails because
// NO family was declared at all; here both lanes exist but the
// assertion picks the WRONG one.
//
// Expected diagnostic: static assertion failed / WRAP-Types-3 #1069
// / Family-B / IsFamilyB.

#include <crucible/Types.h>

// Should FAIL at compile time: ContentHash is Family-A (Cipher-bound,
// byte-stable identity); IsFamilyB<ContentHash> is false; the static
// assertion below trips.
static_assert(::crucible::IsFamilyB<::crucible::ContentHash>,
    "WRAP-Types-3 #1069 cross-lane fixture: this static_assert MUST "
    "fail at compile time because ContentHash belongs to Family-A.");

int main() { return 0; }
