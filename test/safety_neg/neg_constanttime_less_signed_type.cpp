// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating `crucible::safety::ct::less<T>` with a
// signed integer type (int32_t).  The function template's constraint
// is `std::unsigned_integral<T>`; signed types fail the concept and
// GCC rejects at constraint-checking time before the body is
// instantiated.
//
// Discipline rationale (ConstantTime.h):
//   The constant-time `less` primitive uses bitwise / arithmetic ops
//   that depend on the modular two's-complement subtraction shape of
//   FIXED-WIDTH UNSIGNED types.  Signed types would invoke implementation
//   -defined behavior on `~a & b` patterns once the int-promoted result
//   sign-extends; the BoringSSL-form formula in less() is derived
//   only for the unsigned T-width case.  The concept gate IS the
//   discipline.
//
// HS14 — paired with neg_constanttime_eq_length_mismatch for distinct
// mismatch classes:
//   * Class U (THIS file): concept-gate rejection at constraint-checking
//   * Class M (sibling):   consteval CRUCIBLE_PRE fire in eq's body
// Together the pair pins both soundness layers of ct::* primitives:
//   (a) type-domain (unsigned-only) gate; and
//   (b) length-equality invariant on byte-span comparison.
//
// U-141 — first neg-compile pair for ct::* (closes the ConstantTime
// slice of backlog #146 A8-P2 alongside U-140's Machine coverage).

#include <crucible/safety/ConstantTime.h>

#include <cstdint>

// Anchor a legitimate call so the file is self-contained — uint32_t
// satisfies std::unsigned_integral.  This call compiles.
[[maybe_unused]] static constexpr std::uint32_t anchor_less_unsigned() {
    return ::crucible::safety::ct::less<std::uint32_t>(1u, 2u);
}

// VIOLATION: int32_t is signed.  The requires-clause
// `std::unsigned_integral<T>` is unsatisfied.  GCC rejects the
// template instantiation with "constraints not satisfied" naming the
// `unsigned_integral` concept.
[[maybe_unused]] static constexpr auto offending_less_signed() {
    return ::crucible::safety::ct::less<std::int32_t>(
        std::int32_t{1}, std::int32_t{2});   // ERROR: signed type
}

int main() { return 0; }
