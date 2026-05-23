// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PROD-WRAP-6 #535, mismatch class #1 of 2:
// CONSTRUCTING `ValidContentHash` WITH A LITERAL-ZERO
// `ContentHash{uint64_t{0}}` IN CONSTEXPR CONTEXT MUST FIRE THE
// PREDICATE CONTRACT.
//
// `ValidContentHash` is `safety::Refined<safety::non_zero,
// ContentHash>` (declared in MerkleDag.h alongside ValidMerkleRoot
// per PROD-WRAP-6 / #535).  ContentHash{0} is the structural
// sentinel for "this region was never folded by
// compute_content_hash" — empty regions legitimately carry it
// (make_region with num_ops==0), but it is never a valid
// cache-publish / federation / merkle-fold key.  Without the gate,
// an adversarial / buggy caller passing ContentHash{0} would:
//   (1) corrupt KernelCache::publish_l*/lookup — the `(content_hash,
//       device_capability)` slot folds with zero on one side, so
//       two different unbuilt regions alias to the same cache slot
//       (wrong-kernel dispatch on a hash collision);
//   (2) propagate as a no-op replay key into Cipher
//       ContentAddressed* federation round-trip (same defect mode
//       as ValidMerkleRoot at the federation boundary);
//   (3) XOR-fold into compute_merkle_hash as a no-op (LoopNode
//       body_content_hash mixing at line 880) — parent merkle hash
//       becomes structurally indistinguishable from "loop has empty
//       body", a construction bug that make_loop's non-empty-body
//       precondition would otherwise have caught.
//
// Companion fixture: neg_content_hash_default_construct.cpp
//   * That one is the wide miss (= default-constructed
//     ContentHash{}, where zero is reached via the strong-id NSDMI
//     rather than an explicit literal).
//   * This one is the explicit-literal mismatch (= ContentHash{0}).
//     Catches drift where a caller silently passes a zero-init
//     value from a corrupt-source path (e.g. a TraceGraph whose
//     content_hash field has not yet been populated by the
//     streaming fold).
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.  Mirror of the
// ValidMerkleRoot fixture pair (neg_merkle_root_zero_literal +
// neg_merkle_root_default_construct).

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`non_zero(v)`) to be exercised at compile time.
    // ContentHash{uint64_t{0}}.raw() == 0 → non_zero(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidContentHash bad{
        crucible::ContentHash{uint64_t{0}}};
    (void)bad;
    return 0;
}
