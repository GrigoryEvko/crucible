// Negative-compile test.  This file must fail to compile.
//
// hash_family_of, mismatch class #2 of 2:
// Cross-lane static_assert fails.
//
// Each strong hash strong-id is pinned to exactly one family lane.
// ContentHash is Family-A (persistent, byte-stable for Cipher) —
// asserting IsFamilyB<ContentHash> must fail at compile time.  If a
// future regression silently dropped a Family-A specialization or
// gave a hash both family tags, this fixture catches the lane
// confusion before the production header includes it.
//
// Distinct from the unspecialized-type fixture, which fails because
// No family was declared at all; here both lanes exist but the
// assertion picks the wrong one.
//
// Expected diagnostic: static assertion failed, because the family of
// ContentHash is not hash_family::FamilyB.

#include <crucible/Types.h>

// Should fail at compile time: ContentHash is Family-A (Cipher-bound,
// byte-stable identity); IsFamilyB<ContentHash> is false; the static
// assertion below trips.
static_assert(::crucible::IsFamilyB<::crucible::ContentHash>,
              "Cross-lane fixture: this static_assert MUST "
              "fail at compile time because ContentHash belongs to Family-A.");

int main() { return 0; }
