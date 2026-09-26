// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidMerkleRoot from a default MerkleHash, whose
// NSDMI is zero, during constant evaluation.
//
// ValidMerkleRoot is ::fixy::Refined<non_zero, MerkleHash>, and
// ::fixy::mint_refined<non_zero> is its one door.  Zero is the "never
// built" hash: two unbuilt subtrees both hash to zero, so a zero root would
// be accepted as proof of equivalence with anything.  The door's predicate
// refuses it and the constant evaluation fails.
//
// Companion fixture to neg_merkle_root_zero_literal.cpp:
//   - That one spells the zero as a literal.
//   - This one reaches it through the default member initializer, the
//     realistic mode: a fresh node whose merkle_hash was never computed.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>
#include <fixy/Refined.h>

int main() {
    constexpr crucible::ValidMerkleRoot bad = ::fixy::mint_refined<::fixy::non_zero>(crucible::MerkleHash{});
    (void)bad;
    return 0;
}
