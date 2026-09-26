// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidMerkleRoot from a MerkleHash spelled with a
// literal zero, during constant evaluation.
//
// ValidMerkleRoot is ::fixy::Refined<non_zero, MerkleHash>, and
// ::fixy::mint_refined<non_zero> is its one door.  The door's predicate
// refuses zero and the constant evaluation fails.
//
// Companion fixture to neg_merkle_root_default_construct.cpp:
//   - This one catches a caller that hand-spells a zero root.
//   - That one reaches zero through the default member initializer.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidMerkleRoot bad =
        ::fixy::mint_refined<::fixy::non_zero>(crucible::MerkleHash{uint64_t{0}});
    (void)bad;
    return 0;
}
