// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidCipherHead from a DEFAULT-constructed
// ContentHash{} (which is zero via the strong-id NSDMI default) in
// constexpr context — the wide-miss / NSDMI-default mismatch fixture
// for the Cipher::advance_head non-zero gate.
//
// Per #884 WRAP-Cipher-1, ValidCipherHead is
// safety::Refined<safety::non_zero, ContentHash>.  ContentHash{}
// (default-construct) is what every uninitialized / never-written
// Cipher state holds — the NSDMI default in CRUCIBLE_STRONG_HASH(Name)
// pins value_ = 0.  Without the gate, an adversarial / buggy caller
// committing the default ContentHash{} would (1) hit the same downstream
// corruption as the explicit-literal-zero case, but via a subtly
// different route — the value is never explicitly written, just
// FORWARDED unmodified from a fresh struct.
//
// Companion fixture: neg_cipher_head_zero_literal.cpp
//   * That one is the explicit-literal mismatch (= ContentHash{0}).
//   * This one is the NSDMI-default wide miss (= ContentHash{}).
//     Catches drift where a caller forgets to initialize a local
//     ContentHash variable and forwards it directly to advance_head
//     (whose old signature took bare ContentHash and silently
//     accepted the default).  The two failure modes look identical
//     at the byte level (raw == 0) but originate from different
//     source-level mistakes; rejecting both makes the gate watertight.
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
    // ContentHash{}.raw() == 0 (via CRUCIBLE_STRONG_HASH NSDMI) →
    // non_zero(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ContentHash empty{};
    constexpr crucible::ValidCipherHead bad{empty};
    (void)bad;
    return 0;
}
