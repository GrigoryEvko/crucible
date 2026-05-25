// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidScalarType with int8_t{14} in constexpr
// context.  14 is an INTERIOR GAP in ScalarType's sparse enumerator set
// (Bool = 11, BFloat16 = 15 — values 12..14 are not enumerators).
//
// Per the read_meta dtype gate (sibling of #534 ndim / #892 kernel_id),
// ValidScalarType is
// safety::Refined<valid_scalar_type, int8_t>.  read_meta() in Serialize.h
// previously reconstructed `m.dtype = r.r<ScalarType>()` unguarded, so a
// corrupt/version-skewed Cipher byte in a gap slipped through to
// element_size(), whose `default: std::unreachable()` makes any non-
// enumerator value UNDEFINED BEHAVIOUR in the size-math path.
//
// Companion fixture: neg_tensor_meta_dtype_above_max.cpp
//   * This one is the INTERIOR GAP (14).  Catches drift where the
//     predicate is relaxed to a plain range check `[-1, 26]` that would
//     wrongly admit the 12..14, 16..22 gaps.
//   * That one is the ABOVE-MAX wide miss (99 > 26).  Catches "drop the
//     predicate entirely" regression.
//
// In constant evaluation a contract violation makes the expression
// non-constant per P1494R5 — using it where a constant is required is
// ill-formed.  Per HS14, >=2 fixtures per soundness gate, distinct class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidScalarType bad{static_cast<std::int8_t>(14)};
    (void)bad;
    return 0;
}
