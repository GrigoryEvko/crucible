// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidContentHash from a default ContentHash, whose
// NSDMI is zero, during constant evaluation.
//
// ValidContentHash is ::fixy::Refined<non_zero, ContentHash>, declared in
// MerkleDag.h beside ValidMerkleRoot, and ::fixy::mint_refined<non_zero> is
// its one door.  Zero is the "never folded" hash, so the door's predicate
// refuses it and the constant evaluation fails.
//
// Companion fixture to neg_content_hash_zero_literal.cpp:
//   - That one spells the zero as a literal.
//   - This one reaches it through the default member initializer, the
//     realistic mode: a fresh RegionNode or TraceGraph whose content_hash
//     was never folded.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>
#include <fixy/Refined.h>

int main() {
    constexpr crucible::ValidContentHash bad = ::fixy::mint_refined<::fixy::non_zero>(crucible::ContentHash{});
    (void)bad;
    return 0;
}
