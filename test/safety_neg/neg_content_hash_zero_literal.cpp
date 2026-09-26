// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidContentHash from a ContentHash spelled with a
// literal zero, during constant evaluation.
//
// ValidContentHash is ::fixy::Refined<non_zero, ContentHash>, and
// ::fixy::mint_refined<non_zero> is its one door.  Zero is the "never
// folded" hash, so the door's predicate refuses it and the constant
// evaluation fails.
//
// Companion fixture to neg_content_hash_default_construct.cpp:
//   - This one catches a caller that hand-spells a zero hash.
//   - That one reaches zero through the default member initializer.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidContentHash bad =
        ::fixy::mint_refined<::fixy::non_zero>(crucible::ContentHash{uint64_t{0}});
    (void)bad;
    return 0;
}
