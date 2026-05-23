// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Expr-1 #911, mismatch class #2 of 2:
// Tagged<u64, FamilyA> CANNOT MASQUERADE AS Tagged<u64, FamilyB>.
//
// Expr::hash carries the Family-B (process-local intern key) lane;
// it MUST NOT silently swap with a Family-A hash (ContentHash,
// MerkleHash, etc.) because Family-A values flow into Cipher and
// merkle-DAG persistence — accidentally feeding the ASLR-mixed
// Family-B value into a Family-A computation breaks byte-stability
// across processes (CRUCIBLE.md §10).
//
// Tagged<u64, FamilyA> and Tagged<u64, FamilyB> are distinct types
// (different NTTPs); assignment between them is rejected by Tagged's
// fail-closed retag_policy primary template (no admission specified
// between the two families).
//
// Distinct from the bare-u64 fixture which fails because the SOURCE
// side has no wrap at all; here both sides ARE wrapped, but the
// source-lattice families disagree.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/safety/Tagged.h>
#include <crucible/Types.h>

#include <cstdint>

int main() {
    using FamilyAHash = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::hash_family::FamilyA>;
    using FamilyBHash = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::hash_family::FamilyB>;

    FamilyAHash family_a_value{std::uint64_t{0xdeadbeefULL}};
    FamilyBHash family_b_slot{std::uint64_t{0x12345678ULL}};

    // Should FAIL: Tagged<u64, FamilyA> and Tagged<u64, FamilyB>
    // are unrelated wrapper types (distinct family NTTPs); no
    // implicit conversion / retag_policy admission exists between
    // them.
    family_b_slot = family_a_value;

    return 0;
}
