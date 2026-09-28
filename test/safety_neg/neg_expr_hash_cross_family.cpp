// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 2 of 2 for Expr::hash: a Family-A hash cannot take the
// place of the Family-B hash that the field holds.
//
// Expr::hash is process-local, because the intern table mixes the
// addresses of the child array into it.  Family-A values (ContentHash,
// MerkleHash) go into Cipher and into the merkle DAG, and they must be the
// same in each process.  If the two families are one type, a Family-B
// value can reach a persistent key and break byte stability across
// processes.  Tagged<uint64_t, FamilyA> and Tagged<uint64_t, FamilyB> are
// different types, and the retag catalog has no edge between the two.
//
// The fixture takes the type from the production field, so a change to
// Expr::hash changes what the fixture checks.
//
// Companion: neg_expr_hash_bare_u64_assign.cpp refuses a bare uint64_t.

#include <crucible/Expr.h>

#include <cstdint>
#include <type_traits>

int main() {
    using ExprHash = std::remove_const_t<decltype(crucible::Expr::hash)>;

    auto family_a_value = ::fixy::mint_tagged<crucible::hash_family::FamilyA>(std::uint64_t{0xdeadbeefULL});
    ExprHash family_b_slot = ::fixy::mint_tagged<crucible::hash_family::FamilyB>(std::uint64_t{0x12345678ULL});

    // The compiler must reject this line: the two families are different types.
    family_b_slot = family_a_value;
    return 0;
}
