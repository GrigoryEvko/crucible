// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Expr-1 #911, mismatch class #1 of 2:
// BARE u64 CANNOT MASQUERADE AS Tagged<u64, hash_family::FamilyB>.
//
// After the migration, Expr::hash is Tagged<uint64_t,
// hash_family::FamilyB> (regime-1 EBO-collapsed to 8B).  A bare
// uint64_t has no FamilyB provenance witness; assigning it to a
// Tagged slot is rejected by Tagged's explicit-only ctor + no
// assignment-from-T operator.
//
// If this fixture starts compiling, the wrap silently degraded back
// to bare uint64_t and every Family-B vs Family-A discipline at the
// Expr::hash boundary disappeared (in particular, a future
// regression could feed Expr::hash into a Family-A computation
// — Cipher key, merkle_hash, ContentHash — without compile-time
// rejection).
//
// Distinct from the cross-family fixture, which fails because TWO
// Tagged wrappers (different families) collide; here the failure is
// a bare uint64_t trying to land in a wrapped slot.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/safety/Tagged.h>
#include <crucible/Types.h>

#include <cstdint>

int main() {
    using FamilyBHash = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::hash_family::FamilyB>;

    FamilyBHash slot{std::uint64_t{0xdeadbeefULL}};

    // Should FAIL: bare std::uint64_t has no implicit conversion to
    // Tagged<uint64_t, hash_family::FamilyB>.  The explicit ctor
    // exists; cross-T assignment does not.
    slot = std::uint64_t{0x12345678ULL};

    return 0;
}
