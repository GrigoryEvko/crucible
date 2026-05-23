// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PROD-WRAP-6 #535, mismatch class #2 of 2:
// CONSTRUCTING `ValidContentHash` WITH A DEFAULT-CONSTRUCTED
// `ContentHash{}` IN CONSTEXPR CONTEXT MUST FIRE THE PREDICATE
// CONTRACT.
//
// Companion to neg_content_hash_zero_literal.cpp.  The explicit-
// literal fixture there (= `ContentHash{uint64_t{0}}`) catches
// caller-typed-zero defects.  THIS fixture catches the more common
// production defect mode: forgot-to-populate.  When a caller takes
// the address of a freshly-built RegionNode / TraceGraph and reads
// `content_hash` before the streaming fold has run, the field
// holds its strong-id NSDMI default (== 0) — the same end value as
// `ContentHash{0}`, but reached through a different code path
// (silent zero-leak via uninit-default rather than explicit
// literal).
//
// Pinning both mismatch classes forces every future refactor to
// preserve:
//   * strict-equality semantics of non_zero (no relaxation to
//     `>= 0` or similar — which for uint64_t still admits zero);
//   * the strong-id NSDMI default's status as a "must NEVER reach
//     a Refined<non_zero, T>" sentinel value (i.e. a Refined<>
//     ctor witnesses the user's commitment that the value was
//     produced by an act, not by default).
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.  Mirror of the
// ValidMerkleRoot fixture pair.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`non_zero(v)`) to be exercised at compile time.
    // ContentHash{}.raw() == 0 via the strong-id NSDMI default →
    // non_zero(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidContentHash bad{
        crucible::ContentHash{}};
    (void)bad;
    return 0;
}
