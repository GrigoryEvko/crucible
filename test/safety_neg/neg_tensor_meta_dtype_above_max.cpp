// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidScalarType with int8_t{99} in constexpr
// context.  99 is far above ScalarType's highest enumerator
// (Float8_e4m3fnuz = 26), an out-of-range byte.
//
// Per the read_meta dtype gate (sibling of #534 ndim / #892 kernel_id),
// ValidScalarType is
// safety::Refined<valid_scalar_type, int8_t>.  read_meta() in Serialize.h
// previously reconstructed `m.dtype = r.r<ScalarType>()` unguarded, so a
// corrupt/version-skewed Cipher byte slipped through to element_size(),
// whose `default: std::unreachable()` makes any non-enumerator value
// UNDEFINED BEHAVIOUR in the size-math path.
//
// Companion fixture: neg_tensor_meta_dtype_gap_value.cpp
//   * This one is the ABOVE-MAX wide miss (99 > 26).  Catches "drop the
//     predicate entirely" regression where ValidScalarType degrades into
//     a transparent wrapper and any byte enters as a dtype.
//   * That one is the INTERIOR GAP (14).  Catches drift to a plain range
//     check that wrongly admits the sparse gaps.
//
// In constant evaluation a contract violation makes the expression
// non-constant per P1494R5 — using it where a constant is required is
// ill-formed.  Per HS14, >=2 fixtures per soundness gate, distinct class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidScalarType bad{static_cast<std::int8_t>(99)};
    (void)bad;
    return 0;
}
