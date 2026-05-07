// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidCipherHead with a literal-zero
// ContentHash{0} in constexpr context — the explicit-zero mismatch
// fixture for the Cipher::advance_head non-zero gate.
//
// Per #884 WRAP-Cipher-1, ValidCipherHead is
// safety::Refined<safety::non_zero, ContentHash>.  ContentHash{0} is
// the structural sentinel for "no commit yet" — advancing HEAD to it
// would corrupt the log invariant (a step with hash=0 is
// indistinguishable from "before first commit"), break
// hash_at_step()'s binary search, and lose the federation round-trip
// key for that step.  Without the gate, an adversarial / buggy caller
// committing ContentHash{0} would (1) pass through advance_head
// silently, (2) write a HEAD file with all-zero hex, (3) append a
// "step_id,0,ts" log entry that violates the binary-search precondition
// (every entry's hash is supposed to be a content-addressed object
// resident in $root/objects/00/000…0 — and that object never exists).
//
// Companion fixture: neg_cipher_head_default_construct.cpp
//   * That one is the wide miss (= default-constructed ContentHash{},
//     where zero is reached via NSDMI rather than explicit literal).
//   * This one is the explicit-literal mismatch (= ContentHash{0}).
//     Catches drift where a caller silently passes a zero-init value
//     from a corrupt-source path (e.g. an early-return store() that
//     yields ContentHash{} and the caller forwards it without check).
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/Cipher.h>
#include <crucible/Types.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`non_zero(v)`) to be exercised at compile time.
    // ContentHash{uint64_t{0}}.raw() == 0 → non_zero(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidCipherHead bad{
        crucible::ContentHash{uint64_t{0}}};
    (void)bad;
    return 0;
}
