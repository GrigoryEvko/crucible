// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidMerkleRoot with a literal-zero
// MerkleHash{uint64_t{0}} in constexpr context — the explicit-zero
// mismatch fixture for the Tx::commit non-zero gate (and the
// computed_merkle_hash() accessor's `pre(is_non_zero(merkle_hash))`
// clause).
//
// Per #937 WRAP-MerkleDag-1, ValidMerkleRoot is
// safety::Refined<safety::non_zero, MerkleHash>.  MerkleHash{0} is
// the structural sentinel for "subtree never built" (TERMINAL nodes
// legitimately carry it as a leaf marker, but it is never a valid
// commit-root).  Without the gate, an adversarial / buggy caller
// committing MerkleHash{0} would (1) corrupt tx_log's
// step-by-merkle invariant (a zero matches the default Transaction's
// merkle_root field), (2) collide with TERMINAL leaves in dag_diff
// equality short-circuits, and (3) propagate as a no-op replay key
// into federation round-trip flows.
//
// Companion fixture: neg_merkle_root_default_construct.cpp
//   * That one is the wide miss (= default-constructed MerkleHash{},
//     where zero is reached via the strong-id NSDMI rather than an
//     explicit literal).
//   * This one is the explicit-literal mismatch (= MerkleHash{0}).
//     Catches drift where a caller silently passes a zero-init value
//     from a corrupt-source path (e.g. a freshly allocated RegionNode
//     whose merkle_hash field has not yet been populated by
//     recompute_merkle).
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/MerkleDag.h>
#include <crucible/Types.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`non_zero(v)`) to be exercised at compile time.
    // MerkleHash{uint64_t{0}}.raw() == 0 → non_zero(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidMerkleRoot bad{
        crucible::MerkleHash{uint64_t{0}}};
    (void)bad;
    return 0;
}
