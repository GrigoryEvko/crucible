// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidLayout with int8_t{99} in constexpr
// context.  Layout is a DENSE enum (Strided=0 .. SparseBsc=5); 99 is far
// ABOVE the valid range.
//
// Per the read_meta layout gate (last enum reconstruction in read_meta,
// sibling of #534 ndim / #892 kernel_id / dtype / device_type),
// ValidLayout is safety::Refined<valid_layout, int8_t>.  read_meta() in
// Serialize.h previously reconstructed `m.layout = r.r<Layout>()`
// unguarded; layout feeds the node content hash (Merkle identity), so a
// corrupt/version-skewed Cipher byte silently corrupted it.  The gate
// fail-closes instead.
//
// Companion fixture: neg_tensor_meta_layout_below_min.cpp
//   * This one is the ABOVE-MAX wide miss (99 > 5).  Catches "drop the
//     predicate entirely" regression where ValidLayout degrades into a
//     transparent wrapper.
//   * That one is the BELOW-MIN case (-1).  Catches the signed-lower-bound
//     bug where `bounded_above<5>` would wrongly accept negatives.
//
// In constant evaluation a contract violation makes the expression
// non-constant per P1494R5 — using it where a constant is required is
// ill-formed.  Per HS14, >=2 fixtures per soundness gate, distinct class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidLayout bad{static_cast<std::int8_t>(99)};
    (void)bad;
    return 0;
}
